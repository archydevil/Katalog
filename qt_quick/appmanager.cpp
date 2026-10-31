#include "appmanager.h"
#include "core/catalog.h"
#include "core/language.h"
#include "core/database.h"
#include <QDir>
#include "core/databasemanager.h"
#include "core/device.h"
#include "core/catalogjobstoppable.h"
#include "core/filechecksum.h"
#include "core/filemetadata.h"
#include "core/searchjobstoppable.h"
#include "core/statusbarmessagebuilder.h"
#include "core/catalog.h"
#include "core/backupjobstoppable.h"
#include "core/backupprofilegenerator.h"
#include "core/directoryreplicator.h"
#include "core/storage.h"
#include "core/foldertreeloader.h"
#include "core/mdiprofile.h"
#include "core/mdidata.h"
#include "core/mdienrichmentjob.h"
#include "core/mdisystemmonitor.h"
#include "core/mdifts.h"
#include "core/mdicontext.h"
#include "core/mdiknowledge.h"
#include "core/mdivolumes.h"
#include "core/mdipeople.h"
#include "core/mdifaces.h"
#include "core/mditranscriptfilter.h"
#include "core/mdiproposals.h"
#include "core/mdidating.h"
#include "core/mdiwebsearch.h"
#include "core/mdidocument.h"
#include "core/mdihtmlexport.h"
#include "core/mdidiskspace.h"
#include "core/mdiarchivecopy.h"
#include "core/collectionaccess.h"
#include <QElapsedTimer>
#include <QThread>
#include <QThread>
#include <QJsonDocument>
#include <QJsonObject>
#include "version.h"
#include <QGuiApplication>
#include <QScreen>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QTimer>
#include <QVersionNumber>
#include <QSysInfo>
#include <QLocale>
#include <kcoreaddons_version.h>

namespace {

// How long the start of a scan is held back so the activity panel reaches the
// screen first (OPQ-F11). The scan runs on the GUI thread — CatalogJobStoppable
// pumps the event loop by hand — so once it starts, nothing is painted until it
// yields. A zero-delay timer is not enough: it can fire before the window's
// pending update request is turned into a swapped frame, which leaves the panel
// logically visible but never drawn. 50 ms is about three frames at 60 Hz, and
// is imperceptible at the start of an operation that is about to do real work.
constexpr int kStartDeferralMs = 50;

// Render a stored search phrase or exclude string for a single-line display.
// Terms are stored joined with '\n'; each is quoted separately and the quoted
// terms are joined with ", ", so nothing but punctuation is added and the
// result never contains a newline. See SpecSearchList.md (SRL-F11, SRL-C9).
QString quoteSearchTerms(const QString &stored)
{
    const QStringList terms = stored.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QStringList quoted;
    quoted.reserve(terms.size());
    for (const QString &term : terms) {
        const QString trimmed = term.trimmed();
        if (!trimmed.isEmpty())
            quoted << QLatin1Char('"') + trimmed + QLatin1Char('"');
    }
    return quoted.join(QLatin1String(", "));
}

} // namespace

AppManager::AppManager(QObject *parent) : QObject(parent)
{
    // Created before the QML engine loads, so this is still the OS font.
    m_systemFont = QGuiApplication::font();
    m_importSourceDeviceModel = new DeviceListModel(this);
    m_importSourceDeviceModel->setIncludeCollectionRoot(true);
    // Similar faces and face groups kept so far are computed again (fork)
    connect(this, &AppManager::facesChanged,  this, &AppManager::invalidateFaceCache);
    connect(this, &AppManager::peopleChanged, this, &AppManager::invalidateFaceCache);
}
//----------------------------------------------------------------------
AppManager::~AppManager()
{
    // The Home count worker calls back into this object when it ends
    waitForHomeCount();
}
//----------------------------------------------------------------------
void AppManager::initiateApp()
{
    currentVersion  = KATALOG_VERSION_STRING;
    releaseDate     = KATALOG_RELEASE_DATE;

    //Define Setting file path and name: pre-release builds keep their own file,
    //portable file beside the executable first (shared with the command line)
    collection->settingsFilePath = Collection::resolveSettingsFilePath(currentVersion);
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);

    // Read database mode and path so collection->folder can be derived correctly.
    // Database::initialize() reads these same keys again inside startDatabase(),
    // but we need folder set before any core code uses it.
    collection->databaseMode     = settings.value("Settings/databaseMode").toString();
    collection->databaseFilePath = settings.value("Settings/DatabaseFilePath").toString();
    if (collection->databaseMode.isEmpty())
        collection->databaseMode = "File";

    if (collection->databaseMode == "Memory") {
        collection->folder = settings.value("LastCollectionFolder").toString();
    } else if (collection->databaseMode == "File") {
        if (!collection->databaseFilePath.isEmpty())
            collection->folder = QFileInfo(collection->databaseFilePath).absolutePath();
    }

    m_firstRun = !QFile(collection->settingsFilePath).exists()
              || (collection->databaseMode == "Memory" && collection->folder.isEmpty())
              || (collection->databaseMode == "File"   && collection->databaseFilePath.isEmpty());

    selectedDevice->ID = settings.value("Selection/SelectedDeviceID", 1).toInt();
    if (selectedDevice->ID == 0) selectedDevice->type = "All";
    selectedDevice->loadDevice(m_connectionName);

    //Check for new version
    checkVersionChoice = settings.value("Settings/CheckVersion", true).toBool();
    if ( checkVersionChoice == true)
        checkVersion();

    m_fileSortCaseSensitive = settings.value("Settings/FileCaseSensitiveSort", false).toBool();

    //qDebug()<<"initiated App.";
}
//----------------------------------------------------------------------
void AppManager::setSearchObject(SearchSync *search)
{
    searchObject = search;
    search->setDatabaseConnection(m_connectionName);
    connect(search, &Search::searchProgress, this, &AppManager::onSearchProgress);

    m_searchSortModel = new FilesView(this);
    m_searchSortModel->setSourceModel(search);

    m_exploreFilesModel = new ExploreFilesModel(this);
    m_exploreSortModel  = new FilesView(this);
    m_exploreSortModel->setSourceModel(m_exploreFilesModel);

    m_backupPreviewModel = new BackupPreviewModel(this);

    // Devices page Table display mode. The proxy sorts on the model's SortRole,
    // which carries the still-typed value, so counts and sizes sort numerically
    // (SpecDevicesPage DVP-F2).
    m_deviceTableModel     = new DeviceTableModel(this);
    m_deviceTableSortModel = new QSortFilterProxyModel(this);
    m_deviceTableSortModel->setSourceModel(m_deviceTableModel);
    m_deviceTableSortModel->setSortRole(DeviceTableModel::SortRole);
    m_deviceTableSortModel->setSortCaseSensitivity(Qt::CaseInsensitive);

    m_searchSortModel->setCaseSensitive(m_fileSortCaseSensitive);
    m_exploreSortModel->setCaseSensitive(m_fileSortCaseSensitive);

    // Restore saved sort state so data is sorted correctly when it first arrives
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    int searchCol   = settings.value("Search/lastSearchSortSection", -1).toInt();
    int searchOrder = settings.value("Search/lastSearchSortOrder",    0).toInt();
    if (searchCol >= 0)
        m_searchSortModel->sort(searchCol, searchOrder);
    int exploreCol   = settings.value("Explore/lastExploreSortSection", -1).toInt();
    int exploreOrder = settings.value("Explore/lastExploreSortOrder",    0).toInt();
    // An index naming a column Explore never shows (e.g. 4, the folders-first
    // key of beta1/beta2) falls back to the default order (SpecExplore EXP-C15).
    static const QList<int> exploreNeverShown = {3, 4, 5, 7, 13, 14, 15};
    if (exploreCol >= 0 && exploreCol <= 20 && !exploreNeverShown.contains(exploreCol))
        m_exploreSortModel->sort(exploreCol, exploreOrder);
}
//----------------------------------------------------------------------
void AppManager::onSearchProgress(int filesProcessed)
{
    if (!searchObject) return;

    StatusBarMessageBuilder builder;
    builder.setOperation(tr("Search"));

    if (filesProcessed == -1) {
        // Search stopped/cancelled
        builder.setStatus(tr("Stopped"));
        if (!searchObject->currentCatalogName.isEmpty())
            builder.setDeviceContext(searchObject->currentCatalogIndex, searchObject->totalCatalogs, searchObject->currentCatalogName);
        if (searchObject->totalFilesProcessed > 0)
            builder.setProcess(tr("Evaluated"), searchObject->totalFilesProcessed);
        if (searchObject->fileNames.size() > 0)
            builder.setResult((searchObject->searchOnFolderCriteria && searchObject->showFoldersOnly) ? tr("Folders found") : tr("Files found"), searchObject->fileNames.size());
    } else if (filesProcessed == -4) {
        // Loading progress tick (Memory mode CSV loading)
        builder.setStatus(tr("In Progress"));
        SearchJobStoppable *sjs = dynamic_cast<SearchJobStoppable*>(searchObject);
        if (sjs) {
            if (searchObject->totalCatalogs > 0)
                builder.setDeviceContext(searchObject->currentCatalogIndex, searchObject->totalCatalogs, searchObject->currentCatalogName);
            builder.setProcess(sjs->currentOperationVerb, sjs->currentCatalogFilesLoaded, sjs->currentCatalogTotalFiles);
        }
    } else if (filesProcessed < 0) {
        // -2 (loading started) and -3 (loading finished)
        builder.setStatus(tr("In Progress"));
        if (searchObject->totalCatalogs > 0)
            builder.setDeviceContext(searchObject->currentCatalogIndex, searchObject->totalCatalogs, searchObject->currentCatalogName);
        if (searchObject->fileNames.size() > 0)
            builder.setResult((searchObject->searchOnFolderCriteria && searchObject->showFoldersOnly) ? tr("Folders found") : tr("Files found"), searchObject->fileNames.size());
    } else {
        // Regular progress (filesProcessed >= 0)
        builder.setStatus(tr("In Progress"));
        if (searchObject->searchInCatalogsChecked && searchObject->totalCatalogs > 0)
            builder.setDeviceContext(searchObject->currentCatalogIndex, searchObject->totalCatalogs, searchObject->currentCatalogName);
        builder.setProcess(tr("Evaluated"), filesProcessed, searchObject->estimatedTotalFiles);
        if (searchObject->fileNames.size() > 0)
            builder.setResult((searchObject->searchOnFolderCriteria && searchObject->showFoldersOnly) ? tr("Folders found") : tr("Files found"), searchObject->fileNames.size());
    }

    m_searchStatusText = builder.build();
    emit searchStatusTextChanged();
    QCoreApplication::processEvents();
}
//----------------------------------------------------------------------
void AppManager::stopSearch()
{
    if (searchObject) searchObject->stopSearch();
}
//----------------------------------------------------------------------
void AppManager::pauseSearch()
{
    if (searchObject) {
        searchObject->pauseSearch();
        m_searchIsPaused = true;
        emit searchStateChanged();
    }
}
//----------------------------------------------------------------------
void AppManager::resumeSearch()
{
    if (searchObject) {
        searchObject->resumeSearch();
        m_searchIsPaused = false;
        emit searchStateChanged();
    }
}
//----------------------------------------------------------------------
void AppManager::executeSearch()
{
    if (!searchObject) {
        qWarning() << "AppManager::executeSearch: Search object not set";
        return;
    }

    if (!selectedDevice) {
        qWarning() << "AppManager::executeSearch: No device selected";
        return;
    }

    // A catalog update or creation is a blocking call on this same thread that
    // keeps the UI alive by pumping the event loop, so a search can be requested
    // while one runs. Running it here would nest inside that job's call stack and
    // share its database connection. The UI also refuses this (Main.qml
    // runSearch), but the guard belongs where the damage would be done.
    if (m_deviceUpdateIsRunning || m_catalogIsCreating || m_exploreLoadIsRunning
        || (m_deviceUpdateManager && m_deviceUpdateManager->operationRunning())) {
        qWarning() << "AppManager::executeSearch: refused, a catalog operation is running";
        return;
    }

    // Memory mode requires CSV loading during search
    searchObject->setMemoryModeEnabled(collection->databaseMode == "Memory");
    // Instant-search prefilter (fork, D5): SQLite collections only
    searchObject->setMdiDataPath(collection->databaseMode == QLatin1String("Hosted") ? QString() : mdiDataPath());

    // Capture device path at search time so Results page shows the right device
    searchObject->devicePath = Device::getDevicePath(selectedDevice->ID, m_connectionName);

    // Record the device the search is about to run against. K2 does this in
    // saveSearchCriteria (mainwindow_tab_search_pr.cpp:288-314); K3 never did, so
    // the field kept whatever a previous history restore had left in it and every
    // entry was saved against the same stale device (SpecSelection.md SEL-F1).
    // 0 is the "All devices" convention, as in K2.
    searchObject->selectedDeviceIDList.clear();
    if (searchObject->searchInCatalogsChecked)
        searchObject->selectedDeviceIDList.append(
            (selectedDevice->type == QLatin1String("All")) ? 0 : selectedDevice->ID);
    else
        searchObject->selectedDeviceIDList.append(0);

    // Save criteria to history table before search (mirrors SearchManager::startSearchJobStoppable);
    // a read-only collection keeps no history of its searches (D43)
    if (!getReadOnly())
        searchObject->saveSearchHistoryToTable(m_connectionName);

    m_searchIsRunning = true;
    m_searchStatusText = StatusBarMessageBuilder().setOperation(tr("Search")).setStatus(tr("In Progress")).build();
    emit searchStateChanged();
    emit searchStatusTextChanged();

    // Execute search (synchronous — onSearchProgress() fires via direct connection during this call)
    searchObject->searchFiles(selectedDevice);
    qInfo() << "Search: instant-search index used for" << searchObject->ftsPrefilteredCatalogs
            << "catalog(s)";

    m_searchIsRunning = false;
    m_searchIsPaused  = false;
    emit searchStateChanged();

    // Build completion message
    {
        StatusBarMessageBuilder builder;
        builder.setOperation(tr("Search")).setStatus(tr("Completed"));
        if (searchObject->totalCatalogs > 0 && !searchObject->currentCatalogName.isEmpty())
            builder.setDeviceContext(searchObject->currentCatalogIndex, searchObject->totalCatalogs, searchObject->currentCatalogName);
        if (searchObject->totalFilesProcessed > 0)
            builder.setProcess(tr("Evaluated"), searchObject->totalFilesProcessed);
        QString resultTitle = (searchObject->searchOnFolderCriteria && searchObject->showFoldersOnly) ? tr("Folders found") : tr("Files found");
        builder.setResult(resultTitle, searchObject->fileNames.size());
        m_searchStatusText = builder.build();
        // Deliberately not cleared on a timer here. The activity panel owns how
        // long a completed message stays up, under its own single constant; a
        // second five-second timer would race it on the same message (OPQ-C14).
        emit searchStatusTextChanged();
    }

    // New results: drop any filter left from the previous search, or it would
    // silently hide rows of this one (SRF-F7), and republish the catalog list
    // the footer offers.
    if (m_searchSortModel)
        m_searchSortModel->clearFilters();
    emit searchFiltersChanged();
    emit searchCatalogsFoundChanged();

    // Persist history to CSV file (no-op for File/Hosted modes — guarded inside the method)
    if (!getReadOnly())
        collection->saveSearchHistoryTableToFile();

}
//----------------------------------------------------------------------
QString AppManager::getSearchNameFilter() const
{
    return m_searchSortModel ? m_searchSortModel->nameFilter() : QString();
}
//----------------------------------------------------------------------
void AppManager::setSearchNameFilter(const QString &text)
{
    if (!m_searchSortModel || m_searchSortModel->nameFilter() == text)
        return;
    m_searchSortModel->setNameFilter(text);
    emit searchFiltersChanged();
}
//----------------------------------------------------------------------
QVariantList AppManager::getSearchCatalogFilters() const
{
    QVariantList result;
    if (!m_searchSortModel)
        return result;
    // The ticked catalogs: every listed one the user has not unticked (SRF-F5).
    const QList<int> hidden = m_searchSortModel->hiddenCatalogs();
    const QVariantList found = getSearchCatalogsFound();
    for (const QVariant &entry : found) {
        const int id = entry.toMap().value(QStringLiteral("catalogId")).toInt();
        if (!hidden.contains(id))
            result.append(id);
    }
    return result;
}
//----------------------------------------------------------------------
void AppManager::toggleSearchCatalogFilter(int catalogId)
{
    if (!m_searchSortModel)
        return;
    m_searchSortModel->toggleCatalogHidden(catalogId);
    emit searchFiltersChanged();
}
//----------------------------------------------------------------------
// Ticks every catalog: the default state, and the starting point for unticking
// a few - "everything except these" (SRF-F11).
void AppManager::selectAllSearchCatalogFilters()
{
    if (!m_searchSortModel)
        return;
    m_searchSortModel->setHiddenCatalogs({});
    emit searchFiltersChanged();
}
//----------------------------------------------------------------------
// Unticks every catalog, so no row is shown until one is ticked again - the
// starting point for "only these" (SRF-F11).
void AppManager::clearSearchCatalogFilters()
{
    if (!m_searchSortModel)
        return;
    QList<int> all;
    const QVariantList found = getSearchCatalogsFound();
    for (const QVariant &entry : found)
        all.append(entry.toMap().value(QStringLiteral("catalogId")).toInt());
    m_searchSortModel->setHiddenCatalogs(all);
    emit searchFiltersChanged();
}
//----------------------------------------------------------------------
// The catalogs the current results actually come from, each once, in the order
// they first appear. Read straight off the results already in memory - K2 builds
// the same list in Search::processResults (core/search.cpp:475-508), but this
// needs no core change and no query (SRF-C2).
QVariantList AppManager::getSearchCatalogsFound() const
{
    QVariantList result;
    if (!searchObject)
        return result;

    QSet<int> seen;
    for (int i = 0; i < searchObject->fileCatalogIDs.size() && i < searchObject->fileCatalogs.size(); ++i) {
        const int id = searchObject->fileCatalogIDs.at(i);
        if (seen.contains(id))
            continue;
        seen.insert(id);
        QVariantMap entry;
        entry[QStringLiteral("catalogId")] = id;
        entry[QStringLiteral("name")]      = searchObject->fileCatalogs.at(i);
        result.append(entry);
    }
    return result;
}
//----------------------------------------------------------------------
// Also add a method to get current database info for display:
QString AppManager::getCurrentDatabaseInfo() const
{
    QFileInfo dbInfo(collection->databaseFilePath);
    if (dbInfo.exists()) {
        return QString("Database: %1 (%2)")
        .arg(dbInfo.fileName())
            .arg(QLocale().formattedDataSize(dbInfo.size()));
    } else {
        return "Database: Not found";
    }
}
//----------------------------------------------------------------------
void AppManager::checkVersion()
{
    // Port of K2's MainWindow::checkVersion() (SpecVersionCheck.md). Skipped on
    // first run (VCK-F6) and inside Flatpak, whose updates come from Flathub (VCK-F5).
    if (m_firstRun)
        return;
    if (qEnvironmentVariableIsSet("FLATPAK_ID") || QFile::exists(QStringLiteral("/.flatpak-info")))
        return;

    // GitHub redirects this address to .../releases/tag/v<latest version>.
    // Asynchronous: startup and first paint never wait for the reply (VCK-C1).
    QNetworkRequest request(QUrl(QStringLiteral("https://github.com/StephaneCouturier/Katalog/releases/latest")));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *manager = new QNetworkAccessManager(this);
    QNetworkReply *reply = manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, manager, reply]() {
        reply->deleteLater();
        manager->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;   // silent, as in K2 (VCK-F4)

        // Read the version from the address the redirect ended on.
        const QString tagMarker = QStringLiteral("/releases/tag/v");
        const QString finalUrl = reply->url().toString();
        const int pos = finalUrl.indexOf(tagMarker);
        if (pos < 0)
            return;
        const QString latestVersion = finalUrl.mid(pos + tagMarker.size());
        const QVersionNumber latest = QVersionNumber::fromString(latestVersion);
        const QVersionNumber current = QVersionNumber::fromString(currentVersion);
        if (latest.isNull())
            return;

        // A pre-release is older than the final release with the same number (VCK-F3).
        const bool currentIsPreRelease = currentVersion.contains(QLatin1String("beta"), Qt::CaseInsensitive)
                                      || currentVersion.contains(QLatin1String("alpha"), Qt::CaseInsensitive);
        const bool newer = latest > current || (latest == current && currentIsPreRelease);
        if (!newer)
            return;

        emit newVersionAvailable(currentVersion, latestVersion,
                                 QStringLiteral("https://github.com/StephaneCouturier/Katalog/releases/tag/v") + latestVersion);
    });
}
//----------------------------------------------------------------------
//Database management --------------------------------------------------
QString AppManager::startDatabase()
{
    const QString conn = m_connectionName;

    // Who may change it, before anything is opened (D43)
    prepareCollectionAccess();
    QSqlError err = DatabaseManager::connect(conn, collection);
    if (err.type() != QSqlError::NoError) {
        qWarning() << "startDatabase failed:" << err.text();
        return "startDatabase: " + err.text();
    }
    confirmCollectionAccess();

    // Migrations, or nothing written when read-only (D43)
    openCollectionDatabase(conn);

    if (!getReadOnly()) {
        collection->insertPhysicalStorageGroup();
        // Catalogs a split left without a date (fixed since): dated once, so the
        // instant-search index stops rebuilding them at every launch
        collection->repairUndatedCatalogs();
    }
    initializeDeviceListModel();
    selectedDevice->loadDevice(conn);
    emit databaseModeChanged();
    emit recentCollectionsChanged();
    qInfo() << "Katalog" << currentVersion << "– database ready:" << collection->databaseMode;
    // Pending enrichment jobs (MDI-F12), then the instant-search index for
    // catalogs not indexed yet (fork, D5), once the window is up; the
    // background pass is stopped before the application ends
    QTimer::singleShot(2500, this, &AppManager::restoreMdiJobs);
    QTimer::singleShot(3000, this, &AppManager::scheduleFtsIndexing);
    connect(qApp, &QCoreApplication::aboutToQuit, this, &AppManager::stopFtsIndexing, Qt::UniqueConnection);
    connect(qApp, &QCoreApplication::aboutToQuit, this, &AppManager::waitForHomeCount, Qt::UniqueConnection);
    return "startDatabase: No Error";
}
//----------------------------------------------------------------------
void AppManager::initializeDeviceListModel()
{
    if (deviceListModel) {
        delete deviceListModel;
    }
    // Recompute each device's active state from the filesystem (drive mounted?) before
    // loading the list — the stored device_active is stale when opening a collection
    // (it reflects connectivity at the time it was last saved, possibly on another machine).
    collection->updateAllDeviceActive();
    // Seed the mount table so the activation that follows startup does not treat
    // it as changed and check network mounts for nothing.
    m_lastMountSignature = collection->mountSignature();
    deviceListModel = new DeviceListModel(this);
    deviceListModel->loadFromConnection(m_connectionName);
    m_deviceExpandLevel = -1; // reset to fully expanded on new connection

    if (!m_deviceFilterModel) {
        m_deviceFilterModel = new QSortFilterProxyModel(this);
        m_deviceFilterModel->setFilterRole(DeviceListModel::NameRole);
        m_deviceFilterModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    }
    m_deviceFilterModel->setSourceModel(deviceListModel);

    //qDebug() << "DeviceListModel initialized with" << deviceListModel->rowCount() << "devices";
}
//----------------------------------------------------------------------
void AppManager::openDeviceFolder(int deviceId)
{
    Device device;
    device.ID = deviceId;
    device.loadDevice(m_connectionName);
    if (!device.path.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(device.path));
}
//----------------------------------------------------------------------
void AppManager::expandDevice(int deviceId)
{
    if (deviceListModel)
        deviceListModel->expandDevice(deviceId);
}
//----------------------------------------------------------------------
void AppManager::collapseDevice(int deviceId)
{
    if (deviceListModel)
        deviceListModel->collapseDevice(deviceId);
}
//----------------------------------------------------------------------
void AppManager::setDeviceFilter(const QString &text)
{
    if (m_deviceFilterModel)
        m_deviceFilterModel->setFilterFixedString(text);
}
//----------------------------------------------------------------------
void AppManager::expandDevices()
{
    if (!deviceListModel || !canExpandDevices()) return;

    if (m_deviceExpandLevel == -1) return;

    int maxDepth = Device::getMaxHierarchyDepth(m_connectionName);
    m_deviceExpandLevel++;
    if (m_deviceExpandLevel >= maxDepth)
        m_deviceExpandLevel = -1; // show all once past max

    deviceListModel->clearCollapsedUpToLevel(m_deviceExpandLevel);
    deviceListModel->setMaxLevel(m_deviceExpandLevel);
    emit deviceExpandLevelChanged();
}
//----------------------------------------------------------------------
void AppManager::collapseDevices()
{
    if (!deviceListModel || !canCollapseDevices()) return;

    if (m_deviceExpandLevel == -1) {
        int maxDepth = Device::getMaxHierarchyDepth(m_connectionName);
        m_deviceExpandLevel = qMax(0, maxDepth - 1);
    } else {
        m_deviceExpandLevel--;
    }

    deviceListModel->setMaxLevel(m_deviceExpandLevel);
    emit deviceExpandLevelChanged();
}
//----------------------------------------------------------------------
bool AppManager::getShowDeviceInfo() const
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    return settings.value("Selection/ShowDeviceInfo", true).toBool();
}
//----------------------------------------------------------------------
void AppManager::setShowDeviceInfo(bool value)
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("Selection/ShowDeviceInfo", value);
    settings.sync();
    emit showDeviceInfoChanged();
}
//----------------------------------------------------------------------
bool AppManager::getRefreshDeviceStatusOnActivation() const
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    // Opt-in: nobody acquires a background filesystem probe they did not ask for.
    return settings.value("Settings/RefreshDeviceStatusOnActivation", false).toBool();
}
//----------------------------------------------------------------------
void AppManager::setRefreshDeviceStatusOnActivation(bool value)
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("Settings/RefreshDeviceStatusOnActivation", value);
    settings.sync();
    emit refreshDeviceStatusOnActivationChanged();
}
//----------------------------------------------------------------------
bool AppManager::getAllowFileDeletion() const
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    // Off unless asked for: a user who never opens Settings cannot delete a file
    // from a context menu by accident. This gates availability only — the
    // confirmation dialogs still stand behind it. Stored application-wide, like
    // every other Application setting: settingsFilePath is a Collection member
    // but points at one file for the whole app, so the choice is made once.
    return settings.value("Settings/AllowFileDeletion", false).toBool();
}
//----------------------------------------------------------------------
void AppManager::setAllowFileDeletion(bool value)
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("Settings/AllowFileDeletion", value);
    settings.sync();
    emit allowFileDeletionChanged();
}
//----------------------------------------------------------------------
int AppManager::getThemeId() const
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    // Same key, same numbering and the same fallback as K2
    // (mainwindow_setup.cpp:370-374), so a user who already chose a theme there
    // keeps it here without touching Settings again. Default is Katalog Colors.
    // 2 is K3-only: K2 knows 0 and 1, and coerces anything else back to 1.
    int themeId = settings.value("Settings/Theme", 1).toInt();
    if (themeId < 0 || themeId > 2)
        themeId = 1;
    return themeId;
}
//----------------------------------------------------------------------
void AppManager::setThemeId(int value)
{
    const int themeId = (value >= 0 && value <= 2) ? value : 1;
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("Settings/Theme", themeId);
    settings.sync();
    emit themeIdChanged();
}
//----------------------------------------------------------------------
bool AppManager::getBiggerIconSize() const
{
    // K2 stores the checkbox's Qt check state - 2 for checked, 0 for unchecked -
    // and reads it back with toBool(), which is why writing a plain bool here
    // round-trips through K2 unharmed (THM-C8).
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    return settings.value("Settings/ThemeBiggerIconSize", false).toBool();
}
//----------------------------------------------------------------------
void AppManager::setBiggerIconSize(bool value)
{
    if (getBiggerIconSize() == value)
        return;
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("Settings/ThemeBiggerIconSize", value);
    settings.sync();
    emit biggerIconSizeChanged();
}
//----------------------------------------------------------------------
void AppManager::refreshDeviceActiveOnActivation()
{
    // Back from sleep: the collection lock may have been taken over (D43)
    refreshCollectionLock();
    if (!getRefreshDeviceStatusOnActivation())
        return;

    // Every time the window becomes active, re-check the folders of devices on
    // local mounts: a catalogued folder can be renamed or deleted without the
    // mount table changing at all, and that is the common case. The check is one
    // QDir::exists() per device and runs with no delay, so returning to Katalog
    // shows the truth immediately (DAS-F10).
    // Devices on network mounts are checked only when the mount table changed
    // (DAS-F5), because QDir::exists() on an unreachable share blocks this
    // thread. Network devices are a separate use case, not covered here.
    const bool mountsChanged = (collection->mountSignature() != m_lastMountSignature);
    const bool statusChanged = collection->updateAllDeviceActive(!mountsChanged);
    m_lastMountSignature = collection->mountSignature();

    // Rebuild the list only when something actually changed, so an ordinary
    // window switch does not throw away the user's scroll position (DAS-O4).
    if (statusChanged)
        reloadDeviceListModel();
}
//----------------------------------------------------------------------
bool AppManager::getShowSelectionPage() const
{
    // Mirrors K2's Selection/Filters panel show/hide (mainwindow_tab_filters.cpp).
    // K2 stores "go-next" when hidden and "go-previous" when shown; default = shown.
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    return settings.value("Settings/ShowHideFilters", "go-previous").toString() != "go-next";
}
//----------------------------------------------------------------------
void AppManager::setShowSelectionPage(bool value)
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("Settings/ShowHideFilters", value ? "go-previous" : "go-next");
    settings.sync();
    emit showSelectionPageChanged();
}
//----------------------------------------------------------------------
bool AppManager::getDeviceFilterFromSelection() const
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    // On for a collection that has never stored a choice (SpecDevicesPage.md
    // DVP-F9). Applied here, as the fallback of the read, and never seeded by
    // writing the key: an untouched collection must stay distinguishable from
    // one the user deliberately set to off (DVP-C8).
    return settings.value("Devices/FilterFromSelection", true).toBool();
}
//----------------------------------------------------------------------
void AppManager::setDeviceFilterFromSelection(bool value)
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("Devices/FilterFromSelection", value);
    settings.sync();
    emit deviceFilterFromSelectionChanged();
}
//----------------------------------------------------------------------
bool AppManager::getDeviceDisplayAsTable() const
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    // Cards is what a collection with no stored value opens in (DVP-C1).
    return settings.value("Devices/DisplayAsTable", false).toBool();
}
//----------------------------------------------------------------------
void AppManager::setDeviceDisplayAsTable(bool value)
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("Devices/DisplayAsTable", value);
    settings.sync();
    emit deviceDisplayAsTableChanged();
}
//----------------------------------------------------------------------
bool AppManager::getDeviceDisplayFullTable() const
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    // K2 writes this same key from its own Full Table checkbox, as an int; a
    // collection therefore carries one shared value between the two versions
    // (DVP-F4).
    return settings.value("Devices/DisplayFullDeviceTable", false).toBool();
}
//----------------------------------------------------------------------
void AppManager::setDeviceDisplayFullTable(bool value)
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("Devices/DisplayFullDeviceTable", value);
    settings.sync();
    if (m_deviceTableModel)
        m_deviceTableModel->setFullTable(value);
    emit deviceDisplayFullTableChanged();
}
//----------------------------------------------------------------------
QString AppManager::getDeviceDisplayContents() const
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    // K2 stores "Tree" where K3's view filter says "All"; the two other values
    // match by name (mainwindow_tab_device_ui.cpp:45,55,65). Translating at this
    // boundary keeps the shared key readable by K2 (DVP-F7).
    const QString stored = settings.value("Devices/DisplayContents", "Tree").toString();
    if (stored == QLatin1String("Storage") || stored == QLatin1String("Catalogs"))
        return stored;
    // Absent or unrecognised: the Device tree. A deliberate divergence from K2,
    // which falls back to its Catalogs list - see DVP-F8 for why that fallback
    // is an artefact rather than a requirement.
    return QStringLiteral("All");
}
//----------------------------------------------------------------------
void AppManager::setDeviceDisplayContents(const QString &value)
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("Devices/DisplayContents",
                      value == QLatin1String("All") ? QStringLiteral("Tree") : value);
    settings.sync();
    emit deviceDisplayContentsChanged();
}
//----------------------------------------------------------------------
void AppManager::populateDeviceTable(const QString &viewFilter, const QVariantList &rows)
{
    if (!m_deviceTableModel) return;
    m_deviceTableModel->setView(viewFilter);
    m_deviceTableModel->setMemoryMode(collection->databaseMode == QLatin1String("Memory"));
    m_deviceTableModel->setFullTable(getDeviceDisplayFullTable());
    m_deviceTableModel->populate(rows);
}
//----------------------------------------------------------------------
int AppManager::deviceTableColumnWidth(int column) const
{
    return m_deviceTableModel ? m_deviceTableModel->columnWidth(column) : 100;
}
//----------------------------------------------------------------------
int AppManager::deviceTableColumnOfKey(const QString &key) const
{
    return m_deviceTableModel ? m_deviceTableModel->visibleColumnOfKey(key) : -1;
}
//----------------------------------------------------------------------
void AppManager::sortDeviceTable(int column, int order)
{
    if (!m_deviceTableSortModel) return;
    // The tree sorts inside the model, among each parent's own children. The
    // proxy is left unsorted for it: sorting the flattened rows would lift
    // devices out from under their parents (DVP-F14).
    if (m_deviceTableModel && m_deviceTableModel->isTreeView()) {
        m_deviceTableSortModel->sort(-1);
        m_deviceTableModel->sortTree(column, static_cast<Qt::SortOrder>(order));
        return;
    }
    m_deviceTableSortModel->sort(column, static_cast<Qt::SortOrder>(order));
}
//----------------------------------------------------------------------
void AppManager::toggleDeviceTableRow(int row)
{
    // Expanding or collapsing only re-derives the visible rows from the ones
    // already loaded - no reload, and so no active-status probe (DVP-C11).
    if (m_deviceTableModel)
        m_deviceTableModel->toggleExpanded(row);
}
//----------------------------------------------------------------------
// Tree state of a row for keyboard Left/Right (CDT-F10). Row indices are the
// model's own: in the tree view the proxy is left unsorted (see sortDeviceTable).
QVariantMap AppManager::deviceTableRowInfo(int row) const
{
    return m_deviceTableModel ? m_deviceTableModel->treeRowInfo(row) : QVariantMap();
}
//----------------------------------------------------------------------
bool AppManager::getSearchKeepsSelection() const
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    return settings.value("Search/KeepsSelection", true).toBool();
}
//----------------------------------------------------------------------
void AppManager::setSearchKeepsSelection(bool value)
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("Search/KeepsSelection", value);
    settings.sync();
    emit searchKeepsSelectionChanged();
}
//----------------------------------------------------------------------
bool AppManager::getCheckVersionChoice() const
{
    return checkVersionChoice;
}
//----------------------------------------------------------------------
void AppManager::setCheckVersionChoice(bool value)
{
    if (checkVersionChoice == value)
        return;
    checkVersionChoice = value;
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("Settings/CheckVersion", value);
    emit checkVersionChoiceChanged();
}
//----------------------------------------------------------------------
bool AppManager::getFileSortCaseSensitive() const
{
    return m_fileSortCaseSensitive;
}
//----------------------------------------------------------------------
void AppManager::setFileSortCaseSensitive(bool value)
{
    if (m_fileSortCaseSensitive == value)
        return;
    m_fileSortCaseSensitive = value;
    if (m_searchSortModel)  m_searchSortModel->setCaseSensitive(value);
    if (m_exploreSortModel) m_exploreSortModel->setCaseSensitive(value);
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("Settings/FileCaseSensitiveSort", value);
    emit fileSortCaseSensitiveChanged();
}
//----------------------------------------------------------------------
void AppManager::openSettingsFile()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(collection->settingsFilePath));
}
//----------------------------------------------------------------------
QString AppManager::getSettingsFilePath() const
{
    return collection->settingsFilePath;
}
//----------------------------------------------------------------------
QString AppManager::getDatabaseSchemaVersion()
{
    return collection->loadDatabaseSchemaVersion();
}
//----------------------------------------------------------------------
void AppManager::setLastPage(const QString &pageName)
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("Settings/lastPage", pageName);
}
//----------------------------------------------------------------------
QString AppManager::getLastPage() const
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    return settings.value("Settings/lastPage", "Search").toString();
}
//----------------------------------------------------------------------
// Window geometry
//----------------------------------------------------------------------
// Stored in the settings file AppManager::initiateApp resolved — beside the
// executable for a portable build, otherwise in ~/.config — and not in the
// platform-native store the QtCore Settings QML type uses. A portable build
// must not leave window state behind in the user's home or the registry, and
// that store's identity varies per platform.
//
// The position is saved and asked for on every platform, with nothing here
// testing which one is running. A platform that cannot place a window where the
// application asks — Wayland today, where the compositor decides and Qt drops
// the coordinates — simply ignores the request, and the window opens wherever
// that platform chose. Nothing has to detect this, and nothing has to be
// changed here if it ever stops being true.
//----------------------------------------------------------------------
QVariantMap AppManager::getWindowGeometry() const
{
    const int defaultWidth  = kDefaultWindowWidth;
    const int defaultHeight = kDefaultWindowHeight;

    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    int width  = settings.value("Settings/WindowWidth",  defaultWidth).toInt();
    int height = settings.value("Settings/WindowHeight", defaultHeight).toInt();

    if (width  <= 0) width  = defaultWidth;
    if (height <= 0) height = defaultHeight;

    QVariantMap geometry;
    geometry["maximized"]   = settings.value("Settings/WindowMaximized", false).toBool();
    geometry["hasPosition"] = false;

    const bool hasStoredPosition = settings.contains("Settings/WindowX")
                                   && settings.contains("Settings/WindowY");
    const int x = settings.value("Settings/WindowX").toInt();
    const int y = settings.value("Settings/WindowY").toInt();

    //The stored position is reused only if the window would still be reachable:
    //the middle of its title bar has to land on a screen that exists now, so a
    //window saved on a second monitor does not reopen off-screen once that
    //monitor is gone. Without a position, the platform places the window itself.
    const QScreen *targetScreen = nullptr;
    if (hasStoredPosition) {
        const QPoint titleBarCentre(x + width / 2, y + 10);
        const QList<QScreen *> screens = QGuiApplication::screens();
        for (const QScreen *screen : screens) {
            if (screen->availableGeometry().contains(titleBarCentre)) {
                targetScreen = screen;
                break;
            }
        }
    }

    //A size stored on a larger monitor must not survive that monitor being
    //unplugged — but it is measured against the screen the window is actually
    //reopening on, not always the primary one, so a window kept on a wide
    //secondary display keeps its width.
    if (targetScreen == nullptr)
        targetScreen = QGuiApplication::primaryScreen();
    if (targetScreen != nullptr) {
        const QRect available = targetScreen->availableGeometry();
        width  = qMin(width,  available.width());
        height = qMin(height, available.height());
    }

    geometry["width"]  = width;
    geometry["height"] = height;

    if (hasStoredPosition && targetScreen != nullptr && targetScreen->availableGeometry().contains(QPoint(x + width / 2, y + 10))) {
        geometry["x"]           = x;
        geometry["y"]           = y;
        geometry["hasPosition"] = true;
    }

    return geometry;
}
//----------------------------------------------------------------------
void AppManager::setTextScale(qreal scale)
{
    // App-wide text size (TYP-F7): scaling the application font reaches every
    // control, including those the style or Kirigami draw with the application
    // font (combo boxes, toolbar buttons, drawer entries, headings).
    QFont font = m_systemFont;
    if (m_systemFont.pointSizeF() > 0)
        font.setPointSizeF(m_systemFont.pointSizeF() * scale);
    else
        font.setPixelSize(qRound(m_systemFont.pixelSize() * scale));
    QGuiApplication::setFont(font);
    emit textPointSizeChanged();
}
//----------------------------------------------------------------------
// Default size for the Reset window size button (WIN-F4), clamped to the
// available area of the screen the window is on. The screen is found by name:
// a Wayland window does not know its own position.
QVariantMap AppManager::getDefaultWindowSize(const QString &screenName) const
{
    int width  = kDefaultWindowWidth;
    int height = kDefaultWindowHeight;

    const QScreen *targetScreen = nullptr;
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (const QScreen *screen : screens) {
        if (screen->name() == screenName) {
            targetScreen = screen;
            break;
        }
    }
    if (targetScreen == nullptr)
        targetScreen = QGuiApplication::primaryScreen();
    if (targetScreen != nullptr) {
        const QRect available = targetScreen->availableGeometry();
        width  = qMin(width,  available.width());
        height = qMin(height, available.height());
    }

    QVariantMap size;
    size["width"]  = width;
    size["height"] = height;
    return size;
}
//----------------------------------------------------------------------
void AppManager::saveWindowGeometry(int x, int y, int width, int height, bool maximized)
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);

    if (width  > 0) settings.setValue("Settings/WindowWidth",  width);
    if (height > 0) settings.setValue("Settings/WindowHeight", height);
    settings.setValue("Settings/WindowMaximized", maximized);
    settings.setValue("Settings/WindowX", x);
    settings.setValue("Settings/WindowY", y);
}
//----------------------------------------------------------------------
bool AppManager::canExpandDevices() const
{
    return m_deviceExpandLevel != -1; // -1 means already showing all
}
//----------------------------------------------------------------------
bool AppManager::canCollapseDevices() const
{
    return m_deviceExpandLevel != 0; // 0 means only top-level visible
}
//----------------------------------------------------------------------
void AppManager::selectSQLiteDatabase()
{
    qDebug() << "AppManager::selectSQLiteDatabase() called";

    // Get current directory or use a default
    QString currentDir = QFileInfo(collection->databaseFilePath).absolutePath();
    if (currentDir.isEmpty() || !QDir(currentDir).exists()) {
        QStringList standardsPaths = QStandardPaths::standardLocations(QStandardPaths::HomeLocation);
        currentDir = standardsPaths.isEmpty() ? "/" : standardsPaths[0];
    }

    // This will be handled by QML FileDialog, so we just emit a signal
    // The actual file selection will happen in QML
    emit databasePathChanged(collection->databaseFilePath);
}
//----------------------------------------------------------------------
QString AppManager::getDatabaseFilePath() const
{
    return collection->databaseFilePath;
}
//----------------------------------------------------------------------
void AppManager::openDatabaseFile()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(collection->databaseFilePath));
}
//----------------------------------------------------------------------
void AppManager::setDatabaseFilePath(const QString &path)
{
    if (path.isEmpty()) {
        qWarning() << "AppManager::setDatabaseFilePath: Empty path provided";
        emit databaseConnectionChanged(false, tr("Empty database path provided"));
        return;
    }

    //qDebug() << "AppManager::setDatabaseFilePath:" << path;

    // Check if file exists
    QFile dbFile(path);
    if (!dbFile.exists()) {
        qWarning() << "AppManager::setDatabaseFilePath: Database file does not exist:" << path;
        emit databaseConnectionChanged(false, tr("Database file does not exist: %1").arg(path));
        return;
    }

    // Store old settings for rollback if needed
    QString oldPath = collection->databaseFilePath;
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    QString oldMode = settings.value("Settings/databaseMode").toString();
    QString oldFilePath = settings.value("Settings/DatabaseFilePath").toString();

    // Save new settings before reconnecting so Database::initialize() reads them
    settings.setValue("Settings/databaseMode",    "File");
    settings.setValue("Settings/DatabaseFilePath", path);
    settings.sync();

    if (reconnectToDatabase()) {
        qDebug() << "Successfully connected to new database:" << path;
        saveToRecentCollections("File", path, QFileInfo(path).fileName());
        emit databaseConnectionChanged(true, tr("Opened: %1").arg(QFileInfo(path).fileName()));
    } else {
        // Rollback settings and connection
        settings.setValue("Settings/databaseMode",    oldMode);
        settings.setValue("Settings/DatabaseFilePath", oldFilePath);
        settings.sync();
        collection->databaseFilePath = oldPath;
        reconnectToDatabase();
        emit databaseConnectionChanged(false, tr("Failed to open database: %1").arg(lastDatabaseError));
    }
}
//----------------------------------------------------------------------
void AppManager::createNewSQLiteCollection(const QString &path)
{
    if (path.isEmpty()) return;

    // Create the containing folder first: the file dialog returns a path without
    // creating anything, so a folder the user typed rather than picked does not
    // exist yet and the file below could not be written.
    QDir().mkpath(QFileInfo(path).absolutePath());

    // Create an empty file — SQLite will write the schema on first connection.
    QFile dbFile(path);
    if (!dbFile.open(QFile::WriteOnly)) {
        emit databaseConnectionChanged(false, tr("Could not create file: %1").arg(path));
        return;
    }
    dbFile.close();

    // Connect, run migrations (creates schema), add to recents, refresh UI.
    setDatabaseFilePath(path);

    // Seed the three default devices K2 always creates for a new collection.
    // insertPhysicalStorageGroup() is idempotent — it checks for device_id=1 first.
    bool defaultsCreated = collection->insertPhysicalStorageGroup();
    if (defaultsCreated) {
        // Translate the hard-coded English names K2 inserted into the user's locale.
        const QString conn = m_connectionName;
        auto renameDevice = [&](int id, const QString &newName) {
            Device dev;
            dev.setConnectionName(conn);
            dev.ID = id;
            dev.loadDevice(conn);
            dev.name = newName;
            dev.saveDevice();
        };
        renameDevice(1, tr(" Physical Group"));
        renameDevice(2, tr("Virtual device"));

        // The collection is new: the device insertPhysicalStorageGroup() just
        // named "Local disk" is the only one with that name.
        Device localDisk;
        localDisk.setConnectionName(conn);
        localDisk.name = QStringLiteral("Local disk");
        localDisk.getIDFromDeviceName();
        if (localDisk.ID > 0)
            renameDevice(localDisk.ID, tr("Local disk"));

        refreshAllUI();
    }
}
//----------------------------------------------------------------------
QString AppManager::getCollectionBrowsePath() const
{
    // File mode   → folder containing the .db file
    if (collection->databaseMode == "File" && !collection->databaseFilePath.isEmpty())
        return QFileInfo(collection->databaseFilePath).absolutePath();
    // Memory mode → the collection folder
    if (collection->databaseMode == "Memory" && !collection->folder.isEmpty())
        return collection->folder;
    // Hosted mode (or anything unresolved) → user home folder
    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
}
//----------------------------------------------------------------------
QString AppManager::getNewCollectionDefaultPath() const
{
    return getCollectionBrowsePath() + "/katalog.db";
}
//----------------------------------------------------------------------
void AppManager::refreshAllUI()
{
    qDebug() << "AppManager::refreshAllUI() - Starting comprehensive UI refresh";

    // Re-validate the selected device against the (possibly new) connection FIRST,
    // before refreshDeviceList() emits deviceListRefreshed. Pickers that react to
    // that signal (e.g. the Create page Storage picker, whose default is derived
    // from selectedDevice via getDefaultStorageId()) must read a device that
    // actually exists in the current collection — otherwise they resolve against
    // the previous collection's selection and never recompute.
    if (selectedDevice && selectedDevice->ID > 0) {
        selectedDevice->loadDevice(m_connectionName);

        // If device no longer exists, reset to default
        if (selectedDevice->name.isEmpty()) {
            selectedDevice->ID = 1; // Physical Group
            selectedDevice->loadDevice(m_connectionName);
            qDebug() << "Previous selected device not found, reset to Physical Group";
        }
    }

    // Refresh device list model (emits deviceListRefreshed → pickers re-resolve)
    refreshDeviceList();

    // Clear and refresh search results if search object exists
    refreshSearchResults();

    // Refresh statistics if needed
    refreshStatistics();

    // selectedDevice may have been reloaded/reset above without any other notice;
    // notify so device-dependent UI (Storage picker, selection highlight, the
    // Statistics device reminder, …) reflects the current collection's device.
    emit selectedDeviceChanged(selectedDevice ? selectedDevice->ID : 0);
    emit tagsChanged();
    emit uiRefreshCompleted();
    qDebug() << "AppManager::refreshAllUI() - Completed";
}

void AppManager::refreshDeviceList()
{
    if (deviceListModel) {
        qDebug() << "Refreshing device list model";
        // Recompute device active states from the filesystem before reloading the list,
        // so the Selection page reflects what is actually mounted now (not stale values
        // carried over when switching/opening a collection). Mirrors K2's device refresh.
        collection->updateAllDeviceActive();
        // Record the mount table as seen by this check. Without it the retained
        // signature stays stale and the next activation checks network mounts
        // for nothing (DAS-O4).
        m_lastMountSignature = collection->mountSignature();
        reloadDeviceListModel();
    }
}
//----------------------------------------------------------------------
void AppManager::reloadDeviceListModel()
{
    if (!deviceListModel)
        return;
    deviceListModel->refreshData();
    emit deviceListRefreshed();
    emit deviceListModelChanged();
    emit deviceListChanged();
}

void AppManager::refreshSearchResults()
{
    if (searchObject) {
        qDebug() << "Clearing search results";
        searchObject->resetSearchResults();
        emit searchResultsRefreshed();
    }
}

void AppManager::refreshStatistics()
{
    // Add any statistics refresh logic here if needed
    qDebug() << "Statistics refresh (placeholder)";
    emit statisticsRefreshed();
}

bool AppManager::reconnectToDatabase()
{
    const QString conn = m_connectionName;

    // The running job belongs to the collection being left: it ends as on
    // quit and stays pending there, with its waiting list (MDI-F93). The list
    // is already saved there: the caller may have pointed the collection at
    // the new folder, so nothing is written now.
    if (m_mdiJob) {
        m_mdiKeepPending = true;
        m_mdiJob->requestStop();
    }
    m_mdiHeld.clear();
    stopKnowledgeIndexing();   // its documents stay pending in the left collection
    // Home counts belong to the collection being left; a count still running
    // reports to a generation that is no longer current and is dropped
    m_homeGeneration++;
    m_homeQueue.clear();
    m_homeCounting.clear();
    m_homeTypeCache.clear();
    m_homeAiCache.clear();
    m_mdiCatalogRevision.clear();
    m_historyUpdateEntry = 0;   // a restore point of the collection being left
    m_mdiStep.clear();
    m_mdiStepLog.clear();
    m_mdiStatusText.clear();
    emit mdiStatusTextChanged();
    emit mdiStepChanged();
    emit mdiStepLogChanged();
    emit mdiStateChanged();

    // Pending wizard profiles belong to the collection being left
    m_pendingEnrichmentProfiles.clear();
    // The index being built belongs to it too
    stopFtsIndexing();
    m_quickHashWaiting.clear();
    m_mdiWaiting.clear();          // waiting enrichments name devices of that collection
    m_mdiIndexCatalogId = -1;      // previews are re-read from the new collection folder

    // Who may change the collection opened now: its lock, its writers (D43)
    prepareCollectionAccess();
    QSqlError err = DatabaseManager::reconnect(conn, collection);
    if (err.type() != QSqlError::NoError) {
        lastDatabaseError = err.text();
        qWarning() << "AppManager::reconnectToDatabase failed:" << lastDatabaseError;
        m_collectionLock->release();   // the caller may go back to the previous one
        return false;
    }
    confirmCollectionAccess();

    // Migrations, or nothing written when read-only (D43)
    openCollectionDatabase(conn);

    if (collection->databaseMode == "File" && !collection->databaseFilePath.isEmpty())
        collection->folder = QFileInfo(collection->databaseFilePath).absolutePath();

    lastDatabaseError.clear();
    collection->setConnectionName(conn);
    collection->loadImageFolderPath();
    if (!getReadOnly()) {
        collection->insertPhysicalStorageGroup();
        collection->repairUndatedCatalogs();   // see startDatabase()
    }

    // Opening a different collection: reset the selection to the default root
    // (Physical Group, guaranteed to exist by insertPhysicalStorageGroup above).
    // Device IDs are not stable across collections, so keeping the previous
    // selection would silently point at a different/nonexistent device — and the
    // Create page's Storage picker would derive a wrong or invalid parent from it.
    // refreshAllUI() below reloads this device for the new connection and notifies.
    if (selectedDevice)
        selectedDevice->ID = 1;

    refreshAllUI();
    emit databaseModeChanged();
    // Notify of the collection change itself: the drawer header reads the current
    // collection name through this signal, and the rollback path above reconnects
    // without going through saveToRecentCollections().
    emit recentCollectionsChanged();
    // Its own pending enrichment jobs first, then the background passes (MDI-F92)
    QTimer::singleShot(800, this, &AppManager::restoreMdiJobs);
    QTimer::singleShot(1000, this, &AppManager::scheduleFtsIndexing);
    return true;
}

QString AppManager::getSelectedDeviceName() const
{
    if (selectedDevice && !selectedDevice->name.isEmpty()) {
        return selectedDevice->name;
    }
    return "No device selected";
}

int AppManager::getSelectedDeviceId() const
{
    if (selectedDevice) {
        return selectedDevice->ID;
    }
    return -1;
}
//----------------------------------------------------------------------
QString AppManager::getSelectedDeviceType() const
{
    if (selectedDevice)
        return selectedDevice->type;
    return QString();
}
//----------------------------------------------------------------------
bool AppManager::getSelectedDeviceIsActive() const
{
    // Drives the Catalog icon: an active catalog shows a different disc than an
    // inactive one, the same rule the Selection cards use.
    if (selectedDevice)
        return selectedDevice->active;
    return false;
}
//----------------------------------------------------------------------
QString AppManager::getSelectedDevicePath() const
{
    if (selectedDevice)
        return selectedDevice->path;
    return QString();
}

QString AppManager::getHostName() const
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    return settings.value("Settings/databaseHostName").toString();
}
//----------------------------------------------------------------------
QString AppManager::getDatabaseName() const
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    return settings.value("Settings/databaseName").toString();
}
//----------------------------------------------------------------------
QString AppManager::getPhpMyAdminUrl() const
{
    // Base URL the Hosted "Edit" button opens; the database name is appended by
    // the caller. Editable + persisted so each machine can adjust host/port/path
    // (e.g. macOS MAMP uses :8888) without code changes.
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    return settings.value("Settings/PhpMyAdminUrl",
               "http://localhost/phpMyAdmin/index.php?route=/database/structure&db=").toString();
}
//----------------------------------------------------------------------
void AppManager::setPhpMyAdminUrl(const QString &url)
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("Settings/PhpMyAdminUrl", url);
}
//----------------------------------------------------------------------
int AppManager::getDatabasePort() const
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    return settings.value("Settings/databasePort", 3306).toInt();
}
//----------------------------------------------------------------------
QString AppManager::getDatabaseUserName() const
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    return settings.value("Settings/databaseUserName").toString();
}
//----------------------------------------------------------------------
QString AppManager::getDatabasePassword() const
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    return settings.value("Settings/databasePassword").toString();
}
//----------------------------------------------------------------------
QVariantList AppManager::getLanguageList() const
{
    QVariantList result;
    const QStringList codes = Language::getSupportedLanguages();
    for (const QString &code : codes) {
        QVariantMap entry;
        entry["code"]        = code;
        entry["displayName"] = Language::getDisplayName(code);
        entry["flagPath"]    = Language::getFlagPath(code);
        result.append(entry);
    }
    return result;
}
//----------------------------------------------------------------------
QString AppManager::getCurrentLanguage() const
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    return settings.value("Settings/Language", "en_US").toString();
}
//----------------------------------------------------------------------
void AppManager::setLanguage(const QString &languageCode)
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("Settings/Language", languageCode);
    settings.sync();
    emit languageChanged(languageCode);
}
//----------------------------------------------------------------------
void AppManager::openFile(const QString &filePath)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
}
//----------------------------------------------------------------------
void AppManager::openFolder(const QString &folderPath)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath));
}
//----------------------------------------------------------------------
bool AppManager::probeDeviceActive(int deviceId)
{
    // No device behind this row, so there is nothing to probe and nothing to
    // refuse: a connected-directory search reads a live folder directly.
    if (deviceId <= 0)
        return true;

    // loadDevice() calls updateActiveState() internally — one QDir::exists() on
    // this one path, and an UPDATE only when the value actually changed
    // (DAS-C11). No other device is touched (DAS-F12).
    Device device;
    device.ID = deviceId;
    device.catalog->setConnectionName(m_connectionName);
    device.setConnectionName(m_connectionName);
    device.loadDevice(m_connectionName);
    return device.active;
}
//----------------------------------------------------------------------
bool AppManager::searchRowDeviceIsActive(int row)
{
    if (!m_searchSortModel || row < 0)
        return true;
    const int deviceId = searchRowDeviceId(row);
    if (deviceId < 0)
        return true;
    return probeDeviceActive(deviceId);
}
//----------------------------------------------------------------------
// The device a Search results row belongs to: 0 when it has none (a search run
// directly on a disk folder, or a catalog whose device no longer exists), -1
// when the row or the role cannot be resolved. Used by the active-status guard
// and by "Explore folder" (SpecSearchResults SRS-F7).
int AppManager::searchRowDeviceId(int row) const
{
    if (!m_searchSortModel || row < 0)
        return -1;

    // Resolve the role by name rather than by number, as selectionRowForDevice
    // does: Search::Roles is an implementation detail and inserting an
    // enumerator would silently shift any hardcoded Qt::UserRole + N.
    const QHash<int, QByteArray> roles = m_searchSortModel->roleNames();
    int deviceIdRole = -1;
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
        if (it.value() == QByteArrayLiteral("deviceId")) {
            deviceIdRole = it.key();
            break;
        }
    }
    if (deviceIdRole < 0)
        return -1;

    // Read through the proxy so the sorted row maps to the right result.
    const QModelIndex idx = m_searchSortModel->index(row, 0);
    if (!idx.isValid())
        return -1;

    return m_searchSortModel->data(idx, deviceIdRole).toInt();
}
//----------------------------------------------------------------------
bool AppManager::probeExploreDeviceActive()
{
    if (!exploreDevice || exploreDevice->ID <= 0)
        return true;

    // Already loaded, so re-probe in place rather than reloading the whole row.
    exploreDevice->updateActiveState(m_connectionName);
    return exploreDevice->active;
}
//----------------------------------------------------------------------
void AppManager::setBusyCursor(bool busy)
{
    if (busy == m_busyCursor)
        return;
    m_busyCursor = busy;
    if (busy)
        QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    else
        QGuiApplication::restoreOverrideCursor();
}
//----------------------------------------------------------------------
int AppManager::startDeferralMs() const
{
    return kStartDeferralMs;
}
//----------------------------------------------------------------------
bool AppManager::pathExists(const QString &path) const
{
    return QFileInfo::exists(path);
}
//----------------------------------------------------------------------
int AppManager::selectionRowForDevice(int deviceId) const
{
    if (!m_deviceFilterModel || deviceId <= 0)
        return -1;

    // Resolve the role by name rather than by number: DeviceListModel's enum is
    // an implementation detail and inserting an enumerator would silently shift
    // any hardcoded Qt::UserRole + N (SpecSelection.md SEL-F6).
    const QHash<int, QByteArray> roles = m_deviceFilterModel->roleNames();
    int deviceIdRole = -1;
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
        if (it.value() == QByteArrayLiteral("deviceId")) {
            deviceIdRole = it.key();
            break;
        }
    }
    if (deviceIdRole < 0)
        return -1;

    for (int row = 0, count = m_deviceFilterModel->rowCount(); row < count; ++row) {
        const QModelIndex idx = m_deviceFilterModel->index(row, 0);
        if (m_deviceFilterModel->data(idx, deviceIdRole).toInt() == deviceId)
            return row;
    }
    return -1;
}
//----------------------------------------------------------------------
void AppManager::copyToClipboard(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}
//----------------------------------------------------------------------
QString AppManager::systemInformation()
{
    // Assembled for pasting into a public bug report (SpecAbout.md).
    //
    // Every field is named and read one at a time. Nothing here iterates the
    // settings or dumps a config object, so a value added elsewhere in the
    // application can never find its way into this block by accident (ABT-C2).
    //
    // Deliberately absent (ABT-C1): machine hostname, user name, every
    // filesystem path, the collection name, and the hosted database's host,
    // port, name, user and password — the password is stored in plain text in
    // the .ini, and this text is written to the clipboard to be pasted in
    // public.
    //
    // Not translated, by intent: the block is read by a maintainer triaging a
    // report, so English field names keep every report legible whatever the
    // reporter's UI language. Only the button and the confirmation notification
    // that produce it are translated.
    QStringList lines;

    lines << QStringLiteral("Katalog %1 (%2)")
                 .arg(QLatin1String(KATALOG_VERSION_STRING),
                      QLatin1String(KATALOG_RELEASE_DATE));
    lines << QStringLiteral("Qt %1 (built against %2)")
                 .arg(QLatin1String(qVersion()), QLatin1String(QT_VERSION_STR));
    lines << QStringLiteral("KDE Frameworks %1")
                 .arg(QLatin1String(KCOREADDONS_VERSION_STRING));
    lines << QStringLiteral("OS: %1").arg(QSysInfo::prettyProductName());
    lines << QStringLiteral("Kernel: %1 %2")
                 .arg(QSysInfo::kernelType(), QSysInfo::kernelVersion());
    lines << QStringLiteral("Architecture: %1").arg(QSysInfo::currentCpuArchitecture());
    lines << QStringLiteral("Display server: %1").arg(QGuiApplication::platformName());
    // Read by name, never by iterating the environment (ABT-C2).
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    lines << QStringLiteral("Desktop environment: %1")
                 .arg(desktop.isEmpty() ? QStringLiteral("unknown") : desktop);
    lines << QStringLiteral("Language: %1").arg(QLocale().name());
    lines << QStringLiteral("Database mode: %1 (schema %2)")
                 .arg(getDatabaseMode(), getDatabaseSchemaVersion());

    return lines.join(QLatin1Char('\n'));
}
//----------------------------------------------------------------------
QString AppManager::exportSearchResultsToCSV()
{
    if (!searchObject || searchObject->fileNames.isEmpty())
        return QString();

    // The per-mode folder every CSV export uses, as K2 and Backup do (SRS-F9)
    const QString dir = getCollectionBrowsePath();
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    QString filePath  = dir + "/katalog_search_" + timestamp + ".csv";

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return QString();

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    // Header row
    out << "Name\tSize\tDate\tFolder\tCatalog\n";

    int count = searchObject->fileNames.size();
    for (int i = 0; i < count; ++i) {
        out << searchObject->fileNames.value(i)     << "\t"
            << searchObject->fileSizes.value(i)     << "\t"
            << searchObject->fileDateTimes.value(i) << "\t"
            << searchObject->filePaths.value(i)     << "\t"
            << searchObject->fileCatalogs.value(i)  << "\n";
    }

    if (!file.commit())
        return QString();

    return filePath;
}
//----------------------------------------------------------------------
QString AppManager::exportSearchResultsAsCatalog()
{
    if (getReadOnly()) return QString();   // read-only collection (D43)
    if (!searchObject || searchObject->fileNames.isEmpty())
        return QString();

    const QString conn = m_connectionName;
    const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss");
    const QString catalogName = tr("search_results") + "_" + timestamp;

    // Find or create the "Search Results" virtual parent device
    Device holder;
    holder.name = tr("Search Results");
    holder.getIDFromDeviceName();
    if (holder.ID <= 0) {
        holder.generateDeviceID();
        holder.type     = "Virtual";
        holder.groupID  = 1;
        holder.parentID = 0;
        holder.insertDevice();
    }

    // Create the new Device + Catalog
    Device *newDevice = new Device();
    newDevice->generateDeviceID();
    newDevice->type     = "Catalog";
    newDevice->name     = catalogName;
    newDevice->groupID  = 1;
    newDevice->parentID = holder.ID;
    newDevice->path     = "EXPORT";
    newDevice->totalFileCount = searchObject->filesFoundNumber;
    newDevice->totalFileSize  = searchObject->filesFoundTotalSize;

    newDevice->catalog->generateID();
    newDevice->externalID        = newDevice->catalog->ID;
    newDevice->catalog->name     = catalogName;
    newDevice->catalog->setDateUpdated(QDateTime());
    newDevice->catalog->setDateLoaded(QDateTime().addMSecs(100));
    newDevice->catalog->fileType = searchObject->searchOnType
                                   ? searchObject->selectedFileType : "All";
    newDevice->insertDevice();
    newDevice->catalog->sourcePath   = "EXPORT";
    newDevice->catalog->appVersion   = currentVersion;
    newDevice->catalog->insertCatalog();

    searchObject->saveResultsToCatalog(newDevice->catalog->ID, conn);

    // Refresh device list so the new catalog appears in Selection
    refreshDeviceList();

    delete newDevice;
    return catalogName;
}
//----------------------------------------------------------------------
int AppManager::batchMoveSearchResultsToTrash()
{
    if (!searchObject || searchObject->fileNames.isEmpty())
        return 0;
    int count = 0;
    int total = searchObject->fileNames.size();
    for (int i = 0; i < total; ++i) {
        QString path = searchObject->filePaths.value(i) + "/" + searchObject->fileNames.value(i);
        if (QFile::moveToTrash(path))
            ++count;
    }
    return count;
}
//----------------------------------------------------------------------
int AppManager::batchDeleteSearchResults()
{
    // The authoritative gate. The QML binding that greys the action out is only
    // its visual expression: a caller that forgot the binding would otherwise
    // delete files the user never permitted.
    if (!getAllowFileDeletion()) {
        qWarning() << "AppManager::batchDeleteSearchResults: refused, file deletion is not enabled";
        return 0;
    }
    if (!searchObject || searchObject->fileNames.isEmpty())
        return 0;
    int count = 0;
    int total = searchObject->fileNames.size();
    for (int i = 0; i < total; ++i) {
        QString path = searchObject->filePaths.value(i) + "/" + searchObject->fileNames.value(i);
        if (QFile::remove(path))
            ++count;
    }
    return count;
}
//----------------------------------------------------------------------
QVariantMap AppManager::batchVerifyChecksums()
{
    int total = 0, matched = 0, mismatched = 0, calculated = 0, errors = 0;

    if (searchObject) {
        const int n = searchObject->fileNames.size();
        for (int i = 0; i < n; ++i) {
            ++total;
            const QString &fileName  = searchObject->fileNames[i];
            const QString &folder    = searchObject->filePaths[i];
            const QString  filePath  = folder + "/" + fileName;
            const int      catalogId = searchObject->fileCatalogIDs.value(i, -1);
            const QString  stored    = searchObject->checksumSha256s.value(i);

            if (!QFileInfo::exists(filePath)) { ++errors; continue; }

            QString actual = FileChecksum::calculateChecksum(filePath, QCryptographicHash::Sha256);
            if (actual.isEmpty()) { ++errors; continue; }

            if (stored.isEmpty()) {
                if (catalogId > 0 && !getReadOnly())   // read-only: computed, not saved (D43)
                    FileChecksum::updateFileChecksum(m_connectionName,
                                                    catalogId, fileName, folder, actual, "SHA256");
                ++calculated;
            } else {
                actual == stored ? ++matched : ++mismatched;
            }
        }
    }

    QVariantMap result;
    result["total"]      = total;
    result["matched"]    = matched;
    result["mismatched"] = mismatched;
    result["calculated"] = calculated;
    result["errors"]     = errors;
    return result;
}
//----------------------------------------------------------------------
QVariantMap AppManager::batchGetMetadata()
{
    if (getReadOnly()) return QVariantMap{ { QStringLiteral("error"), readOnlyRefusal() }, { QStringLiteral("total"), 0 }, { QStringLiteral("updated"), 0 }, { QStringLiteral("skipped"), 0 }, { QStringLiteral("errors"), 0 } };   // read-only collection (D43)
    int total = 0, updated = 0, skipped = 0, errors = 0;

    if (searchObject) {
        const int n = searchObject->fileNames.size();
        for (int i = 0; i < n; ++i) {
            ++total;
            const QString &fileName  = searchObject->fileNames[i];
            const QString &folder    = searchObject->filePaths[i];
            const QString  filePath  = folder + "/" + fileName;
            const int      catalogId = searchObject->fileCatalogIDs.value(i, -1);

            if (!QFileInfo::exists(filePath)) { ++errors; continue; }
            if (catalogId <= 0)               { ++skipped; continue; }

            QVariantMap metadata = FileMetadata::extractMetadata(filePath, Catalog::METADATA_MEDIA_EXTENDED);
            if (metadata.isEmpty())           { ++skipped; continue; }

            if (FileMetadata::updateFileMetadata(m_connectionName,
                                                catalogId, fileName, folder, metadata))
                ++updated;
            else
                ++errors;
        }
    }

    QVariantMap result;
    result["total"]   = total;
    result["updated"] = updated;
    result["skipped"] = skipped;
    result["errors"]  = errors;
    return result;
}
//----------------------------------------------------------------------
bool AppManager::moveFileToTrash(const QString &fullPath)
{
    return QFile::moveToTrash(fullPath);
}
//----------------------------------------------------------------------
bool AppManager::deleteSingleFile(const QString &fullPath)
{
    // The authoritative gate — see batchDeleteSearchResults(). Both Explore and
    // Search Results reach permanent deletion through here.
    if (!getAllowFileDeletion()) {
        qWarning() << "AppManager::deleteSingleFile: refused, file deletion is not enabled";
        return false;
    }
    return QFile::remove(fullPath);
}
//----------------------------------------------------------------------
bool AppManager::catalogIncludesExtendedMetadata(int catalogId)
{
    Catalog catalog;
    catalog.ID = catalogId;
    catalog.setConnectionName(m_connectionName);
    catalog.loadCatalog();
    return catalog.includeMetadata.contains("Extended");
}
//----------------------------------------------------------------------
// Any metadata setting other than none shows the Explore metadata columns,
// as K2's Explore file list does (SpecExplore EXP-F17).
bool AppManager::catalogIncludesMetadata(int catalogId)
{
    Catalog catalog;
    catalog.ID = catalogId;
    catalog.setConnectionName(m_connectionName);
    catalog.loadCatalog();
    return catalog.includeMetadata != Catalog::METADATA_NONE;
}
//----------------------------------------------------------------------
QString AppManager::getFileMetadataJson(int catalogId, const QString &fileName, const QString &folderPath)
{
    return Catalog::getFileMetadataJson(catalogId, fileName, folderPath, m_connectionName);
}
//----------------------------------------------------------------------
QVariantList AppManager::getFileMetadataParsedFields(int catalogId, const QString &fileName, const QString &folderPath)
{
    QString json = Catalog::getFileMetadataJson(catalogId, fileName, folderPath, m_connectionName);
    if (json.isEmpty())
        return {};

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return {};

    const auto pairs = FileMetadata::parseExtendedMetadataFields(doc.object());
    QVariantList result;
    result.reserve(pairs.size());
    for (const auto &p : pairs) {
        QVariantMap row;
        row["label"] = p.first;
        row["value"] = p.second;
        result.append(row);
    }
    return result;
}
//----------------------------------------------------------------------
QString AppManager::calculateAndSaveChecksum(const QString &filePath, const QString &fileName, const QString &folderPath, int catalogId)
{
    if (!QFileInfo::exists(filePath))
        return QString();
    QString checksum = FileChecksum::calculateChecksum(filePath, QCryptographicHash::Sha256);
    if (!checksum.isEmpty() && catalogId > 0 && !getReadOnly())   // read-only: not saved (D43)
        FileChecksum::updateFileChecksum(m_connectionName, catalogId, fileName, folderPath, checksum, "SHA256");
    return checksum;
}
//----------------------------------------------------------------------
QString AppManager::verifyFileChecksum(const QString &filePath, const QString &expectedChecksum)
{
    auto result = FileChecksum::verifyChecksum(filePath, expectedChecksum, QCryptographicHash::Sha256);
    if (!result.success)
        return "error:" + result.errorMessage;
    return result.match ? "match" : "mismatch:" + result.actualChecksum;
}
//----------------------------------------------------------------------
QString AppManager::calculateChecksumMd5(const QString &filePath)
{
    // Calculated on demand only, never stored (SpecCatalogChecksum CKS-C1)
    if (!QFileInfo::exists(filePath))
        return QString();
    return FileChecksum::calculateChecksum(filePath, QCryptographicHash::Md5);
}
//----------------------------------------------------------------------
QVariantList AppManager::getSearchHistory() const
{
    QVariantList result;
    // One lookup per distinct device rather than per row: a long history is
    // usually a handful of devices searched repeatedly.
    QHash<int, QString> deviceNameById;
    const QList<QVariantMap> rows = collection->loadSearchHistory();
    for (const QVariantMap &row : rows) {
        QStringList parts;

        // ── File name ──────────────────────────────────────────────────────
        if (row.value(QStringLiteral("text_checked")).toBool()) {
            QString phrase = row.value(QStringLiteral("text_phrase")).toString();
            QString criteria = row.value(QStringLiteral("text_criteria")).toString();
            if (!phrase.isEmpty()) {
                // A multi-term phrase holds its terms joined with '\n'. The
                // delegate is a single line, so quote each term and join them
                // with ", " — a newline must never reach it. A single-term
                // phrase renders exactly as it always has.
                QString item = quoteSearchTerms(phrase);
                // "with" and "in" hold internal constants; rows saved before
                // those constants hold the label itself, shown as stored. The
                // labels are the search form's, translated in its context.
                QStringList options;
                if      (criteria == Search::TEXT_CRITERIA_EXACT_PHRASE) criteria = QCoreApplication::translate("PageSearchForm", "Exact Phrase");
                else if (criteria == Search::TEXT_CRITERIA_BEGINS_WITH)  criteria = QCoreApplication::translate("PageSearchForm", "Begins With");
                else if (criteria == Search::TEXT_CRITERIA_ANY_WORD)     criteria = QCoreApplication::translate("PageSearchForm", "Any Word");
                else if (criteria == Search::TEXT_CRITERIA_REGEX)        criteria = QCoreApplication::translate("PageSearchForm", "Regex");
                else if (criteria == Search::TEXT_CRITERIA_ALL_WORDS)    criteria.clear();
                if (!criteria.isEmpty() && criteria != QLatin1String("All Words"))
                    options << criteria;
                QString searchIn = row.value(QStringLiteral("text_search_in")).toString();
                if      (searchIn == Search::SEARCH_IN_FILE_NAMES)        searchIn = QCoreApplication::translate("PageSearchForm", "File names only");
                else if (searchIn == Search::SEARCH_IN_FILES_AND_FOLDERS) searchIn = QCoreApplication::translate("PageSearchForm", "File names or Folder paths");
                else if (searchIn == Search::SEARCH_IN_FOLDER_PATH)       searchIn = QCoreApplication::translate("PageSearchForm", "Folder path only");
                if (!searchIn.isEmpty())
                    options << searchIn;
                if (!options.isEmpty())
                    item += QLatin1String(" (") + options.join(QLatin1String(", ")) + QLatin1Char(')');
                parts << item;
            }
            if (row.value(QStringLiteral("case_sensitive")).toBool())
                parts << tr("Case sensitive");
            QString exclude = row.value(QStringLiteral("text_exclude")).toString();
            if (!exclude.isEmpty())
                parts << tr("Exclude: %1").arg(quoteSearchTerms(exclude));
        }

        // ── File criteria ──────────────────────────────────────────────────
        if (row.value(QStringLiteral("file_criteria_checked")).toBool()) {
            if (row.value(QStringLiteral("file_type_checked")).toBool())
                parts << tr("Type: %1").arg(row.value(QStringLiteral("file_type")).toString());
            if (row.value(QStringLiteral("file_size_checked")).toBool())
                parts << tr("Size: %1 %2 – %3 %4")
                          .arg(row.value(QStringLiteral("file_size_min")).toString(), row.value(QStringLiteral("file_size_min_unit")).toString(),
                               row.value(QStringLiteral("file_size_max")).toString(), row.value(QStringLiteral("file_size_max_unit")).toString());
            if (row.value(QStringLiteral("date_modified_checked")).toBool())
                parts << tr("Date: %1 – %2")
                          .arg(row.value(QStringLiteral("date_modified_min")).toString(), row.value(QStringLiteral("date_modified_max")).toString());
        }

        // ── Metadata ───────────────────────────────────────────────────────
        if (row.value(QStringLiteral("metadata_checked")).toBool())
            parts << tr("Metadata");

        // ── Duplicates ─────────────────────────────────────────────────────
        if (row.value(QStringLiteral("duplicates_checked")).toBool()) {
            QStringList dup;
            if (row.value(QStringLiteral("duplicates_name")).toBool()) dup << tr("Name");
            if (row.value(QStringLiteral("duplicates_size")).toBool()) dup << tr("Size");
            if (row.value(QStringLiteral("duplicates_date_modified")).toBool()) dup << tr("Date");
            if (row.value(QStringLiteral("duplicates_checksum")).toBool()) dup << tr("Checksum");
            parts << tr("Duplicates: %1").arg(dup.isEmpty() ? QStringLiteral("–") : dup.join(QLatin1String(", ")));
        }

        // ── Differences ────────────────────────────────────────────────────
        if (row.value(QStringLiteral("differences_checked")).toBool())
            parts << tr("Differences");

        // ── Folder criteria ────────────────────────────────────────────────
        if (row.value(QStringLiteral("folder_criteria_checked")).toBool()) {
            if (row.value(QStringLiteral("show_folders")).toBool()) parts << tr("Folders only");
            if (row.value(QStringLiteral("tag_checked")).toBool()) {
                QString tag = row.value(QStringLiteral("tag")).toString();
                parts << (tag.isEmpty() ? tr("Tag") : tr("Tag: %1").arg(tag));
            }
        }

        // Device the search was run against (SpecSelection.md SEL-F2). Only the
        // stored id comes from the history row; the name is resolved through the
        // ordinary Device accessor rather than by joining the device table here
        // (SEL-C5). Empty means "all devices" — QML supplies the wording, so no
        // new translatable string is introduced (SEL-C4).
        QString deviceName;
        const QString storedIds = row.value(QStringLiteral("selected_device_ID_list")).toString();
        const int scopeId = storedIds.section(QLatin1Char(','), 0, 0).toInt();
        if (scopeId > 0) {
            auto cached = deviceNameById.constFind(scopeId);
            if (cached != deviceNameById.constEnd()) {
                deviceName = cached.value();
            } else {
                Device dev;
                dev.setConnectionName(m_connectionName);
                dev.ID = scopeId;
                dev.loadDevice(m_connectionName);
                deviceName = dev.name;
                deviceNameById.insert(scopeId, deviceName);
            }
        }

        QVariantMap entry;
        entry["dateTime"]   = row.value(QStringLiteral("date_time")).toString();
        entry["deviceName"] = deviceName;
        entry["summary"]    = parts.join(QLatin1String("  |  "));
        result.append(entry);
    }
    return result;
}
//----------------------------------------------------------------------
QVariantMap AppManager::restoreSearchHistory(const QString &dateTime)
{
    if (!searchObject)
        return {};
    searchObject->searchDateTime = dateTime;
    searchObject->loadSearchHistoryCriteria(m_connectionName);
    return searchObject->properties();
}
//----------------------------------------------------------------------
void AppManager::clearSearchHistory()
{
    if (getReadOnly()) return;   // read-only collection (D43)
    collection->clearSearchHistory(m_connectionName);
}
//----------------------------------------------------------------------
void AppManager::keepLastSearchHistory(int count)
{
    if (getReadOnly()) return;   // read-only collection (D43)
    collection->keepLastSearchHistory(count, m_connectionName);
}
//----------------------------------------------------------------------
QStringList AppManager::getTagNames() const
{
    collection->loadTagFileToTable();
    Tag tag;
    tag.loadFromDatabase(m_connectionName);
    QStringList result;
    for (const QString &name : tag.tagNames())
        if (!result.contains(name))
            result << name;
    result.sort();
    return result;
}
//----------------------------------------------------------------------
QVariantList AppManager::getTagEntries(const QString &filterName) const
{
    collection->loadTagFileToTable();
    Tag tagModel;
    tagModel.loadFromDatabase(m_connectionName, filterName);
    QVariantList result;
    for (int i = 0; i < tagModel.rowCount(); ++i) {
        QModelIndex idx = tagModel.index(i, 0);
        QVariantMap entry;
        entry[QStringLiteral("tagId")]  = tagModel.data(idx, Tag::IdRole);
        entry[QStringLiteral("name")]   = tagModel.data(idx, Tag::NameRole);
        entry[QStringLiteral("folder")] = tagModel.data(idx, Tag::FolderRole);
        result << entry;
    }
    return result;
}
//----------------------------------------------------------------------
bool AppManager::createTag(const QString &name, const QString &path)
{
    if (getReadOnly()) return false;   // read-only collection (D43)
    bool ok = collection->createTag(name, path, QString(), QDateTime::currentDateTime());
    if (ok) emit tagsChanged();
    return ok;
}
//----------------------------------------------------------------------
bool AppManager::deleteTag(int tagID)
{
    if (getReadOnly()) return false;   // read-only collection (D43)
    bool ok = collection->deleteTag(tagID);
    if (ok) emit tagsChanged();
    return ok;
}
//----------------------------------------------------------------------
QVariantMap AppManager::getStatisticsData(const QString &source, const QString &dataType, const QString &startDate) const
{
    collection->loadStatisticsDeviceFileToTable();

    QDateTime dt;
    if (!startDate.isEmpty())
        dt = QDateTime::fromString(startDate, "yyyy-MM-dd");

    StatChartData data = Statistics::getChartData(
        m_connectionName,
        selectedDevice->ID,
        selectedDevice->type,
        source,
        dataType,
        dt
    );

    auto toVarList = [](const QList<StatPoint> &pts) {
        QVariantList list;
        list.reserve(pts.size());
        for (const auto &p : pts) {
            QVariantMap m;
            m[QStringLiteral("x")] = p.msecsSinceEpoch;
            m[QStringLiteral("y")] = p.value;
            list << m;
        }
        return list;
    };

    QVariantMap result;
    result[QStringLiteral("series1")]     = toVarList(data.series1);
    result[QStringLiteral("series2")]     = toVarList(data.series2);
    result[QStringLiteral("series3")]     = toVarList(data.series3);
    result[QStringLiteral("loadSeries1")] = data.loadSeries1;
    result[QStringLiteral("loadSeries2")] = data.loadSeries2;
    result[QStringLiteral("loadSeries3")] = data.loadSeries3;
    result[QStringLiteral("maxValue")]    = data.maxValue;
    result[QStringLiteral("unitKey")]     = data.unitKey;
    result[QStringLiteral("deviceName")]  = selectedDevice->name;
    result[QStringLiteral("hasData")]     = !data.series1.isEmpty();
    return result;
}
//----------------------------------------------------------------------
QString AppManager::getStatisticsSetting(const QString &key) const
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    return settings.value("Statistics/" + key).toString();
}
//----------------------------------------------------------------------
void AppManager::setStatisticsSetting(const QString &key, const QVariant &value)
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("Statistics/" + key, value);
    settings.sync();
}
//----------------------------------------------------------------------
// Persisted table column widths, in the collection settings file
// (SpecCardsAndTables CDT-F12 / CDT-C9). A missing, non-numeric or
// non-positive value reads as 0, meaning "use the built-in default"; 0 is never
// written, because a zero width is how K3 hides a column.
int AppManager::getColumnWidthSetting(const QString &key) const
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    bool ok = false;
    const int width = settings.value(key).toInt(&ok);
    return (ok && width > 0) ? width : 0;
}
//----------------------------------------------------------------------
void AppManager::setColumnWidthSetting(const QString &key, int width)
{
    if (width <= 0)
        return;
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue(key, width);
}
//----------------------------------------------------------------------
// K2's Explore splitter keys, shared so a division set in one version shows in
// the other (EXP-F34). Read and written as QSize, the form K2 uses (EXP-C27).
// Empty unless both are present, K2's restore rule.
QVariantList AppManager::getExploreSplitterWidths() const
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    const int tree  = settings.value("Explore/ExploreSplitterWidget1Size").toSize().width();
    const int files = settings.value("Explore/ExploreSplitterWidget2Size").toSize().width();
    if (tree <= 0 || files <= 0)
        return {};
    return { tree, files };
}
//----------------------------------------------------------------------
void AppManager::setExploreSplitterSizes(int treeWidth, int treeHeight, int filesWidth, int filesHeight)
{
    if (treeWidth <= 0 || filesWidth <= 0)
        return;
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("Explore/ExploreSplitterWidget1Size", QSize(treeWidth, treeHeight));
    settings.setValue("Explore/ExploreSplitterWidget2Size", QSize(filesWidth, filesHeight));
}
//----------------------------------------------------------------------
bool AppManager::shouldShowAlphaWarning() const
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    return settings.value("Settings/ShowBetaVersion", true).toBool();
}

void AppManager::setAlphaWarningShown()
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("Settings/ShowBetaVersion", false);
    settings.sync();
}
//----------------------------------------------------------------------
void AppManager::clearFirstRun()
{
    if (m_firstRun) {
        m_firstRun = false;
        emit firstRunChanged();
    }
}

void AppManager::selectDeviceById(int deviceId)
{
    if (!selectedDevice) {
        selectedDevice = new Device();
    }

    // Only emit signal if the device actually changes
    if (selectedDevice->ID != deviceId) {
        selectedDevice->ID = deviceId;
        if (selectedDevice->ID == 0) selectedDevice->type = "All";

        // Load the row first (single indexed query, synchronous) so that name/path/
        // type are populated before the signal fires. Emitting earlier left bindings
        // on selectedDeviceName/Path/Type reading the *previous* device's values with
        // no later notification, i.e. always one selection behind.
        selectedDevice->loadDevice(m_connectionName);

        QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
        settings.setValue("Selection/SelectedDeviceID", deviceId);

        emit selectedDeviceChanged(deviceId);
    }
}
//----------------------------------------------------------------------
QString AppManager::getDatabaseMode() const
{
    return collection->databaseMode;
}
//----------------------------------------------------------------------
QString AppManager::getCollectionFolder() const
{
    return collection->folder;
}
//----------------------------------------------------------------------
void AppManager::openCollectionMemory(const QString &folder)
{
    collection->folder = folder;

    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("Settings/databaseMode",  "Memory");
    settings.setValue("LastCollectionFolder",   folder);
    settings.sync();

    if (reconnectToDatabase()) {
        saveToRecentCollections("Memory", folder, QFileInfo(folder).fileName());
        emit databaseConnectionChanged(true, tr("Memory collection opened: %1").arg(QFileInfo(folder).fileName()));
    } else {
        emit databaseConnectionChanged(false, tr("Failed to open Memory collection: %1").arg(lastDatabaseError));
    }
}
//----------------------------------------------------------------------
void AppManager::openCollectionHosted(const QString &hostName, const QString &dbName,
                                      int port, const QString &userName, const QString &password)
{
    // Backstop: never connect to, nor save, a public host (HST-C4, HST-C2)
    if (Database::validateHostname(hostName) == Database::PublicOrInvalid) {
        emit databaseConnectionChanged(false, tr("Invalid hostname."));
        return;
    }

    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("Settings/databaseMode",    "Hosted");
    settings.setValue("Settings/databaseHostName", hostName);
    settings.setValue("Settings/databaseName",     dbName);
    settings.setValue("Settings/databasePort",     port);
    settings.setValue("Settings/databaseUserName", userName);
    settings.setValue("Settings/databasePassword", password);
    settings.sync();

    if (reconnectToDatabase()) {
        saveToRecentCollections("Hosted", hostName + "/" + dbName, hostName + "/" + dbName,
                                hostName, dbName, port, userName, password);
        emit databaseConnectionChanged(true, tr("Connected: %1/%2").arg(hostName).arg(dbName));
    } else {
        emit databaseConnectionChanged(false, tr("Hosted connection failed: %1").arg(lastDatabaseError));
    }
}
//----------------------------------------------------------------------
QString AppManager::hostedHostCheck(const QString &hostName) const
{
    switch (Database::validateHostname(hostName)) {
    case Database::Localhost:      return QStringLiteral("localhost");
    case Database::PrivateNetwork: return QStringLiteral("private");
    default:                       return QStringLiteral("invalid");
    }
}
//----------------------------------------------------------------------
QVariantList AppManager::getRecentCollections() const
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    int count = qMin(settings.value("Recent/count", 0).toInt(), maxRecentCollections);
    QVariantList result;
    for (int i = 0; i < count; ++i) {
        QVariantMap entry;
        entry["mode"]        = settings.value(QString("Recent/%1/mode").arg(i)).toString();
        entry["path"]        = settings.value(QString("Recent/%1/path").arg(i)).toString();
        QString storedIcon   = settings.value(QString("Recent/%1/iconName").arg(i)).toString();
        if      (storedIcon == "server-database") storedIcon = "network-server-database";
        else if (storedIcon == "network-server")  storedIcon = "network-workgroup";
        entry["iconName"]    = storedIcon;
        entry["hostName"]    = settings.value(QString("Recent/%1/hostName").arg(i)).toString();
        entry["dbName"]      = settings.value(QString("Recent/%1/dbName").arg(i)).toString();
        entry["port"]        = settings.value(QString("Recent/%1/port").arg(i), 3306).toInt();
        entry["userName"]    = settings.value(QString("Recent/%1/userName").arg(i)).toString();
        entry["password"]    = settings.value(QString("Recent/%1/password").arg(i)).toString();

        // Entries written before the name was computed reliably are stored blank;
        // deriving here rather than trusting the stored value means they recover
        // on their own, without the user reopening each collection to repair it.
        // The path is the last resort so a menu line is never empty.
        QString displayName = settings.value(QString("Recent/%1/displayName").arg(i)).toString();
        if (displayName.isEmpty())
            displayName = collectionDisplayName(entry["mode"].toString(), entry["path"].toString(),
                                                entry["hostName"].toString(), entry["dbName"].toString());
        if (displayName.isEmpty())
            displayName = entry["path"].toString();
        entry["displayName"] = displayName;

        result.append(entry);
    }
    return result;
}
//----------------------------------------------------------------------
QString AppManager::collectionDisplayName(const QString &mode, const QString &path,
                                         const QString &hostName, const QString &dbName)
{
    // QDir::dirName(), not QFileInfo::fileName(): the latter returns an empty
    // string for a path that ends with a separator, which is how a collection
    // folder arrives from some of the write paths. cleanPath() first, because
    // dirName() is empty in turn for a doubled separator; and an empty path is
    // returned as such, since QDir("").dirName() is "." rather than nothing.
    if (mode == "Memory") {
        if (path.isEmpty()) return QString();
        return QDir(QDir::cleanPath(path)).dirName();
    }
    if (mode == "File")   return QFileInfo(path).fileName();
    if (mode == "Hosted") return hostName + "/" + dbName;
    return QString();
}
//----------------------------------------------------------------------
QString AppManager::getCurrentCollectionDisplayName() const
{
    QString mode = getDatabaseMode();
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    // The collection fields are repopulated by Database::initialize() on every
    // connection, so they are current even when the settings file is not; the
    // persisted keys are the fallback for values never set this session.
    QString path, host, name;
    if (mode == "Memory") {
        path = collection->folder;
        if (path.isEmpty()) path = settings.value("LastCollectionFolder").toString();
    } else if (mode == "File") {
        path = collection->databaseFilePath;
        if (path.isEmpty()) path = settings.value("Settings/DatabaseFilePath").toString();
    } else if (mode == "Hosted") {
        host = collection->databaseHostName;
        name = collection->databaseName;
        if (host.isEmpty()) host = settings.value("Settings/databaseHostName").toString();
        if (name.isEmpty()) name = settings.value("Settings/databaseName").toString();
    }
    return collectionDisplayName(mode, path, host, name);
}
//----------------------------------------------------------------------
QString AppManager::getCurrentCollectionIconName() const
{
    QString mode = getDatabaseMode();
    if (mode == "Memory") return "folder";
    if (mode == "File")   return "network-server-database";
    return "network-workgroup";
}
//----------------------------------------------------------------------
void AppManager::openRecentCollection(const QVariantMap &entry)
{
    QString mode = entry["mode"].toString();
    if (mode == "Memory") {
        openCollectionMemory(entry["path"].toString());
    } else if (mode == "File") {
        setDatabaseFilePath(entry["path"].toString());
    } else if (mode == "Hosted") {
        openCollectionHosted(entry["hostName"].toString(),
                             entry["dbName"].toString(),
                             entry["port"].toInt(),
                             entry["userName"].toString(),
                             entry["password"].toString());
    }
}
//----------------------------------------------------------------------
void AppManager::saveToRecentCollections(const QString &mode, const QString &path,
                                         const QString &displayName,
                                         const QString &hostName, const QString &dbName,
                                         int port, const QString &userName, const QString &password)
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    int count = qMin(settings.value("Recent/count", 0).toInt(), maxRecentCollections);

    // Load existing entries
    QVariantList existing;
    for (int i = 0; i < count; ++i) {
        QVariantMap e;
        e["mode"]        = settings.value(QString("Recent/%1/mode").arg(i)).toString();
        e["path"]        = settings.value(QString("Recent/%1/path").arg(i)).toString();
        e["displayName"] = settings.value(QString("Recent/%1/displayName").arg(i)).toString();
        e["iconName"]    = settings.value(QString("Recent/%1/iconName").arg(i)).toString();
        e["hostName"]    = settings.value(QString("Recent/%1/hostName").arg(i)).toString();
        e["dbName"]      = settings.value(QString("Recent/%1/dbName").arg(i)).toString();
        e["port"]        = settings.value(QString("Recent/%1/port").arg(i), 3306).toInt();
        e["userName"]    = settings.value(QString("Recent/%1/userName").arg(i)).toString();
        e["password"]    = settings.value(QString("Recent/%1/password").arg(i)).toString();
        existing.append(e);
    }

    // Remove any existing entry with the same path (dedup)
    for (int i = existing.size() - 1; i >= 0; --i) {
        if (existing[i].toMap()["path"].toString() == path)
            existing.removeAt(i);
    }

    // Build and prepend new entry
    QString iconName = (mode == "Memory") ? "folder"
                     : (mode == "File")   ? "network-server-database"
                                          : "network-workgroup";
    // Computed here, not taken on trust: a caller passing an empty name must not
    // be able to freeze it into the settings file (SpecCollectionIdentity.md CID-C4).
    QString name = collectionDisplayName(mode, path, hostName, dbName);
    if (name.isEmpty())
        name = displayName.isEmpty() ? path : displayName;

    QVariantMap newEntry;
    newEntry["mode"]        = mode;
    newEntry["path"]        = path;
    newEntry["displayName"] = name;
    newEntry["iconName"]    = iconName;
    newEntry["hostName"]    = hostName;
    newEntry["dbName"]      = dbName;
    newEntry["port"]        = port;
    newEntry["userName"]    = userName;
    newEntry["password"]    = password;
    existing.prepend(newEntry);

    // Trim to the list size
    while (existing.size() > maxRecentCollections)
        existing.removeLast();

    // Persist
    settings.setValue("Recent/count", existing.size());
    for (int i = 0; i < existing.size(); ++i) {
        QVariantMap e = existing[i].toMap();
        settings.setValue(QString("Recent/%1/mode").arg(i),        e["mode"]);
        settings.setValue(QString("Recent/%1/path").arg(i),        e["path"]);
        settings.setValue(QString("Recent/%1/displayName").arg(i), e["displayName"]);
        settings.setValue(QString("Recent/%1/iconName").arg(i),    e["iconName"]);
        settings.setValue(QString("Recent/%1/hostName").arg(i),    e["hostName"]);
        settings.setValue(QString("Recent/%1/dbName").arg(i),      e["dbName"]);
        settings.setValue(QString("Recent/%1/port").arg(i),        e["port"]);
        settings.setValue(QString("Recent/%1/userName").arg(i),    e["userName"]);
        settings.setValue(QString("Recent/%1/password").arg(i),    e["password"]);
    }
    settings.sync();

    emit recentCollectionsChanged();
}
//----------------------------------------------------------------------
// Catalog creation
//----------------------------------------------------------------------
void AppManager::setupDeviceUpdateManager()
{
    if (!m_deviceUpdateManager) {
        m_deviceUpdateManager = new DeviceUpdateManager(this);
    }
    disconnect(m_deviceUpdateManager, nullptr, this, nullptr);

    if (!m_catalogProgressManager)
        m_catalogProgressManager = new CatalogProgressManager(this);
    // Reconnected on every setup, not only on creation: the devices variant of
    // this setup points the same progress manager at m_deviceUpdateStatusText,
    // so a creation started after an update would otherwise report its progress
    // into the devices status text and leave the create text stale.
    disconnect(m_catalogProgressManager, nullptr, this, nullptr);
    connect(m_catalogProgressManager, &CatalogProgressManager::statusMessageChanged,
            this, [this](const QString &message, int /*timeout*/) {
                m_catalogStatusText = message;
                emit catalogStatusTextChanged();
            });
    m_deviceUpdateManager->setCatalogProgressManager(m_catalogProgressManager);
    m_deviceUpdateManager->setConnectionName(m_connectionName);

    connect(m_deviceUpdateManager, &DeviceUpdateManager::operationCompleted,
            this, &AppManager::onCatalogCreationCompleted);
    connect(m_deviceUpdateManager, &DeviceUpdateManager::operationError,
            this, &AppManager::onCatalogCreationError);
    connect(m_deviceUpdateManager, &DeviceUpdateManager::operationCancelled,
            this, &AppManager::onCatalogCreationCancelled);
}
//----------------------------------------------------------------------
QString AppManager::createCatalog(const QString &name, const QString &path,
                                   int storageId,
                                   const QString &fileType, bool includeSubDir,
                                   bool includeHidden, bool includeSymlinks,
                                   bool isFullDevice,
                                   const QString &includeMetadata,
                                   const QString &includeChecksum,
                                   const QStringList &perCatalogExcludes)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    m_lastCreateRequestDeviceId = 0;

    // A search is a blocking call on this same thread that keeps the UI alive by
    // pumping the event loop, so Create stays reachable while one runs. Starting
    // here would nest inside the search's call stack on the same database
    // connection. The UI also disables the action; this is the guard that matters.
    if (m_searchIsRunning || m_exploreLoadIsRunning)   // OPQ-F20 for the Explore load
        return tr("A device operation is already running.");

    // Validation
    if (name.trimmed().isEmpty())
        return tr("Provide a name for this new catalog.");
    if (path.trimmed().isEmpty())
        return tr("Provide a path for this new catalog.");
    if (storageId <= 0)
        return tr("Select a Storage for this new catalog.\n(Selection panel on the left and dropdown list)");
    QDir sourceDir(path);
    if (!sourceDir.exists())
        return tr("The source directory does not exist.");

    // Duplicate name check
    Device *newDevice = new Device();
    newDevice->generateDeviceID();
    newDevice->type = "Catalog";
    newDevice->name = name.trimmed();
    if (newDevice->verifyDeviceNameExists()) {
        delete newDevice;
        return tr("There is already a catalog with this name:\n%1\n\nChoose a different name and try again.").arg(name.trimmed());
    }

    // Populate device and catalog
    newDevice->parentID = storageId;
    newDevice->catalog->generateID();
    newDevice->externalID = newDevice->catalog->ID;
    newDevice->groupID    = 0;
    newDevice->path       = path;
    newDevice->insertDevice();

    // Parent Storage, for its path below
    Device parentDevice;
    parentDevice.ID = storageId;
    parentDevice.loadDevice(m_connectionName);

    newDevice->catalog->name             = newDevice->name;
    newDevice->catalog->filePath         = collection->folder + "/" + newDevice->name + ".idx";
    newDevice->catalog->sourcePath       = path;
    newDevice->catalog->fileType         = fileType;
    newDevice->catalog->includeSubDir    = includeSubDir;
    newDevice->catalog->includeHidden    = includeHidden;
    newDevice->catalog->includeSymblinks = includeSymlinks;
    newDevice->catalog->isFullDevice     = isFullDevice;
    newDevice->catalog->includeMetadata  = includeMetadata;
    newDevice->catalog->includeChecksum  = includeChecksum;
    newDevice->catalog->appVersion       = currentVersion;
    newDevice->catalog->insertCatalog();

    // Per-catalog exclude folders
    for (const QString &folder : perCatalogExcludes)
        newDevice->catalog->addExcludeFolder(folder);
    collection->saveCatalogFilterTableToFile();

    // Update parent Storage path if it was empty
    if (parentDevice.path.isEmpty()) {
        parentDevice.path = path;
        parentDevice.saveDevice();
        collection->saveStorageTableToFile();
    }

    // Check for a concurrent operation BEFORE touching the manager: rewiring its
    // handlers now would hand the operation already running to the wrong set,
    // and its completion would be reported as this creation's.
    if (m_catalogIsCreating
        || m_deviceUpdateIsRunning
        || (m_deviceUpdateManager && m_deviceUpdateManager->operationRunning())) {
        // Busy: queue the scan instead of refusing. The device and catalog rows
        // above are already written, so the entry only needs the device id — and
        // the catalog is visible immediately, waiting to be filled. The handlers
        // are wired when the entry actually starts, in startNextDeviceUpdate().
        const int     queuedId   = newDevice->ID;
        const QString queuedName = newDevice->name;
        m_lastCreateRequestDeviceId = queuedId;
        delete newDevice;
        enqueueOperation(queuedId, true, queuedName);
        emit deviceListChanged();
        refreshDeviceList();
        return QString();
    }

    setupDeviceUpdateManager();
    m_creatingDevice = newDevice;
    m_lastCreateRequestDeviceId = newDevice->ID;
    m_catalogIsCreating = true;
    m_catalogCreateStartTime = QDateTime::currentDateTime();
    m_catalogStatusText = StatusBarMessageBuilder().setOperation(tr("Create")).setStatus(tr("In Progress")).build();
    emit catalogIsCreatingChanged();
    emit catalogStatusTextChanged();

    // The scan runs synchronously on the GUI thread (CatalogJobStoppable pumps
    // the event loop by hand), so calling it here would block before Qt Quick
    // ever paints the activity panel — on a small catalog the panel would be
    // shown for zero frames and its indicator never seen. One turn of the event
    // loop first guarantees the panel is on screen, with the indicator running,
    // before the scan takes the thread. The busy flags above are set
    // synchronously, so the concurrency guard still closes at request time.
    Device *deviceToScan = newDevice;
    QTimer::singleShot(kStartDeferralMs, this, [this, deviceToScan]() {
        m_deviceUpdateManager->updateDeviceHierarchy(deviceToScan,
                                                     collection->databaseMode,
                                                     collection->folder,
                                                     "create");
    });
    return QString();
}
//----------------------------------------------------------------------
void AppManager::stopCatalogCreation()
{
    qWarning() << "STOPTRACE AppManager::stopCatalogCreation manager="
               << (m_deviceUpdateManager != nullptr)
               << "operationRunning="
               << (m_deviceUpdateManager && m_deviceUpdateManager->operationRunning())
               << "catalogIsCreating=" << m_catalogIsCreating;
    if (m_deviceUpdateManager && m_deviceUpdateManager->operationRunning())
        m_deviceUpdateManager->requestHardStop();
}
//----------------------------------------------------------------------
bool AppManager::isDirectoryEmpty(const QString &path) const
{
    QDir dir(path);
    return dir.exists() && dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty();
}
//----------------------------------------------------------------------
QString AppManager::createCatalogWithEnrichment(const QString &name, const QString &path,
                                                int storageId,
                                                const QString &fileType, bool includeSubDir,
                                                bool includeHidden, bool includeSymlinks,
                                                bool isFullDevice,
                                                const QString &includeMetadata,
                                                const QString &includeChecksum,
                                                const QStringList &perCatalogExcludes,
                                                const QVariantMap &enrichmentProfile)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    // The wizard reaches creation only through the standard entry point (MDI-C2).
    const QString error = createCatalog(name, path, storageId, fileType, includeSubDir,
                                        includeHidden, includeSymlinks, isFullDevice,
                                        includeMetadata, includeChecksum, perCatalogExcludes);
    if (error.isEmpty() && m_lastCreateRequestDeviceId > 0)
        m_pendingEnrichmentProfiles.insert(m_lastCreateRequestDeviceId, enrichmentProfile);
    return error;
}
//----------------------------------------------------------------------
void AppManager::savePendingEnrichmentProfile(int deviceId)
{
    const QVariantMap pending = m_pendingEnrichmentProfiles.take(deviceId);
    if (pending.isEmpty())
        return;

    MdiProfile profile;
    profile.deviceId        = deviceId;
    profile.destinationPath = pending.value(QStringLiteral("destinationPath")).toString();
    profile.settings        = QJsonObject::fromVariantMap(pending.value(QStringLiteral("settings")).toMap());
    if (!profile.save(m_connectionName)) {
        qWarning() << "savePendingEnrichmentProfile: profile not saved for device" << deviceId;
        return;
    }
    if (collection->databaseMode == "Memory")
        MdiProfile::saveTableToFile(m_connectionName, collection->folder);

    // The enrichment job is added only once the creation has completed (MDI-F7)
    const QString error = startEnrichment(deviceId);
    if (!error.isEmpty())
        qWarning() << "savePendingEnrichmentProfile: enrichment not started:" << error;
}
//----------------------------------------------------------------------
bool AppManager::isPathWritable(const QString &path) const
{
    // A destination that does not exist yet is accepted when the nearest
    // existing folder above it can be written to: in core, shared with the
    // command line (D43)
    return MdiDiskSpace::isWritable(path);
}
//----------------------------------------------------------------------
bool AppManager::isPathInside(const QString &path, const QString &parentPath) const
{
    auto resolved = [](const QString &p) {
        const QFileInfo info(p);
        return info.exists() ? info.canonicalFilePath()
                             : QDir::cleanPath(info.absoluteFilePath());
    };
    if (path.trimmed().isEmpty() || parentPath.trimmed().isEmpty())
        return false;
    const QString child  = resolved(path);
    QString       parent = resolved(parentPath);
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    if (child.compare(parent, cs) == 0)
        return true;
    if (!parent.endsWith('/'))
        parent += '/';
    return child.startsWith(parent, cs);
}
//----------------------------------------------------------------------
QString AppManager::getCreateSetting(const QString &key) const
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    return settings.value("Create/" + key).toString();
}
//----------------------------------------------------------------------
void AppManager::setCreateSetting(const QString &key, const QVariant &value)
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("Create/" + key, value);
    settings.sync();
}
//----------------------------------------------------------------------
// Enrichment (fork, SpecMediaIntelligence.md)
//----------------------------------------------------------------------
namespace {
const QString kMdiMainConnection = QStringLiteral("mdi_main");
const QStringList kMdiAnalysisOrder = {
    QStringLiteral("thumbnail"), QStringLiteral("poster"), QStringLiteral("keyframes"),
    QStringLiteral("clip_video"), QStringLiteral("clip_audio"), QStringLiteral("waveform"),
    QStringLiteral("caption"), QStringLiteral("faces"), QStringLiteral("dating"), QStringLiteral("transcript"),
    QStringLiteral("segments"), QStringLiteral("summary"), QStringLiteral("document"), QStringLiteral("mediainfo"),
    QStringLiteral("snippet"),
    QStringLiteral("archive"), QStringLiteral("classification"), QStringLiteral("quickhash"),
    QStringLiteral("embeddings")
};
}
//----------------------------------------------------------------------
QString AppManager::mdiDataPath() const
{
    return MdiData::filePath(collection->folder);
}
//----------------------------------------------------------------------
bool AppManager::openMdiMainConnection()
{
    // Read side of mdi_data.db on the main thread; the runner has its own
    // connection on its thread (MDI-C4). Reopened when the collection changes.
    const QString path = mdiDataPath();
    // A read-only collection without AI data does not get one (D43)
    if (getReadOnly() && !QFileInfo::exists(path))
        return false;
    if (QSqlDatabase::contains(kMdiMainConnection)) {
        QSqlDatabase db = QSqlDatabase::database(kMdiMainConnection, false);
        if (db.isOpen() && db.databaseName() == path)
            return true;
        MdiData::close(kMdiMainConnection);
    }
    return MdiData::open(kMdiMainConnection, path);
}
//----------------------------------------------------------------------
QVariantMap AppManager::getEnrichmentProfile(int deviceId)
{
    MdiProfile profile;
    if (!profile.load(m_connectionName, deviceId))
        return QVariantMap();
    return QVariantMap{ { QStringLiteral("destinationPath"), profile.destinationPath },
                        { QStringLiteral("settings"),        profile.settings.toVariantMap() } };
}
//----------------------------------------------------------------------
bool AppManager::saveEnrichmentProfile(int deviceId, const QVariantMap &values)
{
    if (getReadOnly()) return false;   // read-only collection (D43)
    MdiProfile profile;
    profile.deviceId        = deviceId;
    profile.destinationPath = values.value(QStringLiteral("destinationPath")).toString();
    profile.settings        = QJsonObject::fromVariantMap(values.value(QStringLiteral("settings")).toMap());
    if (!profile.save(m_connectionName))
        return false;
    if (collection->databaseMode == "Memory")
        MdiProfile::saveTableToFile(m_connectionName, collection->folder);
    return true;
}
//----------------------------------------------------------------------
QVariantMap AppManager::getEnrichmentTools()
{
    // In core, shared with the command line (D43): the settings file first,
    // then the computer, then the programs and models of the portable build
    return MdiEnrichmentJob::findTools(collection->settingsFilePath);
}
//----------------------------------------------------------------------
QString AppManager::mdiAnalysisLabel(const QString &analysis) const
{
    if (analysis == QLatin1String("thumbnail"))  return tr("Thumbnail");
    if (analysis == QLatin1String("poster"))     return tr("Poster frame");
    if (analysis == QLatin1String("keyframes"))  return tr("Keyframe strip");
    if (analysis == QLatin1String("clip_video")) return tr("Video") + QStringLiteral(" · ") + tr("Clip");
    if (analysis == QLatin1String("clip_audio")) return tr("Audio") + QStringLiteral(" · ") + tr("Clip");
    if (analysis == QLatin1String("waveform"))   return tr("Waveform");
    if (analysis == QLatin1String("caption"))    return tr("Caption and tags");
    if (analysis == QLatin1String("summary"))    return tr("Summary and excerpts");
    if (analysis == QLatin1String("transcript")) return tr("Transcript");
    if (analysis == QLatin1String("segments"))   return tr("Topic segments");
    if (analysis == QLatin1String("clusters"))   return tr("Topic clusters");
    if (analysis == QLatin1String("embeddings")) return tr("Index for search by meaning");
    if (analysis == QLatin1String("snippet"))    return tr("Text snippet");
    if (analysis == QLatin1String("fts"))        return tr("Instant search index");
    if (analysis == QLatin1String("quickhash"))  return tr("Quick hash");
    if (analysis == QLatin1String("mediainfo"))  return tr("Technical data");
    if (analysis == QLatin1String("archive"))    return tr("Archive contents");
    if (analysis == QLatin1String("classification")) return tr("Classification");
    if (analysis == QLatin1String("knowledge"))  return tr("Reference documents");
    if (analysis == QLatin1String("faces"))      return tr("Faces");
    if (analysis == QLatin1String("dating"))     return tr("Dates, events and places");
    if (analysis == QLatin1String("document"))   return tr("Document study");
    if (analysis == QLatin1String("proposals"))  return tr("Proposals to approve");
    return analysis;
}
//----------------------------------------------------------------------
QString AppManager::startEnrichment(int deviceId, bool resumed)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    MdiData::PendingJob job;
    job.deviceId = deviceId;
    return startMdiJob(job, resumed);
}
//----------------------------------------------------------------------
QString AppManager::startReinterpretation(int deviceId, const QString &folder, const QString &fileType)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    MdiData::PendingJob job;
    job.deviceId = deviceId;
    job.kind     = QStringLiteral("reinterpret");
    job.folder   = folder;
    job.fileType = fileType;
    return startMdiJob(job, false);
}
//----------------------------------------------------------------------
QString AppManager::startMdiJob(const MdiData::PendingJob &job, bool resumed)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    // One job at a time; another waits its turn, and the same work already
    // running or waiting is not added twice (MDI-F16): same catalog, kind
    // and scope. Stored transcripts being cleaned: the job waits for it too.
    if (m_mdiJob || m_transcriptCleanThread) {
        const bool running = job.sameWork(m_mdiRunning) && !m_mdiLeaving && !m_mdiKeepPending;
        const bool listed  = std::any_of(m_mdiWaiting.cbegin(), m_mdiWaiting.cend(),
                                         [&job](const MdiData::PendingJob &w) { return w.sameWork(job); });
        if (!running && !listed) {
            m_mdiWaiting.append(job);
            saveMdiJobs();
            stepAsideIfWaiting();
        }
        return QString();
    }

    Device device;
    device.ID = job.deviceId;
    device.loadDevice(m_connectionName);
    if (device.type != QLatin1String("Catalog"))
        return tr("Selected device is not a catalog");

    const bool reinterpret = job.kind == QLatin1String("reinterpret");
    MdiProfile profile;
    if (!profile.load(m_connectionName, job.deviceId))
        return tr("Provide a destination path for the previews and derived data.");
    // A re-interpretation writes no derived file; a resumed job whose
    // destination drive is away waits for it (D31)
    if (!reinterpret && !isPathWritable(profile.destinationPath)
        && (!resumed || QFileInfo::exists(profile.destinationPath)))
        return tr("Select a destination path that can be written to.");
    // Room for the AI data before anything starts (MDI-F19): the volume of
    // mdi_data.db, and the one of the derived files unless the job writes
    // none or that drive is away (the job waits for it, D31). A queued or
    // resumed job refused here is held, not tried again in a loop (MDI-F12).
    {
        QStringList volumes{ collection->folder };
        if (!reinterpret && (!resumed || QFileInfo::exists(profile.destinationPath)))
            volumes << profile.destinationPath;
        const MdiDiskSpace::Shortage shortage = MdiDiskSpace::firstShortage(volumes, MdiDiskSpace::kJobMinimumFree);
        if (shortage.isShort())
            return tr("The AI analysis needs at least %1 of free space on %2, which has %3 left.")
                .arg(QLocale().formattedDataSize(shortage.needed), shortage.path,
                     QLocale().formattedDataSize(shortage.available));
    }

    // Tools, models, language, face threshold and the domain context (D33):
    // assembled in core, the same for the command line (D43)
    MdiEnrichmentJob::Config config = MdiEnrichmentJob::analysisConfig(
        getEnrichmentTools(), collection->settingsFilePath, device, mdiDataPath(), profile,
        openMdiMainConnection() ? kMdiMainConnection : QString());
    config.facesOnly               = job.kind == QLatin1String("faces");
    // Dating or document study on its own (fork, 2026-10-30)
    if (job.kind == QLatin1String("dating") || job.kind == QLatin1String("document"))
        config.onlyAnalysis        = job.kind;
    if (reinterpret) {
        if (MdiContext::isEmpty(config.context)) {
            qWarning() << "Re-interpretation without a context: not started";
            return QString();
        }
        config.reinterpret   = true;
        config.scopeFolder   = job.folder;
        config.scopeFileType = job.fileType;
    }
    return launchMdiJob(device, config, job);
}
//----------------------------------------------------------------------
QString AppManager::startQuickHash(int deviceId)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    // A quick-hash pass waits behind any running job (MDI-F16)
    if (m_mdiJob) {
        if (!m_quickHashWaiting.contains(deviceId)) {
            m_quickHashWaiting.append(deviceId);
            stepAsideIfWaiting();
        }
        return QString();
    }
    Device device;
    device.ID = deviceId;
    device.loadDevice(m_connectionName);
    if (device.type != QLatin1String("Catalog"))
        return tr("Selected device is not a catalog");

    const QVariantMap tools = getEnrichmentTools();
    MdiEnrichmentJob::Config config;
    config.catalogId     = device.externalID;
    config.catalogName   = device.name;
    config.dataDbPath    = mdiDataPath();
    config.quickHashOnly = true;
    config.ollamaUrl     = tools.value(QStringLiteral("ollamaUrl")).toString();
    MdiData::PendingJob job;
    job.deviceId = deviceId;
    job.kind     = QStringLiteral("quickhash");   // never kept in the list (MDI-F12)
    return launchMdiJob(device, config, job);
}
//----------------------------------------------------------------------
QString AppManager::launchMdiJob(Device &device, MdiEnrichmentJob::Config config, const MdiData::PendingJob &job)
{
    config.catalogStamp = MdiFts::stamp(device.catalog->dateUpdated);

    // Read on this thread, with the collection connection (Memory: from the .idx)
    const QList<MdiEnrichmentJob::FileItem> files =
        MdiEnrichmentJob::listCatalogFiles(m_connectionName, device.externalID,
                                           collection->databaseMode, device.catalog->filePath);

    m_mdiCatalogName = device.name;
    // A restored job that was paused, or that uses a provider not on this
    // computer, starts paused (D6, D31)
    m_mdiIsPaused    = !config.quickHashOnly && job.paused;
    m_mdiJobStart    = QDateTime::currentDateTime();
    m_mdiSourcePath  = device.path;
    m_mdiRunning          = job;
    m_mdiRunningCatalogId = device.externalID;
    m_mdiQuickHashOnly    = config.quickHashOnly;
    if (!config.quickHashOnly)
        m_mdiHeld.removeIf([&job](const MdiData::PendingJob &h) { return h.sameWork(job); });
    m_mdiWaitingForSource = false;
    m_mdiLeaving          = false;
    m_mdiJob    = new MdiEnrichmentJob(config, files);
    m_mdiThread = new QThread();
    m_mdiJob->moveToThread(m_mdiThread);
    if (m_mdiIsPaused)
        m_mdiJob->setPaused(true);

    auto status = [this](const QString &state) {
        return StatusBarMessageBuilder().setOperation(tr("Enrichment")).setStatus(state)
                                        .setDeviceContext(1, 1, m_mdiCatalogName);
    };
    m_mdiStatusText = mdiStateStatus();
    emit mdiStatusTextChanged();

    connect(m_mdiThread, &QThread::started, m_mdiJob, &MdiEnrichmentJob::run);
    connect(m_mdiJob, &MdiEnrichmentJob::progress, this,
            [this, status](int fileIndex, int fileCount, const QString &analysis, const QString &filePath) {
        if (m_mdiKeepPending)
            return;   // the job of a collection being left shows nothing (MDI-F93)
        m_mdiStatusText = status(m_mdiIsPaused ? tr("Paused") : tr("In Progress"))
                              .setProcess(mdiAnalysisLabel(analysis), fileIndex, fileCount)
                              .setCurrentItem(filePath)
                              .build();
        emit mdiStatusTextChanged();
    });
    connect(m_mdiJob, &MdiEnrichmentJob::activity, this, &AppManager::onMdiActivity);
    const int jobCatalogId = device.externalID;
    connect(m_mdiJob, &MdiEnrichmentJob::datumCommitted, this, [this, jobCatalogId](const QString &, const QString &) {
        // Coalesced: Explore re-reads its previews at most once a second, and
        // only when it shows this job's catalog
        if (!m_mdiRevisionPending) {
            m_mdiRevisionPending = true;
            QTimer::singleShot(1000, this, [this, jobCatalogId]() {
                m_mdiRevisionPending = false;
                bumpMdiRevision(jobCatalogId);
            });
        }
    });
    connect(m_mdiJob, &MdiEnrichmentJob::finished, this, [this, status, jobCatalogId](const QVariantMap &summary) {
        const bool stopped = summary.value(QStringLiteral("stopped")).toBool();
        // Ended by quitting or by opening another collection: it stays pending
        // in its own collection and shows nothing here (MDI-F12, MDI-F93)
        const bool keptPending = m_mdiKeepPending;
        if (keptPending) {
            m_mdiStatusText.clear();
        } else {
            StatusBarMessageBuilder builder = status(stopped ? tr("Cancelled") : tr("Completed"));
            builder.setResult(tr("Completed"), summary.value(QStringLiteral("completed")).toInt());
            m_mdiStatusText = builder.build();
        }
        emit mdiStatusTextChanged();
        m_mdiJob = nullptr;           // deleted with its thread
        m_mdiIsPaused = false;
        m_mdiRunning          = MdiData::PendingJob{-1};
        m_mdiRunningCatalogId = -1;
        m_mdiQuickHashOnly    = false;
        m_mdiWaitingForSource = false;
        m_mdiKeepPending      = false;
        m_mdiLeaving          = false;
        if (!keptPending)
            saveMdiJobs();
        m_mdiStep.clear();
        emit mdiStepChanged();
        bumpMdiRevision(jobCatalogId);
        emit mdiStateChanged();
        emit enrichmentFinished(summary);
        QTimer::singleShot(0, this, &AppManager::startNextEnrichment);
    });
    connect(m_mdiJob,    &MdiEnrichmentJob::finished, m_mdiThread, &QThread::quit);
    connect(m_mdiThread, &QThread::finished, m_mdiJob,    &QObject::deleteLater);
    connect(m_mdiThread, &QThread::finished, m_mdiThread, &QObject::deleteLater);
    connect(m_mdiThread, &QThread::finished, this, [this]() { m_mdiThread = nullptr; });

    // The runner must not outlive the application (MDI-C7: processes ended on exit)
    if (!m_mdiQuitHooked) {
        m_mdiQuitHooked = true;
        connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
            // Quitting keeps the enrichment list for next time (MDI-F12)
            if (m_mdiJob) {
                m_mdiKeepPending = true;
                m_mdiJob->requestStop();
            }
            if (m_mdiThread) {
                m_mdiThread->quit();
                m_mdiThread->wait(5000);
            }
            stopKnowledgeIndexing();
            if (m_monitorThread) {
                m_monitorThread->quit();
                m_monitorThread->wait(3000);
            }
            stopFtsIndexing();
        });
    }

    m_mdiThread->start();
    if (!config.quickHashOnly)
        saveMdiJobs();
    emit mdiStateChanged();
    emit mdiStepChanged();
    updateMonitoring();
    return QString();
}
//----------------------------------------------------------------------
void AppManager::startNextEnrichment()
{
    if (getReadOnly()) return;   // read-only collection (D43)
    // Jobs whose source is reachable first, in their order; then the
    // quick-hash passes; then the jobs that will wait for their drive (MDI-F92)
    if (m_transcriptCleanThread)
        return;   // started again when the stored transcripts are cleaned
    bool notStarted = false;
    auto startJob = [this, &notStarted](const MdiData::PendingJob &next) {
        const QString error = startMdiJob(next, true);
        if (!error.isEmpty()) {
            notStarted = true;
            holdOrDropMdiJob(next, error);
        }
    };
    while (!m_mdiJob && !m_mdiWaiting.isEmpty()) {
        int next = -1;
        for (int k = 0; k < m_mdiWaiting.size() && next < 0; ++k)
            if (mdiJobReachable(m_mdiWaiting.at(k)))
                next = k;
        if (next < 0)
            break;
        startJob(m_mdiWaiting.takeAt(next));
    }
    while (!m_mdiJob && !m_quickHashWaiting.isEmpty()) {
        const int next = m_quickHashWaiting.takeFirst();
        const QString error = startQuickHash(next);
        if (!error.isEmpty())
            qWarning() << "Quick hash not started for device" << next << ":" << error;
    }
    while (!m_mdiJob && !m_mdiWaiting.isEmpty())
        startJob(m_mdiWaiting.takeFirst());
    if (!m_mdiJob && notStarted)
        saveMdiJobs();
    updateMonitoring();   // stops sampling when nothing runs and the page is closed
}
//----------------------------------------------------------------------
void AppManager::stopEnrichment()
{
    // Stop empties the enrichment list (MDI-C28)
    m_mdiWaiting.clear();
    m_quickHashWaiting.clear();
    m_mdiHeld.clear();
    if (m_mdiJob) {
        m_mdiLeaving = true;
        m_mdiJob->requestStop();
    }
    saveMdiJobs();
}
//----------------------------------------------------------------------
void AppManager::setEnrichmentPaused(bool paused)
{
    if (!m_mdiJob || m_mdiIsPaused == paused)
        return;
    m_mdiIsPaused = paused;
    m_mdiJob->setPaused(paused);
    m_mdiStatusText = mdiStateStatus();
    emit mdiStatusTextChanged();
    emit mdiStateChanged();
    saveMdiJobs();   // a job paused when Katalog closes comes back paused (MDI-F12)
}
//----------------------------------------------------------------------
QString AppManager::mdiStateStatus() const
{
    StatusBarMessageBuilder builder;
    builder.setOperation(tr("Enrichment")).setDeviceContext(1, 1, m_mdiCatalogName);
    if (m_mdiIsPaused)
        builder.setStatus(tr("Paused"));
    else if (m_mdiWaitingForSource)   // MDI-F91: the path waited for as current item
        builder.setStatus(tr("Waiting for the drive")).setCurrentItem(m_mdiWaitingPath);
    else
        builder.setStatus(tr("In Progress"));
    return builder.build();
}
//----------------------------------------------------------------------
bool AppManager::mdiSourceReachable(int deviceId)
{
    // Same rule as the runner (MDI-C29): present, and not an empty mount point
    Device device;
    device.ID = deviceId;
    device.loadDevice(m_connectionName);
    return !device.path.isEmpty() && QFileInfo::exists(device.path) && !QDir(device.path).isEmpty();
}
//----------------------------------------------------------------------
bool AppManager::mdiJobReachable(const MdiData::PendingJob &job)
{
    // A re-interpretation reads derived data only: it needs no drive
    return job.kind == QLatin1String("reinterpret") || mdiSourceReachable(job.deviceId);
}
//----------------------------------------------------------------------
void AppManager::stepAsideIfWaiting()
{
    // A job waiting for its drive lets a waiting catalog whose source is
    // there run first, and goes to the end of the list; it never steps aside
    // for nothing (MDI-F92)
    if (!m_mdiJob || !m_mdiWaitingForSource || m_mdiQuickHashOnly || m_mdiLeaving || m_mdiKeepPending)
        return;
    const bool other = std::any_of(m_mdiWaiting.cbegin(), m_mdiWaiting.cend(),
                                   [this](const MdiData::PendingJob &j) { return mdiJobReachable(j); })
                    || std::any_of(m_quickHashWaiting.cbegin(), m_quickHashWaiting.cend(),
                                   [this](int id) { return mdiSourceReachable(id); });
    if (!other)
        return;
    m_mdiLeaving = true;
    MdiData::PendingJob again = m_mdiRunning;
    again.paused = m_mdiIsPaused;
    m_mdiWaiting.append(again);
    saveMdiJobs();
    m_mdiJob->requestStop();
}
//----------------------------------------------------------------------
void AppManager::saveMdiJobs()
{
    if (getReadOnly()) return;   // read-only collection (D43)
    // The running job (unless it is leaving the list or its collection), then
    // the waiting ones, in mdi_data.db of the collection open (MDI-C28)
    if (!openMdiMainConnection())
        return;
    QList<MdiData::PendingJob> jobs;
    if (m_mdiJob && !m_mdiQuickHashOnly && !m_mdiLeaving && !m_mdiKeepPending) {
        MdiData::PendingJob running = m_mdiRunning;
        running.paused = m_mdiIsPaused;
        jobs.append(running);
    }
    jobs += m_mdiWaiting;
    for (const MdiData::PendingJob &held : std::as_const(m_mdiHeld))
        if (std::none_of(jobs.cbegin(), jobs.cend(), [&held](const MdiData::PendingJob &j) { return j.sameWork(held); }))
            jobs.append(held);
    if (!MdiData::savePendingJobs(kMdiMainConnection, jobs))
        qWarning() << "Enrichment list not saved in" << mdiDataPath();
}
//----------------------------------------------------------------------
void AppManager::restoreMdiJobs()
{
    if (getReadOnly()) return;   // read-only collection (D43)
    // Jobs not finished when this collection was last open (MDI-F12): one
    // paused then, or using a provider not on this computer, comes back
    // paused (D6, D31); the others continue by themselves, and one whose
    // drive is away waits for it (MDI-F91). Resuming is incremental (MDI-F13).
    if (!openMdiMainConnection())
        return;
    QList<MdiData::PendingJob> jobs = MdiData::pendingJobs(kMdiMainConnection);
    if (jobs.isEmpty())
        return;
    // Those whose source is reachable first, in their order (MDI-F92)
    std::stable_partition(jobs.begin(), jobs.end(),
                          [this](const MdiData::PendingJob &job) { return mdiJobReachable(job); });
    const QString host = QUrl(getEnrichmentTools().value(QStringLiteral("ollamaUrl")).toString()).host();
    const bool localProvider = host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1")
                               || host == QLatin1String("::1");
    bool notStarted = false;
    for (MdiData::PendingJob job : jobs) {
        job.paused = job.paused || !localProvider;
        const QString error = startMdiJob(job, true);
        if (!error.isEmpty()) {
            notStarted = true;
            holdOrDropMdiJob(job, error);
        }
    }
    if (notStarted)
        saveMdiJobs();
    // Reference documents not indexed yet, while the embeddings stay local (D6)
    if (localProvider && MdiKnowledge::pendingCount(kMdiMainConnection) > 0)
        startKnowledgeIndexing();
}
//----------------------------------------------------------------------
void AppManager::holdOrDropMdiJob(const MdiData::PendingJob &job, const QString &error)
{
    // A pending job leaves the list only when its catalog no longer exists;
    // otherwise it is kept and tried again at the next opening (MDI-F12)
    Device device;
    device.ID = job.deviceId;
    device.loadDevice(m_connectionName);
    if (device.type == QLatin1String("Catalog")) {
        if (std::none_of(m_mdiHeld.cbegin(), m_mdiHeld.cend(), [&job](const MdiData::PendingJob &h) { return h.sameWork(job); }))
            m_mdiHeld.append(job);
        qWarning() << "Enrichment kept pending for device" << job.deviceId << ":" << error;
    } else {
        qWarning() << "Enrichment removed for device" << job.deviceId << ": the catalog no longer exists";
    }
}
//----------------------------------------------------------------------
// Domain context (fork, D33)
QVariantMap AppManager::getContext()
{
    if (!openMdiMainConnection())
        return QVariantMap();
    return MdiContext::load(kMdiMainConnection, MdiContext::kCollectionScope).toVariantMap();
}
//----------------------------------------------------------------------
bool AppManager::saveContext(const QVariantMap &context)
{
    if (getReadOnly()) return false;   // read-only collection (D43)
    // A changed context re-runs nothing by itself (MDI-F99): new files use it,
    // the others are re-interpreted on request
    return openMdiMainConnection()
        && MdiContext::save(kMdiMainConnection, MdiContext::kCollectionScope, QJsonObject::fromVariantMap(context));
}
//----------------------------------------------------------------------
QString AppManager::getCatalogNotes(int deviceId)
{
    Device device;
    device.ID = deviceId;
    device.loadDevice(m_connectionName);
    if (device.type != QLatin1String("Catalog") || !openMdiMainConnection())
        return QString();
    return MdiContext::load(kMdiMainConnection, device.externalID).value("notes").toString();
}
//----------------------------------------------------------------------
bool AppManager::saveCatalogNotes(int deviceId, const QString &notes)
{
    if (getReadOnly()) return false;   // read-only collection (D43)
    Device device;
    device.ID = deviceId;
    device.loadDevice(m_connectionName);
    if (device.type != QLatin1String("Catalog") || !openMdiMainConnection())
        return false;
    if (notes.trimmed().isEmpty())
        return MdiContext::remove(kMdiMainConnection, device.externalID);
    return MdiContext::save(kMdiMainConnection, device.externalID, QJsonObject{{"notes", notes.trimmed()}});
}
//----------------------------------------------------------------------
QVariantList AppManager::contextCatalogs()
{
    // Every catalog once: no pass is ever recorded under this name. A catalog
    // shown in a Virtual group has a device row there too; the row in the
    // Physical group (parent not Virtual) stands for it.
    QHash<int, int>     parentById;
    QHash<int, QString> typeById;
    for (const Device::DeviceTreeNode &node : Device::loadDeviceTree(m_connectionName)) {
        parentById.insert(node.id, node.parentId);
        typeById.insert(node.id, node.type);
    }
    auto isCopy = [&](int deviceId) {
        return typeById.value(parentById.value(deviceId)) == QLatin1String("Virtual");
    };
    QHash<int, int> indexByCatalog;
    QVariantList list;
    for (const QVariantMap &c : MdiFts::catalogsNeedingPass(m_connectionName, mdiDataPath(), QStringLiteral("context-list"))) {
        const int deviceId  = c.value(QStringLiteral("deviceId")).toInt();
        const int catalogId = c.value(QStringLiteral("catalogId")).toInt();
        const QVariantMap item{{QStringLiteral("deviceId"), deviceId},
                               {QStringLiteral("name"),     c.value(QStringLiteral("name"))},
                               {QStringLiteral("path"),     c.value(QStringLiteral("sourcePath"))}};
        const auto listed = indexByCatalog.constFind(catalogId);
        if (listed == indexByCatalog.constEnd()) {
            indexByCatalog.insert(catalogId, list.size());
            list.append(item);
        } else if (isCopy(list.at(*listed).toMap().value(QStringLiteral("deviceId")).toInt()) && !isCopy(deviceId)) {
            list[*listed] = item;
        }
    }
    return list;
}
//----------------------------------------------------------------------
QVariantMap AppManager::homeOverview()
{
    QVariantMap overview;
    QVariantList drives;
    qint64 totalFiles = 0;
    int aiFiles = 0;
    const bool mdi = openMdiMainConnection();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (const QVariant &c : contextCatalogs()) {
        Device device;
        device.ID = c.toMap().value(QStringLiteral("deviceId")).toInt();
        device.loadDevice(m_connectionName);
        if (device.type != QLatin1String("Catalog"))
            continue;
        QVariantMap drive;
        drive[QStringLiteral("deviceId")]  = device.ID;
        drive[QStringLiteral("name")]      = device.name;
        drive[QStringLiteral("path")]      = device.path;
        // A catalog of saved search results has no drive behind it (path "EXPORT")
        drive[QStringLiteral("isDrive")]   = QDir::isAbsolutePath(device.path);
        drive[QStringLiteral("connected")] = mdiSourceReachable(device.ID);
        drive[QStringLiteral("files")]     = device.totalFileCount;
        drive[QStringLiteral("size")]      = formatDataSize(device.totalFileSize);
        drive[QStringLiteral("updated")]   = QLocale().toString(device.dateTimeUpdated.date(), QLocale::ShortFormat);
        // The drives and groups that hold it, so Browse can list what a choice holds
        QVariantList ancestors;
        for (int parentId = device.parentID, guard = 0; parentId > 0 && guard < 32; ++guard) {
            ancestors << parentId;
            Device parent;
            parent.ID = parentId;
            parent.loadDevice(m_connectionName);
            parentId = parent.parentID;
        }
        drive[QStringLiteral("ancestors")] = ancestors;

        // Files by type (once per catalog state) and files with AI results (at
        // most every 15 s) are counted in the background: Home shows what is
        // known and refreshes when the counts arrive (MDI-C38)
        const QString stamp = device.catalog ? device.catalog->dateUpdated.toString(Qt::ISODate) : QString();
        const auto types = m_homeTypeCache.constFind(device.ID);
        const auto ai    = m_homeAiCache.constFind(device.ID);
        const bool typesStale = types == m_homeTypeCache.constEnd() || types->first != stamp;
        const bool aiStale    = ai == m_homeAiCache.constEnd() || now - ai->first > 15;
        if ((typesStale || aiStale) && device.catalog)
            countHomeDrive(device.ID, device.externalID, stamp, device.catalog->filePath, typesStale);
        drive[QStringLiteral("types")]   = types != m_homeTypeCache.constEnd() ? types->second : QVariantMap();
        drive[QStringLiteral("aiFiles")] = ai != m_homeAiCache.constEnd() ? ai->second : 0;

        totalFiles += device.totalFileCount;
        aiFiles    += drive.value(QStringLiteral("aiFiles")).toInt();
        drives.append(drive);
    }
    overview[QStringLiteral("drives")]     = drives;
    overview[QStringLiteral("totalFiles")] = totalFiles;
    overview[QStringLiteral("aiFiles")]    = aiFiles;

    // The archive's context and knowledge base
    const QJsonObject context = mdi ? MdiContext::load(kMdiMainConnection, MdiContext::kCollectionScope) : QJsonObject();
    overview[QStringLiteral("hasContext")]   = !MdiContext::isEmpty(context);
    overview[QStringLiteral("organisation")] = context.value("organisation").toString();
    overview[QStringLiteral("period")]       = context.value("period").toString();
    overview[QStringLiteral("terms")]        = int(context.value("glossary").toArray().size());
    overview[QStringLiteral("categories")]   = int(context.value("categories").toArray().size());
    overview[QStringLiteral("names")]        = int(context.value("entities").toArray().size());
    int passages = 0;
    const QVariantList documents = mdi ? MdiKnowledge::documents(kMdiMainConnection) : QVariantList();
    for (const QVariant &d : documents)
        passages += d.toMap().value(QStringLiteral("passages")).toInt();
    overview[QStringLiteral("documents")] = int(documents.size());
    overview[QStringLiteral("passages")]  = passages;
    return overview;
}
//----------------------------------------------------------------------
void AppManager::countHomeDrive(int deviceId, int catalogId, const QString &stamp, const QString &catalogFile, bool types)
{
    // Counted on a worker thread with its own connections, one catalog at a
    // time: the UI thread never reads a file list (MDI-C38)
    if (m_homeCounting.contains(deviceId))
        return;
    m_homeCounting.insert(deviceId);
    m_homeQueue.append({deviceId, catalogId, stamp, catalogFile, types});
    runNextHomeCount();
}
//----------------------------------------------------------------------
void AppManager::runNextHomeCount()
{
    if (m_homeWorkerBusy || m_homeQueue.isEmpty())
        return;
    m_homeWorkerBusy = true;
    const HomeCount job = m_homeQueue.takeFirst();
    const QString mode   = collection->databaseMode;
    const QString dbFile = collection->databaseFilePath;
    const QString mdiDb  = mdiDataPath();
    const int generation = m_homeGeneration;
    QThread *worker = QThread::create([this, job, mode, dbFile, mdiDb, generation]() {
        const QString name = QStringLiteral("home_count_%1").arg(job.deviceId);
        QVariantMap counts;
        if (job.types && mode != QLatin1String("Hosted")) {
            {
                if (mode == QLatin1String("File")) {
                    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
                    db.setDatabaseName(dbFile);
                    db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
                    db.open();
                }
                for (const MdiEnrichmentJob::FileItem &f : MdiEnrichmentJob::listCatalogFiles(name, job.catalogId, mode, job.catalogFile))
                    counts[f.fileType] = counts.value(f.fileType).toInt() + 1;
            }
            QSqlDatabase::removeDatabase(name);
        }
        int aiFiles = -1;
        const QString mdiName = name + QStringLiteral("_mdi");
        if (QFileInfo::exists(mdiDb) && MdiData::open(mdiName, mdiDb)) {
            // Files with AI results, not every file with some data: the quick
            // hash covers them all (MDI-F130)
            aiFiles = int(aiResultPaths(mdiName, job.catalogId,
                                        MdiData::catalogIndex(mdiName, job.catalogId)).size());
            MdiData::close(mdiName);
        }
        QMetaObject::invokeMethod(this, [this, job, counts, aiFiles, generation]() {
            m_homeWorkerBusy = false;
            // Counted for a collection, or device numbers, no longer current
            if (generation == m_homeGeneration) {
                bool changed = false;
                if (job.types) {
                    const auto old = m_homeTypeCache.constFind(job.deviceId);
                    changed = old == m_homeTypeCache.constEnd() || old->first != job.stamp || old->second != counts;
                    m_homeTypeCache.insert(job.deviceId, {job.stamp, counts});
                }
                // No AI store (none yet, Hosted): no file with AI results. Cached
                // all the same, or Home would ask for this count again at once.
                const int ai = qMax(aiFiles, 0);
                const auto oldAi = m_homeAiCache.constFind(job.deviceId);
                changed = changed || oldAi == m_homeAiCache.constEnd() || oldAi->second != ai;
                m_homeAiCache.insert(job.deviceId, {QDateTime::currentSecsSinceEpoch(), ai});
                m_homeCounting.remove(job.deviceId);
                // Home refreshes only for a count that changed: a refresh asks
                // for the stale counts again
                if (changed)
                    emit homeOverviewChanged();
            }
            runNextHomeCount();
        }, Qt::QueuedConnection);
    });
    m_homeWorker = worker;
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}
//----------------------------------------------------------------------
void AppManager::waitForHomeCount()
{
    // On quit (and destruction): the worker uses its own connections and
    // calls back into this object
    m_homeQueue.clear();
    if (m_homeWorker)
        m_homeWorker->wait(10000);
}
//----------------------------------------------------------------------
void AppManager::draftContext(int deviceId)
{
    if (getReadOnly()) { emit contextDraftReady(QVariantMap(), readOnlyRefusal()); return; }   // read-only collection (D43)
    // What the draft reads: the catalog's folders (by number of files) and a
    // sample of its descriptions, gathered here; the model answers on a
    // worker thread (MDI-F97). Nothing is saved: the page fills its fields.
    Device device;
    device.ID = deviceId;
    device.loadDevice(m_connectionName);
    if (device.type != QLatin1String("Catalog") || !openMdiMainConnection()) {
        emit contextDraftReady(QVariantMap(), QStringLiteral("not a catalog"));
        return;
    }
    QHash<QString, int> folderCount;
    for (const MdiEnrichmentJob::FileItem &f : MdiEnrichmentJob::listCatalogFiles(m_connectionName, device.externalID,
                                                                                 collection->databaseMode, device.catalog->filePath)) {
        QString folder = QFileInfo(f.fullPath).path();
        if (folder.startsWith(device.path))
            folder = folder.mid(device.path.size());
        // Three levels are enough to see the archive's structure
        folder = folder.section('/', 0, 3, QString::SectionSkipEmpty);
        if (!folder.isEmpty())
            folderCount[folder]++;
    }
    QList<QPair<int, QString>> ranked;
    for (auto it = folderCount.cbegin(); it != folderCount.cend(); ++it)
        ranked.append({it.value(), it.key()});
    std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QStringList folders;
    for (int i = 0; i < ranked.size() && i < 200; ++i)
        folders << QStringLiteral("%1 (%2 files)").arg(ranked.at(i).second).arg(ranked.at(i).first);

    QStringList samples;
    for (const QString &analysis : {QStringLiteral("caption"), QStringLiteral("summary"), QStringLiteral("snippet")}) {
        const QList<MdiData::Datum> data = MdiData::forCatalog(kMdiMainConnection, device.externalID, analysis);
        const int step = qMax(1, int(data.size()) / 25);
        for (int i = 0; i < data.size(); i += step) {
            const QJsonObject &v = data.at(i).value;
            const QString text = analysis == QLatin1String("caption") ? v.value("caption").toString()
                               : analysis == QLatin1String("summary") ? v.value("summary").toString()
                               : v.value("text").toString().left(300);
            if (!text.isEmpty())
                samples << QStringLiteral("- ") + QFileInfo(data.at(i).fullPath).fileName() + QStringLiteral(": ") + text;
        }
    }

    const QVariantMap tools = getEnrichmentTools();
    const QString url   = tools.value(QStringLiteral("ollamaUrl")).toString();
    const QString model = tools.value(QStringLiteral("textModel")).toString();
    const QString prompt = MdiContext::draftPrompt(folders, samples,
                                                   QLocale::languageToString(QLocale(getCurrentLanguage()).language()));
    QThread *worker = QThread::create([this, url, model, prompt]() {
        QString error;
        const QJsonObject answer = MdiContext::askText(url, model, prompt, &error);
        const QVariantMap draft = answer.isEmpty() ? QVariantMap() : MdiContext::parseDraft(answer).toVariantMap();
        QMetaObject::invokeMethod(this, [this, draft, error]() { emit contextDraftReady(draft, error); },
                                  Qt::QueuedConnection);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}
//----------------------------------------------------------------------
QVariantMap AppManager::reinterpretEstimate(int deviceId, const QString &folder, const QString &fileType)
{
    // Files in the scope, at the pace of the classifications seen in this
    // session (else a typical local pace)
    Device device;
    device.ID = deviceId;
    device.loadDevice(m_connectionName);
    if (device.type != QLatin1String("Catalog"))
        return QVariantMap();
    const QString prefix = folder.isEmpty() || folder.endsWith('/') ? folder : folder + '/';
    int files = 0;
    for (const MdiEnrichmentJob::FileItem &f : MdiEnrichmentJob::listCatalogFiles(m_connectionName, device.externalID,
                                                                                 collection->databaseMode, device.catalog->filePath))
        if ((prefix.isEmpty() || f.fullPath.startsWith(prefix)) && (fileType.isEmpty() || f.fileType == fileType))
            ++files;
    double total = 0;
    int count = 0;
    for (const QVariant &v : std::as_const(m_mdiStepLog)) {
        const QVariantMap e = v.toMap();
        if (e.value(QStringLiteral("analysis")).toString() == QLatin1String("classification")
            && e.value(QStringLiteral("outcome")).toString() == QLatin1String("completed")) {
            total += e.value(QStringLiteral("durationMs")).toDouble() / 1000.0;
            ++count;
        }
    }
    // No classification measured yet: the time is unknown (MDI-F103)
    const qint64 seconds = count > 0 ? qRound64(files * (total / count + 0.5)) : -1;   // + the embeddings refresh
    return QVariantMap{{QStringLiteral("files"), files},
                       {QStringLiteral("seconds"), seconds}};
}
//----------------------------------------------------------------------
QVariantList AppManager::knowledgeDocuments()
{
    return openMdiMainConnection() ? MdiKnowledge::documents(kMdiMainConnection) : QVariantList();
}
//----------------------------------------------------------------------
int AppManager::addKnowledgePaths(const QVariantList &urls)
{
    if (getReadOnly()) return 0;   // read-only collection (D43)
    // Referenced, never copied or modified (MDI-F104)
    QStringList paths;
    for (const QVariant &u : urls) {
        const QUrl url = u.toUrl();
        paths << (url.isLocalFile() ? url.toLocalFile() : u.toString());
    }
    if (!openMdiMainConnection())
        return 0;
    const int added = MdiKnowledge::addPaths(kMdiMainConnection, paths);
    if (added > 0)
        startKnowledgeIndexing();
    emit knowledgeStateChanged();
    return added;
}
//----------------------------------------------------------------------
bool AppManager::removeKnowledgeDocument(int documentId)
{
    if (getReadOnly()) return false;   // read-only collection (D43)
    // From the knowledge base only: the file itself stays where it is
    const bool removed = openMdiMainConnection() && MdiKnowledge::removeDocument(kMdiMainConnection, documentId);
    emit knowledgeStateChanged();
    return removed;
}
//----------------------------------------------------------------------
void AppManager::startKnowledgeIndexing()
{
    if (getReadOnly()) return;   // read-only collection (D43)
    // Its own runner instance and thread: enrichment keeps going (MDI-C33)
    if (m_kbJob) {
        m_kbPending = true;
        return;
    }
    const QVariantMap tools = getEnrichmentTools();
    MdiEnrichmentJob::Config config;
    config.knowledgeOnly = true;
    config.dataDbPath    = mdiDataPath();
    config.pdftotextPath = tools.value(QStringLiteral("pdftotext")).toString();
    config.ollamaUrl     = tools.value(QStringLiteral("ollamaUrl")).toString();
    config.embedModel    = tools.value(QStringLiteral("embedModel")).toString();
    m_kbPending = false;
    m_kbJob    = new MdiEnrichmentJob(config, {});
    m_kbThread = new QThread();
    m_kbJob->moveToThread(m_kbThread);
    connect(m_kbThread, &QThread::started, m_kbJob, &MdiEnrichmentJob::run);
    connect(m_kbJob, &MdiEnrichmentJob::progress, this,
            [this](int index, int count, const QString &, const QString &path) {
        m_kbStatusText = StatusBarMessageBuilder().setOperation(mdiAnalysisLabel(QStringLiteral("knowledge")))
                             .setStatus(tr("In Progress")).setProcess(tr("Loaded"), index, count)
                             .setCurrentItem(path).build();
        emit knowledgeStateChanged();
    });
    connect(m_kbJob, &MdiEnrichmentJob::finished, this, [this](const QVariantMap &summary) {
        m_kbStatusText = StatusBarMessageBuilder().setOperation(mdiAnalysisLabel(QStringLiteral("knowledge")))
                             .setStatus(summary.value(QStringLiteral("stopped")).toBool() ? tr("Cancelled") : tr("Completed"))
                             .setResult(tr("Completed"), summary.value(QStringLiteral("completed")).toInt()).build();
        m_kbJob = nullptr;   // deleted with its thread
        emit knowledgeStateChanged();
        if (m_kbPending)
            QTimer::singleShot(0, this, &AppManager::startKnowledgeIndexing);
    });
    connect(m_kbJob,    &MdiEnrichmentJob::finished, m_kbThread, &QThread::quit);
    connect(m_kbThread, &QThread::finished, m_kbJob,    &QObject::deleteLater);
    connect(m_kbThread, &QThread::finished, m_kbThread, &QObject::deleteLater);
    connect(m_kbThread, &QThread::finished, this, [this]() { m_kbThread = nullptr; });
    connect(qApp, &QCoreApplication::aboutToQuit, m_kbJob, [this]() { stopKnowledgeIndexing(); });
    m_kbThread->start();
    emit knowledgeStateChanged();
}
//----------------------------------------------------------------------
void AppManager::stopKnowledgeIndexing()
{
    // The document being read stays pending for next time
    m_kbPending = false;
    if (m_kbJob)
        m_kbJob->requestStop();
    if (m_kbThread) {
        m_kbThread->quit();
        m_kbThread->wait(5000);
    }
}
//----------------------------------------------------------------------
// Activity page (MDI-F61..F75)
void AppManager::setActivityMonitoring(bool visible)
{
    m_activityVisible = visible;
    updateMonitoring();
}
//----------------------------------------------------------------------
void AppManager::updateMonitoring()
{
    // Samples only while someone can see them or a job runs: the monitor
    // starts short system programs every two seconds.
    const bool wanted = m_activityVisible || m_mdiJob != nullptr;
    if (wanted && !m_monitor) {
        m_monitor       = new MdiSystemMonitor();
        m_monitorThread = new QThread();
        m_monitor->moveToThread(m_monitorThread);
        connect(m_monitorThread, &QThread::finished, m_monitor, &QObject::deleteLater);
        connect(m_monitorThread, &QThread::finished, m_monitorThread, &QObject::deleteLater);
        connect(m_monitor, &MdiSystemMonitor::snapshot, this, [this](const QVariantMap &snapshot) {
            // A waiting catalog's drive may be connected while the job waits (MDI-F92)
            if (m_mdiWaitingForSource)
                stepAsideIfWaiting();
            m_mdiSystem = snapshot;
            QVariantMap point;
            point[QStringLiteral("t")]   = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
            point[QStringLiteral("cpu")] = snapshot.value(QStringLiteral("cpu"), -1);
            point[QStringLiteral("gpu")] = snapshot.value(QStringLiteral("gpu"), -1);
            const double total = snapshot.value(QStringLiteral("memTotalGb")).toDouble();
            point[QStringLiteral("mem")] = total > 0
                ? qRound(100.0 * snapshot.value(QStringLiteral("memUsedGb")).toDouble() / total) : -1;
            m_mdiHistory.append(point);
            // Session only (D21): the last 30 minutes at one sample every 2 s
            while (m_mdiHistory.size() > 900)
                m_mdiHistory.removeFirst();
            emit mdiSystemChanged();
        });
        m_monitorThread->start();
        QMetaObject::invokeMethod(m_monitor, "setOllamaUrl", Q_ARG(QString, getEnrichmentTools().value(QStringLiteral("ollamaUrl")).toString()));
        QMetaObject::invokeMethod(m_monitor, "start", Q_ARG(int, 2000));
    } else if (!wanted && m_monitor) {
        QMetaObject::invokeMethod(m_monitor, "stop");
        m_monitorThread->quit();
        m_monitor       = nullptr;   // deleted with its thread
        m_monitorThread = nullptr;
    }
}
//----------------------------------------------------------------------
void AppManager::onMdiActivity(const QVariantMap &step)
{
    if (m_mdiKeepPending)
        return;   // the job of a collection being left shows nothing (MDI-F93)
    const QString event = step.value(QStringLiteral("event")).toString();
    if (event == QLatin1String("waiting")) {
        // A drive is away: the job waits for it (MDI-F91)
        m_mdiWaitingForSource = step.value(QStringLiteral("waiting")).toBool();
        m_mdiWaitingPath      = step.value(QStringLiteral("drivePath")).toString();
        m_mdiStatusText = mdiStateStatus();
        emit mdiStatusTextChanged();
        emit mdiStateChanged();
        emit mdiStepChanged();
        stepAsideIfWaiting();
    } else if (event == QLatin1String("start")) {
        m_mdiStep = step;
        m_mdiStep[QStringLiteral("label")]    = mdiAnalysisLabel(step.value(QStringLiteral("analysis")).toString());
        m_mdiStep[QStringLiteral("fileName")] = QFileInfo(step.value(QStringLiteral("filePath")).toString()).fileName();
        emit mdiStepChanged();
    } else if (event == QLatin1String("progress")) {
        m_mdiStep[QStringLiteral("progress")] = step.value(QStringLiteral("progress"));
        m_mdiStep[QStringLiteral("detail")]   = step.value(QStringLiteral("detail"));
        emit mdiStepChanged();
    } else if (event == QLatin1String("end")) {
        QVariantMap entry = step;
        entry[QStringLiteral("label")]    = mdiAnalysisLabel(step.value(QStringLiteral("analysis")).toString());
        entry[QStringLiteral("fileName")] = step.value(QStringLiteral("filePath")).toString().isEmpty()
                                            ? m_mdiCatalogName
                                            : QFileInfo(step.value(QStringLiteral("filePath")).toString()).fileName();
        entry[QStringLiteral("time")]     = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
        entry[QStringLiteral("catalog")]  = m_mdiCatalogName;
        m_mdiStepLog.prepend(entry);
        while (m_mdiStepLog.size() > 500)
            m_mdiStepLog.removeLast();
        m_mdiStep.clear();
        emit mdiStepLogChanged();
        emit mdiStepChanged();
    }
}
//----------------------------------------------------------------------
QVariantMap AppManager::getMdiJobInfo() const
{
    QVariantMap info;
    info[QStringLiteral("running")] = m_mdiJob != nullptr;
    info[QStringLiteral("paused")]  = m_mdiIsPaused;
    info[QStringLiteral("catalog")] = m_mdiCatalogName;
    if (m_mdiJob) {
        const int index = m_mdiStep.value(QStringLiteral("fileIndex")).toInt();
        const int count = m_mdiStep.value(QStringLiteral("fileCount")).toInt();
        info[QStringLiteral("fileIndex")] = index;
        info[QStringLiteral("fileCount")] = count;
        info[QStringLiteral("startedAt")] = m_mdiJobStart.toString(QStringLiteral("HH:mm"));
        // Estimate from the pace of the files already done
        const qint64 elapsed = m_mdiJobStart.secsTo(QDateTime::currentDateTime());
        if (index > 1 && count >= index && elapsed > 0 && !m_mdiWaitingForSource) {
            const qint64 remaining = elapsed * (count - index + 1) / (index - 1);
            info[QStringLiteral("endsAt")] = QDateTime::currentDateTime().addSecs(remaining).toString(QStringLiteral("HH:mm"));
        }
        info[QStringLiteral("sourceConnected")] = !m_mdiSourcePath.isEmpty() && QFileInfo::exists(m_mdiSourcePath);
        info[QStringLiteral("drivePath")]        = m_mdiWaitingPath;
        info[QStringLiteral("waitingForSource")] = m_mdiWaitingForSource;
    }
    QStringList waiting;
    QList<MdiData::PendingJob> listed = m_mdiWaiting;
    for (const MdiData::PendingJob &held : m_mdiHeld)
        if (std::none_of(listed.cbegin(), listed.cend(), [&held](const MdiData::PendingJob &j) { return j.sameWork(held); }))
            listed.append(held);
    for (const MdiData::PendingJob &job : std::as_const(listed)) {
        Device d;
        d.ID = job.deviceId;
        d.loadDevice(m_connectionName);
        waiting << (job.kind == QLatin1String("reinterpret")
                    ? d.name + QStringLiteral(" · ") + mdiAnalysisLabel(QStringLiteral("classification"))
                    : job.kind == QLatin1String("faces") || job.kind == QLatin1String("dating")
                      || job.kind == QLatin1String("document")
                    ? d.name + QStringLiteral(" · ") + mdiAnalysisLabel(job.kind) : d.name);
    }
    info[QStringLiteral("waiting")] = waiting;
    // Where data went in this session: anything not on this Mac is egress
    QStringList remoteHosts;
    for (const QVariant &v : m_mdiStepLog) {
        const QVariantMap e = v.toMap();
        if (e.value(QStringLiteral("where")).toString() != QLatin1String("local"))
            remoteHosts << e.value(QStringLiteral("tool")).toString();
    }
    remoteHosts.removeDuplicates();
    info[QStringLiteral("egress")] = remoteHosts;
    return info;
}
//----------------------------------------------------------------------
QVariantMap AppManager::mdiToolsInfo()
{
    // The paths the analysis uses: the settings, the computer, then the
    // programs that travel inside the portable app
    const QVariantMap tools = getEnrichmentTools();
    QVariantMap configured;
    for (const QString &key : {QStringLiteral("ffmpeg"), QStringLiteral("ffprobe"), QStringLiteral("pdftotext"),
                               QStringLiteral("whisper"), QStringLiteral("whisperModel")})
        configured[key] = tools.value(key);
    QVariantMap info = MdiSystemMonitor::toolsInfo(configured);
    info[QStringLiteral("ollamaUrl")]   = tools.value(QStringLiteral("ollamaUrl"));
    info[QStringLiteral("visionModel")] = tools.value(QStringLiteral("visionModel"));
    info[QStringLiteral("textModel")]   = tools.value(QStringLiteral("textModel"));
    info[QStringLiteral("embedModel")]  = tools.value(QStringLiteral("embedModel"));
    // Voice activity (transcripts) and faces (fork, 2026-10-30)
    info[QStringLiteral("whisperVadModel")]     = tools.value(QStringLiteral("whisperVadModel"));
    info[QStringLiteral("faceDetectorModel")]   = tools.value(QStringLiteral("faceDetectorModel"));
    info[QStringLiteral("faceRecognizerModel")] = tools.value(QStringLiteral("faceRecognizerModel"));
    info[QStringLiteral("opencv")]              = tools.value(QStringLiteral("opencv"));
    return info;
}
//----------------------------------------------------------------------
void AppManager::scheduleQuickHashes()
{
    if (getReadOnly()) return;   // read-only collection (D43)
    // Right after a creation or an update, while the disk is still connected:
    // every catalog whose quick hashes do not cover its current state, when
    // its source is reachable (owner's decision; SQLite collections)
    if (collection->databaseMode == QLatin1String("Hosted"))
        return;
    for (const QVariantMap &c : MdiFts::catalogsNeedingPass(m_connectionName, mdiDataPath(), QStringLiteral("quickhash"))) {
        const QString source = c.value(QStringLiteral("sourcePath")).toString();
        if (source.isEmpty() || !QFileInfo::exists(source))
            continue;
        startQuickHash(c.value(QStringLiteral("deviceId")).toInt());
    }
}
//----------------------------------------------------------------------
void AppManager::scheduleFtsIndexing()
{
    if (getReadOnly()) return;   // read-only collection (D43)
    scheduleQuickHashes();
    if (collection->databaseMode == QLatin1String("Hosted"))
        return;   // FTS5 is SQLite only; Hosted searches as before
    if (m_ftsThread) {
        m_ftsPending = true;   // run again when the current pass ends
        return;
    }
    const QList<QVariantMap> stale = MdiFts::staleCatalogs(m_connectionName, mdiDataPath());
    if (stale.isEmpty())
        return;

    const QString mdiPath  = mdiDataPath();
    const QString mode     = collection->databaseMode;
    const QString dbPath   = collection->databaseFilePath;
    m_ftsStop = false;
    m_ftsThread = QThread::create([this, stale, mdiPath, mode, dbPath]() {
        for (const QVariantMap &c : stale) {
            if (m_ftsStop.load())
                break;
            QElapsedTimer clock;
            clock.start();
            QString error;
            const bool ok = MdiFts::rebuildCatalog(mdiPath, c.value(QStringLiteral("catalogId")).toInt(),
                                                   c.value(QStringLiteral("stamp")).toString(), mode, dbPath,
                                                   c.value(QStringLiteral("filePath")).toString(), &error, &m_ftsStop);
            // Reported on the Activity page like an enrichment step
            QVariantMap step;
            step[QStringLiteral("event")]      = QStringLiteral("end");
            step[QStringLiteral("analysis")]   = QStringLiteral("fts");
            step[QStringLiteral("filePath")]   = QString();
            step[QStringLiteral("catalog")]    = c.value(QStringLiteral("name"));
            step[QStringLiteral("tool")]       = QStringLiteral("sqlite fts5");
            step[QStringLiteral("where")]      = QStringLiteral("local");
            step[QStringLiteral("outcome")]    = ok ? QStringLiteral("completed") : QStringLiteral("failed");
            step[QStringLiteral("reason")]     = error;
            step[QStringLiteral("durationMs")] = clock.elapsed();
            QMetaObject::invokeMethod(this, [this, step]() {
                QVariantMap entry = step;
                entry[QStringLiteral("label")]    = mdiAnalysisLabel(QStringLiteral("fts"));
                entry[QStringLiteral("fileName")] = step.value(QStringLiteral("catalog"));
                entry[QStringLiteral("time")]     = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
                m_mdiStepLog.prepend(entry);
                emit mdiStepLogChanged();
            }, Qt::QueuedConnection);
        }
    });
    connect(m_ftsThread, &QThread::finished, this, [this]() {
        m_ftsThread->deleteLater();
        m_ftsThread = nullptr;
        if (m_ftsPending && !m_ftsStop.load()) {
            m_ftsPending = false;
            scheduleFtsIndexing();
        }
    });
    m_ftsThread->start(QThread::LowPriority);
}
//----------------------------------------------------------------------
void AppManager::stopFtsIndexing()
{
    m_ftsPending = false;
    if (!m_ftsThread)
        return;
    m_ftsStop = true;
    m_ftsThread->wait(10000);   // rebuilds commit in batches and check the flag between them
}
//----------------------------------------------------------------------
void AppManager::bumpMdiRevision(int catalogId)
{
    // catalogId < 0: every catalog's data may have changed
    m_mdiRevision++;
    if (catalogId < 0)
        m_mdiAllRevision = m_mdiRevision;
    else
        m_mdiCatalogRevision.insert(catalogId, m_mdiRevision);
    emit mdiRevisionChanged();
}
//----------------------------------------------------------------------
void AppManager::refreshMdiIndex(int catalogId)
{
    // Reloaded when another catalog is shown, or when this one's data changed
    // since it was read: a job enriching another catalog leaves it alone
    const int changedAt = qMax(m_mdiAllRevision, m_mdiCatalogRevision.value(catalogId, 0));
    if (catalogId == m_mdiIndexCatalogId && m_mdiIndexRevision >= changedAt)
        return;
    m_mdiIndexCatalogId = catalogId;
    m_mdiIndexRevision  = m_mdiRevision;
    m_mdiIndex.clear();
    m_mdiAiPaths.clear();
    if (catalogId > 0 && QFileInfo::exists(mdiDataPath()) && openMdiMainConnection()) {
        m_mdiIndex   = MdiData::catalogIndex(kMdiMainConnection, catalogId);
        m_mdiAiPaths = aiResultPaths(kMdiMainConnection, catalogId, m_mdiIndex);
    }
}
//----------------------------------------------------------------------
QSet<QString> AppManager::aiResultPaths(const QString &mdiConnection, int catalogId,
                                        const QHash<QString, QVariantMap> &catalogIndex)
{
    QSet<QString> paths;
    for (auto it = catalogIndex.cbegin(); it != catalogIndex.cend(); ++it)
        if (it->contains(QStringLiteral("caption")) || it->contains(QStringLiteral("summary")))
            paths.insert(it.key());
    for (const QString &analysis : {QStringLiteral("transcript"), QStringLiteral("classification")})
        for (const MdiData::Datum &d : MdiData::forCatalog(mdiConnection, catalogId, analysis))
            paths.insert(d.fullPath);
    return paths;
}
//----------------------------------------------------------------------
QString AppManager::mdiPreviewUrl(int catalogId, const QString &fullPath, int revision)
{
    Q_UNUSED(revision)
    refreshMdiIndex(catalogId);
    const QString thumbnail = m_mdiIndex.value(fullPath).value(QStringLiteral("thumbnail")).toString();
    // Offline drive: the preview lives on the destination drive, which may be
    // disconnected too; then the file-type icon shows instead (MDI-F45)
    if (thumbnail.isEmpty() || !QFileInfo::exists(thumbnail))
        return QString();
    return QUrl::fromLocalFile(thumbnail).toString();
}
//----------------------------------------------------------------------
bool AppManager::mdiHasData(int catalogId, const QString &fullPath, int revision)
{
    Q_UNUSED(revision)
    refreshMdiIndex(catalogId);
    return m_mdiIndex.value(fullPath).value(QStringLiteral("hasData")).toBool();
}
//----------------------------------------------------------------------
bool AppManager::mdiHasAi(int catalogId, const QString &fullPath, int revision)
{
    Q_UNUSED(revision)
    refreshMdiIndex(catalogId);
    return m_mdiAiPaths.contains(fullPath);
}
//----------------------------------------------------------------------
QVariantList AppManager::mdiDetails(int catalogId, const QString &fullPath)
{
    QVariantList list;
    if (!QFileInfo::exists(mdiDataPath()) || !openMdiMainConnection())
        return list;
    QList<MdiData::Datum> data = MdiData::forFile(kMdiMainConnection, catalogId, fullPath);
    std::sort(data.begin(), data.end(), [](const MdiData::Datum &a, const MdiData::Datum &b) {
        return kMdiAnalysisOrder.indexOf(a.analysis) < kMdiAnalysisOrder.indexOf(b.analysis);
    });
    for (const MdiData::Datum &d : data) {
        QVariantList files;
        for (const QString &f : d.derivedFiles) {
            const QString absolute = QDir(d.derivedRoot).filePath(f);
            files.append(QVariantMap{ { QStringLiteral("path"),   absolute },
                                      { QStringLiteral("url"),    QUrl::fromLocalFile(absolute).toString() },
                                      { QStringLiteral("exists"), QFileInfo::exists(absolute) } });
        }
        list.append(QVariantMap{
            { QStringLiteral("analysis"), d.analysis },
            { QStringLiteral("label"),    mdiAnalysisLabel(d.analysis) },
            { QStringLiteral("outcome"),  d.outcome },
            { QStringLiteral("reason"),   d.reason },
            { QStringLiteral("value"),    d.value.toVariantMap() },
            { QStringLiteral("files"),    files },
            { QStringLiteral("model"),    d.model },
            { QStringLiteral("tool"),     d.tool },
            { QStringLiteral("date"),     d.date }
        });
    }
    return list;
}
//----------------------------------------------------------------------
QVariantMap AppManager::exploreRowInfo(int proxyRow)
{
    QVariantMap info;
    if (!m_exploreSortModel || !m_exploreFilesModel || proxyRow < 0)
        return info;
    const QModelIndex source = m_exploreSortModel->mapToSource(m_exploreSortModel->index(proxyRow, 0));
    if (!source.isValid())
        return info;
    info[QStringLiteral("name")]       = m_exploreFilesModel->data(source, ExploreFilesModel::NameRole);
    info[QStringLiteral("folderPath")] = m_exploreFilesModel->data(source, ExploreFilesModel::FolderPathRole);
    info[QStringLiteral("fullPath")]   = m_exploreFilesModel->data(source, ExploreFilesModel::FullPathRole);
    info[QStringLiteral("entryType")]  = m_exploreFilesModel->data(source, ExploreFilesModel::EntryTypeRole);
    return info;
}
//----------------------------------------------------------------------
QVariantList AppManager::connectedDrives()
{
    QVariantList list;
    for (const QVariantMap &v : MdiVolumes::list())
        list.append(v);
    return list;
}
//----------------------------------------------------------------------
QVariantMap AppManager::mountDriveReadOnly(const QString &device)
{
    QString mountPoint, reason;
    const bool ok = MdiVolumes::mountReadOnly(device, &mountPoint, &reason);
    return QVariantMap{{QStringLiteral("ok"), ok},
                       {QStringLiteral("mountPoint"), mountPoint},
                       {QStringLiteral("reason"), reason}};
}
//----------------------------------------------------------------------
bool AppManager::getViewSetting(const QString &key) const
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    return settings.value("View/" + key, false).toBool();
}
//----------------------------------------------------------------------
void AppManager::setViewSetting(const QString &key, bool value)
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("View/" + key, value);
    settings.sync();
}
//----------------------------------------------------------------------
QVariantMap AppManager::inspectorInfo(int catalogId, const QString &fullPath)
{
    QVariantMap info;
    const QFileInfo file(fullPath);
    const QString name   = file.fileName();
    const QString folder = file.path();
    info[QStringLiteral("name")]     = name;
    info[QStringLiteral("folder")]   = folder;
    info[QStringLiteral("fullPath")] = fullPath;
    info[QStringLiteral("url")]      = QUrl::fromLocalFile(fullPath).toString();

    // Where the file lives: its catalog and the storage above it; online when
    // the file itself can be reached now
    info[QStringLiteral("online")] = file.exists();
    const QList<Device::DeviceTreeNode> tree = Device::loadDeviceTree(m_connectionName);
    QHash<int, const Device::DeviceTreeNode *> byId;
    const Device::DeviceTreeNode *catalogNode = nullptr;
    for (const Device::DeviceTreeNode &n : tree) {
        byId.insert(n.id, &n);
        if (n.type == QLatin1String("Catalog") && n.externalId == catalogId)
            catalogNode = &n;
    }
    if (catalogNode) {
        info[QStringLiteral("catalogName")] = catalogNode->name;
        const Device::DeviceTreeNode *up = byId.value(catalogNode->parentId, nullptr);
        while (up && up->type != QLatin1String("Storage"))
            up = byId.value(up->parentId, nullptr);
        info[QStringLiteral("volumeName")] = up ? up->name : catalogNode->name;
        info[QStringLiteral("volumePath")] = up ? up->path : catalogNode->path;
    }

    // Stored SHA-256 (catalogs with checksums) and extended metadata
    Catalog catalog;
    catalog.ID = catalogId;
    catalog.setConnectionName(m_connectionName);
    info[QStringLiteral("sha256")]   = catalog.getFileChecksum(name, folder);
    info[QStringLiteral("metadata")] = getFileMetadataParsedFields(catalogId, name, folder);

    // GPS position from the photo metadata, as a map link (no map library)
    const QJsonObject meta = QJsonDocument::fromJson(
        Catalog::getFileMetadataJson(catalogId, name, folder, m_connectionName).toUtf8()).object();
    double lat = 0, lon = 0;
    bool hasLat = false, hasLon = false;
    for (auto it = meta.begin(); it != meta.end(); ++it) {
        const QString key = it.key().toLower();
        if (key.contains(QLatin1String("latitude")))  { lat = it.value().toVariant().toDouble(&hasLat); }
        if (key.contains(QLatin1String("longitude"))) { lon = it.value().toVariant().toDouble(&hasLon); }
    }
    if (hasLat && hasLon && (lat != 0.0 || lon != 0.0)) {
        info[QStringLiteral("gps")] = QVariantMap{
            {QStringLiteral("lat"), lat}, {QStringLiteral("lon"), lon},
            {QStringLiteral("url"), QStringLiteral("https://www.openstreetmap.org/?mlat=%1&mlon=%2#map=16/%1/%2")
                                        .arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6)}};
    }

    // Everything the enrichment job derived (previews, snippet, archive, hashes...)
    info[QStringLiteral("analysis")] = mdiDetails(catalogId, fullPath);
    return info;
}
//----------------------------------------------------------------------
QVariantMap AppManager::mdiClusters(int catalogId)
{
    // Catalog-level datum: empty file path, analysis "clusters" (MDI-F37)
    if (!QFileInfo::exists(mdiDataPath()) || !openMdiMainConnection())
        return QVariantMap();
    const QList<MdiData::Datum> data = MdiData::forCatalog(kMdiMainConnection, catalogId, QStringLiteral("clusters"));
    if (data.isEmpty())
        return QVariantMap();
    QVariantMap result = data.first().value.toVariantMap();
    result[QStringLiteral("model")] = data.first().model;
    result[QStringLiteral("date")]  = data.first().date;
    return result;
}
//----------------------------------------------------------------------
QString AppManager::runSemanticSearch(const QString &query, bool onlyMeaning)
{
    m_semanticResults.clear();
    m_semanticActive = true;
    m_semanticOnly   = onlyMeaning;

    QString error;
    const QVariantMap tools = getEnrichmentTools();
    const QString model = tools.value(QStringLiteral("embedModel")).toString();
    if (query.trimmed().isEmpty() || !QFileInfo::exists(mdiDataPath()) || !openMdiMainConnection()) {
        emit semanticResultsChanged();
        return error;
    }

    // The query is embedded by the local model only (MDI-F50 applies to remote ones)
    const QList<QVector<float>> vectors = MdiEnrichmentJob::embedTexts(
        tools.value(QStringLiteral("ollamaUrl")).toString(), model, { query.trimmed() }, &error);
    if (vectors.isEmpty()) {
        emit semanticResultsChanged();
        return error.isEmpty() ? QStringLiteral("embeddings") : error;
    }

    // Only the catalogs of the Selection (MDI-F48); none listed = all catalogs
    QList<int> catalogIds;
    if (selectedDevice && selectedDevice->type != QLatin1String("All")) {
        Device scope;
        scope.ID = selectedDevice->ID;
        scope.loadDevice(m_connectionName);
        if (scope.type == QLatin1String("Catalog"))
            catalogIds.append(scope.externalID);
        // Same scope as the table search: the active catalogs under the selection
        const QList<Device*> catalogs = Device::getActiveCatalogList(m_connectionName, scope.ID);
        for (Device *catalogDevice : catalogs) {
            if (!catalogIds.contains(catalogDevice->externalID))
                catalogIds.append(catalogDevice->externalID);
            delete catalogDevice;
        }
        if (catalogIds.isEmpty())
            catalogIds.append(-1);   // a selection without catalogs matches nothing
    }

    const QVariantList found = MdiData::semanticSearch(kMdiMainConnection, catalogIds, vectors.first(), model);
    for (const QVariant &v : found) {
        QVariantMap row = v.toMap();
        const QFileInfo info(row.value(QStringLiteral("fullPath")).toString());
        row[QStringLiteral("fileName")]   = info.fileName();
        row[QStringLiteral("folderPath")] = info.absolutePath();
        row[QStringLiteral("preview")]    = mdiPreviewUrl(row.value(QStringLiteral("catalogId")).toInt(),
                                                          info.filePath(), m_mdiRevision);
        m_semanticResults.append(row);
    }
    emit semanticResultsChanged();
    return QString();
}
//----------------------------------------------------------------------
void AppManager::clearSemanticResults()
{
    if (!m_semanticActive && m_semanticResults.isEmpty())
        return;
    m_semanticResults.clear();
    m_semanticActive = false;
    m_semanticOnly   = false;
    emit semanticResultsChanged();
}
//----------------------------------------------------------------------
QString AppManager::runInfoSearch(const QString &text, bool onlyInfo)
{
    // A search started before is not stopped: its list is dropped on arrival
    const int serial = ++m_infoSearchSerial;
    m_infoResults.clear();
    m_infoActive     = true;
    m_infoOnly       = onlyInfo;
    m_infoSearchBusy = false;

    // Hosted: not available (MDI-C48). No AI data yet: nothing to find, and
    // the store is not created for a search.
    const QString refusal = collection->databaseMode == QLatin1String("Hosted") ? peopleStoreRefusal() : QString();
    const QString mdiPath = mdiDataPath();
    if (text.trimmed().isEmpty() || !refusal.isEmpty() || !QFileInfo::exists(mdiPath)) {
        emit infoResultsChanged();
        return refusal;
    }

    // The catalogs of the tree, with their device: a row opens in Browse
    QHash<int, int> deviceByCatalog;
    for (const Device::DeviceTreeNode &n : Device::loadDeviceTree(m_connectionName))
        if (n.type == QLatin1String("Catalog") && n.externalId > 0)
            deviceByCatalog.insert(n.externalId, n.id);

    // The choice in My drives, as the table search and the search by meaning
    // read it: the active catalogs under it. All: every catalog of the tree.
    QList<int> catalogIds;
    if (selectedDevice && selectedDevice->type != QLatin1String("All")) {
        Device scope;
        scope.ID = selectedDevice->ID;
        scope.loadDevice(m_connectionName);
        if (scope.type == QLatin1String("Catalog"))
            catalogIds.append(scope.externalID);
        const QList<Device*> catalogs = Device::getActiveCatalogList(m_connectionName, scope.ID);
        for (Device *catalogDevice : catalogs) {
            if (!catalogIds.contains(catalogDevice->externalID))
                catalogIds.append(catalogDevice->externalID);
            delete catalogDevice;
        }
    } else {
        catalogIds = deviceByCatalog.keys();
    }
    if (catalogIds.isEmpty())
        catalogIds.append(-1);   // a choice without catalogs matches nothing

    // Every annotated file's text is read and compared: on a worker thread,
    // with its own connection; it never touches this object
    m_infoSearchBusy = true;
    emit infoResultsChanged();
    auto found = std::make_shared<QVariantList>();
    QThread *worker = QThread::create([found, text, catalogIds, deviceByCatalog, mdiPath]() {
        const QString connection = QStringLiteral("mdi_find_%1").arg(reinterpret_cast<quintptr>(QThread::currentThread()));
        if (MdiData::open(connection, mdiPath)) {
            for (const MdiPeople::FileRef &f : MdiPeople::findFiles(connection, text, catalogIds, 500)) {
                // The preview as Browse shows it: thumbnail, else a video's poster frame
                QString thumb, poster;
                for (const MdiData::Datum &d : MdiData::forFile(connection, f.catalogId, f.fullPath)) {
                    if (d.outcome != QLatin1String("completed") || d.derivedFiles.isEmpty())
                        continue;
                    if (d.analysis == QLatin1String("thumbnail"))
                        thumb = QDir(d.derivedRoot).filePath(d.derivedFiles.first());
                    else if (d.analysis == QLatin1String("poster"))
                        poster = QDir(d.derivedRoot).filePath(d.derivedFiles.first());
                }
                const QString preview = !thumb.isEmpty() ? thumb : poster;
                const QFileInfo info(f.fullPath);
                found->append(QVariantMap{
                    { QStringLiteral("catalogId"),  f.catalogId },
                    { QStringLiteral("deviceId"),   deviceByCatalog.value(f.catalogId, 0) },
                    { QStringLiteral("fullPath"),   f.fullPath },
                    { QStringLiteral("name"),       info.fileName() },
                    { QStringLiteral("folderPath"), info.path() },
                    { QStringLiteral("thumbUrl"),   !preview.isEmpty() && QFileInfo::exists(preview)
                                                        ? QUrl::fromLocalFile(preview).toString() : QString() },
                });
            }
        }
        MdiData::close(connection);
    });
    m_infoSearchThreads.append(worker);
    connect(qApp, &QCoreApplication::aboutToQuit, worker, [worker]() { worker->wait(10000); });
    connect(worker, &QThread::finished, this, [this, worker, found, serial, mdiPath]() {
        m_infoSearchThreads.removeOne(worker);
        worker->deleteLater();
        // A newer search, or another collection opened meanwhile
        if (serial != m_infoSearchSerial || mdiPath != mdiDataPath())
            return;
        m_infoResults    = *found;
        m_infoSearchBusy = false;
        emit infoResultsChanged();
    });
    worker->start(QThread::LowPriority);
    return QString();
}
//----------------------------------------------------------------------
void AppManager::clearInfoResults()
{
    ++m_infoSearchSerial;   // a list still on its way is dropped
    if (!m_infoActive && m_infoResults.isEmpty() && !m_infoSearchBusy)
        return;
    m_infoResults.clear();
    m_infoActive     = false;
    m_infoOnly       = false;
    m_infoSearchBusy = false;
    emit infoResultsChanged();
}
//----------------------------------------------------------------------
void AppManager::checkLocalModel()
{
    // The search by meaning embeds the text with the local model only
    // (runSemanticSearch): offered again only when that model answers
    const QVariantMap tools = getEnrichmentTools();
    const QString url   = tools.value(QStringLiteral("ollamaUrl")).toString();
    const QString model = tools.value(QStringLiteral("embedModel")).toString();
    if (url.isEmpty() || model.isEmpty()) {
        emit localModelChecked(false);
        return;
    }
    QNetworkRequest request(QUrl(url + QStringLiteral("/api/tags")));
    request.setTransferTimeout(3000);
    auto *manager = new QNetworkAccessManager(this);
    QNetworkReply *reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, manager, reply, model]() {
        reply->deleteLater();
        manager->deleteLater();
        bool reachable = false;
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonArray models = QJsonDocument::fromJson(reply->readAll()).object()
                                          .value(QStringLiteral("models")).toArray();
            for (const QJsonValue &m : models) {
                const QString name = m.toObject().value(QStringLiteral("name")).toString();
                if (name == model || name.startsWith(model + QLatin1Char(':')))
                    reachable = true;
            }
        }
        emit localModelChecked(reachable);
    });
}
//----------------------------------------------------------------------
QVariantMap AppManager::reviewCounts()
{
    QVariantMap counts{ { QStringLiteral("faces"),             0 },
                        { QStringLiteral("proposals"),         0 },
                        { QStringLiteral("proposalsDeviceId"), 0 },
                        { QStringLiteral("proposalsFolder"),   QString() } };
    // Read only: a collection without AI data has nothing waiting, and its
    // store is not created for a count. Hosted: not shown (MDI-C48).
    if (collection->databaseMode == QLatin1String("Hosted") || !QFileInfo::exists(mdiDataPath())
        || !openMdiMainConnection())
        return counts;

    // The faces the People page lists to approve; only where it shows them
    int faces = MdiPeople::pendingFaceCount(kMdiMainConnection);
    if (faces > 0 && !facesAvailable())
        faces = 0;
    counts[QStringLiteral("faces")] = faces;

    // Proposals waiting, per catalog of the tree (indexed counts); Browse
    // opens on the catalog with the most of them
    int total = 0, most = 0;
    QSet<int> seen;
    for (const Device::DeviceTreeNode &n : Device::loadDeviceTree(m_connectionName)) {
        if (n.type != QLatin1String("Catalog") || n.externalId <= 0 || seen.contains(n.externalId))
            continue;
        seen.insert(n.externalId);
        int pending = 0;
        const QVariantMap byField = MdiProposals::pendingCounts(kMdiMainConnection, n.externalId);
        for (auto it = byField.cbegin(); it != byField.cend(); ++it)
            pending += it.value().toInt();
        total += pending;
        if (pending > most) {
            most = pending;
            counts[QStringLiteral("proposalsDeviceId")] = n.id;
            counts[QStringLiteral("proposalsFolder")]   = n.path;
        }
    }
    counts[QStringLiteral("proposals")] = total;
    return counts;
}
//----------------------------------------------------------------------
QString AppManager::localFileUrl(const QString &path) const
{
    return QUrl::fromLocalFile(path).toString();
}
//----------------------------------------------------------------------
void AppManager::onCatalogCreationCompleted(const QList<qint64> &results)
{
    collection->saveDeviceTableToFile();
    collection->saveStatiticsTableToFile();

    m_catalogIsCreating = false;
    emit catalogIsCreatingChanged();
    // Status bar message is built and emitted by CatalogProgressManager
    // via statusMessageChanged → m_catalogStatusText, matching the spec:
    // CREATE | Completed | Catalog 1 of 1 | <name> | Indexed: N of N (100%)

    refreshDeviceList();

    // End of creation as in K2 (OPQ-F16..F18): the new catalog becomes the
    // selection, then the creation report is shown on the Devices page
    QVariantMap report;
    if (m_creatingDevice) {
        savePendingEnrichmentProfile(m_creatingDevice->ID);
        selectDeviceById(m_creatingDevice->ID);
        report = buildUpdateReport(m_creatingDevice->ID, results);
        if (!report.isEmpty())
            report[QStringLiteral("created")] = true;
    }
    emit catalogCreationCompleted(true, QString());
    if (!report.isEmpty())
        emit deviceUpdateReportReady(report);
    m_creatingDevice = nullptr;

    finishRunningOperation();
}
//----------------------------------------------------------------------
void AppManager::onCatalogCreationError(const QString &error)
{
    if (m_creatingDevice) {
        m_pendingEnrichmentProfiles.remove(m_creatingDevice->ID);
        m_creatingDevice->deleteDevice(false);
        m_creatingDevice = nullptr;
    }
    m_catalogIsCreating = false;
    m_catalogStatusText.clear();
    emit catalogIsCreatingChanged();
    emit catalogStatusTextChanged();
    refreshDeviceList();
    emit catalogCreationCompleted(false, error);

    // A failure does not abandon what is queued behind it (OPQ-F9).
    finishRunningOperation();
}
//----------------------------------------------------------------------
void AppManager::onCatalogCreationCancelled()
{
    // A stop during the metadata/checksum phase happens after the file list is
    // committed. The catalog is complete as an index and is kept; only the
    // optional scan is unfinished, and the next update finishes it. Deleting the
    // device here would throw away a catalog the user just spent minutes building.
    const bool keptCatalog = m_deviceUpdateManager && m_deviceUpdateManager->lastCatalogCommitted();
    const QString incompleteMessage = m_deviceUpdateManager
                                      ? m_deviceUpdateManager->lastScanIncompleteMessage()
                                      : QString();

    qWarning() << "STOPTRACE AppManager::onCatalogCreationCancelled keptCatalog=" << keptCatalog
               << "creatingDevice=" << (m_creatingDevice != nullptr)
               << "deviceID=" << (m_creatingDevice ? m_creatingDevice->ID : -1)
               << "type=" << (m_creatingDevice ? m_creatingDevice->type : QStringLiteral("<none>"))
               << "catalogID=" << (m_creatingDevice && m_creatingDevice->catalog
                                   ? m_creatingDevice->catalog->ID : -1);
    // A stopped creation gets no enrichment profile, even when the catalog is kept (MDI-F7)
    if (m_creatingDevice)
        m_pendingEnrichmentProfiles.remove(m_creatingDevice->ID);
    if (!keptCatalog && m_creatingDevice) {
        const Device::DeleteOperationResult r = m_creatingDevice->deleteDevice(false);
        qWarning() << "STOPTRACE deleteDevice result=" << int(r.result)
                   << "needsConfirmation=" << r.needsConfirmation
                   << "error=" << r.errorMessage;
    }
    m_creatingDevice = nullptr;

    m_catalogIsCreating = false;
    m_catalogStatusText.clear();
    emit catalogIsCreatingChanged();
    emit catalogStatusTextChanged();
    refreshDeviceList();

    if (keptCatalog)
        emit catalogCreationCompleted(true, incompleteMessage);
    else
        emit catalogCreationCompleted(false, tr("Catalog creation was stopped."));

    // Stopping this creation does not abandon what is queued behind it (OPQ-F7):
    // carry on with the next entry.
    finishRunningOperation();
}
//----------------------------------------------------------------------
// Storage pre-selection for Create page (used by DeviceTreeComboBox storageOnly mode)
//----------------------------------------------------------------------
int AppManager::getDefaultStorageId() const
{
    const QString type = selectedDevice->type;
    if (type == "Storage")
        return selectedDevice->ID;
    if (type == "Catalog")
        return selectedDevice->parentID;
    if (type == "Virtual")
        return Device::getFirstStorageDescendantId(selectedDevice->ID, m_connectionName);
    return 0;
}

// Device delete
//----------------------------------------------------------------------
QVariantMap AppManager::checkDeviceDeleteAllowed(int deviceId) const
{
    const QString conn = m_connectionName;
    Device dev;
    dev.ID = deviceId;
    dev.loadDevice(conn);
    dev.verifyHasSubDevice(conn);

    QVariantMap result;
    if (dev.hasSubDevice) {
        result["allowed"]      = false;
        result["errorMessage"] = tr("The selected device cannot be deleted as long as it has sub-devices.");
    } else {
        result["allowed"]      = true;
        result["errorMessage"] = QString();
    }
    return result;
}
//----------------------------------------------------------------------
QString AppManager::deleteDevice(int deviceId)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    const QString conn = m_connectionName;
    Device dev;
    dev.ID = deviceId;
    dev.loadDevice(conn);

    // Restore point (fork): the device and, for a catalog, its files, folders
    // and every row that shows it. Not for a device that cannot go.
    dev.verifyHasSubDevice(conn);
    const int historyEntry = dev.hasSubDevice ? 0
                           : historyBefore(QStringLiteral("delete"), { deviceId }, false, dev.name);

    Device::DeleteOperationResult res = dev.deleteDevice(false);

    switch (res.result) {
    case Device::DeleteHasSubDevices:
        historyDiscard(historyEntry);
        return tr("The selected device cannot be deleted as long as it has sub-devices.");
    case Device::DeleteError:
        historyDiscard(historyEntry);
        return res.errorMessage;
    case Device::DeleteCancelled:
        historyDiscard(historyEntry);
        return tr("Deletion cancelled.");
    case Device::DeleteSuccess:
        break;
    }

    // Update parent numbers (mirrors K2 deleteDeviceItem)
    if (dev.parentID > 0) {
        Device parent;
        parent.ID = dev.parentID;
        parent.loadDevice(conn);
        parent.updateNumbersFromChildren();
        parent.updateParentsNumbers();
    }

    // Persist to files
    collection->saveDeviceTableToFile();
    collection->saveStorageTableToFile();

    // In Memory mode, move the catalog index files to trash
    if (dev.type == "Catalog") {
        Collection::DeleteCatalogResult fileResult = collection->deleteCatalogFile(&dev);
        if (fileResult == Collection::DeleteFailedToMoveToTrash) {
            historyAfter(historyEntry, {}, QString());
            return tr("Deletion failed");
        }
    }

    historyAfter(historyEntry, {}, tr("Deleted: %1.").arg(dev.name));
    emit deviceListChanged();
    refreshAllUI();
    return QString();
}

// Devices page operations
//----------------------------------------------------------------------
QVariantList AppManager::getDeviceList(const QString &viewFilter, int scopeDeviceId) const
{
    const QString conn = m_connectionName;

    // Re-probe the filesystem before reading device_active, so opening the
    // Devices page or switching its type filter reflects what is mounted now.
    // K2 does the same inside its device-tree model loaders; without it this
    // path would display whatever was cached at the last device operation.
    collection->updateAllDeviceActive();
    // Record the mount table as seen by this check, for the same reason as in
    // refreshDeviceList().
    m_lastMountSignature = collection->mountSignature();

    const QList<Device::DeviceTreeNode> nodes = Device::loadDeviceTree(conn, scopeDeviceId);

    // Build id → type map so each node can resolve its parent's type without a second query.
    // The name map serves the Catalogs table's "Parent storage" column the same way.
    QHash<int, QString> typeById;
    QHash<int, QString> nameById;
    typeById.reserve(nodes.size());
    nameById.reserve(nodes.size());
    for (const Device::DeviceTreeNode &n : nodes) {
        typeById.insert(n.id, n.type);
        nameById.insert(n.id, n.name);
    }

    // The Table display mode needs the Storage and Catalog detail columns
    // (SpecDevicesPage DVP-F3). They are read here, for the list views that show
    // them, rather than when the user switches to Table: toggling the display
    // mode must not reload the list, because a reload would re-probe the active
    // status and break DAS-O4 / DVP-C2. Both loads are plain database reads on
    // existing core methods - no filesystem access, and no SQL in qt_quick
    // (DVP-C4).
    const bool wantStorageDetail = (viewFilter == QLatin1String("Storage"));
    const bool wantCatalogDetail = (viewFilter == QLatin1String("Catalogs"));

    QVariantList result;
    result.reserve(nodes.size());

    for (const Device::DeviceTreeNode &n : nodes) {
        // Apply type filter — "All" (or any unrecognised value) passes everything through.
        if (viewFilter == QLatin1String("Catalogs") && n.type != QLatin1String("Catalog"))
            continue;
        if (viewFilter == QLatin1String("Storage") && n.type != QLatin1String("Storage"))
            continue;

        QVariantMap item;
        item[QStringLiteral("deviceId")]      = n.id;
        item[QStringLiteral("parentId")]      = n.parentId;
        item[QStringLiteral("name")]          = n.name;
        item[QStringLiteral("type")]          = n.type;
        item[QStringLiteral("path")]          = n.path;
        item[QStringLiteral("totalFileSize")] = n.totalFileSize;
        item[QStringLiteral("fileCount")]     = n.totalFileCount;
        item[QStringLiteral("totalSpace")]    = n.totalSpace;
        item[QStringLiteral("freeSpace")]     = n.freeSpace;
        item[QStringLiteral("active")]        = n.isActive;
        item[QStringLiteral("dateUpdated")]   = n.dateUpdated;
        item[QStringLiteral("level")]         = n.level;
        item[QStringLiteral("parentType")]    = typeById.value(n.parentId);
        item[QStringLiteral("groupId")]       = n.groupId;
        item[QStringLiteral("externalId")]    = n.externalId;
        item[QStringLiteral("comment")]       = n.comment;
        // Used space is not stored; K2 derives it the same way
        // (mainwindow_tab_device_pr.cpp:1519).
        item[QStringLiteral("usedSpace")]     = n.totalSpace - n.freeSpace;

        if (wantStorageDetail && n.type == QLatin1String("Storage")) {
            Storage storage;
            storage.ID = n.externalId;
            storage.setConnectionName(conn);
            storage.loadStorage(conn);
            // The number the user wrote on the disk; device_external_id stays the
            // internal key and is not shown (STI-F10).
            item[QStringLiteral("storageUserId")]       = storage.userID;
            item[QStringLiteral("storageType")]         = storage.type;
            item[QStringLiteral("storageLabel")]        = storage.label;
            item[QStringLiteral("storageFileSystem")]   = storage.fileSystem;
            item[QStringLiteral("storageBrand")]        = storage.brand;
            item[QStringLiteral("storageModel")]        = storage.model;
            item[QStringLiteral("storageSerialNumber")] = storage.serialNumber;
            item[QStringLiteral("storageBuildDate")]    = storage.buildDate;
            item[QStringLiteral("storageComment1")]     = storage.comment1;
            item[QStringLiteral("storageComment2")]     = storage.comment2;
            item[QStringLiteral("storageComment3")]     = storage.comment3;
        }

        if (wantCatalogDetail && n.type == QLatin1String("Catalog")) {
            Catalog catalog;
            catalog.ID = n.externalId;
            catalog.setConnectionName(conn);
            catalog.loadCatalog();
            item[QStringLiteral("catalogFileType")]       = catalog.fileType;
            item[QStringLiteral("catalogIncludeHidden")]  = catalog.includeHidden;
            // A pre-metadata catalog stores the literal "false"; the device
            // loader normalises it the same way (device.cpp:130).
            item[QStringLiteral("catalogIncludeMetadata")] =
                (catalog.includeMetadata == QLatin1String("false"))
                    ? Catalog::METADATA_NONE : catalog.includeMetadata;
            item[QStringLiteral("catalogIncludeChecksum")] = catalog.includeChecksum;
            item[QStringLiteral("catalogParentStorage")]   = nameById.value(n.parentId);
            item[QStringLiteral("catalogAppVersion")]      = catalog.appVersion;
            item[QStringLiteral("catalogFilePath")]        = catalog.filePath;
            item[QStringLiteral("catalogDateLoaded")]      =
                catalog.dateLoaded.isValid()
                    ? catalog.dateLoaded.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"))
                    : QString();
        }

        result.append(item);
    }
    return result;
}
//----------------------------------------------------------------------
int AppManager::addDeviceVirtual(int parentId)
{
    if (getReadOnly()) return -1;   // read-only collection (D43)
    const QString conn = m_connectionName;
    Device parent;
    parent.ID = parentId;
    parent.loadDevice(conn);

    Device *newDevice = new Device();
    newDevice->generateDeviceID();
    newDevice->type     = QStringLiteral("Virtual");
    newDevice->name     = tr("Virtual") + "_" + QString::number(newDevice->ID);
    newDevice->parentID = parentId;
    newDevice->groupID  = (parentId == 0) ? 1 : parent.groupID;
    newDevice->externalID = 0;
    newDevice->insertDevice();

    collection->saveDeviceTableToFile();
    emit deviceListChanged();
    refreshDeviceList();

    int id = newDevice->ID;
    delete newDevice;
    return id;
}
//----------------------------------------------------------------------
int AppManager::addDeviceStorage(int parentId)
{
    if (getReadOnly()) return -1;   // read-only collection (D43)
    const QString conn = m_connectionName;
    Device parent;
    parent.ID = parentId;
    parent.loadDevice(conn);

    Device *newDevice = new Device();
    newDevice->generateDeviceID();
    newDevice->type     = QStringLiteral("Storage");
    newDevice->parentID = parentId;
    newDevice->groupID  = 0;
    newDevice->storage->generateID();
    newDevice->name     = tr("Storage") + "_" + QString::number(newDevice->storage->userID);
    newDevice->externalID = newDevice->storage->ID;
    newDevice->insertDevice();
    newDevice->storage->insertStorage();

    collection->saveDeviceTableToFile();
    collection->saveStorageTableToFile();
    emit deviceListChanged();
    refreshDeviceList();

    int id = newDevice->ID;
    delete newDevice;
    return id;
}
//----------------------------------------------------------------------
void AppManager::setupDeviceUpdateManagerForDevices()
{
    if (!m_deviceUpdateManager)
        m_deviceUpdateManager = new DeviceUpdateManager(this);
    disconnect(m_deviceUpdateManager, nullptr, this, nullptr);

    if (!m_catalogProgressManager) {
        m_catalogProgressManager = new CatalogProgressManager(this);
    }
    disconnect(m_catalogProgressManager, nullptr, this, nullptr);
    connect(m_catalogProgressManager, &CatalogProgressManager::statusMessageChanged,
            this, [this](const QString &msg, int) {
                m_deviceUpdateStatusText = msg;
                emit deviceUpdateStatusChanged();
            });
    m_deviceUpdateManager->setCatalogProgressManager(m_catalogProgressManager);
    m_deviceUpdateManager->setConnectionName(m_connectionName);

    connect(m_deviceUpdateManager, &DeviceUpdateManager::operationCompleted,
            this, &AppManager::onDevicePageUpdateCompleted);
    connect(m_deviceUpdateManager, &DeviceUpdateManager::operationError,
            this, [this](const QString &err) {
                m_deviceUpdateStatusText = tr("Error: %1").arg(err);
                emit deviceUpdateStatusChanged();
                // A failure does not abandon the rest of the queue (OPQ-F9).
                // The running flag is cleared by startNextDeviceUpdate() once the
                // queue is found empty, so the panel does not blink out between
                // two entries.
                finishRunningOperation();
            });
    connect(m_deviceUpdateManager, &DeviceUpdateManager::operationCancelled,
            this, [this]() {
                // Stop ends the running operation only; the queue carries on
                // (OPQ-F7). Emptying the queue is the separate Clear action.
                m_isBatchUpdate = false;
                m_deviceUpdateStatusText.clear();
                emit deviceUpdateStatusChanged();
                emit deviceListChanged();
                finishRunningOperation();
            });
}
//----------------------------------------------------------------------
void AppManager::onDevicePageUpdateCompleted(const QList<qint64> &results)
{
    collection->saveDeviceTableToFile();
    collection->saveStatiticsTableToFile();
    emit deviceListChanged();
    refreshDeviceList();

    // A path-root replacement reports in the activity panel and opens no dialog
    // (DSR-F8): its message is already on screen, built by core. Releasing the
    // entry here is what lets the queue carry on (DSR-C7).
    if (m_runningOperation.isReplaceRoot) {
        finishRunningOperation();
        return;
    }

    if (m_isBatchUpdate) {
        // Single operationCompleted for the whole "All Active" batch — show aggregate
        QVariantMap report = buildBatchUpdateReport(results);
        if (!report.isEmpty())
            emit deviceUpdateReportReady(report);

        m_isBatchUpdate = false;
        finishRunningOperation();
    } else {
        if (m_showEachUpdateReport) {
            QVariantMap report = buildUpdateReport(m_currentUpdateDeviceId, results);
            if (!report.isEmpty()) {
                emit deviceUpdateReportReady(report);
                if (!m_pendingDeviceUpdates.isEmpty()) {
                    // More entries waiting — the queue holds until the user
                    // acknowledges this report (OPQ-F10). The entry itself is
                    // over, so it stops being shown as running right away.
                    m_waitingForReportAck = true;
                    m_hasRunningOperation = false;
                    emitQueueChanged();
                    return;
                }
            }
        }
        finishRunningOperation();
    }
}
//----------------------------------------------------------------------
QVariantList AppManager::getOperationQueue() const
{
    // The running entry first, then the waiting ones in run order.
    QVariantList out;
    if (m_hasRunningOperation) {
        QVariantMap e;
        e["deviceId"]   = m_runningOperation.deviceId;
        e["deviceName"] = m_runningOperation.deviceName;
        e["isCreate"]   = m_runningOperation.isCreate;
        e["running"]    = true;
        out.append(e);
    }
    for (const QueuedOperation &op : m_pendingDeviceUpdates) {
        QVariantMap e;
        e["deviceId"]   = op.deviceId;
        e["deviceName"] = op.deviceName;
        e["isCreate"]   = op.isCreate;
        e["running"]    = false;
        out.append(e);
    }
    return out;
}
//----------------------------------------------------------------------
void AppManager::emitQueueChanged()
{
    emit operationQueueChanged();
}
//----------------------------------------------------------------------
bool AppManager::isDeviceQueuedOrRunning(int deviceId) const
{
    if (m_hasRunningOperation && m_runningOperation.deviceId == deviceId)
        return true;
    for (const QueuedOperation &op : m_pendingDeviceUpdates)
        if (op.deviceId == deviceId)
            return true;
    return false;
}
//----------------------------------------------------------------------
void AppManager::enqueueOperation(int deviceId, bool isCreate, const QString &deviceName)
{
    if (getReadOnly()) return;   // read-only collection (D43)
    // A device can appear once: re-requesting one already queued or running
    // leaves the queue untouched rather than doubling the work.
    if (isDeviceQueuedOrRunning(deviceId))
        return;

    QueuedOperation op;
    op.deviceId   = deviceId;
    op.isCreate   = isCreate;
    op.deviceName = deviceName;
    m_pendingDeviceUpdates.append(op);
    emitQueueChanged();
}
//----------------------------------------------------------------------
void AppManager::enqueueReplaceRoot(int deviceId, const QString &deviceName,
                                    const QString &previousPath, const QString &newPath)
{
    if (getReadOnly()) return;   // read-only collection (D43)
    if (isDeviceQueuedOrRunning(deviceId))
        return;

    QueuedOperation op;
    op.deviceId      = deviceId;
    op.deviceName    = deviceName;
    op.isReplaceRoot = true;
    op.previousPath  = previousPath;
    op.newPath       = newPath;
    m_pendingDeviceUpdates.append(op);
    emitQueueChanged();
}
//----------------------------------------------------------------------
void AppManager::removeQueuedOperation(int index)
{
    if (index < 0 || index >= m_pendingDeviceUpdates.size())
        return;

    const QueuedOperation op = m_pendingDeviceUpdates.takeAt(index);

    // A queued creation already has its device and catalog rows in the database
    // (they are written before the operation is queued). Dropping the entry must
    // remove them too, or an empty catalog is left behind.
    if (op.isCreate) {
        m_pendingEnrichmentProfiles.remove(op.deviceId);
        Device dev;
        dev.ID = op.deviceId;
        dev.loadDevice(m_connectionName);
        dev.deleteDevice(false);
        emit deviceListChanged();
        refreshDeviceList();
    }
    emitQueueChanged();
}
//----------------------------------------------------------------------
void AppManager::clearOperationQueue()
{
    while (!m_pendingDeviceUpdates.isEmpty())
        removeQueuedOperation(0);
}
//----------------------------------------------------------------------
void AppManager::finishRunningOperation()
{
    // The single ending for every way an operation can finish — completed,
    // stopped or failed. The running entry is released and the queue carries on
    // (OPQ-F7, OPQ-F9).
    m_hasRunningOperation = false;
    emitQueueChanged();
    scheduleNextOperation();
    // A created or updated catalog gets its instant-search index refreshed
    if (!m_hasRunningOperation)
        scheduleFtsIndexing();
}
//----------------------------------------------------------------------
void AppManager::scheduleNextOperation()
{
    // The next entry must never start from inside a DeviceUpdateManager signal
    // handler, because core is not finished with the previous operation when
    // that signal arrives: CatalogManager::onJobResult() runs cleanupJob() as
    // soon as the handlers return, and onCatalogOperationCompleted() arms
    // cleanupOperation() on a short timer of its own. A job started before
    // those run gets the previous operation's teardown applied to it — deleted
    // job, manager reset to idle — and dies silently, or keeps running with no
    // owner. So: wait past core's own deferred cleanup, then confirm the
    // manager really is idle before starting anything (OPQ-C4).
    if (m_nextOperationScheduled)
        return;
    m_nextOperationScheduled = true;
    QTimer::singleShot(operationChainDelayMs, this, [this]() {
        m_nextOperationScheduled = false;
        if (!m_pendingDeviceUpdates.isEmpty()
            && m_deviceUpdateManager && m_deviceUpdateManager->operationRunning()) {
            // Still winding down — come back rather than start on top of it.
            scheduleNextOperation();
            return;
        }
        startNextDeviceUpdate();
    });
}
//----------------------------------------------------------------------
void AppManager::startNextDeviceUpdate()
{
    if (getReadOnly()) return;   // read-only collection (D43)
    if (m_pendingDeviceUpdates.isEmpty()) {
        m_deviceUpdateIsRunning = false;
        m_isBatchUpdate         = false;
        m_operationsStarted     = 0;
        m_hasRunningOperation   = false;
        emitQueueChanged();
        emit deviceUpdateStateChanged();
        // Status text lingers for 5 seconds then clears (K2 behaviour)
        QTimer::singleShot(5000, this, [this]() {
            m_deviceUpdateStatusText.clear();
            emit deviceUpdateStatusChanged();
        });
        setupDeviceUpdateManager();
        return;
    }
    const QueuedOperation next = m_pendingDeviceUpdates.takeFirst();
    m_runningOperation    = next;
    m_hasRunningOperation = true;
    m_operationsStarted++;
    emitQueueChanged();

    // Claim the running flag here rather than relying on whoever queued the work:
    // this function is the only place an entry actually starts, so it owns the
    // state. Without this, a path that cleared the flag before the queue advanced
    // (a cancellation, an error) would leave a job running with the UI believing
    // nothing is happening — no progress, no Stop button, no panel.
    m_deviceUpdateIsRunning = true;
    emit deviceUpdateStateChanged();

    m_currentUpdateDeviceId = next.deviceId;
    Device *dev = new Device();
    dev->ID = next.deviceId;
    dev->loadDevice(m_connectionName);

    if (next.isReplaceRoot) {
        // Same handler set as a device update: it ends in
        // onDevicePageUpdateCompleted(), which releases the entry and lets the
        // queue carry on. The completion message is built in core and arrives
        // through CatalogProgressManager (DSR-F7, DSR-C5).
        setupDeviceUpdateManagerForDevices();
        m_deviceUpdateStatusText = StatusBarMessageBuilder()
                                       .setOperation(tr("Update"))
                                       .setStatus(tr("In Progress"))
                                       .setDeviceContext(m_operationsStarted,
                                                         m_operationsStarted + m_pendingDeviceUpdates.size(),
                                                         dev->name)
                                       .build();
        emit deviceUpdateStatusChanged();
        // Deferred so the panel paints before the work starts (OPQ-F11). The
        // replacement is near-instant, so without this it would never be seen.
        QTimer::singleShot(kStartDeferralMs, this, [this, dev, next]() {
            // Restore point (fork): the storage and its catalogs' paths
            historyBeforeUpdate(QStringLiteral("edit"), { dev->ID }, true);
            m_deviceUpdateManager->replaceStorageRoot(dev, next.previousPath, next.newPath,
                                                      collection->databaseMode, collection->folder);
        });
        return;
    }

    if (next.isCreate) {
        // Queued creation: the device and catalog rows already exist, so this
        // only runs the scan. The create-specific handlers need m_creatingDevice.
        // The handler set is wired per entry, here rather than by the caller: a
        // creation has to end in onCatalogCreationCompleted(), or m_catalogIsCreating
        // is never cleared and the activity panel stays busy for good.
        setupDeviceUpdateManager();
        m_creatingDevice         = dev;
        m_catalogIsCreating      = true;
        m_catalogCreateStartTime = QDateTime::currentDateTime();
        m_catalogStatusText      = StatusBarMessageBuilder().setOperation(tr("Create"))
                                                            .setStatus(tr("In Progress")).build();
        emit catalogIsCreatingChanged();
        emit catalogStatusTextChanged();
        // Deferred so the panel paints first (OPQ-F11). This function is not only
        // the queue's continuation — it is also where a single request from an
        // idle application starts, so the panel is not already on screen.
        QTimer::singleShot(kStartDeferralMs, this, [this, dev]() {
            m_deviceUpdateManager->updateDeviceHierarchy(dev, collection->databaseMode, collection->folder, "create");
        });
        return;
    }

    setupDeviceUpdateManagerForDevices();

    // Say what is starting straight away. Only the create branch above did this,
    // so an update showed an empty activity panel until the catalog job emitted
    // its first progress line — seconds, on a large catalog, during which the
    // panel said nothing at all. Both strings already exist.
    m_deviceUpdateStatusText = StatusBarMessageBuilder()
                                   .setOperation(tr("Update"))
                                   .setStatus(tr("In Progress"))
                                   .setDeviceContext(m_operationsStarted,
                                                     m_operationsStarted + m_pendingDeviceUpdates.size(),
                                                     dev->name)
                                   .build();
    emit deviceUpdateStatusChanged();

    // Deferred so the panel paints first (OPQ-F11) — this is the path a single
    // update from an idle application takes, not just the queue's continuation.
    // The batch context goes with it: it must still be set AFTER
    // updateDeviceHierarchy (which internally calls cleanupOperation →
    // clearBatchContext), so the context survives into the catalog job's
    // progress signals. Leaving it here would set it before the deferred call
    // and the call would then clear it.
    QTimer::singleShot(kStartDeferralMs, this, [this, dev]() {
        // Restore point (fork) of the catalogs this update rescans
        historyBeforeUpdate(QStringLiteral("update"), { dev->ID }, true);
        m_deviceUpdateManager->updateDeviceHierarchy(dev, collection->databaseMode, collection->folder, "update");
        // Counted from the queue as it stands rather than from a total fixed when the
        // batch was requested: the queue can grow while it drains, and "3 of 5" has
        // to become "3 of 6" when it does.
        const int knownTotal = m_operationsStarted + m_pendingDeviceUpdates.size();
        if (knownTotal > 1 && m_catalogProgressManager)
            m_catalogProgressManager->setBatchContext(m_operationsStarted, knownTotal);
    });
}
//----------------------------------------------------------------------
QVariantMap AppManager::buildUpdateReport(int deviceId, const QList<qint64> &results)
{
    if (results.isEmpty() || results[0] != 1)
        return {};

    const QString conn = m_connectionName;
    Device dev;
    dev.ID = deviceId;
    dev.loadDevice(conn);

    QVariantMap r;
    r[QStringLiteral("deviceName")] = dev.name;
    r[QStringLiteral("deviceType")] = dev.type;
    r[QStringLiteral("devicePath")] = dev.path;

    if (dev.type == QStringLiteral("Catalog")) {
        if (results.size() > 4) {
            r[QStringLiteral("fileCount")]  = results[1];
            r[QStringLiteral("filesAdded")] = results[2];
            r[QStringLiteral("totalSize")]  = results[3];
            r[QStringLiteral("sizeAdded")]  = results[4];
        }
        bool storageUpdated = (results.size() > 7 && results[7] == 1);
        r[QStringLiteral("storageUpdated")] = storageUpdated;
        if (storageUpdated && results.size() > 13) {
            Device parent;
            parent.ID = dev.parentID;
            parent.loadDevice(conn);
            r[QStringLiteral("storageName")]       = parent.name;
            r[QStringLiteral("storagePath")]       = parent.path;
            r[QStringLiteral("storageUsed")]       = results[8];
            r[QStringLiteral("storageUsedAdded")]  = results[9];
            r[QStringLiteral("storageFree")]       = results[10];
            r[QStringLiteral("storageFreeAdded")]  = results[11];
            r[QStringLiteral("storageTotal")]      = results[12];
            r[QStringLiteral("storageTotalAdded")] = results[13];
        }
    } else if (dev.type == QStringLiteral("Storage")) {
        bool storageUpdated = (results.size() > 7 && results[7] == 1);
        r[QStringLiteral("storageUpdated")] = storageUpdated;
        if (results.size() > 13) {
            r[QStringLiteral("storageUsed")]       = results[8];
            r[QStringLiteral("storageUsedAdded")]  = results[9];
            r[QStringLiteral("storageFree")]       = results[10];
            r[QStringLiteral("storageFreeAdded")]  = results[11];
            r[QStringLiteral("storageTotal")]      = results[12];
            r[QStringLiteral("storageTotalAdded")] = results[13];
        }
    } else if (dev.type == QStringLiteral("Virtual")) {
        if (results.size() > 4) {
            r[QStringLiteral("fileCount")]       = results[1];
            r[QStringLiteral("filesAdded")]      = results[2];
            r[QStringLiteral("totalSize")]       = results[3];
            r[QStringLiteral("sizeAdded")]       = results[4];
        }
        if (results.size() > 6) {
            r[QStringLiteral("catalogsUpdated")] = results[5];
            r[QStringLiteral("catalogsSkipped")] = results[6];
        }
        bool storageUpdated = (results.size() > 7 && results[7] == 1);
        r[QStringLiteral("storageUpdated")] = storageUpdated;
        if (storageUpdated && results.size() > 13) {
            r[QStringLiteral("storageUsed")]       = results[8];
            r[QStringLiteral("storageUsedAdded")]  = results[9];
            r[QStringLiteral("storageFree")]       = results[10];
            r[QStringLiteral("storageFreeAdded")]  = results[11];
            r[QStringLiteral("storageTotal")]      = results[12];
            r[QStringLiteral("storageTotalAdded")] = results[13];
        }
    }
    return r;
}
//----------------------------------------------------------------------
QVariantMap AppManager::buildBatchUpdateReport(const QList<qint64> &results)
{
    if (results.isEmpty() || results[0] != 1)
        return {};

    QVariantMap r;
    r[QStringLiteral("deviceType")] = QStringLiteral("list");
    if (results.size() > 4) {
        r[QStringLiteral("fileCount")]       = results[1];
        r[QStringLiteral("filesAdded")]      = results[2];
        r[QStringLiteral("totalSize")]       = results[3];
        r[QStringLiteral("sizeAdded")]       = results[4];
    }
    if (results.size() > 6) {
        r[QStringLiteral("catalogsUpdated")] = results[5];
        r[QStringLiteral("catalogsSkipped")] = results[6];
    }
    bool storageUpdated = (results.size() > 7 && results[7] == 1);
    r[QStringLiteral("storageUpdated")] = storageUpdated;
    if (storageUpdated && results.size() > 13) {
        r[QStringLiteral("storageUsed")]       = results[8];
        r[QStringLiteral("storageUsedAdded")]  = results[9];
        r[QStringLiteral("storageFree")]       = results[10];
        r[QStringLiteral("storageFreeAdded")]  = results[11];
        r[QStringLiteral("storageTotal")]      = results[12];
        r[QStringLiteral("storageTotalAdded")] = results[13];
    }
    return r;
}
//----------------------------------------------------------------------
void AppManager::updateDevice(int deviceId)
{
    if (getReadOnly()) return;   // read-only collection (D43)
    // Refused, not queued, while a search runs: nothing drains the queue when a
    // search ends, so a queued entry would be stranded. Queuing across operation
    // types is a separate decision that has not been taken.
    if (m_searchIsRunning || m_exploreLoadIsRunning) {   // OPQ-F20 for the Explore load
        qWarning() << "AppManager::updateDevice: refused, a search or an Explore load is running";
        return;
    }

    Device dev;
    dev.ID = deviceId;
    dev.loadDevice(m_connectionName);

    // Already busy: queue it instead of dropping the request silently.
    if (m_deviceUpdateIsRunning || (m_deviceUpdateManager && m_deviceUpdateManager->operationRunning())) {
        enqueueOperation(deviceId, false, dev.name);
        return;
    }

    m_isBatchUpdate          = false;
    m_showEachUpdateReport   = true;  // single update always reports
    enqueueOperation(deviceId, false, dev.name);
    startNextDeviceUpdate();
}
//----------------------------------------------------------------------
int AppManager::updateAllActiveDevices(bool showEachReport)
{
    if (getReadOnly()) return 0;   // read-only collection (D43)
    const QString conn = m_connectionName;

    // Scope: catalogs under the currently selected device (same as what the Catalogs
    // list view displays). 0 = no selection = all catalogs in the collection.
    int scopeId = selectedDevice ? selectedDevice->ID : 0;
    QList<Device *> activeCatalogs = Device::getActiveCatalogList(conn, scopeId);

    // How many catalogs go: none tells the page to say so (fork, 2026-10-30)
    const int count = activeCatalogs.size();
    if (activeCatalogs.isEmpty())
        return 0;

    if (m_deviceUpdateIsRunning
        || (m_deviceUpdateManager && m_deviceUpdateManager->operationRunning())) {
        // Busy: append every active catalog as its own entry rather than drop the
        // request (OPQ-F1, OPQ-F4). The aggregate form cannot be queued — it runs
        // the whole set as a single hierarchy — so work queued this way always
        // reports per catalog. m_showEachUpdateReport is deliberately left alone:
        // changing it now would change how the running operation reports itself.
        for (Device *d : activeCatalogs)
            enqueueOperation(d->ID, false, d->name);
        qDeleteAll(activeCatalogs);
        return count;
    }

    m_showEachUpdateReport  = showEachReport;
    m_currentUpdateDeviceId = -1;

    if (showEachReport) {
        // Process one catalog at a time so each report can pause for acknowledgement.
        // Appends: anything already waiting keeps its place (OPQ-F4).
        m_isBatchUpdate = false;
        for (Device *d : activeCatalogs)
            enqueueOperation(d->ID, false, d->name);
        qDeleteAll(activeCatalogs);

        startNextDeviceUpdate();
    } else {
        // Run all as a single hierarchy; show one aggregate report at the end
        m_isBatchUpdate         = true;
        m_deviceUpdateIsRunning = true;
        emit deviceUpdateStateChanged();

        setupDeviceUpdateManagerForDevices();

        Device *dummy = m_deviceUpdateManager->createDummyDeviceFromList(activeCatalogs);
        QList<int> catalogDeviceIds;
        for (Device *d : std::as_const(activeCatalogs))
            catalogDeviceIds << d->ID;
        // Deferred one event-loop turn so the activity panel paints before the
        // scan blocks the GUI thread — same reason as in createCatalog().
        QTimer::singleShot(kStartDeferralMs, this, [this, dummy, catalogDeviceIds]() {
            // Restore point (fork) of every catalog of the batch, in one entry
            historyBeforeUpdate(QStringLiteral("update"), catalogDeviceIds, true);
            m_deviceUpdateManager->updateDeviceHierarchy(dummy, collection->databaseMode, collection->folder, "update");
        });
        // activeCatalogs intentionally not deleted here — createDummyDeviceFromList copies Device
        // values but the copies share raw catalog*/storage* pointers with the originals.
        // Deleting the originals would create dangling pointers in dummy->subDevices (same pattern as K2).
    }
    return count;
}
//----------------------------------------------------------------------
void AppManager::stopDeviceUpdate()
{
    m_waitingForReportAck = false;
    if (m_deviceUpdateManager && m_deviceUpdateManager->operationRunning()) {
        // The queue is left intact: the cancellation handler starts the next
        // entry. Use Clear to empty the queue.
        m_deviceUpdateManager->requestHardStop();
    }
}
//----------------------------------------------------------------------
void AppManager::acknowledgeUpdateReport()
{
    if (!m_waitingForReportAck) return;
    m_waitingForReportAck = false;
    scheduleNextOperation();
}
//----------------------------------------------------------------------
void AppManager::gentleStopDeviceUpdate()
{
    if (m_deviceUpdateManager && m_deviceUpdateManager->operationRunning())
        m_deviceUpdateManager->requestGentleStop();
}
//----------------------------------------------------------------------
QString AppManager::assignCatalogToDevice(int catalogDeviceId, int virtualDeviceId)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    const QString conn = m_connectionName;

    Device catalogDev;
    catalogDev.ID = catalogDeviceId;
    catalogDev.loadDevice(conn);
    if (catalogDev.type != QStringLiteral("Catalog"))
        return tr("The selected device is not a catalog.");

    Device virtualDev;
    virtualDev.ID = virtualDeviceId;
    virtualDev.loadDevice(conn);

    if (Device::isCatalogAssigned(catalogDev.externalID, virtualDeviceId, conn))
        return tr("The catalog is already assigned to this Virtual device.");

    if (!Device::assignCatalogToDevice(&catalogDev, &virtualDev, conn))
        return tr("Failed to assign catalog to device.");

    collection->saveDeviceTableToFile();
    emit deviceListChanged();
    return QString();
}
//----------------------------------------------------------------------
void AppManager::launchFilelight(int deviceId)
{
    Device dev;
    dev.ID = deviceId;
    dev.loadDevice(m_connectionName);
    QString path = (dev.type == QStringLiteral("Catalog") && dev.catalog)
                   ? dev.catalog->sourcePath : dev.path;
    if (!path.isEmpty())
        QProcess::startDetached(QStringLiteral("filelight"), {path});
}
//----------------------------------------------------------------------
QVariantMap AppManager::recordDevicesSnapshot()
{
    if (getReadOnly()) return QVariantMap{ { QStringLiteral("error"), readOnlyRefusal() } };   // read-only collection (D43)
    const QString conn = m_connectionName;

    // In Memory mode the statistics_device table is empty until loaded from
    // file; without this the "previous" totals below read an empty table and
    // every delta equals the full total. No-op in File/Hosted mode. Mirrors
    // K2's recordAllDeviceStats().
    collection->loadStatisticsDeviceFileToTable();

    auto prevCat = Statistics::latestSnapshotTotals(conn, "Catalog");
    auto prevSto = Statistics::latestSnapshotTotals(conn, "Storage");

    collection->recordDevicesSnapshot(QDateTime::currentDateTime());

    auto newCat = Statistics::latestSnapshotTotals(conn, "Catalog");
    auto newSto = Statistics::latestSnapshotTotals(conn, "Storage");

    QVariantMap result;
    result["newCatalogFileCount"]  = newCat.value(0, 0);
    result["newCatalogFileSize"]   = newCat.value(1, 0);
    result["deltaCatalogFileCount"]= newCat.value(0, 0) - prevCat.value(0, 0);
    result["deltaCatalogFileSize"] = newCat.value(1, 0) - prevCat.value(1, 0);
    result["newStorageFreeSpace"]  = newSto.value(2, 0);
    result["newStorageTotalSpace"] = newSto.value(3, 0);
    result["deltaStorageFree"]     = newSto.value(2, 0) - prevSto.value(2, 0);
    result["deltaStorageTotal"]    = newSto.value(3, 0) - prevSto.value(3, 0);
    return result;
}
//----------------------------------------------------------------------
QString AppManager::splitCatalogBySubDirectory(int deviceId)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    const QString refusal = catalogChangeRefusal({ deviceId });
    if (!refusal.isEmpty())
        return refusal;

    Device dev;
    dev.ID = deviceId;
    dev.loadDevice(m_connectionName);

    if (collection->databaseMode == "Memory")
        dev.catalog->loadFoldersToTable();

    const int entry = historyBefore(QStringLiteral("split"), { deviceId }, false);
    QList<int> newIds;
    Collection::CatalogOperationError error = Collection::OperationOk;
    if (!collection->executeSplitBySubDirectory(&dev, &newIds, &error)) {
        historyDiscard(entry);
        QTimer::singleShot(0, this, &AppManager::scheduleFtsIndexing);   // stopped for the split
        if (error == Collection::OperationAiMoveFailed)
            return tr("Split cancelled: the AI results could not be moved to the new catalogs. Nothing was changed.");
        return tr("Split failed: no catalogs were created.");
    }

    collection->saveDeviceTableToFile();
    historyAfter(entry, newIds, tr("Catalog split into %1 catalogs.").arg(newIds.size()));
    // The new catalogs' instant index, as after a creation (MDI-F128)
    afterCatalogStructureChange();
    return QString();
}
//----------------------------------------------------------------------
void AppManager::splitCatalogByFileType(int deviceId, bool verifyFirst)
{
    if (getReadOnly()) { emit splitCompleted(false, readOnlyRefusal()); return; }   // read-only collection (D43)
    const QString refusal = catalogChangeRefusal({ deviceId });
    if (!refusal.isEmpty()) {
        emit splitCompleted(false, refusal);
        return;
    }

    Device *dev = new Device();
    dev->ID = deviceId;
    dev->loadDevice(m_connectionName);

    auto performSplit = [this, dev, deviceId]() {
        // Checked again: the MIME verification ran in between
        const QString lateRefusal = catalogChangeRefusal({ deviceId });
        if (!lateRefusal.isEmpty()) {
            emit splitCompleted(false, lateRefusal);
            delete dev;
            return;
        }
        const int entry = historyBefore(QStringLiteral("split"), { deviceId }, false);
        QList<int> newIds;
        Collection::CatalogOperationError error = Collection::OperationOk;
        if (!collection->executeSplitByFileType(dev, &newIds, &error)) {
            historyDiscard(entry);
            QTimer::singleShot(0, this, &AppManager::scheduleFtsIndexing);   // stopped for the split
            emit splitCompleted(false, error == Collection::OperationAiMoveFailed
                                       ? tr("Split cancelled: the AI results could not be moved to the new catalogs. Nothing was changed.")
                                       : tr("Split failed: no catalogs were created."));
        } else {
            collection->saveDeviceTableToFile();
            historyAfter(entry, newIds, tr("Catalog split into %1 catalogs.").arg(newIds.size()));
            // The new catalogs' instant index, as after a creation (MDI-F128)
            afterCatalogStructureChange();
            emit splitCompleted(true, QString());
        }
        delete dev;
    };

    if (!verifyFirst) {
        performSplit();
        return;
    }

    // Verify MIME types first, then split (MIV-F6)
    runMimeVerification(dev, [this, dev, performSplit](int mismatchCount, const QString &, const QString &error) {
        if (mismatchCount < 0) {
            // The split result dialog is plain text
            emit splitCompleted(false, tr("MIME verification failed:<br/>%1").arg(error).replace(QStringLiteral("<br/>"), QStringLiteral("\n")));
            delete dev;
            return;
        }
        performSplit();
    });
}
//----------------------------------------------------------------------
// History and restore points (fork, owner's decision 2026-10-30)
//----------------------------------------------------------------------
QString AppManager::catalogChangeRefusal(const QList<int> &deviceIds, const QList<int> &catalogIds)
{
    // A split, recompose or restore moves catalogs' rows and their AI data:
    // never under a running update, creation, search, backup or import
    if (m_deviceUpdateIsRunning || m_hasRunningOperation || !m_pendingDeviceUpdates.isEmpty()
        || m_catalogIsCreating || m_catalogUpdateForBackupRunning || m_backupThread
        || m_searchIsRunning || m_exploreLoadIsRunning || m_importIsRunning || m_exportIsRunning
        || (m_deviceUpdateManager && m_deviceUpdateManager->operationRunning()))
        return tr("A device operation is already running.");

    // Nor under the enrichment of one of these catalogs: it would go on
    // writing to a catalog that is no longer there
    if (m_mdiJob && m_mdiRunningCatalogId > 0) {
        CatalogHistory history(collection);
        QList<int> catalogs = history.scopeOf(deviceIds, true).catalogIds;
        catalogs += catalogIds;
        if (catalogs.contains(m_mdiRunningCatalogId))
            return tr("The enrichment is working on this catalog: it can be changed once the enrichment ends or is stopped.");
    }

    // The instant index pauses (it writes to the AI store the change moves
    // data in); it is scheduled again once the change is done
    stopFtsIndexing();
    if (m_ftsThread && !m_ftsThread->isFinished()) {
        QTimer::singleShot(2000, this, &AppManager::scheduleFtsIndexing);   // once it has stopped
        return tr("A device operation is already running.");
    }
    return QString();
}
//----------------------------------------------------------------------
int AppManager::historyBefore(const QString &action, const QList<int> &deviceIds, bool withDescendants,
                              const QString &label)
{
    if (!historyAvailable())
        return 0;
    CatalogHistory history(collection);
    const CatalogHistory::Scope scope = history.scopeOf(deviceIds, withDescendants);
    if (scope.isEmpty())
        return 0;
    QJsonObject extra;
    if (!label.isEmpty())
        extra.insert(QStringLiteral("label"), label);
    return history.createRestorePoint(action, scope, extra);
}
//----------------------------------------------------------------------
void AppManager::historyAfter(int entryId, const QList<int> &resultDeviceIds, const QString &undoMessage,
                              bool dropUnchanged, bool byUser)
{
    if (entryId <= 0)
        return;
    bool kept = false;
    {
        CatalogHistory history(collection);
        kept = history.completeEntry(entryId, history.scopeOf(resultDeviceIds, false), dropUnchanged);
    }
    emit historyChanged();
    if (kept && byUser && !undoMessage.isEmpty())
        emit undoAvailable(entryId, undoMessage);
}
//----------------------------------------------------------------------
void AppManager::historyDiscard(int entryId)
{
    if (entryId <= 0)
        return;
    CatalogHistory history(collection);
    history.discardEntry(entryId);
}
//----------------------------------------------------------------------
void AppManager::historyBeforeUpdate(const QString &action, const QList<int> &deviceIds, bool byUser)
{
    // Every catalog update goes through the device update manager: its end,
    // whichever handler set is connected, closes the restore point
    if (m_historyUpdateEntry > 0)
        historyAfterUpdate();   // the previous one never reported its end
    if (!m_historyHook)
        m_historyHook = new QObject(this);
    if (m_deviceUpdateManager && m_historyHookedManager != m_deviceUpdateManager) {
        m_historyHookedManager = m_deviceUpdateManager;
        connect(m_deviceUpdateManager, &DeviceUpdateManager::operationCompleted,
                m_historyHook, [this]() { historyAfterUpdate(); });
        connect(m_deviceUpdateManager, &DeviceUpdateManager::operationError,
                m_historyHook, [this](const QString &) { historyAfterUpdate(); });
        connect(m_deviceUpdateManager, &DeviceUpdateManager::operationCancelled,
                m_historyHook, [this]() { historyAfterUpdate(); });
    }
    m_historyUpdateByUser = byUser;
    m_historyUpdateEntry  = historyBefore(action, deviceIds, true);
}
//----------------------------------------------------------------------
void AppManager::historyAfterUpdate()
{
    const int entryId = m_historyUpdateEntry;
    m_historyUpdateEntry = 0;
    if (entryId <= 0)
        return;
    bool kept = false;
    QString names;
    {
        CatalogHistory history(collection);
        const CatalogHistory::Entry e = history.entry(entryId);
        // The same rows after the update; catalogs it did not change leave
        // the entry, which goes when none changed
        kept = history.completeEntry(entryId, e.before, true);
        if (kept) {
            QStringList list;
            for (const CatalogHistory::CatalogFact &f : history.entry(entryId).afterCatalogs)
                list << f.name;
            names = list.join(QStringLiteral(", "));
        }
    }
    emit historyChanged();
    if (kept && m_historyUpdateByUser)
        emit undoAvailable(entryId, tr("Updated: %1.").arg(names));
}
//----------------------------------------------------------------------
void AppManager::afterCatalogStructureChange()
{
    // Catalogs came or went: lists, Home and Explore caches keyed by device or
    // catalog number, AI data moved by path, instant index of new catalogs
    collection->saveDeviceTableToFile();
    emit deviceListChanged();
    refreshDeviceList();
    m_homeGeneration++;
    m_homeQueue.clear();
    m_homeCounting.clear();
    m_homeTypeCache.clear();
    m_homeAiCache.clear();
    m_mdiIndexCatalogId = -1;
    bumpMdiRevision(-1);
    emit homeOverviewChanged();
    // Deferred: the index pass stopped for the change must be over first
    QTimer::singleShot(0, this, &AppManager::scheduleFtsIndexing);
}
//----------------------------------------------------------------------
bool AppManager::historyAvailable()
{
    return CatalogHistory::isAvailable(collection->databaseMode, collection->folder);
}
//----------------------------------------------------------------------
QString AppManager::historyErrorText(CatalogHistory::Error error) const
{
    switch (error) {
    case CatalogHistory::NoError:
        return QString();
    case CatalogHistory::Disabled:
        return tr("History is not available for this collection.");
    case CatalogHistory::NotFound:
    case CatalogHistory::NotRestorable:
        return tr("This version can no longer be restored.");
    case CatalogHistory::IdConflict:
        return tr("This version cannot be restored in place: a number it used now belongs to another catalog or device. It can still be opened as a separate catalog.");
    case CatalogHistory::AiMoveFailed:
        return tr("The AI results could not be moved back. Nothing was changed.");
    case CatalogHistory::NoSuchCatalog:
        return tr("This catalog is not part of this version.");
    case CatalogHistory::NoParent:
        return tr("The storage or group this version was in no longer exists.");
    case CatalogHistory::DatabaseFailed:
        break;
    }
    return tr("The collection could not be changed. Nothing was changed.");
}
//----------------------------------------------------------------------
QVariantMap AppManager::historyEntryMap(const CatalogHistory::Entry &e, const CatalogHistory &history, int depth) const
{
    auto catalogsOf = [](const QList<CatalogHistory::CatalogFact> &facts) {
        QVariantList list;
        for (const CatalogHistory::CatalogFact &f : facts)
            list << QVariantMap{ { QStringLiteral("deviceId"),  f.deviceId },
                                 { QStringLiteral("catalogId"), f.catalogId },
                                 { QStringLiteral("name"),      f.name },
                                 { QStringLiteral("files"),     f.files } };
        return list;
    };
    auto namesOf = [&](const QList<CatalogHistory::CatalogFact> &facts) {
        QStringList names;
        for (const CatalogHistory::CatalogFact &f : facts)
            names << f.name;
        if (names.isEmpty())
            names << e.extra.value(QStringLiteral("label")).toString();
        if (names.size() > 3)
            return names.mid(0, 3).join(QStringLiteral(", ")) + QStringLiteral(", …");
        return names.join(QStringLiteral(", "));
    };
    auto sideOf = [](const QList<CatalogHistory::CatalogFact> &facts) {
        qint64 files = 0;
        for (const CatalogHistory::CatalogFact &f : facts)
            files += f.files;
        return tr("%1 catalog(s), %2 file(s)").arg(QLocale().toString(qlonglong(facts.size())),
                                                     QLocale().toString(files));
    };

    const QString before = namesOf(e.beforeCatalogs);
    const QString after  = namesOf(e.afterCatalogs);
    QString title;
    if (e.action == QLatin1String("update"))
        title = tr("Update of %1").arg(before);
    else if (e.action == QLatin1String("split"))
        title = tr("Split of %1 into %2 catalogs").arg(before, QString::number(e.afterCatalogs.size()));
    else if (e.action == QLatin1String("recompose"))
        title = tr("%1 catalogs recomposed into %2").arg(QString::number(e.beforeCatalogs.size()), after);
    else if (e.action == QLatin1String("delete"))
        title = tr("Deletion of %1").arg(before);
    else if (e.action == QLatin1String("edit"))
        title = tr("Change to %1").arg(before);
    else if (e.action == QLatin1String("open"))
        title = tr("Version of %1 opened as %2")
                    .arg(e.extra.value(QStringLiteral("sourceName")).toString(), after);
    else if (e.action == QLatin1String("restore")) {
        const CatalogHistory::Entry undone = history.entry(e.extra.value(QStringLiteral("restoredEntryId")).toInt());
        const QString what = undone.id > 0 && depth < 3
                             ? historyEntryMap(undone, history, depth + 1).value(QStringLiteral("title")).toString()
                             : QString();
        title = what.isEmpty() ? tr("Restored as it was before")
                               : tr("Restored as it was before: %1").arg(what);
    } else
        title = e.action;

    QString details;
    if (e.beforeCatalogs.isEmpty())
        details = tr("After: %1").arg(sideOf(e.afterCatalogs));
    else if (e.afterCatalogs.isEmpty())
        details = tr("Before: %1").arg(sideOf(e.beforeCatalogs));
    else
        details = tr("Before: %1 · After: %2").arg(sideOf(e.beforeCatalogs), sideOf(e.afterCatalogs));

    QVariantMap map;
    if (depth > 0) {
        map[QStringLiteral("title")] = title;
        return map;
    }
    const CatalogHistory::Error restorable = history.checkRestorable(e);
    const QDateTime date = QDateTime::fromString(e.date, Qt::ISODate);
    map[QStringLiteral("id")]             = e.id;
    map[QStringLiteral("date")]           = e.date;
    map[QStringLiteral("dateText")]       = QLocale().toString(date, QLocale::ShortFormat);
    map[QStringLiteral("action")]         = e.action;
    map[QStringLiteral("title")]          = title;
    map[QStringLiteral("details")]        = details;
    map[QStringLiteral("catalogs")]       = e.hasSnapshot ? catalogsOf(e.beforeCatalogs) : QVariantList();
    map[QStringLiteral("resultCatalogs")] = catalogsOf(e.afterCatalogs);
    map[QStringLiteral("canRestore")]     = restorable == CatalogHistory::NoError;
    map[QStringLiteral("restoreProblem")] = e.state == QLatin1String("undone") ? QString() : historyErrorText(restorable);
    map[QStringLiteral("undone")]         = e.state == QLatin1String("undone");
    map[QStringLiteral("kept")]           = e.hasSnapshot;
    return map;
}
//----------------------------------------------------------------------
QVariantList AppManager::historyEntries()
{
    return historyForDevice(0);
}
//----------------------------------------------------------------------
QVariantList AppManager::historyForDevice(int deviceId)
{
    QVariantList list;
    if (!historyAvailable())
        return list;
    CatalogHistory history(collection);
    for (const CatalogHistory::Entry &e : history.entries(deviceId))
        list << historyEntryMap(e, history);
    return list;
}
//----------------------------------------------------------------------
QString AppManager::restoreHistoryEntry(int entryId)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    if (!historyAvailable())
        return historyErrorText(CatalogHistory::Disabled);
    int restorePoint = 0;
    CatalogHistory::Error error = CatalogHistory::NoError;
    {
        CatalogHistory history(collection);
        const CatalogHistory::Entry e = history.entry(entryId);
        if (e.id == 0)
            return historyErrorText(CatalogHistory::NotFound);
        const QString refusal = catalogChangeRefusal(e.before.united(e.after).deviceIds,
                                                     e.before.united(e.after).catalogIds);
        if (!refusal.isEmpty())
            return refusal;
        error = history.restore(entryId, &restorePoint);
    }
    if (error != CatalogHistory::NoError) {
        // Nothing changed; the index pass stopped for it starts again
        QTimer::singleShot(0, this, &AppManager::scheduleFtsIndexing);
        return historyErrorText(error);
    }
    emit historyChanged();
    afterCatalogStructureChange();
    emit undoAvailable(restorePoint, tr("Restored as it was before."));
    return QString();
}
//----------------------------------------------------------------------
QString AppManager::openHistoryVersion(int entryId, int catalogId, const QString &nameSuffix)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    if (!historyAvailable())
        return historyErrorText(CatalogHistory::Disabled);
    const QString refusal = catalogChangeRefusal({});
    if (!refusal.isEmpty())
        return refusal;
    int newDevice = 0, openEntry = 0;
    CatalogHistory::Error error = CatalogHistory::NoError;
    {
        CatalogHistory history(collection);
        error = history.openAsCatalog(entryId, catalogId, nameSuffix, &newDevice, &openEntry);
    }
    if (error != CatalogHistory::NoError) {
        QTimer::singleShot(0, this, &AppManager::scheduleFtsIndexing);
        return historyErrorText(error);
    }
    Device opened;
    opened.ID = newDevice;
    opened.loadDevice(m_connectionName);
    emit historyChanged();
    afterCatalogStructureChange();
    emit undoAvailable(openEntry, tr("Version opened as a separate catalog: %1.").arg(opened.name));
    return QString();
}
//----------------------------------------------------------------------
QVariantMap AppManager::recomposeProposal(int deviceId)
{
    const Collection::RecomposeProposal proposal = collection->proposeRecompose(deviceId);
    QVariantList catalogs;
    for (int id : proposal.deviceIds) {
        Device part;
        part.ID = id;
        part.loadDevice(m_connectionName);
        catalogs << QVariantMap{ { QStringLiteral("deviceId"), id },
                                 { QStringLiteral("name"),     part.name },
                                 { QStringLiteral("files"),    part.totalFileCount } };
    }
    QString reason;
    if (proposal.problem == QLatin1String("type"))
        reason = tr("Only the catalogs of a storage, of a group, or of a split can be recomposed.");
    else if (proposal.problem == QLatin1String("few"))
        reason = tr("At least two catalogs are needed to recompose.");
    else if (proposal.problem == QLatin1String("nocommon"))
        reason = tr("These catalogs have no folder in common.");
    return QVariantMap{ { QStringLiteral("name"),     proposal.name },
                        { QStringLiteral("path"),     proposal.path },
                        { QStringLiteral("catalogs"), catalogs },
                        { QStringLiteral("possible"), proposal.problem.isEmpty() },
                        { QStringLiteral("reason"),   reason } };
}
//----------------------------------------------------------------------
QString AppManager::recomposeCatalogs(const QVariantList &deviceIds, const QString &name)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    QList<int> ids;
    for (const QVariant &v : deviceIds)
        ids << v.toInt();
    if (name.trimmed().isEmpty())
        return tr("Provide a name for this new catalog.");
    const QString refusal = catalogChangeRefusal(ids);
    if (!refusal.isEmpty())
        return refusal;

    const int entry = historyBefore(QStringLiteral("recompose"), ids, false);
    int newDevice = 0;
    const Collection::CatalogOperationError error = collection->executeRecompose(ids, name, &newDevice);
    if (error != Collection::OperationOk) {
        historyDiscard(entry);
        QTimer::singleShot(0, this, &AppManager::scheduleFtsIndexing);
        switch (error) {
        case Collection::OperationTooFew:
            return tr("At least two catalogs are needed to recompose.");
        case Collection::OperationNoCommonFolder:
            return tr("These catalogs have no folder in common.");
        case Collection::OperationNameTaken:
            return tr("There is already a catalog with this name: %1\nChoose a different name.").arg(name.trimmed());
        case Collection::OperationAiMoveFailed:
            return tr("Recomposition cancelled: the AI results could not be moved to the new catalog. Nothing was changed.");
        default:
            return tr("The collection could not be changed. Nothing was changed.");
        }
    }
    historyAfter(entry, { newDevice }, tr("%1 catalogs recomposed into %2.").arg(QString::number(ids.size()), name.trimmed()));
    afterCatalogStructureChange();
    return QString();
}
//----------------------------------------------------------------------
void AppManager::verifyMimeTypes(int deviceId)
{
    if (getReadOnly()) { emit mimeVerificationFinished(-1, QString(), readOnlyRefusal()); return; }   // read-only collection (D43)
    Device *dev = new Device();
    dev->ID = deviceId;
    dev->loadDevice(m_connectionName);
    if (dev->type != QLatin1String("Catalog") || !dev->active || !dev->catalog) {
        delete dev;
        return;
    }
    runMimeVerification(dev, [this, dev](int mismatchCount, const QString &reportPath, const QString &error) {
        delete dev;
        emit mimeVerificationFinished(mismatchCount, reportPath, error);
    });
}
//----------------------------------------------------------------------
void AppManager::runMimeVerification(Device *dev,
                                     std::function<void(int, const QString &, const QString &)> onDone)
{
    // Refused while another device operation or a search runs (MIV-F2)
    if (m_deviceUpdateIsRunning || m_catalogIsCreating || m_searchIsRunning) {
        onDone(-1, QString(), tr("A device operation is already running."));
        return;
    }

    m_deviceUpdateIsRunning  = true;
    m_deviceUpdateStatusText = StatusBarMessageBuilder()
                                   .setOperation(tr("Verify MIME types"))
                                   .setStatus(tr("In Progress"))
                                   .setCatalogName(dev->name)
                                   .build();
    emit deviceUpdateStateChanged();
    emit deviceUpdateStatusChanged();

    // Deferred one event-loop turn so the activity panel paints before the scan
    // blocks the GUI thread — same reason as in createCatalog().
    QTimer::singleShot(kStartDeferralMs, this, [this, dev, onDone]() {
        // Memory mode: the job reads and updates the file table (MIV-F5)
        if (collection->databaseMode == "Memory") {
            QMutex mutex;
            bool stop = false;
            dev->catalog->loadCatalogFileListToTable(mutex, stop);
        }

        CatalogJobStoppable *job = new CatalogJobStoppable(this);
        // The report goes to the browse folder: Hosted mode has no collection folder
        job->configureOperation(dev, CatalogJobStoppable::VerifyMimeTypes,
                                collection->databaseMode, getCollectionBrowsePath(), m_connectionName);

        auto finish = [this, job, onDone](int mismatchCount, const QString &reportPath, const QString &error) {
            job->deleteLater();
            m_deviceUpdateIsRunning = false;
            m_deviceUpdateStatusText.clear();
            emit deviceUpdateStateChanged();
            emit deviceUpdateStatusChanged();
            onDone(mismatchCount, reportPath, error);
        };
        connect(job, &CatalogJobStoppable::catalogProgress, this,
                [this, dev](qint64 processed, qint64 total, const QString &) {
                    m_deviceUpdateStatusText = StatusBarMessageBuilder()
                                                   .setOperation(tr("Verify MIME types"))
                                                   .setStatus(tr("In Progress"))
                                                   .setCatalogName(dev->name)
                                                   .setProcess(tr("Evaluated"), processed, total)
                                                   .build();
                    emit deviceUpdateStatusChanged();
                });
        connect(job, &CatalogJobStoppable::mimeVerificationCompleted, this,
                [this, dev, finish](int mismatchCount, const QString &reportPath) {
                    // Memory mode: keep the verified types in the .idx (MIV-F5)
                    dev->catalog->saveCatalogToFile(collection->databaseMode, collection->folder);
                    finish(mismatchCount, reportPath, QString());
                });
        connect(job, &CatalogJobStoppable::catalogOperationError, this,
                [finish](const QString &error) { finish(-1, QString(), error); });

        job->processCatalog();
    });
}
//----------------------------------------------------------------------
QVariantMap AppManager::verifyDeviceChecksums(int deviceId)
{
    Device dev;
    dev.ID = deviceId;
    dev.loadDevice(m_connectionName);

    if (!dev.catalog) return QVariantMap{{ "error", tr("Device has no catalog.") }};

    if (collection->databaseMode == "Memory") {
        bool stop = false;
        QMutex mutex;
        dev.catalog->loadCatalogFileListToTable(mutex, stop);
    }

    int total = FileChecksum::countFilesWithChecksum(m_connectionName, dev.externalID);
    if (total == 0)
        return QVariantMap{{ "noChecksums", true }, { "verified", 0 }, { "mismatches", 0 }, { "missing", 0 }};

    bool continueFlag = true;
    auto shouldContinue = [&]() { return continueFlag; };
    auto progressCb = [this](int current, int total, const QString &) {
        m_deviceUpdateStatusText = tr("Verifying checksums: %1 / %2").arg(current).arg(total);
        emit deviceUpdateStatusChanged();
        QCoreApplication::processEvents();
    };

    auto res = FileChecksum::verifyCatalogChecksums(
        m_connectionName,
        dev.externalID, dev.catalog->sourcePath,
        shouldContinue, progressCb);

    m_deviceUpdateStatusText.clear();
    emit deviceUpdateStatusChanged();

    QVariantMap result;
    result["noChecksums"] = false;
    result["total"]       = total;
    result["verified"]    = res.verified;
    result["mismatches"]  = res.mismatches;
    result["missing"]     = res.missing;
    QStringList mm; for (const auto &f : res.mismatchedFiles) mm << f;
    QStringList ms; for (const auto &f : res.missingFiles)    ms << f;
    result["mismatchedFiles"] = mm;
    result["missingFiles"]    = ms;
    return result;
}
//----------------------------------------------------------------------
QString AppManager::unassignDevice(int deviceId, int parentId)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    const QString conn = m_connectionName;
    if (!Device::unassignFromDevice(deviceId, parentId, conn))
        return tr("Failed to unassign device.");
    if (collection->databaseMode == "Memory")
        collection->saveDeviceTableToFile();
    emit deviceListChanged();
    refreshDeviceList();
    return QString();
}
//----------------------------------------------------------------------
QString AppManager::importFromVVV(const QString &path)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    QFile sourceFile(path);

    if (!sourceFile.open(QIODevice::ReadOnly))
        return tr("Could not open file.");

    QTextStream ts(&sourceFile);
    QString line = ts.readLine();
    if (!line.startsWith("Volume")) {
        sourceFile.close();
        return tr("File format not recognized (expected VVV CSV).");
    }

    QString dt = "_" + QDateTime::currentDateTime().toString("yy-MM-dd hh-mm-ss");
    QSet<QString> uniqueNames;
    while (!ts.atEnd()) {
        line = ts.readLine();
        if (line.isEmpty()) continue;
        QStringList f = line.split('\t');
        if (f.count() == 7) {
            QString n = QString(f[0]).remove('"').replace('/', '_') + dt;
            uniqueNames.insert(n);
        }
    }
    sourceFile.close();

    if (uniqueNames.isEmpty())
        return tr("No catalog data found in file.");

    // Create virtual device to host imported catalogs
    Device importVirtual;
    importVirtual.generateDeviceID();
    importVirtual.type     = "Virtual";
    importVirtual.name     = "imports from VVV" + dt;
    importVirtual.parentID = 0;
    importVirtual.groupID  = 1;
    importVirtual.path     = "/import";
    importVirtual.dateTimeUpdated = QDateTime::currentDateTime();
    importVirtual.insertDevice();
    collection->saveDeviceTableToFile();

    QMap<QString, Device*>  nameToDev;

    for (const QString &catName : uniqueNames) {
        Device *d = new Device();
        d->generateDeviceID();
        // Catalog names are unique among devices (STI-C18): a clash gets " (2)",
        // " (3)"… as Collection Import does.
        d->name     = catName;
        for (int suffix = 2; d->verifyDeviceNameExists(); ++suffix)
            d->name = QString("%1 (%2)").arg(catName).arg(suffix);
        d->type     = "Catalog";
        d->parentID = importVirtual.ID;
        d->groupID  = 1;
        d->path     = "/import";
        d->catalog->generateID();
        d->externalID = d->catalog->ID;
        d->dateTimeUpdated = QDateTime::currentDateTime();
        d->insertDevice();
        d->catalog->name        = d->name;
        d->catalog->filePath    = collection->folder + "/" + d->name + ".idx";
        d->catalog->sourcePath  = "/import";
        d->catalog->includeHidden     = 1;
        d->catalog->includeSymblinks  = 0;
        d->catalog->isFullDevice      = 0;
        d->catalog->includeMetadata   = Catalog::METADATA_NONE;
        d->catalog->appVersion        = currentVersion;
        d->catalog->insertCatalog();
        nameToDev[catName] = d;
    }

    // Second pass: group the file lines by catalog. No clearing of the
    // file/folder tables: in File and Hosted mode they hold every catalog.
    struct CatalogFiles { QList<QString> names, folders, dates; QList<qint64> sizes; };
    QMap<QString, CatalogFiles> filesByCatalog;

    if (!sourceFile.open(QIODevice::ReadOnly))
        return tr("Could not open file: %1").arg(path);
    ts.setDevice(&sourceFile);
    ts.readLine(); // skip header
    while (!ts.atEnd()) {
        line = ts.readLine();
        if (line.isEmpty()) continue;
        QStringList f = line.split('\t');
        if (f.count() != 7) continue;
        QString catName = QString(f[0]).remove('"').replace('/', '_') + dt;
        if (!nameToDev.contains(catName)) continue;
        CatalogFiles &files = filesByCatalog[catName];
        files.names   << QString(f[2]).remove('"');
        files.folders << "/import" + QString(f[1]).remove('"');
        files.sizes   << f[3].toLongLong();
        files.dates   << f[5];
    }
    sourceFile.close();

    // Insert, count, and write the Memory-mode .idx files (core)
    for (auto it = nameToDev.begin(); it != nameToDev.end(); ++it) {
        Device *d = it.value();
        const CatalogFiles files = filesByCatalog.value(it.key());
        d->catalog->insertFileList(files.names, files.folders, files.sizes, files.dates, "/import");
        d->catalog->updateFileCount();
        d->catalog->updateTotalFileSize();
        d->totalFileCount = d->catalog->fileCount;
        d->totalFileSize  = d->catalog->totalFileSize;
        d->saveDevice();
        d->catalog->saveCatalogToFile(collection->databaseMode, collection->folder);
        d->catalog->saveFoldersToFile(collection->databaseMode, collection->folder);
    }

    qDeleteAll(nameToDev);
    importVirtual.updateNumbersFromChildren();
    collection->saveDeviceTableToFile();
    if (collection->databaseMode == "Memory")
        collection->loadCatalogFilesToTable();

    refreshAllUI();
    return QString();
}
//----------------------------------------------------------------------

// Device edit
//----------------------------------------------------------------------
QVariantMap AppManager::getDeviceDetails(int deviceId) const
{
    const QString conn = m_connectionName;
    Device dev;
    dev.ID = deviceId;
    dev.loadDevice(conn);

    QVariantMap r;
    r["type"]     = dev.type;
    r["name"]     = dev.name;
    r["parentId"] = dev.parentID;
    r["path"]     = dev.path;
    r["deviceId"] = dev.ID;
    r["groupId"]  = dev.groupID;
    r["comment"]  = dev.comment;

    if (dev.type == "Catalog") {
        r["fileType"]        = dev.catalog->fileType;
        r["includeSubDir"]   = dev.catalog->includeSubDir;
        r["includeHidden"]   = dev.catalog->includeHidden;
        r["includeMetadata"] = dev.catalog->includeMetadata;
        r["includeChecksum"] = dev.catalog->includeChecksum;
        r["isFullDevice"]    = dev.catalog->isFullDevice;
        r["excludeFolders"]  = dev.catalog->getExcludeFolders();
    } else if (dev.type == "Storage") {
        // The user's number (STI-F10); device_external_id is the internal key and
        // is not shown or editable.
        r["storageExtId"]         = dev.storage->userID;
        r["storageType"]          = dev.storage->type;
        r["storageLabel"]         = dev.storage->label;
        r["storageFileSystem"]    = dev.storage->fileSystem;
        r["totalSpace"]           = (qlonglong)dev.totalSpace;
        r["freeSpace"]            = (qlonglong)dev.freeSpace;
        r["storageBrand"]         = dev.storage->brand;
        r["storageModel"]         = dev.storage->model;
        r["storageSerialNumber"]  = dev.storage->serialNumber;
        r["storageBuildDate"]     = dev.storage->buildDate;
        r["storageComment1"]      = dev.storage->comment1;
        r["storageComment2"]      = dev.storage->comment2;
        r["storageComment3"]      = dev.storage->comment3;
        r["storagePicturePath"]   = dev.storage->picturePath;
    }
    return r;
}
//----------------------------------------------------------------------
QString AppManager::saveDeviceBasicFields(int deviceId, const QString &name, int parentId, const QString &path, const QString &comment)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    const QString conn = m_connectionName;
    Device dev;
    dev.ID = deviceId;
    dev.loadDevice(conn);

    // Remember the parent the device had before this edit, so a re-parent can
    // refresh the chain it was moved away from (see end of method).
    const int previousParentId = dev.parentID;
    const QString previousName = dev.name;

    // Device id 1 is the reserved " Physical Group" root: its parent is fixed at
    // root (which keeps it in the Physical group, id 0). Force it here so no UI
    // path — including a stale parent picker — can ever re-parent it.
    if (deviceId == 1)
        parentId = 0;

    // Name uniqueness for Catalog
    if (dev.type == "Catalog" && name != dev.name) {
        Device check;
        check.name = name;
        if (check.verifyDeviceNameExists())
            return tr("There is already a catalog with this name: %1\nChoose a different name.").arg(name);
    }

    // Parent + groupId cascade
    Device newParent;
    newParent.ID = parentId;
    if (parentId > 0)
        newParent.loadDevice(conn);

    if (dev.type == "Catalog" && dev.groupID == 0 && parentId > 0
        && newParent.type != "Storage") {
        return tr("A Catalog in the Physical group can only be set under a Storage device.");
    }

    // Restore point (fork) of a change that is really one: name, place, path
    // or note. A catalog brings its files (their catalog name changes too).
    const bool edited = name != previousName || parentId != previousParentId
                     || Catalog::normalizeSourcePath(path) != dev.path || comment != dev.comment;
    const int historyEntry = edited ? historyBefore(QStringLiteral("edit"), { deviceId }, false, previousName) : 0;

    dev.name = name;
    // loadDevice() above populated the comment, so the full-row UPDATE in
    // saveDevice() cannot blank it (SpecDeviceComment.md DCM-C2).
    dev.comment = comment;
    dev.path = Catalog::normalizeSourcePath(path);
    dev.catalog->sourcePath = dev.path;

    // At root (parentId 0) keep the device's current group. K2 forced group 1
    // here ("root == Virtual"), but the seeded " Physical Group" (device id 1)
    // is a group-0 device that lives at root — forcing 1 would cascade-flip the
    // whole physical hierarchy into the Virtual group. Combined with the
    // same-group parent filter, the group can now never change on edit.
    int newGroupId = (parentId == 0) ? dev.groupID : newParent.groupID;
    if (dev.groupID != newGroupId) {
        dev.loadDevice(conn); // reload to get deviceIDList
        Device sub;
        for (int id : dev.deviceIDList) {
            sub.ID = id;
            sub.loadDevice(conn);
            sub.groupID = newGroupId;
            sub.saveDevice();
        }
    }

    dev.parentID = parentId;
    dev.groupID  = newGroupId;
    dev.saveDevice();

    // A catalog's synced copies of its name (SpecStorageIdentity.md STI-F14)
    if (name != previousName)
        dev.writeNameCopies(collection->databaseMode);

    // On a re-parent, recompute the file count / size / space of the parent the
    // device left and the parent it joined, then propagate up to their Virtual
    // ancestors — otherwise Storage and upper Virtual devices keep stale totals.
    // Mirrors K2 saveDeviceForm, which updates both the previous and new parent
    // chains (updateNumbersFromChildren + updateParentsNumbers on each).
    if (previousParentId != parentId) {
        if (previousParentId > 0) {
            Device oldParent;
            oldParent.ID = previousParentId;
            oldParent.loadDevice(conn);
            oldParent.updateNumbersFromChildren();
            oldParent.updateParentsNumbers();
        }
        if (parentId > 0) {
            Device newParentChain;
            newParentChain.ID = parentId;
            newParentChain.loadDevice(conn);
            newParentChain.updateNumbersFromChildren();
            newParentChain.updateParentsNumbers();
        }
    }

    collection->saveDeviceTableToFile();
    historyAfter(historyEntry, { deviceId }, tr("Changes saved: %1.").arg(name));
    return {};
}
//----------------------------------------------------------------------
QVariantMap AppManager::checkCatalogOptionChanges(int deviceId, const QString &fileType, bool includeSubDir,
                                                   bool includeHidden,
                                                   const QString &includeMetadata, const QString &includeChecksum,
                                                   bool isFullDevice) const
{
    const QString conn = m_connectionName;
    Device prev;
    prev.ID = deviceId;
    prev.loadDevice(conn);

    QStringList changedFields;
    bool rescanNeeded = false;

    if (fileType != prev.catalog->fileType) {
        changedFields << tr("File type: %1 → %2").arg(prev.catalog->fileType, fileType);
        rescanNeeded = true;
    }
    if (includeSubDir != prev.catalog->includeSubDir) {
        changedFields << tr("Include subdirectories: %1 → %2")
                             .arg(prev.catalog->includeSubDir ? tr("yes") : tr("no"),
                                  includeSubDir               ? tr("yes") : tr("no"));
        rescanNeeded = true;
    }
    if (includeHidden != prev.catalog->includeHidden) {
        changedFields << tr("Include hidden: %1 → %2")
                             .arg(prev.catalog->includeHidden ? tr("All") : tr("None"),
                                  includeHidden              ? tr("All") : tr("None"));
        rescanNeeded = true;
    }
    if (includeMetadata != prev.catalog->includeMetadata) {
        changedFields << tr("Include metadata: %1 → %2").arg(prev.catalog->includeMetadata, includeMetadata);
        rescanNeeded = true;
    }
    if (includeChecksum != prev.catalog->includeChecksum) {
        changedFields << tr("Include checksum: %1 → %2").arg(prev.catalog->includeChecksum, includeChecksum);
        bool toNone = (prev.catalog->includeChecksum != Catalog::CHECKSUM_NONE
                       && includeChecksum == Catalog::CHECKSUM_NONE);
        if (!toNone) rescanNeeded = true;
    }
    if (isFullDevice != prev.catalog->isFullDevice) {
        changedFields << tr("Is full device: %1 → %2")
                             .arg(prev.catalog->isFullDevice ? tr("yes") : tr("no"),
                                  isFullDevice               ? tr("yes") : tr("no"));
        rescanNeeded = true;
    }

    QVariantMap result;
    result["needsConfirmation"] = !changedFields.isEmpty();
    result["message"]           = changedFields.join('\n');
    result["rescanNeeded"]      = rescanNeeded;
    return result;
}
//----------------------------------------------------------------------
QString AppManager::saveCatalogOptions(int deviceId, const QString &fileType, bool includeSubDir,
                                        bool includeHidden,
                                        const QString &includeMetadata, const QString &includeChecksum,
                                        bool isFullDevice)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    const QString conn = m_connectionName;
    Device dev;
    dev.ID = deviceId;
    dev.loadDevice(conn);

    const QString prevMetadata = dev.catalog->includeMetadata;

    // Restore point (fork) when an option really changes: the metadata
    // transition below can clear fields of every file
    const bool changed = fileType != dev.catalog->fileType || includeSubDir != dev.catalog->includeSubDir
                      || includeHidden != dev.catalog->includeHidden || includeMetadata != prevMetadata
                      || includeChecksum != dev.catalog->includeChecksum || isFullDevice != dev.catalog->isFullDevice;
    const int historyEntry = changed ? historyBefore(QStringLiteral("edit"), { deviceId }, false, dev.name) : 0;

    dev.catalog->fileType        = fileType;
    dev.catalog->includeSubDir   = includeSubDir;
    dev.catalog->includeHidden   = includeHidden;
    dev.catalog->includeMetadata = includeMetadata;
    dev.catalog->includeChecksum = includeChecksum;
    dev.catalog->isFullDevice    = isFullDevice;

    dev.catalog->saveCatalog();
    dev.catalog->updateCatalogFileHeaders(collection->databaseMode);
    dev.catalog->renameCatalogFile(dev.name);

    if (prevMetadata != includeMetadata)
        dev.catalog->handleMetadataTransition(prevMetadata, includeMetadata);

    historyAfter(historyEntry, { deviceId }, tr("Changes saved: %1.").arg(dev.name));
    return {};
}
//----------------------------------------------------------------------
QString AppManager::saveStorageDetails(int deviceId, const QVariantMap &fields)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    const QString conn = m_connectionName;
    Device dev;
    dev.ID = deviceId;
    dev.loadDevice(conn);

    // The form edits the user's number, never device_external_id / storage_id
    // (STI-C1, STI-C6). The previous code reassigned dev.externalID here and then
    // matched the UPDATE below on the already-new value, so on an ID change the
    // whole statement matched nothing: every storage field was silently discarded
    // while dev.saveDevice() had already re-pointed the device row — an orphan.
    const int newUserId = fields.value("storageExtId", dev.storage->userID).toInt();
    // A duplicate warns and the save proceeds (STI-F5); the number is the user's
    // label for a disk, not a key.
    const bool userIdIsDuplicate =
        (newUserId != dev.storage->userID)
        && Device::storageUserIDExists(newUserId, dev.externalID, conn);

    dev.totalSpace = fields.value("totalSpace", (qlonglong)dev.totalSpace).toLongLong();
    dev.freeSpace  = fields.value("freeSpace",  (qlonglong)dev.freeSpace).toLongLong();
    dev.saveDevice();

    dev.storage->type         = fields.value("storageType",         dev.storage->type).toString();
    dev.storage->label        = fields.value("storageLabel",        dev.storage->label).toString();
    dev.storage->fileSystem   = fields.value("storageFileSystem",   dev.storage->fileSystem).toString();
    dev.storage->brand        = fields.value("storageBrand",        dev.storage->brand).toString();
    dev.storage->model        = fields.value("storageModel",        dev.storage->model).toString();
    dev.storage->serialNumber = fields.value("storageSerialNumber", dev.storage->serialNumber).toString();
    dev.storage->buildDate    = fields.value("storageBuildDate",    dev.storage->buildDate).toString();
    dev.storage->comment1     = fields.value("storageComment1",     dev.storage->comment1).toString();
    dev.storage->comment2     = fields.value("storageComment2",     dev.storage->comment2).toString();
    dev.storage->comment3     = fields.value("storageComment3",     dev.storage->comment3).toString();
    dev.storage->picturePath  = fields.value("storagePicturePath",  dev.storage->picturePath).toString();

    dev.storage->totalSpace = dev.totalSpace;
    dev.storage->freeSpace  = dev.freeSpace;
    dev.storage->userID     = newUserId;
    const QString error = dev.storage->saveStorage();
    if (!error.isEmpty())
        return error;

    collection->saveStorageTableToFile();

    if (userIdIsDuplicate)
        emit storageUserIdDuplicate();
    return {};
}
//----------------------------------------------------------------------
void AppManager::triggerDeviceRescan(int deviceId)
{
    if (getReadOnly()) return;   // read-only collection (D43)
    // Delegate rather than drive the manager directly. Calling
    // updateDeviceHierarchy() here set no running flag, so the activity panel
    // never showed the work and nothing reported its progress or completion;
    // it also bypassed the queue, so a rescan could start on top of another
    // operation. updateDevice() is the one entry point that does all of that.
    updateDevice(deviceId);
}
//----------------------------------------------------------------------
void AppManager::triggerStoragePathReplace(int deviceId, const QString &previousPath, const QString &newPath)
{
    if (getReadOnly()) return;   // read-only collection (D43)
    // Requested through the queue, exactly like a device update (DSR-C6): driving
    // DeviceUpdateManager directly set no running flag, so the replacement was
    // invisible in the activity panel and could start on top of another operation.
    // Refused rather than queued while a search runs, for the reason updateDevice()
    // gives: nothing drains the queue when a search ends.
    if (m_searchIsRunning) {
        qWarning() << "AppManager::triggerStoragePathReplace: refused, a search is running";
        return;
    }

    Device dev;
    dev.ID = deviceId;
    dev.loadDevice(m_connectionName);

    if (m_deviceUpdateIsRunning || (m_deviceUpdateManager && m_deviceUpdateManager->operationRunning())) {
        enqueueReplaceRoot(deviceId, dev.name, previousPath, newPath);
        return;
    }

    m_isBatchUpdate = false;
    // m_showEachUpdateReport is deliberately left alone: it is a drain-wide mode,
    // not a per-entry one, and an update queued behind this replacement still owes
    // the user its report. The replacement opens no dialog of its own because
    // onDevicePageUpdateCompleted() returns before that flag is consulted (DSR-F8).
    enqueueReplaceRoot(deviceId, dev.name, previousPath, newPath);
    startNextDeviceUpdate();
}
//----------------------------------------------------------------------
QStringList AppManager::getDeviceExcludeFolders(int deviceId) const
{
    Device dev;
    dev.ID = deviceId;
    dev.loadDevice(m_connectionName);
    if (dev.type != "Catalog") return {};
    return dev.catalog->getExcludeFolders();
}
//----------------------------------------------------------------------
bool AppManager::addDeviceExcludeFolder(int deviceId, const QString &path)
{
    if (getReadOnly()) return false;   // read-only collection (D43)
    Device dev;
    dev.ID = deviceId;
    dev.loadDevice(m_connectionName);
    if (dev.type != "Catalog") return false;
    bool ok = dev.catalog->addExcludeFolder(path.trimmed());
    if (ok) collection->saveCatalogFilterTableToFile();
    return ok;
}
//----------------------------------------------------------------------
bool AppManager::removeDeviceExcludeFolder(int deviceId, const QString &path)
{
    if (getReadOnly()) return false;   // read-only collection (D43)
    Device dev;
    dev.ID = deviceId;
    dev.loadDevice(m_connectionName);
    if (dev.type != "Catalog") return false;
    bool ok = dev.catalog->removeExcludeFolder(path);
    if (ok) collection->saveCatalogFilterTableToFile();
    return ok;
}

// Explore page
//----------------------------------------------------------------------
QVariantMap AppManager::exploreOpenCatalog(int deviceId)
{
    QVariantMap result;
    result[QStringLiteral("deviceId")] = deviceId;

    // A load is running: this request arrived through its pumped event loop.
    // It must not nest; it replaces any earlier pending one and stops the
    // running load, then runs once that load has returned (EXP-F31, EXP-C23).
    if (m_exploreLoadIsRunning) {
        if (deviceId != m_exploreLoadingId) {
            m_explorePendingId = deviceId;
            QMutexLocker locker(&m_exploreMutex);
            m_exploreStopRequested = true;
        }
        result[QStringLiteral("deferred")] = true;
        return result;
    }

    // One operation at a time (EXP-F32, OPQ-F20)
    if (m_catalogIsCreating || m_deviceUpdateIsRunning || m_searchIsRunning
        || (m_deviceUpdateManager && m_deviceUpdateManager->operationRunning())) {
        result[QStringLiteral("refused")] = true;
        result[QStringLiteral("message")] = tr("A device operation is already running.");
        return result;
    }

    // Read into a separate device: Explore keeps reading the catalog it shows
    // until the new one is fully loaded (EXP-C24).
    const QString conn = m_connectionName;
    Device candidate;
    candidate.catalog->setConnectionName(conn);
    candidate.setConnectionName(conn);
    candidate.ID = deviceId;
    candidate.loadDevice(conn);

    if (candidate.type != QLatin1String("Catalog")) {
        result[QStringLiteral("success")] = false;
        result[QStringLiteral("message")] = tr("Selected device is not a catalog");
        return result;
    }

    // Memory mode with files to load: reported, stoppable, and started after
    // the panel has reached the screen (EXP-F28, OPQ-F11, OPQ-C13). Core's
    // own test decides whether there is anything to load (EXP-C22, EXP-F29).
    if (collection->databaseMode == QLatin1String("Memory")
        && candidate.catalog->dateLoaded < candidate.catalog->dateUpdated) {
        m_exploreLoadIsRunning = true;
        m_exploreLoadingId     = deviceId;
        m_exploreStopRequested = false;
        m_exploreStatusText = StatusBarMessageBuilder()
                                  .setOperation(tr("Explore"))
                                  .setStatus(tr("In Progress"))
                                  .setDeviceContext(1, 1, candidate.name)
                                  .build();
        emit exploreLoadStateChanged();
        emit exploreStatusTextChanged();
        QTimer::singleShot(kStartDeferralMs, this, [this, deviceId]() { runExploreLoad(deviceId); });
        result[QStringLiteral("loading")] = true;
        return result;
    }

    return finishExploreOpen(deviceId);
}
//----------------------------------------------------------------------
void AppManager::runExploreLoad(int deviceId)
{
    const QString conn = m_connectionName;
    Device candidate;
    candidate.catalog->setConnectionName(conn);
    candidate.setConnectionName(conn);
    candidate.ID = deviceId;
    candidate.loadDevice(conn);

    // Progress keeps the window alive and the Stop reachable: the load runs
    // on this thread, as Search's Memory-mode load does (EXP-C22).
    int loaded = 0, total = 0;
    const QString name = candidate.name;
    auto build = [this, &name](const QString &status, int n, int of) {
        StatusBarMessageBuilder builder;
        builder.setOperation(tr("Explore")).setStatus(status).setDeviceContext(1, 1, name);
        if (of > 0)
            builder.setProcess(tr("Loaded"), n, of);
        return builder.build();
    };
    const QMetaObject::Connection progress = connect(candidate.catalog, &Catalog::loadProgress, this,
        [&](int filesLoaded, int totalFiles) {
            loaded = filesLoaded;
            total  = totalFiles;
            m_exploreStatusText = build(tr("In Progress"), loaded, total);
            emit exploreStatusTextChanged();
            QCoreApplication::processEvents();
        }, Qt::DirectConnection);

    candidate.catalog->loadFoldersToTable();
    QCoreApplication::processEvents();
    if (!m_exploreStopRequested)
        candidate.catalog->loadCatalogFileListToTable(m_exploreMutex, m_exploreStopRequested);
    disconnect(progress);

    const bool stopped = m_exploreStopRequested;
    m_exploreLoadIsRunning = false;
    m_exploreLoadingId     = -1;
    m_exploreStopRequested = false;

    QVariantMap info;
    info[QStringLiteral("deviceId")] = deviceId;
    if (stopped) {
        // Interrupted by a newer open: no final line, the next load follows
        // at once (EXP-F31). Stopped by the user: Cancelled (EXP-F30).
        m_exploreStatusText = (m_explorePendingId >= 0) ? QString()
                                                        : build(tr("Cancelled"), loaded, total);
        info[QStringLiteral("stopped")] = true;
    } else {
        m_exploreStatusText = build(tr("Completed"), total, total);
        info = finishExploreOpen(deviceId);
    }
    emit exploreStatusTextChanged();
    emit exploreLoadStateChanged();
    emit exploreCatalogOpened(info);

    if (m_explorePendingId >= 0) {
        const int next = m_explorePendingId;
        m_explorePendingId = -1;
        emit exploreOpenPending(next);
    }
}
//----------------------------------------------------------------------
void AppManager::stopExploreLoad()
{
    QMutexLocker locker(&m_exploreMutex);
    if (m_exploreLoadIsRunning)
        m_exploreStopRequested = true;
}
//----------------------------------------------------------------------
// Switches Explore to the catalog, once nothing is left to load (EXP-C24).
QVariantMap AppManager::finishExploreOpen(int deviceId)
{
    QVariantMap result;
    const QString conn = m_connectionName;

    exploreDevice->catalog->setConnectionName(conn);
    exploreDevice->setConnectionName(conn);
    exploreDevice->ID = deviceId;
    exploreDevice->loadDevice(conn);

    if (collection->databaseMode == QLatin1String("Memory"))
        exploreDevice->catalog->loadFoldersToTable();   // no-op when already in memory

    int folderCount = Catalog::getExploreFolderCount(conn, exploreDevice->externalID);

    result[QStringLiteral("deviceId")]    = deviceId;
    result[QStringLiteral("success")]     = true;
    result[QStringLiteral("name")]        = exploreDevice->name;
    result[QStringLiteral("path")]        = exploreDevice->path;
    result[QStringLiteral("externalId")]  = exploreDevice->externalID;
    result[QStringLiteral("folderCount")] = folderCount;
    return result;
}
//----------------------------------------------------------------------
QVariantList AppManager::getExploreFolders()
{
    QVariantList result;
    if (exploreDevice->externalID == 0) return result;

    const QString conn = m_connectionName;
    const QString catalogPath = exploreDevice->path;
    int pos = catalogPath.lastIndexOf(QLatin1Char('/'));
    const QString pathRoot = (pos >= 0) ? catalogPath.left(pos) : QString();

    QList<FolderNode*> nodes = FolderTreeLoader::loadExploreTree(conn, exploreDevice->externalID, pathRoot);

    // Siblings in K2's tree order, which is also the file list's: the query's
    // binary ORDER BY put "_dir" after "Zeta" (EXP-F33, as K2 ExploreTreeView).
    const bool caseSensitive = m_fileSortCaseSensitive;
    auto nameLess = [caseSensitive](const FolderNode *a, const FolderNode *b) {
        return caseSensitive ? a->name < b->name
                             : QString::compare(a->name, b->name, Qt::CaseInsensitive) < 0;
    };

    std::function<void(QList<FolderNode*>, int)> flatten;
    flatten = [&](QList<FolderNode*> list, int level) {
        std::stable_sort(list.begin(), list.end(), nameLess);
        for (FolderNode *node : list) {
            QVariantMap item;
            item[QStringLiteral("name")]      = node->name;
            item[QStringLiteral("fullPath")]  = node->fullPath;
            item[QStringLiteral("level")]     = level;
            item[QStringLiteral("fileCount")] = node->fileCount;
            result.append(item);
            if (!node->children.isEmpty())
                flatten(node->children, level + 1);
        }
    };
    flatten(nodes, 0);
    qDeleteAll(nodes);

    return result;
}
//----------------------------------------------------------------------
QVariantList AppManager::getExploreEntries(const QString &folderPath, bool showFolders, bool showSubFolders)
{
    QVariantList result;
    if (exploreDevice->externalID == 0) return result;

    const QString conn = m_connectionName;
    const QList<Catalog::ExploreFileEntry> entries = Catalog::getExploreEntries(
        conn, exploreDevice->externalID, folderPath, showFolders, showSubFolders);

    for (const Catalog::ExploreFileEntry &e : entries) {
        QVariantMap item;
        item[QStringLiteral("name")]                 = e.name;
        item[QStringLiteral("size")]                 = e.size;
        item[QStringLiteral("dateUpdated")]          = e.dateUpdated;
        item[QStringLiteral("folderPath")]           = e.folderPath;
        item[QStringLiteral("fullPath")]             = e.fullPath;
        item[QStringLiteral("entryType")]            = e.entryType;
        item[QStringLiteral("fileType")]             = e.fileType;
        item[QStringLiteral("mimeType")]             = e.mimeType;
        item[QStringLiteral("videoDurationSeconds")] = e.videoDurationSeconds;
        item[QStringLiteral("audioArtist")]          = e.audioArtist;
        item[QStringLiteral("audioAlbum")]           = e.audioAlbum;
        item[QStringLiteral("audioTitle")]           = e.audioTitle;
        item[QStringLiteral("checksumSha256")]       = e.checksumSha256;
        result.append(item);
    }
    return result;
}
//----------------------------------------------------------------------
void AppManager::loadExploreEntries(const QString &folderPath, bool showFolders, bool showSubFolders)
{
    if (!m_exploreFilesModel) return;
    if (exploreDevice->externalID == 0) {
        m_exploreFilesModel->clear();
        return;
    }
    const QList<Catalog::ExploreFileEntry> entries = Catalog::getExploreEntries(
        m_connectionName, exploreDevice->externalID, folderPath, showFolders, showSubFolders);
    m_exploreFilesModel->populate(entries);
}
//----------------------------------------------------------------------
void AppManager::exploreRemoveRow(int proxyRow)
{
    if (!m_exploreSortModel || !m_exploreFilesModel) return;
    QModelIndex proxyIdx  = m_exploreSortModel->index(proxyRow, 0);
    QModelIndex sourceIdx = m_exploreSortModel->mapToSource(proxyIdx);
    if (sourceIdx.isValid())
        m_exploreFilesModel->removeEntry(sourceIdx.row());
}
//----------------------------------------------------------------------
void AppManager::sortSearch(int column, int order)
{
    if (!m_searchSortModel) return;
    m_searchSortModel->sort(column, order);
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("Search/lastSearchSortSection", column);
    settings.setValue("Search/lastSearchSortOrder",   order);
}
//----------------------------------------------------------------------
void AppManager::sortExplore(int column, int order)
{
    if (!m_exploreSortModel) return;
    m_exploreSortModel->sort(column, order);
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("Explore/lastExploreSortSection", column);
    settings.setValue("Explore/lastExploreSortOrder",   order);
}
//----------------------------------------------------------------------
int AppManager::getSearchSortColumn() const
{
    return m_searchSortModel ? m_searchSortModel->sortColumn() : -1;
}
int AppManager::getSearchSortOrder() const
{
    return m_searchSortModel ? (int)m_searchSortModel->sortOrder() : 0;
}
int AppManager::getExploreSortColumn() const
{
    return m_exploreSortModel ? m_exploreSortModel->sortColumn() : -1;
}
int AppManager::getExploreSortOrder() const
{
    return m_exploreSortModel ? (int)m_exploreSortModel->sortOrder() : 0;
}
//----------------------------------------------------------------------
QString AppManager::exploreGetChecksum(const QString &fileName, const QString &folderPath)
{
    if (exploreDevice->externalID == 0) return QString();
    return exploreDevice->catalog->getFileChecksum(fileName, folderPath);
}
//----------------------------------------------------------------------
QVariantMap AppManager::getExploreFolderStats(const QString &folderPath)
{
    QVariantMap result;
    result["fileCount"] = 0;
    result["totalSize"] = 0LL;
    if (exploreDevice->externalID == 0) return result;
    const QString conn = exploreDevice->catalog->connectionName();
    Catalog::ExploreFolderStats stats = Catalog::getExploreFolderStats(conn, exploreDevice->externalID, folderPath);
    result["fileCount"] = stats.fileCount;
    result["totalSize"] = stats.totalSize;
    return result;
}
//----------------------------------------------------------------------

// Storage helpers
//----------------------------------------------------------------------
QStringList AppManager::getStoragePictureList() const
{
    QStringList result;
    result << QString();  // empty = no picture
    QDir dir(collection->imageFolderPath);
    if (dir.exists()) {
        const QStringList filters = {"*.png","*.jpg","*.jpeg","*.bmp","*.gif","*.webp"};
        result << dir.entryList(filters, QDir::Files, QDir::Name);
    }
    return result;
}
//----------------------------------------------------------------------
QString AppManager::getStorageImageFolderPath() const
{
    return collection->imageFolderPath;
}
//----------------------------------------------------------------------
QString AppManager::getImageFolderPath() const
{
    return collection->imageFolderPath;
}
//----------------------------------------------------------------------
void AppManager::setImageFolderPath(const QString &path)
{
    if (getReadOnly()) { emit imageFolderPathChanged(); return; }   // read-only collection (D43)
    if (collection->imageFolderPath == path)
        return;
    collection->imageFolderPath = path;
    collection->saveImageFolderPath();
    emit imageFolderPathChanged();
}

// Collection import
//----------------------------------------------------------------------
QStringList AppManager::getImportSourcePaths() const
{
    return collection->getImportSourcePaths();
}
//----------------------------------------------------------------------
QVariantList AppManager::runQualityChecks() const
{
    QVariantList result;
    const QList<QualityCheckResult> checks = collection->runQualityChecks();
    for (const QualityCheckResult &check : checks) {
        QVariantList rows;
        for (const QStringList &row : check.rows)
            rows << QVariant(row);
        result << QVariantMap{ { "check", check.checkNumber },
                               { "rows",  rows },
                               { "error", check.error } };
    }
    return result;
}
//----------------------------------------------------------------------
void AppManager::openImportSource(const QString &path)
{
    if (!m_importer)
        m_importer = new CollectionImporter(collection, this);

    QSqlError err = m_importer->openSource(path);
    if (err.type() != QSqlError::NoError) {
        qWarning() << "AppManager::openImportSource error:" << err.text();
        m_importStatusText = tr("Error: could not open collection");
        emit importStatusTextChanged();
        m_importSourceDeviceModel->clear();
        emit importSourceChanged();
        return;
    }

    // Strict abort, as K2 (SpecCollection.md): the source must first be opened,
    // and so migrated, by this version.
    if (!m_importer->checkSchemaCompatibility()) {
        qWarning() << "AppManager::openImportSource: schema mismatch:" << m_importer->lastError();
        m_importStatusText = tr("Schema version mismatch. Import cancelled. (%1)").arg(m_importer->lastError());
        emit importStatusTextChanged();
        m_importer->close();
        m_importSourceDeviceModel->clear();
        emit importSourceChanged();
        return;
    }

    m_importSourceDeviceModel->loadFromConnection(m_importer->sourceConnectionName());

    m_importStatusText = QString();
    emit importStatusTextChanged();
    emit importSourceChanged();
}
//----------------------------------------------------------------------
QString AppManager::importDevice(int srcDeviceId)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    if (!m_importer || !m_importer->isSourceOpen())
        return tr("No source collection is open.");

    m_importIsRunning  = true;
    m_importReportText.clear();
    emit importReportTextChanged();
    m_importStatusText = StatusBarMessageBuilder()
                             .setOperation(tr("Collection Import"))
                             .setStatus(tr("In Progress"))
                             .build();
    emit importIsRunningChanged();
    emit importStatusTextChanged();

    QMetaObject::Connection progressConn = connect(
        m_importer, &CollectionImporter::fileImportProgress,
        this, [this](int catalogIndex, int totalCatalogs, const QString &catalogName,
                     qint64 done, qint64 total) {
            m_importStatusText = StatusBarMessageBuilder()
                                     .setOperation(tr("Collection Import"))
                                     .setStatus(tr("In Progress"))
                                     .setDeviceContext(catalogIndex, totalCatalogs, catalogName)
                                     .setProcess(tr("Files"), done, total)
                                     .build();
            emit importStatusTextChanged();
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        },
        Qt::DirectConnection);

    bool ok;
    if (srcDeviceId == 0)
        ok = (m_importer->importAllDevices() >= 0);
    else
        ok = m_importer->importDevice(srcDeviceId);

    disconnect(progressConn);
    m_importIsRunning = false;
    emit importIsRunningChanged();

    if (!ok) {
        m_importStatusText = StatusBarMessageBuilder()
                                 .setOperation(tr("Collection Import"))
                                 .setStatus(tr("Error"))
                                 .setCurrentItem(m_importer->lastError())
                                 .build();
        emit importStatusTextChanged();
        return m_importer->lastError();
    }

    // Persist to CSV files in Memory mode
    if (collection->databaseMode == "Memory") {
        collection->saveDeviceTableToFile();
        collection->saveMappingTableToFile();
        collection->saveCatalogFilterTableToFile();
        collection->saveStorageTableToFile();
        collection->saveTagTableToFile();
        collection->saveStatiticsTableToFile();
    }

    m_importStatusText = StatusBarMessageBuilder()
                             .setOperation(tr("Collection Import"))
                             .setStatus(tr("Completed"))
                             .build();
    emit importStatusTextChanged();

    refreshAllUI();
    emit importSourceChanged();
    return QString();
}
//----------------------------------------------------------------------
// Collection export (SpecCollectionExport.md)
//----------------------------------------------------------------------
bool AppManager::exportIsBlocked() const
{
    return m_exportIsRunning || m_searchIsRunning || m_catalogIsCreating || m_deviceUpdateIsRunning;
}
//----------------------------------------------------------------------
void AppManager::setExportStatus(const QString &text)
{
    m_exportStatusText = text;
    emit exportStateChanged();
    // All events, user input included, so the Cancel button stays clickable (CEX-F10)
    QCoreApplication::processEvents();
}
//----------------------------------------------------------------------
void AppManager::finishExport(const QString &status, const QString &reason)
{
    m_exportIsRunning = false;
    StatusBarMessageBuilder builder;
    builder.setOperation(tr("Export")).setStatus(status);
    if (!reason.isEmpty())
        builder.setCurrentItem(reason);
    m_exportStatusText = builder.build();
    emit exportStateChanged();
}
//----------------------------------------------------------------------
void AppManager::exportToSQLiteFile(const QString &filePath)
{
    const bool isMemory = (collection->databaseMode == "Memory");
    const bool isHosted = (collection->databaseMode == "Hosted");
    if ((!isMemory && !isHosted) || filePath.isEmpty() || exportIsBlocked())
        return;

    m_exportIsRunning       = true;
    m_exportCancelRequested = false;
    setExportStatus(StatusBarMessageBuilder()
                        .setOperation(tr("Export"))
                        .setStatus(tr("In Progress"))
                        .build());

    // An existing target goes to the trash; if that fails it is left untouched (CEX-F8)
    QFile existingFile(filePath);
    if (existingFile.exists() && !existingFile.moveToTrash()) {
        finishExport(tr("Error"));
        return;
    }

    if (isMemory) {
        // Folders first: the file-list load only records folders holding files,
        // after which loadFoldersToTable() skips the catalog (CEX-F5)
        QList<Device::DeviceTreeNode> catalogNodes;
        for (const Device::DeviceTreeNode &node : Device::loadDeviceTree(m_connectionName))
            if (node.type == QLatin1String("Catalog"))
                catalogNodes.append(node);

        for (int i = 0; i < catalogNodes.size(); ++i) {
            if (m_exportCancelRequested) {
                finishExport(tr("Cancelled"));
                return;
            }
            Device catalogDevice;
            catalogDevice.ID = catalogNodes.at(i).id;
            catalogDevice.loadDevice(m_connectionName);
            catalogDevice.catalog->loadFoldersToTable();
            setExportStatus(StatusBarMessageBuilder()
                                .setOperation(tr("Export"))
                                .setStatus(tr("In Progress"))
                                .setProcess(tr("Loaded"), i + 1, catalogNodes.size())
                                .setCurrentItem(catalogNodes.at(i).name)
                                .build());
        }

        const bool loaded = collection->loadAllCatalogFiles(
            [this](int filesLoaded, int totalFiles, const QString &deviceName) -> bool {
                setExportStatus(StatusBarMessageBuilder()
                                    .setOperation(tr("Export"))
                                    .setStatus(tr("In Progress"))
                                    .setProcess(tr("Loaded"), filesLoaded, totalFiles)
                                    .setCurrentItem(deviceName)
                                    .build());
                return !m_exportCancelRequested;
            });
        if (!loaded) {
            finishExport(tr("Cancelled"));
            return;
        }
    }

    // On cancel, the core removes its partial target file itself
    const bool ok = collection->exportToSQLiteFile(filePath,
        [this](int, int, const QString &tableName) -> bool {
            setExportStatus(StatusBarMessageBuilder()
                                .setOperation(tr("Export"))
                                .setStatus(tr("In Progress"))
                                .setCurrentItem(tableName)
                                .build());
            return !m_exportCancelRequested;
        });

    if (!ok) {
        finishExport(m_exportCancelRequested ? tr("Cancelled") : tr("Error"));
        return;
    }
    finishExport(tr("Completed"));
    emit exportCompleted(filePath);
}
//----------------------------------------------------------------------
void AppManager::exportToMemoryMode(const QString &folderPath)
{
    const bool isFile   = (collection->databaseMode == "File");
    const bool isHosted = (collection->databaseMode == "Hosted");
    if ((!isFile && !isHosted) || folderPath.isEmpty() || exportIsBlocked())
        return;

    // The folder must be empty, so a cancel only ever removes this run's files (CEX-F13, CEX-C5)
    const QDir exportDir(folderPath);
    const QDir::Filters allEntries = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System;
    if (!exportDir.exists()) {
        finishExport(tr("Error"));
        return;
    }
    if (!exportDir.isEmpty(allEntries)) {
        finishExport(tr("Error"), tr("The export folder must be empty."));
        return;
    }

    m_exportIsRunning       = true;
    m_exportCancelRequested = false;
    setExportStatus(StatusBarMessageBuilder()
                        .setOperation(tr("Export"))
                        .setStatus(tr("In Progress"))
                        .build());

    collection->exportAllToMemoryMode(folderPath);

    const bool completed = collection->exportAllCatalogFiles(folderPath,
        [this](int, int, const QString &catalogName) -> bool {
            setExportStatus(StatusBarMessageBuilder()
                                .setOperation(tr("Export"))
                                .setStatus(tr("In Progress"))
                                .setCurrentItem(catalogName)
                                .build());
            return !m_exportCancelRequested;
        });

    if (!completed) {
        if (m_exportCancelRequested) {
            // The folder was empty at start: every file in it was written by this run
            for (const QString &fileName : exportDir.entryList(QDir::Files | QDir::Hidden | QDir::System))
                QFile::remove(exportDir.filePath(fileName));
            finishExport(tr("Cancelled"));
        } else {
            finishExport(tr("Error"));
        }
        return;
    }
    finishExport(tr("Completed"));
    emit exportCompleted(folderPath);
}
//----------------------------------------------------------------------
QString AppManager::updateAllImportsFromSource(const QString &path)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    if (!m_importer)
        m_importer = new CollectionImporter(collection, this);

    m_importIsRunning  = true;
    m_importReportText.clear();
    emit importReportTextChanged();
    m_importStatusText = StatusBarMessageBuilder()
                             .setOperation(tr("Collection Update"))
                             .setStatus(tr("In Progress"))
                             .build();
    emit importIsRunningChanged();
    emit importStatusTextChanged();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    bool ok = m_importer->updateAllImportsFromSource(path);

    m_importIsRunning = false;
    emit importIsRunningChanged();

    if (!ok) {
        m_importStatusText = StatusBarMessageBuilder()
                                 .setOperation(tr("Collection Update"))
                                 .setStatus(tr("Error"))
                                 .setCurrentItem(m_importer->lastError())
                                 .build();
        emit importStatusTextChanged();
        return m_importer->lastError();
    }

    // Persist to CSV files in Memory mode
    if (collection->databaseMode == "Memory") {
        collection->saveDeviceTableToFile();
        collection->saveMappingTableToFile();
        collection->saveCatalogFilterTableToFile();
        collection->saveStorageTableToFile();
        collection->saveTagTableToFile();
        collection->saveStatiticsTableToFile();
    }

    m_importStatusText = StatusBarMessageBuilder()
                             .setOperation(tr("Collection Update"))
                             .setStatus(tr("Completed"))
                             .build();
    emit importStatusTextChanged();

    // Build the change report (file-count and size deltas) from the importer's
    // aggregate stats. Reuses existing translatable labels (Catalogs / Files / Size);
    // arrows, dots, signs and numbers are non-translatable.
    const CollectionImporter::ImportUpdateStats st = m_importer->lastUpdateStats();
    if (st.catalogsUpdated > 0) {
        QLocale loc;
        const qint64 dFiles = st.filesAfter - st.filesBefore;
        const qint64 dSize  = st.sizeAfter  - st.sizeBefore;
        const QString dFilesStr = (dFiles >= 0 ? QStringLiteral("+") : QStringLiteral("-"))
                                  + loc.toString(qAbs(dFiles));
        const QString dSizeStr  = (dSize  >= 0 ? QStringLiteral("+") : QStringLiteral("-"))
                                  + loc.formattedDataSize(qAbs(dSize));
        m_importReportText =
              tr("Catalogs") + ": " + loc.toString(st.catalogsUpdated)
            + "   ·   " + tr("Files") + ": " + loc.toString(st.filesBefore)
                + " → " + loc.toString(st.filesAfter) + " (" + dFilesStr + ")"
            + "   ·   " + tr("Size")  + ": " + loc.formattedDataSize(st.sizeBefore)
                + " → " + loc.formattedDataSize(st.sizeAfter) + " (" + dSizeStr + ")";
        emit importReportTextChanged();
    }

    refreshAllUI();
    return QString();
}
//----------------------------------------------------------------------
QString AppManager::formatDataSize(qlonglong bytes) const
{
    if (bytes <= 0) return QString();
    return QLocale().formattedDataSize(bytes);
}
//----------------------------------------------------------------------
QString AppManager::formatDataSizeDelta(qlonglong bytes) const
{
    return QLocale().formattedDataSize(bytes);
}
//----------------------------------------------------------------------
QVariantMap AppManager::refreshStorageFromDisk(int deviceId)
{
    if (getReadOnly()) return QVariantMap{ { QStringLiteral("error"), readOnlyRefusal() } };   // read-only collection (D43)
    QVariantMap r;
    const QString conn = m_connectionName;
    Device dev;
    dev.ID = deviceId;
    dev.loadDevice(conn);

    if (dev.type != "Storage") {
        r["error"] = tr("Not a Storage device.");
        return r;
    }

    Storage::UpdateResult sr = dev.storage->updateStorageInfo();

    if (sr.errorCode == Storage::ErrorNoPath
     || sr.errorCode == Storage::ErrorEmptyDirectory
     || sr.errorCode == Storage::ErrorCannotGetValues) {
        r["error"] = sr.errorMessage;
        return r;
    }

    collection->saveDeviceTableToFile();
    refreshDeviceList();

    r["error"]        = QString();
    r["totalSpace"]   = (qlonglong)dev.storage->totalSpace;
    r["freeSpace"]    = (qlonglong)dev.storage->freeSpace;
    r["storageType"]  = dev.storage->type;
    r["storageLabel"] = dev.storage->label;
    r["fileSystem"]   = dev.storage->fileSystem;
    return r;
}

//----------------------------------------------------------------------
QString AppManager::pathToFileUrl(const QString &path) const
{
    return QUrl::fromLocalFile(path).toString();
}
//----------------------------------------------------------------------
QString AppManager::pathFromFileUrl(const QString &url) const
{
    return QUrl(url).toLocalFile();
}
//----------------------------------------------------------------------
QString AppManager::normalizeSourcePath(const QString &path) const
{
    return Catalog::normalizeSourcePath(path.trimmed());
}

// Exclude directories (collection-level)
//----------------------------------------------------------------------
QStringList AppManager::getExcludeDirectories() const
{
    return collection->getExcludeDirectories();
}
//----------------------------------------------------------------------
bool AppManager::addExcludeDirectory(const QString &path)
{
    if (getReadOnly()) return false;   // read-only collection (D43)
    if (path.trimmed().isEmpty()) return false;
    bool ok = collection->addExcludeDirectory(path.trimmed());
    if (ok) emit excludeDirectoriesChanged();
    return ok;
}
//----------------------------------------------------------------------
void AppManager::removeExcludeDirectory(const QString &path)
{
    if (getReadOnly()) return;   // read-only collection (D43)
    collection->removeExcludeDirectory(path);
    emit excludeDirectoriesChanged();
}

//----------------------------------------------------------------------
// Backup
//----------------------------------------------------------------------

static MappingFilter buildMappingFilter(const QString &filterType, int deviceId, const QString &mappingType)
{
    MappingFilter filter;
    filter.mappingType = mappingType;
    if (filterType == QLatin1String("Source") && deviceId > 0)
        return MappingFilter(MappingFilter::SourceDevice, deviceId, mappingType);
    if (filterType == QLatin1String("Target") && deviceId > 0)
        return MappingFilter(MappingFilter::TargetDevice, deviceId, mappingType);
    return filter;
}

QVariantList AppManager::getBackupMappings(const QString &filterType, int deviceId, const QString &mappingType) const
{
    BackupMappingManager manager(m_connectionName);
    const MappingFilter filter = buildMappingFilter(filterType, deviceId, mappingType);
    const QList<MappingInfo> mappings = manager.getFilteredMappings(filter);

    // Refresh device_active for every referenced device so the active indicator
    // reflects whether the path currently exists on disk.
    QHash<int, bool> activeByDeviceId;
    for (const MappingInfo &m : mappings) {
        for (int pass = 0; pass < 2; ++pass) {
            int devId    = (pass == 0) ? m.sourceDeviceId : m.targetDeviceId;
            QString path = (pass == 0) ? m.sourcePath     : m.targetPath;
            if (!activeByDeviceId.contains(devId)) {
                Device d;
                d.ID   = devId;
                d.path = path;
                d.updateActiveState(m_connectionName);
                activeByDeviceId[devId] = d.active;
            }
        }
    }

    QVariantList result;
    result.reserve(mappings.size());
    for (const MappingInfo &m : mappings) {
        QVariantMap map;
        map[QStringLiteral("mappingId")]          = m.mappingId;
        map[QStringLiteral("mappingName")]         = m.mappingName;
        map[QStringLiteral("mappingType")]         = m.mappingType;
        map[QStringLiteral("sourceDeviceId")]      = m.sourceDeviceId;
        map[QStringLiteral("targetDeviceId")]      = m.targetDeviceId;
        map[QStringLiteral("sourceName")]          = m.sourceName;
        map[QStringLiteral("sourcePath")]          = m.sourcePath;
        map[QStringLiteral("sourceSize")]          = m.sourceSize;
        map[QStringLiteral("sourceSizeStr")]       = QLocale().formattedDataSize(m.sourceSize);
        map[QStringLiteral("sourceFileCount")]     = m.sourceFileCount;
        map[QStringLiteral("sourceActive")]        = activeByDeviceId.value(m.sourceDeviceId, false);
        map[QStringLiteral("sourceDateUpdated")]   = m.sourceDateUpdated;
        map[QStringLiteral("targetName")]          = m.targetName;
        map[QStringLiteral("targetPath")]          = m.targetPath;
        map[QStringLiteral("targetSize")]          = m.targetSize;
        map[QStringLiteral("targetSizeStr")]       = QLocale().formattedDataSize(m.targetSize);
        map[QStringLiteral("targetFileCount")]     = m.targetFileCount;
        map[QStringLiteral("targetActive")]        = activeByDeviceId.value(m.targetDeviceId, false);
        map[QStringLiteral("targetDateUpdated")]   = m.targetDateUpdated;
        map[QStringLiteral("sizeDiff")]            = m.sourceSize - m.targetSize;
        map[QStringLiteral("sizeDiffStr")]         = QLocale().formattedDataSize(qAbs(m.sourceSize - m.targetSize));
        map[QStringLiteral("fileCountDiff")]       = m.sourceFileCount - m.targetFileCount;
        map[QStringLiteral("strictCopy")]          = m.strictCopy;
        map[QStringLiteral("conflictMode")]        = conflictModeToString(m.conflictMode);
        map[QStringLiteral("sourceDrive")]         = m.sourceDrive;
        map[QStringLiteral("includeEmptyDirs")]    = m.includeEmptyDirs;
        map[QStringLiteral("lastBackupDate")]      = m.lastBackupDate;
        map[QStringLiteral("lastBackupSize")]      = m.lastBackupSize;
        map[QStringLiteral("lastBackupSizeStr")]   = QLocale().formattedDataSize(m.lastBackupSize);
        result.append(map);
    }
    return result;
}
//----------------------------------------------------------------------
QVariantMap AppManager::getBackupTotals(const QString &filterType, int deviceId, const QString &mappingType) const
{
    BackupMappingManager manager(m_connectionName);
    const MappingFilter filter  = buildMappingFilter(filterType, deviceId, mappingType);
    const MappingTotals  totals = manager.calculateTotals(filter);

    const qint64 deviceSize  = selectedDevice->totalFileSize;
    const qint64 mapped      = totals.totalSourceSize;
    const float  coveragePct = (deviceSize > 0)
        ? static_cast<float>(mapped) / static_cast<float>(deviceSize) * 100.0f
        : 0.0f;

    QVariantMap result;
    result[QStringLiteral("totalMappings")]         = totals.totalMappings;
    result[QStringLiteral("totalSourceSizeStr")]    = QLocale().formattedDataSize(totals.totalSourceSize);
    result[QStringLiteral("totalTargetSizeStr")]    = QLocale().formattedDataSize(totals.totalTargetSize);
    result[QStringLiteral("totalSizeDiffStr")]      = QLocale().formattedDataSize(qAbs(totals.totalSizeDifference));
    result[QStringLiteral("totalSourceFileCount")]  = totals.totalSourceFileCount;
    result[QStringLiteral("totalTargetFileCount")]  = totals.totalTargetFileCount;
    result[QStringLiteral("totalFileCountDiff")]    = totals.totalFileCountDifference;
    result[QStringLiteral("deviceName")]            = selectedDevice->name;
    result[QStringLiteral("deviceSizeStr")]         = QLocale().formattedDataSize(deviceSize);
    result[QStringLiteral("coveragePct")]           = QString::number(static_cast<double>(coveragePct), 'f', 1);
    return result;
}
//----------------------------------------------------------------------
QString AppManager::createBackupMapping(const QString &name, const QString &type,
    int sourceId, int targetId, bool strictCopy,
    const QString &conflictMode, bool sourceDrive, bool includeEmptyDirs)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    if (name.trimmed().isEmpty())
        return tr("Provide a link name.");
    if (sourceId <= 0)
        return tr("Select a source catalog first.");
    if (targetId <= 0)
        return tr("Select a target catalog first.");
    if (sourceId == targetId)
        return tr("Select a different source or target (a device shall not be mapped to itself).");

    BackupMappingManager manager(m_connectionName);
    if (!manager.createMapping(name.trimmed(), type, sourceId, targetId, strictCopy, conflictMode, sourceDrive, includeEmptyDirs))
        return tr("Failed to create link.");

    collection->saveMappingTableToFile();
    emit backupMappingsChanged();
    return QString();
}
//----------------------------------------------------------------------
QString AppManager::duplicateBackupMapping(int mappingId)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    // SpecBackup.md BKP-F22 / BKP-C13: adapter-only, no core code. The copy is
    // read with getMappingById() and written with the existing createMapping().
    BackupMappingManager manager(m_connectionName);
    const MappingInfo source = manager.getMappingById(mappingId);
    if (source.mappingId < 0)
        return tr("Failed to create link.");

    // The name is the original followed by a datetime stamp in the shape
    // BKP-F5 already uses. No literal word is added, so the generated name
    // carries no translatable text. Link names are not unique: two duplicates
    // made in the same second share a name, which is accepted, not an error.
    const QString newName = source.mappingName + QLatin1Char('_')
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));

    // Every setting is copied. Run history is not: the duplicate starts with no
    // last-run date and no last-run size (BKP-F10), exactly as a new link does.
    if (!manager.createMapping(newName, source.mappingType,
                               source.sourceDeviceId, source.targetDeviceId,
                               source.strictCopy, conflictModeToString(source.conflictMode),
                               source.sourceDrive, source.includeEmptyDirs))
        return tr("Failed to create link.");

    collection->saveMappingTableToFile();
    emit backupMappingsChanged();
    return QString();
}
//----------------------------------------------------------------------
QString AppManager::updateBackupMapping(int mappingId, const QString &name, const QString &type,
    int sourceId, int targetId, bool strictCopy,
    const QString &conflictMode, bool sourceDrive, bool includeEmptyDirs)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    if (name.trimmed().isEmpty())
        return tr("Provide a link name.");
    if (sourceId <= 0)
        return tr("Select a source catalog first.");
    if (targetId <= 0)
        return tr("Select a target catalog first.");
    if (sourceId == targetId)
        return tr("Select a different source or target (a device shall not be mapped to itself).");

    BackupMappingManager manager(m_connectionName);
    if (!manager.updateMapping(mappingId, name.trimmed(), type, sourceId, targetId, strictCopy, conflictMode, sourceDrive, includeEmptyDirs))
        return tr("Failed to update link.");

    collection->saveMappingTableToFile();
    emit backupMappingsChanged();
    return QString();
}
//----------------------------------------------------------------------
bool AppManager::deleteBackupMapping(int mappingId)
{
    if (getReadOnly()) return false;   // read-only collection (D43)
    BackupMappingManager manager(m_connectionName);
    if (!manager.deleteMapping(mappingId))
        return false;
    collection->saveMappingTableToFile();
    emit backupMappingsChanged();
    return true;
}
//----------------------------------------------------------------------
bool AppManager::invertBackupMapping(int mappingId)
{
    if (getReadOnly()) return false;   // read-only collection (D43)
    BackupMappingManager manager(m_connectionName);
    if (!manager.invertMapping(mappingId))
        return false;
    collection->saveMappingTableToFile();
    emit backupMappingsChanged();
    return true;
}
//----------------------------------------------------------------------
// compareForBackup moved to core (CatalogDifferenceEngine::compareForBackup) per
// BKP-C4/BKP-C8 so the UI layer no longer holds compare/orchestration logic.
//----------------------------------------------------------------------
// startBackupPreview: entry point from QML. Runs the pre-preview catalog update (if
// enabled) on the Backup page, then the compare — both keep the UI responsive — and
// emits backupPreviewReady so the Backup Preview page opens only when the report is
// ready (BKP-F14/BKP-F16).
void AppManager::startBackupPreview(int mappingId)
{
    if (m_backupPreviewRunning || m_catalogUpdateForBackupRunning)
        return;
    m_exportAfterPreview = false;
    beginBackupPreview(mappingId);
}
//----------------------------------------------------------------------
void AppManager::exportBackupLinkToCsv(int mappingId)
{
    if (m_backupPreviewRunning || m_catalogUpdateForBackupRunning)
        return;
    m_exportAfterPreview = true;
    beginBackupPreview(mappingId);
}
//----------------------------------------------------------------------
void AppManager::beginBackupPreview(int mappingId)
{
    // Read-only collection (D43): the preview compares the catalogs as they are
    if (updateBeforeBackup() && !getReadOnly()) {
        m_previewAfterUpdateMappingId = mappingId;
        prepareCatalogsForMapping(mappingId);   // threaded update; chains to runPreviewCompare on completion
        return;
    }
    runPreviewCompare(mappingId);
}
//----------------------------------------------------------------------
void AppManager::stopBackupPreview()
{
    if (m_backupPreviewRunning)
        m_previewStopRequested = true;
}
//----------------------------------------------------------------------
void AppManager::runPreviewCompare(int mappingId)
{
    if (m_backupPreviewModel) m_backupPreviewModel->clear();

    // Enter the running state so the Backup page shows compare progress + a Stop button.
    m_backupPreviewRunning    = true;
    m_previewStopRequested    = false;
    m_backupPreviewStatusText = StatusBarMessageBuilder()
                                    .setOperation(tr("Search"))
                                    .setStatus(tr("In Progress"))
                                    .build();
    emit backupPreviewRunningChanged();
    emit backupPreviewStatusChanged();
    QCoreApplication::processEvents();  // paint the running state before the compare starts

    QVariantMap result;
    result[QStringLiteral("hasData")] = false;
    result[QStringLiteral("error")]   = QString();
    m_lastPreviewSummary = result;

    // Clears the running state and notifies QML. success/cancelled decide whether the
    // Backup Preview page opens (BKP-F14/BKP-F16).
    auto finish = [&](bool success, bool cancelled) {
        m_backupPreviewRunning = false;
        m_backupPreviewStatusText.clear();
        emit backupPreviewRunningChanged();
        emit backupPreviewStatusChanged();
        if (m_exportAfterPreview && success) {
            // Export of this link's preview: report the file, no Preview page
            m_exportAfterPreview = false;
            const QString exported = exportLastBackupPreviewToCsv();
            if (QFileInfo(exported).isAbsolute())
                emit backupCsvExported(exported);   // shown as a clickable path (BKP-F26)
            else
                emit backupNotification(exported, true);
            return;
        }
        emit backupPreviewReady(mappingId, success, cancelled);
    };

    BackupMappingManager manager(m_connectionName);
    MappingInfo mapping = manager.getMappingById(mappingId);
    if (mapping.mappingId < 0) {
        result[QStringLiteral("error")] = tr("Link not found.");
        m_lastPreviewSummary = result;
        emit backupNotification(tr("Link not found."), true);
        finish(false, false);
        return;
    }

    Device sourceDevice;
    sourceDevice.ID = mapping.sourceDeviceId;
    sourceDevice.loadDevice(m_connectionName);
    Device targetDevice;
    targetDevice.ID = mapping.targetDeviceId;
    targetDevice.loadDevice(m_connectionName);

    if (sourceDevice.type != QLatin1String("Catalog") || targetDevice.type != QLatin1String("Catalog")) {
        result[QStringLiteral("error")] = tr("Both source and target must be Catalog devices.");
        m_lastPreviewSummary = result;
        emit backupNotification(tr("Both source and target must be Catalog devices."), true);
        finish(false, false);
        return;
    }

    sourceDevice.updateActiveState(m_connectionName);
    targetDevice.updateActiveState(m_connectionName);

    if (collection->databaseMode == QLatin1String("Memory")) {
        QMutex mutex;
        sourceDevice.catalog->loadCatalogFileListToTable(mutex, m_previewStopRequested);
        targetDevice.catalog->loadCatalogFileListToTable(mutex, m_previewStopRequested);
    }
    if (m_previewStopRequested) { finish(false, true); return; }

    CatalogDifferenceEngine cmpEngine(m_connectionName);
    const BackupCompareResult cmp = cmpEngine.compareForBackup(
        sourceDevice, targetDevice, mapping.strictCopy, mapping.sourceDrive, &m_previewStopRequested);
    if (m_previewStopRequested) { finish(false, true); return; }

    const bool                isArchive = (mapping.mappingType == QLatin1String("Archive"));

    const BackupSpaceCheck spaceCheck = evaluateBackupSpace(
        Storage::availableSpace(targetDevice.path),
        cmp.filesToCopy, cmp.fileConflicts, mapping.conflictMode);

    QString spaceStatusStr;
    switch (spaceCheck.status) {
        case BackupSpaceStatus::OK:           spaceStatusStr = QStringLiteral("OK");           break;
        case BackupSpaceStatus::Low:          spaceStatusStr = QStringLiteral("Low");          break;
        case BackupSpaceStatus::Insufficient: spaceStatusStr = QStringLiteral("Insufficient"); break;
        default:                              spaceStatusStr = QStringLiteral("Unknown");      break;
    }

    m_lastPreviewRows.clear();
    QVariantList filesToCopy;
    for (const DifferenceFileEntry &e : cmp.filesToCopy) {
        QVariantMap row;
        row[QStringLiteral("status")]      = isArchive ? tr("Move") : tr("Copy");
        row[QStringLiteral("fileName")]    = e.fileName;
        row[QStringLiteral("folderPath")]  = e.folderPath;
        row[QStringLiteral("fileSize")]    = e.fileSize;
        row[QStringLiteral("fileSizeStr")] = QLocale().formattedDataSize(e.fileSize);
        filesToCopy.append(row);
        BackupPreviewRow pr;
        pr.status    = isArchive ? tr("Move") : tr("Copy");
        pr.fileName  = e.fileName;
        pr.folderPath= e.folderPath;
        pr.fileSize  = e.fileSize;
        m_lastPreviewRows.append(pr);
    }
    QVariantList fileConflicts;
    for (const DifferenceFileEntry &e : cmp.fileConflicts) {
        QVariantMap row;
        row[QStringLiteral("status")]      = tr("Conflict");
        row[QStringLiteral("fileName")]    = e.fileName;
        row[QStringLiteral("folderPath")]  = e.folderPath;
        row[QStringLiteral("fileSize")]    = e.fileSize;
        row[QStringLiteral("fileSizeStr")] = QLocale().formattedDataSize(e.fileSize);
        fileConflicts.append(row);
        BackupPreviewRow pr;
        pr.status    = tr("Conflict");
        pr.fileName  = e.fileName;
        pr.folderPath= e.folderPath;
        pr.fileSize  = e.fileSize;
        m_lastPreviewRows.append(pr);
    }

    // Feed the resizable-column preview table (BKP-F15): copy/move rows first, then
    // conflicts — the trailing fileConflicts rows are flagged so the table can style them.
    if (m_backupPreviewModel)
        m_backupPreviewModel->populate(m_lastPreviewRows, static_cast<int>(cmp.fileConflicts.size()));

    result[QStringLiteral("hasData")]        = true;
    result[QStringLiteral("isArchive")]      = isArchive;
    result[QStringLiteral("filesToCopy")]    = filesToCopy;
    result[QStringLiteral("fileConflicts")]  = fileConflicts;
    result[QStringLiteral("skippedCount")]   = cmp.skippedCount;
    result[QStringLiteral("spaceStatus")]    = spaceStatusStr;
    result[QStringLiteral("spaceAvailable")] = spaceCheck.available;
    result[QStringLiteral("spaceRequired")]  = spaceCheck.required;
    result[QStringLiteral("sourceName")]     = sourceDevice.name;
    result[QStringLiteral("targetName")]     = targetDevice.name;
    result[QStringLiteral("sourceActive")]   = sourceDevice.active;
    result[QStringLiteral("targetActive")]   = targetDevice.active;
    result[QStringLiteral("mappingName")]    = mapping.mappingName;

    m_lastPreviewSummary = result;
    finish(true, false);
}
//----------------------------------------------------------------------
void AppManager::runBackup(int mappingId)
{
    if (getReadOnly()) { emit backupNotification(readOnlyRefusal(), true); return; }   // read-only collection (D43)
    if (m_backupJob) return;

    // Claimed here rather than in executeBackupJob() so the card is marked running for
    // the catalog-update phase too, which is when the page's own click handler used to
    // set it. executeBackupJob() releases it again on every refusal (BKP-C10).
    setRunningBackupMappingId(mappingId);

    // If "update before" is on, update both catalogs first then call executeBackupJob()
    if (updateBeforeBackup() && !m_catalogUpdateForBackupRunning) {
        m_pendingBackupAfterUpdate = mappingId;
        prepareCatalogsForMapping(mappingId);
        return;
    }

    executeBackupJob(mappingId);
}
//----------------------------------------------------------------------
void AppManager::setRunningBackupMappingId(int mappingId)
{
    if (m_runningBackupMappingId == mappingId)
        return;
    m_runningBackupMappingId = mappingId;
    emit runningBackupMappingIdChanged();
}
//----------------------------------------------------------------------
void AppManager::runListedBackups(const QVariantList &mappingIds)
{
    if (getReadOnly()) { emit backupNotification(readOnlyRefusal(), true); return; }   // read-only collection (D43)
    // One sequence at a time; a second request while one runs is ignored rather than
    // appended, so the confirmed list is always the list that runs (SpecBackup.md BKP-F18).
    if (m_backupJob || !m_pendingBackupMappings.isEmpty())
        return;

    for (const QVariant &id : mappingIds)
        m_pendingBackupMappings.append(id.toInt());

    startNextListedBackup();
}
//----------------------------------------------------------------------
void AppManager::startNextListedBackup()
{
    if (m_pendingBackupMappings.isEmpty())
        return;

    const int next = m_pendingBackupMappings.takeFirst();
    // Through runBackup(), never executeBackupJob(): each link keeps its own
    // update-before-backup phase (BKP-F7) and its own last-run record (BKP-F10).
    // Deferred by a turn of the event loop so the finished job's thread has been
    // deleteLater'd before the next one is built (BKP-C10).
    QTimer::singleShot(0, this, [this, next]() { runBackup(next); });
}
//----------------------------------------------------------------------
void AppManager::executeBackupJob(int mappingId)
{
    BackupMappingManager manager(m_connectionName);
    MappingInfo mapping = manager.getMappingById(mappingId);
    if (mapping.mappingId < 0) {
        setRunningBackupMappingId(-1);
        return;
    }

    Device sourceDevice;
    sourceDevice.ID = mapping.sourceDeviceId;
    sourceDevice.loadDevice(m_connectionName);
    Device targetDevice;
    targetDevice.ID = mapping.targetDeviceId;
    targetDevice.loadDevice(m_connectionName);

    if (sourceDevice.type != QLatin1String("Catalog") || targetDevice.type != QLatin1String("Catalog")) {
        setRunningBackupMappingId(-1);
        emit backupNotification(tr("Both source and target must be Catalog devices."), true);
        return;
    }

    sourceDevice.updateActiveState(m_connectionName);
    targetDevice.updateActiveState(m_connectionName);
    if (!sourceDevice.active) {
        setRunningBackupMappingId(-1);
        emit backupNotification(tr("Source not available: %1").arg(sourceDevice.name), true);
        return;
    }
    if (!targetDevice.active) {
        setRunningBackupMappingId(-1);
        emit backupNotification(tr("Target not available: %1").arg(targetDevice.name), true);
        return;
    }

    m_backupSourceDevice     = sourceDevice;
    m_backupTargetDevice     = targetDevice;
    setRunningBackupMappingId(mappingId);

    if (collection->databaseMode == QLatin1String("Memory")) {
        QMutex mutex;
        bool stopRequested = false;
        sourceDevice.catalog->loadCatalogFileListToTable(mutex, stopRequested);
        targetDevice.catalog->loadCatalogFileListToTable(mutex, stopRequested);
    }

    //Replicate the full directory tree first (preserves empty directories)
    if (mapping.sourceDrive) {
        //Drive mode: walk the source filesystem directly
        DirectoryReplicator replicator(m_connectionName);
        replicator.replicateFromDrive(sourceDevice.path, targetDevice.path, mapping.includeEmptyDirs);
    } else {
        //Catalog mode: replicate from the folder table (includes empty directories)
        if (collection->databaseMode == QLatin1String("Memory"))
            sourceDevice.catalog->loadFoldersToTable();
        DirectoryReplicator replicator(m_connectionName);
        replicator.replicate({sourceDevice.externalID}, sourceDevice.path, targetDevice.path, mapping.includeEmptyDirs);
    }

    CatalogDifferenceEngine cmpEngine(m_connectionName);
    const BackupCompareResult cmp = cmpEngine.compareForBackup(sourceDevice, targetDevice, mapping.strictCopy, mapping.sourceDrive);
    clearBackupReport();   // a new run replaces the last report (BKP-F29)
    if (cmp.filesToCopy.isEmpty() && cmp.fileConflicts.isEmpty()) {
        setRunningBackupMappingId(-1);
        emit backupFinished(0, 0, 0, 0, 0, 0, false);
        startNextListedBackup();
        return;
    }

    const bool isArchive = (mapping.mappingType == QLatin1String("Archive"));
    m_backupIsArchive = isArchive;

    // Space check at run time, as K2 (BKP-F8): insufficient blocks the run;
    // low space already showed in the preview, so the run proceeds
    const BackupSpaceCheck spaceCheck = evaluateBackupSpace(
        Storage::availableSpace(targetDevice.path),
        cmp.filesToCopy, cmp.fileConflicts, mapping.conflictMode);
    if (spaceCheck.status == BackupSpaceStatus::Insufficient) {
        setRunningBackupMappingId(-1);
        emit backupNotification(
            StatusBarMessageBuilder()
                .setOperation(isArchive ? tr("Archive") : tr("Backup"))
                .setStatus(tr("Cancelled"))
                .build()
            + QStringLiteral(" | \u26a0 ")
            + tr("Insufficient disk space - Required: %1, Available: %2, Missing: %3")
                  .arg(QLocale().formattedDataSize(spaceCheck.required))
                  .arg(QLocale().formattedDataSize(spaceCheck.available))
                  .arg(QLocale().formattedDataSize(spaceCheck.required - spaceCheck.available)),
            true);
        startNextListedBackup();
        return;
    }

    m_backupJob = new BackupJobStoppable();
    m_backupJob->setFiles(cmp.filesToCopy);
    m_backupJob->setConflictMode(mapping.conflictMode);
    if (mapping.conflictMode == ConflictMode::RenameOldest)
        m_backupJob->setConflictFiles(cmp.fileConflicts);
    m_backupJob->setSourcePath(sourceDevice.path);
    m_backupJob->setTargetPath(targetDevice.path);
    m_backupJob->setArchiveMode(isArchive);

    m_backupThread = new QThread();
    m_backupJob->moveToThread(m_backupThread);
    connect(m_backupThread, &QThread::started,   m_backupJob,    &BackupJobStoppable::runBackup);
    connect(m_backupJob,    &BackupJobStoppable::backupProgress, this, &AppManager::onBackupProgressInternal);
    connect(m_backupJob,    &BackupJobStoppable::backupFinished, this, &AppManager::onBackupFinishedInternal);
    connect(m_backupJob,    &BackupJobStoppable::backupFinished, m_backupThread, &QThread::quit);
    connect(m_backupThread, &QThread::finished,  m_backupJob,    &QObject::deleteLater);
    connect(m_backupThread, &QThread::finished,  m_backupThread, &QObject::deleteLater);

    m_backupTimer.start();
    m_backupThread->start();
}
//----------------------------------------------------------------------
void AppManager::stopBackup()
{
    // Stop ends the whole sequence, not just this link. The waiting links are not shown
    // anywhere, so letting them fire on after a Stop would be a surprise (BKP-F20).
    m_pendingBackupMappings.clear();
    if (m_backupJob) m_backupJob->stopBackup();
}
//----------------------------------------------------------------------
void AppManager::pauseBackup()
{
    if (m_backupJob) m_backupJob->pauseBackup();
}
//----------------------------------------------------------------------
void AppManager::resumeBackup()
{
    if (m_backupJob) m_backupJob->resumeBackup();
}
//----------------------------------------------------------------------
void AppManager::onBackupProgressInternal(int filesDone, int totalFiles,
    qint64 bytesCopied, qint64 totalBytes, const QString &currentFile)
{
    emit backupProgress(filesDone, totalFiles, bytesCopied, totalBytes, currentFile);
}
//----------------------------------------------------------------------
void AppManager::clearBackupReport()
{
    m_lastReportRows.clear();
    m_lastReportFlaggedCount = 0;
    m_lastReportSummary.clear();
    emit backupReportChanged();
}
//----------------------------------------------------------------------
QString AppManager::showLastBackupReport()
{
    if (m_backupPreviewModel)
        m_backupPreviewModel->populate(m_lastReportRows, m_lastReportFlaggedCount);
    return m_lastReportSummary;
}
//----------------------------------------------------------------------
void AppManager::onBackupFinishedInternal(const BackupReport &report)
{
    // Per-file report, in K2's order: copied, moved, archived & copied,
    // conflicts, errors (BKP-F29, K2 showBackupReport)
    m_lastReportRows.clear();
    const QString archivedCopied = tr("Archived") + " & " + tr("Copied");
    auto addRows = [this](const QList<DifferenceFileEntry> &entries, const QString &status) {
        for (const DifferenceFileEntry &e : entries)
            m_lastReportRows.append({ status, e.fileName, e.folderPath, e.fileSize });
    };
    addRows(report.copied,    tr("Copied"));
    addRows(report.moved,     tr("Moved"));
    addRows(report.renamed,   archivedCopied);
    addRows(report.conflicts, tr("Conflict"));
    for (const QString &err : report.errors)
        m_lastReportRows.append({ tr("Error"), err, QString(), 0 });
    m_lastReportFlaggedCount = report.conflictCount() + report.errorCount();
    m_lastReportSummary = StatusBarMessageBuilder()
        .setOperation(tr("Report"))
        .setStatus(report.wasCancelled ? tr("Cancelled") : tr("Completed"))
        .addResult(tr("Copied"),     report.copiedCount(), report.totalBytesCopied)
        .addResult(archivedCopied,   report.renamedCount())
        .addResult(tr("Conflicts"),  report.conflictCount())
        .addResult(tr("Errors"),     report.errorCount())
        .build();
    emit backupReportChanged();

    const int finishedMappingId = m_runningBackupMappingId;
    m_backupJob              = nullptr;
    m_backupThread           = nullptr;
    setRunningBackupMappingId(-1);
    emit backupFinished(
        report.copiedCount(),
        report.movedCount(),
        report.renamedCount(),
        report.conflictCount(),
        report.errorCount(),
        report.totalBytesCopied,
        report.wasCancelled);

    // Record the completed run on the mapping (date + bytes transferred) so the card can
    // show "Last backup". Done for any completed (non-cancelled) run, including 0 files.
    if (!report.wasCancelled && finishedMappingId >= 0) {
        BackupMappingManager manager(m_connectionName);
        manager.setLastBackup(finishedMappingId,
                              QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")),
                              report.totalBytesCopied);
        collection->saveMappingTableToFile();
        emit backupMappingsChanged();
    }

    // Re-scan the affected catalogs after a successful backup so the card's totals/diff
    // refresh (same rule as K2: the "Update catalogs" checkbox controls before AND after).
    // Backup only changes the target; Archive moves files out of the source, so re-scan
    // both — source first so the visible "To move" count updates, then target.
    if (!report.wasCancelled && updateBeforeBackup() && !m_catalogUpdateForBackupRunning) {
        m_postBackupUpdateQueue.clear();
        if (m_backupIsArchive)
            m_postBackupUpdateQueue.append(m_backupSourceDevice.ID);
        m_postBackupUpdateQueue.append(m_backupTargetDevice.ID);
        // Defer past DeviceUpdateManager's ~10 ms post-operation cleanup window (same
        // singleShot guard used in onCatalogUpdateForBackupStep) so the update runs
        // against a fully idle manager rather than erroring out.
        QTimer::singleShot(60, this, [this]() { runNextPostBackupUpdate(); });
        return;   // the next link starts once those updates have drained
    }

    if (report.wasCancelled)
        m_pendingBackupMappings.clear();
    startNextListedBackup();
}
//----------------------------------------------------------------------
void AppManager::runNextPostBackupUpdate()
{
    if (m_postBackupUpdateQueue.isEmpty()) {
        emit backupMappingsChanged();
        // Only now is the device-update manager idle again, so this is where the next
        // link of a "Run listed links" sequence can safely start (BKP-C11).
        startNextListedBackup();
        return;
    }
    const int id = m_postBackupUpdateQueue.takeFirst();
    setupDeviceUpdateManager();   // clean, idle manager
    disconnect(m_deviceUpdateManager, nullptr, this, nullptr);
    // Refresh after each catalog (on any outcome) and chain to the next one, deferring
    // past the cleanup window so the manager is idle before the next update.
    auto onDone = [this]() {
        setupDeviceUpdateManager();
        emit backupMappingsChanged();
        QTimer::singleShot(60, this, [this]() { runNextPostBackupUpdate(); });
    };
    connect(m_deviceUpdateManager, &DeviceUpdateManager::operationCompleted, this, [onDone]() { onDone(); });
    connect(m_deviceUpdateManager, &DeviceUpdateManager::operationError,     this, [onDone](const QString &) { onDone(); });
    connect(m_deviceUpdateManager, &DeviceUpdateManager::operationCancelled, this, [onDone]() { onDone(); });

    Device *dev = new Device();
    dev->ID = id;
    dev->loadDevice(m_connectionName);
    historyBeforeUpdate(QStringLiteral("update"), { id }, false);   // restore point (fork)
    m_deviceUpdateManager->updateDeviceHierarchy(dev, collection->databaseMode, collection->folder, "update");
}
//----------------------------------------------------------------------
QVariantMap AppManager::replicateDirectories(int mappingId)
{
    QVariantMap result;
    BackupMappingManager manager(m_connectionName);
    MappingInfo mapping = manager.getMappingById(mappingId);
    if (mapping.mappingId < 0) {
        result[QStringLiteral("error")] = tr("Link not found.");
        return result;
    }

    Device sourceDevice;
    sourceDevice.ID = mapping.sourceDeviceId;
    sourceDevice.loadDevice(m_connectionName);
    Device targetDevice;
    targetDevice.ID = mapping.targetDeviceId;
    targetDevice.loadDevice(m_connectionName);

    if (sourceDevice.type != QLatin1String("Catalog") || targetDevice.type != QLatin1String("Catalog")) {
        result[QStringLiteral("error")] = tr("Both source and target must be Catalog devices.");
        return result;
    }
    sourceDevice.updateActiveState(m_connectionName);
    targetDevice.updateActiveState(m_connectionName);
    if (!sourceDevice.active) {
        result[QStringLiteral("error")] = tr("Source not available: %1").arg(sourceDevice.name);
        return result;
    }
    if (!targetDevice.active) {
        result[QStringLiteral("error")] = tr("Target not available: %1").arg(targetDevice.name);
        return result;
    }

    DirectoryReplicator replicator(m_connectionName);
    ReplicationResult repResult;
    if (mapping.sourceDrive) {
        repResult = replicator.replicateFromDrive(sourceDevice.path, targetDevice.path, mapping.includeEmptyDirs);
    } else {
        if (collection->databaseMode == QLatin1String("Memory"))
            sourceDevice.catalog->loadFoldersToTable();
        repResult = replicator.replicate({sourceDevice.externalID}, sourceDevice.path, targetDevice.path, mapping.includeEmptyDirs);
    }

    result[QStringLiteral("error")]   = QString();
    result[QStringLiteral("created")] = repResult.createdCount();
    result[QStringLiteral("skipped")] = repResult.skippedCount();
    result[QStringLiteral("errors")]  = repResult.errorCount();
    return result;
}
//----------------------------------------------------------------------
QString AppManager::exportLastBackupPreviewToCsv()
{
    if (m_lastPreviewRows.isEmpty())
        return tr("No preview data to export.");
    // Browse folder: Hosted mode has no collection folder
    const QString filePath = BackupMappingManager::exportPreviewToCsv(m_lastPreviewRows, getCollectionBrowsePath());
    if (filePath.isEmpty())
        return tr("Export failed.");
    return filePath;
}
//----------------------------------------------------------------------
QString AppManager::generateLuckyBackupProfile(const QVariantList &mappingIds)
{
    QList<int> ids;
    for (const QVariant &v : mappingIds)
        ids.append(v.toInt());

    BackupProfileGenerator generator(m_connectionName);
    const BackupProfileResult result = generator.generateProfile(ids);
    if (!result.success) {
        const QString msg = tr("Failed to generate Backup profile") + ": " + result.errorMessage;
        emit backupNotification(msg, true);
        return msg;
    }
    const QString msg = tr("Backup profile created.") + " " + result.profilePath;
    emit backupNotification(msg, false);
    return msg;
}
//----------------------------------------------------------------------
QString AppManager::getBackupSetting(const QString &key) const
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    return settings.value("BackUp/" + key).toString();
}
//----------------------------------------------------------------------
void AppManager::setBackupSetting(const QString &key, const QVariant &value)
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue("BackUp/" + key, value);
    settings.sync();
}
//----------------------------------------------------------------------
bool AppManager::updateBeforeBackup() const
{
    return getBackupSetting(QStringLiteral("UpdateBeforeBackup")) == QLatin1String("true");
}
//----------------------------------------------------------------------
void AppManager::setUpdateBeforeBackup(bool v)
{
    setBackupSetting(QStringLiteral("UpdateBeforeBackup"), v ? QStringLiteral("true") : QStringLiteral("false"));
    emit updateBeforeBackupChanged();
}
//----------------------------------------------------------------------
// Async: updates source catalog then target catalog for the given mapping.
// Emits catalogsForMappingPrepared() when both are done (or on error).
// runBackup() uses this internally; QML preview page calls it directly.
void AppManager::prepareCatalogsForMapping(int mappingId)
{
    if (getReadOnly()) { emit catalogsForMappingPrepared(mappingId, false, readOnlyRefusal()); return; }   // read-only collection (D43)
    if (m_catalogUpdateForBackupRunning) {
        emit catalogsForMappingPrepared(mappingId, false,
            tr("A catalog update is already in progress. Please wait and try again."));
        return;
    }

    BackupMappingManager manager(m_connectionName);
    MappingInfo mapping = manager.getMappingById(mappingId);
    if (mapping.mappingId < 0) {
        emit catalogsForMappingPrepared(mappingId, false, tr("Mapping not found."));
        return;
    }

    Device sourceDevice;
    sourceDevice.ID = mapping.sourceDeviceId;
    sourceDevice.loadDevice(m_connectionName);
    Device targetDevice;
    targetDevice.ID = mapping.targetDeviceId;
    targetDevice.loadDevice(m_connectionName);

    m_pendingCatalogUpdateMappingId    = mappingId;
    m_pendingCatalogUpdateSourceDevice = sourceDevice;
    m_pendingCatalogUpdateTargetDevice = targetDevice;
    m_backupCatalogUpdatePhase         = BackupCatalogUpdatePhase::UpdatingSource;
    m_catalogUpdateForBackupRunning    = true;
    emit catalogUpdateForBackupRunningChanged();

    setupCatalogUpdateForBackupConnections();

    Device *dev = new Device();
    *dev = m_pendingCatalogUpdateSourceDevice;
    historyBeforeUpdate(QStringLiteral("update"), { dev->ID }, false);   // restore point (fork)
    m_deviceUpdateManager->updateDeviceHierarchy(dev, collection->databaseMode, collection->folder, "update");
}
//----------------------------------------------------------------------
void AppManager::setupCatalogUpdateForBackupConnections()
{
    if (!m_deviceUpdateManager)
        setupDeviceUpdateManager();
    disconnect(m_deviceUpdateManager, nullptr, this, nullptr);

    // Route catalog-update progress to the backup-specific status text so the Backup
    // page can show the same StatusBarMessageBuilder messages as other screens. Wire
    // this explicitly (not via setupDeviceUpdateManager) so it is deterministic even
    // if a device/create update ran earlier and pointed the progress manager elsewhere.
    if (!m_catalogProgressManager)
        m_catalogProgressManager = new CatalogProgressManager(this);
    disconnect(m_catalogProgressManager, nullptr, this, nullptr);
    connect(m_catalogProgressManager, &CatalogProgressManager::statusMessageChanged,
            this, [this](const QString &message, int /*timeout*/) {
                m_catalogUpdateForBackupStatusText = message;
                emit catalogUpdateForBackupStatusChanged();
            });
    m_deviceUpdateManager->setCatalogProgressManager(m_catalogProgressManager);

    connect(m_deviceUpdateManager, &DeviceUpdateManager::operationCompleted,
            this, &AppManager::onCatalogUpdateForBackupStep);

    auto onFail = [this](const QString &error) {
        m_catalogUpdateForBackupRunning = false;
        m_catalogUpdateForBackupStatusText.clear();
        m_backupCatalogUpdatePhase      = BackupCatalogUpdatePhase::None;
        m_pendingBackupAfterUpdate      = -1;
        int id                          = m_pendingCatalogUpdateMappingId;
        m_pendingCatalogUpdateMappingId = -1;
        setupDeviceUpdateManager();
        emit catalogUpdateForBackupRunningChanged();
        emit catalogUpdateForBackupStatusChanged();
        emit catalogsForMappingPrepared(id, false, error);

        // If a preview was waiting on this update, report it as a failed preview so the
        // Backup page clears and the Preview page does not open.
        if (m_previewAfterUpdateMappingId == id) {
            m_previewAfterUpdateMappingId = -1;
            if (!error.isEmpty())
                emit backupNotification(error, true);
            emit backupPreviewReady(id, false, false);
        }
    };
    connect(m_deviceUpdateManager, &DeviceUpdateManager::operationError,     this, onFail);
    connect(m_deviceUpdateManager, &DeviceUpdateManager::operationCancelled, this,
            [onFail]() { onFail(QString()); });
}
//----------------------------------------------------------------------
void AppManager::onCatalogUpdateForBackupStep()
{
    if (m_backupCatalogUpdatePhase == BackupCatalogUpdatePhase::UpdatingSource) {
        // Source done — update target next.
        // DeviceUpdateManager defers cleanup ~10 ms after operationCompleted; delay
        // past that window so the manager is fully reset before the next operation.
        m_backupCatalogUpdatePhase = BackupCatalogUpdatePhase::UpdatingTarget;
        QTimer::singleShot(50, this, [this]() {
            Device *dev = new Device();
            *dev = m_pendingCatalogUpdateTargetDevice;
            historyBeforeUpdate(QStringLiteral("update"), { dev->ID }, false);   // restore point (fork)
            m_deviceUpdateManager->updateDeviceHierarchy(dev, collection->databaseMode, collection->folder, "update");
        });
        return;
    }

    // Both catalogs updated — reload device data then continue
    m_backupCatalogUpdatePhase = BackupCatalogUpdatePhase::None;
    m_pendingCatalogUpdateSourceDevice.loadDevice(m_connectionName);
    m_pendingCatalogUpdateTargetDevice.loadDevice(m_connectionName);

    int id                          = m_pendingCatalogUpdateMappingId;
    m_pendingCatalogUpdateMappingId = -1;
    int pendingBackup               = m_pendingBackupAfterUpdate;
    m_pendingBackupAfterUpdate      = -1;
    m_catalogUpdateForBackupRunning = false;
    m_catalogUpdateForBackupStatusText.clear();   // backup/archive run shows its own progress next

    setupDeviceUpdateManager();  // restore normal catalog/device connections
    emit catalogUpdateForBackupRunningChanged();
    emit catalogUpdateForBackupStatusChanged();
    emit catalogsForMappingPrepared(id, true, QString());

    if (pendingBackup >= 0)
        executeBackupJob(pendingBackup);
    else if (m_previewAfterUpdateMappingId == id) {
        // Preview flow: catalogs are updated — now run the (cancellable) compare, then
        // backupPreviewReady opens the Preview page.
        m_previewAfterUpdateMappingId = -1;
        runPreviewCompare(id);
    }
}

//----------------------------------------------------------------------
// Transcripts, file information, people and faces (fork, owner's request
// 2026-10-30, SpecMediaIntelligence.md). Data in mdi_data.db (File and Memory
// modes), read and written on the main-thread connection; the slow parts
// (cleaning stored transcripts, the "info" vectors for search by meaning)
// run on worker threads with connections of their own.
//----------------------------------------------------------------------
namespace {

QList<MdiPeople::FileRef> peopleFileRefs(const QVariantList &files)
{
    QList<MdiPeople::FileRef> refs;
    for (const QVariant &v : files) {
        const QVariantMap m = v.toMap();
        const MdiPeople::FileRef ref{m.value(QStringLiteral("catalogId")).toInt(),
                                     m.value(QStringLiteral("fullPath")).toString()};
        if (ref.catalogId > 0 && !ref.fullPath.isEmpty() && !refs.contains(ref))
            refs.append(ref);
    }
    return refs;
}

QList<int> peopleIds(const QVariantList &list)
{
    QList<int> ids;
    for (const QVariant &v : list)
        if (v.toInt() > 0 && !ids.contains(v.toInt()))
            ids.append(v.toInt());
    return ids;
}

QList<QPair<int, QString>> peoplePairs(const QList<MdiPeople::FileRef> &refs)
{
    QList<QPair<int, QString>> pairs;
    for (const MdiPeople::FileRef &r : refs)
        pairs.append({r.catalogId, r.fullPath});
    return pairs;
}

// A derived file's URL while its drive is there, else empty (the UI shows an icon)
QString existingFileUrl(const QString &path)
{
    return !path.isEmpty() && QFileInfo::exists(path) ? QUrl::fromLocalFile(path).toString() : QString();
}

QHash<int, QString> personNames(const QString &connectionName)
{
    QHash<int, QString> names;
    for (const MdiPeople::Person &p : MdiPeople::persons(connectionName))
        names.insert(p.id, p.displayName());
    return names;
}

QVariantMap faceRow(const MdiPeople::Face &f, const QHash<int, QString> &names)
{
    return QVariantMap{
        { QStringLiteral("faceId"),            f.id },
        { QStringLiteral("catalogId"),         f.catalogId },
        { QStringLiteral("fullPath"),          f.fullPath },
        { QStringLiteral("fileName"),          QFileInfo(f.fullPath).fileName() },
        { QStringLiteral("x"),                 f.x },
        { QStringLiteral("y"),                 f.y },
        { QStringLiteral("w"),                 f.w },
        { QStringLiteral("h"),                 f.h },
        { QStringLiteral("detectionScore"),    f.score },
        { QStringLiteral("status"),            f.status },
        { QStringLiteral("personId"),          f.personId },
        { QStringLiteral("personName"),        names.value(f.personId) },
        { QStringLiteral("suggestedPersonId"), f.suggestedPersonId },
        { QStringLiteral("suggestedName"),     names.value(f.suggestedPersonId) },
        { QStringLiteral("score"),             f.suggestionScore },
        { QStringLiteral("cropUrl"),           existingFileUrl(f.cropPath()) },
    };
}

} // namespace
//----------------------------------------------------------------------
QString AppManager::peopleStoreRefusal()
{
    if (collection->databaseMode == QLatin1String("Hosted"))
        return tr("File information and people are not available for hosted collections.");
    if (!openMdiMainConnection())
        return tr("The change could not be saved.");
    return QString();
}
//----------------------------------------------------------------------
// Read-only collection and its writers (fork, D43) ---------------------
//----------------------------------------------------------------------
QString AppManager::readOnlyRefusal() const
{
    if (!getReadOnly())
        return QString();
    return tr("The collection is open read-only: this change is not possible.");
}
//----------------------------------------------------------------------
QString AppManager::getReadOnlyReason() const
{
    QStringList parts = m_readOnlyCauses.values();
    if (getReadOnly() && m_migrationSkipped)
        parts << tr("Its database also needs an update for this version of Teca, made only where the collection can be changed.");
    return parts.join(QLatin1Char(' '));
}
//----------------------------------------------------------------------
bool AppManager::getReadOnlyNotAuthorised() const
{
    return m_readOnlyCauses.contains(int(ReadOnlyCause::NotAuthorised));
}
//----------------------------------------------------------------------
void AppManager::setReadOnlyCause(ReadOnlyCause cause, const QString &text)
{
    if (text.isEmpty())
        m_readOnlyCauses.remove(int(cause));
    else
        m_readOnlyCauses.insert(int(cause), text);
}
//----------------------------------------------------------------------
QString AppManager::lockHolderText(const QString &host, qint64 pid, const QString &app) const
{
    return tr("It is open on %1, in %2 (process %3).")
        .arg(host.isEmpty() ? QStringLiteral("?") : host,
             app.isEmpty() ? QStringLiteral("Teca") : app,
             QString::number(pid));
}
//----------------------------------------------------------------------
void AppManager::prepareCollectionAccess()
{
    if (!m_collectionLock) {
        m_collectionLock = new CollectionLock();
        // Released when the application ends; a crash leaves it stale (CollectionLock)
        connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
            if (m_collectionLock)
                m_collectionLock->release();
        });
        m_lockRefreshTimer = new QTimer(this);
        m_lockRefreshTimer->setInterval(CollectionLock::kRefreshSeconds * 1000);
        connect(m_lockRefreshTimer, &QTimer::timeout, this, &AppManager::refreshCollectionLock);
        m_lockRefreshTimer->start();
    }
    m_collectionLock->release();   // the collection being left
    m_readOnlyCauses.clear();
    m_migrationSkipped = false;

    // The collection about to be opened, read as Database::initialize() reads it
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    QString mode = settings.value("Settings/databaseMode").toString();
    if (mode.isEmpty())
        mode = QStringLiteral("File");
    QString folder;
    bool mayWrite = true;
    if (mode == QLatin1String("File")) {
        const QString path = settings.value("Settings/DatabaseFilePath").toString();
        if (!path.isEmpty())
            folder = QFileInfo(path).absolutePath();
        mayWrite = CollectionWriters::mayThisComputerWriteFile(path);
    } else if (mode == QLatin1String("Memory")) {
        folder = collection->folder.isEmpty() ? settings.value("LastCollectionFolder").toString()
                                              : collection->folder;
        mayWrite = CollectionWriters::mayThisComputerWriteFolder(folder);
    }

    // A computer that may not change it does not take the lock: it would keep
    // an authorised one out
    if (!mayWrite) {
        setReadOnlyCause(ReadOnlyCause::NotAuthorised, tr("This Mac is not among the Macs allowed to change it."));
    } else if (!folder.isEmpty()) {
        switch (m_collectionLock->acquire(folder)) {
        case CollectionLock::Result::Acquired:
            break;
        case CollectionLock::Result::HeldElsewhere:
            setReadOnlyCause(ReadOnlyCause::LockedElsewhere,
                             lockHolderText(m_collectionLock->holderHost(), m_collectionLock->holderPid(),
                                            m_collectionLock->holderApp()));
            break;
        case CollectionLock::Result::NotPossible:
            qWarning() << "Collection lock not taken in" << folder;
            break;
        }
    }
    // Read by Database::initialize() and the Memory-mode load
    collection->readOnly     = getReadOnly();
    collection->openReadOnly = !mayWrite && mode == QLatin1String("File");
}
//----------------------------------------------------------------------
void AppManager::confirmCollectionAccess()
{
    // Hosted collections are known only now; File and Memory are confirmed
    if (!getReadOnlyNotAuthorised() && !CollectionWriters(collection).mayThisComputerWrite())
        setReadOnlyCause(ReadOnlyCause::NotAuthorised, tr("This Mac is not among the Macs allowed to change it."));
    applyReadOnlyState();
}
//----------------------------------------------------------------------
void AppManager::applyReadOnlyState()
{
    const bool readOnly = getReadOnly();
    collection->readOnly = readOnly;
    // Probed, not stored, except in the in-memory copy of a Memory collection
    Device::setActiveStateStored(!readOnly || collection->databaseMode == QLatin1String("Memory"));
    if (readOnly) {
        qInfo() << "Collection open read-only:" << m_readOnlyCauses.values().join(QLatin1Char(' '));
        if (m_collectionLock && m_collectionLock->isHeld())
            m_collectionLock->release();
        // Work that writes ends; the list of jobs stays as it was saved
        if (m_mdiJob) {
            m_mdiKeepPending = true;
            m_mdiJob->requestStop();
        }
        m_mdiHeld.clear();
        stopFtsIndexing();
        stopKnowledgeIndexing();
    }
    emit readOnlyChanged();
    emit writersChanged();
}
//----------------------------------------------------------------------
void AppManager::openCollectionDatabase(const QString &conn)
{
    if (getReadOnly()) {
        // Nothing written on open (D43): no migration; one that is needed is
        // said in the reason instead
        m_migrationSkipped = DatabaseManager::migrationNeeded(conn, collection);
        const QSqlError err = DatabaseManager::prepareReadOnly(conn, collection);
        if (err.type() != QSqlError::NoError)
            qWarning() << "Read-only open:" << err.text();
        if (m_migrationSkipped) {
            qInfo() << "Read-only collection: schema update not made";
            emit readOnlyChanged();
        }
        return;
    }
    const QSqlError migErr = DatabaseManager::runMigrations(conn, collection);
    if (migErr.type() != QSqlError::NoError)
        qWarning() << "Migrations failed (non-fatal):" << migErr.text();
}
//----------------------------------------------------------------------
void AppManager::refreshCollectionLock()
{
    if (!m_collectionLock || !m_collectionLock->isHeld() || m_collectionLock->refresh())
        return;
    // No longer this process's: removed (taken again when free), or taken
    // over while this computer slept. Changes stop until the collection is
    // opened again: what is in memory may be older than what the other wrote.
    const QString host = m_collectionLock->holderHost();
    const qint64  pid  = m_collectionLock->holderPid();
    const QString app  = m_collectionLock->holderApp();
    const CollectionLock::Result again = m_collectionLock->acquire(m_collectionLock->folder());
    if (again == CollectionLock::Result::Acquired && pid == 0)
        return;
    if (again == CollectionLock::Result::Acquired)
        m_collectionLock->release();
    if (again == CollectionLock::Result::HeldElsewhere)
        setReadOnlyCause(ReadOnlyCause::LockedElsewhere,
                         lockHolderText(m_collectionLock->holderHost(), m_collectionLock->holderPid(),
                                        m_collectionLock->holderApp()));
    else
        setReadOnlyCause(ReadOnlyCause::LockedElsewhere, lockHolderText(host, pid, app));
    qWarning() << "Collection lock lost: the collection is now read-only";
    applyReadOnlyState();
}
//----------------------------------------------------------------------
QString AppManager::writersChangeText(int change) const
{
    switch (CollectionWriters::Change(change)) {
    case CollectionWriters::Change::Ok:            return QString();
    case CollectionWriters::Change::InvalidCode:   return tr("A code of 12 letters and digits is needed, as shown on the other Mac.");
    case CollectionWriters::Change::AlreadyListed: return tr("This code is already in the list.");
    case CollectionWriters::Change::NotListed:     return tr("This code is not in the list.");
    case CollectionWriters::Change::LastWriter:    return tr("The last Mac in the list cannot be removed while protection is on.");
    case CollectionWriters::Change::Failed:        break;
    }
    return tr("The change could not be saved.");
}
//----------------------------------------------------------------------
bool AppManager::getWritersProtection() const
{
    return CollectionWriters(collection).protectionOn();
}
//----------------------------------------------------------------------
QVariantList AppManager::getAuthorisedWriters() const
{
    QVariantList list;
    const QList<CollectionWriters::Writer> writers = CollectionWriters(collection).writers();
    for (const CollectionWriters::Writer &w : writers)
        list << QVariantMap{ { QStringLiteral("code"), w.code },
                             { QStringLiteral("name"), w.name },
                             { QStringLiteral("thisComputer"), w.thisComputer } };
    return list;
}
//----------------------------------------------------------------------
QString AppManager::getThisComputerCode() const
{
    return CollectionWriters::thisComputerCode();
}
//----------------------------------------------------------------------
QString AppManager::getThisComputerName() const
{
    return CollectionWriters::thisComputerName();
}
//----------------------------------------------------------------------
QString AppManager::setWritersProtection(bool on)
{
    if (getReadOnly())
        return readOnlyRefusal();
    const CollectionWriters::Change change = CollectionWriters(collection).setProtection(on);
    emit writersChanged();
    return writersChangeText(int(change));
}
//----------------------------------------------------------------------
QString AppManager::addAuthorisedWriter(const QString &code, const QString &name)
{
    if (getReadOnly())
        return readOnlyRefusal();
    const CollectionWriters::Change change = CollectionWriters(collection).addWriter(code, name);
    emit writersChanged();
    return writersChangeText(int(change));
}
//----------------------------------------------------------------------
QString AppManager::removeAuthorisedWriter(const QString &code)
{
    if (getReadOnly())
        return readOnlyRefusal();
    CollectionWriters writers(collection);
    const CollectionWriters::Change change = writers.removeWriter(code);
    // This computer removed from the list: read-only from now on
    if (change == CollectionWriters::Change::Ok && !writers.mayThisComputerWrite()) {
        setReadOnlyCause(ReadOnlyCause::NotAuthorised, tr("This Mac is not among the Macs allowed to change it."));
        applyReadOnlyState();
    }
    emit writersChanged();
    return writersChangeText(int(change));
}
//----------------------------------------------------------------------
double AppManager::faceMatchThreshold()
{
    // In core, shared with the command line (D43)
    return MdiEnrichmentJob::faceMatchThreshold(collection->settingsFilePath);
}
//----------------------------------------------------------------------
void AppManager::reindexFileInfo(const QList<QPair<int, QString>> &files)
{
    if (getReadOnly()) return;   // read-only collection (D43)
    // Files of a collection left meanwhile are not written to the new one's store
    if (m_infoIndexPath != mdiDataPath()) {
        m_infoIndexPending.clear();
        m_infoIndexPath = mdiDataPath();
    }
    for (const auto &f : files)
        if (!m_infoIndexPending.contains(f))
            m_infoIndexPending.append(f);
    runInfoIndexing();
}
//----------------------------------------------------------------------
void AppManager::runInfoIndexing()
{
    if (getReadOnly()) return;   // read-only collection (D43)
    // The changed files' text in the instant index, and their "info" vector
    // for search by meaning (local embedding model only, as for the runner;
    // nothing when none is configured or reachable). Never on the UI thread.
    if (m_infoIndexThread || m_infoIndexPending.isEmpty())
        return;
    if (collection->databaseMode == QLatin1String("Hosted") || m_infoIndexPath != mdiDataPath()) {
        m_infoIndexPending.clear();
        return;
    }
    const QList<QPair<int, QString>> batch = m_infoIndexPending;
    m_infoIndexPending.clear();
    const QString mdiPath = mdiDataPath();
    const QVariantMap tools = getEnrichmentTools();
    const QString url   = tools.value(QStringLiteral("ollamaUrl")).toString();
    const QString model = tools.value(QStringLiteral("embedModel")).toString();
    m_infoIndexStop = false;
    m_infoIndexThread = QThread::create([this, batch, mdiPath, url, model]() {
        const QString connection = QStringLiteral("mdi_info_%1").arg(reinterpret_cast<quintptr>(QThread::currentThread()));
        if (MdiData::open(connection, mdiPath)) {
            MdiFts::ensureTables(connection);
            QList<QPair<int, QString>> toEmbed;
            QStringList texts;
            for (const auto &f : batch) {
                if (m_infoIndexStop.load())
                    break;
                MdiFts::updateDerivedText(connection, f.first, f.second);
                const QString text = MdiPeople::infoText(connection, f.first, f.second);
                if (text.isEmpty()) {
                    MdiData::removeEmbeddings(connection, f.first, f.second, {QStringLiteral("info")});
                } else {
                    toEmbed.append(f);
                    texts.append(text);
                }
            }
            constexpr int kBatch = 32;
            for (int from = 0; !model.isEmpty() && from < texts.size() && !m_infoIndexStop.load(); from += kBatch) {
                QString error;
                const QList<QVector<float>> vectors = MdiEnrichmentJob::embedTexts(url, model, texts.mid(from, kBatch),
                                                                                   &error, 60 * 1000);
                if (vectors.isEmpty()) {
                    qWarning() << "File information not indexed for search by meaning:" << error;
                    break;   // indexed again by the next enrichment of these files
                }
                for (int i = 0; i < vectors.size(); ++i) {
                    MdiData::EmbeddingRow row;
                    row.kind   = QStringLiteral("info");
                    row.text   = texts.at(from + i);
                    row.vector = vectors.at(i);
                    const auto &f = toEmbed.at(from + i);
                    MdiData::replaceEmbeddingKind(connection, f.first, f.second, model, row.kind, {row});
                }
            }
        }
        MdiData::close(connection);
    });
    connect(qApp, &QCoreApplication::aboutToQuit, m_infoIndexThread, [this]() { stopPeopleWorkers(); });
    connect(m_infoIndexThread, &QThread::finished, this, [this]() {
        m_infoIndexThread->deleteLater();
        m_infoIndexThread = nullptr;
        if (!m_infoIndexStop.load())
            runInfoIndexing();   // files changed meanwhile
    });
    m_infoIndexThread->start(QThread::LowPriority);
}
//----------------------------------------------------------------------
void AppManager::stopPeopleWorkers()
{
    m_infoIndexStop = true;
    m_transcriptCleanStop = true;
    m_webStop = true;
    if (m_webThread)
        m_webThread->wait(10000);
    if (m_infoIndexThread)
        m_infoIndexThread->wait(10000);
    if (m_transcriptCleanThread)
        m_transcriptCleanThread->wait(10000);
    if (m_faceThread)
        m_faceThread->wait(10000);
}
//----------------------------------------------------------------------
QVariantMap AppManager::cleanTranscripts()
{
    if (getReadOnly()) return QVariantMap{ { QStringLiteral("error"), readOnlyRefusal() }, { QStringLiteral("started"), false } };   // read-only collection (D43)
    QVariantMap result{ { QStringLiteral("started"), false } };
    // The runner writes the same data: the cleaning waits for it to end
    if (m_mdiJob) {
        result[QStringLiteral("error")] = tr("The enrichment is running: the transcripts can be cleaned once it ends or is stopped.");
        return result;
    }
    if (m_transcriptCleanThread) {
        result[QStringLiteral("error")] = tr("The transcripts are being cleaned.");
        return result;
    }
    if (collection->databaseMode == QLatin1String("Hosted") || !QFileInfo::exists(mdiDataPath())
        || !openMdiMainConnection()) {
        result[QStringLiteral("error")] = tr("No transcript to clean in this collection.");
        return result;
    }

    QList<int> catalogIds;
    for (const Device::DeviceTreeNode &n : Device::loadDeviceTree(m_connectionName))
        if (n.type == QLatin1String("Catalog") && n.externalId > 0 && !catalogIds.contains(n.externalId))
            catalogIds.append(n.externalId);
    // The glossary hint as whisper may have been given it, whether or not it
    // is enabled now: a passage reading it back is not speech
    QJsonObject context = MdiContext::load(kMdiMainConnection, MdiContext::kCollectionScope);
    context.insert(QStringLiteral("transcriptHint"), true);
    const QString hint = MdiContext::whisperHint(context);

    stopFtsIndexing();   // it writes the instant index this marks stale
    const QString mdiPath = mdiDataPath();
    m_transcriptCleanStop = false;
    m_transcriptCleanThread = QThread::create([this, catalogIds, hint, mdiPath]() {
        const QString connection = QStringLiteral("mdi_clean_%1").arg(reinterpret_cast<quintptr>(QThread::currentThread()));
        MdiTranscriptFilter::CleanCounts total;
        int catalogs = 0;
        if (MdiData::open(connection, mdiPath)) {
            for (int id : catalogIds) {
                if (m_transcriptCleanStop.load())
                    break;
                const MdiTranscriptFilter::CleanCounts c = MdiTranscriptFilter::cleanStored(connection, id, hint,
                                                                                            &m_transcriptCleanStop);
                total.checked += c.checked;
                total.cleaned += c.cleaned;
                total.removed += c.removed;
                total.failed  += c.failed;
                ++catalogs;
            }
        }
        MdiData::close(connection);
        const QVariantMap counts{ { QStringLiteral("checked"),  total.checked },
                                  { QStringLiteral("cleaned"),  total.cleaned },
                                  { QStringLiteral("removed"),  total.removed },
                                  { QStringLiteral("failed"),   total.failed },
                                  { QStringLiteral("catalogs"), catalogs },
                                  { QStringLiteral("stopped"),  m_transcriptCleanStop.load() } };
        QMetaObject::invokeMethod(this, [this, counts]() { emit transcriptsCleaned(counts); }, Qt::QueuedConnection);
    });
    connect(qApp, &QCoreApplication::aboutToQuit, m_transcriptCleanThread, [this]() { stopPeopleWorkers(); });
    connect(m_transcriptCleanThread, &QThread::finished, this, [this]() {
        m_transcriptCleanThread->deleteLater();
        m_transcriptCleanThread = nullptr;
        bumpMdiRevision(-1);
        scheduleFtsIndexing();   // the catalogs marked stale
        startNextEnrichment();   // jobs asked for meanwhile
    });
    m_transcriptCleanThread->start(QThread::LowPriority);
    result[QStringLiteral("started")] = true;
    return result;
}
//----------------------------------------------------------------------
QVariantMap AppManager::fileInfo(int catalogId, const QString &fullPath)
{
    QVariantMap info;
    const QString refusal = peopleStoreRefusal();
    info[QStringLiteral("available")] = refusal.isEmpty();
    if (!refusal.isEmpty()) {
        info[QStringLiteral("error")] = refusal;
        return info;
    }
    const MdiPeople::Annotation a = MdiPeople::annotation(kMdiMainConnection, catalogId, fullPath);
    info[QStringLiteral("title")]       = a.title;
    info[QStringLiteral("description")] = a.description;
    info[QStringLiteral("event")]       = a.event;
    info[QStringLiteral("place")]       = a.place;
    info[QStringLiteral("date")]        = a.date;
    info[QStringLiteral("author")]      = a.author;
    info[QStringLiteral("notes")]       = a.notes;
    info[QStringLiteral("tags")]        = a.tags;
    info[QStringLiteral("updated")]     = a.updated;
    QVariantList people;
    for (const MdiPeople::FilePerson &p : MdiPeople::peopleOfFile(kMdiMainConnection, catalogId, fullPath))
        people.append(QVariantMap{ { QStringLiteral("personId"), p.personId },
                                   { QStringLiteral("name"),     p.name },
                                   { QStringLiteral("role"),     p.role },
                                   { QStringLiteral("faceId"),   p.faceId },
                                   { QStringLiteral("source"),   p.source } });
    info[QStringLiteral("people")] = people;
    return info;
}
//----------------------------------------------------------------------
QString AppManager::setFileInfo(const QVariantList &files, const QVariantMap &fields)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    const QString refusal = peopleStoreRefusal();
    if (!refusal.isEmpty())
        return refusal;
    const QList<MdiPeople::FileRef> refs = peopleFileRefs(files);
    if (refs.isEmpty())
        return tr("Select one or more files.");

    QVariantMap annotation = fields;
    for (const QString &key : {QStringLiteral("people"), QStringLiteral("peopleAdd"), QStringLiteral("peopleRemove")})
        annotation.remove(key);
    bool ok = MdiPeople::updateAnnotations(kMdiMainConnection, refs, annotation);
    const bool peopleTouched = fields.contains(QStringLiteral("people")) || fields.contains(QStringLiteral("peopleAdd"))
                            || fields.contains(QStringLiteral("peopleRemove"));
    if (ok && fields.contains(QStringLiteral("people")))
        ok = MdiPeople::setManualPeople(kMdiMainConnection, refs, peopleIds(fields.value(QStringLiteral("people")).toList()));
    if (ok && fields.contains(QStringLiteral("peopleAdd")))
        ok = MdiPeople::addPeopleToFiles(kMdiMainConnection, refs, peopleIds(fields.value(QStringLiteral("peopleAdd")).toList()));
    if (ok && fields.contains(QStringLiteral("peopleRemove")))
        for (int personId : peopleIds(fields.value(QStringLiteral("peopleRemove")).toList()))
            for (const MdiPeople::FileRef &r : refs)
                ok = ok && MdiPeople::removePersonFromFile(kMdiMainConnection, personId, r.catalogId, r.fullPath);

    reindexFileInfo(peoplePairs(refs));
    emit fileInfoChanged();
    if (peopleTouched) {
        emit peopleChanged();
        emit facesChanged();
    }
    return ok ? QString() : tr("The change could not be saved.");
}
//----------------------------------------------------------------------
QVariantList AppManager::people(const QString &filter)
{
    QVariantList list;
    if (!peopleStoreRefusal().isEmpty())
        return list;
    const QHash<int, QString> covers = MdiPeople::coverCrops(kMdiMainConnection);
    for (const MdiPeople::Person &p : MdiPeople::persons(kMdiMainConnection, filter)) {
        QString cover = existingFileUrl(covers.value(p.id));
        if (cover.isEmpty()) {
            // No face yet: the preview of one of the person's files
            const QList<MdiPeople::FileRef> files = MdiPeople::filesOfPerson(kMdiMainConnection, p.id);
            for (int i = 0; i < files.size() && i < 5 && cover.isEmpty(); ++i)
                cover = mdiPreviewUrl(files.at(i).catalogId, files.at(i).fullPath, m_mdiRevision);
        }
        list.append(QVariantMap{
            { QStringLiteral("id"),           p.id },
            { QStringLiteral("firstName"),    p.firstName },
            { QStringLiteral("lastName"),     p.lastName },
            { QStringLiteral("name"),         p.displayName() },
            { QStringLiteral("role"),         p.role },
            { QStringLiteral("organisation"), p.organisation },
            { QStringLiteral("notes"),        p.notes },
            { QStringLiteral("fileCount"),    p.fileCount },
            { QStringLiteral("faceCount"),    p.faceCount },
            { QStringLiteral("pendingCount"), p.pendingCount },
            { QStringLiteral("coverUrl"),     cover },
        });
    }
    return list;
}
//----------------------------------------------------------------------
QVariantMap AppManager::person(int personId)
{
    for (const QVariant &v : people(QString()))
        if (v.toMap().value(QStringLiteral("id")).toInt() == personId)
            return v.toMap();
    return QVariantMap();
}
//----------------------------------------------------------------------
int AppManager::savePerson(const QVariantMap &values)
{
    if (getReadOnly()) return -1;   // read-only collection (D43)
    if (!peopleStoreRefusal().isEmpty())
        return -1;
    MdiPeople::Person p;
    p.id           = values.value(QStringLiteral("id")).toInt();
    p.firstName    = values.value(QStringLiteral("firstName")).toString();
    p.lastName     = values.value(QStringLiteral("lastName")).toString();
    p.role         = values.value(QStringLiteral("role")).toString();
    p.organisation = values.value(QStringLiteral("organisation")).toString();
    p.notes        = values.value(QStringLiteral("notes")).toString();
    const int id = MdiPeople::savePerson(kMdiMainConnection, p);
    if (id <= 0)
        return -1;
    // A new name or role: the person's files are found by it
    if (p.id > 0)
        reindexFileInfo(peoplePairs(MdiPeople::filesOfPerson(kMdiMainConnection, id)));
    emit peopleChanged();
    if (p.id > 0) {
        emit fileInfoChanged();
        emit facesChanged();
    }
    return id;
}
//----------------------------------------------------------------------
QString AppManager::deletePerson(int personId)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    const QString refusal = peopleStoreRefusal();
    if (!refusal.isEmpty())
        return refusal;
    // Only the person and the links: no file is ever deleted
    const QList<MdiPeople::FileRef> files = MdiPeople::filesOfPerson(kMdiMainConnection, personId);
    if (!MdiPeople::deletePerson(kMdiMainConnection, personId))
        return tr("The change could not be saved.");
    reindexFileInfo(peoplePairs(files));
    emit peopleChanged();
    emit facesChanged();
    emit fileInfoChanged();
    return QString();
}
//----------------------------------------------------------------------
QString AppManager::addPeopleToFiles(const QVariantList &files, const QVariantList &personIds)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    const QString refusal = peopleStoreRefusal();
    if (!refusal.isEmpty())
        return refusal;
    const QList<MdiPeople::FileRef> refs = peopleFileRefs(files);
    if (refs.isEmpty())
        return tr("Select one or more files.");
    if (!MdiPeople::addPeopleToFiles(kMdiMainConnection, refs, peopleIds(personIds)))
        return tr("The change could not be saved.");
    reindexFileInfo(peoplePairs(refs));
    emit fileInfoChanged();
    emit peopleChanged();
    return QString();
}
//----------------------------------------------------------------------
QString AppManager::removePersonFromFile(int personId, int catalogId, const QString &fullPath)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    const QString refusal = peopleStoreRefusal();
    if (!refusal.isEmpty())
        return refusal;
    if (!MdiPeople::removePersonFromFile(kMdiMainConnection, personId, catalogId, fullPath))
        return tr("The change could not be saved.");
    reindexFileInfo({{catalogId, fullPath}});
    emit fileInfoChanged();
    emit peopleChanged();
    emit facesChanged();
    return QString();
}
//----------------------------------------------------------------------
QVariantList AppManager::filesOfPerson(int personId)
{
    QVariantList list;
    if (!peopleStoreRefusal().isEmpty())
        return list;
    QHash<int, int> deviceByCatalog;
    for (const Device::DeviceTreeNode &n : Device::loadDeviceTree(m_connectionName))
        if (n.type == QLatin1String("Catalog"))
            deviceByCatalog.insert(n.externalId, n.id);
    QList<MdiPeople::FileRef> files = MdiPeople::filesOfPerson(kMdiMainConnection, personId);
    // By catalog: the preview index is read once per catalog
    std::stable_sort(files.begin(), files.end(), [](const MdiPeople::FileRef &a, const MdiPeople::FileRef &b) {
        return a.catalogId < b.catalogId;
    });
    for (const MdiPeople::FileRef &f : std::as_const(files)) {
        const QFileInfo info(f.fullPath);
        list.append(QVariantMap{
            { QStringLiteral("catalogId"),  f.catalogId },
            { QStringLiteral("deviceId"),   deviceByCatalog.value(f.catalogId, 0) },
            { QStringLiteral("fullPath"),   f.fullPath },
            { QStringLiteral("name"),       info.fileName() },
            { QStringLiteral("folderPath"), info.path() },
            { QStringLiteral("thumbUrl"),   mdiPreviewUrl(f.catalogId, f.fullPath, m_mdiRevision) },
        });
    }
    return list;
}
//----------------------------------------------------------------------
QVariantList AppManager::findFilesByInfo(const QString &text, int limit)
{
    QVariantList list;
    if (text.trimmed().isEmpty() || !peopleStoreRefusal().isEmpty())
        return list;
    QHash<int, int> deviceByCatalog;
    for (const Device::DeviceTreeNode &n : Device::loadDeviceTree(m_connectionName))
        if (n.type == QLatin1String("Catalog"))
            deviceByCatalog.insert(n.externalId, n.id);
    for (const MdiPeople::FileRef &f : MdiPeople::findFiles(kMdiMainConnection, text, {}, limit)) {
        const QFileInfo info(f.fullPath);
        list.append(QVariantMap{
            { QStringLiteral("catalogId"),  f.catalogId },
            { QStringLiteral("deviceId"),   deviceByCatalog.value(f.catalogId, 0) },
            { QStringLiteral("fullPath"),   f.fullPath },
            { QStringLiteral("name"),       info.fileName() },
            { QStringLiteral("folderPath"), info.path() },
            { QStringLiteral("thumbUrl"),   mdiPreviewUrl(f.catalogId, f.fullPath, m_mdiRevision) },
        });
    }
    return list;
}
//----------------------------------------------------------------------
bool AppManager::facesAvailable()
{
    return facesUnavailableReason().isEmpty();
}
//----------------------------------------------------------------------
QString AppManager::facesUnavailableReason()
{
    if (collection->databaseMode == QLatin1String("Hosted"))
        return tr("Face recognition is not available for hosted collections.");
    if (!MdiFaces::compiledIn())
        return tr("Face recognition is not available: this version of Teca was built without OpenCV.");
    const QVariantMap tools = getEnrichmentTools();
    if (tools.value(QStringLiteral("faceDetectorModel")).toString().isEmpty()
        || tools.value(QStringLiteral("faceRecognizerModel")).toString().isEmpty())
        return tr("Face recognition models not found: face_detection_yunet and face_recognition_sface (ONNX) are needed in %1.")
            .arg(QDir::homePath() + QStringLiteral("/teca-models"));
    return QString();
}
//----------------------------------------------------------------------
QVariantList AppManager::facesInFile(int catalogId, const QString &fullPath)
{
    QVariantList list;
    if (!peopleStoreRefusal().isEmpty())
        return list;
    const QHash<int, QString> names = personNames(kMdiMainConnection);
    for (const MdiPeople::Face &f : MdiPeople::facesOfFile(kMdiMainConnection, catalogId, fullPath))
        list.append(faceRow(f, names));
    return list;
}
//----------------------------------------------------------------------
QString AppManager::confirmFaces(const QVariantList &faceIds, int personId)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    const QString refusal = peopleStoreRefusal();
    if (!refusal.isEmpty())
        return refusal;
    const QList<int> ids = peopleIds(faceIds);
    QList<QPair<int, QString>> files;
    for (int id : ids) {
        const MdiPeople::Face f = MdiPeople::face(kMdiMainConnection, id);
        if (f.id == id && !files.contains({f.catalogId, f.fullPath}))
            files.append({f.catalogId, f.fullPath});
    }
    if (!MdiPeople::confirmFaces(kMdiMainConnection, ids, personId))
        return tr("The change could not be saved.");
    // The person's new faces make new suggestions, still to be approved
    MdiPeople::suggestForPerson(kMdiMainConnection, personId, faceMatchThreshold());
    reindexFileInfo(files);
    emit facesChanged();
    emit peopleChanged();
    emit fileInfoChanged();
    return QString();
}
//----------------------------------------------------------------------
QString AppManager::rejectFaces(const QVariantList &faceIds)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    const QString refusal = peopleStoreRefusal();
    if (!refusal.isEmpty())
        return refusal;
    const QList<int> ids = peopleIds(faceIds);
    QList<QPair<int, QString>> files;
    for (int id : ids) {
        const MdiPeople::Face f = MdiPeople::face(kMdiMainConnection, id);
        if (f.id == id && f.personId > 0)
            files.append({f.catalogId, f.fullPath});
    }
    if (!MdiPeople::rejectFaces(kMdiMainConnection, ids))
        return tr("The change could not be saved.");
    reindexFileInfo(files);
    emit facesChanged();
    emit peopleChanged();
    if (!files.isEmpty())
        emit fileInfoChanged();
    return QString();
}
//----------------------------------------------------------------------
QString AppManager::clearFaceData()
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    const QString refusal = peopleStoreRefusal();
    if (!refusal.isEmpty())
        return refusal;
    // Not while the analysis may be writing faces
    if (m_mdiThread)
        return tr("A device operation is already running.");
    QList<QPair<int, QString>> files;
    QStringList crops;
    if (!MdiPeople::clearAllFaces(kMdiMainConnection, &files, &crops))
        return tr("The change could not be saved.");
    // The crops are files the analysis made under its own folder: only those go
    for (const QString &crop : std::as_const(crops))
        if (crop.contains(QStringLiteral("/katalog_mdi/")))
            QFile::remove(crop);
    reindexFileInfo(files);
    emit facesChanged();
    emit peopleChanged();
    emit fileInfoChanged();
    return QString();
}
//----------------------------------------------------------------------
QString AppManager::ignoreFaces(const QVariantList &faceIds)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    const QString refusal = peopleStoreRefusal();
    if (!refusal.isEmpty())
        return refusal;
    const QList<int> ids = peopleIds(faceIds);
    QList<QPair<int, QString>> files;
    for (int id : ids) {
        const MdiPeople::Face f = MdiPeople::face(kMdiMainConnection, id);
        if (f.id == id && f.personId > 0)
            files.append({f.catalogId, f.fullPath});
    }
    if (!MdiPeople::ignoreFaces(kMdiMainConnection, ids))
        return tr("The change could not be saved.");
    reindexFileInfo(files);
    emit facesChanged();
    emit peopleChanged();
    if (!files.isEmpty())
        emit fileInfoChanged();
    return QString();
}
//----------------------------------------------------------------------
QString AppManager::clearFace(int faceId)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    const QString refusal = peopleStoreRefusal();
    if (!refusal.isEmpty())
        return refusal;
    const MdiPeople::Face f = MdiPeople::face(kMdiMainConnection, faceId);
    if (!MdiPeople::clearFace(kMdiMainConnection, faceId))
        return tr("The change could not be saved.");
    if (f.personId > 0) {
        reindexFileInfo({{f.catalogId, f.fullPath}});
        emit fileInfoChanged();
    }
    emit facesChanged();
    emit peopleChanged();
    return QString();
}
//----------------------------------------------------------------------
QVariantList AppManager::pendingFaces(int limit)
{
    QVariantList list;
    if (!peopleStoreRefusal().isEmpty())
        return list;
    const QHash<int, QString> names = personNames(kMdiMainConnection);
    for (const MdiPeople::Face &f : MdiPeople::pendingFaces(kMdiMainConnection, limit))
        list.append(faceRow(f, names));
    return list;
}
//----------------------------------------------------------------------
// Similar faces and face groups (fork, 2026-10-30): the grouping compares
// every unnamed face with the others, so both run on a worker with its own
// mdi_data.db connection (Qt SQL connections belong to one thread), and the
// results are kept until faces or people change, a job writes, or the
// collection changes.
namespace {
QVariantList similarFaceRows(const QString &connectionName, int personId, int limit, double floor)
{
    QVariantList list;
    const QHash<int, QString> names = personNames(connectionName);
    for (const auto &pair : MdiPeople::similarFaces(connectionName, personId, limit, floor)) {
        QVariantMap row = faceRow(pair.first, names);
        row[QStringLiteral("suggestionScore")] = row.value(QStringLiteral("score"));
        row[QStringLiteral("score")] = pair.second;   // similarity to this person
        list.append(row);
    }
    return list;
}

QVariantList faceGroupRows(const QString &connectionName, int limit, double threshold)
{
    QVariantList list;
    const QHash<int, QString> names = personNames(connectionName);
    int groupId = 0;
    for (const QList<MdiPeople::Face> &group : MdiPeople::unnamedGroups(connectionName, limit, threshold)) {
        QVariantList faces;
        for (const MdiPeople::Face &f : group)
            faces.append(faceRow(f, names));
        list.append(QVariantMap{ { QStringLiteral("groupId"), ++groupId },
                                 { QStringLiteral("size"),    int(group.size()) },
                                 { QStringLiteral("faces"),   faces } });
    }
    return list;
}

QString similarKey(const QVariantMap &asked)
{
    return QStringLiteral("%1/%2/%3").arg(asked.value(QStringLiteral("personId")).toInt())
                                     .arg(asked.value(QStringLiteral("limit")).toInt())
                                     .arg(asked.value(QStringLiteral("floor")).toDouble(), 0, 'f', 4);
}
} // namespace
//----------------------------------------------------------------------
void AppManager::invalidateFaceCache()
{
    // A computation running now is dropped when it ends: it read older data
    ++m_faceCacheRevision;
    m_faceGroupsCache.clear();
    m_similarCache.clear();
}
//----------------------------------------------------------------------
void AppManager::checkFaceCachePath()
{
    // Another collection: nothing kept from the previous one
    const QString path = mdiDataPath();
    if (m_faceCachePath == path)
        return;
    invalidateFaceCache();
    m_faceGroupsWanted = -1;
    m_similarWanted.clear();
    m_faceCachePath = path;
}
//----------------------------------------------------------------------
void AppManager::requestSimilarFaces(int personId, int limit, double minScore)
{
    if (!peopleStoreRefusal().isEmpty()) {
        emit similarFacesReady(personId, QVariantList(), 0);
        return;
    }
    checkFaceCachePath();
    const QVariantMap asked{ { QStringLiteral("personId"), personId },
                             { QStringLiteral("limit"),    limit },
                             { QStringLiteral("floor"),    minScore > 0 ? minScore : faceMatchThreshold() } };
    // Kept while no job wrote since (a face search adds faces for hours)
    const QVariantMap kept = m_similarCache.value(similarKey(asked));
    if (!kept.isEmpty() && kept.value(QStringLiteral("mdiRevision")).toInt() == m_mdiRevision) {
        emit similarFacesReady(personId, kept.value(QStringLiteral("rows")).toList(),
                               kept.value(QStringLiteral("serial")).toInt());
        return;
    }
    // One person is shown at a time: the last one asked is computed
    m_similarWanted = asked;
    runFaceWorker();
}
//----------------------------------------------------------------------
void AppManager::requestFaceGroups(int limit)
{
    if (!peopleStoreRefusal().isEmpty()) {
        emit faceGroupsReady(QVariantList(), 0);
        return;
    }
    checkFaceCachePath();
    if (!m_faceGroupsCache.isEmpty() && m_faceGroupsCache.value(QStringLiteral("limit")).toInt() == limit
        && m_faceGroupsCache.value(QStringLiteral("mdiRevision")).toInt() == m_mdiRevision) {
        emit faceGroupsReady(m_faceGroupsCache.value(QStringLiteral("rows")).toList(),
                             m_faceGroupsCache.value(QStringLiteral("serial")).toInt());
        return;
    }
    m_faceGroupsWanted = limit;
    runFaceWorker();
}
//----------------------------------------------------------------------
void AppManager::runFaceWorker()
{
    // One computation at a time; what is asked meanwhile runs when it ends
    if (m_faceThread || (m_faceGroupsWanted < 0 && m_similarWanted.isEmpty()))
        return;
    const QString     path           = m_faceCachePath;
    const int         revision       = m_faceCacheRevision;
    const int         mdiRevision    = m_mdiRevision;
    const int         groupsLimit    = m_faceGroupsWanted;
    const double      groupThreshold = qMax(MdiPeople::kGroupThreshold, faceMatchThreshold());
    const QVariantMap similar        = m_similarWanted;
    m_faceGroupsWanted = -1;
    m_similarWanted.clear();
    // The results kept, outdated only by a job's writes: when nothing they
    // show changed, the page keeps what it draws (open groups stay open)
    const bool        hadGroups     = !m_faceGroupsCache.isEmpty()
                                      && m_faceGroupsCache.value(QStringLiteral("limit")).toInt() == groupsLimit;
    const QVariantList oldGroups    = m_faceGroupsCache.value(QStringLiteral("rows")).toList();
    const int         oldGroupsSerial = m_faceGroupsCache.value(QStringLiteral("serial")).toInt();
    const QVariantMap oldSimilar    = similar.isEmpty() ? QVariantMap() : m_similarCache.value(similarKey(similar));

    m_faceThread = QThread::create([this, path, revision, mdiRevision, groupsLimit, groupThreshold, similar,
                                    hadGroups, oldGroups, oldGroupsSerial, oldSimilar]() {
        const QString connection = QStringLiteral("mdi_faces_%1").arg(reinterpret_cast<quintptr>(QThread::currentThread()));
        const int personId = similar.value(QStringLiteral("personId")).toInt();
        QVariantList groups, similarRows;
        const bool opened = MdiData::open(connection, path);
        if (opened) {
            if (personId > 0)
                similarRows = similarFaceRows(connection, personId, similar.value(QStringLiteral("limit")).toInt(),
                                              similar.value(QStringLiteral("floor")).toDouble());
            if (groupsLimit >= 0)
                groups = faceGroupRows(connection, groupsLimit, groupThreshold);
        }
        MdiData::close(connection);
        const bool sameGroups  = hadGroups && groups == oldGroups;
        const bool sameSimilar = !oldSimilar.isEmpty() && similarRows == oldSimilar.value(QStringLiteral("rows")).toList();
        QMetaObject::invokeMethod(this, [this, path, revision, mdiRevision, opened, personId, similar, similarRows,
                                         sameSimilar, oldSimilar, groupsLimit, groups, sameGroups, oldGroupsSerial]() {
            // Faces or people changed meanwhile, or another collection is open:
            // dropped, the page asks again when it shows them
            if (path != m_faceCachePath || revision != m_faceCacheRevision)
                return;
            if (personId > 0) {
                const int serial = !opened ? 0 : sameSimilar ? oldSimilar.value(QStringLiteral("serial")).toInt()
                                                             : ++m_faceSerial;
                if (opened)
                    m_similarCache.insert(similarKey(similar),
                                          QVariantMap{ { QStringLiteral("rows"),        similarRows },
                                                       { QStringLiteral("serial"),      serial },
                                                       { QStringLiteral("mdiRevision"), mdiRevision } });
                if (m_similarWanted == similar)
                    m_similarWanted.clear();   // asked again meanwhile: just answered
                emit similarFacesReady(personId, similarRows, serial);
            }
            if (groupsLimit >= 0) {
                const int serial = !opened ? 0 : sameGroups ? oldGroupsSerial : ++m_faceSerial;
                if (opened)
                    m_faceGroupsCache = QVariantMap{ { QStringLiteral("limit"),       groupsLimit },
                                                     { QStringLiteral("rows"),        groups },
                                                     { QStringLiteral("serial"),      serial },
                                                     { QStringLiteral("mdiRevision"), mdiRevision } };
                if (m_faceGroupsWanted == groupsLimit)
                    m_faceGroupsWanted = -1;
                emit faceGroupsReady(groups, serial);
            }
        }, Qt::QueuedConnection);
    });
    connect(qApp, &QCoreApplication::aboutToQuit, m_faceThread, [this]() { stopPeopleWorkers(); });
    connect(m_faceThread, &QThread::finished, this, [this]() {
        m_faceThread->deleteLater();
        m_faceThread = nullptr;
        runFaceWorker();   // asked meanwhile
    });
    m_faceThread->start(QThread::LowPriority);
}
//----------------------------------------------------------------------
QString AppManager::startFaceAnalysis(int deviceId)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    const QString reason = facesUnavailableReason();
    if (!reason.isEmpty())
        return reason;
    MdiData::PendingJob job;
    job.deviceId = deviceId;
    job.kind     = QStringLiteral("faces");
    return startMdiJob(job, false);
}
//----------------------------------------------------------------------
// Dating, document study, web lookups and HTML export (fork, owner's
// decisions of 2026-10-30). Inferred information is only proposed; the
// owner approves it (MdiProposals).
//----------------------------------------------------------------------
namespace {

QList<int> intList(const QVariantList &values)
{
    QList<int> ids;
    for (const QVariant &v : values) {
        const int id = v.toInt();
        if (id > 0 && !ids.contains(id))
            ids.append(id);
    }
    return ids;
}

QString ownFieldValue(const MdiPeople::Annotation &a, const QString &field)
{
    if (field == QLatin1String("date"))   return a.date;
    if (field == QLatin1String("event"))  return a.event;
    if (field == QLatin1String("place"))  return a.place;
    if (field == QLatin1String("author")) return a.author;
    if (field == QLatin1String("title"))  return a.title;
    return QString();
}

QString documentTypeLabel(const QString &key)
{
    if (key == QLatin1String("letter"))        return AppManager::tr("Letter");
    if (key == QLatin1String("email"))         return AppManager::tr("Email");
    if (key == QLatin1String("regulation"))    return AppManager::tr("Regulation");
    if (key == QLatin1String("results"))       return AppManager::tr("Results");
    if (key == QLatin1String("press-release")) return AppManager::tr("Press release");
    if (key == QLatin1String("invoice"))       return AppManager::tr("Invoice");
    if (key == QLatin1String("receipt"))       return AppManager::tr("Receipt");
    if (key == QLatin1String("minutes"))       return AppManager::tr("Minutes");
    if (key == QLatin1String("report"))        return AppManager::tr("Report");
    if (key == QLatin1String("contract"))      return AppManager::tr("Contract");
    if (key == QLatin1String("form"))          return AppManager::tr("Form");
    if (key == QLatin1String("programme"))     return AppManager::tr("Programme");
    if (key == QLatin1String("certificate"))   return AppManager::tr("Certificate");
    if (key == QLatin1String("newsletter"))    return AppManager::tr("Newsletter");
    if (key == QLatin1String("article"))       return AppManager::tr("Article");
    if (key == QLatin1String("manual"))        return AppManager::tr("Manual");
    if (key == QLatin1String("presentation"))  return AppManager::tr("Presentation");
    if (key == QLatin1String("list"))          return AppManager::tr("List");
    if (key == QLatin1String("budget"))        return AppManager::tr("Budget");
    if (key == QLatin1String("notice"))        return AppManager::tr("Notice");
    return AppManager::tr("Other");
}

QString pagesText(int from, int to)
{
    if (from <= 0)
        return QString();
    return from == to ? QString::number(from) : QStringLiteral("%1–%2").arg(from).arg(to);
}

// The words of the exported page, translated (MdiHtmlExport keys)
MdiHtmlExport::Labels htmlLabels(const std::function<QString(const QString &)> &kindLabel)
{
    MdiHtmlExport::Labels l;
    l["contents"] = AppManager::tr("Contents");
    l["skipToContent"] = AppManager::tr("Skip to content");
    l["backToContents"] = AppManager::tr("Back to contents");
    l["fileCount"] = AppManager::tr("%1 files");
    l["path"] = AppManager::tr("Path");
    l["type"] = AppManager::tr("Type");
    l["size"] = AppManager::tr("Size");
    l["modified"] = AppManager::tr("Modified");
    l["information"] = AppManager::tr("Information");
    l["noInformation"] = AppManager::tr("No information written for this file.");
    l["title"] = AppManager::tr("Title");
    l["description"] = AppManager::tr("Description");
    l["event"] = AppManager::tr("Event");
    l["place"] = AppManager::tr("Place");
    l["date"] = AppManager::tr("Date");
    l["author"] = AppManager::tr("Author");
    l["notes"] = AppManager::tr("Notes");
    l["tags"] = AppManager::tr("Tags");
    l["people"] = AppManager::tr("People");
    l["proposals"] = AppManager::tr("Proposals");
    l["proposalsCaption"] = AppManager::tr("Information inferred by Teca, with its sources");
    l["field"] = AppManager::tr("Field");
    l["value"] = AppManager::tr("Value");
    l["confidence"] = AppManager::tr("Confidence");
    l["status"] = AppManager::tr("Status");
    l["sources"] = AppManager::tr("Sources");
    l["pending"] = AppManager::tr("To approve");
    l["accepted"] = AppManager::tr("Approved");
    l["rejected"] = AppManager::tr("Rejected");
    l["superseded"] = AppManager::tr("Replaced");
    l["precision-day"] = AppManager::tr("day");
    l["precision-month"] = AppManager::tr("month");
    l["precision-year"] = AppManager::tr("year");
    l["precision-range"] = AppManager::tr("period");
    l["precision-coordinates"] = AppManager::tr("coordinates");
    for (const char *k : {"exif", "video", "filename", "folder", "folder-consistency", "caption", "classification",
                          "context", "knowledge", "web", "model", "document"})
        l[QStringLiteral("source-") + QLatin1String(k)] = kindLabel(QLatin1String(k));
    l["aiDescription"] = AppManager::tr("AI description");
    l["classificationTitle"] = AppManager::tr("Title");
    l["entities"] = AppManager::tr("Named");
    l["excerpts"] = AppManager::tr("Excerpts");
    l["study"] = AppManager::tr("Document study");
    l["synthesis"] = AppManager::tr("Synthesis");
    l["documentType"] = AppManager::tr("Kind of document");
    l["documentDate"] = AppManager::tr("Date of the document");
    l["recipients"] = AppManager::tr("Recipients");
    l["pages"] = AppManager::tr("Pages");
    l["ocrPages"] = AppManager::tr("Pages read by OCR");
    l["keyFacts"] = AppManager::tr("Key facts");
    l["dates"] = AppManager::tr("Dates");
    l["places"] = AppManager::tr("Places");
    l["organisations"] = AppManager::tr("Organisations");
    l["events"] = AppManager::tr("Events");
    l["glossary"] = AppManager::tr("Terms of the glossary");
    l["relatedDocuments"] = AppManager::tr("Related reference documents");
    l["relatedPeople"] = AppManager::tr("People named");
    l["fragments"] = AppManager::tr("Fragments");
    l["page"] = AppManager::tr("page %1");
    l["pageRange"] = AppManager::tr("pages %1–%2");
    l["fullText"] = AppManager::tr("Full text");
    l["previewOf"] = AppManager::tr("Preview of %1");
    return l;
}

} // namespace
//----------------------------------------------------------------------
QString AppManager::proposalKindLabel(const QString &kind) const
{
    if (kind == QLatin1String("exif"))               return tr("Photo metadata");
    if (kind == QLatin1String("video"))              return tr("Video metadata");
    if (kind == QLatin1String("filename"))           return tr("File name");
    if (kind == QLatin1String("folder"))             return tr("Folder");
    if (kind == QLatin1String("folder-consistency")) return tr("Other files of the folder");
    if (kind == QLatin1String("caption"))            return tr("Image description");
    if (kind == QLatin1String("classification"))     return tr("Classification");
    if (kind == QLatin1String("context"))            return tr("Context of the archive");
    if (kind == QLatin1String("knowledge"))          return tr("Reference document");
    if (kind == QLatin1String("web"))                return tr("Web");
    if (kind == QLatin1String("model"))              return tr("Text model");
    if (kind == QLatin1String("document"))           return tr("Document study");
    return kind;
}
//----------------------------------------------------------------------
QString AppManager::proposalFieldLabel(const QString &field) const
{
    if (field == QLatin1String("date"))   return tr("Date");
    if (field == QLatin1String("event"))  return tr("Event");
    if (field == QLatin1String("place"))  return tr("Place");
    if (field == QLatin1String("author")) return tr("Author");
    if (field == QLatin1String("title"))  return tr("Title");
    return field;
}
//----------------------------------------------------------------------
QVariantList AppManager::proposalSources(const QVariantList &sources) const
{
    QVariantList out;
    for (const QVariant &v : sources) {
        QVariantMap m = v.toMap();
        m[QStringLiteral("kindLabel")] = proposalKindLabel(m.value(QStringLiteral("kind")).toString());
        out.append(m);
    }
    return out;
}
//----------------------------------------------------------------------
QVariantList AppManager::proposalsForFile(int catalogId, const QString &fullPath)
{
    QVariantList list;
    if (!peopleStoreRefusal().isEmpty())
        return list;
    const MdiPeople::Annotation own = MdiPeople::annotation(kMdiMainConnection, catalogId, fullPath);
    for (const MdiProposals::Proposal &p : MdiProposals::forFile(kMdiMainConnection, catalogId, fullPath)) {
        QVariantMap m = p.toVariant();
        m[QStringLiteral("fieldLabel")] = proposalFieldLabel(p.field);
        m[QStringLiteral("ownValue")]   = ownFieldValue(own, p.field);
        m[QStringLiteral("sources")]    = proposalSources(m.value(QStringLiteral("sources")).toList());
        list.append(m);
    }
    return list;
}
//----------------------------------------------------------------------
QVariantList AppManager::proposalsForFolder(int catalogId, const QString &folderPath, bool recursive)
{
    QVariantList list;
    if (!peopleStoreRefusal().isEmpty())
        return list;
    for (const QVariant &v : MdiProposals::forFolder(kMdiMainConnection, catalogId, folderPath, recursive)) {
        QVariantMap m = v.toMap();
        m[QStringLiteral("fieldLabel")] = proposalFieldLabel(m.value(QStringLiteral("field")).toString());
        m[QStringLiteral("sources")]    = proposalSources(m.value(QStringLiteral("sources")).toList());
        list.append(m);
    }
    return list;
}
//----------------------------------------------------------------------
QString AppManager::acceptProposals(const QVariantList &proposalIds, bool replaceOwnValues)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    const QString refusal = peopleStoreRefusal();
    if (!refusal.isEmpty())
        return refusal;
    const QList<int> ids = intList(proposalIds);
    if (ids.isEmpty())
        return QString();
    QList<QPair<int, QString>> files;
    for (const MdiProposals::Proposal &p : MdiProposals::byIds(kMdiMainConnection, ids))
        if (!files.contains(qMakePair(p.catalogId, p.fullPath)))
            files.append(qMakePair(p.catalogId, p.fullPath));
    const MdiProposals::AcceptResult result = MdiProposals::acceptAndApply(kMdiMainConnection, ids, replaceOwnValues);
    // The written values are searchable like any information of the owner's
    reindexFileInfo(files);
    emit fileInfoChanged();
    emit proposalsChanged();
    if (!result.ok)
        return tr("The change could not be saved.");
    if (result.kept > 0)
        return tr("%n proposal(s) left to approve: the file already has its own value for that field.", "", result.kept);
    return QString();
}
//----------------------------------------------------------------------
QString AppManager::rejectProposals(const QVariantList &proposalIds)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    const QString refusal = peopleStoreRefusal();
    if (!refusal.isEmpty())
        return refusal;
    const bool ok = MdiProposals::setStatus(kMdiMainConnection, intList(proposalIds), QStringLiteral("rejected"));
    emit proposalsChanged();
    return ok ? QString() : tr("The change could not be saved.");
}
//----------------------------------------------------------------------
QString AppManager::startDating(int deviceId)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    const QString refusal = peopleStoreRefusal();
    if (!refusal.isEmpty())
        return refusal;
    MdiData::PendingJob job;
    job.deviceId = deviceId;
    job.kind     = QStringLiteral("dating");
    return startMdiJob(job, false);
}
//----------------------------------------------------------------------
QString AppManager::startDocumentStudy(int deviceId)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    MdiData::PendingJob job;
    job.deviceId = deviceId;
    job.kind     = QStringLiteral("document");
    return startMdiJob(job, false);
}
//----------------------------------------------------------------------
QString AppManager::collectionSettingsKey(const QString &key) const
{
    // The settings file is shared by every collection: a setting of one
    // collection carries its identity (mode and where its database is)
    const QString where = collection->databaseMode == QLatin1String("Hosted")
                        ? collection->databaseHostName + QLatin1Char('/') + collection->databaseName
                        : collection->databaseMode == QLatin1String("Memory") ? collection->folder
                                                                             : collection->databaseFilePath;
    const QByteArray id = QCryptographicHash::hash((collection->databaseMode + QLatin1Char(':') + where).toUtf8(),
                                                   QCryptographicHash::Sha1).toHex().left(16);
    return key + QLatin1Char('/') + QString::fromLatin1(id);
}
//----------------------------------------------------------------------
bool AppManager::webSearchEnabled()
{
    // Per collection, off by default (owner's decision 2026-10-30)
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    return settings.value(collectionSettingsKey(QStringLiteral("MediaIntelligence/WebSearch")), false).toBool();
}
//----------------------------------------------------------------------
void AppManager::setWebSearchEnabled(bool enabled)
{
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    settings.setValue(collectionSettingsKey(QStringLiteral("MediaIntelligence/WebSearch")), enabled);
    settings.sync();
}
//----------------------------------------------------------------------
QVariantList AppManager::pendingWebQueries(int deviceId)
{
    // Listed whether lookups are on or off: the owner sees what would be
    // sent before turning them on. Event, place and year words only.
    if (!peopleStoreRefusal().isEmpty())
        return {};
    Device device;
    device.ID = deviceId;
    device.loadDevice(m_connectionName);
    if (device.type != QLatin1String("Catalog"))
        return {};
    QStringList names;
    for (const MdiPeople::Person &p : MdiPeople::persons(kMdiMainConnection))
        names << p.displayName();
    return MdiDating::webQueries(kMdiMainConnection, device.externalID, device.path,
                                 MdiContext::load(kMdiMainConnection, MdiContext::kCollectionScope), names);
}
//----------------------------------------------------------------------
QString AppManager::runWebQueries(int deviceId, const QVariantList &queries)
{
    if (getReadOnly()) return readOnlyRefusal();   // read-only collection (D43)
    if (!webSearchEnabled())
        return tr("Web lookups are off for this collection.");
    if (m_webThread)
        return tr("Web lookups are already running.");
    const QString refusal = peopleStoreRefusal();
    if (!refusal.isEmpty())
        return refusal;
    Device device;
    device.ID = deviceId;
    device.loadDevice(m_connectionName);
    if (device.type != QLatin1String("Catalog"))
        return tr("Selected device is not a catalog");
    QStringList list;
    for (const QVariant &v : queries) {
        const QString q = (v.typeId() == QMetaType::QVariantMap ? v.toMap().value(QStringLiteral("query")).toString()
                                                                : v.toString()).simplified();
        if (!q.isEmpty() && !list.contains(q, Qt::CaseInsensitive) && list.size() < 50)
            list << q;
    }
    if (list.isEmpty())
        return QString();

    const QString dbPath = mdiDataPath();
    const int catalogId = device.externalID;
    const QString root = device.path;
    const QJsonObject context = MdiContext::load(kMdiMainConnection, MdiContext::kCollectionScope);
    QStringList names;
    for (const MdiPeople::Person &p : MdiPeople::persons(kMdiMainConnection))
        names << p.displayName();
    m_webStop = false;
    m_webThread = QThread::create([this, dbPath, catalogId, root, context, names, list]() {
        const QString conn = QStringLiteral("mdi_web_%1").arg(quintptr(QThread::currentThreadId()));
        int asked = 0, found = 0;
        QString lastError;
        if (MdiData::open(conn, dbPath)) {
            for (const QString &q : list) {
                if (m_webStop.load())
                    break;
                QString err;
                const QList<MdiWebSearch::Result> results = MdiWebSearch::search(q, &err, &m_webStop);
                if (m_webStop.load())
                    break;
                ++asked;
                // A failed lookup is not kept: the query stays pending
                if (results.isEmpty() && !err.isEmpty()) {
                    lastError = err;
                    continue;
                }
                MdiWebSearch::storeResults(conn, q, results);
                if (!results.isEmpty())
                    ++found;
            }
            // The proposals again, with what the web said (no model call here)
            if (!m_webStop.load() && found > 0) {
                MdiDating::DeriveOptions o;
                o.sourceRoot  = root;
                o.context     = context;
                o.personNames = names;
                o.stop        = &m_webStop;
                MdiDating::deriveCatalog(conn, catalogId, o);
            }
            MdiData::close(conn);
        } else {
            lastError = QStringLiteral("mdi_data.db could not be opened");
        }
        QMetaObject::invokeMethod(this, [this, asked, found, lastError]() {
            emit webQueriesFinished(asked, found, lastError);
            emit proposalsChanged();
        }, Qt::QueuedConnection);
    });
    connect(qApp, &QCoreApplication::aboutToQuit, m_webThread, [this]() { stopPeopleWorkers(); });
    connect(m_webThread, &QThread::finished, this, [this]() {
        m_webThread->deleteLater();
        m_webThread = nullptr;
        emit webQueriesRunningChanged();
    });
    m_webThread->start(QThread::LowPriority);
    emit webQueriesRunningChanged();
    return QString();
}
//----------------------------------------------------------------------
int AppManager::deviceIdOfCatalog(int catalogId)
{
    for (const Device::DeviceTreeNode &n : Device::loadDeviceTree(m_connectionName))
        if (n.type == QLatin1String("Catalog") && n.externalId == catalogId)
            return n.id;
    return -1;
}
//----------------------------------------------------------------------
QVariantMap AppManager::documentStudy(int catalogId, const QString &fullPath)
{
    QVariantMap out{ { QStringLiteral("available"), false } };
    if (!openMdiMainConnection())
        return out;
    MdiData::Datum datum;
    bool found = false;
    for (const MdiData::Datum &d : MdiData::forFile(kMdiMainConnection, catalogId, fullPath))
        if (d.analysis == QLatin1String("document")) {
            datum = d;
            found = true;
        }
    if (!found)
        return out;
    const QJsonObject &v = datum.value;
    out[QStringLiteral("available")]  = datum.outcome == QLatin1String("completed");
    out[QStringLiteral("outcome")]    = datum.outcome;
    out[QStringLiteral("reason")]     = datum.reason;
    out[QStringLiteral("model")]      = datum.model;
    out[QStringLiteral("studied")]    = datum.date;
    out[QStringLiteral("ocr")]        = v.value("ocr").toBool();
    out[QStringLiteral("ocrPages")]   = v.value("ocrPages").toArray().toVariantList();
    out[QStringLiteral("pages")]      = v.value("pages").toInt();
    out[QStringLiteral("partial")]    = v.value("partial").toBool();
    out[QStringLiteral("language")]   = v.value("language").toString();
    out[QStringLiteral("title")]      = v.value("title").toString();
    out[QStringLiteral("synthesis")]  = v.value("synthesis").toString();
    out[QStringLiteral("typeKey")]    = v.value("type").toString();
    out[QStringLiteral("type")]       = v.value("type").toString().isEmpty() ? QString()
                                                                              : documentTypeLabel(v.value("type").toString());
    out[QStringLiteral("date")]       = v.value("date").toString();
    out[QStringLiteral("dateValue")]  = v.value("dateValue").toString();
    out[QStringLiteral("author")]     = v.value("author").toString();
    out[QStringLiteral("recipients")] = v.value("recipients").toArray().toVariantList();
    out[QStringLiteral("keyFacts")]   = v.value("keyFacts").toObject().toVariantMap();
    out[QStringLiteral("tags")]       = v.value("tags").toArray().toVariantList();
    out[QStringLiteral("glossary")]   = v.value("glossary").toArray().toVariantList();

    QVariantList fragments;
    QString allText;
    for (const MdiDocument::Fragment &f : MdiDocument::fragments(kMdiMainConnection, catalogId, fullPath)) {
        QVariantList entities;
        for (auto it = f.entities.begin(); it != f.entities.end(); ++it)
            for (const QJsonValue &e : it.value().toArray())
                if (!e.toString().isEmpty() && !entities.contains(e.toString()))
                    entities.append(e.toString());
        fragments.append(QVariantMap{ { QStringLiteral("index"),        f.index },
                                      { QStringLiteral("pages"),        pagesText(f.pageFrom, f.pageTo) },
                                      { QStringLiteral("pageFrom"),     f.pageFrom },
                                      { QStringLiteral("pageTo"),       f.pageTo },
                                      { QStringLiteral("title"),        f.title },
                                      { QStringLiteral("summary"),      f.summary },
                                      { QStringLiteral("tags"),         f.tags },
                                      { QStringLiteral("entities"),     entities },
                                      { QStringLiteral("entityGroups"), f.entities.toVariantMap() },
                                      { QStringLiteral("text"),         f.text } });
        allText += f.text + QLatin1Char('\n');
    }
    out[QStringLiteral("fragments")] = fragments;

    // Related: reference documents found by the study; known people named,
    // with today's list of people
    QVariantList kb;
    for (const QJsonValue &d : v.value("related").toObject().value("kb").toArray())
        kb.append(QVariantMap{ { QStringLiteral("title"), d.toObject().value("title").toString() },
                               { QStringLiteral("path"),  d.toObject().value("path").toString() } });
    QList<MdiDocument::PersonName> people;
    if (collection->databaseMode != QLatin1String("Hosted"))
        for (const MdiPeople::Person &p : MdiPeople::persons(kMdiMainConnection))
            people.append(MdiDocument::PersonName{p.id, p.displayName(), p.firstName, p.lastName});
    out[QStringLiteral("related")] = QVariantMap{
        { QStringLiteral("kb"),     kb },
        { QStringLiteral("people"), MdiDocument::peopleFound(allText, people).toVariantList() } };
    return out;
}
//----------------------------------------------------------------------
QString AppManager::prepareHtmlExport(const QString &scope, int catalogId, const QString &path, const QString &targetFile,
                                      MdiHtmlExport::Scope *request, MdiHtmlExport::Options *options, QString *target)
{
    // What the page covers and how it reads, on this thread (one catalog
    // row); the files themselves are gathered by MdiHtmlExport::scopeRecords
    if (!openMdiMainConnection())
        return tr("The change could not be saved.");
    *target = QUrl(targetFile).isLocalFile() ? QUrl(targetFile).toLocalFile() : targetFile;
    if (target->isEmpty())
        return tr("Select a file to save the report to.");

    Catalog catalog;
    catalog.ID = catalogId;
    catalog.setConnectionName(m_connectionName);
    catalog.loadCatalog();
    QString root = catalog.sourcePath;
    while (root.size() > 1 && root.endsWith(QLatin1Char('/')))
        root.chop(1);
    QString folder = path;
    while (folder.size() > 1 && folder.endsWith(QLatin1Char('/')))
        folder.chop(1);

    request->scope           = scope;
    request->catalogId       = catalogId;
    request->path            = path;
    request->catalogRoot     = catalog.sourcePath;
    request->databaseMode    = collection->databaseMode;
    request->catalogFilePath = catalog.filePath;
    request->withInformation = collection->databaseMode != QLatin1String("Hosted");
    request->typeLabels = { { QStringLiteral("image"), tr("Image") }, { QStringLiteral("video"), tr("Video") },
                            { QStringLiteral("audio"), tr("Audio") }, { QStringLiteral("text"),  tr("Text") },
                            { QStringLiteral("other"), tr("Other") } };
    // Translated words only: safe on a worker thread
    request->documentTypeLabel = [](const QString &key) { return documentTypeLabel(key); };

    const bool single = scope == QLatin1String("file");
    options->scope    = scope;
    options->language = QLocale(getCurrentLanguage()).bcp47Name();
    if (single)
        options->title = QFileInfo(path).fileName();
    else if (scope == QLatin1String("folder"))
        options->title = QFileInfo(folder).fileName().isEmpty() ? folder : QFileInfo(folder).fileName();
    else
        options->title = QFileInfo(root).fileName().isEmpty() ? root : QFileInfo(root).fileName();
    options->subtitle = single ? QFileInfo(path).path() : (scope == QLatin1String("folder") ? folder : root);
    options->generatedText = tr("Generated by Teca on %1").arg(QLocale().toString(QDate::currentDate(), QLocale::ShortFormat));
    options->includeFragmentText = true;
    return QString();
}
//----------------------------------------------------------------------
QString AppManager::exportHtml(const QString &scope, int catalogId, const QString &path, const QString &targetFile)
{
    // On this thread: meant for one file, which is quick. A folder or a
    // catalog goes through startHtmlExport.
    MdiHtmlExport::Scope request;
    MdiHtmlExport::Options options;
    QString target;
    const QString refusal = prepareHtmlExport(scope, catalogId, path, targetFile, &request, &options, &target);
    if (!refusal.isEmpty())
        return refusal;
    const QVariantList records = MdiHtmlExport::scopeRecords(m_connectionName, kMdiMainConnection, request);
    const QString html = MdiHtmlExport::build(records, options,
                                              htmlLabels([this](const QString &k) { return proposalKindLabel(k); }));
    QString error;
    if (!MdiHtmlExport::write(target, html, &error))
        return tr("The report could not be saved: %1").arg(error);
    return QString();
}
//----------------------------------------------------------------------
QString AppManager::startHtmlExport(const QString &scope, int catalogId, const QString &path, const QString &targetFile)
{
    // A whole catalog embeds a preview per file: minutes of work, on a worker
    // with its own connections; the AI runner and the UI go on meanwhile
    if (m_htmlExportThread)
        return tr("An HTML export is already running.");
    MdiHtmlExport::Scope request;
    MdiHtmlExport::Options options;
    QString target;
    const QString refusal = prepareHtmlExport(scope, catalogId, path, targetFile, &request, &options, &target);
    if (!refusal.isEmpty())
        return refusal;
    const MdiHtmlExport::Labels labels = htmlLabels([this](const QString &k) { return proposalKindLabel(k); });
    const QString mode     = collection->databaseMode;
    const QString dbFile   = collection->databaseFilePath;
    const QString mdiDb    = mdiDataPath();
    const QString mainConn = m_connectionName;

    m_htmlExportStop = false;
    m_htmlExportThread = QThread::create([this, request, options, labels, target, mode, dbFile, mdiDb, mainConn]() {
        const QString conn    = QStringLiteral("html_export_%1").arg(quintptr(QThread::currentThreadId()));
        const QString mdiConn = conn + QStringLiteral("_mdi");
        QString error;
        bool stopped = false;
        {
            // The file list: the collection's own connection on this thread
            // (Memory reads the catalog's .idx and needs none)
            if (mode == QLatin1String("File")) {
                QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
                db.setDatabaseName(dbFile);
                db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000"));
                db.open();
            } else if (mode == QLatin1String("Hosted")) {
                QSqlDatabase db = QSqlDatabase::cloneDatabase(mainConn, conn);
                db.open();
            }
            if (MdiData::open(mdiConn, mdiDb)) {
                QElapsedTimer shown;
                shown.start();
                const QVariantList records = MdiHtmlExport::scopeRecords(conn, mdiConn, request,
                    [this, &shown, target](int done, int total) {
                        // At most four updates a second reach the UI
                        if (done == 0 || done == total || shown.elapsed() >= 250) {
                            shown.restart();
                            QMetaObject::invokeMethod(this, [this, target, done, total]() {
                                setHtmlExportProgress(target, done, total);
                            }, Qt::QueuedConnection);
                        }
                        return !m_htmlExportStop.load();
                    }, &stopped);
                MdiData::close(mdiConn);
                if (!stopped && !m_htmlExportStop.load()) {
                    const QString html = MdiHtmlExport::build(records, options, labels);
                    if (m_htmlExportStop.load())
                        stopped = true;
                    else
                        MdiHtmlExport::write(target, html, &error);
                } else {
                    stopped = true;
                }
            } else {
                error = mdiDb;
            }
        }
        if (QSqlDatabase::contains(conn))
            QSqlDatabase::removeDatabase(conn);
        QMetaObject::invokeMethod(this, [this, target, error, stopped]() {
            finishHtmlExport(target, error, stopped);
        }, Qt::QueuedConnection);
    });
    connect(qApp, &QCoreApplication::aboutToQuit, m_htmlExportThread, [this]() {
        m_htmlExportStop = true;
        if (m_htmlExportThread)
            m_htmlExportThread->wait();
    });
    connect(m_htmlExportThread, &QThread::finished, this, [this]() {
        m_htmlExportThread->deleteLater();
        m_htmlExportThread = nullptr;
        emit htmlExportStateChanged();
    });
    m_htmlExportStatusText = StatusBarMessageBuilder().setOperation(tr("Export")).setStatus(tr("In Progress"))
                                                      .setCurrentItem(target).build();
    m_htmlExportThread->start(QThread::LowPriority);
    emit htmlExportStateChanged();
    return QString();
}
//----------------------------------------------------------------------
void AppManager::stopHtmlExport()
{
    m_htmlExportStop = true;
}
//----------------------------------------------------------------------
void AppManager::setHtmlExportProgress(const QString &target, int done, int total)
{
    if (!m_htmlExportThread)
        return;
    m_htmlExportStatusText = StatusBarMessageBuilder().setOperation(tr("Export"))
                                 .setStatus(m_htmlExportStop.load() ? tr("Cancelled") : tr("In Progress"))
                                 .setProcess(tr("Files"), done, total).setCurrentItem(target).build();
    emit htmlExportStateChanged();
}
//----------------------------------------------------------------------
void AppManager::finishHtmlExport(const QString &target, const QString &error, bool stopped)
{
    StatusBarMessageBuilder builder;
    builder.setOperation(tr("Export"));
    QString message;
    if (stopped) {
        builder.setStatus(tr("Cancelled"));
    } else if (!error.isEmpty()) {
        message = tr("The report could not be saved: %1").arg(error);
        builder.setStatus(tr("Error")).setCurrentItem(message);
    } else {
        builder.setStatus(tr("Completed")).setCurrentItem(target);
    }
    m_htmlExportStatusText = builder.build();
    emit htmlExportStateChanged();
    emit htmlExportFinished(target, message, stopped);
}
//----------------------------------------------------------------------
QString AppManager::saveArchiveCopy(const QString &destination)
{
    // Titles, events, people and approved faces live only in mdi_data.db: a
    // dated copy of it, of the collection and of its settings, on a worker
    // with its own connections; a running AI job keeps running (VACUUM INTO
    // reads one snapshot of a database in WAL mode)
    if (m_archiveCopyThread)
        return tr("A copy of the archive data is already being saved.");
    const QString dest = QUrl(destination).isLocalFile() ? QUrl(destination).toLocalFile() : destination;
    MdiArchiveCopy::Sources sources;
    sources.collectionFolder = collection->folder;
    sources.databaseMode     = collection->databaseMode;
    sources.databaseFilePath = collection->databaseFilePath;
    sources.settingsFilePath = collection->settingsFilePath;
    sources.withHistory      = CatalogHistory::isAvailable(collection->databaseMode, collection->folder);
    const QDateTime when = QDateTime::currentDateTime();
    // The settings file is shared by every collection: the date goes under
    // the key of the collection copied, even if another one is opened meanwhile
    const QString lastCopyKey  = collectionSettingsKey(QStringLiteral("MediaIntelligence/LastArchiveCopy"));
    const QString settingsFile = collection->settingsFilePath;
    const QString operation    = tr("Copy of the archive data");

    m_archiveCopyThread = QThread::create([this, sources, dest, when, lastCopyKey, settingsFile, operation]() {
        const MdiArchiveCopy::Result result = MdiArchiveCopy::run(sources, dest, when,
            [this, operation](int done, int total, const QString &item) {
                QMetaObject::invokeMethod(this, [this, operation, done, total, item]() {
                    m_archiveCopyStatusText = StatusBarMessageBuilder().setOperation(operation).setStatus(tr("In Progress"))
                                                  .setProcess(tr("Files"), done, total).setCurrentItem(item).build();
                    emit archiveCopyStateChanged();
                }, Qt::QueuedConnection);
                return true;
            });
        QMetaObject::invokeMethod(this, [this, result, when, lastCopyKey, settingsFile, operation]() {
            StatusBarMessageBuilder builder;
            builder.setOperation(operation);
            QString message;
            if (result.ok()) {
                QSettings settings(settingsFile, QSettings::IniFormat);
                settings.setValue(lastCopyKey, when.toString(Qt::ISODate));
                settings.sync();
                // Hosted: the database is not in it; Home says so beside the button
                message = tr("Copy saved in %1.").arg(result.folder);
                builder.setStatus(tr("Completed")).setCurrentItem(result.folder);
            } else {
                switch (result.error) {
                case MdiArchiveCopy::Error::NoDestination:
                    message = tr("The copy needs a folder that exists.");
                    break;
                case MdiArchiveCopy::Error::InsideCollection:
                    message = tr("The copy needs a folder outside the collection folder.");
                    break;
                case MdiArchiveCopy::Error::NotEnoughSpace:
                    message = tr("The copy needs %1 of free space at the destination, which has %2 left.")
                                  .arg(QLocale().formattedDataSize(result.needed),
                                       QLocale().formattedDataSize(result.available));
                    break;
                default:
                    message = tr("The copy could not be saved: %1").arg(result.detail);
                    qWarning() << "Archive copy failed:" << result.detail;
                    break;
                }
                builder.setStatus(tr("Error")).setCurrentItem(message);
            }
            m_archiveCopyStatusText = builder.build();
            emit archiveCopyStateChanged();
            emit archiveCopyFinished(result.ok(), message);
        }, Qt::QueuedConnection);
    });
    // Never cut half-way by quitting: the copy is waited for
    connect(qApp, &QCoreApplication::aboutToQuit, m_archiveCopyThread, [this]() {
        if (m_archiveCopyThread)
            m_archiveCopyThread->wait();
    });
    connect(m_archiveCopyThread, &QThread::finished, this, [this]() {
        m_archiveCopyThread->deleteLater();
        m_archiveCopyThread = nullptr;
        emit archiveCopyStateChanged();
    });
    m_archiveCopyStatusText = StatusBarMessageBuilder().setOperation(operation).setStatus(tr("In Progress"))
                                                       .setCurrentItem(dest).build();
    m_archiveCopyThread->start(QThread::LowPriority);
    emit archiveCopyStateChanged();
    return QString();
}
//----------------------------------------------------------------------
QVariantMap AppManager::archiveCopyState()
{
    // The date is kept under the key of the collection it belongs to
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    const QDateTime last = QDateTime::fromString(
        settings.value(collectionSettingsKey(QStringLiteral("MediaIntelligence/LastArchiveCopy"))).toString(), Qt::ISODate);
    const int days = last.isValid() ? int(last.daysTo(QDateTime::currentDateTime())) : -1;
    return QVariantMap{
        { QStringLiteral("lastCopy"),     last.isValid() ? last.toString(Qt::ISODate) : QString() },
        { QStringLiteral("lastCopyText"), last.isValid() ? QLocale().toString(last, QLocale::ShortFormat) : QString() },
        { QStringLiteral("days"),         days },
        { QStringLiteral("due"),          !last.isValid() || days > 30 },
        { QStringLiteral("hosted"),       collection->databaseMode == QLatin1String("Hosted") } };
}

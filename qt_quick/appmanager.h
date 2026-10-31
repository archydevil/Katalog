#ifndef APPMANAGER_H
#define APPMANAGER_H

#include <QObject>
#include <atomic>
#include <QMutex>
#include <QDebug>

//QtCore
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QStringList>
#include <QSet>
#include <QStringListModel>
#include <QTranslator>
#include <QDateTime>
#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QStorageInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <QSaveFile>
#include <QSettings>
#include <QFont>
#include <QFontInfo>
#include <QGuiApplication>
//QtGui
#include <QFileSystemModel>
#include <QClipboard>
#include <QStandardItemModel>
#include <QDesktopServices>
#include <QCloseEvent>
#include <QPixmap>
//QtSql
#include <QtSql>

//#include<QtSql/QSqlError>
//#include<QtSql/QSqlDatabase>
//#include<QtSql/QSqlQuery>
#include<QFile>

//QtMultimedia
//#include <QMediaPlayer>
//#include <QMediaMetaData>
//QtNetwork
#include <QNetworkAccessManager>
#include <QNetworkReply>
//QtCharts
//#include <QDateTimeAxis>
//#include <QtCharts/QBarSeries>
//#include <QtCharts/QBarSet>
//#include <QtCharts/QLineSeries>
//#include <QtCharts/QBarCategoryAxis>
//#include <QtCharts/QValueAxis>
//#include <QScatterSeries>
//#include <QtCharts/QLegendMarker>

//KF6
#include <KArchive>

//Katalog object classes
#include "core/collection.h"
#include "core/device.h"
#include "core/tag.h"
#include "core/deviceupdatemanager.h"
#include "core/catalogprogressmanager.h"
#include "core/collectionimporter.h"
#include "core/statistics.h"
#include "core/backupmappingmanager.h"
#include "core/catalogdifferenceengine.h"
#include "adapters/search.h"
#include "adapters/devicelistmodel.h"
#include "adapters/explorefilesmodel.h"
#include "adapters/devicetablemodel.h"
#include "adapters/backuppreviewmodel.h"
#include "filesview.h"
#include <QElapsedTimer>

class BackupJobStoppable;
#include "core/mdienrichmentjob.h"   // Config is passed by value
#include "core/mdidata.h"           // PendingJob is held by value
#include "core/mdihtmlexport.h"     // Scope and Options are prepared by value
#include "core/cataloghistory.h"    // Entry and Error in the history helpers
#include <QPointer>
class QThread;

class MdiSystemMonitor;
class CollectionLock;   // core/collectionaccess.h (D43)
class QTimer;
class AppManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(DeviceListModel* deviceListModel READ getDeviceListModel NOTIFY deviceListModelChanged)
    Q_PROPERTY(qreal systemTextPointSize READ getSystemTextPointSize CONSTANT)
    // Current text size (system size × setting). QML reads this rather than
    // Kirigami.Theme.defaultFont, which on Windows/macOS is a startup snapshot
    // and does not follow the application font.
    Q_PROPERTY(qreal textPointSize READ getTextPointSize NOTIFY textPointSizeChanged)
    Q_PROPERTY(QSortFilterProxyModel* deviceFilterModel  READ getDeviceFilterModel  CONSTANT)
    Q_PROPERTY(QAbstractItemModel*    searchSortModel   READ getSearchSortModel    CONSTANT)
    Q_PROPERTY(QAbstractItemModel*    exploreSortModel  READ getExploreSortModel   CONSTANT)
    Q_PROPERTY(QAbstractItemModel*    backupPreviewModel READ getBackupPreviewModel CONSTANT)
    // Per-file report of the last backup run (SpecBackup.md BKP-F29)
    Q_PROPERTY(bool backupReportAvailable READ backupReportAvailable NOTIFY backupReportChanged)
    Q_PROPERTY(QAbstractItemModel*    deviceTableModel  READ getDeviceTableModel   CONSTANT)
    Q_PROPERTY(int     selectedDeviceId   READ getSelectedDeviceId   NOTIFY selectedDeviceChanged)
    Q_PROPERTY(QString selectedDeviceName READ getSelectedDeviceName NOTIFY selectedDeviceChanged)
    Q_PROPERTY(QString selectedDeviceType READ getSelectedDeviceType NOTIFY selectedDeviceChanged)
    Q_PROPERTY(QString selectedDevicePath READ getSelectedDevicePath NOTIFY selectedDeviceChanged)
    Q_PROPERTY(bool    selectedDeviceIsActive READ getSelectedDeviceIsActive NOTIFY selectedDeviceChanged)
    Q_PROPERTY(QString databaseMode           READ getDatabaseMode           NOTIFY databaseModeChanged)
    Q_PROPERTY(QString appReleaseDate         READ getAppReleaseDate         CONSTANT)
    Q_PROPERTY(QString databaseSchemaVersion  READ getDatabaseSchemaVersion  NOTIFY databaseModeChanged FINAL)
    Q_PROPERTY(QString imageFolderPath        READ getImageFolderPath        WRITE setImageFolderPath   NOTIFY imageFolderPathChanged)
    Q_PROPERTY(bool canExpandDevices READ canExpandDevices NOTIFY deviceExpandLevelChanged)
    Q_PROPERTY(bool canCollapseDevices READ canCollapseDevices NOTIFY deviceExpandLevelChanged)
    Q_PROPERTY(bool showDeviceInfo READ getShowDeviceInfo WRITE setShowDeviceInfo NOTIFY showDeviceInfoChanged)
    Q_PROPERTY(QVariantList operationQueue READ getOperationQueue NOTIFY operationQueueChanged)
    Q_PROPERTY(int operationQueueWaitingCount READ getOperationQueueWaitingCount NOTIFY operationQueueChanged)
    Q_PROPERTY(bool refreshDeviceStatusOnActivation READ getRefreshDeviceStatusOnActivation WRITE setRefreshDeviceStatusOnActivation NOTIFY refreshDeviceStatusOnActivationChanged)
    Q_PROPERTY(bool showSelectionPage READ getShowSelectionPage WRITE setShowSelectionPage NOTIFY showSelectionPageChanged)
    Q_PROPERTY(bool deviceFilterFromSelection READ getDeviceFilterFromSelection WRITE setDeviceFilterFromSelection NOTIFY deviceFilterFromSelectionChanged)
    // Devices page display mode and its Full Table option (SpecDevicesPage
    // DVP-F4). DisplayFullDeviceTable is the key K2 already writes, so a
    // collection carries one shared value between the two versions.
    Q_PROPERTY(bool deviceDisplayAsTable READ getDeviceDisplayAsTable WRITE setDeviceDisplayAsTable NOTIFY deviceDisplayAsTableChanged)
    Q_PROPERTY(bool deviceDisplayFullTable READ getDeviceDisplayFullTable WRITE setDeviceDisplayFullTable NOTIFY deviceDisplayFullTableChanged)
    // Which of the three Devices views is shown (SpecDevicesPage DVP-F7).
    // Stored under K2's own Devices/DisplayContents key, so the choice follows a
    // collection between the two versions.
    Q_PROPERTY(QString deviceDisplayContents READ getDeviceDisplayContents WRITE setDeviceDisplayContents NOTIFY deviceDisplayContentsChanged)
    Q_PROPERTY(bool searchKeepsSelection READ getSearchKeepsSelection WRITE setSearchKeepsSelection NOTIFY searchKeepsSelectionChanged)
    Q_PROPERTY(bool checkVersionChoice READ getCheckVersionChoice WRITE setCheckVersionChoice NOTIFY checkVersionChoiceChanged)
    Q_PROPERTY(bool fileSortCaseSensitive READ getFileSortCaseSensitive WRITE setFileSortCaseSensitive NOTIFY fileSortCaseSensitiveChanged)
    // Permanent file deletion is off until the user asks for it. Gates the
    // Delete actions on Search Results and Explore; Move to Trash is not gated,
    // being recoverable.
    Q_PROPERTY(bool allowFileDeletion READ getAllowFileDeletion WRITE setAllowFileDeletion NOTIFY allowFileDeletionChanged)
    // 0 = Desktop Theme, 1 = Katalog Colors, 2 = Desktop Theme (gray). Same
    // Settings/Theme key and numbering as K2 for 0 and 1, so a user's existing
    // choice carries over untouched; 2 is K3-only and K2 will coerce it to 1.
    Q_PROPERTY(int  themeId READ getThemeId WRITE setThemeId NOTIFY themeIdChanged)
    Q_PROPERTY(bool katalogTheme READ getKatalogTheme NOTIFY themeIdChanged)
    // K2's own "Use bigger icon size" choice, on K2's own key, so the two
    // versions share one preference (SpecTheme.md THM-F7 / THM-C8).
    Q_PROPERTY(bool biggerIconSize READ getBiggerIconSize WRITE setBiggerIconSize NOTIFY biggerIconSizeChanged)
    Q_PROPERTY(QVariantList recentCollections READ getRecentCollections NOTIFY recentCollectionsChanged)
    Q_PROPERTY(QString currentCollectionDisplayName READ getCurrentCollectionDisplayName NOTIFY recentCollectionsChanged)
    Q_PROPERTY(QString currentCollectionIconName    READ getCurrentCollectionIconName    NOTIFY recentCollectionsChanged)
    Q_PROPERTY(bool    searchIsRunning   READ getSearchIsRunning   NOTIFY searchStateChanged)
    // Memory-mode Explore load, reported in the activity panel (EXP-F28, OPQ-F19)
    Q_PROPERTY(bool    exploreLoadIsRunning READ getExploreLoadIsRunning NOTIFY exploreLoadStateChanged)
    Q_PROPERTY(QString exploreStatusText    READ getExploreStatusText    NOTIFY exploreStatusTextChanged)
    // Enrichment runner (fork, SpecMediaIntelligence.md MDI-F9..F17)
    Q_PROPERTY(bool    mdiIsRunning  READ getMdiIsRunning  NOTIFY mdiStateChanged)
    Q_PROPERTY(bool    mdiIsPaused   READ getMdiIsPaused   NOTIFY mdiStateChanged)
    Q_PROPERTY(QString mdiStatusText READ getMdiStatusText NOTIFY mdiStatusTextChanged)
    Q_PROPERTY(int     mdiRevision   READ getMdiRevision   NOTIFY mdiRevisionChanged)
    // Web lookups of the dating running on their worker (fork, 2026-10-30)
    Q_PROPERTY(bool    webQueriesRunning READ getWebQueriesRunning NOTIFY webQueriesRunningChanged)
    // HTML export of a folder or a catalog, and the copy of the archive data,
    // each on its own worker (fork, 2026-10-30)
    Q_PROPERTY(bool    htmlExportRunning    READ getHtmlExportRunning    NOTIFY htmlExportStateChanged)
    Q_PROPERTY(QString htmlExportStatusText READ getHtmlExportStatusText NOTIFY htmlExportStateChanged)
    Q_PROPERTY(bool    archiveCopyRunning    READ getArchiveCopyRunning    NOTIFY archiveCopyStateChanged)
    Q_PROPERTY(QString archiveCopyStatusText READ getArchiveCopyStatusText NOTIFY archiveCopyStateChanged)
    // Activity page (fork, MDI-F61..F75): resources, current step, log, session history
    Q_PROPERTY(QVariantMap  mdiSystem  READ getMdiSystem  NOTIFY mdiSystemChanged)
    Q_PROPERTY(QVariantList mdiHistory READ getMdiHistory NOTIFY mdiSystemChanged)
    Q_PROPERTY(QVariantMap  mdiStep    READ getMdiStep    NOTIFY mdiStepChanged)
    Q_PROPERTY(QVariantMap  mdiJobInfo READ getMdiJobInfo NOTIFY mdiStepChanged)
    Q_PROPERTY(QVariantList mdiStepLog READ getMdiStepLog NOTIFY mdiStepLogChanged)
    // Knowledge-base indexing (fork, D33)
    Q_PROPERTY(bool    knowledgeIsRunning  READ getKnowledgeIsRunning  NOTIFY knowledgeStateChanged)
    Q_PROPERTY(QString knowledgeStatusText READ getKnowledgeStatusText NOTIFY knowledgeStateChanged)
    // Search by meaning (fork, MDI-F47..F51): its own result list, beside the table
    Q_PROPERTY(QVariantList semanticResults READ getSemanticResults NOTIFY semanticResultsChanged)
    Q_PROPERTY(bool         semanticActive  READ getSemanticActive  NOTIFY semanticResultsChanged)
    Q_PROPERTY(bool         semanticOnly    READ getSemanticOnly    NOTIFY semanticResultsChanged)
    // Search "In the information" (fork, MDI-F183): its own list too, filled
    // by a worker thread; infoSearchRunning until the list arrives
    Q_PROPERTY(QVariantList infoResults       READ getInfoResults       NOTIFY infoResultsChanged)
    Q_PROPERTY(bool         infoActive        READ getInfoActive        NOTIFY infoResultsChanged)
    Q_PROPERTY(bool         infoOnly          READ getInfoOnly          NOTIFY infoResultsChanged)
    Q_PROPERTY(bool         infoSearchRunning READ getInfoSearchRunning NOTIFY infoResultsChanged)
    Q_PROPERTY(bool    searchIsPaused    READ getSearchIsPaused    NOTIFY searchStateChanged)
    Q_PROPERTY(QString searchStatusText  READ getSearchStatusText  NOTIFY searchStatusTextChanged)
    // Search results footer filters (SpecSearchResultsFilters.md). Both narrow
    // the rows already displayed; neither runs a search.
    Q_PROPERTY(QString searchNameFilter    READ getSearchNameFilter    WRITE setSearchNameFilter    NOTIFY searchFiltersChanged)
    Q_PROPERTY(QVariantList searchCatalogFilters READ getSearchCatalogFilters NOTIFY searchFiltersChanged)
    Q_PROPERTY(QVariantList searchCatalogsFound READ getSearchCatalogsFound NOTIFY searchCatalogsFoundChanged)
    Q_PROPERTY(bool    catalogIsCreating READ getCatalogIsCreating NOTIFY catalogIsCreatingChanged)
    Q_PROPERTY(QString catalogStatusText READ getCatalogStatusText NOTIFY catalogStatusTextChanged)
    Q_PROPERTY(bool    importIsRunning   READ getImportIsRunning   NOTIFY importIsRunningChanged)
    Q_PROPERTY(QString importStatusText  READ getImportStatusText  NOTIFY importStatusTextChanged)
    Q_PROPERTY(QString importReportText  READ getImportReportText  NOTIFY importReportTextChanged)
    // Collection export (SpecCollectionExport.md)
    Q_PROPERTY(bool    exportIsRunning   READ getExportIsRunning   NOTIFY exportStateChanged)
    Q_PROPERTY(QString exportStatusText  READ getExportStatusText  NOTIFY exportStateChanged)
    Q_PROPERTY(DeviceListModel* importSourceDeviceModel READ getImportSourceDeviceModel NOTIFY importSourceChanged)
    Q_PROPERTY(bool updateBeforeBackup READ updateBeforeBackup WRITE setUpdateBeforeBackup NOTIFY updateBeforeBackupChanged)
    Q_PROPERTY(bool catalogUpdateForBackupRunning READ catalogUpdateForBackupRunning NOTIFY catalogUpdateForBackupRunningChanged)
    Q_PROPERTY(QString catalogUpdateForBackupStatusText READ getCatalogUpdateForBackupStatusText NOTIFY catalogUpdateForBackupStatusChanged)
    Q_PROPERTY(bool backupPreviewRunning READ backupPreviewRunning NOTIFY backupPreviewRunningChanged)
    Q_PROPERTY(QString backupPreviewStatusText READ getBackupPreviewStatusText NOTIFY backupPreviewStatusChanged)
    Q_PROPERTY(int runningBackupMappingId READ runningBackupMappingId NOTIFY runningBackupMappingIdChanged)
    Q_PROPERTY(bool    deviceUpdateIsRunning  READ getDeviceUpdateIsRunning  NOTIFY deviceUpdateStateChanged)
    Q_PROPERTY(QString deviceUpdateStatusText READ getDeviceUpdateStatusText NOTIFY deviceUpdateStatusChanged)
    Q_PROPERTY(bool    isFirstRun             READ isFirstRun                NOTIFY firstRunChanged)
    // Read-only collection (fork, D43): open in another process, or this
    // computer is not among its writers. Every write is refused (readOnlyRefusal).
    Q_PROPERTY(bool    readOnly              READ getReadOnly              NOTIFY readOnlyChanged)
    Q_PROPERTY(QString readOnlyReason        READ getReadOnlyReason        NOTIFY readOnlyChanged)
    Q_PROPERTY(bool    readOnlyNotAuthorised READ getReadOnlyNotAuthorised NOTIFY readOnlyChanged)
    // Computers allowed to change the collection (Settings, D43)
    Q_PROPERTY(bool         writersProtection READ getWritersProtection NOTIFY writersChanged)
    Q_PROPERTY(QVariantList authorisedWriters READ getAuthorisedWriters NOTIFY writersChanged)
    Q_PROPERTY(QString      thisComputerCode  READ getThisComputerCode  CONSTANT)
    Q_PROPERTY(QString      thisComputerName  READ getThisComputerName  CONSTANT)

public:
    explicit AppManager(QObject *parent = nullptr);
    ~AppManager() override;

    //Application version
    QString currentVersion;
    QString releaseDate;
    bool checkVersionChoice;
    bool m_fileSortCaseSensitive = false;
    bool firstRun;
    bool developmentMode;
    int themeID;

    //Objects
    Collection *collection = new Collection();
    Device *selectedDevice = new Device(); //selected device from selection panel, used for operations on any screen
    Device *activeDevice   = new Device(); //active device from any screen, used for operations from that screen
    Device *catalogDevice  = new Device(); //selected catalog/device from Catalog screen
    Device *exploreDevice  = new Device(); //tempory catalog/device to be use in Exploore screen
    SearchSync *searchObject = nullptr;
    DeviceListModel *deviceListModel = nullptr;
    QSortFilterProxyModel *m_deviceFilterModel = nullptr;
    QString lastDatabaseError;
    int m_deviceExpandLevel = -1; // -1=show all; 0..N=show levels 0..N

public slots:
    //Global
    void initiateApp();
    void checkVersion();

    // Database selection methods
    void selectSQLiteDatabase();
    QString getDatabaseFilePath() const;
    void setDatabaseFilePath(const QString &path);
    bool reconnectToDatabase();
    QString getCurrentDatabaseInfo() const;
    void initializeDeviceListModel();
    void refreshAllUI();
    void refreshDeviceList();
    // Rebuilds the device list model and notifies QML, without probing. Split
    // out so the activation refresh can reload only when a status actually
    // changed, instead of resetting the model on every activation.
    void reloadDeviceListModel();
    void refreshSearchResults();
    void refreshStatistics();

    //Database
    QString startDatabase();

    // Collection management
    Q_INVOKABLE QString getDatabaseMode() const;

    // Read-only collection and its writers (fork, D43). The three changes
    // return "" on success, else the reason.
    bool         getReadOnly() const { return !m_readOnlyCauses.isEmpty(); }
    QString      getReadOnlyReason() const;
    bool         getReadOnlyNotAuthorised() const;
    bool         getWritersProtection() const;
    QVariantList getAuthorisedWriters() const;
    QString      getThisComputerCode() const;
    QString      getThisComputerName() const;
    Q_INVOKABLE QString setWritersProtection(bool on);
    Q_INVOKABLE QString addAuthorisedWriter(const QString &code, const QString &name);
    Q_INVOKABLE QString removeAuthorisedWriter(const QString &code);
    Q_INVOKABLE QString getCollectionFolder() const;
    Q_INVOKABLE void openDatabaseFile();
    Q_INVOKABLE void openCollectionMemory(const QString &folder);
    Q_INVOKABLE void openCollectionHosted(const QString &hostName, const QString &dbName,
                                          int port, const QString &userName, const QString &password);
    // "localhost", "private" or "invalid" (SpecHostedSecurity.md HST-F2..F4)
    Q_INVOKABLE QString hostedHostCheck(const QString &hostName) const;
    Q_INVOKABLE void    createNewSQLiteCollection(const QString &path);
    Q_INVOKABLE QString getNewCollectionDefaultPath() const;
    // Default folder for folder pickers: .db folder (File), collection folder
    // (Memory), or user home (Hosted). Reusable across pages.
    Q_INVOKABLE QString getCollectionBrowsePath() const;

    //Useful functions
    void executeSearch();
    void setSearchObject(SearchSync *search);

    DeviceListModel* getDeviceListModel() const { return deviceListModel; }
    QSortFilterProxyModel* getDeviceFilterModel() const { return m_deviceFilterModel; }

    Q_INVOKABLE void setDeviceFilter(const QString &text);
    Q_INVOKABLE void expandDevices();
    Q_INVOKABLE void collapseDevices();
    Q_INVOKABLE void expandDevice(int deviceId);
    Q_INVOKABLE void collapseDevice(int deviceId);
    Q_INVOKABLE void openDeviceFolder(int deviceId);
    Q_INVOKABLE bool canExpandDevices() const;
    Q_INVOKABLE bool canCollapseDevices() const;
    bool getShowDeviceInfo() const;
    void setShowDeviceInfo(bool value);
    bool getRefreshDeviceStatusOnActivation() const;
    void setRefreshDeviceStatusOnActivation(bool value);
    bool getAllowFileDeletion() const;
    void setAllowFileDeletion(bool value);
    int  getThemeId() const;
    void setThemeId(int value);
    // Only Katalog Colors forces the brand palette; both Desktop variants follow
    // the desktop, one from its accent and one from its greys.
    bool getKatalogTheme() const { return getThemeId() == 1; }
    bool getBiggerIconSize() const;
    void setBiggerIconSize(bool value);
    // Called when the application becomes active. Does nothing unless the
    // setting above is on; then skips cheaply when nothing was mounted or
    // unmounted, or when a probe already ran in the last 30 seconds.
    Q_INVOKABLE void refreshDeviceActiveOnActivation();
    bool getShowSelectionPage() const;
    void setShowSelectionPage(bool value);
    bool getDeviceFilterFromSelection() const;
    void setDeviceFilterFromSelection(bool value);
    bool getDeviceDisplayAsTable() const;
    void setDeviceDisplayAsTable(bool value);
    bool getDeviceDisplayFullTable() const;
    void setDeviceDisplayFullTable(bool value);
    QString getDeviceDisplayContents() const;
    void setDeviceDisplayContents(const QString &value);
    bool getSearchKeepsSelection() const;
    void setSearchKeepsSelection(bool value);
    QString getAppReleaseDate() const { return releaseDate; }
    bool getCheckVersionChoice() const;
    void setCheckVersionChoice(bool value);
    bool getFileSortCaseSensitive() const;
    void setFileSortCaseSensitive(bool value);
    QString getSelectedDevicePath() const;
    Q_INVOKABLE void    openSettingsFile();
    Q_INVOKABLE QString getSettingsFilePath() const;
    Q_INVOKABLE QString getDatabaseSchemaVersion();
    Q_INVOKABLE void    setLastPage(const QString &pageName);
    Q_INVOKABLE QString getLastPage() const;
    Q_INVOKABLE QVariantMap getWindowGeometry() const;
    Q_INVOKABLE void        saveWindowGeometry(int x, int y, int width, int height, bool maximized);
    Q_INVOKABLE QVariantMap getDefaultWindowSize(const QString &screenName) const;
    // "1280×800": tooltip of the Reset window size button, not translated (WIN-C5).
    Q_INVOKABLE QString     getDefaultWindowSizeText() const
    { return QString::number(kDefaultWindowWidth) + QChar(0x00D7) + QString::number(kDefaultWindowHeight); }
    Q_INVOKABLE void        setTextScale(qreal scale);
    qreal                   getSystemTextPointSize() const { return QFontInfo(m_systemFont).pointSizeF(); }
    qreal                   getTextPointSize() const { return QFontInfo(QGuiApplication::font()).pointSizeF(); }

    void selectDeviceById(int deviceId);
    QString getSelectedDeviceName() const;
    int     getSelectedDeviceId()   const;
    QString getSelectedDeviceType() const;
    bool    getSelectedDeviceIsActive() const;

    Q_INVOKABLE QStringList  getTagNames() const;

    // Statistics
    Q_INVOKABLE QVariantMap getStatisticsData(const QString &source, const QString &dataType, const QString &startDate) const;
    Q_INVOKABLE QString     getStatisticsSetting(const QString &key) const;
    Q_INVOKABLE int         getColumnWidthSetting(const QString &key) const;
    Q_INVOKABLE void        setColumnWidthSetting(const QString &key, int width);
    // Explore tree / file list division, in K2's splitter keys (EXP-F34/C27)
    Q_INVOKABLE QVariantList getExploreSplitterWidths() const;
    Q_INVOKABLE void         setExploreSplitterSizes(int treeWidth, int treeHeight, int filesWidth, int filesHeight);
    Q_INVOKABLE void        setStatisticsSetting(const QString &key, const QVariant &value);

    // Backup
    Q_INVOKABLE QVariantList getBackupMappings(const QString &filterType = "None", int deviceId = -1, const QString &mappingType = "All") const;
    Q_INVOKABLE QVariantMap  getBackupTotals(const QString &filterType = "None", int deviceId = -1, const QString &mappingType = "All") const;
    Q_INVOKABLE QString      createBackupMapping(const QString &name, const QString &type, int sourceId, int targetId, bool strictCopy, const QString &conflictMode, bool sourceDrive, bool includeEmptyDirs);
    Q_INVOKABLE QString      updateBackupMapping(int mappingId, const QString &name, const QString &type, int sourceId, int targetId, bool strictCopy, const QString &conflictMode, bool sourceDrive, bool includeEmptyDirs);
    Q_INVOKABLE QString      duplicateBackupMapping(int mappingId);
    Q_INVOKABLE bool         deleteBackupMapping(int mappingId);
    Q_INVOKABLE bool         invertBackupMapping(int mappingId);
    Q_INVOKABLE void         startBackupPreview(int mappingId);
    // A link card's "Export to CSV": computes that link's preview, then exports it
    // instead of opening the Preview page (BKP-F26)
    Q_INVOKABLE void         exportBackupLinkToCsv(int mappingId);
    Q_INVOKABLE void         stopBackupPreview();
    Q_INVOKABLE QVariantMap  lastBackupPreviewSummary() const { return m_lastPreviewSummary; }
    bool                     backupReportAvailable() const { return !m_lastReportSummary.isEmpty(); }
    // Fills the preview table with the last run's rows; returns its totals line (BKP-F29)
    Q_INVOKABLE QString      showLastBackupReport();
    Q_INVOKABLE void         runBackup(int mappingId);
    // Run several links one after another (SpecBackup.md BKP-F18). The eligible ids are
    // chosen on the page, from the links currently listed under its filters.
    Q_INVOKABLE void         runListedBackups(const QVariantList &mappingIds);
    Q_INVOKABLE void         stopBackup();
    Q_INVOKABLE void         pauseBackup();
    Q_INVOKABLE void         resumeBackup();
    Q_INVOKABLE QVariantMap  replicateDirectories(int mappingId);
    Q_INVOKABLE QString      exportLastBackupPreviewToCsv();
    Q_INVOKABLE QString      generateLuckyBackupProfile(const QVariantList &mappingIds);
    Q_INVOKABLE QString      getBackupSetting(const QString &key) const;
    Q_INVOKABLE void         setBackupSetting(const QString &key, const QVariant &value);

    // Catalog-update-before-backup
    bool updateBeforeBackup() const;
    void setUpdateBeforeBackup(bool v);
    bool catalogUpdateForBackupRunning() const { return m_catalogUpdateForBackupRunning; }
    QString getCatalogUpdateForBackupStatusText() const { return m_catalogUpdateForBackupStatusText; }
    bool backupPreviewRunning() const { return m_backupPreviewRunning; }
    int runningBackupMappingId() const { return m_runningBackupMappingId; }
    QString getBackupPreviewStatusText() const { return m_backupPreviewStatusText; }
    Q_INVOKABLE void prepareCatalogsForMapping(int mappingId);

    Q_INVOKABLE QVariantList getTagEntries(const QString &filterName = QString()) const;
    Q_INVOKABLE bool         createTag(const QString &name, const QString &path);
    Q_INVOKABLE bool         deleteTag(int tagID);

    // Search progress
    bool    getSearchIsRunning()  const { return m_searchIsRunning; }
    bool    getSearchIsPaused()   const { return m_searchIsPaused; }
    QString getSearchStatusText() const { return m_searchStatusText; }
    void onSearchProgress(int filesProcessed);
    Q_INVOKABLE void stopSearch();
    bool    getExploreLoadIsRunning() const { return m_exploreLoadIsRunning; }
    QString getExploreStatusText()    const { return m_exploreStatusText; }
    Q_INVOKABLE void stopExploreLoad();
    Q_INVOKABLE void pauseSearch();
    Q_INVOKABLE void resumeSearch();

    // Catalog creation progress
    bool    getCatalogIsCreating()  const { return m_catalogIsCreating; }
    QString getCatalogStatusText()  const { return m_catalogStatusText; }

    // Import progress
    bool    getImportIsRunning()  const { return m_importIsRunning; }
    QString getImportStatusText() const { return m_importStatusText; }
    QString getImportReportText() const { return m_importReportText; }

    // Collection export (SpecCollectionExport.md)
    bool    getExportIsRunning()  const { return m_exportIsRunning; }
    QString getExportStatusText() const { return m_exportStatusText; }
    Q_INVOKABLE void exportToSQLiteFile(const QString &filePath);
    Q_INVOKABLE void exportToMemoryMode(const QString &folderPath);
    Q_INVOKABLE void cancelExport() { m_exportCancelRequested = true; }

    // Catalog creation
    Q_INVOKABLE QString      createCatalog(const QString &name, const QString &path,
                                           int storageId,
                                           const QString &fileType, bool includeSubDir,
                                           bool includeHidden, bool includeSymlinks,
                                           bool isFullDevice,
                                           const QString &includeMetadata,
                                           const QString &includeChecksum,
                                           const QStringList &perCatalogExcludes);
    Q_INVOKABLE void         stopCatalogCreation();
    Q_INVOKABLE bool         isDirectoryEmpty(const QString &path) const;

    // Create wizard (fork, SpecMediaIntelligence.md MDI-F1..F8). Same creation as
    // createCatalog(); the profile {destinationPath, settings} is saved for the
    // new catalog only when its creation completes successfully (MDI-F7).
    Q_INVOKABLE QString      createCatalogWithEnrichment(const QString &name, const QString &path,
                                                         int storageId,
                                                         const QString &fileType, bool includeSubDir,
                                                         bool includeHidden, bool includeSymlinks,
                                                         bool isFullDevice,
                                                         const QString &includeMetadata,
                                                         const QString &includeChecksum,
                                                         const QStringList &perCatalogExcludes,
                                                         const QVariantMap &enrichmentProfile);
    Q_INVOKABLE bool         isPathWritable(const QString &path) const;
    Q_INVOKABLE bool         isPathInside(const QString &path, const QString &parentPath) const;
    Q_INVOKABLE QString      getCreateSetting(const QString &key) const;
    Q_INVOKABLE void         setCreateSetting(const QString &key, const QVariant &value);

    // Enrichment (fork, SpecMediaIntelligence.md). deviceId is the catalog's device.
    bool    getMdiIsRunning()  const { return m_mdiJob != nullptr; }
    bool    getMdiIsPaused()   const { return m_mdiIsPaused; }
    QString getMdiStatusText() const { return m_mdiStatusText; }
    int     getMdiRevision()   const { return m_mdiRevision; }
    Q_INVOKABLE QVariantMap  getEnrichmentProfile(int deviceId);
    Q_INVOKABLE bool         saveEnrichmentProfile(int deviceId, const QVariantMap &profile);
    // resumed: a restored or waiting job, which waits for a destination drive
    // that is away instead of being refused (D31)
    Q_INVOKABLE QString      startEnrichment(int deviceId, bool resumed = false);
    Q_INVOKABLE void         stopEnrichment();
    // Classification only, on a folder and a file type, from the results
    // already there (D33, MDI-F103); empty folder / type = the whole catalog
    Q_INVOKABLE QString      startReinterpretation(int deviceId, const QString &folder, const QString &fileType);
    // {files, seconds}: what a re-interpretation would cover and take
    Q_INVOKABLE QVariantMap  reinterpretEstimate(int deviceId, const QString &folder, const QString &fileType);

    // Domain context (fork, D33): the collection's, and notes per catalog
    Q_INVOKABLE QVariantMap  getContext();
    Q_INVOKABLE bool         saveContext(const QVariantMap &context);
    Q_INVOKABLE QString      getCatalogNotes(int deviceId);
    Q_INVOKABLE bool         saveCatalogNotes(int deviceId, const QString &notes);
    Q_INVOKABLE QVariantList contextCatalogs();            // [{deviceId, name, path}]
    // Home page (fork, D34): drives with their state and contents, totals, the
    // archive's context and knowledge. Heavy counts are cached.
    Q_INVOKABLE QVariantMap  homeOverview();
    // Draft of a context from a catalog's folders and results, by the local
    // text model on a worker thread; the answer comes with contextDraftReady
    Q_INVOKABLE void         draftContext(int deviceId);
    // Knowledge base (MDI-F104..F106): reference documents, indexed in the
    // background on their own thread
    Q_INVOKABLE QVariantList knowledgeDocuments();
    Q_INVOKABLE int          addKnowledgePaths(const QVariantList &urls);
    Q_INVOKABLE bool         removeKnowledgeDocument(int documentId);
    bool    getKnowledgeIsRunning()  const { return m_kbJob != nullptr; }
    QString getKnowledgeStatusText() const { return m_kbStatusText; }
    Q_INVOKABLE void         setEnrichmentPaused(bool paused);
    Q_INVOKABLE QVariantMap  getEnrichmentTools();
    QVariantMap  getMdiSystem()  const { return m_mdiSystem; }
    QVariantList getMdiHistory() const { return m_mdiHistory; }
    QVariantMap  getMdiStep()    const { return m_mdiStep; }
    QVariantMap  getMdiJobInfo() const;
    QVariantList getMdiStepLog() const { return m_mdiStepLog; }
    // The monitor samples while the Activity page is shown or a job runs
    Q_INVOKABLE void         setActivityMonitoring(bool visible);
    // Tools with path and version, for the Activity page
    Q_INVOKABLE QVariantMap  mdiToolsInfo();
    Q_INVOKABLE QString      mdiAnalysisName(const QString &analysis) const { return mdiAnalysisLabel(analysis); }
    // Explore / details: revision is passed only to re-evaluate the bindings
    Q_INVOKABLE QString      mdiPreviewUrl(int catalogId, const QString &fullPath, int revision = 0);
    Q_INVOKABLE bool         mdiHasData(int catalogId, const QString &fullPath, int revision = 0);
    // A description, summary, transcript or classification: what Browse tags "AI"
    Q_INVOKABLE bool         mdiHasAi(int catalogId, const QString &fullPath, int revision = 0);
    Q_INVOKABLE QVariantList mdiDetails(int catalogId, const QString &fullPath);
    Q_INVOKABLE QVariantMap  mdiClusters(int catalogId);
    // Inspector panel (fork, owner's request 2026-10-29): everything known about
    // one file, readable with its disk disconnected
    Q_INVOKABLE QVariantMap  inspectorInfo(int catalogId, const QString &fullPath);
    // The Explore file list's row, for the Inspector (proxy row of exploreSortModel)
    Q_INVOKABLE QVariantMap  exploreRowInfo(int proxyRow);
    // Connected drives, mounted or not, and read-only mounting (fork, 2026-10-29)
    Q_INVOKABLE QVariantList connectedDrives();
    Q_INVOKABLE QVariantMap  mountDriveReadOnly(const QString &device);
    // View choices kept across sessions (fork): Inspector shown, Explore columns
    Q_INVOKABLE bool         getViewSetting(const QString &key) const;
    Q_INVOKABLE void         setViewSetting(const QString &key, bool value);
    QVariantList getSemanticResults() const { return m_semanticResults; }
    bool         getSemanticActive()  const { return m_semanticActive; }
    bool         getSemanticOnly()    const { return m_semanticOnly; }
    // Empty on success, else the reason; onlyMeaning: no other criterion was set
    Q_INVOKABLE QString      runSemanticSearch(const QString &query, bool onlyMeaning);
    Q_INVOKABLE void         clearSemanticResults();
    // "In the information" (fork, MDI-F183): the files whose information or
    // people contain every word, in the catalogs of the choice in My drives,
    // found on a worker thread. Empty when started, else the reason (Hosted).
    QVariantList getInfoResults()       const { return m_infoResults; }
    bool         getInfoActive()        const { return m_infoActive; }
    bool         getInfoOnly()          const { return m_infoOnly; }
    bool         getInfoSearchRunning() const { return m_infoSearchBusy; }
    Q_INVOKABLE QString      runInfoSearch(const QString &text, bool onlyInfo);
    Q_INVOKABLE void         clearInfoResults();
    // Asks the local model whether it answers and has the embedding model,
    // without waiting: localModelChecked follows (MDI-F187)
    Q_INVOKABLE void         checkLocalModel();
    // Home's "To review" (fork, MDI-F184), read only: {faces, proposals,
    // proposalsDeviceId, proposalsFolder} — the catalog with the most
    // proposals waiting and its top folder; all 0 in a Hosted collection
    Q_INVOKABLE QVariantMap  reviewCounts();
    Q_INVOKABLE QString      localFileUrl(const QString &path) const;

    // Transcripts (fork, owner's request 2026-10-30): the stored ones filtered
    // like the new ones, on a worker thread, every catalog of the collection;
    // {started: bool, error: text}; the counts {checked, cleaned, removed,
    // failed} come with transcriptsCleaned. Refused while the enrichment runs.
    Q_INVOKABLE QVariantMap  cleanTranscripts();

    // File information and people (fork, owner's request 2026-10-30).
    // files: [{catalogId, fullPath}]. Strings returned: empty on success, else
    // the reason. File and Memory modes (mdi_data.db); not Hosted.
    Q_INVOKABLE QVariantMap  fileInfo(int catalogId, const QString &fullPath);
    Q_INVOKABLE QString      setFileInfo(const QVariantList &files, const QVariantMap &fields);
    Q_INVOKABLE QVariantList people(const QString &filter);
    Q_INVOKABLE QVariantMap  person(int personId);
    Q_INVOKABLE int          savePerson(const QVariantMap &person);
    Q_INVOKABLE QString      deletePerson(int personId);
    Q_INVOKABLE QString      addPeopleToFiles(const QVariantList &files, const QVariantList &personIds);
    Q_INVOKABLE QString      removePersonFromFile(int personId, int catalogId, const QString &fullPath);
    Q_INVOKABLE QVariantList filesOfPerson(int personId);
    // Files whose information or people contain every word of `text`
    Q_INVOKABLE QVariantList findFilesByInfo(const QString &text, int limit = 500);

    // Faces (fork, OpenCV YuNet + SFace, local). Never confirmed by the app:
    // suggestions wait for the owner's approval.
    Q_INVOKABLE bool         facesAvailable();
    Q_INVOKABLE QString      facesUnavailableReason();
    Q_INVOKABLE QVariantList facesInFile(int catalogId, const QString &fullPath);
    Q_INVOKABLE QString      confirmFaces(const QVariantList &faceIds, int personId);
    Q_INVOKABLE QString      rejectFaces(const QVariantList &faceIds);
    // Forgets every face of the collection; people and names added by hand stay
    Q_INVOKABLE QString clearFaceData();
    Q_INVOKABLE QString      ignoreFaces(const QVariantList &faceIds);
    Q_INVOKABLE QString      clearFace(int faceId);
    Q_INVOKABLE QVariantList pendingFaces(int limit);
    // Similar faces and the groups of unnamed faces compare many faces: they
    // are computed on a worker with its own mdi_data.db connection and kept
    // until faces or people change, a job writes, or the collection changes.
    // The answer comes with similarFacesReady / faceGroupsReady, at once when
    // kept; `serial` changes only with a new computation.
    Q_INVOKABLE void         requestSimilarFaces(int personId, int limit, double minScore);
    Q_INVOKABLE void         requestFaceGroups(int limit);
    // Queues the runner with the "faces" analysis only, on the catalog's images
    Q_INVOKABLE QString      startFaceAnalysis(int deviceId);

    // Dating, document study, web lookups and HTML export (fork, owner's
    // decisions of 2026-10-30). Inferred information is only proposed: the
    // owner approves it per file, per folder or in bulk; nothing is written
    // into a file's information by itself.
    // [{proposalId, field, fieldLabel, value, precision, confidence, status,
    //   ownValue, sources: [{kind, kindLabel, label, detail, url}]}]
    Q_INVOKABLE QVariantList proposalsForFile(int catalogId, const QString &fullPath);
    // Pending ones of a folder, grouped: [{field, fieldLabel, value, precision,
    //   confidence, fileCount, proposalIds: [...], sources: [...]}]
    Q_INVOKABLE QVariantList proposalsForFolder(int catalogId, const QString &folderPath, bool recursive);
    // A field the owner already filled keeps its value unless replaceOwnValues;
    // the message then says how many were left waiting
    Q_INVOKABLE QString      acceptProposals(const QVariantList &proposalIds, bool replaceOwnValues = false);
    Q_INVOKABLE QString      rejectProposals(const QVariantList &proposalIds);
    // Queue the runner with the dating analysis (photos, videos) or the
    // document study (documents) only
    Q_INVOKABLE QString      startDating(int deviceId);
    Q_INVOKABLE QString      startDocumentStudy(int deviceId);
    // Web lookups: off by default per collection; the queries are shown
    // before anything is sent ([{query, reason, folder, fileCount}])
    Q_INVOKABLE bool         webSearchEnabled();
    Q_INVOKABLE void         setWebSearchEnabled(bool enabled);
    // A settings key of the collection open: the settings file is shared by
    // every collection ("key/<collection id>")
    QString                  collectionSettingsKey(const QString &key) const;
    Q_INVOKABLE QVariantList pendingWebQueries(int deviceId);
    // Runs on a worker, then proposals are derived again: webQueriesFinished
    // and proposalsChanged follow
    Q_INVOKABLE QString      runWebQueries(int deviceId, const QVariantList &queries);
    bool                     getWebQueriesRunning() const { return m_webThread != nullptr; }
    Q_INVOKABLE QVariantMap  documentStudy(int catalogId, const QString &fullPath);
    // The device of a catalog, -1 when none: actions started from a file
    // (study its drive, show it in Browse) work on the file's own drive
    Q_INVOKABLE int          deviceIdOfCatalog(int catalogId);
    // scope: "file", "folder" or "catalog"; empty or the reason
    Q_INVOKABLE QString      exportHtml(const QString &scope, int catalogId, const QString &path,
                                        const QString &targetFile);
    // The same for a folder or a catalog, on a worker with its own
    // connections: empty when started (htmlExportFinished follows) or the reason
    Q_INVOKABLE QString      startHtmlExport(const QString &scope, int catalogId, const QString &path,
                                             const QString &targetFile);
    Q_INVOKABLE void         stopHtmlExport();
    bool                     getHtmlExportRunning() const { return m_htmlExportThread != nullptr; }
    QString                  getHtmlExportStatusText() const { return m_htmlExportStatusText; }
    // Copy of the archive data (fork, 2026-10-30): a dated folder in
    // destination, on a worker; empty when started (archiveCopyFinished
    // follows) or the reason. State: {lastCopy (ISO), lastCopyText, days
    // (-1 when none), due (none, or older than 30 days), hosted}
    Q_INVOKABLE QString      saveArchiveCopy(const QString &destination);
    Q_INVOKABLE QVariantMap  archiveCopyState();
    bool                     getArchiveCopyRunning() const { return m_archiveCopyThread != nullptr; }
    QString                  getArchiveCopyStatusText() const { return m_archiveCopyStatusText; }

    // Storage pre-selection for Create page (used by DeviceTreeComboBox storageOnly mode)
    Q_INVOKABLE int getDefaultStorageId() const;

    // Device edit
    Q_INVOKABLE QVariantMap  getDeviceDetails(int deviceId) const;
    Q_INVOKABLE QString      saveDeviceBasicFields(int deviceId, const QString &name, int parentId, const QString &path, const QString &comment);
    Q_INVOKABLE QVariantMap  checkCatalogOptionChanges(int deviceId, const QString &fileType, bool includeSubDir,
                                                       bool includeHidden,
                                                       const QString &includeMetadata, const QString &includeChecksum,
                                                       bool isFullDevice) const;
    Q_INVOKABLE QString      saveCatalogOptions(int deviceId, const QString &fileType, bool includeSubDir,
                                                bool includeHidden,
                                                const QString &includeMetadata, const QString &includeChecksum,
                                                bool isFullDevice);
    Q_INVOKABLE QString      saveStorageDetails(int deviceId, const QVariantMap &fields);
    Q_INVOKABLE void         triggerDeviceRescan(int deviceId);
    Q_INVOKABLE void         triggerStoragePathReplace(int deviceId, const QString &previousPath, const QString &newPath);
    Q_INVOKABLE QStringList  getDeviceExcludeFolders(int deviceId) const;
    Q_INVOKABLE bool         addDeviceExcludeFolder(int deviceId, const QString &path);
    Q_INVOKABLE bool         removeDeviceExcludeFolder(int deviceId, const QString &path);

    // Device delete
    Q_INVOKABLE QVariantMap  checkDeviceDeleteAllowed(int deviceId) const;
    Q_INVOKABLE QString      deleteDevice(int deviceId);

    // Devices page operations
    Q_INVOKABLE QVariantList getDeviceList(const QString &viewFilter = "All", int scopeDeviceId = 0) const;
    Q_INVOKABLE int          addDeviceVirtual(int parentId);
    Q_INVOKABLE int          addDeviceStorage(int parentId);
    Q_INVOKABLE void         updateDevice(int deviceId);
    Q_INVOKABLE int          updateAllActiveDevices(bool showEachReport = false);   // catalogs started or queued
    // Operation queue (SpecOperationQueue.md)
    QVariantList             getOperationQueue() const;
    int                      getOperationQueueWaitingCount() const { return m_pendingDeviceUpdates.size(); }
    Q_INVOKABLE void         removeQueuedOperation(int index);
    Q_INVOKABLE void         clearOperationQueue();
    Q_INVOKABLE void         acknowledgeUpdateReport();
    Q_INVOKABLE void         stopDeviceUpdate();
    Q_INVOKABLE void         gentleStopDeviceUpdate();
    Q_INVOKABLE QVariantMap  recordDevicesSnapshot();
    Q_INVOKABLE QString      splitCatalogBySubDirectory(int deviceId);
    Q_INVOKABLE void         splitCatalogByFileType(int deviceId, bool verifyFirst);

    // History and restore points (fork, owner's decision 2026-10-30): before
    // an action changes catalogs, their rows are kept in teca_history.db.
    // Entries newest first: {id, date, dateText, action, title, details,
    // catalogs: [{deviceId, catalogId, name, files}], resultCatalogs: [...],
    // canRestore, undone, kept, restoreProblem}
    Q_INVOKABLE bool         historyAvailable();
    Q_INVOKABLE QVariantList historyEntries();
    Q_INVOKABLE QVariantList historyForDevice(int deviceId);
    Q_INVOKABLE QString      restoreHistoryEntry(int entryId);    // "" or an error
    Q_INVOKABLE QString      openHistoryVersion(int entryId, int catalogId, const QString &nameSuffix);
    // Recompose: {name, path, catalogs: [{deviceId, name, files}], possible, reason}
    Q_INVOKABLE QVariantMap  recomposeProposal(int deviceId);
    Q_INVOKABLE QString      recomposeCatalogs(const QVariantList &deviceIds, const QString &name);
    Q_INVOKABLE QVariantMap  verifyDeviceChecksums(int deviceId);
    // MIME verification of one catalog (SpecMimeVerification.md MIV-F1..F5)
    Q_INVOKABLE void         verifyMimeTypes(int deviceId);
    Q_INVOKABLE QString      unassignDevice(int deviceId, int parentId);
    Q_INVOKABLE QString      assignCatalogToDevice(int catalogDeviceId, int virtualDeviceId);
    Q_INVOKABLE void         launchFilelight(int deviceId);
    Q_INVOKABLE QString      importFromVVV(const QString &path);
    bool    getDeviceUpdateIsRunning()  const { return m_deviceUpdateIsRunning; }
    QString getDeviceUpdateStatusText() const { return m_deviceUpdateStatusText; }

    // Explore page
    // Opens a catalog in Explore. Returns the open's result, or one of:
    // refused (another operation runs, EXP-F32), deferred (a load runs; the
    // request is kept as the pending one, EXP-F31), loading (a Memory-mode load
    // has started; the result comes with exploreCatalogOpened, EXP-F28).
    Q_INVOKABLE QVariantMap  exploreOpenCatalog(int deviceId);
    Q_INVOKABLE QVariantList getExploreFolders();
    Q_INVOKABLE QVariantList getExploreEntries(const QString &folderPath, bool showFolders, bool showSubFolders);
    Q_INVOKABLE void         loadExploreEntries(const QString &folderPath, bool showFolders, bool showSubFolders);
    Q_INVOKABLE void         exploreRemoveRow(int proxyRow);
    Q_INVOKABLE QVariantMap  getExploreFolderStats(const QString &folderPath);
    Q_INVOKABLE QString      exploreGetChecksum(const QString &fileName, const QString &folderPath);

    // Sort
    Q_INVOKABLE void sortSearch(int column, int order);
    Q_INVOKABLE void sortExplore(int column, int order);
    Q_INVOKABLE void sortDeviceTable(int column, int order);
    /** Device tree table: opens or closes one row's children (DVP-F13). */
    Q_INVOKABLE void toggleDeviceTableRow(int row);
    Q_INVOKABLE QVariantMap deviceTableRowInfo(int row) const;

    QString getSearchNameFilter() const;
    void    setSearchNameFilter(const QString &text);
    /** The selected catalog IDs; empty means every catalog (SRF-F5). */
    QVariantList getSearchCatalogFilters() const;
    Q_INVOKABLE void toggleSearchCatalogFilter(int catalogId);
    /** Selection helpers (SRF-F11): every listed catalog, or the empty set. */
    Q_INVOKABLE void selectAllSearchCatalogFilters();
    Q_INVOKABLE void clearSearchCatalogFilters();
    /** The catalogs present in the current results: {catalogId, name} entries. */
    QVariantList getSearchCatalogsFound() const;
    Q_INVOKABLE int  getSearchSortColumn()  const;
    Q_INVOKABLE int  getSearchSortOrder()   const;
    Q_INVOKABLE int  getExploreSortColumn() const;
    Q_INVOKABLE int  getExploreSortOrder()  const;

    // Sort model accessors (for Q_PROPERTY)
    QAbstractItemModel *getSearchSortModel()  const { return m_searchSortModel; }
    QAbstractItemModel *getExploreSortModel() const { return m_exploreSortModel; }
    QAbstractItemModel *getBackupPreviewModel() const { return m_backupPreviewModel; }
    QAbstractItemModel *getDeviceTableModel()   const { return m_deviceTableSortModel; }

    /** Feeds the Devices page table with the rows already loaded for the cards. */
    Q_INVOKABLE void populateDeviceTable(const QString &viewFilter, const QVariantList &rows);
    /** Width hint for a visible table column, for the QML columnWidthProvider. */
    Q_INVOKABLE int  deviceTableColumnWidth(int column) const;
    Q_INVOKABLE int  deviceTableColumnOfKey(const QString &key) const;

    // Storage helpers
    Q_INVOKABLE QStringList  getStoragePictureList() const;
    Q_INVOKABLE QString      getStorageImageFolderPath() const;
    Q_INVOKABLE QString      formatDataSize(qlonglong bytes) const;
    Q_INVOKABLE QString      formatDataSizeDelta(qlonglong bytes) const;
    Q_INVOKABLE QVariantMap  refreshStorageFromDisk(int deviceId);

    // URL / path conversion helpers (cross-platform, wrap QUrl)
    Q_INVOKABLE QString      pathToFileUrl(const QString &path) const;
    Q_INVOKABLE QString      pathFromFileUrl(const QString &url) const;
    Q_INVOKABLE QString      normalizeSourcePath(const QString &path) const;

    // Exclude directories (collection-level)
    Q_INVOKABLE QStringList  getExcludeDirectories() const;
    Q_INVOKABLE bool         addExcludeDirectory(const QString &path);
    Q_INVOKABLE void         removeExcludeDirectory(const QString &path);

    // Search history
    Q_INVOKABLE QVariantList getSearchHistory() const;
    Q_INVOKABLE QVariantMap  restoreSearchHistory(const QString &dateTime);
    Q_INVOKABLE void         clearSearchHistory();
    Q_INVOKABLE void         keepLastSearchHistory(int count);

    // File / folder operations from results
    Q_INVOKABLE void    openFile(const QString &filePath);
    Q_INVOKABLE void    openFolder(const QString &folderPath);
    // Probes ONE device path at the moment of the click, rather than reading the
    // cached device_active, so a drive disconnected after the results were
    // produced is still detected (SpecDeviceActiveStatus.md DAS-F12).
    // deviceId <= 0 means the row has no device behind it (a connected-directory
    // search browses a live folder), and is reported active.
    Q_INVOKABLE bool    probeDeviceActive(int deviceId);
    // Same probe for a Search results row. `row` is a row of the SORTED proxy and
    // is resolved through it, never by indexing the result arrays, and the device
    // is read by role name so no role number is hardcoded in QML.
    Q_INVOKABLE bool    searchRowDeviceIsActive(int row);
    Q_INVOKABLE int     searchRowDeviceId(int row) const;
    // Same probe for the device the Explore page is currently showing.
    Q_INVOKABLE bool    probeExploreDeviceActive();
    // Busy cursor over the whole window while long work blocks it, as K2 does
    // around opening a catalog in Explore. Calls do not stack.
    Q_INVOKABLE void    setBusyCursor(bool busy);
    // The delay that lets a change reach the screen before blocking work starts
    // (kStartDeferralMs, OPQ-F11), for QML callers.
    Q_INVOKABLE int     startDeferralMs() const;
    // Whether the file or folder about to be opened exists, tested once at click
    // time on that one path, so a stale catalog does not hand a missing path to
    // the desktop environment (SpecDeviceActiveStatus.md DAS-F13).
    Q_INVOKABLE bool    pathExists(const QString &path) const;
    // Row of a device in the filtered Selection list, or -1 when it is not shown.
    // Lets the UI scroll to a device without hardcoding a role number.
    Q_INVOKABLE int     selectionRowForDevice(int deviceId) const;

    Q_INVOKABLE void    copyToClipboard(const QString &text);
    // Version and environment facts for pasting into a bug report (SpecAbout.md).
    Q_INVOKABLE QString systemInformation();
    Q_INVOKABLE QString exportSearchResultsToCSV();
    Q_INVOKABLE QString exportSearchResultsAsCatalog();
    Q_INVOKABLE int          batchMoveSearchResultsToTrash();
    Q_INVOKABLE int          batchDeleteSearchResults();
    Q_INVOKABLE QVariantMap  batchVerifyChecksums();
    Q_INVOKABLE QVariantMap  batchGetMetadata();

    // Single-file operations from context menu
    Q_INVOKABLE bool    moveFileToTrash(const QString &fullPath);
    Q_INVOKABLE bool    deleteSingleFile(const QString &fullPath);
    Q_INVOKABLE bool         catalogIncludesExtendedMetadata(int catalogId);
    Q_INVOKABLE bool         catalogIncludesMetadata(int catalogId);
    Q_INVOKABLE QString      getFileMetadataJson(int catalogId, const QString &fileName, const QString &folderPath);
    Q_INVOKABLE QVariantList getFileMetadataParsedFields(int catalogId, const QString &fileName, const QString &folderPath);
    Q_INVOKABLE QString calculateAndSaveChecksum(const QString &filePath, const QString &fileName, const QString &folderPath, int catalogId);
    Q_INVOKABLE QString verifyFileChecksum(const QString &filePath, const QString &expectedChecksum);
    Q_INVOKABLE QString calculateChecksumMd5(const QString &filePath);

    Q_INVOKABLE bool    shouldShowAlphaWarning() const;
    Q_INVOKABLE void    setAlphaWarningShown();
    bool                isFirstRun() const { return m_firstRun; }
    Q_INVOKABLE void    clearFirstRun();

    // Recent collections
    QVariantList getRecentCollections() const;
    Q_INVOKABLE void openRecentCollection(const QVariantMap &entry);
    QString getCurrentCollectionDisplayName() const;
    QString getCurrentCollectionIconName() const;

    // Hosted database settings
    Q_INVOKABLE QString getHostName() const;
    Q_INVOKABLE QString getDatabaseName() const;
    Q_INVOKABLE QString getPhpMyAdminUrl() const;
    Q_INVOKABLE void    setPhpMyAdminUrl(const QString &url);
    Q_INVOKABLE int     getDatabasePort() const;
    Q_INVOKABLE QString getDatabaseUserName() const;
    Q_INVOKABLE QString getDatabasePassword() const;

    // Image folder
    Q_INVOKABLE QString getImageFolderPath() const;
    Q_INVOKABLE void    setImageFolderPath(const QString &path);

    // Collection import
    Q_INVOKABLE QStringList       getImportSourcePaths() const;
    // Quality check (SpecQualityCheck.md): list of {check, rows, error}
    Q_INVOKABLE QVariantList      runQualityChecks() const;
    Q_INVOKABLE void              openImportSource(const QString &path);
    Q_INVOKABLE QString           importDevice(int srcDeviceId);
    DeviceListModel              *getImportSourceDeviceModel() const { return m_importSourceDeviceModel; }
    Q_INVOKABLE QString      updateAllImportsFromSource(const QString &path);

    // Language
    Q_INVOKABLE QVariantList getLanguageList() const;
    Q_INVOKABLE QString      getCurrentLanguage() const;
    Q_INVOKABLE void         setLanguage(const QString &languageCode);

signals:
    void textPointSizeChanged();
    void searchStateChanged();
    void exploreLoadStateChanged();
    void exploreStatusTextChanged();
    void mdiStateChanged();
    void mdiStatusTextChanged();
    void mdiRevisionChanged();
    void mdiSystemChanged();
    void mdiStepChanged();
    void mdiStepLogChanged();
    void enrichmentFinished(const QVariantMap &summary);
    void contextDraftReady(const QVariantMap &draft, const QString &error);
    void homeOverviewChanged();   // a Home count computed in the background is ready
    void knowledgeStateChanged();
    void semanticResultsChanged();
    void infoResultsChanged();
    void localModelChecked(bool reachable);
    void fileInfoChanged();
    void peopleChanged();
    void facesChanged();
    void similarFacesReady(int personId, const QVariantList &faces, int serial);
    void faceGroupsReady(const QVariantList &groups, int serial);
    void transcriptsCleaned(const QVariantMap &counts);
    void proposalsChanged();
    void webQueriesRunningChanged();
    // asked: queries sent; found: those with results; error: the last failure
    void webQueriesFinished(int asked, int found, const QString &error);
    void htmlExportStateChanged();
    // error: empty on success; stopped: ended by Stop
    void htmlExportFinished(const QString &target, const QString &error, bool stopped);
    void archiveCopyStateChanged();
    // message: what to tell the owner (the folder written, or why not)
    void archiveCopyFinished(bool ok, const QString &message);
    // End of a Memory-mode Explore load: the open's result, or stopped
    void exploreCatalogOpened(const QVariantMap &info);
    // The latest open requested during a load, to run now that it has ended
    void exploreOpenPending(int deviceId);
    void searchStatusTextChanged();
    void searchFiltersChanged();
    void searchCatalogsFoundChanged();
    void catalogIsCreatingChanged();
    void catalogStatusTextChanged();
    void catalogCreationCompleted(bool success, const QString &report);
    void excludeDirectoriesChanged();
    void deviceListModelChanged();
    void deviceExpandLevelChanged();
    void showDeviceInfoChanged();
    void operationQueueChanged();
    void refreshDeviceStatusOnActivationChanged();
    void allowFileDeletionChanged();
    void themeIdChanged();
    void biggerIconSizeChanged();
    void showSelectionPageChanged();
    void searchKeepsSelectionChanged();
    void deviceFilterFromSelectionChanged();
    void deviceDisplayAsTableChanged();
    void deviceDisplayFullTableChanged();
    void deviceDisplayContentsChanged();
    void checkVersionChoiceChanged();
    // A newer release is published (SpecVersionCheck.md VCK-F2)
    void newVersionAvailable(const QString &currentVersion, const QString &latestVersion,
                             const QString &releaseNotesUrl);
    void fileSortCaseSensitiveChanged();
    void databasePathChanged(const QString &newPath);
    void databaseConnectionChanged(bool success, const QString &message);
    void deviceListRefreshed();
    void searchResultsRefreshed();
    void statisticsRefreshed();
    void uiRefreshCompleted();
    void selectedDeviceChanged(int deviceId);
    void databaseModeChanged();
    void recentCollectionsChanged();
    void languageChanged(const QString &code);
    void imageFolderPathChanged();
    void importSourceChanged();
    void importIsRunningChanged();
    void importStatusTextChanged();
    void importReportTextChanged();
    void exportStateChanged();
    void exportCompleted(const QString &targetPath);
    void tagsChanged();
    void backupMappingsChanged();
    void backupProgress(int filesDone, int totalFiles, qint64 bytesCopied, qint64 totalBytes, const QString &currentFile);
    void backupReportChanged();
    void backupFinished(int copiedCount, int movedCount, int renamedCount, int conflictCount, int errorCount, qint64 totalBytesCopied, bool wasCancelled);
    void backupNotification(const QString &message, bool isError);
    void backupCsvExported(const QString &filePath);
    void updateBeforeBackupChanged();
    void catalogUpdateForBackupRunningChanged();
    void catalogUpdateForBackupStatusChanged();
    void catalogsForMappingPrepared(int mappingId, bool success, const QString &error);
    void runningBackupMappingIdChanged();
    void backupPreviewRunningChanged();
    void backupPreviewStatusChanged();
    void backupPreviewReady(int mappingId, bool success, bool cancelled);
    void deviceUpdateStateChanged();
    void deviceUpdateStatusChanged();
    void deviceUpdateReportReady(QVariantMap report);
    void firstRunChanged();
    void deviceListChanged();
    // Another storage already carries the number just saved (STI-F5). The save
    // went through; this is a warning, not a refusal.
    void storageUserIdDuplicate();
    void checksumVerificationCompleted(QVariantMap result);
    void splitCompleted(bool success, const QString &error);
    // History (fork): the list changed; an action just done can be undone
    // with restoreHistoryEntry(entryId)
    void historyChanged();
    void undoAvailable(int entryId, const QString &message);
    // mismatchCount < 0 means the verification failed with errorMessage
    void mimeVerificationFinished(int mismatchCount, const QString &reportPath, const QString &errorMessage);
    // Read-only collection (D43)
    void readOnlyChanged();
    void writersChanged();

private:
    // Memory-mode Explore load (EXP-F28 to EXP-F32, EXP-C22 to EXP-C24)
    void         runExploreLoad(int deviceId);
    QVariantMap  finishExploreOpen(int deviceId);
    bool    m_exploreLoadIsRunning  = false;
    bool    m_exploreStopRequested  = false;
    QMutex  m_exploreMutex;
    int     m_exploreLoadingId      = -1;
    int     m_explorePendingId      = -1;
    QString m_exploreStatusText;
    bool m_busyCursor = false;
    // Default window size (WIN-C1, SpecWindowGeometry.md): first start and reset.
    static constexpr int kDefaultWindowWidth  = 1280;
    static constexpr int kDefaultWindowHeight = 800;

    // The single naming rule for a collection, shared by the drawer header and the
    // Open menu's recent entries so the two cannot drift apart
    // (SpecCollectionIdentity.md CID-C5). Callers supply the values they hold;
    // the mode decides which ones are used.
    static QString collectionDisplayName(const QString &mode, const QString &path,
                                         const QString &hostName, const QString &dbName);

    QString m_connectionName = "defaultConnection";
    // Device active-status refresh policy (SpecDeviceActiveStatus.md).
    // Mutable: getDeviceList() is const but legitimately re-probes before reading.
    // The mount table as it was when the devices were last checked. Used only
    // to decide whether devices on network mounts need checking too
    // (SpecDeviceActiveStatus.md DAS-F5/DAS-C3). Mutable because
    // getDeviceList() is const and checks.
    mutable QString m_lastMountSignature;
    bool    m_firstRun = false;
    bool    m_searchIsRunning  = false;
    bool    m_searchIsPaused   = false;
    QString m_searchStatusText;
    bool    m_catalogIsCreating = false;
    QString m_catalogStatusText;
    Device *m_creatingDevice   = nullptr;
    QDateTime m_catalogCreateStartTime;
    // Create wizard: profiles waiting for their catalog's creation to complete,
    // keyed by catalog device ID (a creation can wait in the operation queue)
    int     m_lastCreateRequestDeviceId = 0;
    QHash<int, QVariantMap> m_pendingEnrichmentProfiles;
    void    savePendingEnrichmentProfile(int deviceId);

    // Enrichment runner: one job at a time, the others wait in order (MDI-F16)
    MdiEnrichmentJob *m_mdiJob      = nullptr;
    QThread          *m_mdiThread   = nullptr;
    bool              m_mdiIsPaused = false;
    QString           m_mdiStatusText;
    QString           m_mdiCatalogName;
    QList<MdiData::PendingJob> m_mdiWaiting;
    int               m_mdiRevision = 0;
    bool              m_mdiRevisionPending = false;
    bool              m_mdiQuitHooked = false;
    // Explore display cache of the catalog shown, keyed by full path
    int                          m_mdiIndexCatalogId = -1;
    int                          m_mdiIndexRevision  = -1;
    QHash<QString, QVariantMap>  m_mdiIndex;
    QSet<QString>                m_mdiAiPaths;   // files of the indexed catalog with AI results
    // Files with a description, summary, transcript or classification: what
    // Browse tags "AI" and Home counts (MDI-F130)
    static QSet<QString> aiResultPaths(const QString &mdiConnection, int catalogId,
                                       const QHash<QString, QVariantMap> &catalogIndex);
    QString mdiDataPath() const;
    bool    openMdiMainConnection();
    void    refreshMdiIndex(int catalogId);
    void    startNextEnrichment();
    // Kept across restarts and collection switches in mdi_data.db (MDI-F12, MDI-C28)
    MdiData::PendingJob m_mdiRunning{-1};          // the running job (device -1 when none)
    bool              m_mdiQuickHashOnly = false;  // the running job is a quick-hash pass
    bool              m_mdiWaitingForSource = false;   // a drive is away (MDI-F91)
    QString           m_mdiWaitingPath;                // the path waited for
    bool              m_mdiKeepPending = false;    // ends on quit or switch: stays pending (MDI-F93)
    bool              m_mdiLeaving = false;        // ends by Stop or by stepping aside (MDI-F92)
    // Pending jobs that cannot start now (no profile, destination not
    // writable): kept, and tried again when the collection is opened (MDI-F12)
    QList<MdiData::PendingJob> m_mdiHeld;
    // Home page caches: file types per catalog (by catalog date), files with AI
    // results per catalog (refreshed at most every 15 s)
    QHash<int, QPair<QString, QVariantMap>> m_homeTypeCache;
    QHash<int, QPair<qint64, int>>          m_homeAiCache;
    QSet<int>                               m_homeCounting;   // catalogs counted or waiting
    struct HomeCount { int deviceId; int catalogId; QString stamp; QString catalogFile; bool types; };
    QList<HomeCount>                        m_homeQueue;      // one worker at a time
    bool                                    m_homeWorkerBusy = false;
    // A count belongs to the collection (and device numbers) it was queued
    // for: a result from before a switch or a split is dropped
    int                                     m_homeGeneration = 0;
    QPointer<QThread>                       m_homeWorker;
    void countHomeDrive(int deviceId, int catalogId, const QString &stamp, const QString &catalogFile, bool types);
    void runNextHomeCount();
    void waitForHomeCount();
    // Catalog of the running enrichment job (-1 when none): a split,
    // recompose or restore of it waits for the job to end
    int                                     m_mdiRunningCatalogId = -1;
    // Revision at which each catalog's AI data last changed, and at which all
    // did: Explore reloads the shown catalog's index only when it changed
    QHash<int, int>                         m_mdiCatalogRevision;
    int                                     m_mdiAllRevision = 0;
    void bumpMdiRevision(int catalogId);

    // History (fork): the restore point of the update running, closed when
    // the device update manager reports the end (through m_historyHook, a
    // receiver of its own that the manager's disconnect(this) leaves alone)
    int      m_historyUpdateEntry  = 0;
    bool     m_historyUpdateByUser = false;
    QObject *m_historyHook         = nullptr;
    QPointer<DeviceUpdateManager> m_historyHookedManager;
    int      historyBefore(const QString &action, const QList<int> &deviceIds, bool withDescendants,
                           const QString &label = QString());
    void     historyAfter(int entryId, const QList<int> &resultDeviceIds, const QString &undoMessage,
                          bool dropUnchanged = false, bool byUser = true);
    void     historyDiscard(int entryId);
    void     historyBeforeUpdate(const QString &action, const QList<int> &deviceIds, bool byUser);
    void     historyAfterUpdate();
    QString  catalogChangeRefusal(const QList<int> &deviceIds, const QList<int> &catalogIds = {});
    void     afterCatalogStructureChange();
    QVariantMap historyEntryMap(const CatalogHistory::Entry &e, const CatalogHistory &history, int depth = 0) const;
    QString  historyErrorText(CatalogHistory::Error error) const;
    void    holdOrDropMdiJob(const MdiData::PendingJob &job, const QString &error);
    QString startMdiJob(const MdiData::PendingJob &job, bool resumed);
    bool    mdiJobReachable(const MdiData::PendingJob &job);
    // Knowledge-base indexing: its own runner instance and thread (MDI-C33)
    MdiEnrichmentJob *m_kbJob    = nullptr;
    QThread          *m_kbThread = nullptr;
    bool              m_kbPending = false;   // documents added while it runs
    QString           m_kbStatusText;
    void    startKnowledgeIndexing();
    void    stopKnowledgeIndexing();
    void    saveMdiJobs();
    void    restoreMdiJobs();
    void    stepAsideIfWaiting();
    bool    mdiSourceReachable(int deviceId);
    QString mdiStateStatus() const;   // In Progress, Paused or waiting for the drive

    // Activity page
    MdiSystemMonitor *m_monitor       = nullptr;
    QThread          *m_monitorThread = nullptr;
    bool              m_activityVisible = false;
    QVariantMap       m_mdiSystem;
    QVariantList      m_mdiHistory;      // session only (D21)
    QVariantMap       m_mdiStep;         // running step, empty between steps
    QVariantList      m_mdiStepLog;      // finished steps, newest first
    QDateTime         m_mdiJobStart;
    QString           m_mdiSourcePath;   // catalog source, to tell whether it is connected
    void    updateMonitoring();
    void    onMdiActivity(const QVariantMap &step);

    // Instant-search index (fork, D5): rebuilt in the background, one catalog
    // at a time, for the catalogs whose index is missing or stale
    QThread          *m_ftsThread = nullptr;
    std::atomic<bool> m_ftsStop{false};
    bool              m_ftsPending = false;
    void    scheduleFtsIndexing();
    // Quick hash (fork): pass right after creation/update, all files, on by default
    QList<int>        m_quickHashWaiting;
    QString           startQuickHash(int deviceId);
    void              scheduleQuickHashes();
    QString           launchMdiJob(Device &device, MdiEnrichmentJob::Config config, const MdiData::PendingJob &job);
    void    stopFtsIndexing();
    QString mdiAnalysisLabel(const QString &analysis) const;
    QVariantList m_semanticResults;
    bool         m_semanticActive = false;
    bool         m_semanticOnly   = false;
    // "In the information" (MDI-F183): only the latest search's list is kept
    QVariantList     m_infoResults;
    bool             m_infoActive       = false;
    bool             m_infoOnly         = false;
    bool             m_infoSearchBusy   = false;
    int              m_infoSearchSerial = 0;
    QList<QThread *> m_infoSearchThreads;
    // File information, people, faces, transcript cleaning (fork, 2026-10-30)
    QThread          *m_transcriptCleanThread = nullptr;
    std::atomic<bool> m_transcriptCleanStop{false};
    QThread          *m_infoIndexThread = nullptr;
    std::atomic<bool> m_infoIndexStop{false};
    QList<QPair<int, QString>> m_infoIndexPending;   // files whose info changed: instant index + "info" vectors
    QString           m_infoIndexPath;                // the mdi_data.db they belong to
    QString peopleStoreRefusal();                     // empty when mdi_data.db is open for them
    // Read-only collection (fork, D43). One text per cause, readOnly while any
    // is set. The collection lock is held only while it can be changed.
    enum class ReadOnlyCause {
        LockedElsewhere = 1,   // another process holds the collection lock
        NotAuthorised   = 2    // protection on, this computer not in the list
        // P2 hook: PublishedCopy = 3 — a published copy is opened read-only;
        // its text goes through setReadOnlyCause like the two above.
    };
    QMap<int, QString> m_readOnlyCauses;
    bool               m_migrationSkipped = false;   // read-only: the update of its database is not made
    CollectionLock    *m_collectionLock   = nullptr;
    QTimer            *m_lockRefreshTimer = nullptr;
    QString readOnlyRefusal() const;                  // "" when the collection can be changed
    void    setReadOnlyCause(ReadOnlyCause cause, const QString &text);   // empty text clears it
    void    applyReadOnlyState();                     // core flags, lock, running work, notify
    void    prepareCollectionAccess();                // before connecting: writers from the files, then the lock
    void    confirmCollectionAccess();                // after connecting: writers from the database (Hosted too)
    void    openCollectionDatabase(const QString &conn);   // migrations, or the read-only preparation
    void    refreshCollectionLock();                  // every CollectionLock::kRefreshSeconds
    QString lockHolderText(const QString &host, qint64 pid, const QString &app) const;
    QString writersChangeText(int change) const;      // CollectionWriters::Change in words, "" for Ok
    double  faceMatchThreshold();
    void    reindexFileInfo(const QList<QPair<int, QString>> &files);
    void    runInfoIndexing();
    void    stopPeopleWorkers();
    // Web lookups of the dating (fork, 2026-10-30)
    QThread          *m_webThread = nullptr;
    std::atomic<bool> m_webStop{false};
    // HTML export and copy of the archive data on their workers (fork, 2026-10-30)
    QThread          *m_htmlExportThread = nullptr;
    std::atomic<bool> m_htmlExportStop{false};
    QString           m_htmlExportStatusText;
    QThread          *m_archiveCopyThread = nullptr;
    QString           m_archiveCopyStatusText;
    QString prepareHtmlExport(const QString &scope, int catalogId, const QString &path, const QString &targetFile,
                              MdiHtmlExport::Scope *request, MdiHtmlExport::Options *options, QString *target);
    void    setHtmlExportProgress(const QString &target, int done, int total);
    void    finishHtmlExport(const QString &target, const QString &error, bool stopped);
    // Similar faces and face groups (fork, 2026-10-30): one worker at a time,
    // results kept per collection until a change (see requestFaceGroups)
    QThread          *m_faceThread = nullptr;
    int               m_faceCacheRevision = 0;     // bumped by facesChanged / peopleChanged
    int               m_faceSerial = 0;            // one per computation
    QString           m_faceCachePath;             // the mdi_data.db the results belong to
    QVariantMap       m_faceGroupsCache;           // {limit, rows, serial, mdiRevision}
    QHash<QString, QVariantMap> m_similarCache;    // "person/limit/score" -> same shape
    int               m_faceGroupsWanted = -1;     // limit asked, -1 when none
    QVariantMap       m_similarWanted;             // {personId, limit, floor}, empty when none
    void    invalidateFaceCache();
    void    checkFaceCachePath();
    void    runFaceWorker();
    QString proposalKindLabel(const QString &kind) const;
    QString proposalFieldLabel(const QString &field) const;
    QVariantList proposalSources(const QVariantList &sources) const;
    DeviceUpdateManager    *m_deviceUpdateManager    = nullptr;
    CatalogProgressManager *m_catalogProgressManager = nullptr;
    CollectionImporter     *m_importer               = nullptr;
    bool    m_importIsRunning  = false;
    QString m_importStatusText;
    QString m_importReportText;
    bool    m_exportIsRunning       = false;
    bool    m_exportCancelRequested = false;
    QString m_exportStatusText;
    bool    exportIsBlocked() const;
    // Runs the MIME job on dev (Memory mode: loads the file list first, saves
    // the .idx after), then calls onDone(mismatchCount, reportPath, error)
    void    runMimeVerification(Device *dev,
                                std::function<void(int, const QString &, const QString &)> onDone);
    void    setExportStatus(const QString &text);
    void    finishExport(const QString &status, const QString &reason = QString());
    DeviceListModel        *m_importSourceDeviceModel = nullptr;
    QFont                   m_systemFont;   // OS font captured at startup: base of the text-size setting (TYP-F1)

    void setupDeviceUpdateManager();
    void onCatalogCreationCompleted(const QList<qint64> &results);
    void onCatalogCreationError(const QString &error);
    void onCatalogCreationCancelled();

    // Backup helpers
    // Runs the preview compare via core (CatalogDifferenceEngine::compareForBackup) with
    // cooperative yielding + m_previewStopRequested so the UI stays responsive and can be
    // cancelled (BKP-F16/BKP-C8); populates the model + m_lastPreviewSummary, then emits
    // backupPreviewReady.
    void runPreviewCompare(int mappingId);
    void executeBackupJob(int mappingId);
    void setRunningBackupMappingId(int mappingId);
    void startNextListedBackup();
    void onBackupProgressInternal(int filesDone, int totalFiles, qint64 bytesCopied, qint64 totalBytes, const QString &currentFile);
    void onBackupFinishedInternal(const BackupReport &report);

    BackupJobStoppable     *m_backupJob             = nullptr;
    QThread                *m_backupThread          = nullptr;
    bool                    m_backupIsArchive        = false;
    QElapsedTimer           m_backupTimer;
    Device                  m_backupSourceDevice;
    Device                  m_backupTargetDevice;
    int                     m_runningBackupMappingId = -1;
    // Links still waiting in a "Run listed links" sequence (SpecBackup.md BKP-C10).
    QList<int>              m_pendingBackupMappings;
    QList<BackupPreviewRow> m_lastPreviewRows;
    bool                    m_exportAfterPreview = false;
    void                    beginBackupPreview(int mappingId);
    QList<BackupPreviewRow> m_lastReportRows;          // BKP-F29, session only (BKP-C15)
    int                     m_lastReportFlaggedCount = 0; // trailing conflict + error rows
    QString                 m_lastReportSummary;
    void                    clearBackupReport();

    // Async, cancellable preview state (BKP-F16/BKP-C8)
    bool        m_backupPreviewRunning        = false;
    bool        m_previewStopRequested        = false;
    QString     m_backupPreviewStatusText;
    QVariantMap m_lastPreviewSummary;
    int         m_previewAfterUpdateMappingId = -1;  // preview waiting on the pre-preview catalog update

    // Post-backup catalog re-scan queue (refreshes the card's totals/diff). Backup
    // re-scans the target; Archive re-scans source (files moved out) AND target.
    QList<int>              m_postBackupUpdateQueue;
    void                    runNextPostBackupUpdate();

    // Catalog-update-before-backup state
    enum class BackupCatalogUpdatePhase { None, UpdatingSource, UpdatingTarget };
    BackupCatalogUpdatePhase m_backupCatalogUpdatePhase       = BackupCatalogUpdatePhase::None;
    int                      m_pendingCatalogUpdateMappingId  = -1;
    int                      m_pendingBackupAfterUpdate       = -1;
    Device                   m_pendingCatalogUpdateSourceDevice;
    Device                   m_pendingCatalogUpdateTargetDevice;
    bool                     m_catalogUpdateForBackupRunning  = false;
    QString                  m_catalogUpdateForBackupStatusText;
    void setupCatalogUpdateForBackupConnections();
    void onCatalogUpdateForBackupStep();

    // Devices page state
    bool    m_deviceUpdateIsRunning  = false;
    QString m_deviceUpdateStatusText;

    // Operation queue (SpecOperationQueue.md). One entry per requested catalog
    // create or device update; they run strictly one at a time, in order.
    // A create's device and catalog rows already exist in the database by the
    // time it is queued, so an entry only needs the device id.
    struct QueuedOperation {
        int     deviceId = 0;
        bool    isCreate = false;
        QString deviceName;
        // A path-root replacement is an update entry (SpecOperationQueue.md,
        // note on OPQ-C2), distinguished only by what the entry runs and by
        // carrying the two paths the replacement needs.
        bool    isReplaceRoot = false;
        QString previousPath;
        QString newPath;
    };
    QList<QueuedOperation> m_pendingDeviceUpdates;
    QueuedOperation        m_runningOperation;
    bool                   m_hasRunningOperation = false;
    void enqueueOperation(int deviceId, bool isCreate, const QString &deviceName);
    void enqueueReplaceRoot(int deviceId, const QString &deviceName,
                            const QString &previousPath, const QString &newPath);
    bool isDeviceQueuedOrRunning(int deviceId) const;
    void emitQueueChanged();
    // Releases the entry that just ended and hands over to the next one; called
    // from every completion, cancellation and error handler.
    void finishRunningOperation();
    // Starts the next entry once core has finished with the previous one.
    void scheduleNextOperation();
    bool    m_nextOperationScheduled = false;
    // How long to wait before starting the next entry. Must outlast core's own
    // deferred teardown of the operation that just ended — DeviceUpdateManager
    // arms cleanupOperation() 10 ms after completion — and the wait is invisible
    // next to operations that run for minutes.
    static constexpr int operationChainDelayMs = 150;
    // Entries started since the queue was last empty; with the entries still
    // waiting it gives the "catalog N of M" batch context, which stays right
    // when the queue grows mid-drain.
    int     m_operationsStarted      = 0;
    int     m_currentUpdateDeviceId  = 0;
    bool    m_isBatchUpdate          = false;
    bool    m_showEachUpdateReport   = false;
    bool    m_waitingForReportAck    = false;
    void setupDeviceUpdateManagerForDevices();
    void onDevicePageUpdateCompleted(const QList<qint64> &results);
    void startNextDeviceUpdate();
    QVariantMap buildUpdateReport(int deviceId, const QList<qint64> &results);
    QVariantMap buildBatchUpdateReport(const QList<qint64> &results);

    ExploreFilesModel *m_exploreFilesModel = nullptr;
    FilesView         *m_exploreSortModel  = nullptr;
    DeviceTableModel      *m_deviceTableModel     = nullptr;
    QSortFilterProxyModel *m_deviceTableSortModel = nullptr;
    BackupPreviewModel *m_backupPreviewModel = nullptr;
    FilesView         *m_searchSortModel   = nullptr;

    // Size of the drawer's Open > recent list (SpecCollectionOpen.md OPN-F7).
    // Main.qml holds one menu action per slot, so it must match this.
    static constexpr int maxRecentCollections = 10;
    void saveToRecentCollections(const QString &mode, const QString &path,
                                 const QString &displayName,
                                 const QString &hostName = QString(),
                                 const QString &dbName   = QString(),
                                 int port = 3306,
                                 const QString &userName = QString(),
                                 const QString &password = QString());
};

#endif // APPMANAGER_H

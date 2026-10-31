/*LICENCE
    This file is part of Katalog

    Copyright (C) 2020, the Katalog Development team

    Author: Stephane Couturier (Symbioxy)

    Katalog is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Katalog is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Katalog; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
/*FILE DESCRIPTION
/////////////////////////////////////////////////////////////////////////////
// Application: Katalog
// File Name:   commandline.cpp
// Purpose:     Class/model for the command lines
// Description:
// Author:      Stephane Couturier
/////////////////////////////////////////////////////////////////////////////
*/
#include "commandline.h"
#include "core/catalogjobstoppable.h"
#include "core/catalogmanager.h"
#include "core/searchjobstoppable.h"
#include "core/databasemanager.h"
#include "core/mdienrichmentjob.h"
#include "core/mdiprofile.h"
#include "core/mdidata.h"
#include "core/mdifts.h"
#include "core/mdidiskspace.h"
#include <QThread>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>
#include <csignal>
#include <QDir>
#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QDebug>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSettings>
#include <QLocale>
#include <QFile>
#include <QProcess>
#include <algorithm>

CommandLineHandler::CommandLineHandler(QObject *parent)
    : QObject(parent)
    , collection(nullptr)
    , searchManager(nullptr)
    , searchEngine(nullptr)
    , selectedDevice(nullptr)
    , eventLoop(nullptr)
    , searchResult(0)
    , searchCompleted(false)
    , verbose(false)
    , outputLimit(-1)
    , useSearchHistory(true)
    , overrideCaseSensitive(false)
    , searchCriteriaProvided(false)
{
    setupCommandLineParser();
}

CommandLineHandler::~CommandLineHandler()
{
    cleanup();
}

//--- Initialize ------------------------------------------------------------------------

void CommandLineHandler::cleanup()
{
    // Clean up search objects
    if (searchManager) {
        searchManager->deleteLater();
        searchManager = nullptr;
    }

    if (searchEngine) {
        searchEngine->deleteLater();
        searchEngine = nullptr;
    }

    if (selectedDevice) {
        delete selectedDevice;
        selectedDevice = nullptr;
    }

    if (eventLoop) {
        delete eventLoop;
        eventLoop = nullptr;
    }

    // Collection will be cleaned up by parent
}

void CommandLineHandler::setupCommandLineParser()
{
    parser.setApplicationDescription(
        "Katalog - File Catalog Management and Search Tool\n\n"
        "Examples:\n"
        "  Katalog search                                     # Use collection and device from settings\n"
        "  Katalog search --selectedDeviceID 4                # Use collection from settings, device ID 4\n"
        "  Katalog search --collection /path/to/collection    # Use specified collection, all devices\n"
        "  Katalog search --collection /path/to/collection --selectedDeviceID 4  # Use specified collection, device ID 4\n"
        "  Katalog search --collection /path/to/collection --selectedDeviceID 0  # Use specified collection, all devices (explicit)"
        // AI analysis and exit codes (fork, D43)
        "\n"
        "  Katalog analyze 4                                  # AI analysis of catalog 4 with its saved profile\n"
        "  Katalog analyze_all                                # AI analysis of every catalog with a saved profile\n"
        "  Katalog publish                                    # Publish for the read-only Macs, into the folder set in Teca\n"
        "  Katalog publish /path/to/folder                    # Publish into that folder\n"
        "\n"
        "Exit codes:\n"
        "  0    Success\n"
        "  1    Error: invalid option or action, collection not found or not openable,\n"
        "       catalog not updated or not analyzed\n"
        "  2    Analysis done, but some steps failed: they run again next time\n"
        "  3    The collection is in use by Teca or by another command line run: nothing was done\n"
        "  4    This computer may not write to the collection: nothing was done\n"
        "  130  Stopped by SIGINT (Ctrl+C); 143: stopped by SIGTERM. The analyses done so far are kept"
        );

    parser.addHelpOption();
    parser.addVersionOption();

    // Positional arguments
    parser.addPositionalArgument("action", "The action to be performed. Choices:\n"
                                           "  list_catalogs     - List all catalogs (ID, active state, name)\n"
                                           "  update_catalog    - Update a specific catalog (deviceID)\n"
                                           "  update_all_active - Update all active catalogs\n"
                                           "  search            - Execute search using last search criteria"
                                           // Fork, D43
                                           "\n"
                                           "  analyze           - AI analysis of one catalog with its saved profile (deviceID)\n"
                                           "  analyze_all       - AI analysis of every catalog with a saved profile\n"
                                           "  publish           - Publish for the read-only Macs ([folder]; default: the folder set in Teca)");

    parser.addPositionalArgument("deviceID", "Device ID for update_catalog action (optional)", "[deviceID]");

    // Options
    //Report option deactivated for now
    // parser.addOption(QCommandLineOption(QStringList() << "r" << "report",
    //                                     "Show detailed report for update operations"));

    parser.addOption(QCommandLineOption(QStringList() << "c" << "collection",
                                        "Path to collection folder",
                                        "path"));

    parser.addOption(QCommandLineOption("verbose",
                                        "Enable verbose output"));

    parser.addOption(QCommandLineOption("limit",
                                        "Limit number of files to display",
                                        "number"));

    parser.addOption(QCommandLineOption("selectedDeviceID",
                                        "Device ID to search in. Default: use settings file value, or 0 (All) when used with --collection",
                                        "deviceID"));

    // Search criteria override options
    parser.addOption(QCommandLineOption("text",
                                        "Search text/phrase to look for",
                                        "search-term"));

    parser.addOption(QCommandLineOption("type",
                                        "Filter by file type: all, audio, image, text, video",
                                        "file-type", "all"));

    parser.addOption(QCommandLineOption("size-min",
                                        "Minimum file size (e.g. 1MB, 5GB)",
                                        "size"));

    parser.addOption(QCommandLineOption("size-max",
                                        "Maximum file size (e.g. 100MB, 2GB)",
                                        "size"));

    parser.addOption(QCommandLineOption("date-after",
                                        "Files modified after date (YYYY-MM-DD)",
                                        "date"));

    parser.addOption(QCommandLineOption("date-before",
                                        "Files modified before date (YYYY-MM-DD)",
                                        "date"));

    parser.addOption(QCommandLineOption("case-sensitive",
                                        "Enable case-sensitive text search"));

    parser.addOption(QCommandLineOption("search-in",
                                        "Search scope: filenames, files-and-folders, folder-paths",
                                        "scope", "filenames"));

    parser.addOption(QCommandLineOption("text-criteria",
                                        "Text matching: all-words, exact-phrase, begins-with, any-word",
                                        "criteria", "all-words"));

    parser.addOption(QCommandLineOption("exclude",
                                        "Exclude files containing these terms",
                                        "exclude-terms"));

    parser.addOption(QCommandLineOption("no-history",
                                        "Start with default criteria instead of loading search history"));
}

bool CommandLineHandler::parseArguments(const QCoreApplication &app)
{
    parser.process(app);

    // Get options
    verbose = parser.isSet("verbose");

    // Parse collection option
    if (parser.isSet("collection")) {
        collectionPath = parser.value("collection");
    }

    // Parse limit option
    if (parser.isSet("limit")) {
        bool ok;
        outputLimit = parser.value("limit").toInt(&ok);
        if (!ok || outputLimit < 1) {
            QTextStream stderr_stream(stderr);
            stderr_stream << "Error: --limit must be a positive number" << Qt::endl;
            return false;
        }
        if (verbose) {
            QTextStream stdout_stream(stdout);
            stdout_stream << "Output limit set to: " << outputLimit << " files" << Qt::endl;
        }
    }

    // Parse selectedDeviceID option
    selectedDeviceID = 0; // Default value
    selectedDeviceIDProvided = false;

    if (parser.isSet("selectedDeviceID")) {
        bool ok;
        selectedDeviceID = parser.value("selectedDeviceID").toInt(&ok);
        selectedDeviceIDProvided = true;

        if (!ok || selectedDeviceID < 0) {
            QTextStream stderr_stream(stderr);
            stderr_stream << "Error: --selectedDeviceID must be a non-negative number" << Qt::endl;
            return false;
        }

        if (verbose) {
            QTextStream stdout_stream(stdout);
            stdout_stream << "Command line selectedDeviceID: " << selectedDeviceID << Qt::endl;
        }
    }

    // Parse search criteria overrides
    searchCriteriaProvided = false;
    useSearchHistory = !parser.isSet("no-history");

    if (parser.isSet("text")) {
        overrideSearchText = parser.value("text");
        searchCriteriaProvided = true;
    }

    if (parser.isSet("type")) {
        overrideFileType = parser.value("type").toLower();
        QStringList validTypes = {"all", "audio", "image", "text", "video"};
        if (!validTypes.contains(overrideFileType)) {
            QTextStream stderr_stream(stderr);
            stderr_stream << "Error: --type must be one of: " << validTypes.join(", ") << Qt::endl;
            return false;
        }
        searchCriteriaProvided = true;
    }

    if (parser.isSet("size-min")) {
        overrideSizeMin = parser.value("size-min");
        if (!validateSizeFormat(overrideSizeMin)) {
            QTextStream stderr_stream(stderr);
            stderr_stream << "Error: Invalid --size-min format. Use format like: 1MB, 5GB, 100KB" << Qt::endl;
            return false;
        }
        searchCriteriaProvided = true;
    }

    if (parser.isSet("size-max")) {
        overrideSizeMax = parser.value("size-max");
        if (!validateSizeFormat(overrideSizeMax)) {
            QTextStream stderr_stream(stderr);
            stderr_stream << "Error: Invalid --size-max format. Use format like: 1MB, 5GB, 100KB" << Qt::endl;
            return false;
        }
        searchCriteriaProvided = true;
    }

    if (parser.isSet("date-after")) {
        QString dateStr = parser.value("date-after");
        if (!validateDateFormat(dateStr)) {
            QTextStream stderr_stream(stderr);
            stderr_stream << "Error: Invalid --date-after format. Use YYYY-MM-DD format" << Qt::endl;
            return false;
        }
        overrideDateAfter = parseDate(dateStr);
        searchCriteriaProvided = true;
    }

    if (parser.isSet("date-before")) {
        QString dateStr = parser.value("date-before");
        if (!validateDateFormat(dateStr)) {
            QTextStream stderr_stream(stderr);
            stderr_stream << "Error: Invalid --date-before format. Use YYYY-MM-DD format" << Qt::endl;
            return false;
        }
        overrideDateBefore = parseDate(dateStr);
        searchCriteriaProvided = true;
    }

    return true;
}

int CommandLineHandler::processCommandLine(QCoreApplication &app)
{
    if (!parseArguments(app)) {
        return 1;
    }

    // Store parsed values
    positionalArgs = parser.positionalArguments();
    requestedAction = positionalArgs.value(0);

    // If no command line action, return special code to continue to GUI
    if (requestedAction.isEmpty()) {
        return -1; // Special code to indicate "continue to GUI"
    }

    if (verbose) {
        QTextStream stdout_stream(stdout);
        stdout_stream << "Katalog Command Line" << Qt::endl;
        stdout_stream << "===================" << Qt::endl;
        stdout_stream << "Action selected: \"" << requestedAction << "\"" << Qt::endl;
    }

    // Initialize collection and database for any command
    if (!initializeDatabase()) {
        // Refused for a reason of its own (collection lock): message printed
        if (m_initExitCode != 0)
            return m_initExitCode;
        QTextStream stderr_stream(stderr);
        stderr_stream << "Error: Failed to initialize database" << Qt::endl;
        return 1;
    }

    // Handle different actions
    if (requestedAction == "search") {
        return cmd_search();
    }
    else if (requestedAction == "list_catalogs") {
        cmd_listGroup0Catalogs();
        return 0;
    }
    else if (requestedAction == "update_catalog") {
        bool deviceIdOk = false;
        int deviceId = positionalArgs.value(1).toInt(&deviceIdOk);
        if (!deviceIdOk) {
            QTextStream stderr_stream(stderr);
            stderr_stream << "Error: Invalid device ID: " << positionalArgs.value(1) << Qt::endl;
            return 1;
        }
        return cmd_updateCatalog(deviceId, false) ? 0 : 1;
    }
    else if (requestedAction == "update_all_active") {
        return cmd_updateAllActive(false) ? 0 : 1;
    }
    else if (requestedAction == "analyze") {
        bool deviceIdOk = false;
        int deviceId = positionalArgs.value(1).toInt(&deviceIdOk);
        if (!deviceIdOk) {
            QTextStream stderr_stream(stderr);
            stderr_stream << "Error: Invalid device ID: " << positionalArgs.value(1) << Qt::endl;
            return 1;
        }
        return cmd_analyze(deviceId);
    }
    else if (requestedAction == "analyze_all") {
        return cmd_analyzeAll();
    }
    else if (requestedAction == "publish") {
        return cmd_publish(positionalArgs.value(1));
    }
    else {
        QTextStream stderr_stream(stderr);
        stderr_stream << "Error: Unknown action: " << requestedAction << Qt::endl;
        return 1;
    }
}

void CommandLineHandler::setCollection(Collection *coll)
{
    // This method allows external setting of collection (for compatibility)
    if (collection == nullptr) {
        collection = coll;
    }
}

bool CommandLineHandler::initializeDatabase()
{
    // Initialize collection object
    collection = new Collection();
    collection->appVersion = QCoreApplication::applicationVersion();

    // Same settings file as the window of the running application
    collection->settingsFilePath = Collection::resolveSettingsFilePath(collection->appVersion);

    QString homePath = QStandardPaths::standardLocations(QStandardPaths::HomeLocation).value(0);

    // Auto-detect database mode if collection path provided
    QString databaseMode;
    QString databaseFilePath;

    if (!collectionPath.isEmpty()) {
        // Auto-detect mode from provided collection path
        databaseMode = autoDetectDatabaseMode(collectionPath);

        if (databaseMode.isEmpty()) {
            QTextStream stderr_stream(stderr);
            stderr_stream << "Error: Could not determine database mode for collection path: " << collectionPath << Qt::endl;
            stderr_stream << "Path should contain either:" << Qt::endl;
            stderr_stream << "  - CSV files (device.csv, storage.csv) for Memory mode" << Qt::endl;
            stderr_stream << "  - Database file (*.db) for File mode" << Qt::endl;
            return false;
        }

        if (verbose) {
            QTextStream stdout_stream(stdout);
            stdout_stream << "Auto-detected database mode: " << databaseMode << Qt::endl;
        }

        // Set up collection paths based on detected mode
        if (databaseMode == "Memory") {
            collection->folder = collectionPath;
        }
        else if (databaseMode == "File") {
            QFileInfo pathInfo(collectionPath);
            if (pathInfo.isFile()) {
                // Path is the .db file itself
                databaseFilePath = collectionPath;
                collection->folder = pathInfo.absolutePath();
            }
            else {
                // Path is directory containing .db file - find the .db file
                QDir dir(collectionPath);
                QStringList dbFiles = dir.entryList(QStringList() << "*.db", QDir::Files);
                if (dbFiles.isEmpty()) {
                    QTextStream stderr_stream(stderr);
                    stderr_stream << "Error: No .db files found in directory: " << collectionPath << Qt::endl;
                    return false;
                }
                databaseFilePath = dir.absoluteFilePath(dbFiles.first());
                collection->folder = collectionPath;

                if (verbose) {
                    QTextStream stdout_stream(stdout);
                    stdout_stream << "Using database file: " << databaseFilePath << Qt::endl;
                }
            }
        }
    }
    else {
        // No collection path provided - use settings (Database::initialize will load from settings)
        QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
        databaseMode = settings.value("Settings/databaseMode").toString();

        if (verbose) {
            QTextStream stdout_stream(stdout);
            stdout_stream << "Loading settings from: " << collection->settingsFilePath << Qt::endl;
            stdout_stream << "Available settings keys: " << settings.allKeys().join(", ") << Qt::endl;
        }

        if (databaseMode.isEmpty()) {
            databaseMode = "Memory"; // Default
        }

        // Load collection paths from settings based on mode
        if (databaseMode == "Memory") {
            collection->folder = settings.value("LastCollectionFolder").toString();
            if (verbose) {
                QTextStream stdout_stream(stdout);
                stdout_stream << "Loaded LastCollectionFolder from settings: '" << collection->folder << "'" << Qt::endl;
            }
            if (collection->folder.isEmpty()) {
                collection->folder = homePath + "/.local/share/katalog";
                if (verbose) {
                    QTextStream stdout_stream(stdout);
                    stdout_stream << "LastCollectionFolder was empty, using default: " << collection->folder << Qt::endl;
                }
            }
        }
        else if (databaseMode == "File") {
            databaseFilePath = settings.value("Settings/DatabaseFilePath").toString();
            if (verbose) {
                QTextStream stdout_stream(stdout);
                stdout_stream << "Loaded DatabaseFilePath from settings: '" << databaseFilePath << "'" << Qt::endl;
            }
            if (databaseFilePath.isEmpty()) {
                QTextStream stderr_stream(stderr);
                stderr_stream << "Error: No database file configured for File mode" << Qt::endl;
                return false;
            }
            QFileInfo dbFileInfo(databaseFilePath);
            collection->folder = dbFileInfo.absolutePath();
        }

        if (verbose) {
            QTextStream stdout_stream(stdout);
            stdout_stream << "Using database mode from settings: " << databaseMode << Qt::endl;
        }
    }

    if (verbose) {
        QTextStream stdout_stream(stdout);
        stdout_stream << "Settings file: " << collection->settingsFilePath << Qt::endl;
        stdout_stream << "Final database mode: " << databaseMode << Qt::endl;
    }

    // Validate paths exist
    if (databaseMode == "Memory") {
        QDir collectionDir(collection->folder);
        if (!collectionDir.exists()) {
            QTextStream stderr_stream(stderr);
            stderr_stream << "Error: Collection folder does not exist: " << collection->folder << Qt::endl;
            return false;
        }

        QString deviceCsvPath = collection->folder + "/device.csv";
        if (!QFile::exists(deviceCsvPath)) {
            QTextStream stderr_stream(stderr);
            stderr_stream << "Error: Collection folder does not contain device.csv: " << collection->folder << Qt::endl;
            return false;
        }
    }
    else if (databaseMode == "File") {
        if (!QFile::exists(databaseFilePath)) {
            QTextStream stderr_stream(stderr);
            stderr_stream << "Error: Database file does not exist: " << databaseFilePath << Qt::endl;
            return false;
        }
    }

    if (verbose) {
        QTextStream stdout_stream(stdout);
        stdout_stream << "Using collection folder: " << collection->folder << Qt::endl;
        if (databaseMode == "File") {
            stdout_stream << "Using database file: " << databaseFilePath << Qt::endl;
        }
    }

    if (verbose) {
        QTextStream stdout_stream(stdout);
        stdout_stream << "=== Database Initialization Debug ===" << Qt::endl;
        stdout_stream << "collectionPath provided: " << (!collectionPath.isEmpty() ? "Yes" : "No") << Qt::endl;
        stdout_stream << "Before Database::initialize():" << Qt::endl;
        stdout_stream << "  databaseMode: " << databaseMode << Qt::endl;
        stdout_stream << "  databaseFilePath: " << databaseFilePath << Qt::endl;
        stdout_stream << "  collectionFolder: " << collection->folder << Qt::endl;
    }

    // A published copy (D43, MDI-F204): opened read-only, nothing written in
    // its folder (no -wal or -shm), and no action that writes runs on it
    m_publishedCopy = databaseMode != QLatin1String("Hosted") && MdiPublish::isPublishedFolder(collection->folder);
    if (m_publishedCopy) {
        collection->readOnly     = true;
        collection->openReadOnly = true;
        MdiData::setReadOnlyStores({ MdiData::filePath(collection->folder) });
    }

    // The collection lock, before anything is written (D43): an action that
    // writes does not run while Teca or another command line run holds it
    if (actionWritesCollection()) {
        m_initExitCode = lockCollection(databaseMode, databaseFilePath);
        if (m_initExitCode != 0)
            return false;
    }

    // Use Database class for initialization with optional overrides
    QSqlError err;
    if (!collectionPath.isEmpty()) {
        // Command line override - pass detected values to override settings
        err = Database::initialize(m_connectionName, collection, databaseMode, databaseFilePath);
    } else {
        // No command line override - use normal settings-based initialization
        // Note: collection->folder was already set from settings above for Memory mode
        err = Database::initialize(m_connectionName, collection);
    }

    if (err.type() != QSqlError::NoError) {
        QTextStream stderr_stream(stderr);
        stderr_stream << "Error: Failed to initialize database: " << err.text() << Qt::endl;
        return false;
    }

    if (verbose) {
        QTextStream stdout_stream(stdout);
        stdout_stream << "After Database::initialize():" << Qt::endl;
        stdout_stream << "  collection->databaseMode: " << collection->databaseMode << Qt::endl;
        stdout_stream << "  collection->databaseFilePath: " << collection->databaseFilePath << Qt::endl;
        stdout_stream << "  collection->folder: " << collection->folder << Qt::endl;

        // Check what database is actually connected
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        stdout_stream << "  Actual database connected: " << db.databaseName() << Qt::endl;
        stdout_stream << "  Database is open: " << (db.isOpen() ? "Yes" : "No") << Qt::endl;

        // Additional verification: check a simple query
        QSqlQuery testQuery(db);
        testQuery.exec("SELECT COUNT(*) FROM device");
        if (testQuery.next()) {
            int deviceCount = testQuery.value(0).toInt();
            stdout_stream << "Device count in connected database: " << deviceCount << Qt::endl;
        }
    }

    // Load collection data only for Memory mode
    if (collection->databaseMode == "Memory") {
        if (verbose) {
            QTextStream stdout_stream(stdout);
            stdout_stream << "Memory mode: Loading collection data from CSV files..." << Qt::endl;
        }
        collection->load();
    } else {
        if (verbose) {
            QTextStream stdout_stream(stdout);
            stdout_stream << collection->databaseMode << " mode: Using persistent database" << Qt::endl;
        }
    }

    if (verbose) {
        QTextStream stdout_stream(stdout);
        stdout_stream << "=== End Database Initialization Debug ===" << Qt::endl;
    }

    return true;
}

QSqlError CommandLineHandler::initializeDatabaseConnection(const QString &connectionName)
{
    // Load database settings (similar to MainWindow::initializeDatabase)
    QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
    collection->databaseMode = settings.value("Settings/databaseMode").toString();
    collection->databaseFilePath = settings.value("Settings/DatabaseFilePath").toString();
    collection->databaseHostName = settings.value("Settings/databaseHostName").toString();
    collection->databaseName = settings.value("Settings/databaseName").toString();
    collection->databasePort = settings.value("Settings/databasePort").toInt();
    collection->databaseUserName = settings.value("Settings/databaseUserName").toString();
    collection->databasePassword = settings.value("Settings/databasePassword").toString();

    // Set defaults if values are not provided
    if (collection->databaseMode.isEmpty())
        collection->databaseMode = "Memory";

    // Prepare database based on selected mode
    QSqlDatabase db;

    if (collection->databaseMode == "Memory") {
        db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db.setDatabaseName(":memory:");
    }
    else if (collection->databaseMode == "File") {
        QFile databaseFile(collection->databaseFilePath);
        if (!databaseFile.exists()) {
            return QSqlError("Database file not found", collection->databaseFilePath, QSqlError::ConnectionError);
        }
        else {
            db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
            db.setDatabaseName(collection->databaseFilePath);
        }
    }
    else if (collection->databaseMode == "Hosted") {
        QString driver;
        if (collection->databaseType == "PostgreSQL") {
            driver = "QPSQL";
        } else {
            driver = "QMYSQL";  // Default to MySQL
        }

        db = QSqlDatabase::addDatabase(driver, connectionName);
        db.setHostName(collection->databaseHostName);
        db.setDatabaseName(collection->databaseName);
        db.setPort(collection->databasePort);
        db.setUserName(collection->databaseUserName);
        db.setPassword(collection->databasePassword);
    }

    // Open the database connection
    if (!db.open()) {
        return db.lastError();
    }

    return QSqlError();
}

void CommandLineHandler::loadCollection()
{
    if (verbose) {
        QTextStream stdout_stream(stdout);
        stdout_stream << "Loading collection..." << Qt::endl;

        // Debug: Check database connection
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        stdout_stream << "Database is open: " << (db.isOpen() ? "Yes" : "No") << Qt::endl;
        stdout_stream << "Database type: " << db.driverName() << Qt::endl;
        stdout_stream << "Database name: " << db.databaseName() << Qt::endl;

        // Check if tables exist
        QStringList tables = db.tables();
        stdout_stream << "Available tables: " << tables.join(", ") << Qt::endl;
    }

    bool loaded = collection->load();

    if (verbose) {
        QTextStream stdout_stream(stdout);
        if (loaded) {
            stdout_stream << "Collection loaded successfully (defaults created)" << Qt::endl;
        } else {
            stdout_stream << "Collection loaded from existing data" << Qt::endl;
        }

        // Debug: Check if device table has any data
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        if (db.isOpen()) {
            QSqlQuery debugQuery(db);
            if (debugQuery.exec("SELECT COUNT(*) FROM device")) {
                if (debugQuery.next()) {
                    int deviceCount = debugQuery.value(0).toInt();
                    stdout_stream << "Total devices in database: " << deviceCount << Qt::endl;
                } else {
                    stdout_stream << "Failed to get device count: " << debugQuery.lastError().text() << Qt::endl;
                }
            } else {
                stdout_stream << "Failed to execute device count query: " << debugQuery.lastError().text() << Qt::endl;
            }

            if (debugQuery.exec("SELECT COUNT(*) FROM device WHERE device_type = 'Catalog'")) {
                if (debugQuery.next()) {
                    int catalogCount = debugQuery.value(0).toInt();
                    stdout_stream << "Total catalogs in database: " << catalogCount << Qt::endl;
                } else {
                    stdout_stream << "Failed to get catalog count: " << debugQuery.lastError().text() << Qt::endl;
                }
            } else {
                stdout_stream << "Failed to execute catalog count query: " << debugQuery.lastError().text() << Qt::endl;
            }
        } else {
            stdout_stream << "Database is not open!" << Qt::endl;
        }
    }
}

void CommandLineHandler::loadLastSearchCriteria()
{
    // NOTE: This method is for Step 2, but keeping it for compatibility
    // For Step 1, we don't actually load from search history

    if (verbose) {
        QTextStream stdout_stream(stdout);
        stdout_stream << "loadLastSearchCriteria() called (Step 1: not used)" << Qt::endl;
    }

    // FIXED: Use searchEngine instead of undefined 'search' variable
    if (!searchEngine) {
        if (verbose) {
            QTextStream stdout_stream(stdout);
            stdout_stream << "No search engine available for loading criteria" << Qt::endl;
        }
        return;
    }

    // Load criteria from the last search in the search history
    searchEngine->loadSearchHistoryCriteria(m_connectionName);

    // Check if any search criteria were loaded
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    QString querySQL = QLatin1String(R"(
        SELECT date_time, text_phrase, search_catalog_checked, search_directory_checked
        FROM search
        ORDER BY date_time DESC
        LIMIT 1
    )");
    query.prepare(querySQL);
    query.exec();

    if (query.next()) {
        QString dateTime = query.value(0).toString();
        QString searchText = query.value(1).toString();
        bool catalogsChecked = query.value(2).toBool();
        bool directoriesChecked = query.value(3).toBool();

        if (verbose) {
            QTextStream stdout_stream(stdout);
            stdout_stream << "Loaded search criteria from: " << dateTime << Qt::endl;
            // Terms are stored joined with '\n'; quote each so the entry stays
            // on one line (SpecSearchList.md SRL-C9).
            QStringList quotedTerms;
            const QStringList terms = searchText.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString &term : terms) {
                const QString trimmed = term.trimmed();
                if (!trimmed.isEmpty())
                    quotedTerms << QLatin1Char('"') + trimmed + QLatin1Char('"');
            }
            stdout_stream << "Search text: " << quotedTerms.join(QLatin1String(", ")) << Qt::endl;
            stdout_stream << "Search in catalogs: " << (catalogsChecked ? "Yes" : "No") << Qt::endl;
            stdout_stream << "Search in directories: " << (directoriesChecked ? "Yes" : "No") << Qt::endl;
        }
    } else {
        if (verbose) {
            QTextStream stdout_stream(stdout);
            stdout_stream << "No previous search found in history" << Qt::endl;
        }
    }
}

void CommandLineHandler::sendSearchParametersFromSearchHistory(Search *search)
{
    if (!search) return;

    // Setting search parameters for 'return all files'
    bool tempSearchSkipCriteria = false;
    if (tempSearchSkipCriteria) {
        // ... existing code for skipping criteria ...
    }
    else {
        QTextStream stdout_stream(stdout);
        stdout_stream << "Setting search parameters from search history..." << Qt::endl;

        // Get the latest search date first and set it on the search object
        QSqlQuery query(QSqlDatabase::database(m_connectionName));
        QString querySQL = QLatin1String(R"(
            SELECT date_time
            FROM search
            ORDER BY date_time DESC
            LIMIT 1
        )");
        query.prepare(querySQL);
        query.exec();

        if (query.next()) {
            QString latestSearchDateTime = query.value(0).toString();
            search->searchDateTime = latestSearchDateTime;

            if (verbose) {
                stdout_stream << "Loading search criteria from: " << latestSearchDateTime << Qt::endl;
            }
        } else {
            if (verbose) {
                stdout_stream << "No previous search found in history" << Qt::endl;
            }
            search->searchDateTime = "";
        }

        // Load from search history (gets the ready-to-use fields)
        if (useSearchHistory) {
            search->loadSearchHistoryCriteria(m_connectionName);
        }

        // Apply command line overrides (this overrides history values)
        if (searchCriteriaProvided) {
            if (verbose) {
                stdout_stream << "Applying command line search criteria overrides..." << Qt::endl;
            }

            // Apply text search overrides
            if (parser.isSet("text")) {
                search->searchOnFileName = true;
                search->searchText = overrideSearchText;
                if (verbose) stdout_stream << "  Override search text: \"" << search->searchText << "\"" << Qt::endl;
            }

            // Apply file type override
            if (parser.isSet("type")) {
                search->searchOnType = (overrideFileType != "all");
                search->selectedFileType = (overrideFileType == "all") ? "All" :
                                               overrideFileType.left(1).toUpper() + overrideFileType.mid(1);
                if (verbose) stdout_stream << "  Override file type: " << search->selectedFileType << Qt::endl;
            }

            // Apply size overrides
            if (parser.isSet("size-min") || parser.isSet("size-max")) {
                search->searchOnSize = true;
                if (parser.isSet("size-min")) {
                    parseSizeValue(overrideSizeMin, search->selectedMinimumSize, search->selectedMinSizeUnit);
                }
                if (parser.isSet("size-max")) {
                    parseSizeValue(overrideSizeMax, search->selectedMaximumSize, search->selectedMaxSizeUnit);
                }
                search->setMultipliers();
                if (verbose) stdout_stream << "  Override size range: "
                                  << search->selectedMinimumSize << search->selectedMinSizeUnit
                                  << " - " << search->selectedMaximumSize << search->selectedMaxSizeUnit << Qt::endl;
            }

            // Apply date overrides
            if (parser.isSet("date-after") || parser.isSet("date-before")) {
                search->searchOnDate = true;

                if (parser.isSet("date-after")) {
                    search->selectedDateMin = overrideDateAfter;
                }
                if (parser.isSet("date-before")) {
                    search->selectedDateMax = overrideDateBefore;
                }

                if (verbose) {
                    stdout_stream << "  Override date range: "
                                  << search->selectedDateMin.toString("yyyy-MM-dd")
                                  << " to " << search->selectedDateMax.toString("yyyy-MM-dd") << Qt::endl;
                }
            }

            // Apply case sensitive override
            if (parser.isSet("case-sensitive")) {
                search->caseSensitive = true;
                if (verbose) stdout_stream << "  Override case sensitive: enabled" << Qt::endl;
            }
        }

        // Set selected devices (use CLI device selection)
        search->selectedDeviceIDList.clear();
        if (selectedDevice && selectedDevice->ID != 0) {
            search->selectedDeviceIDList.append(selectedDevice->ID);
        } else {
            search->selectedDeviceIDList.append(0);  // All devices
        }

        // ... rest of existing code ...
    }
}

Device* CommandLineHandler::getSelectedDevice()
{
    Device *device = new Device();

    if (selectedDeviceIDProvided) {
        // --selectedDeviceID was explicitly provided: use it regardless of collection source
        device->ID = selectedDeviceID;

        if (verbose) {
            QTextStream stdout_stream(stdout);
            stdout_stream << "Using selectedDeviceID from command line: ID=" << device->ID;
            if (collectionPath.isEmpty()) {
                stdout_stream << " (with collection from settings file)";
            } else {
                stdout_stream << " (with collection from --collection)";
            }
            stdout_stream << Qt::endl;
        }
    } else if (collectionPath.isEmpty()) {
        // No --collection and no --selectedDeviceID: use selectedDevice from settings file
        QSettings settings(collection->settingsFilePath, QSettings::IniFormat);
        device->ID = settings.value("Selection/SelectedDeviceID", 0).toInt();

        if (verbose) {
            QTextStream stdout_stream(stdout);
            stdout_stream << "Using selectedDevice from settings file: ID=" << device->ID << Qt::endl;
        }
    } else {
        // --collection specified but no --selectedDeviceID: default to 0 (All)
        device->ID = 0;

        if (verbose) {
            QTextStream stdout_stream(stdout);
            stdout_stream << "Using default selectedDeviceID=0 (All) with collection from --collection" << Qt::endl;
        }
    }

    // Load device details
    if (device->ID == 0) {
        device->type = "All";
        device->name = "All Devices";
    }

    device->loadDevice(m_connectionName);

    return device;
}

//--- Commands ---------------------------------------------------------------------------
bool CommandLineHandler::cmd_updateCatalog(int deviceId, bool displayReport)
{
    Q_UNUSED(displayReport); // TODO: Implement reporting for command line

    qInfo() << "Updating device:" << deviceId;

    // Load and validate the device
    selectedDevice = new Device();
    selectedDevice->ID = deviceId;
    selectedDevice->loadDevice(m_connectionName);
    selectedDevice->updateActiveState(m_connectionName);

    if (selectedDevice->type != "Catalog") {
        qInfo() << "The device selected must be a Catalog. Try with a different device ID";
        qInfo() << "Device ID:" << selectedDevice->ID;
        qInfo() << "Device Name:" << selectedDevice->name;
        qInfo() << "Device Type:" << selectedDevice->type;
        delete selectedDevice;
        selectedDevice = nullptr;
        return false;
    }

    if (!selectedDevice->active) {
        qInfo() << "";
        qInfo() << "The Catalog was not updated as it is not active.";
        qInfo() << "Device ID:" << selectedDevice->ID;
        qInfo() << "Device Name:" << selectedDevice->name;
        delete selectedDevice;
        selectedDevice = nullptr;
        return false;
    }

    qInfo() << "-----------------------------------------------------------------------";
    qInfo() << "Catalog values prior to update:";
    qInfo() << "Catalog ID:" << selectedDevice->ID;
    qInfo() << "Catalog Name:" << selectedDevice->name;
    qInfo() << "Catalog Path:" << selectedDevice->path;
    qInfo() << "Catalog Type:" << selectedDevice->type;
    qInfo() << "Catalog Size:" << selectedDevice->totalFileSize;
    qInfo() << "Catalog Files:" << selectedDevice->totalFileCount;
    qInfo() << "Catalog update date:" << selectedDevice->dateTimeUpdated.toString();

    // Use the SAME approach as UI - create CatalogManager
    qInfo() << "Starting catalog update using CatalogManager (same as UI)...";

    CatalogManager* catalogManager = new CatalogManager(this);
    CatalogJobStoppable* catalogJobStoppable = new CatalogJobStoppable(this);

    // Simple event loop to wait for completion (same pattern as UI)
    QEventLoop loop;

    // Connect to the same signals as UI, and record the outcome for the exit code
    bool finished = false;
    bool succeeded = false;
    QString failureMessage;
    connect(catalogManager, &CatalogManager::catalogOperationCompleted, &loop, [&]() {
        finished = true;
        succeeded = true;
        loop.quit();
    });
    connect(catalogManager, &CatalogManager::catalogOperationError, &loop, [&](const QString &error) {
        finished = true;
        failureMessage = error;
        loop.quit();
    });
    connect(catalogManager, &CatalogManager::catalogOperationCancelled, &loop, [&]() {
        finished = true;
        failureMessage = "Operation cancelled";
        loop.quit();
    });

    // Timeout protection
    QTimer::singleShot(60 * 60 * 1000, &loop, &QEventLoop::quit); // 1 hour timeout

    // Start catalog operation (SAME as UI)
    catalogManager->startCatalogJobStoppable(
        catalogJobStoppable,
        selectedDevice,
        CatalogJobStoppable::UpdateCatalog,
        collection->databaseMode,
        collection->folder
        );

    qInfo() << "Waiting for catalog update to complete...";
    // An invalid configuration is reported before the loop starts
    if (!finished)
        loop.exec();

    if (!finished && catalogManager->catalogOperationRunning()) {
        // Still running = timeout
        qInfo() << "---";
        qWarning() << "WARNING: Catalog update FAILED: Operation timed out";
        catalogManager->stopCatalogOperation();
    } else if (!succeeded) {
        qInfo() << "---";
        qWarning() << "WARNING: Catalog update FAILED:" << failureMessage;
    } else {
        qInfo() << "Catalog operation completed";

        // Reload device to get updated statistics
        selectedDevice->loadDevice(m_connectionName);

        // Save collection data (same as UI)
        collection->saveDeviceTableToFile();
        collection->saveStatiticsTableToFile();

        qInfo() << "---";
        qInfo() << "Catalog updated successfully.";
        qInfo() << "Catalog ID:" << selectedDevice->ID;
        qInfo() << "Catalog Name:" << selectedDevice->name;
        qInfo() << "Catalog Path:" << selectedDevice->path;
        qInfo() << "Catalog Type:" << selectedDevice->type;
        qInfo() << "Catalog Size:" << selectedDevice->totalFileSize;
        qInfo() << "Catalog Files:" << selectedDevice->totalFileCount;
        qInfo() << "Catalog update date:" << selectedDevice->dateTimeUpdated.toString();
    }

    // Clean up
    catalogManager->deleteLater();
    delete selectedDevice;
    selectedDevice = nullptr;

    return succeeded;
}

bool CommandLineHandler::cmd_updateAllActive(bool displayReport)
{
    qInfo() << "Starting update of all active catalogs...";

    // Select all active catalog devices from database
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    QString querySQL = QLatin1String(R"(
                           SELECT device_id
                           FROM device
                           WHERE device_type = 'Catalog'
                           AND device_active = 1
                           ORDER BY device_id
                       )");
    query.prepare(querySQL);

    if (!query.exec()) {
        qWarning() << "WARNING: Error querying active catalogs:" << query.lastError().text();
        return false;
    }

    // Collect all device IDs first
    QList<int> deviceIDs;
    while (query.next()) {
        deviceIDs.append(query.value(0).toInt());
    }

    if (deviceIDs.isEmpty()) {
        qInfo() << "No active catalogs found to update.";
        return true;
    }

    qInfo() << QString("Found %1 active catalog(s) to update.").arg(deviceIDs.size());

    // Global statistics tracking (same as UI batch system)
    int successCount = 0;
    int errorCount = 0;
    QStringList errorCatalogs;

    // Update each catalog sequentially
    for (int i = 0; i < deviceIDs.size(); ++i) {
        int deviceID = deviceIDs[i];

        qInfo() << "-----------------------------------------------------------------------";
        qInfo() << QString("Processing catalog %1 of %2 (ID: %3)")
                        .arg(i + 1)
                        .arg(deviceIDs.size())
                        .arg(deviceID);

        // Load device info for logging
        Device tempDevice;
        tempDevice.ID = deviceID;
        tempDevice.loadDevice(m_connectionName);

        qInfo() << "Catalog name:" << tempDevice.name;

        try {
            // Use the updated single catalog method
            if (cmd_updateCatalog(deviceID, displayReport)) {
                successCount++;
                qInfo() << "✓ Catalog" << deviceID << "updated successfully";
            } else {
                errorCount++;
                errorCatalogs.append(QString("%1 (%2)").arg(tempDevice.name).arg(deviceID));
                qWarning() << "WARNING: ✗ Catalog" << deviceID << "failed";
            }

        } catch (const std::exception& e) {
            errorCount++;
            errorCatalogs.append(QString("%1 (%2)").arg(tempDevice.name).arg(deviceID));
            qWarning() << "WARNING: ✗ Catalog" << deviceID << "failed:" << e.what();

        } catch (...) {
            errorCount++;
            errorCatalogs.append(QString("%1 (%2)").arg(tempDevice.name).arg(deviceID));
            qWarning() << "WARNING: ✗ Catalog" << deviceID << "failed with unknown error";
        }
    }

    // Final summary report
    qInfo() << "=======================================================================";
    qInfo() << "BATCH UPDATE SUMMARY";
    qInfo() << "=======================================================================";
    qInfo() << "Total catalogs processed:" << deviceIDs.size();
    qInfo() << "Successful updates:" << successCount;
    qWarning() << "WARNING: Failed updates:" << errorCount;

    if (!errorCatalogs.isEmpty()) {
        qInfo() << "";
        qWarning() << "WARNING: Failed catalogs:";
        for (const QString& errorCatalog : errorCatalogs) {
            qInfo() << "  -" << errorCatalog;
        }
    }

    qInfo() << "=======================================================================";
    qInfo() << "All active catalogs processing completed.";
    qInfo() << "=======================================================================";

    return errorCount == 0;
}

void CommandLineHandler::cmd_listGroup0Catalogs()
{
    if (verbose) {
        QTextStream stdout_stream(stdout);
        stdout_stream << "Executing catalog list query..." << Qt::endl;
    }

    // Check database connection first
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.isOpen()) {
        qInfo() << "Database is not open!";
        return;
    }

    // Query the database for all devices of type catalog
    QSqlQuery query(db);
    QString querySQL = QLatin1String(R"(
                                    SELECT device_id, device_name, device_active
                                    FROM device
                                    WHERE device_type = 'Catalog'
                                    ORDER BY device_id
                                )");

    // Don't use prepare() for this simple query to avoid parameter issues
    if (!query.exec(querySQL)) {
        qWarning() << "WARNING: Failed to execute catalog list query:" << query.lastError().text();
        qInfo() << "Query was:" << querySQL;
        return;
    }

    qInfo() << "-----------------------------------------------------------------------";
    qInfo() << "Catalogs";
    qInfo() << "-----------------------------------------------------------------------";
    qInfo() << "Device ID" << "    Active" << "      Device Name:";

    int catalogCount = 0;
    // Iterate through the results and print the device ID and name
    while (query.next()) {
        int deviceID = query.value(0).toInt();
        QString deviceName = query.value(1).toString();
        bool deviceActive = query.value(2).toBool();
        qInfo() << "  " << deviceID << "         " << deviceActive << "      " << deviceName;
        catalogCount++;
    }

    if (catalogCount == 0) {
        qInfo() << "  No catalogs found in the collection.";
        qInfo() << "  Check if the collection path is correct and contains catalog data.";

        // Debug: Show all devices
        if (verbose) {
            QTextStream stdout_stream(stdout);
            stdout_stream << "Debug: Listing all devices..." << Qt::endl;
            QSqlQuery allQuery(db);
            if (allQuery.exec("SELECT device_id, device_name, device_type FROM device")) {
                while (allQuery.next()) {
                    stdout_stream << "  ID: " << allQuery.value(0).toInt()
                    << ", Name: " << allQuery.value(1).toString()
                    << ", Type: " << allQuery.value(2).toString() << Qt::endl;
                }
            }
        }
    }

    qInfo() << "-----------------------------------------------------------------------";
}

 int CommandLineHandler::cmd_search()
{
    if (verbose) {
        QTextStream stdout_stream(stdout);
        stdout_stream << "Executing search..." << Qt::endl;
    }

    // Create SearchManager (same as UI)
    if (!searchManager) {
        searchManager = new SearchManager(this);

        // Connect signals for synchronous execution
        connect(searchManager, &SearchManager::searchCompleted,
                this, &CommandLineHandler::onSearchCompleted);
        connect(searchManager, &SearchManager::searchCancelled,
                this, &CommandLineHandler::onSearchCancelled);
        connect(searchManager, &SearchManager::searchError,
                this, &CommandLineHandler::onSearchError);
    }

    // Create SearchJobStoppable (same as UI)
    searchEngine = new SearchJobStoppable(this);
    searchEngine->setDatabaseConnection(m_connectionName);

    // Enable memory mode if collection is in Memory mode (same as UI)
    if (collection->databaseMode == "Memory") {
        if (verbose) {
            QTextStream stdout_stream(stdout);
            stdout_stream << "Memory mode: Enabling memory mode for SearchJobStoppable" << Qt::endl;
        }
        searchEngine->setMemoryModeEnabled(true);
    }

    // Connect progress for verbose output
    if (verbose) {
        connect(searchEngine, &Search::searchProgress, this, &CommandLineHandler::handleSearchProgress);
    }

    // FIXED: Get a single catalog device instead of virtual "All" device
    selectedDevice = getSelectedDevice();

    if (selectedDevice->ID != 0) {
        // Validate that specific device exists
        QSqlQuery checkQuery(QSqlDatabase::database(m_connectionName));
        checkQuery.prepare("SELECT device_name, device_type FROM device WHERE device_id = :id");
        checkQuery.bindValue(":id", selectedDevice->ID);

        if (!checkQuery.exec() || !checkQuery.next()) {
            QTextStream stderr_stream(stderr);
            stderr_stream << "Error: Device ID " << selectedDevice->ID << " not found" << Qt::endl;
            return 1;
        }

        if (verbose) {
            QTextStream stdout_stream(stdout);
            stdout_stream << "Found device - ID: " << selectedDevice->ID
                          << ", Name: " << checkQuery.value(0).toString()
                          << ", Type: " << checkQuery.value(1).toString() << Qt::endl;
        }
    }

    if (!selectedDevice) {
        QTextStream stderr_stream(stderr);
        stderr_stream << "Error: No active catalog devices found" << Qt::endl;
        return 1;
    }

    if (verbose) {
        QTextStream stdout_stream(stdout);
        stdout_stream << "Selected device - ID: " << selectedDevice->ID
                      << ", Name: " << selectedDevice->name
                      << ", Type: " << selectedDevice->type << Qt::endl;
    }

    // Set search parameters
    sendSearchParametersFromSearchHistory(searchEngine);

    if (verbose) {
        QTextStream stdout_stream(stdout);
        stdout_stream << "Starting search via SearchManager..." << Qt::endl;
    }

    // Start search via SearchManager - this should now work!
    searchManager->startSearchJobStoppable(searchEngine, selectedDevice);

    // Wait for search completion (CLI-specific)
    eventLoop = new QEventLoop(this);
    eventLoop->exec();  // Blocks until onSearchCompleted/Cancelled/Error

    // Clean up
    delete eventLoop;
    eventLoop = nullptr;

    return searchResult;
}

//--- Process ----------------------------------------------------------------------------
void CommandLineHandler::onSearchCompleted()
{
    if (verbose) {
        QTextStream stdout_stream(stdout);
        stdout_stream << Qt::endl << "Search completed successfully." << Qt::endl;

        // Get results from SearchManager
        Search* currentSearch = searchManager->getCurrentSearch();
        if (currentSearch) {
            stdout_stream << "Files found: " << currentSearch->filesFoundNumber << Qt::endl;
            stdout_stream << "Total size: " << currentSearch->filesFoundTotalSize << " octets" << Qt::endl;
        }
    }

    // Output results
    outputSearchResults();

    searchResult = 0;  // Success
    searchCompleted = true;
    if (eventLoop) {
        eventLoop->quit();
    }
}

void CommandLineHandler::onSearchCancelled()
{
    if (verbose) {
        QTextStream stdout_stream(stdout);
        stdout_stream << Qt::endl << "Search was cancelled." << Qt::endl;
    }

    searchResult = 1;  // Error
    searchCompleted = true;
    if (eventLoop) {
        eventLoop->quit();
    }
}

void CommandLineHandler::onSearchError(const QString &error)
{
    QTextStream stderr_stream(stderr);
    stderr_stream << "Search error: " << error << Qt::endl;

    searchResult = 1;  // Error
    searchCompleted = true;
    if (eventLoop) {
        eventLoop->quit();
    }
}

void CommandLineHandler::handleSearchProgress(int filesProcessed)
{
    QTextStream stdout_stream(stdout);

    // Handle special progress values
    if (filesProcessed == -2) {
        stdout_stream << "Loading catalog..." << Qt::flush;
        return;
    } else if (filesProcessed == -3) {
        stdout_stream << "Processing..." << Qt::flush;
        return;
    } else if (filesProcessed == -4) {
        stdout_stream << "." << Qt::flush;
        return;
    }

    // Regular progress
    if (filesProcessed > 0 && filesProcessed % 1000 == 0) {
        stdout_stream << "." << Qt::flush;
    }
}

QString CommandLineHandler::formatDeviceIDList(const QList<int> &deviceIDs)
{
    if (deviceIDs.isEmpty()) {
        return "[]";
    }

    QStringList stringList;
    for (int id : deviceIDs) {
        stringList << QString::number(id);
    }
    return "[" + stringList.join(", ") + "]";
}

void CommandLineHandler::outputSearchResults()
{
    Search* currentSearch = searchManager->getCurrentSearch();
    if (!currentSearch) {
        QTextStream stdout_stream(stdout);
        stdout_stream << "No search results available." << Qt::endl;
        return;
    }

    outputSearchResultsStdout();
}

void CommandLineHandler::outputSearchResultsStdout()
{
    Search* currentSearch = searchManager->getCurrentSearch();
    if (!currentSearch) {
        return;
    }

    QTextStream stdout_stream(stdout);

    if (currentSearch->filesFoundNumber == 0) {
        stdout_stream << Qt::endl << "No files found." << Qt::endl;
        return;
    }

    stdout_stream << Qt::endl << "Search Results:" << Qt::endl;
    stdout_stream << "===============" << Qt::endl;

    // Apply limit if specified
    int maxDisplay = currentSearch->fileNames.size();
    if (outputLimit > 0 && outputLimit < maxDisplay) {
        maxDisplay = outputLimit;
        if (verbose) {
            stdout_stream << "Showing first " << maxDisplay << " files (limited by --limit option)" << Qt::endl;
        }
    }

    // Output files up to maxDisplay
    for (int i = 0; i < maxDisplay; ++i) {
        QString catalogName = "";
        QString fileName = "";
        QString filePath = "";
        QString fullPath = "";
        QString fileSize = "";
        QString fileDate = "";

        // Get catalog name
        if (i < currentSearch->fileCatalogs.size()) {
            catalogName = currentSearch->fileCatalogs[i];
        }

        // Get file name
        if (i < currentSearch->fileNames.size()) {
            fileName = currentSearch->fileNames[i];
        }

        // Get file path and construct full path
        if (i < currentSearch->filePaths.size()) {
            filePath = currentSearch->filePaths[i];
            // Construct full path: path + "/" + filename
            if (filePath.endsWith("/")) {
                fullPath = filePath + fileName;
            } else {
                fullPath = filePath + "/" + fileName;
            }
        }

        // Get file size
        if (i < currentSearch->fileSizes.size()) {
            fileSize = QString::number(currentSearch->fileSizes[i]);
        }

        // Get file date
        if (i < currentSearch->fileDateTimes.size()) {
            fileDate = currentSearch->fileDateTimes[i];
        }

        // Output in tab-separated format: catalog_name	file_full_path	size	date
        stdout_stream << catalogName << "\t" << fullPath << "\t" << fileSize << "\t" << fileDate << Qt::endl;
    }

    // Show summary with limit info
    if (outputLimit > 0 && currentSearch->fileNames.size() > outputLimit) {
        stdout_stream << Qt::endl;
        stdout_stream << "... and " << (currentSearch->fileNames.size() - outputLimit) << " more files (use --limit 0, or remove option --limit to show all)" << Qt::endl;
    }

    stdout_stream << Qt::endl;
    stdout_stream << "Total: " << currentSearch->filesFoundNumber << " files, "
                  << currentSearch->filesFoundTotalSize << " bytes" << Qt::endl;
}

QString CommandLineHandler::generateTimestamp()
{
    return QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
}

QString CommandLineHandler::autoDetectDatabaseMode(const QString &path)
{
    if (path.isEmpty()) {
        return "";  // Let caller handle settings fallback
    }

    QFileInfo pathInfo(path);

    if (verbose) {
        QTextStream stdout_stream(stdout);
        stdout_stream << "Auto-detecting database mode for: " << path << Qt::endl;
    }

    // Case 1: Path is a .db file -> File mode
    if (pathInfo.isFile() && pathInfo.suffix().toLower() == "db") {
        if (verbose) {
            QTextStream stdout_stream(stdout);
            stdout_stream << "Detected File mode: path is a .db file" << Qt::endl;
        }
        return "File";
    }

    // Case 2: Path is a directory -> check contents
    if (pathInfo.isDir()) {
        QDir dir(path);

        // Look for Memory mode indicators (.csv files)
        QStringList csvFiles = dir.entryList(QStringList() << "device.csv" << "storage.csv", QDir::Files);
        bool hasMemoryFiles = csvFiles.contains("device.csv") && csvFiles.contains("storage.csv");

        // Look for File mode indicators (.db files)
        QStringList dbFiles = dir.entryList(QStringList() << "*.db", QDir::Files);
        bool hasDbFiles = !dbFiles.isEmpty();

        if (verbose) {
            QTextStream stdout_stream(stdout);
            stdout_stream << "Directory analysis:" << Qt::endl;
            stdout_stream << "  Memory mode files (device.csv, storage.csv): " << (hasMemoryFiles ? "Found" : "Not found") << Qt::endl;
            stdout_stream << "  Database files (*.db): " << (hasDbFiles ? "Found" : "Not found") << Qt::endl;
        }

        // Priority: Memory mode if CSV files found
        if (hasMemoryFiles) {
            if (verbose) {
                QTextStream stdout_stream(stdout);
                stdout_stream << "Detected Memory mode: found required CSV files" << Qt::endl;
            }
            return "Memory";
        }

        // Fallback: File mode if .db files found
        if (hasDbFiles) {
            if (verbose) {
                QTextStream stdout_stream(stdout);
                stdout_stream << "Detected File mode: found .db files in directory" << Qt::endl;
            }
            return "File";
        }

        // Could add Hosted mode detection here if there's a config file pattern
        // e.g., if (dir.exists("connection.conf")) return "Hosted";
    }

    if (verbose) {
        QTextStream stdout_stream(stdout);
        stdout_stream << "Could not auto-detect database mode from path" << Qt::endl;
    }

    return "";  // Could not determine
}

bool CommandLineHandler::validateSizeFormat(const QString &sizeStr)
{
    // Regular expression to match formats like: 1, 1B, 1KB, 1.5MB, 1GB, etc.
    QRegularExpression sizeRegex("^\\d+(\\.\\d+)?(B|KB|MB|GB|TB|KiB|MiB|GiB|TiB)?$",
                                 QRegularExpression::CaseInsensitiveOption);
    return sizeRegex.match(sizeStr).hasMatch();
}

void CommandLineHandler::parseSizeValue(const QString &sizeStr, qint64 &value, QString &unit)
{
    // Extract numeric part and unit part
    QRegularExpression regex("^(\\d+(?:\\.\\d+)?)(.*)?$", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = regex.match(sizeStr);

    if (match.hasMatch()) {
        double numericValue = match.captured(1).toDouble();
        QString unitStr = match.captured(2).toUpper();

        // Default to bytes if no unit specified
        if (unitStr.isEmpty()) {
            unitStr = "BYTES";
        }

        // Map common units to internal constants
        if (unitStr == "B" || unitStr == "BYTES") {
            unit = Search::SIZE_UNIT_BYTES;
            value = static_cast<qint64>(numericValue);
        }
        else if (unitStr == "KB" || unitStr == "KIB") {
            unit = Search::SIZE_UNIT_KIB;
            value = static_cast<qint64>(numericValue);
        }
        else if (unitStr == "MB" || unitStr == "MIB") {
            unit = Search::SIZE_UNIT_MIB;
            value = static_cast<qint64>(numericValue);
        }
        else if (unitStr == "GB" || unitStr == "GIB") {
            unit = Search::SIZE_UNIT_GIB;
            value = static_cast<qint64>(numericValue);
        }
        else if (unitStr == "TB" || unitStr == "TIB") {
            unit = Search::SIZE_UNIT_TIB;
            value = static_cast<qint64>(numericValue);
        }
        else {
            // Default fallback
            unit = Search::SIZE_UNIT_BYTES;
            value = static_cast<qint64>(numericValue);
        }
    }
    else {
        // Fallback for invalid format
        unit = Search::SIZE_UNIT_BYTES;
        value = 0;
    }
}

bool CommandLineHandler::validateDateFormat(const QString &dateStr)
{
    // Accept YYYY-MM-DD format
    QDate date = QDate::fromString(dateStr, "yyyy-MM-dd");
    return date.isValid();
}

QDateTime CommandLineHandler::parseDate(const QString &dateStr)
{
    QDate date = QDate::fromString(dateStr, "yyyy-MM-dd");
    if (date.isValid()) {
        // Create QDateTime with the date and start of day (00:00:00)
        return QDateTime(date, QTime(0, 0, 0));
    }
    return QDateTime(); // Invalid datetime
}

//--- AI analysis (fork, D43) --------------------------------------------------------------
namespace {
volatile std::sig_atomic_t s_stopSignal = 0;

void onStopSignal(int signalNumber)
{
    // Only a flag here: the waiting loop asks the runner to stop. A second
    // signal ends the program at once.
    s_stopSignal = signalNumber;
    std::signal(signalNumber, SIG_DFL);
}

const QString kCliMdiConnection = QStringLiteral("cli_mdi");
const QString kSeparator = QStringLiteral("-----------------------------------------------------------------------");
const QString kDoubleSeparator = QStringLiteral("=======================================================================");
}

void CommandLineHandler::installStopSignalHandlers()
{
    std::signal(SIGINT,  onStopSignal);
    std::signal(SIGTERM, onStopSignal);
}

int CommandLineHandler::stopSignal()
{
    return s_stopSignal;
}

bool CommandLineHandler::actionWritesCollection() const
{
    return requestedAction == QLatin1String("update_catalog")
        || requestedAction == QLatin1String("update_all_active")
        || requestedAction == QLatin1String("analyze")
        || requestedAction == QLatin1String("analyze_all")
        || requestedAction == QLatin1String("publish");
}

int CommandLineHandler::lockCollection(const QString &databaseMode, const QString &databaseFilePath)
{
    // The lock and the writers of a File- or Memory-mode collection (D43);
    // a hosted collection has no folder to hold them in
    if (databaseMode == QLatin1String("Hosted") || collection->folder.isEmpty())
        return 0;

    QTextStream stderr_stream(stderr);
    const QString path = Collection::lockFilePath(collection->folder);

    // A published copy is read-only on every computer (MDI-F204)
    if (m_publishedCopy) {
        stderr_stream << "Error: This collection is a published copy for the read-only Macs, so it may not be changed."
                      << " Nothing was done." << Qt::endl;
        return ExitNotWritable;
    }

    // A protected collection is changed only by its writers, read from its
    // files before anything is opened for writing
    const bool mayWrite = databaseMode == QLatin1String("Memory")
                              ? CollectionWriters::mayThisComputerWriteFolder(collection->folder)
                              : CollectionWriters::mayThisComputerWriteFile(databaseFilePath);
    if (!mayWrite) {
        stderr_stream << "Error: This computer is not among the writers of this collection, so it may not change it."
                      << " Nothing was done." << Qt::endl;
        return ExitNotWritable;
    }

    // The same lock as Teca's window (CollectionLock), kept young while the
    // run lasts so that no other computer takes it over
    m_collectionLock = std::make_unique<CollectionLock>();
    switch (m_collectionLock->acquire(collection->folder)) {
    case CollectionLock::Result::Acquired:
        m_lockRefreshTimer = new QTimer(this);
        m_lockRefreshTimer->setInterval(CollectionLock::kRefreshSeconds * 1000);
        connect(m_lockRefreshTimer, &QTimer::timeout, this, &CommandLineHandler::refreshCollectionLock);
        m_lockRefreshTimer->start();
        if (verbose) {
            QTextStream stdout_stream(stdout);
            stdout_stream << "Collection lock taken: " << path << Qt::endl;
        }
        return 0;
    case CollectionLock::Result::HeldElsewhere:
        stderr_stream << "Error: The collection is in use by Teca or by another command line run"
                      << " (" << m_collectionLock->holderApp() << ", process " << m_collectionLock->holderPid()
                      << " on " << m_collectionLock->holderHost() << "). Nothing was done." << Qt::endl;
        stderr_stream << "Lock file: " << path << Qt::endl;
        m_collectionLock.reset();
        return ExitCollectionInUse;
    case CollectionLock::Result::NotPossible:
        break;
    }
    // The folder cannot hold a lock file (a read-only share, for instance)
    m_collectionLock.reset();
    stderr_stream << "Error: The collection folder cannot be written to, so this computer may not change the collection."
                  << " Nothing was done." << Qt::endl;
    stderr_stream << "Lock file: " << path << Qt::endl;
    return ExitNotWritable;
}

void CommandLineHandler::refreshCollectionLock()
{
    if (!m_collectionLock || m_lockLost || m_collectionLock->refresh())
        return;
    // Taken over by another process (this computer slept longer than the
    // lock lasts, for instance): the analysis stops, nothing more is written
    m_lockLost = true;
    QTextStream stderr_stream(stderr);
    stderr_stream << "Error: The collection lock was taken over"
                  << " (" << m_collectionLock->holderApp() << ", process " << m_collectionLock->holderPid()
                  << " on " << m_collectionLock->holderHost() << "): the analysis stops." << Qt::endl;
}

bool CommandLineHandler::prepareAnalysis()
{
    // As when Teca opens the collection: schema checks, the AI tables, and in
    // Memory mode the saved profiles (DatabaseManager::runMigrations, after
    // the collection files were loaded)
    const QSqlError err = DatabaseManager::runMigrations(m_connectionName, collection);
    if (err.type() != QSqlError::NoError) {
        QTextStream stderr_stream(stderr);
        stderr_stream << "Error: The collection could not be prepared for the AI analysis: " << err.text() << Qt::endl;
        return false;
    }
    return true;
}

void CommandLineHandler::reportAnalysisTools(const QVariantMap &tools)
{
    QTextStream stdout_stream(stdout);

    // A step whose program or model is missing is skipped by the runner,
    // with the reason on its line (MDI-F20)
    const QList<QPair<QString, QString>> needed = {
        { QStringLiteral("ffmpeg"),              QStringLiteral("ffmpeg") },
        { QStringLiteral("ffprobe"),             QStringLiteral("ffprobe") },
        { QStringLiteral("pdftotext"),           QStringLiteral("pdftotext") },
        { QStringLiteral("whisper"),             QStringLiteral("whisper-cli") },
        { QStringLiteral("whisperModel"),        QStringLiteral("whisper model") },
        { QStringLiteral("exiv2"),               QStringLiteral("exiv2") },
        { QStringLiteral("pdftoppm"),            QStringLiteral("pdftoppm") },
        { QStringLiteral("tesseract"),           QStringLiteral("tesseract") },
        { QStringLiteral("tessdata"),            QStringLiteral("tesseract language data") },
        { QStringLiteral("faceDetectorModel"),   QStringLiteral("face detection model") },
        { QStringLiteral("faceRecognizerModel"), QStringLiteral("face recognition model") },
    };
    QStringList missing;
    for (const auto &tool : needed) {
        const QString path = tools.value(tool.first).toString();
        if (path.isEmpty())
            missing << tool.second;
        else if (verbose)
            stdout_stream << "Tool " << tool.second << ": " << path << Qt::endl;
    }
    if (tools.value(QStringLiteral("opencv")).toString().isEmpty())
        missing << QStringLiteral("OpenCV");
    if (!missing.isEmpty())
        stdout_stream << "Not found: " << missing.join(QStringLiteral(", "))
                      << " (the steps that need them are skipped)" << Qt::endl;

    // Ollama, for the AI steps (captions, summaries, topic segments, embeddings...)
    const QString url = tools.value(QStringLiteral("ollamaUrl")).toString();
    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(url + QStringLiteral("/api/tags")));
    request.setTransferTimeout(3000);
    QNetworkReply *reply = manager.get(request);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (reply->error() != QNetworkReply::NoError) {
        stdout_stream << "Ollama is not reachable at " << url
                      << ": the AI steps fail and run again next time." << Qt::endl;
    } else {
        QStringList available;
        const QJsonArray models = QJsonDocument::fromJson(reply->readAll()).object()
                                      .value(QStringLiteral("models")).toArray();
        for (const QJsonValue &m : models)
            available << m.toObject().value(QStringLiteral("name")).toString();
        QStringList absent;
        for (const QString &key : { QStringLiteral("visionModel"), QStringLiteral("textModel"), QStringLiteral("embedModel") }) {
            const QString model = tools.value(key).toString();
            // Same match as the app: the name, or the name with a tag
            const bool found = std::any_of(available.cbegin(), available.cend(), [&model](const QString &name) {
                return name == model || name.startsWith(model + QLatin1Char(':'));
            });
            if (!found && !absent.contains(model))
                absent << model;
        }
        if (!absent.isEmpty())
            stdout_stream << "Ollama models not found: " << absent.join(QStringLiteral(", "))
                          << " (the AI steps that use them fail and run again next time)" << Qt::endl;
        else if (verbose)
            stdout_stream << "Ollama: " << url << Qt::endl;
    }
    delete reply;
}

CommandLineHandler::AnalysisOutcome CommandLineHandler::analyzeCatalog(int deviceId, const QVariantMap &tools,
                                                                       bool explicitRequest)
{
    QTextStream stdout_stream(stdout);
    QTextStream stderr_stream(stderr);

    Device device;
    device.ID = deviceId;
    device.loadDevice(m_connectionName);
    if (device.type != QLatin1String("Catalog")) {
        stderr_stream << "The device selected must be a Catalog. Try with a different device ID" << Qt::endl;
        stderr_stream << "Device ID: " << deviceId << Qt::endl;
        return AnalysisOutcome::NotRun;
    }
    stdout_stream << "Catalog ID: " << device.ID << Qt::endl;
    stdout_stream << "Catalog Name: " << device.name << Qt::endl;
    stdout_stream << "Catalog Path: " << device.path << Qt::endl;

    MdiProfile profile;
    if (!profile.load(m_connectionName, deviceId)) {
        stderr_stream << "Error: No AI analysis profile is saved for this catalog." << Qt::endl;
        return AnalysisOutcome::NotRun;
    }
    stdout_stream << "Derived files: " << profile.destinationPath << Qt::endl;

    // The source, by the rule of the app and the runner (MDI-C29): present
    // and not an empty mount point. Not waited for here: a scheduled run goes
    // on with the next catalog, and the next run continues this one.
    if (device.path.isEmpty() || !QFileInfo::exists(device.path) || QDir(device.path).isEmpty()) {
        (explicitRequest ? stderr_stream : stdout_stream)
            << "The catalog source is not connected: the catalog was not analyzed." << Qt::endl;
        return AnalysisOutcome::DriveAway;
    }
    // The derived files: a folder that can be written to, or one on a drive
    // that is away (the app waits for it, D31; skipped here)
    if (!MdiDiskSpace::isWritable(profile.destinationPath)) {
        if (!profile.destinationPath.isEmpty() && !QFileInfo::exists(profile.destinationPath)) {
            (explicitRequest ? stderr_stream : stdout_stream)
                << "The drive of the derived files is not connected: the catalog was not analyzed." << Qt::endl;
            return AnalysisOutcome::DriveAway;
        }
        stderr_stream << "Error: The destination of the derived files cannot be written to." << Qt::endl;
        return AnalysisOutcome::NotRun;
    }
    // Room for the AI data before anything starts (MDI-F19)
    const MdiDiskSpace::Shortage shortage = MdiDiskSpace::firstShortage(
        { collection->folder, profile.destinationPath }, MdiDiskSpace::kJobMinimumFree);
    if (shortage.isShort()) {
        stderr_stream << "Error: "
                      << QStringLiteral("The AI analysis needs at least %1 of free space on %2, which has %3 left.")
                             .arg(QLocale::c().formattedDataSize(shortage.needed), shortage.path,
                                  QLocale::c().formattedDataSize(shortage.available))
                      << Qt::endl;
        return AnalysisOutcome::NotRun;
    }

    // The same config as the app's (core, D43), with the domain context read
    // from mdi_data.db
    const QString dataDbPath = MdiData::filePath(collection->folder);
    const bool mdiOpen = MdiData::open(kCliMdiConnection, dataDbPath);
    MdiEnrichmentJob::Config config = MdiEnrichmentJob::analysisConfig(
        tools, collection->settingsFilePath, device, dataDbPath, profile,
        mdiOpen ? kCliMdiConnection : QString());
    if (mdiOpen)
        MdiData::close(kCliMdiConnection);
    config.catalogStamp = MdiFts::stamp(device.catalog->dateUpdated);
    const QList<MdiEnrichmentJob::FileItem> files =
        MdiEnrichmentJob::listCatalogFiles(m_connectionName, device.externalID,
                                           collection->databaseMode, device.catalog->filePath);
    stdout_stream << "Catalog Files: " << files.size() << Qt::endl;
    stdout_stream << "Starting AI analysis..." << Qt::endl;

    // The runner on its own thread, as in the app (MDI-C3); this thread only
    // prints its steps and passes on a stop
    auto *job    = new MdiEnrichmentJob(config, files);
    auto *thread = new QThread();
    job->moveToThread(thread);

    QEventLoop loop;
    QVariantMap summary;
    bool driveLeft = false;
    bool stopAsked = false;
    connect(thread, &QThread::started, job, &MdiEnrichmentJob::run);
    connect(thread, &QThread::finished, job, &QObject::deleteLater);
    connect(job, &MdiEnrichmentJob::activity, &loop, [&](const QVariantMap &step) {
        const QString event = step.value(QStringLiteral("event")).toString();
        if (event == QLatin1String("end")) {
            // One line per step that ran; steps already current are not run
            const QString outcome = step.value(QStringLiteral("outcome")).toString();
            const QString reason  = step.value(QStringLiteral("reason")).toString();
            QString line = QStringLiteral("[%1/%2] %3 %4")
                               .arg(step.value(QStringLiteral("fileIndex")).toInt())
                               .arg(step.value(QStringLiteral("fileCount")).toInt())
                               .arg(step.value(QStringLiteral("analysis")).toString(), outcome);
            if (!reason.isEmpty())
                line += QStringLiteral(" (") + reason + QLatin1Char(')');
            else if (outcome == QLatin1String("completed"))
                line += QStringLiteral(" (%1 s)").arg(step.value(QStringLiteral("durationMs")).toLongLong() / 1000.0, 0, 'f', 1);
            line += QStringLiteral(": ") + step.value(QStringLiteral("filePath")).toString();
            stdout_stream << line << Qt::endl;
        } else if (event == QLatin1String("waiting") && step.value(QStringLiteral("waiting")).toBool() && !driveLeft) {
            // A drive gone during the run is not waited for here (MDI-F91):
            // the steps done are kept and the next run continues
            driveLeft = true;
            stdout_stream << "The drive of " << step.value(QStringLiteral("drivePath")).toString()
                          << " is not connected: the analysis stops, the next run continues." << Qt::endl;
            job->requestStop();
        }
    });
    connect(job, &MdiEnrichmentJob::finished, &loop, [&](const QVariantMap &result) {
        summary = result;
        loop.quit();
    });

    // SIGINT or SIGTERM: the runner stops after recording what is done; the
    // step it abandons is not recorded (MDI-F10) and runs again next time
    QTimer signalPoll;
    connect(&signalPoll, &QTimer::timeout, &loop, [&]() {
        if (stopAsked)
            return;
        if (stopSignal() != 0) {
            stopAsked = true;
            stdout_stream << "Stop requested: the analyses done so far are kept, the next run continues." << Qt::endl;
            job->requestStop();
        } else if (m_lockLost) {
            stopAsked = true;   // the lock is another process's now (message printed)
            job->requestStop();
        }
    });
    signalPoll.start(100);
    thread->start();
    loop.exec();
    signalPoll.stop();
    thread->quit();
    thread->wait();
    delete thread;   // the job was deleted when its thread finished

    const int failed = summary.value(QStringLiteral("failed")).toInt();
    stdout_stream << "---" << Qt::endl;
    stdout_stream << QStringLiteral("AI analysis: %1 completed, %2 unchanged, %3 skipped, %4 failed")
                         .arg(summary.value(QStringLiteral("completed")).toInt())
                         .arg(summary.value(QStringLiteral("unchanged")).toInt())
                         .arg(summary.value(QStringLiteral("skipped")).toInt())
                         .arg(failed)
                  << Qt::endl;
    if (summary.contains(QStringLiteral("error"))) {
        stderr_stream << "Error: The AI data of the collection could not be opened: " << dataDbPath << Qt::endl;
        return AnalysisOutcome::NotRun;
    }
    if (stopAsked)
        return AnalysisOutcome::Stopped;
    if (driveLeft)
        return AnalysisOutcome::DriveAway;
    stdout_stream << "Catalog analyzed." << Qt::endl;
    return failed > 0 ? AnalysisOutcome::DoneWithFailures : AnalysisOutcome::Done;
}

int CommandLineHandler::cmd_analyze(int deviceId)
{
    QTextStream stdout_stream(stdout);
    stdout_stream << "Starting AI analysis of catalog " << deviceId << "..." << Qt::endl;
    if (!prepareAnalysis())
        return ExitError;
    installStopSignalHandlers();

    const QVariantMap tools = MdiEnrichmentJob::findTools(collection->settingsFilePath);
    reportAnalysisTools(tools);
    stdout_stream << kSeparator << Qt::endl;

    switch (analyzeCatalog(deviceId, tools, true)) {
    case AnalysisOutcome::Done:             return ExitSuccess;
    case AnalysisOutcome::DoneWithFailures: return ExitAnalysisFailures;
    case AnalysisOutcome::Stopped:          return m_lockLost ? ExitCollectionInUse : 128 + stopSignal();
    case AnalysisOutcome::DriveAway:
    case AnalysisOutcome::NotRun:           break;
    }
    return ExitError;
}

int CommandLineHandler::cmd_analyzeAll()
{
    QTextStream stdout_stream(stdout);
    stdout_stream << "Starting AI analysis of every catalog with a saved profile..." << Qt::endl;
    if (!prepareAnalysis())
        return ExitError;
    installStopSignalHandlers();

    const QList<int> deviceIDs = MdiProfile::deviceIds(m_connectionName);
    if (deviceIDs.isEmpty()) {
        stdout_stream << "No catalog has a saved AI analysis profile." << Qt::endl;
        return ExitSuccess;
    }
    stdout_stream << QStringLiteral("Found %1 catalog(s) with an AI analysis profile.").arg(deviceIDs.size()) << Qt::endl;

    const QVariantMap tools = MdiEnrichmentJob::findTools(collection->settingsFilePath);
    reportAnalysisTools(tools);

    // One catalog after the other, in the order of their device IDs
    int processed = 0, analyzed = 0, withFailures = 0, driveAway = 0;
    QStringList notAnalyzed;
    bool stopped = false;
    for (int i = 0; i < deviceIDs.size() && !stopped; ++i) {
        if (stopSignal() != 0 || m_lockLost) {
            stopped = true;
            break;
        }
        const int deviceID = deviceIDs.at(i);
        stdout_stream << kSeparator << Qt::endl;
        stdout_stream << QString("Processing catalog %1 of %2 (ID: %3)")
                             .arg(i + 1).arg(deviceIDs.size()).arg(deviceID) << Qt::endl;
        ++processed;
        switch (analyzeCatalog(deviceID, tools, false)) {
        case AnalysisOutcome::Done:             ++analyzed;     break;
        case AnalysisOutcome::DoneWithFailures: ++withFailures; break;
        case AnalysisOutcome::DriveAway:        ++driveAway;    break;
        case AnalysisOutcome::Stopped:          stopped = true; break;
        case AnalysisOutcome::NotRun: {
            Device tempDevice;
            tempDevice.ID = deviceID;
            tempDevice.loadDevice(m_connectionName);
            notAnalyzed.append(QString("%1 (%2)").arg(tempDevice.name).arg(deviceID));
            break;
        }
        }
    }

    stdout_stream << kDoubleSeparator << Qt::endl;
    stdout_stream << "AI ANALYSIS SUMMARY" << Qt::endl;
    stdout_stream << kDoubleSeparator << Qt::endl;
    stdout_stream << "Total catalogs processed: " << processed << Qt::endl;
    stdout_stream << "Analyzed: " << analyzed << Qt::endl;
    stdout_stream << "Analyzed with failed steps: " << withFailures << Qt::endl;
    stdout_stream << "Not analyzed, drive not connected: " << driveAway << Qt::endl;
    stdout_stream << "Not analyzed, error: " << notAnalyzed.size() << Qt::endl;
    for (const QString &name : std::as_const(notAnalyzed))
        stdout_stream << "  - " << name << Qt::endl;
    if (stopped)
        stdout_stream << "Stopped before the end: the next run continues." << Qt::endl;
    stdout_stream << kDoubleSeparator << Qt::endl;

    if (stopped)
        return m_lockLost ? ExitCollectionInUse : 128 + stopSignal();
    if (!notAnalyzed.isEmpty())
        return ExitError;
    return withFailures > 0 ? ExitAnalysisFailures : ExitSuccess;
}

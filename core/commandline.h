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
// File Name:   commandline.h
// Purpose:     Class/model for the command lines
// Description:
// Author:      Stephane Couturier
/////////////////////////////////////////////////////////////////////////////
*/

#ifndef COMMANDLINE_H
#define COMMANDLINE_H

#include <QObject>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QEventLoop>
#include <QTimer>
#include <QDateTime>
#include <QLockFile>
#include <QVariantMap>
#include <memory>

#include "collection.h"
#include "device.h"
#include "database.h"
#include "search.h"
#include "searchmanager.h"
#include "searchjobstoppable.h"

class CommandLineHandler : public QObject
{
    Q_OBJECT

public:
    explicit CommandLineHandler(QObject *parent = nullptr);
    ~CommandLineHandler();

    // Main entry point for command line processing
    int processCommandLine(QCoreApplication &app);

    // Optional: Set external collection (for compatibility)
    void setCollection(Collection *collection);

    // Command line catalog operations (moved from MainWindow)
    bool cmd_updateCatalog(int deviceId, bool displayReport);
    void cmd_listGroup0Catalogs();
    bool cmd_updateAllActive(bool displayReport);

    // AI analysis from the command line (fork, D43): the app's runner
    // (MdiEnrichmentJob) on one catalog, or on every catalog with a saved
    // profile, headless; they return the exit code
    int cmd_analyze(int deviceId);
    int cmd_analyzeAll();

    // Exit codes, listed in --help. 0 and 1 are those of every action
    // (SpecCommandLine.md CLI-F8); a stop by SIGINT or SIGTERM exits with
    // 128 + the signal number (130, 143)
    static constexpr int ExitSuccess          = 0;
    static constexpr int ExitError            = 1;
    static constexpr int ExitAnalysisFailures = 2;   // analysed, some steps failed
    static constexpr int ExitCollectionInUse  = 3;   // the collection lock is held
    static constexpr int ExitNotWritable      = 4;   // this Mac may not write to the collection

private slots:
    // Search completion handling
    void onSearchCompleted();
    void onSearchCancelled();
    void onSearchError(const QString &error);

    // Progress handling
    void handleSearchProgress(int filesProcessed);

private:
    QString m_connectionName = "defaultConnection";
    QString autoDetectDatabaseMode(const QString &path);
    QString formatDeviceIDList(const QList<int> &deviceIDs);
    // Command line parsing
    void setupCommandLineParser();
    bool parseArguments(const QCoreApplication &app);

    // Search functionality
    int cmd_search();
    void sendSearchParametersFromSearchHistory(Search *search);
    void loadLastSearchCriteria();
    void outputSearchResults();
    void outputSearchResultsStdout();

    // Helper methods
    bool initializeDatabase();
    QSqlError initializeDatabaseConnection(const QString &connectionName);
    void loadCollection();
    Device* getSelectedDevice();
    QString generateTimestamp();

    // Cleanup
    void cleanup();

    // Collection lock (.teca.lock), held while an action writes to the
    // collection: update_catalog, update_all_active, analyze, analyze_all
    bool actionWritesCollection() const;
    int  lockCollection();   // 0, or the exit code of the refusal (message printed)
    std::unique_ptr<QLockFile> m_collectionLock;
    int  m_initExitCode = 0;   // set when initializeDatabase() refused for a reason of its own

    // AI analysis
    enum class AnalysisOutcome { Done, DoneWithFailures, NotRun, DriveAway, Stopped };
    bool prepareAnalysis();                          // MDI tables, Memory-mode profiles
    void reportAnalysisTools(const QVariantMap &tools);   // missing tools, Ollama state
    AnalysisOutcome analyzeCatalog(int deviceId, const QVariantMap &tools, bool explicitRequest);
    static void installStopSignalHandlers();
    static int  stopSignal();   // the signal received (SIGINT, SIGTERM), 0 when none

    // Members - Core objects
    QCommandLineParser parser;
    Collection *collection;
    SearchManager *searchManager;
    SearchJobStoppable *searchEngine;
    Device *selectedDevice;
    QEventLoop *eventLoop;

    // Search result tracking
    int searchResult;
    bool searchCompleted;

    // Command line options
    bool verbose;
    QString collectionPath;
    int outputLimit;
    int selectedDeviceID;
    bool selectedDeviceIDProvided;
    // Search criteria overrides
    bool useSearchHistory;
    QString overrideSearchText;
    QString overrideFileType;
    QString overrideSizeMin;
    QString overrideSizeMax;
    QDateTime overrideDateAfter;
    QDateTime overrideDateBefore;
    bool overrideCaseSensitive;
    QString overrideSearchIn;
    QString overrideTextCriteria;
    QString overrideExclude;
    bool searchCriteriaProvided;
    // Helper methods for search criteria parsing
    bool validateSizeFormat(const QString &sizeStr);
    void parseSizeValue(const QString &sizeStr, qint64 &value, QString &unit);
    bool validateDateFormat(const QString &dateStr);
    QDateTime parseDate(const QString &dateStr);

    // Action tracking
    QString requestedAction;
    QStringList positionalArgs;
};

#endif // COMMANDLINE_H

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
// File Name:   collection.h
// Purpose:     Class/model for the collection (all contents including devices, catalogs, files)
// Description:
// Author:      Stephane Couturier
/////////////////////////////////////////////////////////////////////////////
*/

#ifndef COLLECTION_H
#define COLLECTION_H

#include "device.h"
#include <QAbstractTableModel>
#include <QDateTime>
#include <QFile>
#include <functional>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QRegularExpression>

// One Quality Check result (SpecQualityCheck.md). Each row holds the check's
// columns in the order of its SELECT; checkNumber is the check's number in the spec.
// A non-empty error means the query failed and rows cannot be trusted as clean.
struct QualityCheckResult
{
    int checkNumber = 0;
    QList<QStringList> rows;
    QString error;
};

class Collection : public QObject
{
    Q_OBJECT

public:
    explicit Collection(QObject *parent = nullptr);

    //Main attributes
    QString appVersion; //current version of the app
    QString dbSchemaVersion;
    QString folder;
    QString settingsFilePath;

    //Settings file used by the application of version appVersion (window and command line)
    static QString resolveSettingsFilePath(const QString &appVersion);
    //A key of the app-wide settings file that belongs to this collection: the
    //file is shared by every collection, so the key carries the collection's
    //identity (mode and where its database is). The same for the window and
    //the command line (fork, D43; MDI-C66). Set the mode and paths first.
    QString settingsKey(const QString &key) const;

    //Collection lock (fork, D43): the lock file of a File- or Memory-mode
    //collection folder, the same for Teca and the command line (CollectionLock)
    static QString lockFilePath(const QString &folder);
    //Read-only open (fork, D43). readOnly: no file of the collection is
    //written (Memory-mode saves and missing files). openReadOnly: File mode
    //opens the SQLite file read-only and immutable (no -wal or -shm either).
    //Set before connecting.
    bool readOnly     = false;
    bool openReadOnly = false;

    QString loadDatabaseSchemaVersion();
    void setDatabaseSchemaVersion();
    QString imageFolderPath;
    void loadImageFolderPath();
    void saveImageFolderPath();

    //Database management
    QString databaseMode;
    QString databaseType;
    QString databaseHostName;
    QString databaseName;
    int     databasePort;
    QString databaseUserName;
    QString databasePassword;
    QString databaseFilePath;

    //File paths
    QString deviceFilePath;
    QString searchHistoryFilePath;
    QString storageFilePath;
    QString statisticsDeviceFileName;
    QString statisticsDeviceFilePath;
    QString parameterFilePath;
    QString tagFilePath;
    QString mappingFilePath;
    QString catalogFilterFilePath;
    void generateCollectionFilesPaths();
    void generateCollectionFiles();

    //File loading
    bool load();
    void clearDatabaseData();
    bool loadAllCatalogFiles(std::function<bool(int filesLoaded, int totalFiles, const QString &deviceName)> progressCallback = nullptr);
    void loadDeviceFileToTable();
    void loadCatalogFilesToTable();
    void loadStorageFileToTable();
    void loadStatisticsDeviceFileToTable();
    void loadParameterFileToTable();
    void loadSearchHistoryFileToTable();
    void loadTagFileToTable();
    void loadMappingFileToTable();
    void loadCatalogFilterFileToTable();

    //File saving
    void saveDeviceTableToFile();
    void saveStorageTableToFile();
    void saveStatiticsTableToFile();
    // Saves the current statistics of every device as a "snapshot" dated
    // dateTime, then writes the statistics file (Memory mode).
    // Memory mode: the file is rewritten from the statistics_device table, so
    // the caller must have loaded it (loadStatisticsDeviceFileToTable) or the
    // saved history is lost. Not done here: it would drop rows an update added
    // to the table but not yet written to the file.
    void recordDevicesSnapshot(const QDateTime &dateTime);
    void saveParameterTableToFile();
    void saveSearchHistoryTableToFile();
    void saveTagTableToFile();
    void saveMappingTableToFile();
    void saveCatalogFilterTableToFile();

    //Search history management
    void clearSearchHistory(const QString &connectionName);
    // Every saved search, newest first; one map per search, keyed by column name.
    QList<QVariantMap> loadSearchHistory();
    void keepLastSearchHistory(int count, const QString &connectionName);

    //File deleting
    enum DeleteCatalogResult {
        DeleteSuccess = 0,
        DeleteFailedToMoveToTrash = 1,
        DeleteInvalidPath = 2
    };

    DeleteCatalogResult deleteCatalogFile(Device *device);

    //Exclude directory management
    bool addExcludeDirectory(const QString &path);
    bool removeExcludeDirectory(const QString &path);
    QStringList getExcludeDirectories();

    //Quality check (SpecQualityCheck.md) - read-only, phase 1 reports only
    QList<QualityCheckResult> runQualityChecks();

    //Import source management
    QStringList getImportSourcePaths();

    //Tag CRUD
    bool createTag(const QString &name, const QString &path, const QString &type, const QDateTime &dateTime);
    bool deleteTag(int tagID);

    //Export operations
    bool exportAllToMemoryMode(const QString &exportFolder);
    bool exportAllCatalogFiles(const QString &outputFolder,
                               std::function<bool(int current, int total, const QString &catalogName)> progressCallback = nullptr);
    bool exportSingleCatalogFoldersFile(int catalogId, const QString &filePath);
    bool exportToSQLiteFile(const QString &filePath,
                            std::function<bool(int current, int total, const QString &tableName)> progressCallback = nullptr);

    //Data management
    bool insertPhysicalStorageGroup();
    // skipNetworkPaths: leave devices on network mounts untouched, so an
    // unreachable share cannot block the caller. Classification comes from the
    // mount listing alone and never touches the device path itself.
    // SpecDeviceActiveStatus.md DAS-C9 / DAS-C10.
    // Returns true when at least one device's active state changed.
    bool updateAllDeviceActive(bool skipNetworkPaths = false);
    // Signature of the currently mounted volumes' root paths. Compare successive
    // values to detect a mount or unmount without probing any device path.
    QString mountSignature() const;

    // Split and recompose (fork): why one did not happen. Nothing is changed
    // when one fails.
    enum CatalogOperationError {
        OperationOk = 0,
        OperationNothingToDo,       // split: no sub-folder / no files to share
        OperationTooFew,            // recompose: fewer than two catalogs
        OperationNoCommonFolder,    // recompose: the catalogs share no folder
        OperationNameTaken,         // recompose: another catalog has that name
        OperationAiMoveFailed,      // mdi_data.db busy or unreadable
        OperationFailed             // database error
    };
    // newDeviceIds: the new catalogs' devices in the Physical group
    bool executeSplitBySubDirectory(Device *activeDevice, QList<int> *newDeviceIds = nullptr,
                                    CatalogOperationError *error = nullptr);
    bool executeSplitByFileType(Device *activeDevice, QList<int> *newDeviceIds = nullptr,
                                CatalogOperationError *error = nullptr);

    // Recompose (fork, owner's decision 2026-10-30): merges catalogs into one
    // new catalog, the reverse of a split. Its folder is the folder the
    // catalogs share; files, folders, exclude filters, AI results and the
    // enrichment profile go to it; the old catalogs' devices are deleted
    // (nothing on disk). Memory mode: .idx files written and removed as a
    // split does.
    CatalogOperationError executeRecompose(const QList<int> &deviceIds, const QString &newName,
                                           int *newDeviceId = nullptr);
    // What recompose proposes for a device: a storage's or a group's catalogs,
    // or a split catalog with its siblings of the same base name ("<base>_x").
    struct RecomposeProposal {
        QString    name;             // suggested name: the split's base, free
        QString    path;             // the folder they share
        QList<int> deviceIds;        // their devices in the Physical group
        QString    problem;          // "", "type", "few", "nocommon"
    };
    RecomposeProposal proposeRecompose(int deviceId);
    // Catalogs left without a date by a split before the fix get one, so the
    // instant-search index stops rebuilding them at every launch. Returns the
    // catalogs dated.
    int repairUndatedCatalogs();

    enum CollectionFolderStatus {
        VALID_EMPTY,           // Empty folder - can create new collection
        VALID_MEMORY_MODE,     // Contains Memory mode files (CSV)
        VALID_FILE_MODE,       // Contains File mode auxiliary files
        INVALID_MEMORY_FILES,  // Contains Memory mode files but we're in File mode
        INVALID_FILE_FILES,    // Contains File mode files but we're in Memory mode
        INVALID_MIXED_DATA,    // Contains user/system data mixed with collection data
        INVALID_USER_DATA      // Contains user data, not a collection folder
    };

    CollectionFolderStatus validateCollectionFolder(const QString& folderPath, const QString& targetMode) const;
    QString getValidationMessage(CollectionFolderStatus status) const; // For UI messages

    void setConnectionName(const QString &name) { m_connectionName = name; }
    QString connectionName() const { return m_connectionName; }

private:
    QString m_connectionName = "defaultConnection";
    bool applySplitResult(Device *activeDevice, const QList<Catalog*> &newCatalogs,
                          QList<int> *newDeviceIds, CatalogOperationError *error);
    void undoSplitCatalogs(Device *activeDevice, const QList<Catalog*> &newCatalogs);
    Device* resolvePhysicalDevice(Device *activeDevice, Device &buffer);

};

#endif // COLLECTION_H

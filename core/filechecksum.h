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
// File Name:   filechecksum.h
// Purpose:     Class for calculating and managing file checksums
// Description: Provides checksum calculation using Qt QCryptographicHash
// Author:      Stephane Couturier
/////////////////////////////////////////////////////////////////////////////
*/

#ifndef FILECHECKSUM_H
#define FILECHECKSUM_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QSqlError>
#include <QCryptographicHash>
#include <functional>

class FileChecksum : public QObject
{
    Q_OBJECT

public:
    explicit FileChecksum(QObject *parent = nullptr);

    // Main calculation and storage method
    static bool calculateAndStore(const QString &filePath,
                                  const QString &connectionName,
                                  int catalogId,
                                  QString includeChecksum);

    // Calculate checksum without storing (for testing/preview)
    static QString calculateChecksum(const QString &filePath,
                                     QCryptographicHash::Algorithm algorithm,
                                     std::function<void(qint64, qint64)> progressCallback = nullptr);

    // Fast partial hash (fork): identifies a file for duplicate detection
    // without reading it whole. Up to kQuickHashFullLimit bytes the whole file
    // is hashed; above, the exact size (8 bytes, little-endian) followed by
    // 64 KB at the start, 64 KB from the middle (size / 2) and the last 64 KB.
    // Hex result; empty when the file cannot be read. Not a content checksum:
    // two files can only be called identical by the full checksum.
    static constexpr qint64 kQuickHashChunk     = 64 * 1024;
    static constexpr qint64 kQuickHashFullLimit = 3 * kQuickHashChunk;   // 192 KB
    static QString computeQuickHash(const QString &filePath,
                                    QCryptographicHash::Algorithm algorithm = QCryptographicHash::Blake2b_256);

    // Update existing file record with checksum
    static bool updateFileChecksum(const QString &connectionName,
                                   int catalogId,
                                   const QString &fileName,
                                   const QString &folderPath,
                                   const QString &checksumValue,
                                   const QString &checksumType);

    // Batch update checksums (for performance)
    static bool batchUpdateFileChecksum(const QString &connectionName,
                                        int catalogId,
                                        const QStringList &fileNames,
                                        const QStringList &folderPaths,
                                        const QStringList &checksumValues);

    //Utility to get algorithm from string
    static QCryptographicHash::Algorithm getAlgorithmFromString(const QString &algorithmName);

    //Verification method
    struct VerificationResult {
        bool success;           // Operation succeeded (file exists, readable)
        bool match;             // Checksums match (only valid if success=true)
        QString expectedChecksum;
        QString actualChecksum;
        QString errorMessage;   // Empty if success=true
    };

    static VerificationResult verifyChecksum(const QString &filePath,
                                             const QString &expectedChecksum,
                                             QCryptographicHash::Algorithm algorithm,
                                             std::function<void(qint64, qint64)> progressCallback = nullptr);

    // Helper to get checksum from DB
    static QString getFileChecksum(const QString &connectionName, int catalogId,
                                   const QString &fileName, const QString &folderPath);

    // Count files with checksums in a catalog
    static int countFilesWithChecksum(const QString &connectionName, int catalogId);

    // Verify all checksums in a catalog
    struct CatalogVerificationResult {
        int totalFiles;
        int verified;
        int mismatches;
        int missing;
        QStringList mismatchedFiles;  // List of files with checksum mismatches
        QStringList missingFiles;     // List of files that no longer exist
    };

    static CatalogVerificationResult verifyCatalogChecksums(
        const QString &connectionName,
        int catalogId,
        const QString &catalogSourcePath,
        std::function<bool()> shouldContinue,
        std::function<void(int, int, const QString&)> progressCallback = nullptr
        );

signals:
    void checksumCalculated(const QString &filePath);
    void calculationError(const QString &filePath, const QString &error);
};

#endif // FILECHECKSUM_H

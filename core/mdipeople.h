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
// File Name:   mdipeople.h
// Purpose:     File information, people and faces (fork only, SpecMediaIntelligence.md)
// Description: What the owner writes about a file or a group of files
//              (title, description, event, place, date, author, notes, free
//              tags), the people of the archive (first and last name, role,
//              organisation, notes), who is in which file, and the faces the
//              enrichment runner found with their association to a person
//              (owner's request, 2026-10-30). Kept in mdi_data.db (File and
//              Memory modes) next to the derived data, keyed like it by
//              catalog ID and full path, so it follows split, recomposed and
//              restored catalogs (MdiData::moveToCatalogs).
//              A face is never confirmed automatically: the runner suggests a
//              person, the owner approves (owner's answer 2026-10-30).
/////////////////////////////////////////////////////////////////////////////
*/
#ifndef MDIPEOPLE_H
#define MDIPEOPLE_H

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QList>
#include <QHash>
#include <QVector>
#include <QPair>

class MdiPeople
{
public:
    // Store version of mdi_data.db once these tables exist
    static constexpr int kStoreVersion = 2;
    // SFace cosine similarity from which two faces are the same person
    // (OpenCV's published threshold for this model)
    static constexpr double kDefaultMatchThreshold = 0.363;
    // Unnamed faces are grouped a little more strictly, so a group holds one person
    static constexpr double kGroupThreshold = 0.42;
    // Faces read when grouping unnamed faces, best detections first
    static constexpr int    kGroupMaxFaces  = 4000;

    struct FileRef {
        int     catalogId = 0;
        QString fullPath;
        bool operator==(const FileRef &o) const { return catalogId == o.catalogId && fullPath == o.fullPath; }
    };

    struct Annotation {
        QString     title, description, event, place, date, author, notes;
        QStringList tags;
        QString     updated;
        bool        exists = false;
    };

    struct Person {
        int     id = 0;
        QString firstName, lastName, role, organisation, notes;
        QString created, updated;
        int     fileCount = 0;      // files the person is in
        int     faceCount = 0;      // faces confirmed as this person
        int     pendingCount = 0;   // faces suggested as this person, awaiting approval
        QString displayName() const;
    };

    struct FilePerson {
        int     personId = 0;
        QString name, role;
        int     faceId = 0;         // 0: added by hand, without a face
        QString source;             // "manual" or "face"
    };

    struct Face {
        int            id = 0;
        int            catalogId = 0;
        QString        fullPath;
        double         x = 0, y = 0, w = 0, h = 0;   // 0..1 of the image (after EXIF orientation)
        double         score = 0;                    // detection score
        QVector<float> embedding;                    // unit length
        int            personId = 0;
        QString        status = QStringLiteral("unknown");   // unknown, suggested, confirmed, rejected
        int            suggestedPersonId = 0;
        double         suggestionScore = 0;
        QList<int>     rejectedPersons;              // "not this person": never suggested again
        QString        cropRoot;                     // derived root
        QString        cropFile;                     // relative to cropRoot
        QString        created;
        QString cropPath() const;
    };

    // Creates the tables when absent and stamps the store version. Idempotent.
    static bool ensureTables(const QString &connectionName);

    // ---- File information
    static Annotation annotation(const QString &connectionName, int catalogId, const QString &fullPath);
    // For every file: a key of `fields` with a value replaces it (title,
    // description, event, place, date, author, notes, tags); a key absent is
    // left as it is; "tagsAdd" / "tagsRemove" add or remove tags. One transaction.
    static bool updateAnnotations(const QString &connectionName, const QList<FileRef> &files,
                                  const QVariantMap &fields);

    // ---- People
    // filter: part of the name, role or organisation (all when empty)
    static QList<Person> persons(const QString &connectionName, const QString &filter = QString());
    static Person        person(const QString &connectionName, int personId);
    // Creates when id is 0; returns the id, -1 on error or without any name
    static int           savePerson(const QString &connectionName, const Person &p);
    // Faces of the person go back to unknown, links to files are removed;
    // no file is touched
    static bool          deletePerson(const QString &connectionName, int personId);

    // ---- Who is in which file
    static QList<FilePerson> peopleOfFile(const QString &connectionName, int catalogId, const QString &fullPath);
    static bool addPeopleToFiles(const QString &connectionName, const QList<FileRef> &files,
                                 const QList<int> &personIds);
    // Replaces the people added by hand to these files (faces confirmed stay)
    static bool setManualPeople(const QString &connectionName, const QList<FileRef> &files,
                                const QList<int> &personIds);
    // Also returns the file's faces confirmed as this person to unknown
    static bool removePersonFromFile(const QString &connectionName, int personId, int catalogId,
                                     const QString &fullPath);
    static QList<FileRef> filesOfPerson(const QString &connectionName, int personId);

    // ---- Search
    // The file's information and the names and roles of its people, for the instant index
    static QString searchText(const QString &connectionName, int catalogId, const QString &fullPath);
    // The same as one readable text for search by meaning (embedding kind
    // "info"): "name. title. description. Event: … Place: … Date: … Author: …
    // notes. Tags: … People: First Last – role, organisation; …"; empty when none
    static QString infoText(const QString &connectionName, int catalogId, const QString &fullPath);
    static QHash<QString, QString> searchTextByFile(const QString &connectionName, int catalogId);
    // Files whose information or people contain `text` (each word, any field),
    // over the given catalogs (all when empty)
    static QList<FileRef> findFiles(const QString &connectionName, const QString &text,
                                    const QList<int> &catalogIds = {}, int limit = 500);

    // ---- Faces
    static QList<Face> facesOfFile(const QString &connectionName, int catalogId, const QString &fullPath);
    // Each person's best confirmed face (crop path, absolute), for the people list
    static QHash<int, QString> coverCrops(const QString &connectionName);
    static Face        face(const QString &connectionName, int faceId);
    // The file's new detections: a detection overlapping a face already known
    // (IoU >= 0.5) keeps that face's ID, person and status; known faces not
    // found again are removed unless confirmed. Returns the IDs of the faces
    // now in the file, in the detections' order; empty list and false on error.
    static bool replaceFileFaces(const QString &connectionName, int catalogId, const QString &fullPath,
                                 const QList<Face> &detections, QList<int> *faceIds);
    static bool confirmFaces(const QString &connectionName, const QList<int> &faceIds, int personId);
    // "Not this person": the suggestion is cleared and that person is never suggested again for these faces
    static bool rejectFaces(const QString &connectionName, const QList<int> &faceIds);
    // Not a face, or nobody to name: hidden from suggestions and groups
    static bool ignoreFaces(const QString &connectionName, const QList<int> &faceIds);
    // Back to unknown, with no person and no suggestion
    static bool clearFace(const QString &connectionName, int faceId);
    // Every face of the collection forgotten (owner, 2026-10-30, GDPR): the
    // faces, the names given through them and the "faces" analysis of every
    // file, so a new search starts afresh. People and the names added by hand
    // stay. The files given a name through a face are returned in *files and
    // the face crops (derived files the analysis made) in *crops.
    static bool clearAllFaces(const QString &connectionName, QList<QPair<int, QString>> *files,
                              QStringList *crops);

    // Suggestions (never a confirmation): the best person whose confirmed
    // faces match at least `threshold`, by the best match over those faces
    static int suggestForFaces(const QString &connectionName, const QList<int> &faceIds, double threshold);
    static int suggestForPerson(const QString &connectionName, int personId, double threshold);

    // Faces not confirmed, ranked by similarity to the person's confirmed faces
    static QList<QPair<Face, double>> similarFaces(const QString &connectionName, int personId,
                                                   int limit, double minScore);
    static QList<Face> pendingFaces(const QString &connectionName, int limit);
    // How many faces pendingFaces lists without a limit: read only, for Home
    static int         pendingFaceCount(const QString &connectionName);
    // Unknown faces grouped greedily: each group's faces match its first face
    // (the best detection) by at least `threshold`; groups of 2 or more, largest first
    static QList<QList<Face>> unnamedGroups(const QString &connectionName, int limit, double threshold);

    static double cosine(const QVector<float> &a, const QVector<float> &b);
};

#endif // MDIPEOPLE_H

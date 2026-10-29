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
// File Name:   mdiknowledge.h
// Purpose:     Local knowledge base of a collection (fork only, D33)
// Description: Reference documents the owner adds (PDF, Word, Excel/CSV,
//              text): referenced, never copied or modified; their text is
//              cut into passages and indexed with the embeddings model, in
//              mdi_data.db (MDI-C30). The enrichment runner does the
//              indexing (its knowledge mode) and reads the passages most
//              related to a file when it classifies it. Brute-force cosine,
//              no vector-index library (MDI-C9); the vectors are loaded once
//              per job, not once per file.
/////////////////////////////////////////////////////////////////////////////
*/
#ifndef MDIKNOWLEDGE_H
#define MDIKNOWLEDGE_H

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <QList>

class MdiKnowledge
{
public:
    struct Document {
        int     id = 0;
        QString path;
    };
    struct Passage {
        int     documentId = 0;
        int     index = 0;      // passage number within its document
        QString title;          // the document's file name
        QString text;
        double  score = 0;
    };

    // pdf, doc, docx, odt, rtf, txt, md, csv, tsv, xlsx, ods (zip archives are not read)
    static const QStringList &supportedSuffixes();

    // Adds files and the supported files of folders (recursively) as pending;
    // a known document whose size or date changed is pending again. Returns
    // the number of documents added or renewed.
    static int  addPaths(const QString &connectionName, const QStringList &paths);
    // [{id, path, title, status ("pending", "indexed", "failed"), reason, passages, date}]
    static QVariantList documents(const QString &connectionName);
    static bool removeDocument(const QString &connectionName, int documentId);
    static QList<Document> pendingDocuments(const QString &connectionName);
    static int  pendingCount(const QString &connectionName);
    // Replaces a document's passages and marks it indexed
    static bool storePassages(const QString &connectionName, int documentId, const QStringList &texts,
                              const QList<QVector<float>> &vectors, const QString &model);
    static void setFailed(const QString &connectionName, int documentId, const QString &reason);

    // Passages of about `size` characters, cut at paragraph or sentence ends,
    // each starting `overlap` characters before the previous one ended
    static QStringList passages(const QString &text, int size = 1000, int overlap = 150);

    // The indexed passages of one embeddings model, in memory
    class Index {
    public:
        bool load(const QString &connectionName, const QString &model);
        bool isEmpty() const { return m_passages.isEmpty(); }
        QList<Passage> search(const QVector<float> &query, int count = 5, double minScore = 0.35) const;
    private:
        QVector<Passage>        m_passages;
        QVector<QVector<float>> m_vectors;
    };
};

#endif // MDIKNOWLEDGE_H

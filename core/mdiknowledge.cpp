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
// File Name:   mdiknowledge.cpp
// Purpose:     Local knowledge base of a collection (fork only, D33)
/////////////////////////////////////////////////////////////////////////////
*/
#include "core/mdiknowledge.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDirIterator>
#include <QFileInfo>
#include <QDateTime>
#include <QVariantMap>
#include <QDebug>
#include <algorithm>

const QStringList &MdiKnowledge::supportedSuffixes()
{
    static const QStringList suffixes = {"pdf", "doc", "docx", "odt", "rtf", "pptx", "odp",
                                         "txt", "md", "markdown", "html", "htm", "json", "xml", "log",
                                         "srt", "vtt", "csv", "tsv", "xlsx", "ods"};
    return suffixes;
}

int MdiKnowledge::addPaths(const QString &connectionName, const QStringList &paths)
{
    QStringList files;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) {
            QDirIterator it(path, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString f = it.next();
                if (supportedSuffixes().contains(QFileInfo(f).suffix().toLower()))
                    files << f;
            }
        } else if (info.isFile() && supportedSuffixes().contains(info.suffix().toLower())) {
            files << info.absoluteFilePath();
        }
    }

    QSqlDatabase db = QSqlDatabase::database(connectionName);
    db.transaction();
    int added = 0;
    for (const QString &f : files) {
        const QFileInfo info(f);
        const QString date = info.lastModified().toString(Qt::ISODate);
        QSqlQuery find(db);
        find.prepare(QStringLiteral("SELECT doc_id, doc_size, doc_date FROM mdi_kb_document WHERE doc_path = :path"));
        find.bindValue(":path", f);
        if (find.exec() && find.next()) {
            if (find.value(1).toLongLong() == info.size() && find.value(2).toString() == date)
                continue;   // known and unchanged
            QSqlQuery renew(db);
            renew.prepare(QStringLiteral("UPDATE mdi_kb_document SET doc_size = :size, doc_date = :date, "
                                         "doc_status = 'pending', doc_reason = NULL WHERE doc_id = :id"));
            renew.bindValue(":size", info.size());
            renew.bindValue(":date", date);
            renew.bindValue(":id",   find.value(0));
            if (renew.exec())
                ++added;
            continue;
        }
        QSqlQuery ins(db);
        ins.prepare(QStringLiteral("INSERT INTO mdi_kb_document (doc_path, doc_title, doc_size, doc_date, doc_added, doc_status) "
                                   "VALUES (:path, :title, :size, :date, :added, 'pending')"));
        ins.bindValue(":path",  f);
        ins.bindValue(":title", info.fileName());
        ins.bindValue(":size",  info.size());
        ins.bindValue(":date",  date);
        ins.bindValue(":added", QDateTime::currentDateTime().toString(Qt::ISODate));
        if (ins.exec())
            ++added;
        else
            qWarning() << "MdiKnowledge::addPaths failed:" << ins.lastError().text();
    }
    db.commit();
    return added;
}

QVariantList MdiKnowledge::documents(const QString &connectionName)
{
    QVariantList list;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    if (!q.exec(QStringLiteral("SELECT doc_id, doc_path, doc_title, doc_status, doc_reason, doc_passages, doc_added "
                               "FROM mdi_kb_document ORDER BY doc_title COLLATE NOCASE")))
        return list;
    while (q.next()) {
        QVariantMap d;
        d[QStringLiteral("id")]       = q.value(0).toInt();
        d[QStringLiteral("path")]     = q.value(1).toString();
        d[QStringLiteral("title")]    = q.value(2).toString();
        d[QStringLiteral("status")]   = q.value(3).toString();
        d[QStringLiteral("reason")]   = q.value(4).toString();
        d[QStringLiteral("passages")] = q.value(5).toInt();
        d[QStringLiteral("date")]     = q.value(6).toString();
        list.append(d);
    }
    return list;
}

bool MdiKnowledge::removeDocument(const QString &connectionName, int documentId)
{
    // The document's record and passages only; the file itself is never touched
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    db.transaction();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM mdi_kb_passage WHERE passage_doc_id = :id"));
    q.bindValue(":id", documentId);
    bool ok = q.exec();
    q.prepare(QStringLiteral("DELETE FROM mdi_kb_document WHERE doc_id = :id"));
    q.bindValue(":id", documentId);
    ok = ok && q.exec();
    return ok ? db.commit() : (db.rollback(), false);
}

QList<MdiKnowledge::Document> MdiKnowledge::pendingDocuments(const QString &connectionName)
{
    QList<Document> list;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    if (!q.exec(QStringLiteral("SELECT doc_id, doc_path FROM mdi_kb_document WHERE doc_status = 'pending' ORDER BY doc_id")))
        return list;
    while (q.next())
        list.append({ q.value(0).toInt(), q.value(1).toString() });
    return list;
}

int MdiKnowledge::pendingCount(const QString &connectionName)
{
    QSqlQuery q(QSqlDatabase::database(connectionName));
    return q.exec(QStringLiteral("SELECT count(*) FROM mdi_kb_document WHERE doc_status = 'pending'")) && q.next()
           ? q.value(0).toInt() : 0;
}

bool MdiKnowledge::storePassages(const QString &connectionName, int documentId, const QStringList &texts,
                                 const QList<QVector<float>> &vectors, const QString &model)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    if (!db.transaction())
        return false;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM mdi_kb_passage WHERE passage_doc_id = :id"));
    q.bindValue(":id", documentId);
    bool ok = q.exec();
    q.prepare(QStringLiteral("INSERT INTO mdi_kb_passage (passage_doc_id, passage_index, passage_text, passage_model, passage_vector) "
                             "VALUES (:id, :index, :text, :model, :vector)"));
    for (int i = 0; ok && i < texts.size() && i < vectors.size(); ++i) {
        q.bindValue(":id",     documentId);
        q.bindValue(":index",  i);
        q.bindValue(":text",   texts.at(i));
        q.bindValue(":model",  model);
        q.bindValue(":vector", QByteArray(reinterpret_cast<const char *>(vectors.at(i).constData()),
                                          vectors.at(i).size() * int(sizeof(float))));
        ok = q.exec();
    }
    q.prepare(QStringLiteral("UPDATE mdi_kb_document SET doc_status = 'indexed', doc_reason = NULL, "
                             "doc_passages = :count WHERE doc_id = :id"));
    q.bindValue(":count", int(texts.size()));
    q.bindValue(":id",    documentId);
    ok = ok && q.exec();
    if (!ok) {
        qWarning() << "MdiKnowledge::storePassages failed:" << q.lastError().text();
        db.rollback();
        return false;
    }
    return db.commit();
}

void MdiKnowledge::setFailed(const QString &connectionName, int documentId, const QString &reason)
{
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QStringLiteral("UPDATE mdi_kb_document SET doc_status = 'failed', doc_reason = :reason WHERE doc_id = :id"));
    q.bindValue(":reason", reason);
    q.bindValue(":id",     documentId);
    q.exec();
}

QStringList MdiKnowledge::passages(const QString &text, int size, int overlap)
{
    QStringList out;
    const QString clean = text.simplified().isEmpty() ? QString() : text;
    int start = 0;
    while (start < clean.size()) {
        int end = qMin(clean.size(), start + size);
        if (end < clean.size()) {
            // Cut at a paragraph end, else a sentence end, in the last third
            const int floor = start + size * 2 / 3;
            int cut = clean.lastIndexOf(QStringLiteral("\n\n"), end);
            if (cut < floor)
                cut = qMax(clean.lastIndexOf(QStringLiteral(". "), end), clean.lastIndexOf('\n', end));
            if (cut >= floor)
                end = cut + 1;
        }
        const QString passage = clean.mid(start, end - start).simplified();
        if (passage.size() > 40)   // headers and page numbers alone are noise
            out << passage;
        if (end >= clean.size())
            break;
        start = qMax(start + 1, end - overlap);
    }
    return out;
}

bool MdiKnowledge::Index::load(const QString &connectionName, const QString &model)
{
    m_passages.clear();
    m_vectors.clear();
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QStringLiteral("SELECT p.passage_doc_id, p.passage_index, d.doc_title, p.passage_text, p.passage_vector "
                             "FROM mdi_kb_passage p JOIN mdi_kb_document d ON d.doc_id = p.passage_doc_id "
                             "WHERE p.passage_model = :model AND d.doc_status = 'indexed'"));
    q.bindValue(":model", model);
    if (!q.exec())
        return false;
    while (q.next()) {
        const QByteArray blob = q.value(4).toByteArray();
        QVector<float> v(blob.size() / int(sizeof(float)));
        std::copy_n(reinterpret_cast<const float *>(blob.constData()), v.size(), v.begin());
        m_passages.append({ q.value(0).toInt(), q.value(1).toInt(), q.value(2).toString(), q.value(3).toString(), 0 });
        m_vectors.append(v);
    }
    return true;
}

QList<MdiKnowledge::Passage> MdiKnowledge::Index::search(const QVector<float> &query, int count, double minScore) const
{
    // Unit vectors: the dot product is the cosine
    QList<Passage> found;
    for (int i = 0; i < m_vectors.size(); ++i) {
        const QVector<float> &v = m_vectors.at(i);
        if (v.size() != query.size())
            continue;
        double dot = 0;
        for (int k = 0; k < v.size(); ++k)
            dot += double(v[k]) * double(query[k]);
        if (dot < minScore)
            continue;
        Passage p = m_passages.at(i);
        p.score = dot;
        found.append(p);
    }
    std::sort(found.begin(), found.end(), [](const Passage &a, const Passage &b) { return a.score > b.score; });
    if (found.size() > count)
        found = found.mid(0, count);
    return found;
}

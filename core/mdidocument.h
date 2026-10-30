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
// File Name:   mdidocument.h
// Purpose:     Study of text documents (fork only, SpecMediaIntelligence.md,
//              owner's decisions of 2026-10-30)
// Description: The "document" analysis of the enrichment runner. Reads a
//              document page by page (pdftotext; pages without a text layer
//              are rendered with pdftoppm and read by Tesseract, locally),
//              splits it into fragments by headings, pages and paragraphs,
//              and with the local text model writes per fragment a summary,
//              tags and the entities named, then per document a synthesis,
//              its type, date, author, recipients, key facts and tags
//              (map-reduce for long documents). Adds the context's glossary
//              terms found, the related knowledge-base documents and the
//              known people named. The document is only read.
//              The study is a datum ("document"); its fragments are rows of
//              mdi_fragment in mdi_data.db.
/////////////////////////////////////////////////////////////////////////////
*/
#ifndef MDIDOCUMENT_H
#define MDIDOCUMENT_H

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include <QList>
#include <atomic>
#include <functional>

#include "core/mdiproposals.h"

class MdiDocument
{
public:
    // Version of the "document" analysis and of its prompts (MDI-F13, MDI-F54)
    static constexpr int kVersion = 1;
    // Fragments summarised one by one; a longer document gets larger fragments
    static constexpr int kMaxModelFragments = 80;
    // Text read at most (characters)
    static constexpr int kMaxChars = 3 * 1000 * 1000;

    struct Page {
        int     number = 0;
        QString text;
        bool    ocr = false;
    };

    struct Fragment {
        int         index = 0;      // 0-based
        int         pageFrom = 0;   // 1-based; 0 when the format has no pages
        int         pageTo = 0;
        QString     title;
        QString     text;
        QString     summary;
        QStringList tags;
        QJsonObject entities;       // {people, places, organisations, events, dates}: [..]
        QJsonObject toJson(bool withText = true) const;
        static Fragment fromJson(const QJsonObject &o);
    };

    struct Tools {
        QString pdftotext;
        QString pdftoppm;
        QString tesseract;
        QString tessdataDir;        // --tessdata-dir; empty: Tesseract's own
        QString ocrLanguages;       // "ita+eng"; empty: from tessdataDir
        int     dpi = 300;
        int     maxOcrPages = 300;
        int     timeoutMs = 5 * 60 * 1000;   // per program run
        const std::atomic<bool> *stop = nullptr;
    };

    // Text of formats other than PDF (the runner's own extraction)
    using TextExtractor = std::function<QString(const QString &path, QString *tool, QString *reason)>;
    // One JSON answer of the local text model
    using Asker = std::function<QJsonObject(const QString &prompt, QString *error)>;
    using Progress = std::function<void(double fraction, const QString &detail)>;
    // Embedding of a text for related documents; empty when unavailable
    using Embedder = std::function<QVector<float>(const QString &text)>;

    // ---- Reading
    static QList<Page> readPages(const QString &path, const Tools &tools, const TextExtractor &other,
                                 QString *tool, QString *reason, const Progress &progress = {});
    static QString ocrImage(const QString &imagePath, const Tools &tools, QString *error);
    // ~/teca-models/tessdata, then Homebrew's and the system's share folders
    static QString findTessdata(const QString &configured = QString());
    // "ita+eng" (and others listed) when their traineddata are there
    static QString ocrLanguages(const QString &tessdataDir, const QStringList &wanted = {QStringLiteral("ita"), QStringLiteral("eng")});
    static bool    pageNeedsOcr(const QString &text);
    // Joins words cut at line ends, removes repeated blank lines
    static QString cleanText(const QString &text);

    // ---- Fragments (pure)
    static bool isHeading(const QString &line);
    static QList<Fragment> fragment(const QList<Page> &pages, int minChars = 1500, int maxChars = 3000);
    // "it", "en", "fr", "de", "es"; empty when unsure
    static QString detectLanguage(const QString &text);
    static QStringList glossaryFound(const QString &text, const QJsonObject &context);

    struct PersonName {
        int         id = 0;
        QString     name;           // as shown
        QString     firstName, lastName;
    };
    // Known people named in the text by first and last name: [{personId, name}]
    static QJsonArray peopleFound(const QString &text, const QList<PersonName> &people);

    // ---- Prompts
    static const QStringList &documentTypes();
    static QString fragmentPrompt(const Fragment &f, int count, const QString &docName,
                                  const QString &preamble, const QString &language);
    static QString reducePrompt(const QStringList &summaries, const QString &docName, const QString &language);
    static QString synthesisPrompt(const QStringList &parts, const QString &opening, const QString &docName,
                                   int pageCount, const QString &preamble, const QString &language);

    // ---- The study
    struct StudyOptions {
        Tools         tools;
        TextExtractor extractOther;
        Asker         ask;
        Embedder      embed;            // optional: related documents by meaning
        QString       preamble;         // MdiContext::preamble
        QString       language;         // of the generated text (MdiContext::language)
        QJsonObject   context;
        QList<PersonName> people;       // D38 people
        QString       connectionName;   // mdi_data.db, for the knowledge base
        Progress      progress;
    };
    // The datum value; fragments filled. outcome: completed, skipped (no
    // text), failed (reason). tool: the programs used ("pdftotext+tesseract").
    static QJsonObject study(const QString &path, const StudyOptions &o, QList<Fragment> *fragments,
                             QString *outcome, QString *reason, QString *tool);
    // Proposals for the document's information: date, author, title
    static QList<MdiProposals::Proposal> proposals(const QJsonObject &study, const QString &fileName);

    // Knowledge-base documents related by the key facts and tags, and by
    // meaning when an embedder is given: [{title, path, score}]
    static QJsonArray relatedKnowledge(const QString &connectionName, const QJsonObject &study,
                                       const QString &excludePath, const Embedder &embed = {}, int max = 5);

    // Text for the instant index (synthesis, type, title, tags, key facts)
    static QString searchText(const QJsonObject &study);

    // ---- Storage of the fragments (mdi_data.db)
    static bool ensureTables(const QString &connectionName);
    static bool storeFragments(const QString &connectionName, int catalogId, const QString &fullPath,
                               const QList<Fragment> &fragments);
    static QList<Fragment> fragments(const QString &connectionName, int catalogId, const QString &fullPath);
    static bool removeFragments(const QString &connectionName, int catalogId, const QString &fullPath);
    static bool clearCatalog(const QString &connectionName, int catalogId);
    // Same binds as MdiData::moveToCatalogs: :new_id, :old_id, :full_path
    static QString moveStatement();
};

#endif // MDIDOCUMENT_H

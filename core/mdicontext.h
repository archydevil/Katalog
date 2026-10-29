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
// File Name:   mdicontext.h
// Purpose:     Domain context of a collection (fork only, D33)
// Description: The context the AI analyses are interpreted in: the
//              organisation, period, domain, language and narrative style,
//              a glossary, the owner's categories and the known entities,
//              for the collection, plus notes per catalog. Kept as JSON in
//              mdi_data.db (MDI-C30); builds the prompt sections (MDI-C14).
//
// Context JSON (scope 0, the collection):
//   organisation, period, domain, language, style, notes: text
//   glossary:   [{term, preferred, aliases: [..], definition}]
//   categories: [{name, description, values: [..], closed: bool, multiple: bool}]
//   entities:   [{name, kind, aliases: [..], notes}]
//   transcriptHint: bool (glossary terms suggested to whisper.cpp, off by default)
// Catalog notes (scope = catalog ID): {notes}
/////////////////////////////////////////////////////////////////////////////
*/
#ifndef MDICONTEXT_H
#define MDICONTEXT_H

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <atomic>

class MdiContext
{
public:
    static constexpr int kCollectionScope = 0;

    // Storage (table mdi_context of mdi_data.db, on the caller's connection)
    static QJsonObject load(const QString &connectionName, int scope);
    static bool        save(const QString &connectionName, int scope, const QJsonObject &context);
    static bool        remove(const QString &connectionName, int scope);

    // Short content hash, recorded with each classification (not a parameter)
    static QString version(const QJsonObject &context);
    // No brief, glossary, categories or entities: classification stays off
    static bool    isEmpty(const QJsonObject &context);
    // The context's output language, else the fallback (D12's language)
    static QString language(const QJsonObject &context, const QString &fallback);

    // Domain preamble and preferred terms for the perception prompts
    // (caption, summary, segments, clusters); empty without a context
    static QString preamble(const QJsonObject &context, const QString &catalogNotes);
    // Glossary terms for whisper.cpp's --prompt; empty unless enabled
    static QString whisperHint(const QJsonObject &context);

    // Classification prompt: what is known of the file, the context, and the
    // knowledge-base passages ("[n] title: text")
    static QString classificationPrompt(const QJsonObject &context, const QString &catalogNotes,
                                        const QString &fileFacts, const QStringList &passages,
                                        const QString &language);
    // Keeps the owner's categories only, the allowed values of closed ones,
    // one value for single ones, and the glossary's preferred forms
    static QJsonObject normaliseClassification(const QJsonObject &answer, const QJsonObject &context);

    // Draft of a context from a catalog's folder names and existing results
    static QString     draftPrompt(const QStringList &folders, const QStringList &samples, const QString &language);
    static QJsonObject parseDraft(const QJsonObject &answer);

    // One JSON answer from a local Ollama endpoint; synchronous, for a worker
    // thread (never the UI thread). stop, when given, aborts the request.
    static QJsonObject askText(const QString &ollamaUrl, const QString &model, const QString &prompt,
                               QString *error, int timeoutMs = 300 * 1000,
                               const std::atomic<bool> *stop = nullptr);
};

#endif // MDICONTEXT_H

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
// File Name:   mdihtmlexport.cpp
// Purpose:     Readable, distributable report of files (fork only)
/////////////////////////////////////////////////////////////////////////////
*/
#include "core/mdihtmlexport.h"
#include "core/mdidata.h"
#include "core/mdipeople.h"
#include "core/mdiproposals.h"
#include "core/mdidocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>

namespace {

QString esc(const QString &s)
{
    return s.toHtmlEscaped();
}

// Paragraphs of a plain text, escaped
QString paragraphs(const QString &text, const QString &cls = QString())
{
    QString out;
    const QString open = cls.isEmpty() ? QStringLiteral("<p>") : QStringLiteral("<p class=\"%1\">").arg(cls);
    for (const QString &p : text.split(QRegularExpression(QStringLiteral("\\n\\s*\\n")), Qt::SkipEmptyParts)) {
        QString line = esc(p.trimmed());
        line.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
        out += open + line + QStringLiteral("</p>\n");
    }
    return out;
}

QStringList strings(const QVariant &v)
{
    QStringList out;
    for (const QVariant &x : v.toList()) {
        const QString s = x.toString().trimmed();
        if (!s.isEmpty())
            out << s;
    }
    if (out.isEmpty() && v.typeId() == QMetaType::QStringList)
        out = v.toStringList();
    return out;
}

QString tagList(const QStringList &tags)
{
    if (tags.isEmpty())
        return QString();
    QString out = QStringLiteral("<ul class=\"tags\">");
    for (const QString &t : tags)
        out += QStringLiteral("<li>") + esc(t) + QStringLiteral("</li>");
    return out + QStringLiteral("</ul>\n");
}

QString mimeOf(const QString &path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    if (s == QLatin1String("webp")) return QStringLiteral("image/webp");
    if (s == QLatin1String("png"))  return QStringLiteral("image/png");
    if (s == QLatin1String("gif"))  return QStringLiteral("image/gif");
    return QStringLiteral("image/jpeg");
}

const char *const kCss = R"CSS(
:root { --fg: #1d1d1f; --muted: #5f6368; --bg: #ffffff; --panel: #f5f5f7; --line: #d9d9de; --accent: #0b57d0; --pending: #8a5a00; --ok: #1e7b34; }
@media (prefers-color-scheme: dark) {
  :root { --fg: #ececf1; --muted: #a9adb4; --bg: #17181b; --panel: #222429; --line: #3a3d44; --accent: #8ab4f8; --pending: #f2c46d; --ok: #81c995; }
}
* { box-sizing: border-box; }
html { font-size: 100%; }
body { margin: 0 auto; max-width: 62rem; padding: 1.5rem 1rem 3rem; font: 1rem/1.55 system-ui, -apple-system, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif; color: var(--fg); background: var(--bg); }
a { color: var(--accent); }
a:focus-visible { outline: 3px solid var(--accent); outline-offset: 2px; }
.skip { position: absolute; left: -999px; top: 0; padding: .5rem 1rem; background: var(--bg); }
.skip:focus { left: 1rem; }
header h1 { margin: 0 0 .25rem; font-size: 1.8rem; line-height: 1.2; }
.meta, .muted { color: var(--muted); }
nav.toc { margin: 1.5rem 0; padding: 1rem 1.25rem; background: var(--panel); border-radius: .5rem; }
nav.toc h2 { margin-top: 0; font-size: 1.2rem; }
nav.toc ol { margin: .25rem 0; padding-left: 1.4rem; }
section.folder > h2 { margin-top: 2.5rem; padding-bottom: .3rem; border-bottom: 2px solid var(--line); font-size: 1.4rem; }
article.file { margin: 1.5rem 0; padding: 1rem 1.25rem; border: 1px solid var(--line); border-radius: .5rem; }
article.file > h3, article.file > h2 { margin-top: 0; font-size: 1.25rem; overflow-wrap: anywhere; }
h4 { margin: 1.25rem 0 .4rem; font-size: 1.05rem; }
h5 { margin: 1rem 0 .3rem; font-size: 1rem; }
figure { margin: .5rem 0 1rem; }
figure img { max-width: 100%; height: auto; max-height: 20rem; border-radius: .35rem; }
figcaption { color: var(--muted); font-size: .9rem; }
dl.fields { display: grid; grid-template-columns: max-content 1fr; gap: .2rem 1rem; margin: .4rem 0; }
dl.fields dt { font-weight: 600; }
dl.fields dd { margin: 0; overflow-wrap: anywhere; }
table { width: 100%; border-collapse: collapse; margin: .4rem 0; font-size: .95rem; }
caption { text-align: left; color: var(--muted); padding-bottom: .3rem; }
th, td { text-align: left; vertical-align: top; padding: .35rem .5rem; border-bottom: 1px solid var(--line); }
th { font-weight: 600; }
td ul, dd ul { margin: 0; padding-left: 1.1rem; }
.status-pending { color: var(--pending); font-weight: 600; }
.status-accepted { color: var(--ok); font-weight: 600; }
ul.tags { list-style: none; padding: 0; margin: .3rem 0; display: flex; flex-wrap: wrap; gap: .35rem; }
ul.tags li { padding: .05rem .55rem; border: 1px solid var(--line); border-radius: 999px; font-size: .9rem; }
.fragment { margin: 1rem 0; padding-left: .9rem; border-left: 3px solid var(--line); }
.fulltext { font-size: .92rem; color: var(--fg); }
.fulltext p { margin: .4rem 0; }
.back { font-size: .9rem; }
footer { margin-top: 3rem; color: var(--muted); font-size: .9rem; }
@media (max-width: 40rem) { dl.fields { grid-template-columns: 1fr; } dl.fields dt { margin-top: .4rem; } }
@media print {
  :root { --fg: #000; --muted: #444; --bg: #fff; --panel: #fff; --line: #999; --accent: #000; }
  body { max-width: none; padding: 0; font-size: 10.5pt; }
  .skip, .back { display: none; }
  nav.toc { break-after: page; border: 1px solid var(--line); }
  section.folder > h2 { break-after: avoid; }
  article.file { border: none; padding: 0; break-before: auto; }
  h3, h4, h5 { break-after: avoid; }
  figure, table, dl { break-inside: avoid; }
  main a[href^="http"]::after { content: " (" attr(href) ")"; font-size: .85em; overflow-wrap: anywhere; }
}
)CSS";

class Builder
{
public:
    Builder(const MdiHtmlExport::Options &o, const MdiHtmlExport::Labels &l) : m_o(o), m_l(l)
    {
        m_defaults = MdiHtmlExport::englishLabels();
    }

    QString t(const QString &key) const
    {
        const QString v = m_l.value(key);
        return v.isEmpty() ? m_defaults.value(key, key) : v;
    }

    QString fieldsList(const QList<QPair<QString, QString>> &rows) const
    {
        QString out;
        for (const auto &r : rows)
            if (!r.second.trimmed().isEmpty())
                out += QStringLiteral("<dt>") + esc(r.first) + QStringLiteral("</dt><dd>") + r.second + QStringLiteral("</dd>\n");
        return out.isEmpty() ? QString() : QStringLiteral("<dl class=\"fields\">\n") + out + QStringLiteral("</dl>\n");
    }

    QString preview(const QVariantMap &r)
    {
        const QString path = r.value("preview").toString();
        if (!m_o.embedPreviews || path.isEmpty())
            return QString();
        QFile f(path);
        if (f.size() <= 0 || f.size() > m_o.maxPreviewBytes || m_used + f.size() > m_o.previewBudget
            || !f.open(QIODevice::ReadOnly))
            return QString();
        const QByteArray bytes = f.readAll();
        m_used += bytes.size();
        QString alt = r.value("ai").toMap().value("caption").toString();
        if (alt.isEmpty())
            alt = r.value("information").toMap().value("title").toString();
        if (alt.isEmpty())
            alt = t("previewOf").arg(r.value("name").toString());
        return QStringLiteral("<figure><img src=\"data:%1;base64,%2\" alt=\"%3\" loading=\"lazy\"></figure>\n")
            .arg(mimeOf(path), QString::fromLatin1(bytes.toBase64()), esc(alt));
    }

    QString information(const QVariantMap &r, const QString &h) const
    {
        const QVariantMap a = r.value("information").toMap();
        QList<QPair<QString, QString>> rows;
        for (const char *k : {"title", "description", "event", "place", "date", "author", "notes"}) {
            const QString v = a.value(QLatin1String(k)).toString();
            rows << qMakePair(t(QLatin1String(k)), v.contains(QLatin1Char('\n')) ? paragraphs(v) : esc(v));
        }
        const QStringList tags = strings(a.value("tags"));
        if (!tags.isEmpty())
            rows << qMakePair(t("tags"), tagList(tags));
        QString body = fieldsList(rows);
        const QVariantList people = r.value("people").toList();
        if (!people.isEmpty()) {
            QString list = QStringLiteral("<ul>");
            for (const QVariant &p : people) {
                const QVariantMap m = p.toMap();
                list += QStringLiteral("<li>") + esc(m.value("name").toString());
                if (!m.value("role").toString().isEmpty())
                    list += QStringLiteral(" <span class=\"muted\">(") + esc(m.value("role").toString()) + QStringLiteral(")</span>");
                list += QStringLiteral("</li>");
            }
            body += QStringLiteral("<p><strong>") + esc(t("people")) + QStringLiteral("</strong></p>") + list + QStringLiteral("</ul>\n");
        }
        if (body.isEmpty())
            body = QStringLiteral("<p class=\"muted\">") + esc(t("noInformation")) + QStringLiteral("</p>\n");
        return QStringLiteral("<%1>").arg(h) + esc(t("information")) + QStringLiteral("</%1>\n").arg(h) + body;
    }

    QString proposals(const QVariantMap &r, const QString &h, const QString &id) const
    {
        const QVariantList list = r.value("proposals").toList();
        if (list.isEmpty())
            return QString();
        QString out = QStringLiteral("<%1 id=\"%2\">").arg(h, id) + esc(t("proposals")) + QStringLiteral("</%1>\n").arg(h);
        out += QStringLiteral("<table>\n<caption>") + esc(t("proposalsCaption")) + QStringLiteral("</caption>\n<thead><tr>");
        for (const char *k : {"field", "value", "confidence", "status", "sources"})
            out += QStringLiteral("<th scope=\"col\">") + esc(t(QLatin1String(k))) + QStringLiteral("</th>");
        out += QStringLiteral("</tr></thead>\n<tbody>\n");
        for (const QVariant &v : list) {
            const QVariantMap p = v.toMap();
            const QString status = p.value("status").toString();
            QString sources = QStringLiteral("<ul>");
            for (const QVariant &s : p.value("sources").toList()) {
                const QVariantMap m = s.toMap();
                QString item = QStringLiteral("<strong>") + esc(t(QStringLiteral("source-") + m.value("kind").toString()))
                             + QStringLiteral("</strong>");
                if (!m.value("label").toString().isEmpty())
                    item += QStringLiteral(": ") + esc(m.value("label").toString());
                const QString url = m.value("url").toString();
                if (url.startsWith(QLatin1String("http")))
                    item = QStringLiteral("<a href=\"%1\">").arg(esc(url)) + item + QStringLiteral("</a>");
                else if (!url.isEmpty())
                    item += QStringLiteral(" <span class=\"muted\">") + esc(url) + QStringLiteral("</span>");
                const QString detail = m.value("detail").toString();
                if (!detail.isEmpty() && detail != m.value("label").toString())
                    item += QStringLiteral(" — <q>") + esc(detail) + QStringLiteral("</q>");
                sources += QStringLiteral("<li>") + item + QStringLiteral("</li>");
            }
            sources += QStringLiteral("</ul>");
            QString value = esc(p.value("value").toString());
            const QString precision = p.value("precision").toString();
            if (!precision.isEmpty() && !t(QStringLiteral("precision-") + precision).startsWith(QLatin1String("precision-")))
                value += QStringLiteral(" <span class=\"muted\">(") + esc(t(QStringLiteral("precision-") + precision)) + QStringLiteral(")</span>");
            out += QStringLiteral("<tr><th scope=\"row\">") + esc(t(p.value("field").toString())) + QStringLiteral("</th><td>")
                 + value + QStringLiteral("</td><td>") + QString::number(qRound(p.value("confidence").toDouble() * 100))
                 + QStringLiteral("%</td><td class=\"status-%1\">").arg(esc(status)) + esc(t(status))
                 + QStringLiteral("</td><td>") + sources + QStringLiteral("</td></tr>\n");
        }
        return out + QStringLiteral("</tbody>\n</table>\n");
    }

    QString ai(const QVariantMap &r, const QString &h, const QString &id) const
    {
        const QVariantMap a = r.value("ai").toMap();
        QString body;
        if (!a.value("caption").toString().isEmpty())
            body += paragraphs(a.value("caption").toString());
        if (!a.value("summary").toString().isEmpty())
            body += paragraphs(a.value("summary").toString());
        const QStringList excerpts = strings(a.value("excerpts"));
        if (!excerpts.isEmpty()) {
            body += QStringLiteral("<p><strong>") + esc(t("excerpts")) + QStringLiteral("</strong></p><ul>");
            for (const QString &e : excerpts)
                body += QStringLiteral("<li><q>") + esc(e) + QStringLiteral("</q></li>");
            body += QStringLiteral("</ul>\n");
        }
        QList<QPair<QString, QString>> rows;
        rows << qMakePair(t("classificationTitle"), esc(a.value("title").toString()));
        if (!a.value("description").toString().isEmpty())
            rows << qMakePair(t("description"), paragraphs(a.value("description").toString()));
        const QVariantMap categories = a.value("categories").toMap();
        for (auto it = categories.cbegin(); it != categories.cend(); ++it)
            rows << qMakePair(it.key(), esc(strings(it.value()).join(QStringLiteral(", "))));
        const QStringList entities = strings(a.value("entities"));
        if (!entities.isEmpty())
            rows << qMakePair(t("entities"), esc(entities.join(QStringLiteral(", "))));
        body += fieldsList(rows);
        body += tagList(strings(a.value("tags")));
        if (body.isEmpty())
            return QString();
        return QStringLiteral("<%1 id=\"%2\">").arg(h, id) + esc(t("aiDescription")) + QStringLiteral("</%1>\n").arg(h) + body;
    }

    QString study(const QVariantMap &r, const QString &h, const QString &h2, const QString &id) const
    {
        const QVariantMap s = r.value("study").toMap();
        if (s.value("synthesis").toString().isEmpty() && s.value("fragments").toList().isEmpty())
            return QString();
        QString out = QStringLiteral("<%1 id=\"%2\">").arg(h, id) + esc(t("study")) + QStringLiteral("</%1>\n").arg(h);
        if (!s.value("synthesis").toString().isEmpty())
            out += QStringLiteral("<%1>").arg(h2) + esc(t("synthesis")) + QStringLiteral("</%1>\n").arg(h2)
                 + paragraphs(s.value("synthesis").toString());
        QList<QPair<QString, QString>> rows;
        rows << qMakePair(t("title"), esc(s.value("title").toString()));
        const QString type = s.value("typeLabel").toString().isEmpty() ? s.value("type").toString()
                                                                       : s.value("typeLabel").toString();
        rows << qMakePair(t("documentType"), esc(type));
        rows << qMakePair(t("documentDate"), esc(s.value("date").toString()));
        rows << qMakePair(t("author"), esc(s.value("author").toString()));
        rows << qMakePair(t("recipients"), esc(strings(s.value("recipients")).join(QStringLiteral(", "))));
        if (s.value("pages").toInt() > 0)
            rows << qMakePair(t("pages"), QString::number(s.value("pages").toInt()));
        QStringList ocrPages;
        for (const QVariant &p : s.value("ocrPages").toList())
            ocrPages << QString::number(p.toInt());
        if (!ocrPages.isEmpty())
            rows << qMakePair(t("ocrPages"), esc(ocrPages.join(QStringLiteral(", "))));
        out += fieldsList(rows);

        const QVariantMap facts = s.value("keyFacts").toMap();
        QList<QPair<QString, QString>> factRows;
        for (const char *k : {"dates", "people", "places", "organisations", "events"}) {
            const QStringList v = strings(facts.value(QLatin1String(k)));
            if (v.isEmpty())
                continue;
            QString list = QStringLiteral("<ul>");
            for (const QString &x : v)
                list += QStringLiteral("<li>") + esc(x) + QStringLiteral("</li>");
            factRows << qMakePair(t(QLatin1String(k)), list + QStringLiteral("</ul>"));
        }
        if (!factRows.isEmpty())
            out += QStringLiteral("<%1>").arg(h2) + esc(t("keyFacts")) + QStringLiteral("</%1>\n").arg(h2) + fieldsList(factRows);
        const QStringList tags = strings(s.value("tags"));
        if (!tags.isEmpty())
            out += QStringLiteral("<%1>").arg(h2) + esc(t("tags")) + QStringLiteral("</%1>\n").arg(h2) + tagList(tags);
        const QStringList glossary = strings(s.value("glossary"));
        if (!glossary.isEmpty())
            out += QStringLiteral("<%1>").arg(h2) + esc(t("glossary")) + QStringLiteral("</%1>\n").arg(h2) + tagList(glossary);

        const QVariantMap related = s.value("related").toMap();
        QString rel;
        for (const QVariant &v : related.value("kb").toList()) {
            const QVariantMap d = v.toMap();
            rel += QStringLiteral("<li>") + esc(d.value("title").toString()) + QStringLiteral(" <span class=\"muted\">")
                 + esc(d.value("path").toString()) + QStringLiteral("</span></li>");
        }
        QString people;
        for (const QVariant &v : related.value("people").toList())
            people += QStringLiteral("<li>") + esc(v.toMap().value("name").toString()) + QStringLiteral("</li>");
        if (!rel.isEmpty())
            out += QStringLiteral("<%1>").arg(h2) + esc(t("relatedDocuments")) + QStringLiteral("</%1>\n<ul>").arg(h2) + rel + QStringLiteral("</ul>\n");
        if (!people.isEmpty())
            out += QStringLiteral("<%1>").arg(h2) + esc(t("relatedPeople")) + QStringLiteral("</%1>\n<ul>").arg(h2) + people + QStringLiteral("</ul>\n");

        const QVariantList fragments = s.value("fragments").toList();
        if (!fragments.isEmpty()) {
            out += QStringLiteral("<%1>").arg(h2) + esc(t("fragments")) + QStringLiteral(" (%1)</%2>\n").arg(fragments.size()).arg(h2);
            const QString h3 = QStringLiteral("h") + QString::number(std::min(6, h2.mid(1).toInt() + 1));
            for (const QVariant &v : fragments) {
                const QVariantMap f = v.toMap();
                const int from = f.value("pageFrom").toInt(), to = f.value("pageTo").toInt();
                QString heading = esc(QStringLiteral("%1. ").arg(f.value("index").toInt() + 1) + f.value("title").toString());
                if (from > 0)
                    heading += QStringLiteral(" <span class=\"muted\">(")
                             + esc(from == to ? t("page").arg(from) : t("pageRange").arg(from).arg(to)) + QStringLiteral(")</span>");
                out += QStringLiteral("<section class=\"fragment\"><%1>").arg(h3) + heading + QStringLiteral("</%1>\n").arg(h3);
                if (!f.value("summary").toString().isEmpty())
                    out += paragraphs(f.value("summary").toString());
                out += tagList(strings(f.value("tags")));
                if (m_o.includeFragmentText && !f.value("text").toString().isEmpty())
                    out += QStringLiteral("<div class=\"fulltext\" aria-label=\"%1\">\n").arg(esc(t("fullText")))
                         + paragraphs(f.value("text").toString()) + QStringLiteral("</div>\n");
                out += QStringLiteral("</section>\n");
            }
        }
        return out;
    }

    // One file. level: heading level of its name (1 for the file scope)
    QString file(const QVariantMap &r, int index, int level)
    {
        const QString id = QStringLiteral("file-%1").arg(index);
        const QString h = QStringLiteral("h%1").arg(level);
        const QString hs = QStringLiteral("h%1").arg(std::min(6, level + 1));
        const QString hss = QStringLiteral("h%1").arg(std::min(6, level + 2));
        QString out = QStringLiteral("<article class=\"file\" id=\"%1\" aria-labelledby=\"%1-h\">\n").arg(id);
        if (level > 1)
            out += QStringLiteral("<%1 id=\"%2-h\">").arg(h, id) + esc(r.value("name").toString()) + QStringLiteral("</%1>\n").arg(h);
        else
            out += QStringLiteral("<span id=\"%1-h\" hidden>").arg(id) + esc(r.value("name").toString()) + QStringLiteral("</span>\n");
        out += preview(r);
        QList<QPair<QString, QString>> facts;
        facts << qMakePair(t("path"), esc(r.value("fullPath").toString()))
              << qMakePair(t("type"), esc(r.value("type").toString()))
              << qMakePair(t("size"), esc(r.value("size").toString()))
              << qMakePair(t("modified"), esc(r.value("modified").toString()));
        out += fieldsList(facts);
        out += information(r, hs);
        out += proposals(r, hs, id + QStringLiteral("-proposals"));
        out += ai(r, hs, id + QStringLiteral("-ai"));
        out += study(r, hs, hss, id + QStringLiteral("-study"));
        if (level > 1)
            out += QStringLiteral("<p class=\"back\"><a href=\"#toc\">") + esc(t("backToContents")) + QStringLiteral("</a></p>\n");
        return out + QStringLiteral("</article>\n");
    }

    QString page(const QVariantList &records)
    {
        // Grouped by folder, in the order given
        QStringList folders;
        QHash<QString, QList<int>> byFolder;
        for (int i = 0; i < records.size(); ++i) {
            const QString f = records.at(i).toMap().value("folder").toString();
            if (!byFolder.contains(f))
                folders << f;
            byFolder[f] << i;
        }
        const bool single = m_o.scope == QLatin1String("file") && records.size() == 1;

        QString body;
        QString toc;
        if (single) {
            const QVariantMap r = records.first().toMap();
            toc += QStringLiteral("<li><a href=\"#file-0-info\">") + esc(t("information")) + QStringLiteral("</a></li>");
            if (!r.value("proposals").toList().isEmpty())
                toc += QStringLiteral("<li><a href=\"#file-0-proposals\">") + esc(t("proposals")) + QStringLiteral("</a></li>");
            if (!r.value("ai").toMap().isEmpty())
                toc += QStringLiteral("<li><a href=\"#file-0-ai\">") + esc(t("aiDescription")) + QStringLiteral("</a></li>");
            if (!r.value("study").toMap().isEmpty())
                toc += QStringLiteral("<li><a href=\"#file-0-study\">") + esc(t("study")) + QStringLiteral("</a></li>");
            QString article = file(r, 0, 1);
            // The information heading carries the anchor of the contents
            article.replace(QStringLiteral("<h2>") + esc(t("information")), QStringLiteral("<h2 id=\"file-0-info\">") + esc(t("information")));
            body += article;
        } else {
            int folderIndex = 0;
            for (const QString &f : std::as_const(folders)) {
                const QString fid = QStringLiteral("folder-%1").arg(folderIndex++);
                const QString shown = f.isEmpty() ? QStringLiteral("/") : f;
                toc += QStringLiteral("<li><a href=\"#%1\">").arg(fid) + esc(shown) + QStringLiteral("</a> <span class=\"muted\">(")
                     + esc(t("fileCount").arg(byFolder.value(f).size())) + QStringLiteral(")</span><ol>");
                body += QStringLiteral("<section class=\"folder\" aria-labelledby=\"%1\">\n<h2 id=\"%1\">").arg(fid) + esc(shown) + QStringLiteral("</h2>\n");
                for (int i : byFolder.value(f)) {
                    const QVariantMap r = records.at(i).toMap();
                    toc += QStringLiteral("<li><a href=\"#file-%1\">").arg(i) + esc(r.value("name").toString()) + QStringLiteral("</a></li>");
                    body += file(r, i, 3);
                }
                toc += QStringLiteral("</ol></li>");
                body += QStringLiteral("</section>\n");
            }
        }

        QString html = QStringLiteral("<!doctype html>\n<html lang=\"%1\">\n<head>\n<meta charset=\"utf-8\">\n"
                                      "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
                                      "<meta name=\"generator\" content=\"Teca\">\n<title>%2</title>\n<style>%3</style>\n</head>\n<body>\n")
                           .arg(esc(m_o.language), esc(m_o.title), QString::fromUtf8(kCss));
        html += QStringLiteral("<a class=\"skip\" href=\"#main\">") + esc(t("skipToContent")) + QStringLiteral("</a>\n");
        html += QStringLiteral("<header>\n<h1>") + esc(m_o.title) + QStringLiteral("</h1>\n");
        if (!m_o.subtitle.isEmpty())
            html += QStringLiteral("<p class=\"meta\">") + esc(m_o.subtitle) + QStringLiteral("</p>\n");
        if (!single)
            html += QStringLiteral("<p class=\"meta\">") + esc(t("fileCount").arg(records.size())) + QStringLiteral("</p>\n");
        html += QStringLiteral("</header>\n");
        if (!toc.isEmpty())
            html += QStringLiteral("<nav class=\"toc\" aria-labelledby=\"toc\">\n<h2 id=\"toc\">") + esc(t("contents"))
                  + QStringLiteral("</h2>\n<ol>") + toc + QStringLiteral("</ol>\n</nav>\n");
        html += QStringLiteral("<main id=\"main\">\n") + body + QStringLiteral("</main>\n");
        if (!m_o.generatedText.isEmpty())
            html += QStringLiteral("<footer><p>") + esc(m_o.generatedText) + QStringLiteral("</p></footer>\n");
        return html + QStringLiteral("</body>\n</html>\n");
    }

private:
    const MdiHtmlExport::Options &m_o;
    const MdiHtmlExport::Labels &m_l;
    MdiHtmlExport::Labels m_defaults;
    qint64 m_used = 0;
};

} // namespace

MdiHtmlExport::Labels MdiHtmlExport::englishLabels()
{
    return Labels{
        {"contents", "Contents"}, {"skipToContent", "Skip to content"}, {"backToContents", "Back to contents"},
        {"fileCount", "%1 files"}, {"path", "Path"}, {"type", "Type"}, {"size", "Size"}, {"modified", "Modified"},
        {"information", "Information"}, {"noInformation", "No information written for this file."},
        {"title", "Title"}, {"description", "Description"}, {"event", "Event"}, {"place", "Place"}, {"date", "Date"},
        {"author", "Author"}, {"notes", "Notes"}, {"tags", "Tags"}, {"people", "People"},
        {"proposals", "Proposals"}, {"proposalsCaption", "Information inferred by Teca, with its sources"},
        {"field", "Field"}, {"value", "Value"}, {"confidence", "Confidence"}, {"status", "Status"}, {"sources", "Sources"},
        {"pending", "To approve"}, {"accepted", "Approved"}, {"rejected", "Rejected"}, {"superseded", "Replaced"},
        {"precision-day", "day"}, {"precision-month", "month"}, {"precision-year", "year"}, {"precision-range", "period"},
        {"precision-coordinates", "coordinates"},
        {"source-exif", "Photo metadata"}, {"source-video", "Video metadata"}, {"source-filename", "File name"},
        {"source-folder", "Folder"}, {"source-folder-consistency", "Other files of the folder"},
        {"source-caption", "Image description"}, {"source-classification", "Classification"},
        {"source-context", "Context of the archive"}, {"source-knowledge", "Reference document"},
        {"source-web", "Web"}, {"source-model", "Text model"}, {"source-document", "Document study"},
        {"aiDescription", "AI description"}, {"classificationTitle", "Title"}, {"entities", "Named"},
        {"excerpts", "Excerpts"}, {"study", "Document study"}, {"synthesis", "Synthesis"},
        {"documentType", "Kind of document"}, {"documentDate", "Date of the document"}, {"recipients", "Recipients"},
        {"pages", "Pages"}, {"ocrPages", "Pages read by OCR"}, {"keyFacts", "Key facts"}, {"dates", "Dates"},
        {"places", "Places"}, {"organisations", "Organisations"}, {"events", "Events"}, {"glossary", "Terms of the glossary"},
        {"relatedDocuments", "Related reference documents"}, {"relatedPeople", "People named"},
        {"fragments", "Fragments"}, {"page", "page %1"}, {"pageRange", "pages %1–%2"}, {"fullText", "Full text"},
        {"previewOf", "Preview of %1"},
    };
}

QVariantMap MdiHtmlExport::fileRecord(const QString &mdiConnection, int catalogId, const QString &fullPath,
                                      const QVariantMap &facts)
{
    QVariantMap r = facts;
    if (!r.contains("name"))
        r["name"] = QFileInfo(fullPath).fileName();
    r["fullPath"] = fullPath;

    const MdiPeople::Annotation a = MdiPeople::annotation(mdiConnection, catalogId, fullPath);
    if (a.exists)
        r["information"] = QVariantMap{{"title", a.title}, {"description", a.description}, {"event", a.event},
                                       {"place", a.place}, {"date", a.date}, {"author", a.author}, {"notes", a.notes},
                                       {"tags", a.tags}};
    QVariantList people;
    for (const MdiPeople::FilePerson &p : MdiPeople::peopleOfFile(mdiConnection, catalogId, fullPath))
        people << QVariantMap{{"name", p.name}, {"role", p.role}};
    r["people"] = people;

    QVariantList proposals;
    for (const MdiProposals::Proposal &p : MdiProposals::forFile(mdiConnection, catalogId, fullPath))
        if (p.status == QLatin1String("pending") || p.status == QLatin1String("accepted"))
            proposals << p.toVariant();
    r["proposals"] = proposals;

    QVariantMap ai;
    for (const MdiData::Datum &d : MdiData::forFile(mdiConnection, catalogId, fullPath)) {
        if (d.outcome != QLatin1String("completed"))
            continue;
        const QVariantMap v = d.value.toVariantMap();
        if (d.analysis == QLatin1String("caption")) {
            ai["caption"] = v.value("caption");
            ai["tags"] = v.value("tags");
        } else if (d.analysis == QLatin1String("summary")) {
            ai["summary"] = v.value("summary");
            ai["excerpts"] = v.value("excerpts");
            if (!ai.contains("tags"))
                ai["tags"] = v.value("tags");
        } else if (d.analysis == QLatin1String("classification")) {
            ai["title"] = v.value("title");
            ai["description"] = v.value("description");
            ai["categories"] = v.value("categories");
            ai["entities"] = v.value("entities");
            QStringList tags = strings(ai.value("tags"));
            for (const QString &t : strings(v.value("tags")))
                if (!tags.contains(t, Qt::CaseInsensitive))
                    tags << t;
            ai["tags"] = tags;
        } else if ((d.analysis == QLatin1String("thumbnail") || d.analysis == QLatin1String("poster"))
                   && !d.derivedFiles.isEmpty() && !r.contains("preview")) {
            r["preview"] = QDir(d.derivedRoot).filePath(d.derivedFiles.first());
        } else if (d.analysis == QLatin1String("document")) {
            QVariantMap study = v;
            QVariantList fragments;
            for (const MdiDocument::Fragment &f : MdiDocument::fragments(mdiConnection, catalogId, fullPath))
                fragments << f.toJson().toVariantMap();
            study["fragments"] = fragments;
            r["study"] = study;
        }
    }
    if (!ai.isEmpty())
        r["ai"] = ai;
    return r;
}

QString MdiHtmlExport::build(const QVariantList &records, const Options &options, const Labels &labels)
{
    Builder b(options, labels);
    return b.page(records);
}

bool MdiHtmlExport::write(const QString &targetFile, const QString &html, QString *error)
{
    QSaveFile f(targetFile);
    if (!f.open(QIODevice::WriteOnly)) {
        *error = f.errorString();
        return false;
    }
    f.write(html.toUtf8());
    if (!f.commit()) {
        *error = f.errorString();
        return false;
    }
    return true;
}

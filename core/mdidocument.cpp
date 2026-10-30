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
// File Name:   mdidocument.cpp
// Purpose:     Study of text documents (fork only)
/////////////////////////////////////////////////////////////////////////////
*/
#include "core/mdidocument.h"
#include "core/mdidating.h"
#include "core/mdiknowledge.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QHash>
#include <QSet>
#include <QMap>
#include <QDebug>
#include <algorithm>

namespace {

QString foldText(const QString &s)
{
    const QString d = s.normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(d.size());
    for (const QChar c : d)
        if (c.category() != QChar::Mark_NonSpacing)
            out += c.toLower();
    return out;
}

// Words of a text joined by single spaces, for word-boundary matching
QString wordLine(const QString &text)
{
    QString out;
    out.reserve(text.size() + 2);
    out += QLatin1Char(' ');
    bool space = true;
    for (const QChar c : foldText(text)) {
        if (c.isLetterOrNumber()) {
            out += c;
            space = false;
        } else if (!space) {
            out += QLatin1Char(' ');
            space = true;
        }
    }
    if (!space)
        out += QLatin1Char(' ');
    return out;
}

QString wordsKey(const QString &text)
{
    return wordLine(text).trimmed();
}

bool runProgram(const QString &program, const QStringList &args, int timeoutMs, const std::atomic<bool> *stop,
                QByteArray *out, QString *error)
{
    QProcess p;
    p.setProgram(program);
    p.setArguments(args);
    p.start();
    if (!p.waitForStarted(10000)) {
        *error = QStringLiteral("%1 did not start").arg(QFileInfo(program).fileName());
        return false;
    }
    QElapsedTimer clock;
    clock.start();
    QByteArray buffer;
    while (!p.waitForFinished(100)) {
        buffer += p.readAllStandardOutput();
        if ((stop && stop->load()) || clock.elapsed() > timeoutMs) {
            p.kill();
            p.waitForFinished(2000);
            *error = (stop && stop->load()) ? QStringLiteral("stopped") : QStringLiteral("time limit reached");
            return false;
        }
    }
    buffer += p.readAllStandardOutput();
    if (out)
        *out = buffer;
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        const QString err = QString::fromUtf8(p.readAllStandardError()).trimmed();
        *error = err.isEmpty() ? QStringLiteral("%1: exit code %2").arg(QFileInfo(program).fileName()).arg(p.exitCode())
                               : err.section(QLatin1Char('\n'), -1);
        return false;
    }
    return true;
}

QStringList stringList(const QJsonValue &v, int max = 30)
{
    QStringList out;
    const QJsonArray array = v.isArray() ? v.toArray() : QJsonArray{v};
    for (const QJsonValue &x : array) {
        QString s;
        if (x.isString())
            s = x.toString();
        else if (x.isObject())   // {"name": ..} or {"value": ..}
            s = x.toObject().value("name").toString(x.toObject().value("value").toString());
        else if (x.isDouble())
            s = QString::number(x.toDouble());
        s = s.simplified();
        if (!s.isEmpty() && !out.contains(s, Qt::CaseInsensitive) && out.size() < max)
            out << s;
    }
    return out;
}

QJsonArray toArray(const QStringList &list)
{
    QJsonArray a;
    for (const QString &s : list)
        a.append(s);
    return a;
}

const char *const kEntityKinds[] = {"people", "places", "organisations", "events", "dates"};

QJsonObject entitiesFrom(const QJsonObject &answer)
{
    QJsonObject e;
    for (const char *k : kEntityKinds) {
        QJsonValue v = answer.value(QLatin1String(k));
        if (v.isUndefined())
            v = answer.value("entities").toObject().value(QLatin1String(k));
        if (v.isUndefined() && QLatin1String(k) == QLatin1String("organisations"))
            v = answer.value("organizations");
        e[QLatin1String(k)] = toArray(stringList(v, 20));
    }
    return e;
}

void mergeInto(QJsonObject *facts, const QJsonObject &more, int max = 40)
{
    for (const char *k : kEntityKinds) {
        QStringList list = stringList(facts->value(QLatin1String(k)), max);
        for (const QString &s : stringList(more.value(QLatin1String(k)), max))
            if (!list.contains(s, Qt::CaseInsensitive) && list.size() < max)
                list << s;
        (*facts)[QLatin1String(k)] = toArray(list);
    }
}

} // namespace

//======================================================================
// Fragment
//======================================================================

QJsonObject MdiDocument::Fragment::toJson(bool withText) const
{
    QJsonObject o{{"index", index}, {"pageFrom", pageFrom}, {"pageTo", pageTo}, {"title", title},
                  {"summary", summary}, {"tags", toArray(tags)}, {"entities", entities}};
    if (withText)
        o["text"] = text;
    return o;
}

MdiDocument::Fragment MdiDocument::Fragment::fromJson(const QJsonObject &o)
{
    Fragment f;
    f.index = o.value("index").toInt();
    f.pageFrom = o.value("pageFrom").toInt();
    f.pageTo = o.value("pageTo").toInt();
    f.title = o.value("title").toString();
    f.text = o.value("text").toString();
    f.summary = o.value("summary").toString();
    f.tags = stringList(o.value("tags"));
    f.entities = o.value("entities").toObject();
    return f;
}

//======================================================================
// Reading
//======================================================================

QString MdiDocument::findTessdata(const QString &configured)
{
    QStringList candidates;
    if (!configured.isEmpty())
        candidates << configured;
    candidates << QDir::homePath() + QStringLiteral("/teca-models/tessdata")
               << QStringLiteral("/opt/homebrew/share/tessdata")
               << QStringLiteral("/usr/local/share/tessdata")
               << QStringLiteral("/usr/share/tesseract-ocr/5/tessdata")
               << QStringLiteral("/usr/share/tesseract-ocr/4.00/tessdata")
               << QStringLiteral("/usr/share/tessdata");
    for (const QString &c : std::as_const(candidates))
        if (!QDir(c).entryList({QStringLiteral("*.traineddata")}, QDir::Files).isEmpty())
            return c;
    return QString();
}

QString MdiDocument::ocrLanguages(const QString &tessdataDir, const QStringList &wanted)
{
    QStringList have;
    for (const QString &l : wanted)
        if (tessdataDir.isEmpty() || QFileInfo::exists(tessdataDir + QLatin1Char('/') + l + QStringLiteral(".traineddata")))
            have << l;
    return have.join(QLatin1Char('+'));
}

bool MdiDocument::pageNeedsOcr(const QString &text)
{
    int letters = 0;
    for (const QChar c : text)
        if (c.isLetterOrNumber() && ++letters >= 40)
            return false;
    return true;
}

QString MdiDocument::cleanText(const QString &textIn)
{
    QString text = textIn;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    // "docu-\nment" -> "document" (a lower-case letter follows)
    static const QRegularExpression hyphen(QStringLiteral("(\\w)-\\n(\\p{Ll})"));
    text.replace(hyphen, QStringLiteral("\\1\\2"));
    text.removeIf([](QChar c) { return c.category() == QChar::Other_Control && c != QLatin1Char('\n') && c != QLatin1Char('\t'); });
    static const QRegularExpression spaces(QStringLiteral("[ \\t]+\\n"));
    text.replace(spaces, QStringLiteral("\n"));
    static const QRegularExpression blank(QStringLiteral("\\n{3,}"));
    text.replace(blank, QStringLiteral("\n\n"));
    return text.trimmed();
}

QString MdiDocument::ocrImage(const QString &imagePath, const Tools &tools, QString *error)
{
    if (tools.tesseract.isEmpty()) {
        *error = QStringLiteral("tesseract not found");
        return QString();
    }
    const QString dataDir = tools.tessdataDir;
    const QString languages = tools.ocrLanguages.isEmpty() ? ocrLanguages(dataDir) : tools.ocrLanguages;
    // Page segmentation with orientation detection when osd is installed
    const bool osd = !dataDir.isEmpty() && QFileInfo::exists(dataDir + QStringLiteral("/osd.traineddata"));
    QStringList args{imagePath, QStringLiteral("stdout")};
    if (!languages.isEmpty())
        args << QStringLiteral("-l") << languages;
    args << QStringLiteral("--psm") << (osd ? QStringLiteral("1") : QStringLiteral("3"));
    if (!dataDir.isEmpty())
        args << QStringLiteral("--tessdata-dir") << dataDir;
    QByteArray out;
    if (!runProgram(tools.tesseract, args, tools.timeoutMs, tools.stop, &out, error))
        return QString();
    return QString::fromUtf8(out);
}

QList<MdiDocument::Page> MdiDocument::readPages(const QString &path, const Tools &tools, const TextExtractor &other,
                                                QString *tool, QString *reason, const Progress &progress)
{
    QList<Page> pages;
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix != QLatin1String("pdf")) {
        if (!other) {
            *reason = QStringLiteral("no text extraction for .%1").arg(suffix);
            return pages;
        }
        const QString text = other(path, tool, reason);
        if (text.trimmed().isEmpty())
            return pages;
        Page p;
        p.number = 0;   // no pages in this format
        p.text = cleanText(text.left(kMaxChars));
        pages << p;
        return pages;
    }

    if (tools.pdftotext.isEmpty()) {
        *reason = QStringLiteral("pdftotext not found");
        return pages;
    }
    QByteArray out;
    QString err;
    // Pages end with a form feed
    if (!runProgram(tools.pdftotext, {QStringLiteral("-enc"), QStringLiteral("UTF-8"), QStringLiteral("-l"),
                                      QStringLiteral("1000"), path, QStringLiteral("-")},
                    tools.timeoutMs, tools.stop, &out, &err)) {
        *reason = err;
        return pages;
    }
    QStringList tools_{QStringLiteral("pdftotext")};
    QStringList raw = QString::fromUtf8(out).split(QLatin1Char('\f'));
    if (!raw.isEmpty() && raw.last().trimmed().isEmpty())
        raw.removeLast();
    for (int i = 0; i < raw.size(); ++i) {
        Page p;
        p.number = i + 1;
        p.text = raw.at(i);
        pages << p;
    }

    // Scanned pages: rendered and read by Tesseract, on this computer
    QList<int> toRead;
    for (int i = 0; i < pages.size(); ++i)
        if (pageNeedsOcr(pages.at(i).text))
            toRead << i;
    const bool canOcr = !tools.pdftoppm.isEmpty() && !tools.tesseract.isEmpty();
    if (!toRead.isEmpty() && canOcr) {
        QTemporaryDir temp;
        int done = 0;
        for (int i : std::as_const(toRead)) {
            if ((tools.stop && tools.stop->load()) || done >= tools.maxOcrPages)
                break;
            const QString prefix = temp.path() + QStringLiteral("/page");
            const QString number = QString::number(pages.at(i).number);
            QString renderErr;
            if (!runProgram(tools.pdftoppm, {QStringLiteral("-r"), QString::number(tools.dpi), QStringLiteral("-png"),
                                             QStringLiteral("-f"), number, QStringLiteral("-l"), number,
                                             QStringLiteral("-singlefile"), path, prefix},
                            tools.timeoutMs, tools.stop, nullptr, &renderErr)) {
                if (reason && reason->isEmpty())
                    *reason = renderErr;
                continue;
            }
            QString ocrErr;
            const QString text = ocrImage(prefix + QStringLiteral(".png"), tools, &ocrErr);
            QFile::remove(prefix + QStringLiteral(".png"));
            if (!text.trimmed().isEmpty()) {
                pages[i].text = text;
                pages[i].ocr = true;
            } else if (!ocrErr.isEmpty() && reason && reason->isEmpty()) {
                *reason = ocrErr;
            }
            ++done;
            if (progress)
                progress(double(done) / toRead.size(),
                         QStringLiteral("OCR %1 / %2").arg(done).arg(toRead.size()));
        }
        if (done > 0)
            tools_ << QStringLiteral("pdftoppm") << QStringLiteral("tesseract");
    }
    *tool = tools_.join(QLatin1Char('+'));
    int total = 0;
    for (Page &p : pages) {
        p.text = cleanText(p.text);
        total += p.text.size();
        if (total > kMaxChars)
            p.text.clear();
    }
    return pages;
}

//======================================================================
// Fragments
//======================================================================

bool MdiDocument::isHeading(const QString &lineIn)
{
    const QString line = lineIn.trimmed();
    if (line.size() < 3 || line.size() > 90)
        return false;
    if (line.startsWith(QLatin1Char('#')))
        return true;
    const QChar last = line.at(line.size() - 1);
    if (last == QLatin1Char('.') || last == QLatin1Char(',') || last == QLatin1Char(';'))
        return false;
    static const QRegularExpression numbered(QStringLiteral(
        "^(?:\\d{1,2}(?:\\.\\d{1,2}){0,3}\\.?\\s+\\p{Lu}|[IVXLC]{1,6}\\.\\s+\\S|"
        "(?:art(?:icolo|icle|\\.)?|capitolo|chapter|sezione|section|titolo|parte|part|allegato|annex|appendice|appendix|"
        "premessa|introduzione|introduction|conclusioni|conclusion)\\b)"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    if (numbered.match(line).hasMatch())
        return true;
    int letters = 0, upper = 0;
    for (const QChar c : line) {
        if (c.isLetter()) {
            ++letters;
            if (c.isUpper())
                ++upper;
        }
    }
    return letters >= 4 && upper == letters;
}

QList<MdiDocument::Fragment> MdiDocument::fragment(const QList<Page> &pages, int minChars, int maxChars)
{
    struct Block { QString text; int page; bool heading; };
    QList<Block> blocks;
    for (const Page &p : pages) {
        const QStringList paragraphs = p.text.split(QRegularExpression(QStringLiteral("\\n\\s*\\n")), Qt::SkipEmptyParts);
        for (const QString &para : paragraphs) {
            // A heading line at the top of a paragraph stands on its own
            QStringList lines = para.split(QLatin1Char('\n'));
            while (!lines.isEmpty() && isHeading(lines.first()) && lines.size() > 1) {
                blocks << Block{lines.takeFirst().trimmed(), p.number, true};
            }
            const QString body = lines.join(QLatin1Char('\n')).trimmed();
            if (body.isEmpty())
                continue;
            const bool heading = !body.contains(QLatin1Char('\n')) && isHeading(body);
            // A paragraph longer than a fragment: cut at sentence ends
            if (body.size() > maxChars) {
                static const QRegularExpression sentenceEnd(QStringLiteral("(?<=[.!?;:])\\s+"));
                QString chunk;
                for (const QString &s : body.split(sentenceEnd, Qt::SkipEmptyParts)) {
                    if (!chunk.isEmpty() && chunk.size() + s.size() + 1 > maxChars) {
                        blocks << Block{chunk, p.number, false};
                        chunk.clear();
                    }
                    if (s.size() > maxChars) {
                        for (int k = 0; k < s.size(); k += maxChars)
                            blocks << Block{s.mid(k, maxChars), p.number, false};
                        continue;
                    }
                    chunk += (chunk.isEmpty() ? QString() : QStringLiteral(" ")) + s;
                }
                if (!chunk.isEmpty())
                    blocks << Block{chunk, p.number, false};
            } else {
                blocks << Block{body, p.number, heading};
            }
        }
    }

    QList<Fragment> out;
    Fragment current;
    int size = 0;
    auto close = [&]() {
        if (current.text.trimmed().isEmpty())
            return;
        current.index = out.size();
        current.text = current.text.trimmed();
        if (current.title.isEmpty()) {
            QString first = current.text.section(QLatin1Char('\n'), 0, 0).simplified();
            if (first.size() > 80)
                first = first.left(77).section(QLatin1Char(' '), 0, -2) + QStringLiteral("…");
            current.title = first;
        }
        out << current;
        current = Fragment();
        size = 0;
    };
    for (const Block &b : std::as_const(blocks)) {
        if (b.heading && size >= minChars)
            close();
        else if (!b.heading && size > 0 && size + b.text.size() > maxChars && size >= minChars / 2)
            close();
        if (current.text.isEmpty()) {
            current.pageFrom = b.page;
            if (b.heading)
                current.title = b.text.trimmed().remove(QRegularExpression(QStringLiteral("^#+\\s*")));
        }
        current.pageTo = std::max(current.pageTo, b.page);
        current.text += (current.text.isEmpty() ? QString() : QStringLiteral("\n\n")) + b.text;
        size += b.text.size() + 2;
    }
    close();
    return out;
}

QString MdiDocument::detectLanguage(const QString &text)
{
    static const QHash<QString, QStringList> stop = {
        {"it", {"il", "di", "che", "la", "per", "del", "della", "sono", "una", "nel", "con", "non", "alla", "gli", "anche"}},
        {"en", {"the", "and", "of", "to", "is", "that", "for", "with", "was", "this", "are", "be", "by", "from"}},
        {"fr", {"le", "les", "des", "est", "une", "pour", "dans", "qui", "sur", "avec", "pas", "du", "aux"}},
        {"de", {"der", "die", "und", "das", "ist", "nicht", "mit", "den", "von", "ein", "eine", "auf", "sich"}},
        {"es", {"el", "los", "las", "que", "por", "con", "una", "para", "del", "como", "pero", "sus", "esta"}},
    };
    QHash<QString, int> counts;
    const QStringList words = wordLine(text.left(8000)).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &w : words)
        for (auto it = stop.cbegin(); it != stop.cend(); ++it)
            if (it.value().contains(w))
                ++counts[it.key()];
    QString best;
    int bestCount = 0, second = 0;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        if (it.value() > bestCount) {
            second = bestCount;
            bestCount = it.value();
            best = it.key();
        } else if (it.value() > second) {
            second = it.value();
        }
    }
    return (bestCount >= 5 && bestCount > second * 1.3) ? best : QString();
}

QStringList MdiDocument::glossaryFound(const QString &text, const QJsonObject &context)
{
    QStringList found;
    const QString words = wordLine(text);
    for (const QJsonValue &v : context.value("glossary").toArray()) {
        const QJsonObject g = v.toObject();
        const QString term = g.value("term").toString().trimmed();
        const QString preferred = g.value("preferred").toString().trimmed().isEmpty() ? term
                                  : g.value("preferred").toString().trimmed();
        QStringList forms{term, preferred};
        for (const QJsonValue &a : g.value("aliases").toArray())
            forms << a.toString();
        for (const QString &f : std::as_const(forms)) {
            const QString k = wordsKey(f);
            if (k.size() >= 2 && words.contains(QLatin1Char(' ') + k + QLatin1Char(' '))) {
                if (!found.contains(preferred))
                    found << preferred;
                break;
            }
        }
        if (found.size() >= 40)
            break;
    }
    return found;
}

QJsonArray MdiDocument::peopleFound(const QString &text, const QList<PersonName> &people)
{
    QJsonArray out;
    const QString words = wordLine(text);
    for (const PersonName &p : people) {
        const QString first = wordsKey(p.firstName), last = wordsKey(p.lastName);
        if (first.isEmpty() || last.isEmpty())
            continue;   // a single name would match too much
        for (const QString &form : {first + QLatin1Char(' ') + last, last + QLatin1Char(' ') + first}) {
            if (words.contains(QLatin1Char(' ') + form + QLatin1Char(' '))) {
                out.append(QJsonObject{{"personId", p.id}, {"name", p.name}});
                break;
            }
        }
    }
    return out;
}

//======================================================================
// Prompts
//======================================================================

const QStringList &MdiDocument::documentTypes()
{
    static const QStringList types{
        QStringLiteral("letter"), QStringLiteral("email"), QStringLiteral("regulation"), QStringLiteral("results"),
        QStringLiteral("press-release"), QStringLiteral("invoice"), QStringLiteral("receipt"), QStringLiteral("minutes"),
        QStringLiteral("report"), QStringLiteral("contract"), QStringLiteral("form"), QStringLiteral("programme"),
        QStringLiteral("certificate"), QStringLiteral("newsletter"), QStringLiteral("article"), QStringLiteral("manual"),
        QStringLiteral("presentation"), QStringLiteral("list"), QStringLiteral("budget"), QStringLiteral("notice"),
        QStringLiteral("other")};
    return types;
}

QString MdiDocument::fragmentPrompt(const Fragment &f, int count, const QString &docName,
                                    const QString &preamble, const QString &language)
{
    const QString shape = QStringLiteral(
        "{\"title\":\"\",\"summary\":\"\",\"tags\":[],\"people\":[],\"places\":[],\"organisations\":[],"
        "\"events\":[],\"dates\":[]}");
    const QString pages = f.pageFrom > 0 ? (f.pageFrom == f.pageTo ? QStringLiteral(" (page %1)").arg(f.pageFrom)
                                                                    : QStringLiteral(" (pages %1-%2)").arg(f.pageFrom).arg(f.pageTo))
                                         : QString();
    return preamble + QStringLiteral(
        "You are reading part %1 of %2 of the document \"%3\"%4. Give this part a short title, summarise it in "
        "1 to 3 sentences, give up to 8 tags, and list what it names: people, places, organisations, events and "
        "dates (dates as yyyy-mm-dd, yyyy-mm or yyyy when the text makes them certain, otherwise as written). "
        "Only what the text states; do not invent. Write in %5. Answer as JSON only: %6\n\nTEXT:\n%7")
        .arg(f.index + 1).arg(count).arg(docName, pages, language, shape, f.text);
}

QString MdiDocument::reducePrompt(const QStringList &summaries, const QString &docName, const QString &language)
{
    return QStringLiteral(
        "These are the summaries of consecutive parts of the document \"%1\". Merge them into one paragraph of 4 "
        "to 6 sentences that keeps the dates, names, places, bodies and figures. Write in %2. Answer as JSON only: "
        "{\"summary\":\"\"}\n\nPARTS:\n%3").arg(docName, language, summaries.join(QLatin1Char('\n')));
}

QString MdiDocument::synthesisPrompt(const QStringList &parts, const QString &opening, const QString &docName,
                                     int pageCount, const QString &preamble, const QString &language)
{
    const QString shape = QStringLiteral(
        "{\"title\":\"\",\"synthesis\":\"\",\"type\":\"\",\"date\":\"\",\"author\":\"\",\"recipients\":[],"
        "\"keyFacts\":{\"dates\":[],\"people\":[],\"places\":[],\"organisations\":[],\"events\":[]},\"tags\":[]}");
    const QString pages = pageCount > 0 ? QStringLiteral(", %1 pages").arg(pageCount) : QString();
    return preamble + QStringLiteral(
        "You are studying the document \"%1\"%2. Below are its beginning and the summaries of its parts, in order. "
        "Write: a title; a synthesis of 5 to 10 sentences; its type, one of: %3; the date of the document "
        "(yyyy-mm-dd, yyyy-mm or yyyy when certain, otherwise as written, empty if unknown); its author (a person "
        "or a body) and its recipients; its key facts: dates (with what happened), people, places, organisations, "
        "events; up to 12 tags. Only what the text states; do not invent. Write in %4 (the type stays one of the "
        "listed words). Answer as JSON only: %5\n\nBEGINNING:\n%6\n\nPARTS:\n%7")
        .arg(docName, pages, documentTypes().join(QStringLiteral(", ")), language, shape, opening,
             parts.join(QLatin1Char('\n')));
}

//======================================================================
// The study
//======================================================================

QJsonObject MdiDocument::study(const QString &path, const StudyOptions &o, QList<Fragment> *fragmentsOut,
                               QString *outcome, QString *reason, QString *tool)
{
    *outcome = QStringLiteral("completed");
    QJsonObject value;
    value["version"] = kVersion;
    const QString docName = QFileInfo(path).fileName();
    const std::atomic<bool> *stop = o.tools.stop;

    // 1. Text, page by page
    QString readReason;
    const QList<Page> pages = readPages(path, o.tools, o.extractOther, tool, &readReason,
                                        [&o](double f, const QString &d) { if (o.progress) o.progress(0.25 * f, d); });
    QString all;
    QJsonArray ocrPages, emptyPages;
    for (const Page &p : pages) {
        all += p.text + QLatin1Char('\n');
        if (p.ocr)
            ocrPages.append(p.number);
        if (p.number > 0 && p.text.trimmed().isEmpty())
            emptyPages.append(p.number);
    }
    value["pages"] = pages.size() == 1 && pages.first().number == 0 ? 0 : int(pages.size());
    value["ocr"] = !ocrPages.isEmpty();
    value["ocrPages"] = ocrPages;
    value["pagesWithoutText"] = emptyPages;
    value["characters"] = int(all.size());
    if (all.trimmed().size() < 40) {
        if (stop && stop->load()) {
            *outcome = QStringLiteral("failed");
            *reason = QStringLiteral("stopped");
        } else {
            *outcome = readReason.isEmpty() || readReason.startsWith(QLatin1String("no text")) ? QStringLiteral("skipped")
                                                                                                 : QStringLiteral("failed");
            *reason = readReason.isEmpty() ? QStringLiteral("no text") : readReason;
        }
        return value;
    }
    value["language"] = detectLanguage(all);

    // 2. Fragments: larger for long documents, so at most kMaxModelFragments
    int minChars = 1500, maxChars = 3000;
    const double scale = double(all.size()) / (kMaxModelFragments * 2500.0);
    if (scale > 1) {
        minChars = int(std::min(4500.0, minChars * scale));
        maxChars = int(std::min(9000.0, maxChars * scale));
    }
    QList<Fragment> fragments = fragment(pages, minChars, maxChars);

    // 3. Each fragment: summary, tags, entities
    int summarised = 0, failures = 0;
    QString lastError;
    QJsonObject facts;
    QStringList tags;
    for (Fragment &f : fragments) {
        if (stop && stop->load())
            break;
        if (!o.ask || f.index >= kMaxModelFragments)
            continue;
        QString err;
        const QJsonObject a = o.ask(fragmentPrompt(f, fragments.size(), docName, o.preamble, o.language), &err);
        if (a.isEmpty()) {
            lastError = err;
            // The model is not answering: no point in asking for every part
            if (++failures >= 3 && summarised == 0)
                break;
            continue;
        }
        const QString title = a.value("title").toString().simplified();
        if (!title.isEmpty())
            f.title = title;
        f.summary = a.value("summary").toString().simplified();
        f.tags = stringList(a.value("tags"), 8);
        f.entities = entitiesFrom(a);
        mergeInto(&facts, f.entities);
        for (const QString &t : std::as_const(f.tags))
            if (!tags.contains(t, Qt::CaseInsensitive))
                tags << t;
        ++summarised;
        if (o.progress)
            o.progress(0.25 + 0.6 * double(f.index + 1) / std::min<int>(fragments.size(), kMaxModelFragments),
                       QStringLiteral("%1 / %2").arg(f.index + 1).arg(fragments.size()));
    }
    if (stop && stop->load()) {
        *outcome = QStringLiteral("failed");
        *reason = QStringLiteral("stopped");
        return value;
    }

    // 4. The document: map-reduce of the part summaries, then the synthesis
    QStringList parts;
    for (const Fragment &f : std::as_const(fragments)) {
        if (f.summary.isEmpty())
            continue;
        const QString pagesText = f.pageFrom > 0 ? QStringLiteral(" (p. %1-%2)").arg(f.pageFrom).arg(f.pageTo) : QString();
        parts << QStringLiteral("[%1]%2 %3: %4").arg(f.index + 1).arg(pagesText, f.title, f.summary);
    }
    int partsChars = 0;
    for (const QString &p : std::as_const(parts))
        partsChars += p.size();
    if (o.ask && partsChars > 12000) {
        QStringList reduced;
        for (int i = 0; i < parts.size(); i += 15) {
            if (stop && stop->load())
                break;
            QString err;
            const QJsonObject a = o.ask(reducePrompt(parts.mid(i, 15), docName, o.language), &err);
            const QString s = a.value("summary").toString().simplified();
            reduced << QStringLiteral("[%1-%2] ").arg(i + 1).arg(std::min<int>(i + 15, parts.size()))
                       + (s.isEmpty() ? parts.mid(i, 15).join(QLatin1Char(' ')).left(1500) : s);
        }
        parts = reduced;
    }
    QJsonObject synthesis;
    if (o.ask && (summarised > 0 || fragments.size() <= 1)) {
        QString err;
        const QString opening = all.left(3000);
        synthesis = o.ask(synthesisPrompt(parts, opening, docName, value.value("pages").toInt(), o.preamble, o.language), &err);
        if (synthesis.isEmpty())
            lastError = err;
    }
    if (synthesis.value("synthesis").toString().trimmed().isEmpty()) {
        *outcome = QStringLiteral("failed");
        *reason = lastError.isEmpty() ? QStringLiteral("no synthesis in the model answer") : lastError;
        // The fragments read so far are kept with the failed datum
        if (fragmentsOut)
            *fragmentsOut = fragments;
        value["fragmentCount"] = int(fragments.size());
        value["summarisedFragments"] = summarised;
        return value;
    }

    value["title"] = synthesis.value("title").toString().simplified();
    value["synthesis"] = synthesis.value("synthesis").toString().trimmed();
    QString type = synthesis.value("type").toString().trimmed().toLower();
    type.replace(QLatin1Char(' '), QLatin1Char('-'));
    if (type == QLatin1String("result-sheet") || type == QLatin1String("result"))
        type = QStringLiteral("results");
    value["type"] = documentTypes().contains(type) ? type : QStringLiteral("other");
    if (!documentTypes().contains(type) && !type.isEmpty())
        value["typeGiven"] = synthesis.value("type").toString().trimmed();
    const QString date = synthesis.value("date").toString().trimmed();
    value["date"] = date;
    const QList<MdiDating::FoundDate> dates = MdiDating::findDates(date, 1800);
    if (!dates.isEmpty())
        value["dateValue"] = dates.first().date.text();
    value["author"] = synthesis.value("author").toString().simplified();
    value["recipients"] = toArray(stringList(synthesis.value("recipients"), 20));
    QJsonObject keyFacts = entitiesFrom(synthesis.value("keyFacts").toObject());
    mergeInto(&keyFacts, facts);
    value["keyFacts"] = keyFacts;
    QStringList allTags = stringList(synthesis.value("tags"), 12);
    for (const QString &t : std::as_const(tags))
        if (!allTags.contains(t, Qt::CaseInsensitive) && allTags.size() < 20)
            allTags << t;
    value["tags"] = toArray(allTags);

    // 5. What the collection knows: glossary terms, people, reference documents
    value["glossary"] = toArray(glossaryFound(all, o.context));
    QJsonObject related;
    related["people"] = peopleFound(all, o.people);
    if (!o.connectionName.isEmpty())
        related["kb"] = relatedKnowledge(o.connectionName, value, path, o.embed);
    value["related"] = related;
    value["fragmentCount"] = int(fragments.size());
    value["summarisedFragments"] = summarised;
    if (fragments.size() > kMaxModelFragments)
        value["partial"] = true;
    if (fragmentsOut)
        *fragmentsOut = fragments;
    if (o.progress)
        o.progress(1.0, QString());
    return value;
}

QList<MdiProposals::Proposal> MdiDocument::proposals(const QJsonObject &s, const QString &fileName)
{
    QList<MdiProposals::Proposal> out;
    auto make = [&](const QString &field, const QString &value, const QString &precision, double confidence,
                    const QString &detail) {
        if (value.trimmed().isEmpty())
            return;
        MdiProposals::Proposal p;
        p.field = field;
        p.value = value.simplified();
        p.precision = precision;
        p.confidence = confidence;
        MdiProposals::Source src;
        src.kind = QStringLiteral("document");
        src.label = fileName;
        src.detail = detail.left(300);
        src.confidence = confidence;
        p.sources << src;
        out << p;
    };
    const MdiDating::DateValue d = MdiDating::DateValue::fromText(s.value("dateValue").toString());
    if (d.isValid())
        make(QStringLiteral("date"), d.text(), d.precision, d.precision == QLatin1String("day") ? 0.65 : 0.55,
             s.value("date").toString());
    make(QStringLiteral("author"), s.value("author").toString(), QString(), 0.55, s.value("synthesis").toString());
    make(QStringLiteral("title"), s.value("title").toString(), QString(), 0.50, s.value("synthesis").toString());
    return out;
}

QJsonArray MdiDocument::relatedKnowledge(const QString &connectionName, const QJsonObject &s,
                                         const QString &excludePath, const Embedder &embed, int max)
{
    struct Doc { QString title, path; double score = 0; QStringList matched; };
    QHash<int, Doc> docs;
    QSqlDatabase db = QSqlDatabase::database(connectionName);

    // By key facts and tags named in the reference documents
    QStringList terms;
    const QJsonObject facts = s.value("keyFacts").toObject();
    for (const char *k : {"people", "organisations", "events", "places"})
        for (const QString &t : stringList(facts.value(QLatin1String(k)), 40))
            terms << t;
    for (const QString &t : stringList(s.value("tags"), 20))
        terms << t;
    QStringList keys;
    QStringList shown;
    for (const QString &t : std::as_const(terms)) {
        const QString k = wordsKey(t);
        if (k.size() >= 4 && !keys.contains(k)) {
            keys << k;
            shown << t;
        }
    }
    QSqlQuery q(db);
    if (!keys.isEmpty() && q.exec(QStringLiteral("SELECT p.passage_doc_id, p.passage_text, d.doc_title, d.doc_path "
                                                 "FROM mdi_kb_passage p JOIN mdi_kb_document d ON d.doc_id = p.passage_doc_id"))) {
        QHash<int, QSet<int>> hits;
        while (q.next()) {
            const int id = q.value(0).toInt();
            if (q.value(3).toString() == excludePath)
                continue;
            const QString words = wordLine(q.value(1).toString());
            for (int i = 0; i < keys.size(); ++i)
                if (words.contains(QLatin1Char(' ') + keys.at(i) + QLatin1Char(' ')))
                    hits[id].insert(i);
            if (!docs.contains(id) && hits.contains(id))
                docs.insert(id, Doc{q.value(2).toString(), q.value(3).toString(), 0, {}});
        }
        for (auto it = hits.cbegin(); it != hits.cend(); ++it) {
            Doc &d = docs[it.key()];
            d.score = it.value().size();
            for (int i : it.value())
                if (d.matched.size() < 6)
                    d.matched << shown.at(i);
        }
    }
    // By meaning, with the collection's embeddings model
    if (embed) {
        const QVector<float> vector = embed(s.value("title").toString() + QStringLiteral(". ") + s.value("synthesis").toString());
        QSqlQuery m(db);
        if (!vector.isEmpty() && m.exec(QStringLiteral("SELECT passage_model FROM mdi_kb_passage LIMIT 1")) && m.next()) {
            MdiKnowledge::Index index;
            if (index.load(connectionName, m.value(0).toString())) {
                for (const MdiKnowledge::Passage &p : index.search(vector, 10, 0.45)) {
                    QSqlQuery d(db);
                    d.prepare(QStringLiteral("SELECT doc_title, doc_path FROM mdi_kb_document WHERE doc_id = :id"));
                    d.bindValue(":id", p.documentId);
                    if (!d.exec() || !d.next() || d.value(1).toString() == excludePath)
                        continue;
                    Doc &doc = docs[p.documentId];
                    doc.title = d.value(0).toString();
                    doc.path = d.value(1).toString();
                    doc.score = std::max(doc.score, 2.0) + p.score * 2;
                }
            }
        }
    }
    QList<Doc> list;
    for (const Doc &d : std::as_const(docs))
        if (d.score >= 2)
            list << d;
    std::sort(list.begin(), list.end(), [](const Doc &a, const Doc &b) { return a.score > b.score; });
    QJsonArray out;
    for (const Doc &d : std::as_const(list)) {
        if (out.size() >= max)
            break;
        out.append(QJsonObject{{"title", d.title}, {"path", d.path}, {"score", qRound(d.score * 10) / 10.0},
                               {"matched", toArray(d.matched)}});
    }
    return out;
}

QString MdiDocument::searchText(const QJsonObject &s)
{
    QStringList parts;
    for (const char *k : {"title", "synthesis", "author", "date"})
        if (!s.value(QLatin1String(k)).toString().isEmpty())
            parts << s.value(QLatin1String(k)).toString();
    parts << stringList(s.value("tags")) << stringList(s.value("recipients")) << stringList(s.value("glossary"));
    const QJsonObject facts = s.value("keyFacts").toObject();
    for (const char *k : kEntityKinds)
        parts << stringList(facts.value(QLatin1String(k)), 40);
    return parts.join(QLatin1Char(' '));
}

//======================================================================
// Storage
//======================================================================

bool MdiDocument::ensureTables(const QString &connectionName)
{
    QSqlQuery q(QSqlDatabase::database(connectionName));
    const bool ok = q.exec(QLatin1String(R"(
        CREATE TABLE IF NOT EXISTS mdi_fragment (
            frag_catalog_id INTEGER NOT NULL,
            frag_full_path  TEXT    NOT NULL,
            frag_index      INTEGER NOT NULL,
            frag_page_from  INTEGER,
            frag_page_to    INTEGER,
            frag_title      TEXT,
            frag_text       TEXT,
            frag_summary    TEXT,
            frag_tags       TEXT,
            frag_entities   TEXT,
            PRIMARY KEY (frag_catalog_id, frag_full_path, frag_index)
        )
    )"));
    if (!ok)
        qWarning() << "MdiDocument::ensureTables:" << q.lastError().text();
    return ok;
}

bool MdiDocument::storeFragments(const QString &connectionName, int catalogId, const QString &fullPath,
                                 const QList<Fragment> &fragments)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    if (!db.transaction())
        return false;
    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM mdi_fragment WHERE frag_catalog_id = :catalog_id AND frag_full_path = :full_path"));
    del.bindValue(":catalog_id", catalogId);
    del.bindValue(":full_path", fullPath);
    if (!del.exec()) {
        db.rollback();
        return false;
    }
    QSqlQuery ins(db);
    ins.prepare(QLatin1String(R"(
        INSERT INTO mdi_fragment (frag_catalog_id, frag_full_path, frag_index, frag_page_from, frag_page_to,
                                  frag_title, frag_text, frag_summary, frag_tags, frag_entities)
        VALUES (:catalog_id, :full_path, :index, :page_from, :page_to, :title, :text, :summary, :tags, :entities)
    )"));
    for (const Fragment &f : fragments) {
        ins.bindValue(":catalog_id", catalogId);
        ins.bindValue(":full_path", fullPath);
        ins.bindValue(":index", f.index);
        ins.bindValue(":page_from", f.pageFrom);
        ins.bindValue(":page_to", f.pageTo);
        ins.bindValue(":title", f.title);
        ins.bindValue(":text", f.text);
        ins.bindValue(":summary", f.summary);
        ins.bindValue(":tags", QString::fromUtf8(QJsonDocument(toArray(f.tags)).toJson(QJsonDocument::Compact)));
        ins.bindValue(":entities", QString::fromUtf8(QJsonDocument(f.entities).toJson(QJsonDocument::Compact)));
        if (!ins.exec()) {
            qWarning() << "MdiDocument::storeFragments:" << ins.lastError().text();
            db.rollback();
            return false;
        }
    }
    return db.commit();
}

QList<MdiDocument::Fragment> MdiDocument::fragments(const QString &connectionName, int catalogId, const QString &fullPath)
{
    QList<Fragment> list;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QStringLiteral("SELECT * FROM mdi_fragment WHERE frag_catalog_id = :catalog_id AND frag_full_path = :full_path "
                             "ORDER BY frag_index"));
    q.bindValue(":catalog_id", catalogId);
    q.bindValue(":full_path", fullPath);
    if (!q.exec())
        return list;
    while (q.next()) {
        Fragment f;
        f.index = q.value("frag_index").toInt();
        f.pageFrom = q.value("frag_page_from").toInt();
        f.pageTo = q.value("frag_page_to").toInt();
        f.title = q.value("frag_title").toString();
        f.text = q.value("frag_text").toString();
        f.summary = q.value("frag_summary").toString();
        f.tags = stringList(QJsonDocument::fromJson(q.value("frag_tags").toString().toUtf8()).array());
        f.entities = QJsonDocument::fromJson(q.value("frag_entities").toString().toUtf8()).object();
        list << f;
    }
    return list;
}

bool MdiDocument::removeFragments(const QString &connectionName, int catalogId, const QString &fullPath)
{
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QStringLiteral("DELETE FROM mdi_fragment WHERE frag_catalog_id = :catalog_id AND frag_full_path = :full_path"));
    q.bindValue(":catalog_id", catalogId);
    q.bindValue(":full_path", fullPath);
    return q.exec();
}

bool MdiDocument::clearCatalog(const QString &connectionName, int catalogId)
{
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QStringLiteral("DELETE FROM mdi_fragment WHERE frag_catalog_id = :catalog_id"));
    q.bindValue(":catalog_id", catalogId);
    return q.exec();
}

QString MdiDocument::moveStatement()
{
    return QStringLiteral("UPDATE OR IGNORE mdi_fragment SET frag_catalog_id = :new_id "
                          "WHERE frag_catalog_id = :old_id AND frag_full_path = :full_path");
}

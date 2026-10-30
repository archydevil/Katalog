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
// File Name:   mditranscriptfilter.cpp
// Purpose:     Real speech only in transcripts (fork only, SpecMediaIntelligence.md)
/////////////////////////////////////////////////////////////////////////////
*/
#include "core/mditranscriptfilter.h"
#include "core/mdidata.h"
#include "core/mdifts.h"

#include <QJsonObject>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QSet>
#include <QHash>
#include <QDateTime>
#include <QDebug>

const QString MdiTranscriptFilter::kVersion = QStringLiteral("filter-v1");

namespace {

// Whole passages whisper writes on silence, music or noise, in the languages
// of the owner's archive and the ones whisper most often falls back to.
// Matched on the normalized text (lower case, no accents, no punctuation).
const char *const kExactPhrases[] = {
    // English
    "thank you", "thank you very much", "thank you so much", "thanks", "thank you all",
    "thanks for watching", "thank you for watching", "thank you so much for watching",
    "thank you for watching this video", "thanks for listening", "thank you for listening",
    "please subscribe", "subscribe", "like and subscribe", "please like and subscribe",
    "don't forget to subscribe", "subscribe to my channel", "subscribe to the channel",
    "see you next time", "see you in the next video", "see you in the next one",
    "we'll be right back", "we will be right back", "be right back", "we'll be back",
    "bye", "bye bye", "goodbye", "you", "the end", "oh", "uh", "um", "hmm", "mm", "mhm", "ah", "huh",
    "music", "applause", "laughter", "laughing", "silence", "gunshot", "gunshots", "explosion",
    "music playing", "upbeat music", "soft music", "dramatic music", "intro music", "outro music",
    "background music", "instrumental music", "piano music", "inaudible", "blank audio", "no audio",
    "cheering", "beep", "static", "noise", "sigh", "sighs", "coughing", "footsteps",
    // Italian
    "grazie", "grazie mille", "grazie a tutti", "grazie per la visione", "grazie per aver guardato",
    "grazie per l'ascolto", "grazie per la visione e alla prossima", "iscriviti al canale",
    "iscrivetevi al canale", "ciao", "ciao ciao", "alla prossima", "a presto", "musica", "applausi",
    "risate", "silenzio", "fine", "sottotitoli",
    // Spanish, Portuguese
    "gracias", "muchas gracias", "gracias por ver", "gracias por ver el video", "suscribete",
    "suscribete al canal", "adios", "hasta la proxima", "aplausos", "obrigado", "obrigada",
    "muito obrigado", "obrigado por assistir", "inscreva se",
    // French
    "merci", "merci beaucoup", "merci d'avoir regarde", "merci d'avoir regarde cette video",
    "abonnez vous", "musique", "applaudissements", "au revoir", "a bientot",
    // German
    "danke", "vielen dank", "danke schon", "danke furs zuschauen", "bis zum nachsten mal",
    "tschuss", "musik", "applaus",
    // Russian
    "спасибо", "спасибо за просмотр", "продолжение следует", "музыка", "аплодисменты",
    // Japanese, Chinese, Korean
    "ご視聴ありがとうございました", "ありがとうございました", "チャンネル登録よろしくお願いします",
    "おやすみなさい", "音楽", "拍手", "谢谢观看", "謝謝觀看", "感谢观看", "谢谢", "謝謝", "字幕", "音乐",
    "시청해주셔서 감사합니다", "감사합니다", "구독과 좋아요", "음악",
};

// Credits and channel lines: a passage holding one of these is not speech
const char *const kContainedPhrases[] = {
    "amara org", "subtitles by", "subtitled by", "captions by", "captioning by", "closed captioning",
    "transcribed by", "transcription by", "translated by", "music by", "song by",
    "sottotitoli creati", "sottotitoli a cura", "sottotitoli e revisione", "sottotitoli di",
    "untertitel im auftrag", "untertitel der", "untertitelung", "sous titres realises", "sous titrage",
    "subtitulos realizados", "subtitulos por", "legendas pela", "legendado por",
    "субтитры", "редактор субтитров", "请不吝点赞", "請不吝點贊", "字幕由", "字幕提供",
    "ご視聴ありがとう", "チャンネル登録", "www ", "http",
};

const QSet<QString> &exactSet()
{
    static const QSet<QString> set = [] {
        QSet<QString> s;
        for (const char *p : kExactPhrases)
            s.insert(MdiTranscriptFilter::normalize(QString::fromUtf8(p)));
        return s;
    }();
    return set;
}

const QStringList &containedList()
{
    static const QStringList list = [] {
        QStringList l;
        for (const char *p : kContainedPhrases) {
            const QString raw = QString::fromUtf8(p);
            // "www " keeps its trailing space: a word, not a prefix of one
            l << (raw.endsWith(' ') ? MdiTranscriptFilter::normalize(raw) + ' ' : MdiTranscriptFilter::normalize(raw));
        }
        return l;
    }();
    return list;
}

bool isCjk(QChar c)
{
    const QChar::Script s = c.script();
    return s == QChar::Script_Han || s == QChar::Script_Hiragana || s == QChar::Script_Katakana;
}

QStringList tokens(const QString &normalized)
{
    return normalized.split(' ', Qt::SkipEmptyParts);
}

// "Thank you. Thank you. Thank you." is one line said once
QString collapseRepeatedSentences(const QString &text)
{
    static const QRegularExpression split(QStringLiteral("(?<=[.!?。！？…])\\s*"));
    const QStringList parts = text.split(split, Qt::SkipEmptyParts);
    if (parts.size() < 2)
        return text;
    const QString first = MdiTranscriptFilter::normalize(parts.first());
    for (const QString &p : parts)
        if (MdiTranscriptFilter::normalize(p) != first)
            return text;
    return parts.first().trimmed();
}

bool containsWords(const QString &haystack, const QString &needle)
{
    return !needle.isEmpty() && (QLatin1Char(' ') + haystack + QLatin1Char(' ')).contains(QLatin1Char(' ') + needle + QLatin1Char(' '));
}

} // namespace

QString MdiTranscriptFilter::normalize(const QString &text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_KD);
    QString out;
    out.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (c.category() == QChar::Mark_NonSpacing || c.category() == QChar::Mark_SpacingCombining)
            continue;
        out += c.isLetterOrNumber() ? c.toCaseFolded() : QChar(' ');
    }
    return out.simplified();
}

QString MdiTranscriptFilter::stripSoundTags(const QString &text)
{
    static const QRegularExpression tags(QStringLiteral(
        "\\[[^\\]]*\\]|\\([^)]*\\)|\\*[^*]*\\*|【[^】]*】|（[^）]*）|［[^］]*］|♪[^♪]*♪|♫[^♫]*♫|[♪♫🎵🎶]"));
    QString t = text;
    t.remove(tags);
    // An unclosed tag at an end: "[Music" or "*gunshot"
    static const QRegularExpression open(QStringLiteral("^\\s*[\\[(*（【][^\\])*）】]*$|^[^\\[(*（【]*[\\])*）】]\\s*$"));
    if (open.match(t).hasMatch() && normalize(t).split(' ', Qt::SkipEmptyParts).size() <= 3)
        t.clear();
    return t.simplified();
}

bool MdiTranscriptFilter::isKnownHallucination(const QString &normalized)
{
    if (normalized.isEmpty())
        return true;
    if (exactSet().contains(normalized))
        return true;
    const QString padded = normalized + QLatin1Char(' ');
    for (const QString &c : containedList())
        if (padded.contains(c))
            return true;
    return false;
}

int MdiTranscriptFilter::wordCount(const QString &text)
{
    int words = 0;
    for (const QString &token : text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
        int cjk = 0;
        for (const QChar c : token)
            if (isCjk(c))
                ++cjk;
        words += cjk > 0 ? qMax(1, (cjk + 1) / 2) : 1;
    }
    return words;
}

QString MdiTranscriptFilter::srt(const QJsonArray &passages)
{
    auto time = [](double seconds) {
        const qint64 ms = qint64(seconds * 1000.0);
        return QStringLiteral("%1:%2:%3,%4").arg(ms / 3600000, 2, 10, QLatin1Char('0'))
            .arg((ms / 60000) % 60, 2, 10, QLatin1Char('0'))
            .arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
            .arg(ms % 1000, 3, 10, QLatin1Char('0'));
    };
    QString out;
    int n = 0;
    for (const QJsonValue &v : passages) {
        const QJsonObject p = v.toObject();
        out += QStringLiteral("%1\n%2 --> %3\n%4\n\n").arg(++n)
                   .arg(time(p.value("start").toDouble()), time(p.value("end").toDouble()), p.value("text").toString());
    }
    return out;
}

MdiTranscriptFilter::Outcome MdiTranscriptFilter::filter(const QJsonArray &passages, double duration,
                                                         const QString &hint, const QString &fileName)
{
    Outcome o;
    o.input = passages.size();

    const QString hintNorm = normalize(hint);
    const QSet<QString> hintWords = [&] {
        const QStringList t = tokens(hintNorm);
        return QSet<QString>(t.cbegin(), t.cend());
    }();
    const QString baseName = normalize(QFileInfo(fileName).completeBaseName());
    const QString fullName = normalize(QFileInfo(fileName).fileName());

    QStringList previous;            // normalized text of the previous 2 passages read
    QHash<QString, int> kept;        // normalized text -> times kept
    double lastEnd = 0;

    for (const QJsonValue &v : passages) {
        const QJsonObject p = v.toObject();
        const double start = p.value("start").toDouble();
        const double end   = p.value("end").toDouble();
        lastEnd = qMax(lastEnd, end);

        QString text = collapseRepeatedSentences(stripSoundTags(p.value("text").toString()));
        const QString norm = normalize(text);
        const QStringList words = tokens(norm);

        bool drop = norm.isEmpty() || isKnownHallucination(norm);
        // The same line as one of the 2 before it: a repetition loop
        drop = drop || previous.contains(norm);
        // Kept already as often as a real line repeats
        drop = drop || kept.value(norm) >= kMaxRepeats;
        // The prompt hint read back, whole or in part
        if (!drop && !hintNorm.isEmpty()) {
            if (containsWords(hintNorm, norm))
                drop = true;
            else if (words.size() >= 3) {
                int inHint = 0;
                for (const QString &w : words)
                    if (hintWords.contains(w))
                        ++inHint;
                drop = double(inHint) / double(words.size()) >= kHintShare;
            }
        }
        // The recording's own name
        if (!drop && !baseName.isEmpty())
            drop = norm == baseName || norm == fullName
                || (words.size() >= 2 && tokens(baseName).size() >= 2 && containsWords(baseName, norm));
        // One or two words said over and over inside a passage: "la la la la la la"
        if (!drop && words.size() >= 6) {
            const QSet<QString> distinct(words.cbegin(), words.cend());
            drop = distinct.size() <= 2;
        }

        previous.append(norm);
        if (previous.size() > 2)
            previous.removeFirst();

        if (drop) {
            ++o.dropped;
            o.changed = true;
            continue;
        }
        kept[norm]++;
        if (text != p.value("text").toString().trimmed())
            o.changed = true;
        o.passages.append(QJsonObject{{"start", start}, {"end", end}, {"text", text}});
        o.words   += wordCount(text);
        o.covered += qMax(0.0, end - start);
    }

    o.duration = duration > 0 ? duration : lastEnd;
    const double coverage = o.duration > 0 ? o.covered / o.duration : 1.0;
    o.noSpeech = o.passages.isEmpty()
              || (o.passages.size() < kMinPassages && o.words < kMinWords && coverage < kShortCoverage)
              || (coverage < kMinCoverage && o.words < kCoverageWords);
    if (o.noSpeech)
        o.changed = true;
    return o;
}

//----------------------------------------------------------------------
MdiTranscriptFilter::CleanCounts MdiTranscriptFilter::cleanStored(const QString &connectionName, int catalogId,
                                                                  const QString &hint, const std::atomic<bool> *stop)
{
    CleanCounts counts;
    QSet<QString> affected;   // files whose transcript changed

    for (MdiData::Datum d : MdiData::forCatalog(connectionName, catalogId, QStringLiteral("transcript"))) {
        if (stop && stop->load())
            break;
        ++counts.checked;
        const Outcome o = filter(d.value.value("passages").toArray(), d.value.value("duration").toDouble(),
                                 hint, d.fullPath);
        if (!o.changed)
            continue;

        // Derived files this app made, under its own folder (MDI-F14): never a source
        const bool ownFolder = d.derivedRoot.endsWith(QLatin1String("katalog_mdi"));
        if (o.noSpeech) {
            const QStringList files = d.derivedFiles;
            d.outcome = QStringLiteral("skipped");
            d.reason  = QStringLiteral("no speech found");
            d.value   = QJsonObject{{"filter", kVersion}};
            d.derivedFiles.clear();
            d.date    = QDateTime::currentDateTime().toString(Qt::ISODate);
            if (!MdiData::upsert(connectionName, d)) {
                ++counts.failed;
                continue;
            }
            if (ownFolder)
                for (const QString &rel : files)
                    if (rel.endsWith(QLatin1String(".srt")))
                        QFile::remove(QDir(d.derivedRoot).filePath(rel));
            ++counts.removed;
        } else {
            d.value["passages"] = o.passages;
            d.value["filter"]   = kVersion;
            d.date = QDateTime::currentDateTime().toString(Qt::ISODate);
            if (!MdiData::upsert(connectionName, d)) {
                ++counts.failed;
                continue;
            }
            if (ownFolder)
                for (const QString &rel : d.derivedFiles)
                    if (rel.endsWith(QLatin1String(".srt"))) {
                        QFile out(QDir(d.derivedRoot).filePath(rel));
                        if (out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
                            out.write(srt(o.passages).toUtf8());
                    }
            ++counts.cleaned;
        }
        affected.insert(d.fullPath);

        // What was built from the old transcript is rebuilt by the next run
        MdiData::removeDatum(connectionName, catalogId, d.fullPath, QStringLiteral("segments"));
        MdiData::removeDatum(connectionName, catalogId, d.fullPath, QStringLiteral("embeddings"));
        MdiData::removeEmbeddings(connectionName, catalogId, d.fullPath,
                                  {QStringLiteral("passage"), QStringLiteral("segment")});
        for (const MdiData::Datum &s : MdiData::forFile(connectionName, catalogId, d.fullPath))
            if (s.analysis == QLatin1String("summary")
                && s.value.value("source").toString() == QLatin1String("whisper"))
                MdiData::removeDatum(connectionName, catalogId, d.fullPath, QStringLiteral("summary"));
    }

    if (affected.isEmpty())
        return counts;

    // The catalog's clusters: members of these files removed, marked stale so
    // the next run groups the segments again
    const QList<MdiData::Datum> clusterData = MdiData::forCatalog(connectionName, catalogId, QStringLiteral("clusters"));
    if (!clusterData.isEmpty()) {
        MdiData::Datum c = clusterData.first();
        QJsonArray clusters;
        for (const QJsonValue &v : c.value.value("clusters").toArray()) {
            QJsonObject cluster = v.toObject();
            QJsonArray members;
            for (const QJsonValue &m : cluster.value("members").toArray())
                if (!affected.contains(m.toObject().value("fullPath").toString()))
                    members.append(m);
            if (members.size() < 2)
                continue;
            cluster["members"] = members;
            clusters.append(cluster);
        }
        c.value["clusters"] = clusters;
        c.value["stale"]    = true;
        MdiData::upsert(connectionName, c);
    }
    // The instant index holds the summaries removed: rebuilt on the next schedule
    MdiFts::markStale(connectionName, catalogId);
    return counts;
}

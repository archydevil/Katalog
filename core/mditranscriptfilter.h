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
// File Name:   mditranscriptfilter.h
// Purpose:     Real speech only in transcripts (fork only, SpecMediaIntelligence.md)
// Description: On silence, music or noise whisper writes lines nobody said:
//              "Thank you.", "We'll be right back.", "*music*", subtitle
//              credits, the prompt hint, the same line again and again. This
//              filter keeps the passages that are speech, and tells when what
//              remains is too little to be a transcript (owner's request,
//              2026-10-30: "Se non ci sta parlato non mettere nulla").
//              Applied to every new transcript, and to the stored ones on
//              request (cleanStored), which also invalidates what was built
//              from a changed transcript.
/////////////////////////////////////////////////////////////////////////////
*/
#ifndef MDITRANSCRIPTFILTER_H
#define MDITRANSCRIPTFILTER_H

#include <QString>
#include <QStringList>
#include <QJsonArray>
#include <QList>
#include <atomic>

class MdiTranscriptFilter
{
public:
    // Recorded with each transcript ("filter" in its value and parameters)
    static const QString kVersion;

    // What is too little to be a transcript. After filtering, the result is
    // "no speech" when nothing is left, or when
    //  - fewer than kMinPassages passages AND fewer than kMinWords words
    //    remain AND they cover less than kShortCoverage of the recording
    //    (a short clip with one real sentence is kept), or
    //  - the passages left cover less than kMinCoverage of the recording
    //    AND hold fewer than kCoverageWords words (a long recording with a
    //    few stray words).
    static constexpr int    kMinPassages   = 3;
    static constexpr int    kMinWords      = 12;
    static constexpr double kShortCoverage = 0.5;
    static constexpr double kMinCoverage   = 0.08;
    static constexpr int    kCoverageWords = 40;
    // The same passage text is kept at most this many times in a recording
    static constexpr int    kMaxRepeats    = 2;
    // A passage is a hint echo when this share of its words (3 or more) are hint words
    static constexpr double kHintShare     = 0.8;

    struct Outcome {
        QJsonArray passages;       // kept: [{start, end, text}], text cleaned of sound tags
        int        input    = 0;   // passages read
        int        dropped  = 0;   // passages removed
        int        words    = 0;   // words kept (CJK: two characters count as one word)
        double     covered  = 0;   // seconds covered by the passages kept
        double     duration = 0;   // recording length used for the coverage
        bool       noSpeech = false;
        bool       changed  = false;   // passages differ from the input
    };

    // passages: whisper's [{start, end, text}] in seconds. duration <= 0: the
    // last passage's end. hint: the --prompt given to whisper (may be empty).
    // fileName: the recording's file name (its base name is not speech).
    static Outcome filter(const QJsonArray &passages, double duration, const QString &hint,
                          const QString &fileName);

    // Lower case, accents and punctuation removed, spaces collapsed
    static QString normalize(const QString &text);
    // Bracketed, starred and musical-note sound tags removed: "[Music]", "*gunshot*", "(applause)", "♪ … ♪"
    static QString stripSoundTags(const QString &text);
    // A known hallucination (normalized text): "thank you", "grazie", subtitle credits...
    static bool    isKnownHallucination(const QString &normalized);
    static int     wordCount(const QString &text);

    // Subtitles for the passages
    static QString srt(const QJsonArray &passages);

    struct CleanCounts {
        int checked = 0;   // completed transcripts read
        int cleaned = 0;   // passages removed, transcript kept
        int removed = 0;   // nothing left: now "skipped", "no speech found"
        int failed  = 0;   // not rewritten (store busy)
    };

    // Applies the filter to a catalog's stored transcripts, on a connection to
    // mdi_data.db of the caller's thread. A changed transcript is rewritten
    // with its .srt (under the derived folder the app made, never a source
    // file); one with nothing left becomes "skipped" / "no speech found", its
    // value cleared and its .srt removed. For both, what was built from it is
    // invalidated so the next enrichment run rebuilds it: the file's topic
    // segments, its search-by-meaning index (datum and passage / segment
    // vectors), a summary made from this transcript; the catalog's topic
    // clusters are marked stale (members of these files removed) and its
    // instant-search index state is cleared.
    static CleanCounts cleanStored(const QString &connectionName, int catalogId, const QString &hint,
                                   const std::atomic<bool> *stop = nullptr);
};

#endif // MDITRANSCRIPTFILTER_H

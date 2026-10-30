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
// File Name:   mdidating.cpp
// Purpose:     Dating photos and videos (fork only)
/////////////////////////////////////////////////////////////////////////////
*/
#include "core/mdidating.h"
#include "core/mdidata.h"
#include "core/mdiwebsearch.h"

#include <QFileInfo>
#include <QMimeDatabase>
#include <QProcess>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSet>
#include <QMap>
#include <QtMath>
#include <QDebug>
#include <algorithm>
#include <cmath>

#include <KFileMetaData/ExtractorCollection>
#include <KFileMetaData/Extractor>
#include <KFileMetaData/SimpleExtractionResult>
#include <KFileMetaData/Properties>

namespace {

//======================================================================
// Text helpers
//======================================================================

// Lower case without accents; map[j] is the index in `s` of folded char j
QString foldMapped(const QString &s, QVector<int> *map = nullptr)
{
    QString out;
    out.reserve(s.size());
    if (map) {
        map->clear();
        map->reserve(s.size());
    }
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (c.isSurrogate()) {
            out += c;
            if (map) map->append(i);
            continue;
        }
        const QString d = QString(c).normalized(QString::NormalizationForm_D);
        for (const QChar k : d) {
            if (k.category() == QChar::Mark_NonSpacing)
                continue;
            out += k.toLower();
            if (map) map->append(i);
        }
    }
    return out;
}

QString fold(const QString &s)
{
    return foldMapped(s);
}

// Words of a folded text, joined by single spaces ("l'aquila" -> "l aquila")
QString wordsOf(const QString &folded)
{
    QString out;
    bool space = false;
    for (const QChar c : folded) {
        if (c.isLetterOrNumber()) {
            if (space && !out.isEmpty())
                out += QLatin1Char(' ');
            out += c;
            space = false;
        } else {
            space = true;
        }
    }
    return out;
}

QString key(const QString &s)
{
    return wordsOf(fold(s));
}

// "COMPETIZIONI" -> "Competizioni"; short acronyms ("ECH") and mixed case stay
QString displayWord(const QString &w)
{
    bool hasLower = false, hasLetter = false;
    for (const QChar c : w) {
        hasLower = hasLower || c.isLower();
        hasLetter = hasLetter || c.isLetter();
    }
    if (!hasLetter || hasLower || w.size() <= 3)
        return w;
    return w.left(1) + w.mid(1).toLower();
}

int currentYear()
{
    return QDate::currentDate().year();
}

double clamp01(double v)
{
    return std::max(0.0, std::min(0.99, v));
}

// Strongest source first; each other independent source adds part of the
// remaining doubt: c0 + (1 - c0) * (1 - prod(1 - c_i / 2))
double combineConfidences(QList<double> list)
{
    if (list.isEmpty())
        return 0;
    std::sort(list.begin(), list.end(), std::greater<double>());
    double rest = 1;
    for (int i = 1; i < list.size(); ++i)
        rest *= 1 - 0.5 * list.at(i);
    return clamp01(list.first() + (1 - list.first()) * (1 - rest));
}

//======================================================================
// Vocabulary
//======================================================================

const QHash<QString, int> &monthNames()
{
    static const QHash<QString, int> names = [] {
        QHash<QString, int> h;
        const char *lists[12] = {
            "gennaio|january|janvier|januar|enero|gen|jan|ene|janv",
            "febbraio|february|fevrier|februar|febrero|feb|febr|fev|fevr",
            "marzo|march|mars|marz|maerz|mar|mrz",
            "aprile|april|avril|abril|apr|avr|abr",
            "maggio|may|mai|mayo|mag",
            "giugno|june|juin|juni|junio|giu|jun",
            "luglio|july|juillet|juli|julio|lug|jul|juil",
            "agosto|august|aout|ago|aug",
            "settembre|september|septembre|septiembre|setiembre|set|sep|sept",
            "ottobre|october|octobre|oktober|octubre|ott|oct|okt",
            "novembre|november|noviembre|nov",
            "dicembre|december|decembre|dezember|diciembre|dic|dec|dez"};
        for (int m = 0; m < 12; ++m)
            for (const QString &n : QString::fromLatin1(lists[m]).split(QLatin1Char('|')))
                h.insert(n, m + 1);
        return h;
    }();
    return names;
}

// Long month names only: a folder named "Mar" says nothing
bool isLongMonthName(const QString &folded)
{
    return folded.size() >= 4 && monthNames().contains(folded)
        && folded != QLatin1String("sept") && folded != QLatin1String("janv")
        && folded != QLatin1String("febr") && folded != QLatin1String("fevr")
        && folded != QLatin1String("juil") && folded != QLatin1String("maerz");
}

QString monthPattern()
{
    static const QString pattern = [] {
        QStringList names = monthNames().keys();
        std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
            return a.size() > b.size();
        });
        return QStringLiteral("(?:") + names.join(QLatin1Char('|')) + QStringLiteral(")\\.?");
    }();
    return pattern;
}

int monthOf(const QString &word)
{
    QString w = word;
    if (w.endsWith(QLatin1Char('.')))
        w.chop(1);
    return monthNames().value(w, 0);
}

// Words that only say "photos" or "a folder": dropped from event names
const QSet<QString> &genericWords()
{
    static const QSet<QString> set = [] {
        QSet<QString> s;
        for (const char *w : {"foto", "fotos", "fotografie", "fotografia", "photo", "photos", "photographs",
                              "pictures", "picture", "pics", "pic", "immagini", "immagine", "images", "image",
                              "img", "video", "videos", "filmati", "filmato", "movies", "movie", "clip", "clips",
                              "dcim", "camera", "raw", "jpg", "jpeg", "nef", "cr2", "arw", "heic", "originali",
                              "originale", "originals", "original", "edited", "edit", "modificate", "export",
                              "exports", "esportate", "selezione", "selezionate", "selected", "select", "best",
                              "migliori", "varie", "vari", "misc", "various", "nuova", "cartella", "new", "folder",
                              "backup", "copia", "copy", "download", "downloads", "whatsapp", "telegram",
                              "screenshots", "screenshot", "desktop", "documents", "documenti", "archivio",
                              "archive", "scansioni", "scans", "album", "untitled", "senza", "titolo", "users",
                              "home", "volumes", "media", "files", "file", "data", "dati", "mobile", "iphone",
                              "android", "cellulare", "telefono", "phone", "canon", "nikon", "sony", "gopro",
                              "dji", "apple", "samsung", "huawei", "xiaomi", "lightroom", "photoshop", "hd",
                              "sd", "def", "definitive", "bis", "tmp", "temp", "old", "vecchie", "nuove"})
            s.insert(QString::fromLatin1(w));
        return s;
    }();
    return set;
}

// Kept in event names, ignored when reading them
const QSet<QString> &stopWords()
{
    static const QSet<QString> set = [] {
        QSet<QString> s;
        for (const char *w : {"di", "del", "della", "delle", "dei", "degli", "dello", "da", "dal", "dalla", "a",
                              "al", "alla", "ai", "agli", "e", "ed", "il", "lo", "la", "le", "gli", "i", "un",
                              "una", "uno", "in", "con", "per", "su", "sul", "sulla", "tra", "fra", "the", "of",
                              "and", "at", "on", "for", "to", "from", "with", "les", "des", "du", "de", "der",
                              "die", "das", "und", "y", "el", "los", "las", "en", "vs", "contro", "d", "l"})
            s.insert(QString::fromLatin1(w));
        return s;
    }();
    return set;
}

// Named events: a championship, a cup... (a web lookup can find these)
const QSet<QString> &specificEventWords()
{
    static const QSet<QString> set = [] {
        QSet<QString> s;
        for (const char *w : {"campionato", "campionati", "championship", "championships", "europei", "europeo",
                              "europeans", "european", "mondiali", "mondiale", "world", "worlds", "olimpiadi",
                              "olimpiade", "olympics", "olympic", "olympiad", "paralimpiadi", "paralympics",
                              "paralympic", "giochi", "games", "universiadi", "universiade", "universiad", "cup",
                              "coppa", "trofeo", "trophy", "memorial", "torneo", "tournament", "tornei", "gp",
                              "prix", "open", "festival", "expo", "fiera", "rally", "maratona", "marathon",
                              "finale", "finali", "final", "finals", "qualificazioni", "qualification",
                              "qualifier", "qualifiers", "playoff", "playoffs", "derby", "gala", "premio",
                              "award", "awards", "premiazione", "premiazioni", "cerimonia", "ceremony",
                              "inaugurazione", "congresso", "convegno", "conference", "summit", "assemblea",
                              "raduno", "championnat", "meisterschaft", "campeonato", "grand", "trofei",
                              "challenge", "series", "league", "lega", "serie", "nazionali", "nationals",
                              "national", "regionali", "provinciali", "assoluti", "assoluto", "italiani"})
            s.insert(QString::fromLatin1(w));
        return s;
    }();
    return set;
}

// Words that qualify an event's name ("Winter cup", "Junior open"): never a
// person's name, so they may go into a web query
const QSet<QString> &eventDescriptorWords()
{
    static const QSet<QString> set = [] {
        QSet<QString> s;
        for (const char *w : {"winter", "summer", "spring", "autumn", "fall", "indoor", "outdoor", "junior", "juniores",
                              "juniors", "senior", "seniores", "seniors", "master", "masters", "youth", "giovanili",
                              "giovanile", "under", "cadetti", "allievi", "esordienti", "ragazzi", "femminile",
                              "maschile", "women", "womens", "men", "mens", "ladies", "international", "internazionale",
                              "internazionali", "nazionale", "regionale", "provinciale", "invernale", "invernali",
                              "estivo", "estivi", "estiva", "estive", "primaverile", "autunnale", "club", "team",
                              "squadre", "squadra", "staffetta", "staffette", "relay", "golden", "classic", "classica",
                              "city", "citta", "town", "grande", "gran", "mondo", "europa", "europe", "italia",
                              "italy", "universitari", "university", "college", "scuola", "school", "scolastici",
                              "veterani", "veterans", "elite", "pro", "amatori", "amateur", "open", "invitational"})
            s.insert(QString::fromLatin1(w));
        return s;
    }();
    return set;
}

// Events of everyday life: named by the owner, rarely found on the web
const QSet<QString> &genericEventWords()
{
    static const QSet<QString> set = [] {
        QSet<QString> s;
        for (const char *w : {"competizione", "competizioni", "competition", "competitions", "gara", "gare",
                              "race", "races", "partita", "partite", "match", "matches", "evento", "eventi",
                              "event", "events", "manifestazione", "manifestazioni", "festa", "feste", "party",
                              "matrimonio", "nozze", "wedding", "battesimo", "baptism", "comunione", "cresima",
                              "compleanno", "birthday", "laurea", "graduation", "anniversario", "anniversary",
                              "concerto", "concert", "mostra", "exhibition", "spettacolo", "show", "sfilata",
                              "parata", "parade", "processione", "visita", "visit", "viaggio", "viaggi", "trip",
                              "gita", "gite", "vacanza", "vacanze", "holiday", "holidays", "vacation", "ferie",
                              "stage", "corso", "course", "seminario", "seminar", "workshop", "riunione",
                              "meeting", "incontro", "allenamento", "allenamenti", "training", "ritiro", "camp",
                              "campus", "cena", "pranzo", "dinner", "lunch", "tour", "giro", "escursione",
                              "excursion", "trasferta", "cerimonie", "saggio", "recita", "celebrazione"})
            s.insert(QString::fromLatin1(w));
        return s;
    }();
    return set;
}

// Holidays: a date once the year is known
struct Holiday { const char *names; int month; int day; int days; };
const Holiday kHolidays[] = {
    {"natale|christmas|xmas|noel|weihnachten|navidad", 12, 24, 3},
    {"vigilia", 12, 24, 1},
    {"ferragosto", 8, 15, 1},
    {"halloween", 10, 31, 1},
    {"san valentino|valentino|valentine|valentines", 2, 14, 1},
    {"epifania|befana", 1, 6, 1},
    {"santo stefano", 12, 26, 1},
    {"pasqua|easter|paques|ostern|pascua", 0, 0, 3},     // computed
    {"pasquetta|easter monday", 0, 1, 1},                // Easter + 1
};

QDate easterSunday(int year)
{
    // Anonymous Gregorian algorithm (Meeus/Jones/Butcher)
    const int a = year % 19, b = year / 100, c = year % 100, d = b / 4, e = b % 4;
    const int f = (b + 8) / 25, g = (b - f + 1) / 3, h = (19 * a + b - d - g + 15) % 30;
    const int i = c / 4, k = c % 4, l = (32 + 2 * e + 2 * i - h - k) % 7;
    const int m = (a + 11 * h + 22 * l) / 451;
    const int month = (h + l - 7 * m + 114) / 31, day = ((h + l - 7 * m + 114) % 31) + 1;
    return QDate(year, month, day);
}

MdiDating::DateValue holidayDate(const QString &holidayKey, int year)
{
    for (const Holiday &h : kHolidays) {
        const QStringList names = QString::fromLatin1(h.names).split(QLatin1Char('|'));
        if (names.first() != holidayKey)
            continue;
        if (h.month == 0) {
            const QDate easter = easterSunday(year);
            if (h.day == 0)
                return MdiDating::DateValue::range(easter.addDays(-1), easter.addDays(1));
            return MdiDating::DateValue::day(year, easter.addDays(1).month(), easter.addDays(1).day());
        }
        const QDate from(year, h.month, h.day);
        if (h.days <= 1)
            return MdiDating::DateValue::day(year, h.month, h.day);
        return MdiDating::DateValue::range(from, from.addDays(h.days - 1));
    }
    return {};
}

// Seasons, as months of the same year
struct Season { const char *names; int fromMonth; int toMonth; };
const Season kSeasons[] = {
    {"estate|summer|ete|sommer|verano", 6, 8},
    {"primavera|spring|printemps|fruhling", 3, 5},
    {"autunno|autumn|fall|automne|herbst|otono", 9, 11},
};

//======================================================================
// Places: a small built-in list, for folder names and GPS positions.
// The first name is the one shown; "*" marks a name that is also a common
// word or a first name (weak, never sent to the web).
//======================================================================

struct PlaceRow { const char *names; double lat; double lon; };
const PlaceRow kPlaces[] = {
    // Italy
    {"Roma|rome", 41.90, 12.50}, {"Milano|milan|milan", 45.46, 9.19}, {"Napoli|naples", 40.85, 14.27},
    {"Torino|turin", 45.07, 7.69}, {"Palermo", 38.12, 13.36}, {"Genova|genoa", 44.41, 8.93},
    {"Bologna", 44.49, 11.34}, {"Firenze|florence*", 43.77, 11.26}, {"Bari", 41.12, 16.87},
    {"Catania", 37.50, 15.09}, {"Venezia|venice", 45.44, 12.32}, {"Verona", 45.44, 10.99},
    {"Messina", 38.19, 15.55}, {"Padova|padua", 45.41, 11.88}, {"Trieste", 45.65, 13.78},
    {"Brescia", 45.54, 10.21}, {"Parma", 44.80, 10.33}, {"Taranto", 40.47, 17.24}, {"Modena", 44.65, 10.93},
    {"Reggio Calabria", 38.11, 15.65}, {"Reggio Emilia", 44.70, 10.63}, {"Perugia", 43.11, 12.39},
    {"Livorno", 43.55, 10.31}, {"Ravenna", 44.42, 12.20}, {"Cagliari", 39.22, 9.12}, {"Foggia", 41.46, 15.54},
    {"Rimini", 44.06, 12.57}, {"Salerno", 40.68, 14.77}, {"Ferrara", 44.84, 11.62}, {"Sassari", 40.73, 8.56},
    {"Pescara", 42.46, 14.21}, {"Bergamo", 45.70, 9.67}, {"Trento", 46.07, 11.12}, {"Vicenza", 45.55, 11.55},
    {"Bolzano|bozen", 46.50, 11.35}, {"Ancona", 43.62, 13.52}, {"Udine", 46.06, 13.24}, {"Lecce", 40.35, 18.17},
    {"Pisa", 43.72, 10.40}, {"Lucca", 43.84, 10.50}, {"Siena", 43.32, 11.33}, {"Como*", 45.81, 9.09},
    {"Aosta", 45.74, 7.32}, {"L'Aquila|aquila", 42.35, 13.40}, {"Potenza", 40.64, 15.80},
    {"Campobasso", 41.56, 14.66}, {"Catanzaro", 38.91, 16.59}, {"Cosenza", 39.30, 16.25},
    {"Matera", 40.67, 16.60}, {"Sanremo|san remo", 43.82, 7.78}, {"Cortina d'Ampezzo|cortina", 46.54, 12.14},
    {"Courmayeur", 45.79, 6.97}, {"Livigno", 46.54, 10.14}, {"Bormio", 46.47, 10.37},
    {"Sestriere", 44.96, 6.88}, {"Monza", 45.58, 9.27}, {"Novara", 45.45, 8.62}, {"Piacenza", 45.05, 9.69},
    {"Arezzo", 43.46, 11.88}, {"Latina*", 41.47, 12.90}, {"Terni", 42.56, 12.65}, {"Siracusa|syracuse", 37.08, 15.29},
    {"Brindisi", 40.63, 17.94}, {"Caserta", 41.07, 14.33}, {"Varese", 45.82, 8.83}, {"La Spezia|spezia", 44.10, 9.82},
    {"Pesaro", 43.91, 12.91}, {"Treviso", 45.67, 12.24}, {"Rovigo", 45.07, 11.79}, {"Mantova|mantua", 45.16, 10.79},
    {"Cremona", 45.13, 10.02}, {"Pavia", 45.19, 9.16}, {"Alessandria", 44.91, 8.61}, {"Asti", 44.90, 8.21},
    {"Cuneo*", 44.39, 7.55}, {"Biella", 45.56, 8.05}, {"Lignano Sabbiadoro|lignano", 45.68, 13.13},
    {"Riccione", 44.00, 12.65}, {"Jesolo", 45.53, 12.64}, {"Gorizia", 45.94, 13.62}, {"Pordenone", 45.96, 12.66},
    {"Belluno", 46.14, 12.22}, {"Sondrio", 46.17, 9.87}, {"Lecco", 45.86, 9.40}, {"Lodi*", 45.31, 9.50},
    {"Savona", 44.31, 8.48}, {"Imperia", 43.89, 8.03}, {"Massa*", 44.04, 10.14}, {"Carrara", 44.08, 10.10},
    {"Grosseto", 42.76, 11.11}, {"Viterbo", 42.42, 12.11}, {"Rieti", 42.40, 12.86}, {"Frosinone", 41.64, 13.35},
    {"Benevento*", 41.13, 14.78}, {"Avellino", 40.91, 14.79}, {"Agrigento", 37.31, 13.58}, {"Trapani", 38.02, 12.51},
    {"Ragusa*", 36.93, 14.73}, {"Enna", 37.57, 14.28}, {"Caltanissetta", 37.49, 14.06}, {"Nuoro", 40.32, 9.33},
    {"Oristano", 39.90, 8.59}, {"Olbia", 40.92, 9.50}, {"Teramo", 42.66, 13.70}, {"Chieti", 42.35, 14.17},
    {"Macerata", 43.30, 13.45}, {"Ascoli Piceno|ascoli", 42.85, 13.58}, {"Fermo*", 43.16, 13.72},
    {"Forlì|forli", 44.22, 12.04}, {"Cesena", 44.14, 12.24}, {"Imola", 44.35, 11.71}, {"Faenza", 44.29, 11.88},
    {"San Marino", 43.94, 12.45},
    // Europe
    {"Parigi|paris|paris", 48.86, 2.35}, {"Londra|london", 51.51, -0.13}, {"Berlino|berlin", 52.52, 13.40},
    {"Madrid", 40.42, -3.70}, {"Barcellona|barcelona", 41.39, 2.17}, {"Valencia*", 39.47, -0.38},
    {"Siviglia|sevilla|seville", 37.39, -5.98}, {"Malaga", 36.72, -4.42}, {"Lisbona|lisbon|lisboa", 38.72, -9.14},
    {"Porto*|oporto", 41.15, -8.61}, {"Vienna|wien", 48.21, 16.37}, {"Praga|prague|praha", 50.08, 14.44},
    {"Budapest", 47.50, 19.04}, {"Varsavia|warsaw|warszawa", 52.23, 21.01}, {"Cracovia|krakow|cracow", 50.06, 19.94},
    {"Danzica|gdansk", 54.35, 18.65}, {"Breslavia|wroclaw", 51.11, 17.03}, {"Poznan", 52.41, 16.93},
    {"Zagabria|zagreb", 45.81, 15.98}, {"Spalato|split*", 43.51, 16.44}, {"Fiume|rijeka", 45.33, 14.44},
    {"Osijek|osiek", 45.55, 18.69}, {"Zara|zadar", 44.12, 15.23}, {"Dubrovnik", 42.65, 18.09},
    {"Pola|pula", 44.87, 13.85}, {"Lubiana|ljubljana", 46.06, 14.51}, {"Maribor", 46.55, 15.65},
    {"Belgrado|belgrade|beograd", 44.79, 20.45}, {"Novi Sad", 45.27, 19.83}, {"Sarajevo", 43.86, 18.41},
    {"Podgorica", 42.44, 19.26}, {"Skopje", 41.99, 21.43}, {"Tirana", 41.33, 19.82}, {"Sofia*", 42.70, 23.32},
    {"Plovdiv", 42.14, 24.75}, {"Varna", 43.21, 27.91}, {"Bucarest|bucharest|bucuresti", 44.43, 26.10},
    {"Cluj-Napoca|cluj", 46.77, 23.59}, {"Atene|athens|athina", 37.98, 23.73},
    {"Salonicco|thessaloniki", 40.64, 22.94}, {"Istanbul", 41.01, 28.98}, {"Ankara", 39.93, 32.86},
    {"Antalya", 36.90, 30.71}, {"Smirne|izmir", 38.42, 27.14}, {"Bratislava", 48.15, 17.11},
    {"Kosice", 48.72, 21.26}, {"Brno", 49.20, 16.61}, {"Ostrava", 49.82, 18.26},
    {"Monaco di Baviera|munich|munchen|muenchen", 48.14, 11.58}, {"Amburgo|hamburg", 53.55, 9.99},
    {"Francoforte|frankfurt", 50.11, 8.68}, {"Colonia|cologne|koln|koeln", 50.94, 6.96},
    {"Düsseldorf|dusseldorf|duesseldorf", 51.23, 6.77}, {"Stoccarda|stuttgart", 48.78, 9.18},
    {"Dresda|dresden", 51.05, 13.74}, {"Lipsia|leipzig", 51.34, 12.37}, {"Norimberga|nuremberg|nurnberg", 49.45, 11.08},
    {"Hannover|hanover", 52.37, 9.73}, {"Brema|bremen", 53.08, 8.80}, {"Dortmund", 51.51, 7.47},
    {"Essen*", 51.46, 7.01}, {"Heidelberg", 49.40, 8.67}, {"Innsbruck", 47.27, 11.39},
    {"Salisburgo|salzburg", 47.81, 13.04}, {"Graz", 47.07, 15.44}, {"Linz", 48.31, 14.29},
    {"Zurigo|zurich|zuerich", 47.38, 8.54}, {"Ginevra|geneva|geneve|genf", 46.20, 6.14},
    {"Losanna|lausanne", 46.52, 6.63}, {"Berna|bern|berne", 46.95, 7.45}, {"Basilea|basel|bale", 47.56, 7.59},
    {"Lugano", 46.00, 8.95}, {"Bellinzona", 46.19, 9.02}, {"St. Moritz|st moritz|san moritz|saint moritz", 46.50, 9.84},
    {"Amsterdam", 52.37, 4.90}, {"Rotterdam", 51.92, 4.48}, {"L'Aia|the hague|den haag", 52.08, 4.30},
    {"Eindhoven", 51.44, 5.47}, {"Utrecht", 52.09, 5.12}, {"Bruxelles|brussels|brussel", 50.85, 4.35},
    {"Anversa|antwerp|antwerpen", 51.22, 4.40}, {"Gand|ghent|gent", 51.05, 3.72}, {"Liegi|liege", 50.63, 5.57},
    {"Lussemburgo|luxembourg", 49.61, 6.13}, {"Lione|lyon", 45.76, 4.84}, {"Marsiglia|marseille", 43.30, 5.37},
    {"Nizza|nice*", 43.70, 7.27}, {"Tolosa|toulouse", 43.60, 1.44}, {"Bordeaux", 44.84, -0.58},
    {"Nantes", 47.22, -1.55}, {"Strasburgo|strasbourg", 48.57, 7.75}, {"Lille", 50.63, 3.06},
    {"Montpellier", 43.61, 3.88}, {"Grenoble", 45.19, 5.72}, {"Chamonix", 45.92, 6.87},
    {"Monaco*|monte carlo|montecarlo", 43.74, 7.42}, {"Dublino|dublin", 53.35, -6.26},
    {"Edimburgo|edinburgh", 55.95, -3.19}, {"Glasgow", 55.86, -4.25}, {"Manchester", 53.48, -2.24},
    {"Liverpool", 53.41, -2.98}, {"Birmingham", 52.49, -1.89}, {"Leeds", 53.80, -1.55}, {"Cardiff", 51.48, -3.18},
    {"Belfast", 54.60, -5.93}, {"Copenaghen|copenhagen|kobenhavn", 55.68, 12.57}, {"Aarhus", 56.16, 10.20},
    {"Stoccolma|stockholm", 59.33, 18.07}, {"Göteborg|gothenburg|goteborg", 57.71, 11.97}, {"Malmö|malmo", 55.60, 13.00},
    {"Oslo", 59.91, 10.75}, {"Bergen", 60.39, 5.32}, {"Helsinki", 60.17, 24.94}, {"Tampere", 61.50, 23.76},
    {"Reykjavík|reykjavik", 64.15, -21.94}, {"Tallinn", 59.44, 24.75}, {"Riga", 56.95, 24.11},
    {"Vilnius", 54.69, 25.28}, {"Kaunas", 54.90, 23.89}, {"Minsk", 53.90, 27.56}, {"Kiev|kyiv", 50.45, 30.52},
    {"Leopoli|lviv", 49.84, 24.03}, {"Odessa|odesa", 46.48, 30.72}, {"Charkiv|kharkiv", 49.99, 36.23},
    {"Mosca|moscow|moskva", 55.76, 37.62}, {"San Pietroburgo|saint petersburg|st petersburg", 59.94, 30.31},
    {"Kazan", 55.80, 49.11}, {"Soči|sochi|soci", 43.60, 39.73}, {"Chişinău|chisinau", 47.01, 28.86},
    {"La Valletta|valletta", 35.90, 14.51}, {"Nicosia", 35.17, 33.36}, {"Limassol", 34.68, 33.04},
    {"Tbilisi", 41.72, 44.79}, {"Erevan|yerevan", 40.18, 44.51}, {"Baku", 40.41, 49.87},
    {"Andorra la Vella|andorra", 42.51, 1.52}, {"Vaduz", 47.14, 9.52},
    // World
    {"New York", 40.71, -74.01}, {"Los Angeles", 34.05, -118.24}, {"Chicago", 41.88, -87.63},
    {"Washington", 38.91, -77.04}, {"Boston", 42.36, -71.06}, {"San Francisco", 37.77, -122.42},
    {"Miami", 25.76, -80.19}, {"Atlanta", 33.75, -84.39}, {"Las Vegas", 36.17, -115.14}, {"Houston", 29.76, -95.37},
    {"Toronto", 43.65, -79.38}, {"Montréal|montreal", 45.50, -73.57}, {"Vancouver", 49.28, -123.12},
    {"Calgary", 51.05, -114.07}, {"Città del Messico|mexico city|ciudad de mexico", 19.43, -99.13},
    {"Rio de Janeiro|rio", -22.91, -43.17}, {"San Paolo|sao paulo", -23.55, -46.63},
    {"Buenos Aires", -34.60, -58.38}, {"Santiago del Cile|santiago*", -33.45, -70.67}, {"Lima*", -12.05, -77.04},
    {"Bogotà|bogota", 4.71, -74.07}, {"Tokyo|tokio", 35.68, 139.69}, {"Osaka", 34.69, 135.50},
    {"Kyoto", 35.01, 135.77}, {"Sapporo", 43.06, 141.35}, {"Nagano", 36.65, 138.18},
    {"Pechino|beijing|peking", 39.90, 116.41}, {"Shanghai", 31.23, 121.47}, {"Hong Kong", 22.32, 114.17},
    {"Seul|seoul", 37.57, 126.98}, {"Pyeongchang", 37.37, 128.39}, {"Busan", 35.18, 129.08},
    {"Taipei", 25.03, 121.57}, {"Bangkok", 13.76, 100.50}, {"Singapore", 1.35, 103.82},
    {"Giacarta|jakarta", -6.21, 106.85}, {"Manila", 14.60, 120.98}, {"Nuova Delhi|new delhi|delhi", 28.61, 77.21},
    {"Mumbai|bombay", 19.08, 72.88}, {"Dubai", 25.20, 55.27}, {"Abu Dhabi", 24.45, 54.38}, {"Doha", 25.29, 51.53},
    {"Riad|riyadh", 24.71, 46.68}, {"Teheran|tehran", 35.69, 51.39}, {"Tel Aviv", 32.09, 34.78},
    {"Gerusalemme|jerusalem", 31.77, 35.21}, {"Il Cairo|cairo", 30.04, 31.24}, {"Tunisi|tunis", 36.81, 10.18},
    {"Algeri|algiers", 36.75, 3.06}, {"Rabat", 34.02, -6.84}, {"Casablanca", 33.57, -7.59},
    {"Marrakech|marrakesh", 31.63, -7.99}, {"Città del Capo|cape town", -33.92, 18.42},
    {"Johannesburg", -26.20, 28.05}, {"Nairobi", -1.29, 36.82}, {"Dakar", 14.72, -17.47},
    {"Sydney", -33.87, 151.21}, {"Melbourne", -37.81, 144.96}, {"Brisbane", -27.47, 153.03},
    {"Perth", -31.95, 115.86}, {"Auckland", -36.85, 174.76}, {"Almaty", 43.24, 76.95}, {"Astana", 51.17, 71.45},
    {"Tashkent", 41.30, 69.24},
};

// Countries: places without a position
const char *const kCountries[] = {
    "Italia|italy", "Francia|france", "Germania|germany|deutschland", "Spagna|spain|espana",
    "Portogallo|portugal", "Croazia|croatia|hrvatska", "Slovenia", "Serbia", "Bosnia", "Montenegro", "Albania",
    "Macedonia", "Grecia|greece", "Turchia|turkey|turkiye", "Austria", "Svizzera|switzerland|schweiz|suisse",
    "Belgio|belgium", "Paesi Bassi|netherlands|olanda|holland", "Polonia|poland", "Ungheria|hungary", "Romania",
    "Bulgaria", "Slovacchia|slovakia", "Repubblica Ceca|czech republic|czechia", "Svezia|sweden",
    "Norvegia|norway", "Finlandia|finland", "Danimarca|denmark", "Islanda|iceland", "Irlanda|ireland",
    "Regno Unito|united kingdom|uk", "Inghilterra|england", "Scozia|scotland", "Galles|wales", "Russia",
    "Ucraina|ukraine", "Bielorussia|belarus", "Lituania|lithuania", "Lettonia|latvia", "Estonia",
    "Moldavia|moldova", "Georgia*", "Armenia", "Azerbaigian|azerbaijan", "Cipro|cyprus", "Malta",
    "Giappone|japan", "Cina|china", "Corea|korea", "India", "Thailandia|thailand", "Australia",
    "Nuova Zelanda|new zealand", "Stati Uniti|united states|usa", "Canada", "Messico|mexico", "Brasile|brazil",
    "Argentina", "Cile|chile", "Perù|peru", "Colombia", "Egitto|egypt", "Marocco|morocco", "Tunisia", "Algeria",
    "Sudafrica|south africa", "Kenya", "Israele|israel", "Emirati Arabi Uniti|emirati|uae", "Qatar",
    "Kazakistan|kazakhstan", "Uzbekistan", "Sicilia|sicily", "Sardegna|sardinia", "Toscana|tuscany",
    "Lombardia|lombardy", "Piemonte|piedmont", "Veneto", "Liguria", "Puglia|apulia", "Calabria", "Campania",
    "Lazio", "Abruzzo", "Marche", "Umbria", "Emilia-Romagna|emilia romagna", "Friuli", "Trentino",
    "Alto Adige|sudtirol|south tyrol", "Valle d'Aosta|val d aosta", "Basilicata", "Molise", "Dolomiti|dolomites",
    "Baviera|bavaria|bayern", "Dalmazia|dalmatia", "Istria", "Costa Azzurra|cote d azur", "Provenza|provence",
};

struct PlaceEntry {
    QString display;
    double  lat = qQNaN();
    double  lon = qQNaN();
    bool    weak = false;
    bool    country = false;
};

const QHash<QString, PlaceEntry> &placeIndex()
{
    static const QHash<QString, PlaceEntry> index = [] {
        QHash<QString, PlaceEntry> h;
        auto addRow = [&h](const char *names, double lat, double lon, bool country) {
            const QStringList list = QString::fromUtf8(names).split(QLatin1Char('|'));
            PlaceEntry e;
            e.display = list.first();
            e.display.remove(QLatin1Char('*'));
            e.lat = lat;
            e.lon = lon;
            e.country = country;
            for (QString n : list) {
                const bool weak = n.endsWith(QLatin1Char('*'));
                n.remove(QLatin1Char('*'));
                PlaceEntry entry = e;
                entry.weak = weak;
                const QString k = key(n);
                if (!k.isEmpty() && !h.contains(k))
                    h.insert(k, entry);
            }
        };
        for (const PlaceRow &r : kPlaces)
            addRow(r.names, r.lat, r.lon, false);
        for (const char *c : kCountries)
            addRow(c, qQNaN(), qQNaN(), true);
        return h;
    }();
    return index;
}

double distanceKm(double lat1, double lon1, double lat2, double lon2)
{
    const double r = 6371.0;
    const double dLat = qDegreesToRadians(lat2 - lat1), dLon = qDegreesToRadians(lon2 - lon1);
    const double a = std::sin(dLat / 2) * std::sin(dLat / 2)
                   + std::cos(qDegreesToRadians(lat1)) * std::cos(qDegreesToRadians(lat2))
                   * std::sin(dLon / 2) * std::sin(dLon / 2);
    return 2 * r * std::atan2(std::sqrt(a), std::sqrt(1 - a));
}

//======================================================================
// Context helpers
//======================================================================

struct ContextEntity {
    QString name, kind, notes;
    QStringList keys;      // folded name and aliases
};

QList<ContextEntity> contextEntities(const QJsonObject &context)
{
    QList<ContextEntity> list;
    for (const QJsonValue &v : context.value("entities").toArray()) {
        const QJsonObject o = v.toObject();
        ContextEntity e;
        e.name  = o.value("name").toString().trimmed();
        e.kind  = o.value("kind").toString().trimmed().toLower();
        e.notes = o.value("notes").toString();
        if (e.name.isEmpty())
            continue;
        e.keys << key(e.name);
        for (const QJsonValue &a : o.value("aliases").toArray())
            if (!key(a.toString()).isEmpty())
                e.keys << key(a.toString());
        list << e;
    }
    return list;
}

// Glossary term or alias (folded) -> preferred form
QHash<QString, QString> glossaryForms(const QJsonObject &context)
{
    QHash<QString, QString> h;
    for (const QJsonValue &v : context.value("glossary").toArray()) {
        const QJsonObject g = v.toObject();
        const QString term = g.value("term").toString().trimmed();
        const QString preferred = g.value("preferred").toString().trimmed().isEmpty() ? term
                                  : g.value("preferred").toString().trimmed();
        if (preferred.isEmpty())
            continue;
        if (!key(term).isEmpty())
            h.insert(key(term), preferred);
        for (const QJsonValue &a : g.value("aliases").toArray())
            if (!key(a.toString()).isEmpty())
                h.insert(key(a.toString()), preferred);
    }
    return h;
}

// " a b c " contains " b c " on word boundaries
bool containsWords(const QString &haystackWords, const QString &needleWords)
{
    if (needleWords.isEmpty())
        return false;
    return (QLatin1Char(' ') + haystackWords + QLatin1Char(' '))
        .contains(QLatin1Char(' ') + needleWords + QLatin1Char(' '));
}

MdiDating::DateValue contextPeriod(const QJsonObject &context)
{
    const QList<MdiDating::FoundDate> found = MdiDating::findDates(context.value("period").toString(), 1800);
    MdiDating::DateValue period;
    for (const MdiDating::FoundDate &f : found) {
        if (!period.isValid()) {
            period = f.date;
            continue;
        }
        period = MdiDating::DateValue::range(std::min(period.from, f.date.from), std::max(period.to, f.date.to));
    }
    return period;
}

QString excerptAround(const QString &text, int position, int length, int radius = 140)
{
    const int from = std::max(0, position - radius);
    const int to = std::min<int>(text.size(), position + length + radius);
    QString out = text.mid(from, to - from).simplified();
    if (from > 0)
        out.prepend(QStringLiteral("…"));
    if (to < text.size())
        out.append(QStringLiteral("…"));
    return out;
}

} // namespace

//======================================================================
// DateValue
//======================================================================

QString MdiDating::DateValue::text() const
{
    if (!isValid())
        return QString();
    if (precision == QLatin1String("day"))
        return from.toString(QStringLiteral("yyyy-MM-dd"));
    if (precision == QLatin1String("month"))
        return from.toString(QStringLiteral("yyyy-MM"));
    if (precision == QLatin1String("year"))
        return QString::number(from.year());
    // Whole years: "2013/2015"; otherwise days
    if (from.month() == 1 && from.day() == 1 && to.month() == 12 && to.day() == 31)
        return QStringLiteral("%1/%2").arg(from.year()).arg(to.year());
    return from.toString(QStringLiteral("yyyy-MM-dd")) + QLatin1Char('/') + to.toString(QStringLiteral("yyyy-MM-dd"));
}

qint64 MdiDating::DateValue::gap(const DateValue &o) const
{
    if (!isValid() || !o.isValid() || overlaps(o))
        return 0;
    return to < o.from ? to.daysTo(o.from) : o.to.daysTo(from);
}

MdiDating::DateValue MdiDating::DateValue::day(int y, int m, int d)
{
    DateValue v;
    const QDate date(y, m, d);
    if (!date.isValid())
        return v;
    v.from = v.to = date;
    v.precision = QStringLiteral("day");
    return v;
}

MdiDating::DateValue MdiDating::DateValue::month(int y, int m)
{
    DateValue v;
    const QDate first(y, m, 1);
    if (!first.isValid())
        return v;
    v.from = first;
    v.to = first.addMonths(1).addDays(-1);
    v.precision = QStringLiteral("month");
    return v;
}

MdiDating::DateValue MdiDating::DateValue::year(int y)
{
    DateValue v;
    v.from = QDate(y, 1, 1);
    v.to = QDate(y, 12, 31);
    v.precision = QStringLiteral("year");
    return v;
}

MdiDating::DateValue MdiDating::DateValue::range(const QDate &from, const QDate &to)
{
    DateValue v;
    if (!from.isValid() || !to.isValid())
        return v;
    v.from = std::min(from, to);
    v.to = std::max(from, to);
    if (v.from == v.to)
        v.precision = QStringLiteral("day");
    else if (v.from.day() == 1 && v.to == v.from.addMonths(1).addDays(-1))
        v.precision = QStringLiteral("month");
    else if (v.from.month() == 1 && v.from.day() == 1 && v.to == QDate(v.from.year(), 12, 31))
        v.precision = QStringLiteral("year");
    else
        v.precision = QStringLiteral("range");
    return v;
}

MdiDating::DateValue MdiDating::DateValue::fromText(const QString &text)
{
    const QString t = text.trimmed();
    static const QRegularExpression dayRe(QStringLiteral("^(\\d{4})-(\\d{2})-(\\d{2})$"));
    static const QRegularExpression monthRe(QStringLiteral("^(\\d{4})-(\\d{2})$"));
    static const QRegularExpression yearRe(QStringLiteral("^(\\d{4})$"));
    static const QRegularExpression yearsRe(QStringLiteral("^(\\d{4})/(\\d{4})$"));
    static const QRegularExpression daysRe(QStringLiteral("^(\\d{4}-\\d{2}-\\d{2})/(\\d{4}-\\d{2}-\\d{2})$"));
    QRegularExpressionMatch m;
    if ((m = dayRe.match(t)).hasMatch())
        return day(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
    if ((m = monthRe.match(t)).hasMatch())
        return month(m.captured(1).toInt(), m.captured(2).toInt());
    if ((m = yearRe.match(t)).hasMatch())
        return year(m.captured(1).toInt());
    if ((m = yearsRe.match(t)).hasMatch())
        return range(QDate(m.captured(1).toInt(), 1, 1), QDate(m.captured(2).toInt(), 12, 31));
    if ((m = daysRe.match(t)).hasMatch())
        return range(QDate::fromString(m.captured(1), Qt::ISODate), QDate::fromString(m.captured(2), Qt::ISODate));
    return {};
}

//======================================================================
// Evidence
//======================================================================

QJsonObject MdiDating::Evidence::toJson() const
{
    QJsonObject o{{"field", field}, {"value", value}, {"kind", kind},
                  {"confidence", qRound(confidence * 100) / 100.0}};
    if (!precision.isEmpty()) o["precision"] = precision;
    if (!label.isEmpty())     o["label"] = label;
    if (!detail.isEmpty())    o["detail"] = detail;
    if (!url.isEmpty())       o["url"] = url;
    return o;
}

MdiDating::Evidence MdiDating::Evidence::fromJson(const QJsonObject &o)
{
    Evidence e;
    e.field      = o.value("field").toString();
    e.value      = o.value("value").toString();
    e.precision  = o.value("precision").toString();
    e.confidence = o.value("confidence").toDouble();
    e.kind       = o.value("kind").toString();
    e.label      = o.value("label").toString();
    e.detail     = o.value("detail").toString();
    e.url        = o.value("url").toString();
    return e;
}

namespace {

MdiDating::Evidence dateEvidence(const MdiDating::DateValue &d, double confidence, const QString &kind,
                                 const QString &label, const QString &detail, const QString &url = QString())
{
    MdiDating::Evidence e;
    e.field = QStringLiteral("date");
    e.value = d.text();
    e.precision = d.precision;
    e.confidence = clamp01(confidence);
    e.kind = kind;
    e.label = label;
    e.detail = detail;
    e.url = url;
    return e;
}

MdiDating::Evidence textEvidence(const QString &field, const QString &value, double confidence,
                                 const QString &kind, const QString &label, const QString &detail,
                                 const QString &precision = QString(), const QString &url = QString())
{
    MdiDating::Evidence e;
    e.field = field;
    e.value = value.simplified();
    e.precision = precision;
    e.confidence = clamp01(confidence);
    e.kind = kind;
    e.label = label;
    e.detail = detail;
    e.url = url;
    return e;
}

double precisionConfidence(const QString &precision, double day, double month, double year, double range)
{
    if (precision == QLatin1String("day"))   return day;
    if (precision == QLatin1String("month")) return month;
    if (precision == QLatin1String("year"))  return year;
    return range;
}

} // namespace

//======================================================================
// Dates in text
//======================================================================

QList<MdiDating::FoundDate> MdiDating::findDates(const QString &text, int minYear, int maxYear)
{
    if (maxYear <= 0)
        maxYear = currentYear() + 1;
    QVector<int> map;
    const QString t = foldMapped(text, &map);
    QVector<bool> used(t.size(), false);
    QList<FoundDate> found;

    auto yearOk = [&](int y) { return y >= minYear && y <= maxYear; };
    auto free = [&](int from, int len) {
        for (int i = from; i < from + len && i < used.size(); ++i)
            if (used[i])
                return false;
        return true;
    };
    auto add = [&](const DateValue &d, int from, int len) {
        if (!d.isValid() || !yearOk(d.from.year()) || !yearOk(d.to.year()) || !free(from, len))
            return;
        for (int i = from; i < from + len && i < used.size(); ++i)
            used[i] = true;
        FoundDate f;
        f.date = d;
        const int start = map.value(from, 0);
        const int end = len > 0 ? map.value(from + len - 1, start) + 1 : start;
        f.position = start;
        f.length = end - start;
        found << f;
    };
    // The patterns, compiled once, most precise first
    using Maker = std::function<DateValue(const QRegularExpressionMatch &)>;
    static const QList<QPair<QRegularExpression, Maker>> patterns = [] {
        QList<QPair<QRegularExpression, Maker>> list;
        auto scan = [&list](const QString &pattern, const Maker &make) {
            QRegularExpression re(pattern, QRegularExpression::UseUnicodePropertiesOption);
            re.optimize();
            list.append(qMakePair(re, make));
        };
        const QString M = monthPattern();
        const QString Y = QStringLiteral("((?:1[89]|20)\\d{2})");
        // 12-15 giugno 2015 / 12 to 15 June 2015
        scan(QStringLiteral("(?<!\\d)(\\d{1,2})\\s*(?:-|–|/|all'|all’|al|to|until|e|and|&)\\s*(\\d{1,2})\\s+(?:di\\s+|de\\s+|of\\s+)?(") + M
                 + QStringLiteral(")\\s*,?\\s*") + Y + QStringLiteral("(?!\\d)"),
             [](const QRegularExpressionMatch &m) {
                 const int mo = monthOf(m.captured(3)), y = m.captured(4).toInt();
                 return DateValue::range(QDate(y, mo, m.captured(1).toInt()), QDate(y, mo, m.captured(2).toInt()));
             });
        // 30 giugno - 2 luglio 2015
        scan(QStringLiteral("(?<!\\d)(\\d{1,2})\\s+(") + M + QStringLiteral(")\\s*(?:-|–|all'|all’|al|to|until)\\s*(\\d{1,2})\\s+(") + M
                 + QStringLiteral(")\\s*,?\\s*") + Y + QStringLiteral("(?!\\d)"),
             [](const QRegularExpressionMatch &m) {
                 const int y = m.captured(5).toInt();
                 return DateValue::range(QDate(y, monthOf(m.captured(2)), m.captured(1).toInt()),
                                         QDate(y, monthOf(m.captured(4)), m.captured(3).toInt()));
             });
        // 12 luglio 2019, 12th of July 2019
        scan(QStringLiteral("(?<!\\d)(\\d{1,2})(?:st|nd|rd|th|°|º|o)?\\s*(?:di\\s+|de\\s+|of\\s+)?(") + M
                 + QStringLiteral(")\\s*,?\\s*(?:del\\s+|de\\s+)?") + Y + QStringLiteral("(?!\\d)"),
             [](const QRegularExpressionMatch &m) {
                 return DateValue::day(m.captured(3).toInt(), monthOf(m.captured(2)), m.captured(1).toInt());
             });
        // July 12, 2019
        scan(QStringLiteral("\\b(") + M + QStringLiteral(")\\s+(\\d{1,2})(?:st|nd|rd|th)?\\s*,?\\s+") + Y
                 + QStringLiteral("(?!\\d)"),
             [](const QRegularExpressionMatch &m) {
                 return DateValue::day(m.captured(3).toInt(), monthOf(m.captured(1)), m.captured(2).toInt());
             });
        // 2019-07-12, 2019_07_12, 2019.07.12
        scan(QStringLiteral("(?<!\\d)") + Y + QStringLiteral("[-_./](\\d{1,2})[-_./](\\d{1,2})(?!\\d)"),
             [](const QRegularExpressionMatch &m) {
                 return DateValue::day(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
             });
        // 20190712
        scan(QStringLiteral("(?<!\\d)((?:19|20)\\d{2})(\\d{2})(\\d{2})(?!\\d)"),
             [](const QRegularExpressionMatch &m) {
                 return DateValue::day(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
             });
        // 12-07-2019, 12.07.2019, 12/07/2019: day first (Italian order) unless impossible
        scan(QStringLiteral("(?<!\\d)(\\d{1,2})[-_./](\\d{1,2})[-_./]") + Y + QStringLiteral("(?!\\d)"),
             [](const QRegularExpressionMatch &m) {
                 int d = m.captured(1).toInt(), mo = m.captured(2).toInt();
                 if (mo > 12 && d <= 12)
                     std::swap(d, mo);
                 return DateValue::day(m.captured(3).toInt(), mo, d);
             });
        // 2013-2014, 2013/14 (a season)
        scan(QStringLiteral("(?<!\\d)") + Y + QStringLiteral("\\s*(?:-|–|/|_)\\s*(\\d{4}|\\d{2})(?!\\d)"),
             [](const QRegularExpressionMatch &m) {
                 const int y1 = m.captured(1).toInt();
                 const QString second = m.captured(2);
                 int y2 = second.toInt();
                 if (second.size() == 2) {
                     if (y2 != (y1 % 100) + 1)
                         return DateValue();
                     y2 = y1 - (y1 % 100) + y2;
                 }
                 if (y2 <= y1 || y2 - y1 > 10)
                     return DateValue();
                 return DateValue::range(QDate(y1, 1, 1), QDate(y2, 12, 31));
             });
        // luglio 2019, July of 2019; 2019 luglio
        scan(QStringLiteral("\\b(") + M + QStringLiteral(")\\s*(?:del\\s+|of\\s+|de\\s+)?'?") + Y + QStringLiteral("(?!\\d)"),
             [](const QRegularExpressionMatch &m) {
                 return DateValue::month(m.captured(2).toInt(), monthOf(m.captured(1)));
             });
        scan(QStringLiteral("(?<!\\d)") + Y + QStringLiteral("[\\s_-]+(") + M + QStringLiteral(")\\b"),
             [](const QRegularExpressionMatch &m) {
                 return DateValue::month(m.captured(1).toInt(), monthOf(m.captured(2)));
             });
        // 2019-07, 07-2019
        scan(QStringLiteral("(?<!\\d)") + Y + QStringLiteral("[-_.](\\d{1,2})(?![\\d])"),
             [](const QRegularExpressionMatch &m) {
                 const int mo = m.captured(2).toInt();
                 return mo >= 1 && mo <= 12 ? DateValue::month(m.captured(1).toInt(), mo) : DateValue();
             });
        scan(QStringLiteral("(?<![\\d.])(\\d{1,2})[-_.]") + Y + QStringLiteral("(?!\\d)"),
             [](const QRegularExpressionMatch &m) {
                 const int mo = m.captured(1).toInt();
                 return mo >= 1 && mo <= 12 ? DateValue::month(m.captured(2).toInt(), mo) : DateValue();
             });
        // 2019
        // (not a picture size: 1920x1080)
        scan(QStringLiteral("(?<![\\d×]|\\dx)") + Y + QStringLiteral("(?![\\d]|\\s?[x×]\\s?\\d)"),
             [](const QRegularExpressionMatch &m) { return DateValue::year(m.captured(1).toInt()); });
        return list;
    }();
    for (const auto &p : patterns) {
        QRegularExpressionMatchIterator it = p.first.globalMatch(t);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            add(p.second(m), m.capturedStart(0), m.capturedLength(0));
        }
    }

    std::sort(found.begin(), found.end(), [](const FoundDate &a, const FoundDate &b) {
        return a.position < b.position;
    });
    return found;
}

//======================================================================
// File names
//======================================================================

QList<MdiDating::Evidence> MdiDating::readFileName(const QString &fileName)
{
    QList<Evidence> list;
    const QString base = QFileInfo(fileName).completeBaseName();
    const QString folded = fold(base);
    const int maxYear = currentYear() + 1;

    // WhatsApp: the day the file was sent or received
    static const QRegularExpression whatsapp(QStringLiteral("^(?:img|vid|aud|ptt|doc)-((?:19|20)\\d{2})(\\d{2})(\\d{2})-wa\\d+"));
    QRegularExpressionMatch m = whatsapp.match(folded);
    if (m.hasMatch()) {
        const DateValue d = DateValue::day(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
        if (d.isValid() && d.from.year() <= maxYear)
            list << dateEvidence(d, 0.70, QStringLiteral("filename"), base, QStringLiteral("WhatsApp"));
        return list;
    }

    // A camera's counter (DSC_2015, IMG_2014, P1012015) is no year
    static const QRegularExpression counter(QStringLiteral(
        "^(?:dsc[nf]?|img|imgp|_mg|mvi|mov|vid|p\\d{0,3}|gopr|gp|gh\\d{2}|dji|cimg|sam|pict|dcp|hpim|pano|sdc|dc|dcim|photo|foto)"
        "[ _-]?\\d{3,5}(?:[ _-]\\d+)?(?: ?\\(\\d+\\))?$"));
    const bool isCounter = counter.match(folded).hasMatch();

    static const QRegularExpression withTime(QStringLiteral(
        "(?<!\\d)((?:19|20)\\d{2})[-_.]?(\\d{2})[-_.]?(\\d{2})[ _t-]*(?:at[ _]?)?([01]\\d|2[0-3])[-_.:h]?([0-5]\\d)[-_.:m]?([0-5]\\d)(?!\\d)"));
    const bool hasTime = withTime.match(folded).hasMatch();

    for (const FoundDate &f : findDates(base, 1950, maxYear)) {
        const DateValue &d = f.date;
        if (isCounter && d.precision == QLatin1String("year"))
            continue;
        double c = precisionConfidence(d.precision, hasTime ? 0.85 : 0.75, 0.60, 0.50, 0.45);
        // A year glued to letters ("ECH2015") is weaker
        if (d.precision == QLatin1String("year")) {
            const QChar before = f.position > 0 ? base.at(f.position - 1) : QChar();
            const QChar after = f.position + f.length < base.size() ? base.at(f.position + f.length) : QChar();
            if (before.isLetter() || after.isLetter())
                c = 0.40;
        }
        list << dateEvidence(d, c, QStringLiteral("filename"), base, base.mid(f.position, f.length));
        break;   // the first date of a name is its date
    }
    return list;
}

//======================================================================
// Folder names
//======================================================================

QString MdiDating::knownPlace(const QString &words, const QJsonObject &context)
{
    const QString k = key(words);
    if (k.isEmpty())
        return QString();
    for (const ContextEntity &e : contextEntities(context))
        if (e.kind == QLatin1String("place") && e.keys.contains(k))
            return e.name;
    const auto it = placeIndex().constFind(k);
    return it == placeIndex().constEnd() ? QString() : it->display;
}

QString MdiDating::nearestPlace(double latitude, double longitude, double *distance)
{
    QString best;
    double bestKm = 40.0;
    QSet<QString> seen;
    for (const PlaceEntry &e : placeIndex()) {
        if (e.country || qIsNaN(e.lat) || seen.contains(e.display))
            continue;
        seen.insert(e.display);
        const double km = distanceKm(latitude, longitude, e.lat, e.lon);
        if (km < bestKm) {
            bestKm = km;
            best = e.display;
        }
    }
    if (distance)
        *distance = best.isEmpty() ? -1 : bestKm;
    return best;
}

MdiDating::FolderReading MdiDating::readFolderName(const QString &name, const QJsonObject &context)
{
    FolderReading r;
    r.name = name;
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        r.generic = true;
        return r;
    }
    // Camera folders: 100CANON, 101MSDCF, 100APPLE, 100_PANA
    static const QRegularExpression cameraFolder(QStringLiteral("^\\d{3}[A-Za-z_]{4,5}$"));
    if (cameraFolder.match(trimmed).hasMatch()) {
        r.generic = true;
        return r;
    }

    const int maxYear = currentYear() + 1;
    const QList<FoundDate> dates = findDates(trimmed, 1950, maxYear);
    // The most precise date of the name; spans covered by dates are not words
    QVector<bool> isDate(trimmed.size(), false);
    for (const FoundDate &f : dates) {
        for (int i = f.position; i < f.position + f.length && i < isDate.size(); ++i)
            isDate[i] = true;
        if (!r.date.isValid() || f.date.span() < r.date.span())
            r.date = f.date;
    }

    // Words with their original form
    struct Word { QString text; QString folded; bool date = false; };
    QList<Word> words;
    {
        int start = -1;
        for (int i = 0; i <= trimmed.size(); ++i) {
            const bool letter = i < trimmed.size() && (trimmed.at(i).isLetterOrNumber() || trimmed.at(i) == QLatin1Char('\''));
            if (letter && start < 0)
                start = i;
            if (!letter && start >= 0) {
                Word w;
                w.text = trimmed.mid(start, i - start);
                w.folded = wordsOf(fold(w.text));
                for (int k = start; k < i; ++k)
                    w.date = w.date || isDate[k];
                if (!w.folded.isEmpty())
                    words << w;
                start = -1;
            }
        }
    }

    const QHash<QString, QString> glossary = glossaryForms(context);
    const QList<ContextEntity> entities = contextEntities(context);
    const QString allWords = key(trimmed);

    // Known events of the context, by name or alias
    for (const ContextEntity &e : entities) {
        if (e.kind != QLatin1String("event"))
            continue;
        for (const QString &k : e.keys) {
            if (k.size() >= 3 && containsWords(allWords, k)) {
                r.event = e.name;
                r.eventConfidence = 0.85;
                r.specificEvent = true;
                break;
            }
        }
        if (!r.event.isEmpty())
            break;
    }

    // Places: longest word sequences first (up to three words)
    QVector<int> placeAt(words.size(), -1);   // index into r.places
    QList<bool> placeWeak;
    for (int n = 3; n >= 1; --n) {
        for (int i = 0; i + n <= words.size(); ++i) {
            bool taken = false;
            QStringList parts;
            for (int k = i; k < i + n; ++k) {
                taken = taken || placeAt[k] >= 0 || words[k].date;
                parts << words[k].folded;
            }
            if (taken)
                continue;
            const QString phrase = parts.join(QLatin1Char(' '));
            QString display;
            bool weak = false;
            for (const ContextEntity &e : entities)
                if (e.kind == QLatin1String("place") && e.keys.contains(phrase))
                    display = e.name;
            if (display.isEmpty()) {
                const auto it = placeIndex().constFind(phrase);
                if (it != placeIndex().constEnd()) {
                    // The owner's own spelling of the place ("Tokio", "Munich")
                    QStringList shown;
                    for (int k = i; k < i + n; ++k)
                        shown << displayWord(words[k].text);
                    display = shown.join(QLatin1Char(' '));
                    weak = it->weak;
                }
            }
            if (display.isEmpty())
                continue;
            const int index = r.places.indexOf(display);
            if (index < 0) {
                r.places << display;
                placeWeak << weak;
            }
            for (int k = i; k < i + n; ++k)
                placeAt[k] = index < 0 ? r.places.size() - 1 : index;
        }
    }

    // Holidays and seasons
    QString holiday;
    for (const Holiday &h : kHolidays)
        for (const QString &n : QString::fromLatin1(h.names).split(QLatin1Char('|')))
            if (holiday.isEmpty() && containsWords(allWords, n))
                holiday = QString::fromLatin1(h.names).section(QLatin1Char('|'), 0, 0);
    int seasonFrom = 0, seasonTo = 0;
    for (const Season &s : kSeasons)
        for (const QString &n : QString::fromLatin1(s.names).split(QLatin1Char('|')))
            if (seasonFrom == 0 && containsWords(allWords, n)) {
                seasonFrom = s.fromMonth;
                seasonTo = s.toMonth;
            }

    // Classify the remaining words
    bool generic = true;
    QStringList phrase;      // the event phrase, original words
    int monthOnly = 0;
    for (int i = 0; i < words.size(); ++i) {
        const Word &w = words.at(i);
        const QString &f = w.folded;
        if (w.date) {
            generic = false;
            // A standalone year stays in an event name ("Winter cup 2015")
            if (r.date.precision == QLatin1String("year") && f.size() == 4)
                phrase << w.text;
            continue;
        }
        if (placeAt[i] >= 0) {
            generic = false;
            phrase << displayWord(w.text);
            continue;
        }
        if (genericWords().contains(f))
            continue;
        if (stopWords().contains(f)) {
            phrase << w.text.toLower();
            continue;
        }
        generic = false;
        if (isLongMonthName(f)) {
            monthOnly = monthNames().value(f);
            continue;
        }
        if (glossary.contains(f)) {
            phrase << glossary.value(f);
            r.eventWords << f;
            r.specificEvent = true;
            continue;
        }
        if (specificEventWords().contains(f)) {
            r.eventWords << f;
            r.specificEvent = true;
            phrase << displayWord(w.text);
            continue;
        }
        if (genericEventWords().contains(f)) {
            r.eventWords << f;
            phrase << displayWord(w.text);
            continue;
        }
        phrase << displayWord(w.text);
        if (eventDescriptorWords().contains(f))
            continue;
        bool digits = true;
        for (const QChar c : f)
            digits = digits && c.isDigit();
        if (!digits && f.size() >= 3 && w.text.at(0).isUpper())
            r.unknownWords << w.text;
    }
    r.generic = generic;

    // Month without a year ("Luglio"): kept for the parent folder's year
    if (!r.date.isValid() && monthOnly > 0)
        r.date.precision = QStringLiteral("month-only:") + QString::number(monthOnly);
    if (!holiday.isEmpty())
        r.date.precision = r.date.isValid() ? r.date.precision : QStringLiteral("holiday:") + holiday;
    if (!holiday.isEmpty() && r.date.isValid() && r.date.precision == QLatin1String("year")) {
        const DateValue h = holidayDate(holiday, r.date.from.year());
        if (h.isValid())
            r.date = h;
    }
    if (seasonFrom > 0 && r.eventWords.isEmpty() && r.date.precision == QLatin1String("year")) {
        const int y = r.date.from.year();
        r.date = DateValue::range(QDate(y, seasonFrom, 1), QDate(y, seasonTo, 1).addMonths(1).addDays(-1));
    }

    // Trim stop words at the ends of the phrase
    while (!phrase.isEmpty() && stopWords().contains(fold(phrase.first())))
        phrase.removeFirst();
    while (!phrase.isEmpty() && stopWords().contains(fold(phrase.last())))
        phrase.removeLast();
    const QString phraseText = phrase.join(QLatin1Char(' ')).simplified();

    if (r.event.isEmpty() && !phraseText.isEmpty()) {
        const bool hasYear = r.date.isValid();
        if (r.specificEvent) {
            r.event = phraseText;
            r.eventConfidence = 0.70;
        } else if (!r.eventWords.isEmpty()) {
            r.event = phraseText;
            r.eventConfidence = (hasYear || !r.places.isEmpty()) ? 0.55 : 0.50;
        } else if (!r.places.isEmpty() && hasYear && phrase.size() > 1) {
            // "Munich 2015", "TOKIO 2020": likely an event held there
            r.event = phraseText;
            r.eventConfidence = 0.45;
        }
    }
    // A weak place alone ("Nice", "Sofia") is kept, but only as a hint
    for (int i = r.places.size() - 1; i >= 0; --i)
        if (placeWeak.value(i) && r.eventWords.isEmpty() && !r.date.isValid())
            r.places.removeAt(i);
    return r;
}

namespace {

// "month-only:7" / "holiday:natale" kept in an invalid DateValue's precision
int pendingMonth(const MdiDating::DateValue &d)
{
    return d.precision.startsWith(QLatin1String("month-only:")) ? d.precision.mid(11).toInt() : 0;
}

QString pendingHoliday(const MdiDating::DateValue &d)
{
    return d.precision.startsWith(QLatin1String("holiday:")) ? d.precision.mid(8) : QString();
}

QString placeEvidenceKind(const QString &place, const QJsonObject &context)
{
    for (const ContextEntity &e : contextEntities(context))
        if (e.kind == QLatin1String("place") && e.name == place)
            return QStringLiteral("context");
    return QStringLiteral("folder");
}

} // namespace

QList<MdiDating::Evidence> MdiDating::readFolders(const QString &relativeFolder, const QJsonObject &context,
                                                  QHash<QString, FolderReading> *cache)
{
    QList<Evidence> list;
    QStringList parts = relativeFolder.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    std::reverse(parts.begin(), parts.end());   // nearest first

    // Readings first: the nearest year, at or above a folder, dates its event
    QList<FolderReading> readings;
    for (const QString &name : std::as_const(parts)) {
        if (cache && cache->contains(name)) {
            readings << cache->value(name);
        } else {
            readings << readFolderName(name, context);
            if (cache)
                cache->insert(name, readings.last());
        }
    }
    auto yearAtOrAbove = [&readings](int level) {
        for (int k = level; k < readings.size(); ++k)
            if (readings.at(k).date.isValid() && !readings.at(k).generic)
                return readings.at(k).date.from.year();
        return 0;
    };

    bool haveDate = false, haveEvent = false, havePlace = false;
    int pendingMonthValue = 0;
    QString pendingMonthFolder, pendingHolidayKey, pendingHolidayFolder;
    for (int level = 0; level < parts.size(); ++level) {
        const QString &name = parts.at(level);
        const FolderReading &reading = readings.at(level);
        if (reading.generic)
            continue;
        const double decay = std::max(0.6, 1.0 - 0.08 * level);

        if (!haveDate) {
            if (reading.date.isValid()) {
                DateValue d = reading.date;
                QString label = name;
                // "Luglio" inside "2015": July 2015
                if (pendingMonthValue > 0 && d.precision == QLatin1String("year")) {
                    d = DateValue::month(d.from.year(), pendingMonthValue);
                    label = name + QLatin1Char('/') + pendingMonthFolder;
                } else if (!pendingHolidayKey.isEmpty() && d.precision == QLatin1String("year")) {
                    const DateValue h = holidayDate(pendingHolidayKey, d.from.year());
                    if (h.isValid()) {
                        d = h;
                        label = name + QLatin1Char('/') + pendingHolidayFolder;
                    }
                }
                const double c = precisionConfidence(d.precision, 0.70, 0.65, 0.60, 0.55) * decay;
                list << dateEvidence(d, c, QStringLiteral("folder"), label, d.text());
                haveDate = true;
            } else if (pendingMonth(reading.date) > 0 && pendingMonthValue == 0) {
                pendingMonthValue = pendingMonth(reading.date);
                pendingMonthFolder = name;
            } else if (!pendingHoliday(reading.date).isEmpty() && pendingHolidayKey.isEmpty()) {
                pendingHolidayKey = pendingHoliday(reading.date);
                pendingHolidayFolder = name;
            }
        }
        if (!haveEvent && !reading.event.isEmpty()) {
            list << textEvidence(QStringLiteral("event"), reading.event, reading.eventConfidence * decay,
                                 reading.fromModel ? QStringLiteral("model")
                                 : reading.eventConfidence >= 0.85 ? QStringLiteral("context")
                                                                    : QStringLiteral("folder"),
                                 name, reading.event);
            haveEvent = true;
            // What the context knows of this event: its dates and place
            list << readContext(reading.event, yearAtOrAbove(level), context);
        }
        if (!havePlace && !reading.places.isEmpty()) {
            for (const QString &place : reading.places) {
                const QString kind = reading.fromModel ? QStringLiteral("model") : placeEvidenceKind(place, context);
                const double c = kind == QLatin1String("context") ? 0.80
                               : kind == QLatin1String("model")   ? 0.45 : 0.65;
                list << textEvidence(QStringLiteral("place"), place, c * decay, kind, name, place,
                                     QStringLiteral("name"));
            }
            havePlace = true;
        }
    }
    return list;
}

QList<MdiDating::Evidence> MdiDating::readContext(const QString &eventOrName, int folderYear, const QJsonObject &context)
{
    QList<Evidence> list;
    const QString words = key(eventOrName);
    if (words.isEmpty())
        return list;
    for (const ContextEntity &e : contextEntities(context)) {
        if (e.kind != QLatin1String("event"))
            continue;
        bool match = false;
        for (const QString &k : e.keys)
            match = match || (k.size() >= 3 && (containsWords(words, k) || containsWords(k, words)));
        if (!match)
            continue;
        // Its dates, as the owner wrote them in the notes
        for (const FoundDate &f : findDates(e.notes + QLatin1Char(' ') + e.name, 1800)) {
            const bool sameYear = folderYear == 0 || (f.date.from.year() <= folderYear && folderYear <= f.date.to.year());
            const double c = sameYear ? precisionConfidence(f.date.precision, 0.80, 0.75, 0.65, 0.78) : 0.45;
            list << dateEvidence(f.date, c, QStringLiteral("context"), e.name, e.notes.simplified().left(200));
        }
        // Its place
        const QString notesWords = key(e.notes);
        for (const ContextEntity &p : contextEntities(context))
            if (p.kind == QLatin1String("place"))
                for (const QString &k : p.keys)
                    if (containsWords(notesWords, k)) {
                        list << textEvidence(QStringLiteral("place"), p.name, 0.70, QStringLiteral("context"),
                                             e.name, e.notes.simplified().left(200), QStringLiteral("name"));
                        break;
                    }
        const QStringList noteWords = notesWords.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        QList<const PlaceEntry *> cities, countries;
        for (int n = 3; n >= 1; --n)
            for (int i = 0; i + n <= noteWords.size(); ++i) {
                const auto it = placeIndex().constFind(noteWords.mid(i, n).join(QLatin1Char(' ')));
                if (it != placeIndex().constEnd() && !it->weak)
                    (it->country ? countries : cities) << &it.value();
            }
        // A city says more than its country ("Osijek (Croazia)")
        for (const PlaceEntry *p : cities.isEmpty() ? countries : cities)
            list << textEvidence(QStringLiteral("place"), p->display, 0.65, QStringLiteral("context"),
                                 e.name, e.notes.simplified().left(200), QStringLiteral("name"));
        break;
    }
    return list;
}

QList<MdiDating::Evidence> MdiDating::readDerived(const QString &analysis, const QJsonObject &value,
                                                  const QJsonObject &context)
{
    QList<Evidence> list;
    QString text;
    QStringList named;   // entity names and category values
    QStringList eventValues, placeValues, dateValues;
    if (analysis == QLatin1String("caption")) {
        text = value.value("caption").toString();
        for (const QJsonValue &t : value.value("tags").toArray())
            named << t.toString();
    } else if (analysis == QLatin1String("classification")) {
        text = value.value("title").toString() + QStringLiteral(". ") + value.value("description").toString();
        for (const QJsonValue &t : value.value("entities").toArray())
            named << t.toString();
        const QJsonObject categories = value.value("categories").toObject();
        for (auto it = categories.begin(); it != categories.end(); ++it) {
            const QString cat = fold(it.key());
            QStringList values;
            for (const QJsonValue &v : it.value().toArray())
                values << v.toString();
            if (it.value().isString())
                values << it.value().toString();
            if (cat.contains(QLatin1String("event")) || cat.contains(QLatin1String("manifestazion"))
                || cat.contains(QLatin1String("gara")) || cat.contains(QLatin1String("competiz"))
                || cat.contains(QLatin1String("competition")))
                eventValues << values;
            else if (cat.contains(QLatin1String("luogo")) || cat.contains(QLatin1String("place"))
                     || cat.contains(QLatin1String("location")) || cat.contains(QLatin1String("citta"))
                     || cat.contains(QLatin1String("city")) || cat.contains(QLatin1String("sede"))
                     || cat.contains(QLatin1String("venue")) || cat.contains(QLatin1String("localita")))
                placeValues << values;
            else if (cat.contains(QLatin1String("anno")) || cat.contains(QLatin1String("year"))
                     || cat.contains(QLatin1String("data")) || cat.contains(QLatin1String("date"))
                     || cat.contains(QLatin1String("periodo")) || cat.contains(QLatin1String("period")))
                dateValues << values;
            else
                named << values;
        }
    } else {
        return list;
    }

    const double base = analysis == QLatin1String("classification") ? 0.55 : 0.45;
    for (const QString &v : std::as_const(eventValues))
        if (!v.trimmed().isEmpty())
            list << textEvidence(QStringLiteral("event"), v, base, analysis, v, text.left(200));
    for (const QString &v : std::as_const(placeValues))
        if (!v.trimmed().isEmpty())
            list << textEvidence(QStringLiteral("place"), v, base, analysis, v, text.left(200), QStringLiteral("name"));
    for (const QString &v : std::as_const(dateValues))
        for (const FoundDate &f : findDates(v, 1950))
            list << dateEvidence(f.date, base - 0.05, analysis, v, text.left(200));

    // Known entities named in the text or among the names
    const QString allWords = key(text + QLatin1Char(' ') + named.join(QLatin1Char(' ')));
    for (const ContextEntity &e : contextEntities(context)) {
        if (e.kind != QLatin1String("event") && e.kind != QLatin1String("place"))
            continue;
        for (const QString &k : e.keys)
            if (k.size() >= 3 && containsWords(allWords, k)) {
                list << textEvidence(e.kind, e.name, base, analysis, e.name, text.left(200),
                                     e.kind == QLatin1String("place") ? QStringLiteral("name") : QString());
                break;
            }
    }
    // Years written in the picture or the description (banners, captions)
    for (const FoundDate &f : findDates(text, 1950))
        list << dateEvidence(f.date, analysis == QLatin1String("caption") ? 0.35 : 0.40, analysis,
                             text.mid(f.position, f.length), excerptAround(text, f.position, f.length, 80));
    return list;
}

//======================================================================
// Embedded metadata
//======================================================================

QDateTime MdiDating::parseMetadataDate(const QString &textIn)
{
    QString text = textIn.trimmed();
    if (text.isEmpty() || text.startsWith(QLatin1String("0000")))
        return {};
    QDateTime dt;
    // EXIF: "2019:07:12 10:22:33"
    static const QRegularExpression exif(QStringLiteral("^(\\d{4}):(\\d{2}):(\\d{2})[ T](\\d{2}):(\\d{2}):(\\d{2})"));
    const QRegularExpressionMatch m = exif.match(text);
    if (m.hasMatch()) {
        dt = QDateTime(QDate(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt()),
                       QTime(m.captured(4).toInt(), m.captured(5).toInt(), m.captured(6).toInt()));
    } else {
        // ISO 8601, with or without zone; QuickTime "2019-07-12T10:22:33+0200"
        static const QRegularExpression zone(QStringLiteral("([+-]\\d{2})(\\d{2})$"));
        text.replace(zone, QStringLiteral("\\1:\\2"));
        dt = QDateTime::fromString(text, Qt::ISODateWithMs);
        if (!dt.isValid())
            dt = QDateTime::fromString(text, Qt::ISODate);
        if (!dt.isValid())
            dt = QDateTime::fromString(text.left(19), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    }
    if (!dt.isValid())
        return {};
    const QDate d = dt.date();
    // Camera and container defaults, and clocks never set
    if (d.year() < 1980 || d.year() == 1904
        || (d == QDate(2000, 1, 1) && dt.time().hour() == 0 && dt.time().minute() == 0)
        || d > QDate::currentDate().addDays(1))
        return {};
    return dt;
}

namespace {

double gpsRational(const QString &value, const QString &ref)
{
    // exiv2 -Pv: "45/1 33/1 1800/100"; also plain decimals
    const QStringList parts = value.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    double out = 0, scale = 1;
    bool any = false;
    for (const QString &p : parts) {
        double v;
        if (p.contains(QLatin1Char('/'))) {
            const double num = p.section(QLatin1Char('/'), 0, 0).toDouble();
            const double den = p.section(QLatin1Char('/'), 1, 1).toDouble();
            if (den == 0)
                continue;
            v = num / den;
        } else {
            bool ok = false;
            v = p.toDouble(&ok);
            if (!ok)
                continue;
        }
        out += v / scale;
        scale *= 60;
        any = true;
    }
    if (!any)
        return qQNaN();
    if (ref.startsWith(QLatin1Char('S'), Qt::CaseInsensitive) || ref.startsWith(QLatin1Char('W'), Qt::CaseInsensitive))
        out = -out;
    return out;
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
    *out = buffer;
    // exiv2 exits 253 when some keys are absent: the output is still good
    if (p.exitStatus() != QProcess::NormalExit) {
        *error = QStringLiteral("%1 crashed").arg(QFileInfo(program).fileName());
        return false;
    }
    return true;
}

// ISO 6709 "+45.5550+018.6955+090.000/"
bool parseIso6709(const QString &text, double *lat, double *lon)
{
    static const QRegularExpression re(QStringLiteral("^([+-]\\d+(?:\\.\\d+)?)([+-]\\d+(?:\\.\\d+)?)"));
    const QRegularExpressionMatch m = re.match(text.trimmed());
    if (!m.hasMatch())
        return false;
    *lat = m.captured(1).toDouble();
    *lon = m.captured(2).toDouble();
    return true;
}

} // namespace

QJsonObject MdiDating::readEmbedded(const QString &path, const QString &fileType, const Tools &tools, QString *error)
{
    QJsonObject out;
    const bool video = fileType == QLatin1String("video");

    // 1. KFileMetaData (exiv2 and ffmpeg extractors), as the catalog's metadata
    if (tools.useKFileMetaData) {
        const QString mime = QMimeDatabase().mimeTypeForFile(path).name();
        KFileMetaData::ExtractorCollection collection;
        for (KFileMetaData::Extractor *extractor : collection.fetchExtractors(mime)) {
            KFileMetaData::SimpleExtractionResult result(path, mime, KFileMetaData::ExtractionResult::ExtractMetaData);
            extractor->extract(&result);
            const KFileMetaData::PropertyMultiMap props = result.properties();
            auto text = [&props](KFileMetaData::Property::Property p) {
                const QVariant v = props.value(p);
                if (!v.isValid())
                    return QString();
                if (v.typeId() == QMetaType::QDateTime)
                    return v.toDateTime().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
                return v.toString();
            };
            if (out.value("dateTimeOriginal").toString().isEmpty() && !text(KFileMetaData::Property::PhotoDateTimeOriginal).isEmpty())
                out["dateTimeOriginal"] = text(KFileMetaData::Property::PhotoDateTimeOriginal);
            if (out.value("modifyDate").toString().isEmpty() && !text(KFileMetaData::Property::ImageDateTime).isEmpty())
                out["modifyDate"] = text(KFileMetaData::Property::ImageDateTime);
            if (out.value("createDate").toString().isEmpty() && !text(KFileMetaData::Property::CreationDate).isEmpty())
                out["createDate"] = text(KFileMetaData::Property::CreationDate);
            if (out.value("make").toString().isEmpty() && !text(KFileMetaData::Property::Manufacturer).isEmpty())
                out["make"] = text(KFileMetaData::Property::Manufacturer).simplified();
            if (out.value("model").toString().isEmpty() && !text(KFileMetaData::Property::Model).isEmpty())
                out["model"] = text(KFileMetaData::Property::Model).simplified();
            const QVariant lat = props.value(KFileMetaData::Property::PhotoGpsLatitude);
            const QVariant lon = props.value(KFileMetaData::Property::PhotoGpsLongitude);
            if (lat.isValid() && lon.isValid() && !out.contains("latitude")
                && (lat.toDouble() != 0 || lon.toDouble() != 0)) {
                out["latitude"] = lat.toDouble();
                out["longitude"] = lon.toDouble();
            }
        }
        if (!out.isEmpty())
            out["source"] = QStringLiteral("kfilemetadata");
    }

    // 2. exiv2's program, for photos KFileMetaData gave no date for
    if (!video && out.value("dateTimeOriginal").toString().isEmpty() && !tools.exiv2.isEmpty()) {
        QByteArray bytes;
        QString err;
        const QStringList keys{QStringLiteral("Exif.Photo.DateTimeOriginal"), QStringLiteral("Exif.Photo.DateTimeDigitized"),
                               QStringLiteral("Exif.Image.DateTime"), QStringLiteral("Exif.Image.Make"),
                               QStringLiteral("Exif.Image.Model"), QStringLiteral("Exif.Image.Software"),
                               QStringLiteral("Exif.GPSInfo.GPSLatitude"), QStringLiteral("Exif.GPSInfo.GPSLatitudeRef"),
                               QStringLiteral("Exif.GPSInfo.GPSLongitude"), QStringLiteral("Exif.GPSInfo.GPSLongitudeRef"),
                               QStringLiteral("Xmp.photoshop.DateCreated"), QStringLiteral("Xmp.xmp.CreateDate"),
                               QStringLiteral("Iptc.Application2.DateCreated")};
        QStringList args{QStringLiteral("-q"), QStringLiteral("-Pkv")};
        for (const QString &k : keys)
            args << QStringLiteral("-K") << k;
        args << path;
        if (runProgram(tools.exiv2, args, tools.timeoutMs, tools.stop, &bytes, &err)) {
            QHash<QString, QString> values;
            for (const QString &line : QString::fromUtf8(bytes).split(QLatin1Char('\n'))) {
                const QString l = line.trimmed();
                const int space = l.indexOf(QLatin1Char(' '));
                if (space > 0)
                    values.insert(l.left(space), l.mid(space + 1).trimmed());
            }
            auto set = [&out](const char *k, const QString &v) {
                if (!v.isEmpty() && out.value(QLatin1String(k)).toString().isEmpty())
                    out[QLatin1String(k)] = v;
            };
            set("dateTimeOriginal", values.value("Exif.Photo.DateTimeOriginal"));
            set("createDate", values.value("Exif.Photo.DateTimeDigitized"));
            set("createDate", values.value("Xmp.xmp.CreateDate"));
            set("createDate", values.value("Xmp.photoshop.DateCreated"));
            set("createDate", values.value("Iptc.Application2.DateCreated"));
            set("modifyDate", values.value("Exif.Image.DateTime"));
            set("make", values.value("Exif.Image.Make"));
            set("model", values.value("Exif.Image.Model"));
            set("software", values.value("Exif.Image.Software"));
            if (!out.contains("latitude") && values.contains("Exif.GPSInfo.GPSLatitude")) {
                const double lat = gpsRational(values.value("Exif.GPSInfo.GPSLatitude"), values.value("Exif.GPSInfo.GPSLatitudeRef"));
                const double lon = gpsRational(values.value("Exif.GPSInfo.GPSLongitude"), values.value("Exif.GPSInfo.GPSLongitudeRef"));
                if (!qIsNaN(lat) && !qIsNaN(lon) && (lat != 0 || lon != 0)) {
                    out["latitude"] = lat;
                    out["longitude"] = lon;
                }
            }
            if (!values.isEmpty())
                out["source"] = out.contains("source") ? out.value("source").toString() + QStringLiteral("+exiv2")
                                                       : QStringLiteral("exiv2");
        } else if (error) {
            *error = err;
        }
    }

    // 3. ffprobe for videos: QuickTime's creation date (local time) first
    if (video && !tools.ffprobe.isEmpty()) {
        QByteArray bytes;
        QString err;
        if (runProgram(tools.ffprobe, {QStringLiteral("-v"), QStringLiteral("quiet"), QStringLiteral("-print_format"),
                                       QStringLiteral("json"), QStringLiteral("-show_entries"),
                                       QStringLiteral("format_tags:stream_tags=creation_time"), path},
                       tools.timeoutMs, tools.stop, &bytes, &err)) {
            const QJsonObject root = QJsonDocument::fromJson(bytes).object();
            const QJsonObject tags = root.value("format").toObject().value("tags").toObject();
            auto tag = [&tags](const QString &name) {
                for (auto it = tags.begin(); it != tags.end(); ++it)
                    if (it.key().compare(name, Qt::CaseInsensitive) == 0)
                        return it.value().toString();
                return QString();
            };
            const QString qtDate = tag(QStringLiteral("com.apple.quicktime.creationdate"));
            QString created = qtDate.isEmpty() ? tag(QStringLiteral("creation_time")) : qtDate;
            if (created.isEmpty())
                for (const QJsonValue &s : root.value("streams").toArray()) {
                    created = s.toObject().value("tags").toObject().value("creation_time").toString();
                    if (!created.isEmpty())
                        break;
                }
            if (!created.isEmpty() && out.value("createDate").toString().isEmpty())
                out["createDate"] = created;
            if (!qtDate.isEmpty())
                out["createDateLocal"] = true;
            const QString make = tag(QStringLiteral("com.apple.quicktime.make"));
            const QString model = tag(QStringLiteral("com.apple.quicktime.model"));
            if (!make.isEmpty() && out.value("make").toString().isEmpty())
                out["make"] = make;
            if (!model.isEmpty() && out.value("model").toString().isEmpty())
                out["model"] = model;
            QString location = tag(QStringLiteral("com.apple.quicktime.location.ISO6709"));
            if (location.isEmpty())
                location = tag(QStringLiteral("location"));
            double lat = 0, lon = 0;
            if (!out.contains("latitude") && parseIso6709(location, &lat, &lon) && (lat != 0 || lon != 0)) {
                out["latitude"] = lat;
                out["longitude"] = lon;
            }
            out["source"] = out.contains("source") ? out.value("source").toString() + QStringLiteral("+ffprobe")
                                                   : QStringLiteral("ffprobe");
        } else if (error) {
            *error = err;
        }
    }
    return out;
}

QList<MdiDating::Evidence> MdiDating::embeddedEvidence(const QJsonObject &e)
{
    QList<Evidence> list;
    const QString camera = (e.value("make").toString() + QLatin1Char(' ') + e.value("model").toString()).simplified();
    struct Field { const char *key; const char *label; double confidence; };
    const Field fields[] = {
        {"dateTimeOriginal", "DateTimeOriginal", 0.90},
        {"createDate", "CreateDate", 0.80},
        {"modifyDate", "DateTime", 0.60},      // often the date of an edit
    };
    for (const Field &f : fields) {
        const QDateTime dt = parseMetadataDate(e.value(QLatin1String(f.key)).toString());
        if (!dt.isValid())
            continue;
        const bool video = e.value("source").toString().contains(QLatin1String("ffprobe"));
        // A video's creation_time is UTC unless QuickTime's local date: the day may shift
        double c = f.confidence;
        if (video && QLatin1String(f.key) == QLatin1String("createDate"))
            c = e.value("createDateLocal").toBool() ? 0.85 : 0.75;
        QString detail = e.value(QLatin1String(f.key)).toString();
        if (!camera.isEmpty())
            detail += QStringLiteral(" · ") + camera;
        list << dateEvidence(DateValue::day(dt.date().year(), dt.date().month(), dt.date().day()), c,
                             video ? QStringLiteral("video") : QStringLiteral("exif"),
                             QString::fromLatin1(f.label), detail);
        break;   // the most reliable date only
    }
    if (e.contains("latitude") && e.contains("longitude")) {
        const double lat = e.value("latitude").toDouble(), lon = e.value("longitude").toDouble();
        const QString coords = QStringLiteral("%1, %2").arg(lat, 0, 'f', 5).arg(lon, 0, 'f', 5);
        double km = -1;
        const QString near = nearestPlace(lat, lon, &km);
        if (!near.isEmpty()) {
            list << textEvidence(QStringLiteral("place"), near, km <= 15 ? 0.75 : 0.60, QStringLiteral("exif"),
                                 QStringLiteral("GPS"), QStringLiteral("%1 (%2 km)").arg(coords).arg(qRound(km)),
                                 QStringLiteral("name"));
        } else {
            list << textEvidence(QStringLiteral("place"), coords, 0.60, QStringLiteral("exif"),
                                 QStringLiteral("GPS"), coords, QStringLiteral("coordinates"));
        }
    }
    return list;
}

QJsonObject MdiDating::analyseFile(const QString &fullPath, const QString &fileType, const Tools &tools,
                                   QString *outcome, QString *reason)
{
    *outcome = QStringLiteral("completed");
    QJsonObject value;
    value["version"] = kVersion;
    QString err;
    QJsonObject embedded;
    if (QFileInfo(fullPath).isReadable())
        embedded = readEmbedded(fullPath, fileType, tools, &err);
    else
        err = QStringLiteral("file not readable");
    value["embedded"] = embedded;
    if (!err.isEmpty())
        value["embeddedError"] = err;
    QJsonArray evidence;
    for (const Evidence &e : embeddedEvidence(embedded))
        evidence.append(e.toJson());
    for (const Evidence &e : readFileName(QFileInfo(fullPath).fileName()))
        evidence.append(e.toJson());
    value["evidence"] = evidence;
    if (err == QLatin1String("stopped")) {
        *outcome = QStringLiteral("failed");
        *reason = err;
    }
    return value;
}

//======================================================================
// Combination
//======================================================================

namespace {

struct Candidate {
    QString value, display, precision;
    MdiDating::DateValue date;
    double score = 0;
    QList<MdiDating::Evidence> support;
};

// Strongest evidence per kind: several folder levels are one source
QList<double> perKindMax(const QList<MdiDating::Evidence> &list)
{
    QHash<QString, double> best;
    for (const MdiDating::Evidence &e : list)
        best[e.kind] = std::max(best.value(e.kind), e.confidence);
    return best.values();
}

QList<MdiProposals::Source> toSources(QList<MdiDating::Evidence> support)
{
    std::stable_sort(support.begin(), support.end(), [](const MdiDating::Evidence &a, const MdiDating::Evidence &b) {
        return a.confidence > b.confidence;
    });
    QList<MdiProposals::Source> sources;
    QSet<QString> seen;
    for (const MdiDating::Evidence &e : support) {
        const QString k = e.kind + QLatin1Char('\x1f') + e.label + QLatin1Char('\x1f') + e.url;
        if (seen.contains(k))
            continue;
        seen.insert(k);
        MdiProposals::Source s;
        s.kind = e.kind;
        s.label = e.label;
        s.detail = e.detail;
        s.url = e.url;
        s.confidence = e.confidence;
        sources << s;
    }
    return sources;
}

QString textKey(const QString &value)
{
    // Years and generic words do not tell two event names apart
    QStringList kept;
    for (const QString &w : key(value).split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        bool digits = true;
        for (const QChar c : w)
            digits = digits && c.isDigit();
        if (!digits && !genericWords().contains(w) && !stopWords().contains(w))
            kept << w;
    }
    return kept.join(QLatin1Char(' '));
}

} // namespace

QList<MdiProposals::Proposal> MdiDating::combine(const QList<Evidence> &evidence, const QJsonObject &context,
                                                 double minConfidence, int alternatives)
{
    QList<MdiProposals::Proposal> out;
    const DateValue period = contextPeriod(context);

    // ---- Dates
    {
        QList<Evidence> dates;
        QList<DateValue> values;
        for (const Evidence &e : evidence)
            if (e.field == QLatin1String("date")) {
                const DateValue d = DateValue::fromText(e.value);
                if (d.isValid()) {
                    dates << e;
                    values << d;
                }
            }
        QList<Candidate> candidates;
        QSet<QString> seen;
        for (int i = 0; i < dates.size(); ++i) {
            const QString text = values.at(i).text();
            if (seen.contains(text))
                continue;
            seen.insert(text);
            Candidate c;
            c.date = values.at(i);
            c.value = text;
            c.precision = c.date.precision;
            QList<Evidence> contra;
            for (int k = 0; k < dates.size(); ++k) {
                const DateValue &o = values.at(k);
                if (o.contains(c.date) || c.date.contains(o))
                    c.support << dates.at(k);
                else if (c.date.gap(o) > 2)   // a day apart: a time zone, not a contradiction
                    contra << dates.at(k);
            }
            double score = combineConfidences(perKindMax(c.support));
            for (double x : perKindMax(contra))
                score *= 1 - 0.5 * x;
            // Outside the period the collection covers
            if (period.isValid() && c.date.gap(period) > 366)
                score *= 0.7;
            c.score = score;
            candidates << c;
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
            if (std::abs(a.score - b.score) > 0.02)
                return a.score > b.score;
            return a.date.span() < b.date.span();
        });
        if (!candidates.isEmpty() && candidates.first().score >= minConfidence) {
            Candidate best = candidates.first();
            // A vaguer value is supported by every finer one: the most precise
            // value inside it that is nearly as well supported is the proposal
            const double floor = std::max(minConfidence, 0.85 * best.score);
            const Candidate top = best;
            for (const Candidate &c : std::as_const(candidates))
                if (top.date.contains(c.date) && c.score >= floor && c.date.span() < best.date.span())
                    best = c;
            MdiProposals::Proposal p;
            p.field = QStringLiteral("date");
            p.value = best.value;
            p.precision = best.precision;
            p.confidence = best.score;
            p.sources = toSources(best.support);
            out << p;
            int added = 0;
            for (const Candidate &c : std::as_const(candidates)) {
                if (added >= alternatives)
                    break;
                // A day or two apart is a time zone or a late night, not another date
                if (c.score < minConfidence || c.date.overlaps(best.date) || c.date.gap(best.date) <= 2)
                    continue;
                MdiProposals::Proposal alt;
                alt.field = QStringLiteral("date");
                alt.value = c.value;
                alt.precision = c.precision;
                alt.confidence = c.score;
                alt.sources = toSources(c.support);
                out << alt;
                ++added;
            }
        }
    }

    // ---- Event and place
    for (const QString &field : {QStringLiteral("event"), QStringLiteral("place")}) {
        QList<Evidence> items;
        for (const Evidence &e : evidence)
            if (e.field == field && !e.value.trimmed().isEmpty())
                items << e;
        QHash<QString, Candidate> byKey;
        QStringList order;
        for (const Evidence &e : std::as_const(items)) {
            QString k = textKey(e.value);
            if (k.isEmpty())
                k = key(e.value);
            // "Europei Osijek" and "Europei Osijek 2015" are one event
            Candidate &c = byKey[k];
            if (c.value.isEmpty()) {
                order << k;
                c.value = e.value;
                c.precision = e.precision;
            }
            // Shown as its most confident source wrote it; a year kept when one has it
            const Evidence *strongest = nullptr;
            for (const Evidence &s : std::as_const(c.support))
                if (!strongest || s.confidence > strongest->confidence)
                    strongest = &s;
            if (!strongest || e.confidence > strongest->confidence)
                c.value = e.value;
            c.support << e;
        }
        QList<Candidate> candidates;
        for (const QString &k : std::as_const(order)) {
            Candidate c = byKey.value(k);
            double score = combineConfidences(perKindMax(c.support));
            for (const QString &other : std::as_const(order)) {
                if (other == k)
                    continue;
                // A different value weakens this one, a little: nested
                // folders often name both a series and its edition
                for (double x : perKindMax(byKey.value(other).support))
                    score *= 1 - 0.3 * x;
            }
            c.score = score;
            candidates << c;
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
            return a.score > b.score;
        });
        int added = 0;
        for (const Candidate &c : std::as_const(candidates)) {
            if (c.score < minConfidence || added > alternatives)
                break;
            // An alternative is shown only when it is close to the best
            if (added > 0 && c.score < 0.6 * candidates.first().score)
                break;
            MdiProposals::Proposal p;
            p.field = field;
            p.value = c.value;
            p.precision = c.precision;
            p.confidence = c.score;
            p.sources = toSources(c.support);
            out << p;
            ++added;
        }
    }
    return out;
}

//======================================================================
// Catalog pass
//======================================================================

bool MdiDating::ensureTables(const QString &connectionName)
{
    QSqlQuery q(QSqlDatabase::database(connectionName));
    // What the text model read in a folder name (cache: one question per name)
    const bool ok = q.exec(QLatin1String(R"(
        CREATE TABLE IF NOT EXISTS mdi_folder_reading (
            reading_key   TEXT NOT NULL PRIMARY KEY,
            reading_value TEXT NOT NULL,
            reading_model TEXT,
            reading_date  TEXT
        )
    )"));
    if (!ok)
        qWarning() << "MdiDating::ensureTables:" << q.lastError().text();
    return ok;
}

namespace {

QString relativeFolderOf(const QString &fullPath, const QString &sourceRoot)
{
    QString folder = QFileInfo(fullPath).path();
    QString root = sourceRoot;
    while (root.size() > 1 && root.endsWith(QLatin1Char('/')))
        root.chop(1);
    if (!root.isEmpty() && (folder == root || folder.startsWith(root + QLatin1Char('/'))))
        folder = folder.mid(root.size());
    return folder;
}

void applyModelAnswer(MdiDating::FolderReading *r, const QJsonObject &a)
{
    if (!a.value("event").toString().trimmed().isEmpty() && r->event.isEmpty()) {
        r->event = a.value("event").toString().trimmed();
        r->eventConfidence = 0.50;
        r->fromModel = true;
    }
    if (!a.value("place").toString().trimmed().isEmpty() && r->places.isEmpty()) {
        r->places << a.value("place").toString().trimmed();
        r->fromModel = true;
    }
    if (a.value("person").toBool())
        r->unknownWords.clear();   // a person's name: never an event, never searched for
}

// The rules' reading of a folder name, with the model's cached answer when any
MdiDating::FolderReading cachedReading(const QString &connectionName, const QString &name, const QJsonObject &context)
{
    MdiDating::FolderReading r = MdiDating::readFolderName(name, context);
    if (r.generic || !r.event.isEmpty() || !r.places.isEmpty() || r.unknownWords.isEmpty())
        return r;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QStringLiteral("SELECT reading_value FROM mdi_folder_reading WHERE reading_key = :key"));
    q.bindValue(":key", key(name));
    if (q.exec() && q.next())
        applyModelAnswer(&r, QJsonDocument::fromJson(q.value(0).toString().toUtf8()).object());
    return r;
}

// The web query of a folder: the nearest folder naming an event, with the
// nearest year at or above it. Empty when none would be specific.
QString folderQuery(const QString &relativeFolder, QHash<QString, MdiDating::FolderReading> &readings,
                    const QJsonObject &context, const QStringList &personNames,
                    const std::function<MdiDating::FolderReading(const QString &)> &read)
{
    QStringList parts = relativeFolder.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    std::reverse(parts.begin(), parts.end());
    MdiDating::FolderReading eventReading;
    MdiDating::DateValue year;
    for (const QString &name : std::as_const(parts)) {
        if (!readings.contains(name))
            readings.insert(name, read(name));
        const MdiDating::FolderReading &r = readings[name];
        if (eventReading.event.isEmpty() && !r.event.isEmpty())
            eventReading = r;
        if (!year.isValid() && r.date.isValid())
            year = MdiDating::DateValue::year(r.date.from.year());
    }
    if (eventReading.event.isEmpty())
        return QString();
    if (!eventReading.date.isValid())
        eventReading.date = year;
    return MdiDating::webQuery(eventReading, context, personNames);
}

// Folder names the rules could not read, read by the text model in batches
int readFoldersWithModel(const QString &connectionName, const QStringList &names,
                         QHash<QString, MdiDating::FolderReading> *cache,
                         const MdiDating::DeriveOptions &o)
{
    int calls = 0;
    if (names.isEmpty())
        return 0;
    // Cached answers first
    QStringList toAsk;
    {
        QSqlQuery q(QSqlDatabase::database(connectionName));
        q.prepare(QStringLiteral("SELECT reading_value FROM mdi_folder_reading WHERE reading_key = :key"));
        for (const QString &n : names) {
            q.bindValue(":key", key(n));
            if (q.exec() && q.next()) {
                MdiDating::FolderReading r = cache->value(n);
                applyModelAnswer(&r, QJsonDocument::fromJson(q.value(0).toString().toUtf8()).object());
                cache->insert(n, r);
            } else {
                toAsk << n;
            }
        }
    }
    // Without a model, the answers already given are all there is
    if (!o.ask)
        return 0;
    const int batch = 40;
    for (int i = 0; i < toAsk.size(); i += batch) {
        if (o.stop && o.stop->load())
            break;
        const QStringList part = toAsk.mid(i, batch);
        const QString shape = QStringLiteral("{\"folders\":[{\"name\":\"\",\"event\":\"\",\"place\":\"\",\"person\":false}]}");
        QString prompt = QStringLiteral(
            "These are folder names of a photo and video archive. For each name, say which event it names "
            "(a competition, a ceremony, a trip...) and which place (city, venue, region or country), only "
            "when the name itself says so; set person to true when the name is mainly a person's name. Leave "
            "a field empty when the name does not say it; do not invent. Keep the original spelling. Write "
            "in %1. Answer as JSON only: %2\n\n").arg(o.language.isEmpty() ? QStringLiteral("English") : o.language, shape);
        const QString brief = o.context.value("domain").toString().trimmed();
        if (!brief.isEmpty())
            prompt += QStringLiteral("The archive: ") + brief.left(600) + QStringLiteral("\n\n");
        prompt += QStringLiteral("FOLDERS:\n") + part.join(QLatin1Char('\n'));
        QString err;
        const QJsonObject answer = o.ask(prompt, &err);
        ++calls;
        QSqlQuery store(QSqlDatabase::database(connectionName));
        store.prepare(QLatin1String(R"(
            INSERT OR REPLACE INTO mdi_folder_reading (reading_key, reading_value, reading_model, reading_date)
            VALUES (:key, :value, :model, :date)
        )"));
        for (const QJsonValue &v : answer.value("folders").toArray()) {
            const QJsonObject a = v.toObject();
            const QString name = a.value("name").toString();
            QString match;
            for (const QString &n : part)
                if (key(n) == key(name))
                    match = n;
            if (match.isEmpty())
                continue;
            QJsonObject kept{{"event", a.value("event").toString().trimmed()},
                             {"place", a.value("place").toString().trimmed()},
                             {"person", a.value("person").toBool()}};
            store.bindValue(":key", key(match));
            store.bindValue(":value", QString::fromUtf8(QJsonDocument(kept).toJson(QJsonDocument::Compact)));
            store.bindValue(":model", QStringLiteral("text"));
            store.bindValue(":date", QDateTime::currentDateTime().toString(Qt::ISODate));
            store.exec();
            MdiDating::FolderReading r = cache->value(match);
            applyModelAnswer(&r, kept);
            cache->insert(match, r);
        }
    }
    return calls;
}

struct KbPassage {
    QString text, folded, title, path;
};

QList<KbPassage> loadKnowledge(const QString &connectionName)
{
    QList<KbPassage> list;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    if (!q.exec(QStringLiteral("SELECT p.passage_text, d.doc_title, d.doc_path FROM mdi_kb_passage p "
                               "JOIN mdi_kb_document d ON d.doc_id = p.passage_doc_id")))
        return list;
    while (q.next()) {
        KbPassage p;
        p.text = q.value(0).toString();
        p.folded = fold(p.text);
        p.title = q.value(1).toString();
        p.path = q.value(2).toString();
        list << p;
    }
    return list;
}

// Words of an event that tell it apart (no years, no generic words)
QStringList keyWords(const QString &event)
{
    QStringList list;
    for (const QString &w : textKey(event).split(QLatin1Char(' '), Qt::SkipEmptyParts))
        if (w.size() >= 3 && !genericEventWords().contains(w))
            list << w;
    return list;
}

// A word and, when it names a known place, the place's other names
QStringList placeForms(const QString &word)
{
    QStringList forms{word};
    const auto it = placeIndex().constFind(word);
    if (it == placeIndex().constEnd())
        return forms;
    for (auto p = placeIndex().cbegin(); p != placeIndex().cend(); ++p)
        if (p->display == it->display && !p.key().contains(QLatin1Char(' ')) && !forms.contains(p.key()))
            forms << p.key();
    return forms;
}

// Dates a text states near the words of an event
QList<MdiDating::Evidence> datesNear(const QString &text, const QString &folded, const QStringList &words,
                                     int folderYear, const QString &kind, const QString &label, const QString &url,
                                     double sameYear, double otherYear, int maxYearGap = -1)
{
    QList<MdiDating::Evidence> list;
    if (words.isEmpty())
        return list;
    int first = -1;
    for (const QString &w : words) {
        // A place may be written another way there ("Tokio", "Tokyo")
        int at = -1;
        for (const QString &form : placeForms(w)) {
            const int found = folded.indexOf(form);
            if (found >= 0 && (at < 0 || found < at))
                at = found;
        }
        if (at < 0)
            return list;   // every word of the event must be there
        if (first < 0 || at < first)
            first = at;
    }
    const int from = std::max(0, first - 300);
    const QString window = text.mid(from, 700);
    const QList<MdiDating::FoundDate> found = MdiDating::findDates(window, 1900);
    bool precise = false;
    for (const MdiDating::FoundDate &f : found)
        precise = precise || f.date.precision != QLatin1String("year");
    for (const MdiDating::FoundDate &f : found) {
        // A bare year far from any other clue says little; next to stated
        // dates it is usually the event's name ("Tokyo 2020", held in 2021)
        if (f.date.precision == QLatin1String("year") && (folderYear == 0 || precise))
            continue;
        // Another edition of the event, or another subject that shares a word
        if (maxYearGap >= 0 && folderYear > 0
            && (f.date.to.year() < folderYear - maxYearGap || f.date.from.year() > folderYear + maxYearGap))
            continue;
        const bool same = folderYear == 0
                       || (f.date.from.year() <= folderYear && folderYear <= f.date.to.year());
        const double c = same ? sameYear : otherYear;
        list << dateEvidence(f.date, f.date.precision == QLatin1String("year") ? c - 0.1 : c, kind, label,
                             excerptAround(window, f.position, f.length), url);
        if (list.size() >= 3)
            break;
    }
    return list;
}

} // namespace

MdiDating::DeriveSummary MdiDating::deriveCatalog(const QString &connectionName, int catalogId,
                                                  const DeriveOptions &o)
{
    DeriveSummary summary;
    ensureTables(connectionName);
    MdiProposals::ensureTables(connectionName);

    // The files' own evidence (dating datums) and what other analyses said
    const QList<MdiData::Datum> datums = MdiData::forCatalog(connectionName, catalogId, QStringLiteral("dating"));
    if (datums.isEmpty())
        return summary;
    QHash<QString, QJsonObject> captions, classifications;
    for (const MdiData::Datum &d : MdiData::forCatalog(connectionName, catalogId, QStringLiteral("caption")))
        captions.insert(d.fullPath, d.value);
    for (const MdiData::Datum &d : MdiData::forCatalog(connectionName, catalogId, QStringLiteral("classification")))
        classifications.insert(d.fullPath, d.value);

    // Folder readings, by the rules, then by the model for the names left unread
    QHash<QString, FolderReading> readings;
    QHash<QString, QList<Evidence>> folderEvidence;   // relative folder -> evidence
    QHash<QString, QStringList> filesByFolder;
    for (const MdiData::Datum &d : datums)
        filesByFolder[relativeFolderOf(d.fullPath, o.sourceRoot)] << d.fullPath;
    QStringList unread;
    for (auto it = filesByFolder.cbegin(); it != filesByFolder.cend(); ++it)
        for (const QString &name : it.key().split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
            if (readings.contains(name))
                continue;
            const FolderReading r = readFolderName(name, o.context);
            readings.insert(name, r);
            if (!r.generic && r.event.isEmpty() && r.places.isEmpty() && !r.unknownWords.isEmpty())
                unread << name;
        }
    if (!unread.isEmpty())
        summary.modelCalls = readFoldersWithModel(connectionName, unread.mid(0, o.maxModelFolders), &readings, o);

    for (auto it = filesByFolder.cbegin(); it != filesByFolder.cend(); ++it)
        folderEvidence.insert(it.key(), readFolders(it.key(), o.context, &readings));

    // Folder consistency: what the dated files of a folder agree on
    QHash<QString, QJsonArray> intrinsic;
    for (const MdiData::Datum &d : datums)
        intrinsic.insert(d.fullPath, d.value.value("evidence").toArray());
    QHash<QString, Evidence> consistency;   // relative folder -> evidence
    for (auto it = filesByFolder.cbegin(); it != filesByFolder.cend(); ++it) {
        QMap<int, int> years;
        QMap<QString, int> months;
        QDate first, last;
        int dated = 0;
        for (const QString &path : it.value()) {
            for (const QJsonValue &v : intrinsic.value(path)) {
                const Evidence e = Evidence::fromJson(v.toObject());
                if (e.field != QLatin1String("date") || e.confidence < 0.70)
                    continue;
                const DateValue d = DateValue::fromText(e.value);
                if (!d.isValid() || (d.precision != QLatin1String("day") && d.precision != QLatin1String("month")))
                    continue;
                ++dated;
                ++years[d.from.year()];
                ++months[d.from.toString(QStringLiteral("yyyy-MM"))];
                first = first.isValid() ? std::min(first, d.from) : d.from;
                last = last.isValid() ? std::max(last, d.to) : d.to;
                break;
            }
        }
        if (dated < 3)
            continue;
        int topYear = 0, topYearCount = 0;
        for (auto y = years.cbegin(); y != years.cend(); ++y)
            if (y.value() > topYearCount) {
                topYear = y.key();
                topYearCount = y.value();
            }
        QString topMonth;
        int topMonthCount = 0;
        for (auto m = months.cbegin(); m != months.cend(); ++m)
            if (m.value() > topMonthCount) {
                topMonth = m.key();
                topMonthCount = m.value();
            }
        const double yearShare = double(topYearCount) / dated;
        const double monthShare = double(topMonthCount) / dated;
        DateValue d;
        double share = 0;
        if (first.daysTo(last) <= 7 && years.size() == 1) {
            d = DateValue::range(first, last);       // one event, a few days
            share = 1.0;
        } else if (monthShare >= 0.6) {
            d = DateValue::fromText(topMonth);
            share = monthShare;
        } else if (yearShare >= 0.6) {
            d = DateValue::year(topYear);
            share = yearShare;
        }
        if (!d.isValid())
            continue;
        const QString folderName = it.key().section(QLatin1Char('/'), -1);
        consistency.insert(it.key(), dateEvidence(d, std::min(0.70, 0.40 + 0.30 * share),
                                                  QStringLiteral("folder-consistency"), folderName,
                                                  QStringLiteral("%1/%2").arg(qRound(share * dated)).arg(dated)));
    }

    // Knowledge base and web results, per event
    const QList<KbPassage> knowledge = loadKnowledge(connectionName);
    QHash<QString, QList<Evidence>> outsideByFolder;
    for (auto it = folderEvidence.cbegin(); it != folderEvidence.cend(); ++it) {
        QString event;
        int year = 0;
        for (const Evidence &e : it.value()) {
            if (e.field == QLatin1String("event") && event.isEmpty())
                event = e.value;
            if (e.field == QLatin1String("date") && e.kind == QLatin1String("folder") && year == 0)
                year = DateValue::fromText(e.value).from.year();
        }
        if (event.isEmpty())
            continue;
        const QStringList words = keyWords(event);
        QList<Evidence> found;
        for (const KbPassage &p : knowledge) {
            found << datesNear(p.text, p.folded, words, year, QStringLiteral("knowledge"), p.title, p.path, 0.65, 0.40);
            if (found.size() >= 4)
                break;
        }
        // Web: only results the owner fetched (MdiWebSearch), never fetched here
        const QString query = folderQuery(it.key(), readings, o.context, o.personNames,
                                          [&o](const QString &n) { return MdiDating::readFolderName(n, o.context); });
        const QStringList queries = query.isEmpty() ? QStringList() : QStringList{query};
        for (const QString &q : queries)
            for (const MdiWebSearch::Result &r : MdiWebSearch::cachedResults(connectionName, q)) {
                const QString text = r.title + QStringLiteral(". ") + r.text;
                // A page about the event names its year ("Tokio Hotel" does not)
                if (year > 0 && !text.contains(QString::number(year)))
                    continue;
                found << datesNear(text, fold(text), words, year, QStringLiteral("web"), r.title, r.url, 0.55, 0.40, 2);
                if (found.size() >= 8)
                    break;
            }
        outsideByFolder.insert(it.key(), found);
    }

    // Proposals per file
    QHash<QString, QList<MdiProposals::Proposal>> byFile;
    int done = 0;
    for (const MdiData::Datum &d : datums) {
        if (o.stop && o.stop->load()) {
            summary.stopped = true;
            return summary;
        }
        const QString folder = relativeFolderOf(d.fullPath, o.sourceRoot);
        QList<Evidence> all;
        bool strongDate = false;
        for (const QJsonValue &v : d.value.value("evidence").toArray()) {
            const Evidence e = Evidence::fromJson(v.toObject());
            strongDate = strongDate || (e.field == QLatin1String("date") && e.confidence >= 0.70);
            all << e;
        }
        all << folderEvidence.value(folder);
        if (!strongDate && consistency.contains(folder))
            all << consistency.value(folder);
        all << outsideByFolder.value(folder);
        if (captions.contains(d.fullPath))
            all << readDerived(QStringLiteral("caption"), captions.value(d.fullPath), o.context);
        if (classifications.contains(d.fullPath))
            all << readDerived(QStringLiteral("classification"), classifications.value(d.fullPath), o.context);
        byFile.insert(d.fullPath, combine(all, o.context));
        ++summary.files;
        if (o.progress && (++done % 200 == 0))
            o.progress(done, datums.size());
    }
    const int pending = MdiProposals::replaceForFiles(connectionName, catalogId, QStringLiteral("dating"), byFile);
    if (pending < 0)
        summary.error = QStringLiteral("proposals not saved");
    else
        summary.proposals = pending;
    if (o.progress)
        o.progress(datums.size(), datums.size());
    return summary;
}

//======================================================================
// Web queries
//======================================================================

QString MdiDating::webQuery(const FolderReading &reading, const QJsonObject &context, const QStringList &personNames)
{
    if (reading.generic || reading.event.isEmpty())
        return QString();
    // Never a person's name: the words of known people, of the context's
    // people, and any capitalised word not understood are left out
    QSet<QString> banned;
    for (const QString &n : personNames)
        for (const QString &w : key(n).split(QLatin1Char(' '), Qt::SkipEmptyParts))
            banned.insert(w);
    for (const ContextEntity &e : contextEntities(context))
        if (e.kind == QLatin1String("person"))
            for (const QString &k : e.keys)
                for (const QString &w : k.split(QLatin1Char(' '), Qt::SkipEmptyParts))
                    banned.insert(w);
    for (const QString &u : reading.unknownWords)
        for (const QString &w : key(u).split(QLatin1Char(' '), Qt::SkipEmptyParts))
            banned.insert(w);

    QSet<QString> placeWords;
    for (const QString &p : reading.places) {
        const auto it = placeIndex().constFind(key(p));
        if (it != placeIndex().constEnd() && it->weak)
            continue;   // "Nice", "Sofia": also first names or words
        for (const QString &w : key(p).split(QLatin1Char(' '), Qt::SkipEmptyParts))
            placeWords.insert(w);
    }
    const QHash<QString, QString> glossary = glossaryForms(context);
    bool contextEvent = false;
    for (const ContextEntity &e : contextEntities(context))
        if (e.kind == QLatin1String("event") && e.name == reading.event)
            contextEvent = true;

    QStringList words;
    bool named = contextEvent;
    for (const QString &w : reading.event.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        const QString f = key(w);
        if (f.isEmpty() || banned.contains(f))
            continue;
        bool digits = true;
        for (const QChar c : f)
            digits = digits && c.isDigit();
        const bool allowed = digits || contextEvent || placeWords.contains(f) || specificEventWords().contains(f)
                          || genericEventWords().contains(f) || eventDescriptorWords().contains(f) || glossary.contains(f)
                          || glossary.values().contains(w) || stopWords().contains(f);
        if (!allowed)
            continue;
        named = named || placeWords.contains(f) || glossary.contains(f) || glossary.values().contains(w);
        // A known place in its usual spelling ("Tokio" -> "Tokyo"): what the encyclopedias use
        const auto known = placeIndex().constFind(f);
        if (placeWords.contains(f) && known != placeIndex().constEnd() && !known->display.contains(QLatin1Char(' ')))
            words << known->display;
        else
            words << w;
    }
    // Specific enough: a place, a known event or term, or a named kind of event with its year
    const bool hasYear = reading.date.isValid();
    if (!named && !(reading.specificEvent && hasYear))
        return QString();
    if (hasYear) {
        const QString y = QString::number(reading.date.from.year());
        if (!words.contains(y))
            words << y;
    }
    while (!words.isEmpty() && stopWords().contains(key(words.first())))
        words.removeFirst();
    while (!words.isEmpty() && stopWords().contains(key(words.last())))
        words.removeLast();
    int meaningful = 0;
    for (const QString &w : words)
        if (!stopWords().contains(key(w)))
            ++meaningful;
    return meaningful >= 2 ? words.join(QLatin1Char(' ')) : QString();
}

QVariantList MdiDating::webQueries(const QString &connectionName, int catalogId, const QString &sourceRoot,
                                   const QJsonObject &context, const QStringList &personNames, int max)
{
    struct Item { QString query, folder; int files = 0; };
    QHash<QString, Item> byQuery;
    QHash<QString, FolderReading> readings;
    QHash<QString, int> filesByFolder;
    for (const MdiData::Datum &d : MdiData::forCatalog(connectionName, catalogId, QStringLiteral("dating")))
        ++filesByFolder[relativeFolderOf(d.fullPath, sourceRoot)];

    for (auto it = filesByFolder.cbegin(); it != filesByFolder.cend(); ++it) {
        const QString query = folderQuery(it.key(), readings, context, personNames,
                                          [&](const QString &n) { return cachedReading(connectionName, n, context); });
        if (query.isEmpty())
            continue;
        Item &item = byQuery[query.toLower()];
        if (item.query.isEmpty()) {
            item.query = query;
            item.folder = it.key();
        }
        item.files += it.value();
    }
    QList<Item> items;
    for (const Item &i : std::as_const(byQuery))
        if (!MdiWebSearch::isCached(connectionName, i.query))
            items << i;
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.files > b.files; });
    QVariantList out;
    for (const Item &i : std::as_const(items)) {
        if (out.size() >= max)
            break;
        out << QVariantMap{{"query", i.query}, {"folder", i.folder}, {"fileCount", i.files},
                           {"reason", i.folder.section(QLatin1Char('/'), -1)}};
    }
    return out;
}

---
id: Proposals
title: Proposals
description: "Teca's proposals: dates, events and places deduced for the files, approved per file or per folder, web lookups only when switched on, and your own value never replaced"
version: "3.0"
---
# Proposals
![Status](https://img.shields.io/badge/Status-Teca%20only-purple) ![3.0](https://img.shields.io/badge/Version-3.0-blue)

## Summary
Teca deduces values for the [information](FileInformation) of the files — the date above all, and also the event, the place, the title or the author — and **proposes** them. A proposal is written into the file's information only when you approve it.

* [Where the proposals come from](#sources): the metadata, the names, the folders, the context, the reference documents, and the web only when it is switched on.
* Approving [per file](#per-file) or [per folder](#per-folder).
* [Your own value is never replaced](#your-value).
* [Web lookups](#web), off unless switched on for the collection.

Teca is based on Katalog by Stéphane Couturier.

![Proposals for this folder: the date and the event deduced for its files, each with its sources, how sure it is, and Approve for n files](/img/proposals_01_folder.png)

## Where the proposals come from {#sources}
The **Dates and context** analysis looks at photos and videos. For each file it gathers clues to its date and to the other fields of its information, then proposes values. It never writes a file's information by itself.

| Clue | Shown as |
|------|----------|
| The date stored in a photo, or the creation time of a video, read from the file while its drive is connected. | *Photo metadata*, *Video metadata* |
| The file name. | *File name* |
| The names of its folders, for example `Munich 2015`. | *Folder* |
| The dates of the other files in the same folder. | *Other files of the folder* |
| What the AI saw in the image, the text visible in it included. | *Image description* |
| The context of the archive and the notes of the catalog, written in *Archive knowledge*. | *Context of the archive* |
| The reference documents of the archive. | *Reference document* |
| The web, only when it is switched on (see [Web lookups](#web)). | *Web* |

Each proposal shows the field, the value proposed, how sure Teca is (*n% sure*) and its sources, in words: for example *Photo metadata: 12/07/2015* or *Folder: Munich 2015*.

The analysis uses the collection's AI model, which runs on this computer by default. It runs in the background, after the catalog is created, and only on the files not handled yet. It is on by default, and is started in one of these ways:
* with the switch *Dates and context* of the *AI analysis* section, when a drive is added (*Add a drive*) or with *Analyse with AI* in *Browse the archive*;
* with *Deduce dates and context for this drive*, in [Proposals for this folder](#per-folder).

## Approving for one file {#per-file}
In *File details* (in *Browse the archive* and in the search results), the section **Proposals** lists the proposals of the file selected: *Deduced from the metadata, the names, the folder, the context and the reference documents. Nothing is written until you approve it.*

| Button | Effect |
|--------|--------|
| *Approve* | Writes the value into the file's information. |
| *Discard* | Drops the proposal. It is not proposed again for that file while its source stays the same. |

## Approving for a folder {#per-folder}
In *Browse the archive*, the *Proposals* action of the toolbar opens **Proposals for this folder**: the proposals of the files of the folder shown and of its sub-folders, grouped by field and value. Each group shows the number of files (*n files*), how sure Teca is (*n% sure*) and its sources.

| Button | Effect |
|--------|--------|
| *Approve for n files* | Writes the value into the information of every file of the group. |
| *Discard* | Drops the proposals of the group. |
| *Deduce dates and context for this drive* | Starts the analysis for the whole drive, in the background. The proposals appear in the window as it goes. |
| *Close* | Closes the window. |

When the folder has nothing to approve, the window says so: the proposals appear after the AI analysis of the drive (*Dates and context*).

The card *To review* of *Home* counts the proposals waiting, and its *Browse* button opens *Browse the archive* on a folder that has some, with this window open (see [Search in the information](SearchInformation#to-review)).

## Your own value is never replaced {#your-value}
A value you wrote is yours: no approval replaces it, for one file, for a folder or for many files at once.

* When a field of the file already has your own value, the proposal shows it below the value proposed: *Yours: …*
* Approving such a proposal leaves your value as it is. A message says how many proposals are left to approve, because the file already has its own value for that field.
* Your value changes only when you edit it yourself, with *Edit the information* (see [File information](FileInformation#one-file)).

## Web lookups {#web}
Web lookups help to date events, places and years found in the names. They are **off by default** and are switched on for each collection separately.

In *Proposals for this folder*, the card **Look on the web**:
1. The switch *Web searches allowed for this collection* turns them on or off for the collection.
2. *Prepare the searches* lists the searches that would be sent. When the names hold no event to look up, it says so.
3. Nothing leaves this computer until *Send these searches*.
4. At the end, a notification says how many searches were sent and how many had an answer. The answers become sources of proposals, shown as *Web*.

Only the search words leave this computer: never a file, an image, a preview or a passage of text. The searches go to DuckDuckGo and Wikipedia (in Italian and in English), with no account.

On *Home*, the card *Data sent* counts the web searches sent, and says when web searches are allowed for the collection.

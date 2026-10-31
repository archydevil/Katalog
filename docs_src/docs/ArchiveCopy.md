---
id: ArchiveCopy
title: Copy of the archive data
description: "Teca's copy of the archive data: what it holds, how it runs in the background, the reminder after 30 days, and why titles, events, people and approved faces need it"
version: "3.0"
---
# Copy of the archive data
![Status](https://img.shields.io/badge/Status-Teca%20only-purple) ![3.0](https://img.shields.io/badge/Version-3.0-blue)

## Summary
**Save a copy…**, on *Home*, writes a dated copy of the archive data into a folder you choose: the collection, its AI data, the action history and the settings.

* [Why it matters](#why): titles, events, people and approved faces live only in the collection.
* [What the copy holds](#content).
* [How to save a copy](#how): it runs in the background, and Teca keeps working.
* [The reminder](#reminder) after 30 days.

Teca is based on Katalog by Stéphane Couturier.

![Home with the card Copy of the archive data: the date of the last copy and Save a copy…](/img/archivecopy_01_home.png)

## Why it matters {#why}
A catalog can always be made again: Teca reads the drive once more. The work you add on top of it cannot:
* the titles, events, places, dates and notes written for the files;
* the people, and the faces approved for them;
* the proposals approved;
* the context of the archive and the history of the actions.

All of this lives **only in the collection**, on this computer. It is not on the drives, and not inside the files. If the disk of this computer fails, or the collection folder is lost, it is lost with it. A copy kept somewhere else — an external drive, a backup disk — keeps it safe.

## What the copy holds {#content}
Each copy is a **new folder**, named with the date and time, for example `Teca copy 2026-10-31 18-45`, inside the folder chosen. It holds a copy of:

| Part | Content |
|------|---------|
| The collection | Its database file, or the files of its collection folder. |
| The AI data | The descriptions, transcripts, summaries and studies made by the AI, the files' information, the people, the faces, the proposals and the context of the archive. |
| The action history | The restore points of the catalogs (see [Action history](ActionHistory)). |
| The settings | The settings file. |

* Each part is copied only when it exists.
* The copy is consistent even while Teca keeps writing, for example during an AI analysis.
* An earlier copy is never overwritten or deleted: each copy gets its own folder.
* The files copied are not changed, moved or deleted.
* The preview images and clips, kept in their own destination folder, are not part of the copy.

For a collection on a hosted database, the copy holds everything but that database, which is kept on its server; the card says so before the copy starts.

## How to save a copy {#how}
1. On *Home*, the card **Copy of the archive data** shows the date of the last copy (*Last copy:* followed by the date), or *No copy yet*.
2. *Save a copy…* opens a window to choose a folder. The folder must already exist, and be outside the collection folder: an external drive is a good place.
3. The copy runs in the background. Its progress shows on the card and in the [activity panel](Interface#activity-panel). The AI analysis keeps running meanwhile, and Teca keeps working.
4. At the end, a notification says *Copy saved in* followed by the folder, and the card shows the new date.

When the copy cannot be saved, the card says why, and nothing is written:
* *The copy needs a folder that exists.*
* *The copy needs a folder outside the collection folder.*
* the destination does not have enough free space: the message gives the space needed and the space left;
* *A copy of the archive data is already being saved.*

## The reminder {#reminder}
At the top of *Home*, a reminder shows when:
* no copy of the archive data has been saved yet, or
* the last copy is **more than 30 days old**.

It recalls that titles, events, people and approved faces live only in this collection, and offers *Save a copy…*. The tag of the card *Copy of the archive data* then turns to a warning. Once a copy is saved, the reminder goes away for 30 days.

The date of the last copy is kept for each collection separately.

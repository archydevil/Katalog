---
id: SearchInformation
title: Search in the information
description: "Teca's search In the information, the chips of File details, the To review card on Home, and what happens when a search finds nothing"
version: "3.0"
---
# Search in the information
![Status](https://img.shields.io/badge/Status-Teca%20only-purple) ![3.0](https://img.shields.io/badge/Version-3.0-blue)

## Summary
This page describes three things that help find and review what was written about the files:

* the way of searching [In the information](#in-the-information), in *Search the drives*, and the [chips](#chips) of *File details* that start it;
* the card [To review](#to-review) on *Home*;
* what happens [when a search finds nothing](#nothing-found).

The other search criteria are described in [Search](Search), and the results in [Search results](SearchResults).

Teca is based on Katalog by Stéphane Couturier.

![Search the drives with In the information chosen, and its results in a list of their own](/img/searchinformation_01_overview.png)

## In the information {#in-the-information}
In *Search the drives*, the card **What are you looking for** offers three ways of searching. One or several can be on at once:

| Way | Where it looks |
|-----|----------------|
| *By name* | In the names of files and folders. |
| *By meaning* | With other words too, in the descriptions, summaries and transcripts made by the AI. |
| **In the information** | In the title, event, place, date, author, notes, tags and people written for the files. |

*In the information* finds the files whose [information](FileInformation) — or whose people's names, roles and organisations — contain the words typed, in the field *A name, an event, a place, a tag…*:
* it finds the words **as written**, not by meaning: every word searched must be found;
* upper and lower case do not matter;
* it searches the catalogs of the drives chosen in *My drives*, as the other ways do;
* the results of a search by name do not change.

`Enter` in the field, or *Search*, runs the search.

### The results {#results}
The files found show in a list of their own on the results page, under **In the information**: *Files whose information or people hold every word searched. Each one opens in Browse.* A click on a file, or `Enter`, opens it in *Browse the archive*, selected and shown in *File details*.

When nothing matches, the list says *No file has these words in its information.*

*In the information* finds exact words. To find information with other words of the same meaning, *By meaning* searches the information and the people of the files too.

:::note
*In the information* is not available for a collection on a hosted database (*Hosted* mode).
:::

### From File details: the chips {#chips}
In *File details*, the event, the place, each tag and each person of a file show as chips (see [File information](FileInformation#chips)). Activating a chip runs the search *In the information* with that value alone, and shows the results: for example, every photo of the same event, or every file where the same person is.

## To review, on Home {#to-review}
The card **To review** on *Home* says what waits for your approval, each item with its count:

| Item | Button | Opens |
|------|--------|-------|
| *Faces to approve* | *People* | The [People](People#faces-to-approve) page, where the faces proposed are approved or rejected. |
| *Proposals to approve* | *Browse* | *Browse the archive* on a folder with proposals, with [Proposals for this folder](Proposals#per-folder) open. |

* The card shows only when something waits.
* Its counts are read again while *Home* is shown, for example during an analysis.
* *Home* adds no action of its own: the approvals are made in the places it opens.
* The card is not shown for a collection on a hosted database.

## When a search finds nothing {#nothing-found}
When a search finds no file, the results page shows the card **Search again with one change**: *Nothing was found. The same search can run again in another way or in more places.*

Each button runs the same search again at once, with one change:

| Button | Change |
|--------|--------|
| *File names or Folder paths* | Looks for the words in the folder paths too, when the search looked in the file names only. |
| *By meaning* | Looks for the same words by meaning. Offered when the AI model on this computer is reachable. |
| *In the information* | Looks for the same words in the information of the files. |
| *All the drives* | Runs the search in all the drives, when it was run in one drive or group. The choice in *My drives* does not change. |

A way the search already used is not offered again. Nothing else changes in the search criteria.

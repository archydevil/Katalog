---
id: ActionHistory
title: Action history
description: "Teca's action history: restore points of the catalogs, Undo, Restore how it was before, Open as a separate catalog, and joining split catalogs"
version: "3.0"
---
# Action history
![Status](https://img.shields.io/badge/Status-Teca%20only-purple) ![3.0](https://img.shields.io/badge/Version-3.0-blue)

## Summary
Every change to a catalog leaves a **restore point**. The page **Action history**, in the sidebar under *Archive*, shows what happened and can take the catalogs back to how they were before.

* [Restore points](#restore-points): the last 10 versions of each catalog.
* [Undo](#undo), right after an action.
* [Restore how it was before](#restore) and [Open as a separate catalog](#open-separate).
* [Joining split catalogs](#join) back into one.

Nothing in this page reads, writes or deletes anything on the drives: it works on the catalogs only.

Teca is based on Katalog by Stéphane Couturier.

![Action history: the split catalogs to join at the top, then the actions, newest first, each with its catalogs and Restore how it was before](/img/actionhistory_01_overview.png)

## Restore points {#restore-points}
Before Teca updates, splits, joins, edits or deletes a catalog, it records a restore point of each catalog concerned. A restore point holds the catalog as it was: its definition, its list of files and folders, and its AI analysis settings, with the date and a description of the action.

* The **last 10 versions** of each catalog are kept. When a newer one is recorded, the oldest beyond 10 is removed.
* Restore points are kept in the collection folder, apart from the collection itself: recording them does not change the collection.
* The history needs the collection's folder on this computer. For a collection on a hosted database without such a folder, the page says that the history is not kept.

## The actions {#actions}
The section **Actions** lists the recorded actions, newest first. Each one shows:
* its description (for example the catalogs updated) and its date;
* each catalog concerned, with its number of files as it was then, and *Open as a separate catalog*;
* *Restore how it was before*;
* the tag *Undone* once it has been restored.

When nothing has changed yet, the page says so: an action shows here as soon as a catalog is updated, split, joined, edited or deleted.

## Undo, right after an action {#undo}
Right after an action that recorded a restore point, a notification says what was done (for example *Updated:* followed by the catalog names) and offers **Undo**. *Undo* puts the catalogs back at once, as *Restore how it was before* does.

## Restore how it was before {#restore}
*Restore how it was before*, on an action, puts its catalogs back as they were before it.

A confirmation asks first, with the action's description: the catalogs go back to how they were before it; the files on the drives are not touched; the AI results follow; and the current state is saved first, so this can be undone too.

* *Restore* confirms. A notification says *Restored.*
* *Cancel* changes nothing.

What happens then:
1. The current state of the catalogs is saved as a new restore point.
2. The catalogs that resulted from the action are replaced by the versions recorded.
3. The AI results of the files — previews, descriptions, transcripts, information, people, faces — follow their files into the restored catalogs.

## Open as a separate catalog {#open-separate}
*Open as a separate catalog*, next to a catalog of an action, adds that earlier version to the collection as a **new catalog**. Its name is the catalog's name followed by the date of the action, for example *Photos 2015 (version of 30/10/2026 18:42)*.

* The current catalog and its AI results do not change.
* The new catalog has no AI results: previews, descriptions, transcripts and classifications are not copied.
* From then on it is an ordinary catalog, in *My drives*. A notification says *The earlier version is now a catalog of its own, in My drives.*

This is useful to look at what a catalog held before an update, without losing the current one.

## Joining split catalogs {#join}
A drive can hold several catalogs, one per folder, for example after a catalog was split. The section **Split catalogs** lists these drives: each card shows the drive, how many catalogs it holds and in which folder, and the names of the catalogs.

How to join them:
1. *Join into one catalog*, on the card, opens a window.
2. The window says how many catalogs become one, with every file, folder and AI result, and that nothing on the drive is touched and a restore point is saved first.
3. *Name of the catalog* holds the name of the new catalog; it can be changed.
4. *Join* joins them; *Cancel* changes nothing.

The result:
* one catalog, whose source is the common parent folder of the catalogs, with its sub-folders;
* all their files and folders, and all their AI results, now belong to it;
* the AI analysis settings are carried over;
* the old catalogs are removed from the collection;
* the search index of the new catalog is built in the background.

A notification says *The catalogs are one again:* followed by the name. The join is an action of the list: it can be undone like the others.

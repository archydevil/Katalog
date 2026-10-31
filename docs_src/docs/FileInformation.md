---
id: FileInformation
title: File information
description: "Teca's file information: title, event, place, date, author, notes, tags and people written for one file or many, and the clickable chips of File details"
version: "3.0"
---
# File information
![Status](https://img.shields.io/badge/Status-Teca%20only-purple) ![3.0](https://img.shields.io/badge/Version-3.0-blue)

## Summary
A file can carry information written by you: what it shows, when, where, who made it and who is in it. Photos are the main case, but any file can carry it.

* The [fields](#fields): title, description, event, place, date, author, notes, tags.
* For [one file](#one-file) or for [many files at once](#many-files).
* The [people](#people) in a file, added by hand.
* The [clickable chips](#chips) of *File details*, which find the other files with the same event, place, tag or person.

Teca is based on Katalog by Stéphane Couturier.

![The Information form of a photo: title, description, event, place, date, author, tags, notes, and the people in it](/img/fileinformation_01_form.png)

This information is yours:
* it is kept apart from the file's own dates and metadata, from the descriptions and classifications made by the AI, and from the *Labels* of folders, and it never changes them;
* nothing is written on the drives or in the files;
* it is kept when the collection is closed and opened again, and it shows whether the drive is connected or not;
* the [proposals](Proposals) deduced by Teca never replace it.

:::note
File information and people are not available for a collection on a hosted database (*Hosted* mode).
:::

## The fields {#fields}
Every field is optional.

| Field | Content |
|-------|---------|
| *Title* | A name for the file. |
| *Description* | What it shows or contains. |
| *Event* | The occasion: a wedding, a match, a trip. |
| *Place* | Where it was taken or made. |
| *Date* | Free text, for example `2015-07-12` or `July 2015`. |
| *Author* | Who made it: the photographer, the writer. |
| *Tags* | Free words, separated by commas. |
| *Notes* | Anything else. |
| *People* | The people in it: see [People](#people). |

## One file {#one-file}
The form *Information* opens in three ways:
* in *Browse the archive*, from the menu of a file (right click): *Add information…*;
* in *Browse the archive*, with the *Information* action of the toolbar, for the file selected;
* in *File details* (in *Browse the archive* and in the search results), with *Edit the information*.

The form shows the values the file already has. *Save* writes them, and a notification says *Information saved.*; *Cancel* changes nothing. A field emptied and saved is cleared.

## Many files at once {#many-files}
In the list of *Browse the archive*, several files are selected with the mouse:
* `Cmd`+click on macOS (`Ctrl`+click on Linux and Windows) adds a file to the selection or removes it;
* `Shift`+click selects a range of files.

*Information (n)* in the toolbar, or *Add information to n files…* in the menu of a file, then opens *Information for n files*. The form starts empty:
* only the fields filled in are applied to the files; the others stay as each file has them;
* tags and people are added to every file; the tags and people a file already has stay.

*Save* writes them, and a notification says *Information saved for n files.*

A single click still selects one file only, and a double click still opens it. The selection of several files works in the list view, with the mouse.

## People {#people}
The form lists the people of the file. Each person shows as a button: a click on it removes the person from the file when the form is saved.

*Add a person* opens *Who is it?*:
* the people of the archive are listed, and the field *Name, role or organisation…* narrows the list; a click chooses a person;
* under *New person*, *Add and choose* creates a person from a *First name*, a *Last name* and a *Role*, and chooses them;
* *Cancel* closes it.

For many files, the people chosen are added to every file.

No face is needed: a person can be added to any file, a document or a video as well as a photo. A person is also counted on a photo when one of its faces is approved for them (see [People and faces](People)). The *People* page lists the photos of each person.

## In File details {#file-details}
*File details* shows, for the file selected, the section **Information and people**: the file's information and its people. When nothing is written yet, it says so: *Nothing written yet: a title, an event, a place, the people in it…*

| Button | Effect |
|--------|--------|
| *Edit the information* | Opens the form *Information* for this file. |
| *Add a person* | Opens *Who is it?*; the person chosen is added to the file at once. |

For a photo with faces that have no approved name yet, a line says how many, and the faces are framed on the preview: see [Faces in File details](People#file-details).

### Clickable chips {#chips}
The event, the place, each tag and each person of the file show as **chips**. Activating a chip (a click, or `Enter` or `Space` with the keyboard) finds the files with the same value in their information: the other photos of the same event, of the same place, or of the same person. Its tooltip says *Find the files with this in their information*.

The chip runs the search *In the information* with that value, in the drives chosen in *My drives*, and shows the results: see [Search in the information](SearchInformation#chips).

## Search by meaning
The information of the files and the names, roles and organisations of their people are also found by the search *By meaning* of *Search the drives*. For that, they are read by the collection's AI model, which runs on this computer by default.

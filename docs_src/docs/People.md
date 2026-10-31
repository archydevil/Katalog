---
id: People
title: People and faces
description: "Teca's People page: the people of the archive, the faces found in the photos and approved by you, similar faces, groups of faces without a name, and the privacy of face data"
version: "3.0"
---
# People and faces
![Status](https://img.shields.io/badge/Status-Teca%20only-purple) ![3.0](https://img.shields.io/badge/Version-3.0-blue)

## Summary
The **People** page holds the people of the archive: who they are and the photos they are in. It is in the sidebar, under *Intelligence*.

* [People](#people) and their information: name, role, organisation, notes.
* The [faces](#how-faces-are-found) found in the photos. Teca only proposes who a face is: a face gets a name only when you approve it, one face or many at once.
* The [similar faces](#similar-faces) of a person, in the other photos.
* The [faces without a name](#faces-without-a-name), grouped by likeness, to name a whole group at once.
* [Ignore these faces](#ignore) and [Show the photo](#face-menu).
* [Delete all face data](#delete-face-data).
* [Privacy](#privacy): faces stay on this computer.

Teca is based on Katalog by Stéphane Couturier.

![People page: faces to approve at the top, then the list of people, the person chosen with their photos, and the groups of faces without a name](/img/people_01_overview.png)

:::note
People and faces are not available for a collection on a hosted database (*Hosted* mode).
:::

## People {#people}
A person has a *First name*, a *Last name*, a *Role*, an *Organisation* and *Notes*. A first name or a last name is needed; the other fields are optional.

| Action | Effect |
|--------|--------|
| *New person* | Opens an empty person card. *Save* adds the person. |
| *Name, role or organisation…* | The filter above the list: it shows only the people whose name, role or organisation contains the text typed. |
| *Edit* | On the person chosen: opens their card, with *Save*, *Delete* and *Cancel*. |

Each person is a card with their face (or their initials), their role and organisation, the number of photos they are in, and, when faces of theirs wait for approval, how many (*n to approve*).

Choosing a person shows:
* their notes;
* *Photos (n)*: the photos they are in. Selecting a photo shows it in *Browse the archive*. Above 60 photos, *Show all n photos* shows the rest;
* *Similar faces*: see [below](#similar-faces).

A photo counts for a person in two ways: the person was added to it by hand (see [File information](FileInformation#people)), or one of its faces was approved for that person.

A person can also be created on the spot, from a photo: *Add a person* opens *Who is it?*, where *Add and choose* makes a new person from a first name, a last name and a role.

### Delete a person {#delete-person}
*Delete*, on the person's card, first asks *Delete this person?*.
* Confirmed, the person is removed from the people and from the photos they were in. Their faces go back to having no name.
* The photos and the files stay as they are: nothing changes on the drives or in the catalogs.
* *Cancel* changes nothing.

## How faces are found {#how-faces-are-found}
The **Faces of the people** analysis looks for the faces in the photos of a drive, on this computer, in the background. *Work in progress* shows how it goes. It is off by default, and is started in one of two ways:
* on the *People* page, in the card *Find the faces in a drive*: the drive is chosen in the list, and *Find the faces* starts the search;
* with the switch *Faces of the people* in the *AI analysis* section, when a drive is added (*Add a drive*) or with *Analyse with AI* in *Browse the archive*.

Nothing is attributed without your approval: the analysis finds the faces, then proposes who they are once you have named some.

When this copy of Teca cannot recognise faces (a part of it or its models are missing), the page says why. People can still be added to photos by hand, from *File details*.

### The first names {#first-names}
At first, no face has a name: the faces found are grouped under [Faces without a name](#faces-without-a-name). Naming a few groups gives the analysis its first examples. From then on, it proposes these people for the faces that look like theirs, under [Faces to approve](#faces-to-approve) and in [Similar faces](#similar-faces).

## Faces to approve {#faces-to-approve}
The analysis proposes who these faces are. Each face shows the name proposed and how alike it is (*n% similar*). Only the faces approved get the name; the others stay without one.

Each face has a tick: a click, or `Enter` or `Space` when the face has the keyboard focus, ticks it or unticks it. Hovering a face shows the photo it comes from.

With one or more faces ticked:

| Button | Effect |
|--------|--------|
| *Approve the selected (n)* | Each ticked face gets the name proposed for it. |
| *Not them* | The proposals are removed; the faces stay without a name. That person is not proposed again for these faces. |
| *Ignore the selected (n)* | See [Ignore faces](#ignore). |

The *To review* card on *Home* also counts the faces to approve, and opens this page (see [Search in the information](SearchInformation#to-review)).

## Similar faces {#similar-faces}
For the person chosen, *Look for similar faces* lists the faces in the other photos that look like the approved faces of that person, the most alike first. Faces that already have a name, and faces already rejected for that person, are not listed.

The comparison runs in the background (*Comparing the faces…*). When it finds nothing, a message says so: approving more faces of this person helps.

| Button | Effect |
|--------|--------|
| *Attribute to …* | Followed by the person's name and the number of faces ticked: the ticked faces get that name. |
| *Tick all* | Ticks every face of the list. |
| *Not …* | Followed by the person's name: the ticked faces are not that person, and are not proposed for them again. |

One face can be attributed on its own: only that face is ticked.

## Faces without a name {#faces-without-a-name}
Faces that look alike are grouped, so that a group likely holds one person. Each group shows its number of faces (*n faces*) and its first 12 faces; *Show all n faces* shows the others.

How a group gets a name:
1. The faces that are really the same person are ticked.
2. *Name these faces* opens *Who is it?*: a person of the list is chosen (the field *Name, role or organisation…* narrows the list), or a new one is made with *Add and choose*.
3. The ticked faces get that name. They now count among the person's photos.

When no face of the group is ticked, the name goes to the whole group, the faces not shown included. A confirmation asks first: *Name all n faces of the group?* The groups are approximate: ticking the right faces first is safer. *Name these faces* confirms; *Cancel* changes nothing.

## Ignore faces {#ignore}
Some faces are not worth a name: a poster, a crowd in the background, a shape taken for a face. Ignoring them takes them out of the lists:
* *Ignore the selected (n)*, under *Faces to approve* or in a group, for the faces ticked;
* *Ignore this face*, in the [menu of a face](#face-menu).

A confirmation asks first: *Ignore n faces?*
* Ignored faces are no longer proposed nor grouped, and no list of the page brings them back.
* Nothing is deleted: on their photo, in *File details*, the faces still show and can still be named.
* A new face search keeps them ignored.
* *Cancel* changes nothing.

## The menu of a face {#face-menu}
Every face of the page has a menu. It opens with *More actions* (the *⋯* button at the top right of the face), with a right click, or with the menu key:
* **Show the photo** opens *Browse the archive* at the photo's drive and folder, with the photo selected and shown in *File details*. It works whether the drive is connected or not. *Back* returns to *People*. The choice in *My drives* does not change.
* **Ignore this face**: see [Ignore faces](#ignore).

## Faces in File details {#file-details}
In *File details*, a photo with faces shows a frame on each face: green when its name is approved, in the colour of the AI when a name is only proposed. Hovering a frame shows the name, *Maybe …* with the name proposed, or *Who is it?*.

A click on a frame opens its menu:

| Entry | Effect |
|-------|--------|
| *It is …* | Approves the name proposed. |
| *Who is it?…* | Chooses the person in *Who is it?*. For a face already named, the entry reads *Someone else…*. |
| *Not …* | Rejects the name proposed. |
| *Remove the name* | The face goes back to having no name. |
| *Ignore this face* | As on the *People* page. |

## Delete all face data {#delete-face-data}
The card *Face data*, at the bottom of the page, has *Delete all face data*. It forgets every face of this collection. A confirmation first says what is deleted and what stays: *Delete all face data?*

| Deleted | Stays |
|---------|-------|
| Every face found in this collection, and its crop. | The people. |
| The names given through faces. | The names added to photos by hand. |
| The record of the face search, so a new search starts afresh. | The files' information, and the photos themselves. |

*Delete* confirms, and a notification says *Face data deleted.*; *Cancel* changes nothing. It is refused while an AI analysis is running. A new face search can be run later.

## Privacy {#privacy}
Faces are personal data. Teca keeps them on this computer:
* the faces are found and compared on this computer, without any internet connection;
* the faces, their crops and the names given through them are never sent anywhere, to no AI service, whatever the settings;
* they are kept only in the collection's AI data and, for the crops, in the folder chosen for the previews;
* the HTML export never contains face data;
* *Delete all face data* removes them all, at any time.

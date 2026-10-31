---
id: DocumentReader
title: Document reader
description: "Teca's study of a document (synthesis, key facts, fragments, OCR), the Read the document view, and the HTML export"
version: "3.0"
---
# Document reader
![Status](https://img.shields.io/badge/Status-Teca%20only-purple) ![3.0](https://img.shields.io/badge/Version-3.0-blue)

## Summary
Teca can **study** a text document: it reads it and keeps a synthesis, the key facts, and the document cut into fragments, each with its summary.

* [The study of a document](#study): what it finds, and how scanned pages are read (OCR).
* [Read the document](#reader): the synthesis, the facts and the fragments in one view, even when the drive is not connected.
* [Export HTML](#export): a web page of a document, a folder or a whole catalog, readable without Teca.

Teca is based on Katalog by Stéphane Couturier.

![Reading a document: the synthesis, the key facts, then the document fragment by fragment](/img/documentreader_01_overview.png)

## The study of a document {#study}
The **Study of the documents** analysis works on text documents. It is on by default in the *AI analysis* section, when a drive is added (*Add a drive*) or with *Analyse with AI* in *Browse the archive*. It runs on this computer, in the background; *Work in progress* shows how it goes.

From the text of the document, the collection's AI model finds:

| Part | Content |
|------|---------|
| **Synthesis** | The gist of the whole document, in a few lines. |
| **Key facts** | The dates, people, places, organisations, events and recipients it names. |
| **Tags** | A few words that describe it. |
| **Fragments** | The document in reading order, cut into parts, each with its pages and its summary. |
| **Links to the archive** | The glossary terms it uses, the reference documents and the people of the archive it relates to (see *Archive knowledge* and [People and faces](People)). |

A person named in a document shows as a mention: it is not added to the file's people by itself.

**Scanned documents.** A PDF page with no text in it (a scan) is read with OCR, on this computer, in Italian and English. Such a document shows the tag *Read with OCR*. When the OCR program is missing, these pages are skipped, with the reason.

The AI model runs on this computer by default: the document and its text are not sent to the internet.

## In File details {#file-details}
For a studied document, *File details* shows the card **Study of the document**: the type and the date of the document, the start of its synthesis, and the button *Read the document*.

## Read the document {#reader}
*Read the document* opens the view *Reading:* followed by the file name. From top to bottom:
1. The name of the document, its type, date and author, with the tags *Read with OCR* (for a scan), its language and its number of fragments.
2. **Synthesis**.
3. **Key facts**: *Dates*, *People*, *Places*, *Organisations*, *Events*, *Recipients* and *Tags*.
4. **In the archive's context**: *Glossary*, *Reference documents* and *People of the archive*, when the document relates to them.
5. **The document, fragment by fragment**: each part with its title (or *Part n*), its pages, its summary and its tags. *Show the text* opens the text of the fragment below it; *Hide the text* closes it.

The view works whether the drive is connected or not, and with the keyboard and a screen reader. *Close* (or `Esc`) closes it.

While an analysis runs, the view follows it without losing the place or closing the fragments opened.

**A document not studied yet.** The view says so, and offers *Study the documents of this drive*: it starts the study of the drive that holds this document, in the background.

## Export HTML {#export}
**Export HTML** writes a web page that can be read in any browser, without Teca, and shared. It is offered:
* in the document reader, for that document;
* in the toolbar of *Browse the archive*, with three choices: *The selected file*, *This folder*, *The whole catalog*. For a folder or a catalog, the page starts with an index of its files.

A save window asks where to write the page. One file is written at once. A folder or a whole catalog is written in the background: its progress shows in the [activity panel](Interface#activity-panel) at the bottom of the window, and Teca keeps working. At the end, a notification says *Exported:* followed by the path.

**What the page holds:** for each file, its information and its people, the proposals with their sources, the description and the classification made by the AI, the previews, and for a document its full study with every fragment. It never holds face data.

**Accessible:** headings in order, page regions, text alternatives for the images, the language of the page, and a layout for printing.

**Where it goes:** nothing is sent; the page is written only where you choose. A folder of this computer is the right place for it: Teca only reads the catalogued drives.

:::note
The page holds the names of the people linked to the files and the information written about them. Sharing the page shares them too.
:::

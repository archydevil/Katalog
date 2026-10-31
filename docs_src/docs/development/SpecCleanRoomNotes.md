---
id: SpecCleanRoomNotes
title: Clean-Room Rewrite — Protocol and Specification Sanitisation (plan B)
description: How Teca could be rewritten free of Katalog's code if no commercial licence is granted; the inventory of what the specifications must shed first
---

# CLEAN-ROOM REWRITE — PROTOCOL AND SPECIFICATION SANITISATION (PLAN B)

![Status](https://img.shields.io/badge/Status-Plan%20B-lightgrey) ![Fork](https://img.shields.io/badge/Fork-archydevil%2FKatalog-blue)

**Nothing here is a requirement and nothing here is started as code.** This is
the fork owner's *plan B* of 2026-10-31: if the upstream author grants no
commercial licence (the letter of the *B* path), Teca may be rewritten so that
it contains none of Katalog's code. The *A* path by hand was estimated at
€0.5–1.8 M and 2–3 years; with AI coding under the protocol below, at
€50–120 k and 4–6 months — the verification, not the writing, sets the pace.
This is not legal advice; a lawyer reviews the protocol before it is used.

## Why a protocol, not a gradual rewrite

Rewriting Katalog file by file, by people who have read it, produces a work
with the same architecture, the same data model and the same decisions: still
a derivative. A clean room separates two teams:

- the **specification team** ("dirty room": the owner and the agents of this
  repository) may read Katalog and writes *functional* specifications —
  behaviour, data, rules — with no code, no identifiers, no SQL, no file names;
- the **implementation team** ("clean room": people or AI sessions that have
  never seen Katalog) receives the specifications and nothing else, and writes
  the new program from them.

With AI sessions as the clean room, the whole conversation of every session
is kept: it is the proof that no line of Katalog was passed across. That
evidence is easier to produce than with human teams, and a lawyer should
confirm it suffices.

## Protocol

| # | Rule |
| --- | --- |
| 1 | A clean-room session receives **only** the sanitised specifications of `docs_src/docs/development/clean/` (to be created), the public contracts (`SpecTecaApi.md`'s OpenAPI document, file formats to import), and the owner's answers to its questions. Never a path into this repository, never a `core/` or `qt_quick/` file, never a database created by Katalog. |
| 2 | The owner, and anyone who has read Katalog, **reviews behaviour, not code**: they run the clean-room build against the specification's test charters and report pass/fail. They do not read the new code side by side with Katalog's and do not suggest "how Katalog does it". |
| 3 | Every clean-room prompt and answer is archived with the commit it produced. |
| 4 | The **data model is new**. Katalog collections are read through an *importer* written from the published description of the format (table and column names, types, meaning), not from Katalog's SQL. Interoperability with an existing format is lawful in the EU (Software Directive, art. 6); copying its DDL is not needed for it. |
| 5 | The owner's own code of this fork (the files added by the fork, ~28 000 lines) is the owner's copyright and may be reused **after** a check that it quotes no upstream code and once it is adapted to the new core — or it is re-specified and re-implemented like the rest, which is cleaner. |
| 6 | Qt and KDE Frameworks are LGPL: a proprietary Teca links them dynamically, ships their notices and allows relinking, or buys a Qt commercial licence. External programs (ffmpeg, whisper.cpp, poppler, tesseract) stay external, as today (`MDI-C7`). |
| 7 | Code produced by an AI with little human authorship may not be protected by copyright (US Copyright Office, 2025; EU case law requires a human author). A proprietary Teca is still distributable as such — binaries, EULA, trade secret — but suing a copier of those parts is uncertain. The trademark *Teca* and the services are protected regardless. |

## Inventory — what the specifications must shed (2026-10-31)

Counted over `docs_src/docs/development/Spec*.md`:

| Kind | Count | Example | Replacement |
| --- | --- | --- | --- |
| C++/QML identifiers (`Class::method`, `camelCase()`, `*.cpp`, `*.h`, `*.qml`) | ≈ 990 | `MdiPublish::isComplete`, `PageSettings.qml`, `getReadOnly()` | the behaviour, in words: "the completeness check", "the Settings page" |
| Table and column names, SQL | ≈ 660 | `mdi_datum`, `device_path`, `SELECT …` | the data model section of each page, in words and in a neutral schema description (entity, attribute, type, meaning) |
| Settings keys | not counted | `MediaIntelligence/PublishFolder` | "the publish folder setting, per collection" |
| Upstream-only pages (K2 widgets, K2 deployment, `k2/` doc copies) | 4 pages | `SpecK2Deployment.md` | dropped: Teca has no K2 |

Per page, the heaviest: `SpecMediaIntelligence.md` (≈ 320 identifiers, 3 500
lines — the fork's own design, mostly), `SpecSearchList.md` (54),
`SpecDevicesPage.md` (50), `SpecExplore.md` (17), `SpecCollectionOpen.md` (13),
`SpecCommandLine.md` (9).

## Method

1. **Copy, do not edit in place.** The sanitised pages go to
   `docs_src/docs/development/clean/`, one per subject, with the same IDs so the
   test charters stay traceable. The originals stay as they are for this
   repository's own gate.
2. **One page at a time**, in the order a clean-room team needs them:
   collection and open (`SpecCollectionOpen`, `SpecCollection`), devices and
   catalogs (`SpecDevicesPage`, `SpecDeviceStorageRoot`, `SpecStorageIdentity`,
   `SpecDevicesSplit`), creation (`SpecCatalog*`), search (`SpecSearchList`,
   `SpecSearchResults`, `SpecSearchResultsFilters`, `SpecSelection`), browse
   (`SpecExplore`), the rest (`SpecBackup`, `SpecTags`…), then
   `SpecMediaIntelligence` split by section.
3. **A sanitisation checklist per page**: no identifier matches
   `[A-Za-z]+::[A-Za-z_]+`, `\.(cpp|h|qml)\b`, `\b[a-z]+[A-Z]\w+\(\)`; no SQL
   keyword; no settings key; every "how" sentence rewritten as a "what"; the
   data model described as entities and attributes. A script runs the regexes
   and blocks a page that fails.
4. **A data-model page** (`clean/DataModel.md`): every entity Teca stores, its
   attributes, their types and meaning, and the import mapping from Katalog's
   format — described, not quoted.
5. **Lots for the clean room**, each with its acceptance test charter: core
   model and storage → collection open → devices and catalog creation → search
   → browse → media intelligence (profiles, runner, each analysis) → publishing
   and read-only → API (`SpecTecaApi.md` is already behaviour-only, apart from
   `API-C1`–`C3`, which are rewritten for the clean room).

## Estimate (AI coding, under the protocol)

| Item | Estimate |
| --- | --- |
| Sanitising the specifications (steps 1–4) | 2–3 weeks of the specification team |
| Code produced by clean-room sessions, verified lot by lot | 15–25 session-days for ≈ 70 000 lines |
| Verification, tests, builds on macOS, Windows, Linux, debugging | 3–5 months of one person — the pace-setter |
| Base strings re-translated into 29 languages (AI, then review) | 1–2 weeks |
| AI usage | €3–8 k per month, 4–6 months |
| A person who drives the sessions and verifies, if not the owner | €30–50 k |
| Legal review of the protocol and of the result | €5–15 k |
| **Total** | **€50–120 k, 4–6 months** |

## When this plan starts

Only if the upstream author declines a commercial licence, and by the owner's
explicit decision then. Until that day, step 1 of the method (the `clean/`
folder) is not created: the *B + D* path has priority, and `SpecTecaApi.md` is
written behaviour-first so it serves both futures.

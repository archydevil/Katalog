---
id: SpecTecaApi
title: Teca API — Local HTTP Interface for Other Applications
description: A local HTTP API over katalog-core, so other applications read and drive a collection without linking to Teca
---

# TECA API — LOCAL HTTP INTERFACE FOR OTHER APPLICATIONS

![Status](https://img.shields.io/badge/Status-Draft-orange) ![Implementation](https://img.shields.io/badge/Implementation-planned-orange) ![Fork](https://img.shields.io/badge/Fork-archydevil%2FKatalog-blue) ![K3](https://img.shields.io/badge/K3-only-blue)

## Context

Decided by the fork owner on 2026-10-31 (the *B + D* path: a commercial licence
is asked of the upstream author; meanwhile the fork's commercial surface is
built **outside** Teca). Teca and `katalog-core` are GPL (`LICENSE.md`; the
source headers say GPL 2 or later): any program that links them, or that is
derived from them, is distributed under the GPL. A program that talks to Teca
**over a process boundary** — a socket — is a separate work and may carry any
licence. The API is that boundary: it is part of Teca, GPL, and is the one
contract other applications depend on.

The API serves the same collection the window serves, through `katalog-core`
only, with the same read-only rules (`SpecMediaIntelligence.md`, `MDI-F199`,
`MDI-C64`) and the same privacy stance (`MDI-C11`): it listens on this
computer only unless the user opens it to their private network, and it never
reaches the Internet. It is the natural shape of the *hub* of the P5 design
(`SpecMediaIntelligenceBacklogNotes.md`, D45): a reader of a Hosted or
published collection may be a client of this API instead of a second Teca.

The upstream `CMakeLists.txt` has carried a commented option
`BUILD_WEB_SERVER "KatalogWeb - REST API server"` with no code behind it; this
specification gives that slot its content, for K3 only (`MDI-C1`).

## Scope at a glance

| In scope | Not in scope |
|----------|--------------|
| A local HTTP/1.1 + JSON API over `katalog-core`: collection, devices, catalogs, folders, files, search (by name, by meaning, in the information), derived data and previews, jobs status; served by the Teca window while it runs and by a headless `teca --serve`. Authentication by a local token. Versioning. Read-only enforcement. Rate and size limits. OpenAPI description. | A public or Internet-facing server; TLS termination (a tunnel or reverse proxy does that — `HST-O1`); users and roles beyond "the token"; write operations in the first version (`API-F10`); any client application (those are separate works); the Hosted-database security itself (`SpecHostedSecurity.md`); K2 (`MDI-C1`). |

---

## Operational requirements — *why / for whom*

| ID | Requirement | Status |
|----|-------------|--------|
| API-O1 | A developer writes an application — on this computer or on the user's private network — that reads a Teca collection, searches it and shows its previews and AI results, without linking to Teca and under a licence of their own, because the only contact is an HTTP API with a published contract. | [Planned] |
| API-O2 | A user who turns the API on knows exactly who can reach it: nothing but this computer by default, their private network only when they say so, never the Internet; and nothing a client does can change the collection until the user allows writes in a later version. | [Planned] |
| API-O3 | A client written against version 1 of the API keeps working with every Teca that serves version 1, and learns from the server which versions it serves. | [Planned] |

## Functional requirements — *what the system does*

| ID | Requirement | Status |
|----|-------------|--------|
| API-F1 | **Turning it on.** Settings has an *API* section: a switch *Serve the API* (off by default), the port (default `48700`), the bind scope — *This computer only* (default: `127.0.0.1` and `::1`) or *My private network* (every address of this computer; a confirmation as `HST-F3`'s, since data leaves the machine) — and the token (`API-F2`). While the switch is on and a collection is open, the window serves the API; it stops when the collection closes or the window quits. The settings are per computer, in the application settings (`MDI-C7`). | [Planned] |
| API-F2 | **The token.** Turning the API on generates a random token of at least 128 bits, shown once with *Copy* and *Regenerate*; it is stored in the application settings on Windows and Linux and in the login keychain on macOS when D45's keychain row is built (until then, the settings file, and Settings says so). Every request carries it as `Authorization: Bearer <token>`; a request without it, or with a wrong one, gets `401` and nothing else — not even the version. Regenerating invalidates the old token at once. | [Planned] |
| API-F3 | **Discovery and version.** `GET /api` answers, to an authenticated client, the Teca version, the API versions served (`["v1"]`), the collection open (name, mode, read-only state and cause, `MDI-F201`), and the OpenAPI document's path. Every other route is under `/api/v1/`. A route of a version not served gets `404` with `{"error":"unsupported_version"}`. | [Planned] |
| API-F4 | **Collection and devices.** `GET /api/v1/collection` — what `GET /api` says plus the counts (devices, catalogs, files). `GET /api/v1/devices` — the device tree as *My drives* shows it (`SpecSelection.md`): id, name, type, parent, path, connected state (`MDI-F209` applied), active state, dates, totals. `GET /api/v1/devices/{id}` — one device. | [Planned] |
| API-F5 | **Browsing.** `GET /api/v1/catalogs/{id}/folders?path=` — the sub-folders and files of a folder, as Browse shows them (`SpecExplore.md`): name, size, dates, type, and for each file whether derived data exists. Paged (`API-F9`). Memory mode loads the catalog's files first, as the window does (`core` rule). | [Planned] |
| API-F6 | **Search.** `POST /api/v1/search` with a body naming the mode — `name` (the Search page's text criteria: text, type, size, dates, device scope, `SpecSearchList.md`), `meaning` (`MDI-F47`), `information` (`MDI-F183`) — and the device scope. Answers the result list as the Results page shows it (`SpecSearchResults.md`), paged. A search through the API is **not** added to the collection's search history (as a read-only search, `MDI-F200`, `Q74` (d)). Long searches run on the search job of `core` with the same cancellation; a client disconnect cancels it. | [Planned] |
| API-F7 | **Derived data and previews.** `GET /api/v1/files/derived?catalog=&path=` — the file's derived data as *Dettagli del file* shows it (`MDI-F44`, `MDI-F116`): analyses, outcome, values (caption, tags, summary, transcript passages and clusters), provenance. `GET /api/v1/files/preview?catalog=&path=&kind=` — the derived file itself (thumbnail, poster, contact sheet, WebP) with its MIME type, found as the window finds it (`MDI-F207`, `MDI-F208`). Face crops are never served (`MDI-C51`); people and faces are served only when the window would show them (`MDI-F213`, `MDI-F214`: locked layers → absent). Notes are never served (`MDI-F215` applies to what leaves the machine). | [Planned] |
| API-F8 | **Jobs.** `GET /api/v1/jobs` — the enrichment and catalog jobs running or pending, as *Lavori in corso* shows them (`MDI-F61`–`F69`): kind, catalog, progress, state. Read-only in version 1: no start, pause or stop through the API (`API-F10`). | [Planned] |
| API-F9 | **Paging, limits, errors.** List routes take `limit` (default 200, max 2000) and `cursor`, and answer `next_cursor`. Request bodies above 1 MiB and more than 20 concurrent requests per token get `413` / `429`. Errors are JSON `{"error": <code>, "message": <text>}`; the message is English, not translated — the API is for programs (`MDI-C17` does not apply to it). Every route answers in under 100 ms except search, browsing of a Memory-mode catalog and previews. | [Planned] |
| API-F10 | **Writes come later.** Version 1 serves no route that changes the collection, its settings, its jobs or its files: every `POST`/`PUT`/`DELETE` other than `/search` gets `405`. Writes (tags, information, job control) are a version 2 decided with the owner, bounded by the writers list (`MDI-F195`) and the read-only state (`MDI-F199`). | [Planned — v2] |
| API-F11 | **Headless serving.** `teca --serve [--port N] [--scope local\|lan] [--collection <path>]` serves the API without a window, with the collection and token of the settings file, until stopped; it takes the collection lock like the window (`MDI-F192`, `MDI-C62`) and runs no AI job unless the P5 hub rules say so (D45). It writes no setting (`CLI-C4`) and exits with `CLI-F8`'s codes. Meant for the hub of D45 and for a NAS or a Mac mini that serves readers. | [Planned] |
| API-F12 | **The contract.** An OpenAPI 3.1 document describes every route, parameter and schema of version 1; it is served at `GET /api/v1/openapi.json` and committed in the repository (`docs_src/static/api/v1/openapi.json`). The document is the contract: a change that breaks a client is a new version, not an edit. | [Planned] |

## Constructional requirements — *how it is built / limits / MUST-NOTs*

| ID | Requirement | Status |
|----|-------------|--------|
| API-C1 | **Where it lives.** The server is a new K3-side module, `qt_quick/api/` (or the upstream slot `web/server/`, the owner's choice at design listing), compiled only with `-DBUILD_API=ON`; it links `katalog-core` and is linked by the K3 executable. It contains **no SQL** and **no domain logic**: every answer is built from `core` methods that exist or are added under `MDI-C14` (listed before they are written). It never includes a Qt Widgets or Qt Quick header. K2 is not changed (`MDI-C1`). | [Planned] |
| API-C2 | **Transport library.** The owner chose `Qt6::HttpServer` (`QHttpServer`, Qt 6.5+; `Qt6::WebSockets` is not used), an exception to `MDI-C8` approved on 2026-10-31. **Finding (2026-10-31):** the Homebrew `qt` formula on the owner's Mac does not ship `Qt6HttpServer`; the CI Linux and macOS packages build Qt with other installers that do. Before the design listing: install the module locally (a separate formula or a Qt build) or, failing that, serve HTTP/1.1 on `QTcpServer` (`Qt6::Network`, already linked) with a minimal, tested request parser — one implementation, chosen once, recorded here. | [Planned] |
| API-C3 | **Same thread rules as the window.** Requests are served on the main thread for the SQLite connections' sake, as `AppManager` serves QML; searches and previews run on worker threads through the `core` jobs and the derived-data store's own connections (D17). A request MUST NOT run a `QSqlQuery` directly. While a search or an Explore load runs for the window, API searches queue (as `checkPublishedCopy` waits, `MDI-F206`). | [Planned] |
| API-C4 | **Read-only and privacy.** The API applies the collection's read-only state from `core` (`MDI-C64`) and never writes in version 1 (`API-F10`). It binds to loopback unless the user chose the private network; it MUST NOT bind to `0.0.0.0`/`::` without that choice, MUST NOT register with any discovery or relay service, and MUST NOT make an outbound connection of its own (`MDI-C11`). Request logs, when on, hold method, route, status and duration — never the token, never file contents. | [Planned] |
| API-C5 | **Versioning and compatibility.** The route prefix carries the major version; additive changes (new fields, new routes) stay in `v1`; a removed or renamed field or a changed meaning is `v2`. The server may serve several versions at once. `openapi.json` is generated from the same route table the server uses, so they cannot diverge. | [Planned] |
| API-C6 | **Licence boundary, stated.** `docs_src/docs/development/SpecTecaApi.md` (this page) and a `docs_src/docs/Api.md` user page state that the API is the supported integration surface and that clients are separate works. Teca's own code, the API server included, stays GPL; every source file of the fork carries a GPL header with the fork owner's copyright line (`Q77` (a)); the package carries the GPL text, Katalog's attribution and the source offer (`Q75` (k), `MDI-C73`). | [Planned] |
| API-C7 | **Bounds.** No new K3 page: the API section is a section of `PageSettings.qml` (`MDI-C18`). New strings (Settings section, confirmation, status) in English, approved together before the commit, in all languages (D16). New settings keys only add (`MDI-C7`): `Api/Enabled`, `Api/Port`, `Api/Scope`, `Api/Token` (until the keychain). Memory mode serves as File mode (`MDI-C5`). Hosted mode serves what the window shows (`MDI-C48` until D45's companion exists). | [Planned] |

## Open questions

| ID | Question | When |
|----|----------|------|
| Q77 | (a) The copyright line for the fork's files: the owner's legal name, or a project name ("Teca contributors")? Twenty-nine QML files of the fork have no licence header today. (b) `Qt6::HttpServer` locally: install, or the `QTcpServer` fallback (`API-C2`)? (c) Where the module lives: `qt_quick/api/` or upstream's `web/server/` slot (`API-C1`)? (d) Should `teca --serve` be the P5 hub's way of serving readers, replacing the "take the published AI data" step of D45 for clients that are not Teca? (e) The default port `48700`: registered nowhere; fine for a local API — confirm. | (a) before the licence-header commit; (b), (c) at the design listing; (d) when P5 rows are written; (e) at the design listing |

## Manual test charter

- **API-F1 / API-C4** — Switch off: nothing listens on the port (`lsof -i :48700` empty). Switch on, *This computer only*: listens on `127.0.0.1` and `::1` only; a request from another computer on the LAN is refused at the socket. *My private network*: the confirmation dialog; *No* keeps loopback; *Yes* listens on every address.
- **API-F2** — A request without `Authorization` gets `401` with an empty body; with the token, `200`. *Regenerate*: the old token gets `401` at once.
- **API-F3 / API-F12** — `GET /api` lists `["v1"]` and the OpenAPI path; `GET /api/v1/openapi.json` validates as OpenAPI 3.1; `GET /api/v2/collection` gets `404 unsupported_version`.
- **API-F4 / API-F5** — The device tree matches *My drives*; browsing a Memory-mode catalog returns its files (the load happened); a folder with 5000 files pages at 200 with `next_cursor`.
- **API-F6** — A name search returns the same count as the Search page for the same criteria; the collection's search history is unchanged afterwards; closing the client mid-search stops the job (*Lavori in corso* shows it cancelled).
- **API-F7** — A thumbnail is served with `image/webp`; a face crop path gets `404`; with the layers locked, `people` is absent from the derived data; Notes are never present.
- **API-F10** — `DELETE /api/v1/devices/1` gets `405`; nothing changed.
- **API-F11** — `teca --serve` on a closed window serves the API and holds the lock: opening the collection in the window shows the read-only banner (`MDI-F193`); stopping the process releases it; the settings file is byte-identical afterwards.
- **API-C4** — With the API on for an hour, the outbound connections of the Teca process are none (`lsof -i -P | grep Teca`, only the listening sockets).

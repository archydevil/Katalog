---
id: SpecMediaIntelligenceBacklogNotes
title: Media Intelligence — Proposals Not Yet Decided
description: Designs written for the fork owner's decision, kept out of the requirement tables of SpecMediaIntelligence until decided
---

# MEDIA INTELLIGENCE — PROPOSALS NOT YET DECIDED

![Status](https://img.shields.io/badge/Status-Proposal-orange) ![Fork](https://img.shields.io/badge/Fork-archydevil%2FKatalog-blue)

Proposals for `SpecMediaIntelligence.md`, written before the fork owner decides
them. **Nothing here is a requirement.** No row of this page authorises code:
the specifications gate returns **NOT IN SPEC** for any change built on a
proposal below until the owner decides it, and the decision is then recorded as
a `D*` row of `SpecMediaIntelligence.md` with permanent `MDI-*` IDs. The IDs
here (`P5-*`) are provisional and are retired when that happens.

---

## P5 — AI data in Hosted mode (proposed 2026-10-31) {#p5}

The design for D43 (7), the *second road*: a Hosted collection with AI data and
read-only readers. `MDI-C68` requires it to be designed and listed before P5
starts; `MDI-F211`, `MDI-F212` and the *Sharing P5* phasing row are the open
slots it fills. Pointed to from `Q76`. Its security part extends
`SpecHostedSecurity.md`, whose own rows are not changed here.

### Why this shape

1. **Two findings.** `Database::validateHostname()` refuses `100.64.0.0/10`,
   the range Tailscale assigns, and refuses every name (`*.ts.net` included):
   the owner's own setup of D43 cannot reach a hosted server today. The Hosted
   password is kept in clear in the settings file and read back to pre-fill the
   form.
2. **The stores stay SQLite.** The derived data, the instant index (FTS5), the
   embeddings read whole for a search, the people and the faces are SQLite
   stores. Porting them to MySQL/MariaDB and PostgreSQL means two more dialects
   to write and keep, slower searches over the network, and a server that holds
   people and faces in clear (`MDI-C51`, `Q65`). `MDI-F211` says "stored and
   read as in File mode": this design reads it literally.
3. **Reuse what P2a builds.** The AI data reaches the other computers through
   the publishing of `MDI-F202`–`F207` — snapshot, marker, republishing, reload
   — which this design reuses as specified there (P2a is `[Planned]`; code
   exists but is not yet recorded as built, a DRIFT noted in `Q76`).

### Summary (the decision the owner is asked to take)

| # | Proposal |
| --- | --- |
| 1 | **Companion folder.** On each computer, a Hosted collection has a *companion folder* for what File mode keeps beside the collection: `mdi_data.db`, `teca_history.db`, the lock, the `katalog_mdi` fallback. Default under the application's data folder, changeable in Settings, never on the server. |
| 2 | **Nothing new on the server** except what File mode already keeps in the collection database: `mdi_profile`, `mdi_schema` (a MySQL and a PostgreSQL DDL each) and `parameter` rows (writers, hub). |
| 3 | **One hub.** Among the authorised Macs, one is the hub: only it runs AI jobs on the collection and publishes its companion's AI data. Other computers take the hub's published AI folder, read-only, and read the catalogs live from the server. |
| 4 | **Read-only database users** open the collection read-only and are told why; grants are read, never probed by a write. |
| 5 | **Reaching the server safely** (`SpecHostedSecurity.md` extension): tailnet and IPv6 private ranges count as private network; names classified by what they resolve to; TLS off localhost; the password out of the settings file on macOS. |
| 6 | **Later, not now:** several hubs, writes from readers ("Poi allargheremo"), AI tables on the server, a reader build for Windows or Linux. |

### Operational — *why / for whom*

| ID | Proposal |
| --- | --- |
| P5-O1 | A user keeps the catalogs of an archive on a database server of their own network and still has its AI results, previews, people and faces on every computer that opens it: the catalogs live from the server, the AI data from the hub's published folder. The authorised computers with a writing database account change the catalogs; the hub alone changes the AI data; a computer with a read-only account changes nothing. |

### Functional — *what the system does*

| ID | Proposal | Amends / touches |
| --- | --- | --- |
| P5-F1 | **The companion folder.** A Hosted collection opened on a computer gets a companion folder on it, by default under the application's data folder, named after the server and database, without asking. Settings shows it and offers *Change…*; the previous folder is left as it is. It holds `mdi_data.db` (D17), `teca_history.db` (`MDI-C45`), the lock (`MDI-C62`) and the `katalog_mdi` fallback of `MDI-F207`. The features that say they are unavailable in Hosted mode then work. The default-without-asking is a choice for the owner: `Q76` (f). | `MDI-C6`, `MDI-C48`, D17, `MDI-C45`, `MDI-F136`, `MDI-F164`, `MDI-F183`, `MDI-F184` |
| P5-F2 | **The hub.** In Settings, on an authorised Mac, one computer of the authorised list (`MDI-F195`, `MDI-F196`) is named the hub; naming another replaces it after a confirmation (`MDI-C17`), and is refused while the current hub runs a job (`Q76` (g)). Only the hub runs AI jobs on the collection — analysis, re-interpretation, *faces*, dating, document study — and only the hub publishes. Elsewhere those actions are disabled and say which computer is the hub. A Hosted collection with the protection off has no hub: AI jobs run on any computer into its own companion, unshared, and nothing is published (today's behaviour made explicit; `Q76` (h)). | `MDI-F195`–`F197` |
| P5-F3 | **Publishing the AI data.** On the hub, *Publish for the read-only Macs* (`MDI-F202`) and the automatic republishing (`MDI-F205`) write the companion's `mdi_data.db` (cleaned as `MDI-F215`), the derived files (`MDI-F203`) and the marker — no collection database, since the collection is on the server. The marker records the mode `Hosted`, the server's host and database name, and lists no collection database. The protected layers travel as in File mode (`MDI-F213`, `MDI-F214`). | `MDI-F202`–`F205` |
| P5-F4 | **Taking the published AI data.** On a computer that is not the hub, Settings offers to take a folder — the synced copy of the hub's published folder — as this collection's *published AI data* on this computer. Teca then reads `mdi_data.db` and the derived files from it, read-only and immutable (`MDI-F204`, `MDI-C64`); the collection is read live from the server; this computer's own companion keeps its `teca_history.db` and lock. A newer snapshot is reloaded as `MDI-F206`; an incomplete one is never opened. Such a computer runs no AI job and writes nothing in that folder. | `MDI-F204`, `MDI-F206`, `MDI-C64` |
| P5-F5 | **A read-only database user.** When the user may not write the collection's tables, Teca opens the collection read-only (`MDI-F199`) with the banner (`MDI-F201`) saying that the database user may only read. Nothing is created or altered on the server at opening — no table, index or migration (`MDI-C64`); a collection the server does not hold yet is refused with the same reason. The writers list applies on top: a read-only user on an authorised Mac still reads only. Fills `MDI-F212`. | `MDI-F212`, `MDI-F199`, `MDI-F201` |
| P5-F6 | **Writers on the server.** A writing database user on an authorised Mac (or on any Mac while the protection is off) changes the catalogs as today. No lock is taken on the server: the server's transactions settle concurrent writes by two authorised Macs, and the lock of `MDI-C62` lives in the companion and protects this computer's companion and its AI jobs only. This reverses, for Hosted mode only, the single-writer rule of D43 (1): `Q74` (i), `Q76` (i). | `MDI-C62`, `MDI-F192`–`F194` |
| P5-F7 | **A partial read-only state.** On a computer with published AI data (P5-F4), the AI data is read-only while the catalogs may be writable; on a read-only user (P5-F5) everything is. `MDI-F199`/`MDI-C64` define one state for the whole collection: P5 amends them with a *read-only AI data* state — the AI actions of `MDI-F199` disabled, the catalog actions governed by the database user and the writers list — and the banner (`MDI-F201`) names the cause among: another Teca (`MDI-F193`), this Mac not authorised (`MDI-F197`), a read-only database user (P5-F5), published AI data (P5-F4). | `MDI-F199`, `MDI-F201`, `MDI-C64` |
| P5-F8 | **Settings.** The Hosted form shows, for the open collection: the server (host, TLS state, user, whether it may write — P5-H1…H5), the companion folder with *Change…*, the hub with *Make this computer the hub*, *Take the published AI data…*, and on the hub the publish folder of `MDI-F202`. | `SpecSettingsPage.md` |
| P5-F9 | **Command line.** The AI analysis and the publish of `MDI-F210` run on a Hosted collection only on its hub, with the companion folder the window uses — the default of P5-F1 when none is remembered, so no settings key is written (`CLI-C4`); elsewhere they exit with `CLI-F8`'s error code and say which computer is the hub. The password move of P5-H4 and the companion default are never written by a command-line run (`CLI-C4`). | `MDI-F210`, `CLI-C4`, `CLI-C1` |

### Constructional — *how it is built / limits*

| ID | Proposal | Amends / touches |
| --- | --- | --- |
| P5-C1 | **The companion is the collection folder of a Hosted collection.** In `core/`, a Hosted collection's folder (today empty) is its companion on this computer, so every row that writes or reads "beside the collection" — the derived-data store (D17), the catalog history (`MDI-C45`), the lock (`MDI-C62`), the derived-root fallback (`MDI-F207`) — works unchanged. The folder is remembered in the application's settings per collection and per computer (`MDI-C66`), never on the server. No core schema change (`MDI-C15`). | `MDI-C66`, `MDI-C15` |
| P5-C2 | **What the server holds.** The core tables as today, `mdi_profile` and `mdi_schema` (already the collection database's in File mode, `MDI-C15`) with a MySQL/MariaDB and a PostgreSQL DDL each, listed before they are written, and the `parameter` rows of the writers (`MDI-C63`) and of the hub (one row: machine id and name; type name listed first). Nothing else: no derived datum, face, person, index or embedding table on the server. Amends `MDI-C6` (the runner's own connection is to the companion; the server connection serves the catalogs), `MDI-C48` and D17 (the stores exist in Hosted mode, in the companion), and answers `Q11`: one destination path per catalog, recorded as today, remapped per computer by `MDI-F208`. | `MDI-C6`, `MDI-C48`, D17, `Q11` |
| P5-C3 | **Privileges read, never probed.** Whether the user may write is read from the server's catalog — MySQL/MariaDB: `SHOW GRANTS FOR CURRENT_USER()`; PostgreSQL: `has_table_privilege()` on the `device` table — before any table or index is created. A grant that cannot be read counts as read-only (`Q76` (j)). The state is set in `core/`, so the command line applies it alike (`CLI-C1`). | `MDI-C64`, `CLI-C1` |
| P5-C4 | **The published AI data is a published copy.** The publishing of `MDI-F202` gains a Hosted source: no database file, the companion as the collection folder. The marker, its completeness check and the reload of `MDI-F206` are the ones P2a specifies, unchanged in format: a Hosted marker simply lists no collection database. A published AI folder is opened read-only and immutable as `MDI-F204`/`MDI-C64`. No new core file; new methods are listed before they are written (`MDI-C14`). | `MDI-F202`, `MDI-F204`, `MDI-F206`, `MDI-C14`, `MDI-C65` |
| P5-C5 | **Bounds.** K2 is not changed (`MDI-C1`): K2 opens the same Hosted collection and ignores the companion, the hub and the `mdi_` rows, as in File mode — but see P5-H6 on `validateHostname()`. No new library, Qt or KF6 module (`MDI-C8`): `QMYSQL` and `QPSQL` as today; TLS through Qt SQL's connect options; the keychain through the framework of D44 (P5-H7). No new K3 file (`MDI-C18`). New settings keys only add (`MDI-C7`): an existing `katalog3_prerelease_settings.ini` opens its Hosted collection as before and gets the default companion. Memory mode untouched (`MDI-C5`). Messages follow `MDI-C17`. New strings: English, approved together before the commit (fork rule), then **all languages under D16** — the Italian-only exception of `MDI-C39` covers the redesign's strings, not these, unless the owner extends it (`Q76` (k)). | `MDI-C1`, `MDI-C5`, `MDI-C7`, `MDI-C8`, `MDI-C17`, `MDI-C18`, `MDI-C39`, D16 |

### Extension of `SpecHostedSecurity.md` (proposed)

`HST-F1`–`HST-F6` and `HST-C1`–`HST-C5` are upstream's and are not changed by
this page. The rows below would be added there when decided; each names the
upstream row it amends.

| ID | Proposal | Amends |
| --- | --- | --- |
| P5-H1 | **Tailnet and IPv6 private ranges.** `100.64.0.0/10` (CGNAT, the range Tailscale assigns) and Tailscale's `fd7a:115c:a1e0::/48` are classified as *private network*: the `HST-F3` confirmation, with its three strings, continues on *Yes*. Whether all of `fc00::/7` (IPv6 unique local) and `fe80::/10` (link-local) join them: `Q76` (c). The literal range list of `HST-F4`'s message then no longer matches the rule: `HST-C3` is amended (one string changed, per-string approval) or the list is left as is and the new ranges are documented only — the owner's choice. | `HST-F3`, `HST-F4`, `HST-C3`; the *Not in scope* line of `SpecHostedSecurity.md` ("any change to the classification ranges") |
| P5-H2 | **A name is classified by what it resolves to.** A host that is not an address is resolved first (short timeout) and classified as the strictest class among its addresses — one public address makes it public and refused (`HST-F4`), all private makes it private (`HST-F3`), all loopback makes it localhost (`HST-F2`); a name that does not resolve is refused as today. The dialog of `HST-F3` shows the name and the address. The lookup happens before the confirmation and sends only the name to the configured resolver: stated under `HST-O1`/`MDI-C11`. Whether names are accepted at all: `Q76` (c); if not, this row is dropped. | `HST-F2`–`F4`, `HST-O1`, `MDI-C11` |
| P5-H3 | **TLS off localhost.** For a private-network host the connection requires TLS: MySQL/MariaDB through the `QMYSQL` connect options (CA, certificate and key files set per collection when the server needs them; the required-mode option if the Qt build exposes it — checked first, `Q76` (d)), PostgreSQL through `sslmode=require` (`verify-full` with a CA file). A server without TLS is refused with the reason and nothing is written (`HST-C2`). Localhost connects as today. A per-collection *Allow a connection without TLS* switch, off by default, asking for confirmation: `Q76` (d). | `HST-C5` (new keys), `HST-C3` (new strings) |
| P5-H4 | **The password leaves the settings file.** On macOS the Hosted password is kept in the login keychain as a generic password keyed by host, port, database and user, and `Settings/databasePassword` is written empty; a password found there is moved to the keychain once, by the window only (`CLI-C4`), after the owner accepts the rewrite (`Q76` (e)). The form pre-fills from the keychain. On Windows and Linux the password stays in the settings file and the form says so. No password is ever logged or shown. This widens the Security-framework exception of D44 from the reader key (`MDI-C70`) to the Hosted password: an amendment the owner must approve. | `MDI-C70`, `MDI-C8`, `HST-C5` |
| P5-H5 | **Read-only users.** P5-F5 and P5-C3, as rows of `SpecHostedSecurity.md`. | `HST-O1` |
| P5-H6 | **Where the classification changes — and K2.** P5-H1 and P5-H2 change `Database::validateHostname()` in `core/`, which `HST-C1` froze and which **K2 calls too** (`qt_widgets/mainwindow_tab_settings.cpp`): either the new classes are gated so K2 keeps `HST-F2`–`F4` as `[Implemented]` (a parameter of the function, K3 passing the new rule), or K2 changes too — an `MDI-C1` amendment. The owner chooses: `Q76` (b). Host resolution uses `Qt6::Network`, already linked (`MDI-C8`). The backstop `HST-C4` keeps applying. | `HST-C1`, `HST-C4`, `MDI-C1`, `MDI-C8`, `MDI-C14` |
| P5-H7 | **No new library.** TLS only through `QSqlDatabase::setConnectOptions`; the keychain through the macOS framework D44 approved (its generic-password method listed before it is written, `MDI-C14`), with the "not available" behaviour on other systems. | `MDI-C8`, `MDI-C70` |

### Manual checks the design would need

- P5-H1 — host `100.101.102.103`: the `HST-F3` dialog; *No* writes nothing; *Yes* connects. Host `8.8.8.8`: refused as before. K2: unchanged or changed according to `Q76` (b).
- P5-H2 — a name resolving to `100.x`: dialog with name and address; a name resolving to a public address: refused inline; an unresolvable name: refused.
- P5-H3 — MariaDB without TLS on `192.168.1.10`: refused, nothing written; with TLS: connects; `localhost` without TLS: connects.
- P5-H4 — macOS: after *Connect* the file's password key is empty and the keychain holds the item; restart pre-fills the form.
- P5-F5 — a `SELECT`-only user: read-only banner, no table created on the server, write actions disabled (`MDI-F199`), search and Browse work (`MDI-F200`).
- P5-F1/F2/F3/F4 — hub publishes; a second computer takes the published folder, sees the AI data, reloads on a new snapshot (`MDI-F206`), cannot run a job; its catalogs stay live from the server.
- P5-F6 — two authorised Macs with writing users edit different devices at once: both changes persist.

### Open points for the owner (mirrored as `Q76`)

| | Point | Proposed answer |
| --- | --- | --- |
| (a) | One road or two: the AI data on readers lags behind the hub by the publish cadence (`MDI-F205`), as on road 1. Accept, or want live AI data (then the tables go on the server — the cost above)? | Accept the lag; not in P5. |
| (b) | K2 and `validateHostname()` (P5-H6): gate the new classes to K3, or change K2 too? | Gate to K3; `MDI-C1` kept. |
| (c) | Ranges and names (P5-H1, H2): Tailscale's ranges only, or all of `fc00::/7` and `fe80::/10`? Names accepted by resolution, or addresses only? And the `HST-F4` range-list string: changed or left? | Tailscale's ranges and `fc00::/7`; names by resolution; the string changed (one approval). |
| (d) | TLS inside a tailnet (P5-H3): WireGuard already encrypts; require TLS anyway (defence in depth, a LAN server is not in a tunnel), or a per-collection switch with a warning? Whether `QMYSQL` on Qt 6.12 exposes a required-mode option is checked before writing. | Require TLS; no switch in P5. |
| (e) | Password (P5-H4): keychain on macOS only, settings file elsewhere; the one-time rewrite of the user's settings file at the first opening. | Accept; the rewrite only when the window saves the form, never silently. |
| (f) | Companion folder (P5-F1): a silent default under the application's data folder, or a prompt at the first opening? | Silent default, changeable in Settings. |
| (g) | Naming another hub while the current one runs a job. | Refused, with the reason. |
| (h) | Protection off: no hub, nothing published — publishing tied to the writers switch. | Accept: publishing presumes an owner. |
| (i) | No lock on the server (P5-F6): accept concurrent authorised writers in Hosted mode, reversing D43 (1) there? | Accept: the server is built for it. |
| (j) | A grant that cannot be read counts as read-only (P5-C3). | Accept. |
| (k) | New strings: all languages (D16), or the Italian-only exception extended to P5? | D16, all languages. |
| (l) | DRIFT: P2a (`MDI-F202`–`F207`) and the keychain file (`MDI-C70`) are `[Planned]` while their code exists; this design reuses them. Record them as built first. | Record before P5 starts. |

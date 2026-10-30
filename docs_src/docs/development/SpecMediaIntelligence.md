---
id: SpecMediaIntelligence
title: "Media Intelligence: Previews, AI Enrichment and Semantic Search"
description: "Requirements for the fork-only K3 enrichment of catalogs: previews, transcripts, AI summaries, provenance and semantic search; the fork's K3 redesign (D34); catalog history, navigation and accessibility (D36, D37)"
version: "3.1"
---

# MEDIA INTELLIGENCE — PREVIEWS, AI ENRICHMENT AND SEMANTIC SEARCH

![Status](https://img.shields.io/badge/Status-Draft-orange) ![Version](https://img.shields.io/badge/Version-fork%203.1-blue) ![Implementation](https://img.shields.io/badge/Implementation-partial%20(phases%20A%20to%20D)-yellow) ![Scope](https://img.shields.io/badge/Scope-fork%20only%20(K3)-purple)

## Context

A catalog today records *that* a file exists (name, size, dates, type, optional
technical metadata). It does not record *what the file contains*. A user holding
many offline drives cannot see an image, hear a recording, read the gist of a
PDF, or jump to the moment a topic is discussed in a two-hour video without
mounting the drive and opening the file.

This spec adds **enrichment**: after a catalog is created, a background runner
derives previews (thumbnails, frames, low-resolution clips, waveforms) and
content data (captions, tags, summaries, excerpts, transcripts, topic segments,
topic clusters, embeddings) for each file. Derived files are written to a
**destination path** chosen by the user — never to the cataloged device — and
derived data is stored with full provenance in its own SQLite file in the
collection folder, `mdi_data.db` (D17). The result is browsable when the device is disconnected and searchable by meaning
(semantic search).

> **Reading the requirement IDs.** Each requirement has a permanent ID (`MDI-`).
> IDs are never renumbered or reused; a retired requirement is marked
> `[Removed]`, not deleted. Status is one of
> `[Implemented] / [Planned] / [Backlog] / [Removed]`. A suffix names the
> phase or increment that implemented the row (`[Implemented — Phase A]`).
> `[Planned — part implemented in Phase A]` means only the part named in
> [Implementation status](#implementation-status) is built; the rest is planned.

### Vocabulary {#vocabulary}

| Term | Meaning |
| ------ | --------- |
| **Enrichment** | Producing derived data for the files of a catalog, after and separately from catalog creation or update. |
| **Analysis** | One kind of enrichment: thumbnail, poster frame, keyframe strip, video clip, audio clip, waveform, transcript, segmentation, clustering, image caption and tags, document summary and excerpts, classification (D33), embeddings, application information. |
| **Derived datum** | One result of one analysis for one file (or one segment / cluster), with its provenance record. |
| **Derived file** | A derived datum that is a file (image, clip, waveform) written under the destination path. |
| **Destination path** | The folder, chosen per catalog, that holds the derived files. It is never on the cataloged source. |
| **Enrichment profile** | The per-catalog choice of file types, preview options, analyses and provider, saved so later runs reuse it. |
| **Enrichment job** | One run of the enrichment runner over one catalog with its profile. |
| **Provider** | The service that runs a model: **local** (Ollama on this machine), **local-network** (Ollama on a private-network host), or **remote** (a commercial API). |
| **Context** | The user's description of the domain a collection belongs to — organisation, period, subject, narrative style, glossary, categories, known entities — that the AI analyses use (D33, `MDI-F94`). |
| **Catalog notes** | A catalog's own additions to the collection's context (`MDI-F95`). |
| **Knowledge base** | The user's reference documents for a collection, indexed on this computer as text passages with embeddings, from which analyses draw (`MDI-F104`). |
| **Classification** | The AI analysis that describes a file in the context's terms and assigns values of the context's categories (`MDI-F100`). |
| **Design system** | The K3 redesign's shared look — design tokens (colours, radii, spacing) defined once and the components built on them: card, info tag, sidebar item, section header, metric card, progress bar (D34, `MDI-C37`). |
| **Info tag** | A short text on a rounded, coloured background that states a state or a kind, in one of six tones (`MDI-F114`). |
| **Sidebar** | The fixed list of places at the left of the K3 window that replaces the side menu (`MDI-F111`). |
| **Teca** | K3's visible name in this fork. Files, settings and identifiers keep the name Katalog (`MDI-F123`, `MDI-C41`). |
| **TecaStyle** | The fork's Qt Quick Controls style. It draws the native controls in the design system and is derived from Fusion (`MDI-C42`). |
| **Restore point** | A copy of a catalog's rows — device, catalog, files, folders, enrichment profile — recorded in `teca_history.db` before an action changes the catalog (D36, `MDI-F136`). |
| **History** (*Storico*) | The list of recorded actions with their restore points, and the K3 page that shows it (D36, `MDI-F138`). |
| **Recompose** (*Ricomponi*) | Putting the catalogs of a split back into one catalog (D36, `MDI-F142`). |
| **Scope chip** | A short statement, on a page that acts on the choice in *I miei dischi*, of what that choice is, with *Cambia* (D37, `MDI-F147`). |

---

## Authority and decisions {#decisions}

The user approved the draft as a whole and the answers recorded as D5–D14 by
replying "ok" on **2026-10-28**. The reply was given in the main session; the
main agent reported it to the specifications agent, which did not see the reply
itself. D1–D4 are confirmed by that same reply. These decisions are the
authority for every row below; a change to any of them needs a new user decision
and an edit to this section first.

The version label "fork 3.1" in the badge is the fork's release label. It is
**not** a schema number: the core schema stays 3.0 (D5).

| # | Decision (2026-10-28, confirmed by the user) |
| --- | ------------------------ |
| D1 | **Scope.** A fork-only feature set (`archydevil/Katalog`, branch `work/dev-fork`), not proposed upstream. The K2-parity prerequisite of `CLAUDE.md` ("new major UI features in K3 only after the full K2 feature set has been migrated") does **not** block it in this fork. K3 (`qt_quick`) only; K2 (`qt_widgets`) is untouched. |
| D2 | **Execution model.** A separate enrichment runner, **not** inside catalog creation or update. Catalog creation stays as it is; enrichment runs afterwards, in the background, on its own `QThread` (precedent: the backup sequencer, `SpecBackup.md` `BKP-C11`), per file, stoppable, pausable, resumable after restart (pattern of `metadata_extraction_date`, `SpecCatalogMetadata.md`), incremental (only new or changed files). Searches and Explore keep working while it runs. |
| D3 | **Privacy.** Local by default (Ollama). Commercial providers (OpenAI-compatible API, Anthropic API) can be enabled explicitly per collection or per job, with an explicit warning and a cost estimate shown before anything is sent. Nothing leaves the machine unless enabled. API key storage is decided in D10. |
| D4 | **Tools.** `ffmpeg` / `ffprobe`, `whisper.cpp`, and poppler (`pdftotext` / `pdftoppm`) are approved as **external programs** invoked through `QProcess`, never linked. Their paths are detected and configurable; a missing tool skips the related analysis with a clear status, never a crash. Viable on Windows, Linux and macOS. LLM and embedding calls go over HTTP with `Qt6::Network` (already linked). No vector-index library: embeddings are stored in the database and similarity is computed in `core/`. Distribution of the tools: D9. |
| D5 | **Schema (`Q12`, `Q13`).** The core schema stays at **3.0**; this fork never bumps the shared schema version and never stamps 3.1. Every new table has the prefix `mdi_` and the set carries its own version ("media intelligence schema version 1"), stored separately from the core schema version (e.g. a single-row `mdi_schema` table). A separate idempotent migration creates or upgrades the `mdi_` tables when they are absent or older. No 3.0 table or column is altered. K2 and upstream K3 3.0 therefore open such a collection normally and ignore the `mdi_` tables. Memory mode: new files in the collection folder with an `mdi_` prefix; no existing file format changes. |
| D6 | **Resume after restart (`Q2`).** A job that uses only local tools and the local provider resumes automatically when the collection is opened again. Any other job — one that uses a commercial or remote provider (and, by the first sentence, a local-network one) — waits for Resume. |
| D7 | **Playback (`Q5`).** Phase A has no in-app playback. *Open at timecode* opens the clip with the system default application. Qt Multimedia is not added (may be revisited later, with its own approval). |
| D8 | **Existing catalogs (`Q9`).** Enrichment can be started on an existing catalog (created in standard mode or before this feature) from the catalog's page and from Explore; the catalog's enrichment profile is edited there after creation. |
| D9 | **Tool distribution (`Q15`).** In this fork the external tools and the whisper model are installed by the user (e.g. Homebrew); Katalog does not bundle them. Bundling and its licence review are deferred. |
| D10 | **API keys (`Q1`).** An API key is never written in plain text in any `.ini`. It is held for the session only, or read from an environment variable (options b + c). Applies from Phase B. |
| D11 | **Clustering scope (`Q3`).** Topic clusters are computed per catalog. |
| D12 | **Output language (`Q4`).** Generated text is in the source's language, with an option to use the UI language instead. |
| D13 | **Re-run history (`Q7`).** On re-run, the replaced data is discarded and only its provenance record is kept. |
| D14 | **Prices (`Q10`).** Provider prices are entered by the user only; no price list is shipped. |

D15–D18 were decided by the fork owner later on **2026-10-28**, in the main
session, and reported to the specifications agent by the main agent (the agent
did not see the replies itself).

| # | Decision (2026-10-28, fork owner) |
| --- | ------------------------ |
| D15 | **One-page wizard.** The wizard mode of the Create page is one page, with no steps: the standard form's *Catalog definition* section, then the enrichment options (destination path, file types as toggle buttons, previews as switches, an *AI analysis* section). It supersedes the stepped wizard of `MDI-F2` (now `[Removed]`, replaced by `MDI-F60`). Reason: the user disliked a multi-step wizard whose steps produced nothing visible. |
| D16 | **String process in the fork.** New strings are written in English while a step is built, reusing existing K2/K3 strings where they fit; the fork owner approves all of a step's new strings together at the end of the step, before its commit; translations run after that approval. Existing strings are still never changed without per-string approval (`CLAUDE.md` FORK block, `MDI-C17`). |
| D17 | **Derived-data store (`Q6`).** Derived data lives in its own SQLite file, `mdi_data.db`, in the collection folder, for File and Memory modes; WAL journal mode and a busy timeout. The runner writes it on its own thread and connection; the UI reads it through a separate main-thread connection. The enrichment profile stays in the collection database (Memory mode: `mdi_profile.csv`). Hosted mode is still open (`Q11`). |
| D18 | **Work split.** The core part (`core/mdidata.*`, `core/mdienrichmentjob.*`) is implemented by another session; the K3 part (`qt_quick/`) by the main session. `core/mdistore.*` is an earlier, unused draft that is not compiled; deleting it is the fork owner's decision. |

D19–D21 were decided by the fork owner on **2026-10-28**, in the main session,
answering explicit questions, and reported to the specifications agent by the
main agent (the agent did not see the replies itself).

| # | Decision (2026-10-28, fork owner) |
| --- | ------------------------ |
| D19 | **Activity page.** A new K3 page, *Activity*, in the side menu, is approved. It amends `MDI-C18` ("No new Page") for this page only. Purpose: say clearly, at every moment, what is running, where (local / local network / remote), with which resources and for what. Inspired by the fork owner's own omegamind Control Center. Content: a status line; resource tiles; a "right now" card; a tools card; the enrichment queue; a log of finished steps (`MDI-F61`–`F69`). The bottom activity panel stays, as a compact summary that opens the page. |
| D20 | **Redesign scope.** Redesigned now: the Activity page (D19); the Create page, as a single page of cards — Source, Destination, What to make, AI — replacing the step wizard; Explore, with thumbnail tiles and *Analyze* at hand. Search, Devices and Settings are unchanged for now. The Create and Explore redesigns have no requirement rows yet (`Q18`). |
| D21 | **Resource history.** CPU, GPU and memory history charts cover the current session only; nothing is persisted. |

D22–D30 were decided by the fork owner on **2026-10-28 and 2026-10-29**, in the
main session, answering explicit question prompts and approval requests, and
reported to the specifications agent by the main agent (the agent did not see
the replies itself).

| # | Decision (2026-10-28/29, fork owner) |
| --- | ------------------------ |
| D22 | **Instant search index.** The owner chose to keep D5: the index is a SQLite FTS5 trigram index in `mdi_data.db` (tables `mdi_fts`, `mdi_fts_doc`, `mdi_fts_state`, `core/mdifts.*`), never in the core schema. It holds file name, folder and derived text (snippet, summary, caption, tags, archive entry names), is rebuilt per catalog in a background thread after the collection is opened and after each creation or update, and is updated per file by the runner. K3 text search uses it as a prefilter in `SearchJobStoppable::searchFilesInCatalog`; K2 never enables it. No SQLite triggers (`MDI-F76`, `F77`, `MDI-C22`, `C23`). |
| D23 | **Thumbnails and video previews.** Thumbnails are WebP **files** under the destination path, not database BLOBs: 256 px, quality 75; images decoded in-process by Qt in a parallel pre-pass; `ffmpeg` only for formats Qt cannot read; JPEG when Qt has no WebP writer. The video keyframe strip is **one contact sheet** (frames side by side, default 4, 144 px high). A *Technical data* analysis reads codec, resolution, frame rate, duration, bit rate and audio with `ffprobe`. Supersedes `MDI-F29` and `MDI-F31` (`MDI-F78`–`F80`, `MDI-C24`). |
| D24 | **Text snippet.** The first 800 characters of text, cleaned, for a fixed list of text, PDF and Office/OpenDocument extensions, whatever file types the profile selects; binary files skipped (`MDI-F81`). |
| D25 | **Archive listing.** KArchive (`KF6::Archive`) may be linked to core, optionally (`KATALOG_HAS_KARCHIVE`); it amends `MDI-C8` for this library only. Archive entries are listed and indexed; showing archives as folders in the Explore tree is backlog (`MDI-F82`, `F83`, `MDI-C25`). |
| D26 | **Quick hash.** `FileChecksum::computeQuickHash` (BLAKE2b-256 over size and three 64 KB samples) is computed by the runner after each creation or update, for every catalog whose source is reachable, with or without a profile, and stored in `mdi_data.db` only: no core column (`CKS-C4`, D5), scan unchanged, no "Quick" catalog checksum option (K2 and upstream would read an unknown value as SHA-256). The duplicates search uses it as its checksum key, with an optional *Verify with full checksum*; compare-two-devices stays on SHA-256 (backlog) (`MDI-F84`–`F86`, `MDI-C26`). |
| D27 | **Offline Inspector.** A K3 side panel (owner's choice over a K2 Widgets version) in Explore and Search results, new file `qt_quick/Inspector.qml` (a component, not a Page); visibility remembered (`MDI-F87`). |
| D28 | **Explore column view.** A Finder-style column view in K3 Explore, new file `qt_quick/ColumnBrowser.qml` (a component, not a Page); choice remembered (`MDI-F88`). |
| D29 | **Simplified side menu** (owner's request). Main entries Selection, Search, Explore, Create, Activity, Settings; a *Tools* submenu with Devices, Tags, Statistics, Backup, About; *Documentation* and *Quit* removed from the drawer (Quit stays as the standard shortcut); footer pins as icon buttons with tooltips (`MDI-F89`). |
| D30 | **Connected drives** (owner's request, NTFS). `core/mdivolumes.*` lists external drives, mounted or not, detects third-party file-system drivers (Paragon, Tuxera, macFUSE) and mounts a drive read-only on request; the Create page shows the list. Katalog never reads raw devices (that needs administrator rights): reading a drive the system cannot mount is out of scope. Context: the owner's WD 2 TB NTFS drive failed to mount because Paragon's kext was not loaded; with Paragon removed, macOS mounts it read-only and it is readable (`MDI-F90`, `MDI-C27`). |

D31 was requested by the fork owner on **2026-10-29** in the main session and
its details approved on **2026-10-29**, answering explicit question prompts
(a job paused at quit stays paused; a job waiting for its drive lets a queued
catalog with a connected drive go first; opening another collection stops the
job and keeps it pending; the four technical rules of `MDI-F11`, `F91` and
`MDI-C29`: "Sì, tutte"). Follow-up replies the same day, after the first
implementation: the destination drive is waited for too ("Aspetta anche quel
disco"), and checking the queued catalogs' drives on the UI thread is accepted
("Va bene così"). Reported to the specifications agent by the main agent (the
agent did not see the replies itself).

| # | Decision (2026-10-29, fork owner) |
| --- | ------------------------ |
| D31 | **Resume after unplugging the drive** (owner's request, 2026-10-29: "se metto in pausa l'analisi ai dei contenuti, e stacco il disco, posso riprendere in un secondo momento dal punto in cui ho interrotto? Se no, inseriamo questa opzione"). A job whose source drive is disconnected, paused or not, waits for the drive and continues with the same file when it is back, instead of skipping the remaining files; Pause reads nothing from the source; the running job and the waiting list are kept in `mdi_data.db` across restarts and collection switches; a job paused when Katalog closed comes back paused; a job waiting for its drive lets a queued catalog with a reachable source run first. Follow-up, same day: the drive of the destination path is waited for like the source drive, and a pending job leaves the list only when its catalog no longer exists; checking the drives of queued catalogs on the UI thread is accepted, with the risk of network volumes (`MDI-F11`, `F12`, `F16`, `F91`–`F93`, `MDI-C15`, `C28`, `C29`). |

D32 was decided by the fork owner on **2026-10-29** in the main session,
answering an explicit question prompt after a real run on catalog 2 (71 `.doc`
files without text), together with "correggi" for the two fixes recorded under
*Fixes after D31*. Reported to the specifications agent by the main agent (the
agent did not see the replies itself).

| # | Decision (2026-10-29, fork owner) |
| --- | ------------------------ |
| D32 | **`textutil` on macOS** ("Per leggere i 71 vecchi documenti Word (.doc), Katalog può usare «textutil», il programma di macOS già presente sul Mac. Non va installato nulla, lavora offline e legge soltanto, senza modificare i file. Lo approvi?" → "Sì, usa textutil (Consigliato)"; "Solo su Mac. Su Linux resta catdoc, come oggi."). On macOS, the system program `/usr/bin/textutil` reads the text of Word 97–2004 `.doc` files when KFileMetaData returns none (its extractor needs `catdoc`, which Homebrew does not provide). It adds this program to the external tools of D4, on macOS only; nothing is installed, nothing is written, nothing leaves the machine. Linux keeps `catdoc` through KFileMetaData (`MDI-C8`, `MDI-F81`). |

D33 was requested by the fork owner on **2026-10-29** in the main session; its
four choices were answered the same day through explicit question prompts, each
with the recommended option. Reported to the specifications agent by the main
agent (the agent did not see the replies itself).

**Ratified 2026-10-29.** Follow-up replies the same day, through explicit
question prompts, relayed by the main agent (the agent did not see the replies
itself):

- `Q19` "Dove vuoi l'editor del Contesto …?" → "Pagina nel menu Strumenti
  (Consigliato)" — "Una pagina "Contesto" dentro il sottomenu Strumenti: il menu
  principale resta con le sue 6 voci."
- `Q21` "Il pulsante "Rielabora con il contesto" … cosa deve rifare?" → "Solo
  la classificazione (Consigliato)".
- `Q22` "Quando attivi la classificazione su un catalogo già analizzato …?" →
  "Solo file nuovi; gli altri con Rielabora (Consigliato)".
- Ratification of the drafted rows: "Dettagli proposti: (1) le aggiunte del
  singolo catalogo sono solo note …; (2) la lingua del contesto vale per tutti i
  testi dell'AI; (3) la rielaborazione interrotta riprende dopo il riavvio, come
  l'analisi; (4) nei documenti di riferimento sì a PDF, Word, Excel/CSV e testo,
  no agli archivi zip; (5) i termini del glossario passati alla trascrizione
  come suggerimento sono un'opzione separata e spenta …; (6) senza contesto la
  classificazione resta spenta, e usa i 5 passaggi più pertinenti dei documenti.
  Li approvi?" → "Sì, tutti (Consigliato)". This answers `Q20`, `Q23`, `Q24`,
  `Q27` and `Q28`, and ratifies the D33 rows as drafted, with these answers
  applied.

`Q25` and `Q26` stay open (not blocking); `Q29`–`Q31` belong to context
phase 2. The core class and method design is the separate approval of
`MDI-C14` (see *Context phase 1 — proposed core design*).

| # | Decision (2026-10-29, fork owner) |
| --- | ------------------------ |
| D33 | **Domain context for interpretation** (owner's request, 2026-10-29: "creiamo un tool che permetta di inserire un contesto specifico su cui incentrare la classificazione dei file, immagini, video ecc. Ad esempio i dati che stiamo caricando e classificando appertengono a UITS e sono un archivio dal 1981 di dati. Dammi la possibilità di caricare archivi di dati e informazioni e di attingere a mcp, rag e kb locale o online e su cloud per arricchire e perfezionare le interpretazioni, e usare un linguaggio e una narrativa e terminologia specializzata e verticale."). Answers: (1) scope «Collezione + aggiunte» — one context for the whole collection (e.g. UITS), and each catalog can add its own notes; (2) re-interpretation «Solo su richiesta» — new files use the context at once; files already analysed are re-interpreted only through an explicit «Rielabora con il contesto» action, with a time estimate, limitable to a folder or a file type; a change of context never re-runs analyses by itself; (3) sources «Prima locale, poi MCP» — phase 1: context brief, glossary, categories, known-entity lists, and documents added and indexed on this computer (local knowledge base); phase 2: an MCP client (local and remote servers) and online or cloud sources, each enabled explicitly with a warning; (4) categories «Categorie fisse + tag liberi» — the AI assigns values of the owner's categories (e.g. Disciplina, Evento, Anno, Sezione TSN, Persone, Tipo di documento) using the glossary's terms, plus free tags. Follow-up, same day: the context editor is a page *Context* in the *Tools* submenu, the six main entries unchanged; re-interpretation redoes the classification only; *Analyze* classifies new or changed files only, the others through re-interpretation; catalog additions are notes only; the context's language applies to all AI-generated text; an interrupted re-interpretation resumes after a restart; reference documents are PDF, Word, Excel/CSV and text, not zip archives; glossary terms reach the transcription only through a separate option, off by default; without a context the classification is off, and it uses the 5 most relevant passages (`MDI-O8`, `MDI-F89`, `MDI-F94`–`F110`, `MDI-C18`, `MDI-C30`–`C36`). |

D34 was requested by the fork owner on **2026-10-29** in the main session; its
choices were answered the same day through explicit question prompts, a
prototype was shown, and the owner then replied «procedi», approving the
implementation. Reported to the specifications agent by the main agent (the
agent did not see the replies itself). The text after each "—" in the answers
below is the option's description in the question prompt, not the owner's
words.

**Rows drafted, then settled by the owner's answers (2026-10-29).** The rows
written from D34 (`MDI-O9`, `MDI-F111`–`F115`, `MDI-C37`–`C39`, and the
amendments to `MDI-C18`, `MDI-F89` and `MDI-F107`) were drafted by the
specifications agent while phase R1 was already being coded and committed
(see *Phase R1 (D34) — code committed before the rows were ratified*). The
main agent told the owner so, and asked him to settle the open points and to
confirm the choices made in code. His answers, below, are relayed by the main
agent (the agent did not see the replies itself). The rows were then amended
to them. The owner approved the **choices**; he was not shown the rows' text.
If he wants to review that wording, it is in the *K3 redesign* section.

- `Q32` "\"Cambiamo tutto\": la nuova interfaccia riguarda solo Katalog 3, o
  anche Katalog 2 …?" → "Solo Katalog 3 (Consigliato)".
- `Q33` "Le altre 28 lingue mostreranno in inglese i testi nuovi finché non li
  traduciamo. Va bene, e posso aggiornare CLAUDE.md … con questa eccezione e
  con quella \"i nomi di K3 non seguono più K2\"?" → "Sì, e aggiorna CLAUDE.md
  (Consigliato)". The main agent updated `CLAUDE.md`. It added a fork
  exception (D34) to "K3 labels must stay in sync with K2". It added an
  exception to "Unchanged in the fork: 30 languages" (the redesign's strings
  are in Italian only for now). It added a FORK bullet stating that the K3
  redesign is authorised phase by phase by this spec, and that the portage and
  verbatim-string rules apply to behaviour, not to the redesign's names.
- `Q34` + `Q35` "Aspetto del design system: dimensioni del testo e colori." →
  "Fisse come nel prototipo": fixed text sizes and a fixed Katalog palette,
  independent of the *Text size* and *Theme* settings. Light and dark follow
  the system's colour scheme, as in the prototype. This is an owner-decided
  exception to `SpecTypography.md` and `SpecTheme.md` for the redesign
  (`MDI-C37`).
- `Q36` + `Q37` + `Q38` "Approvi queste scelte fatte nel codice? (1) menu della
  collezione sul nome in alto; (2) \"Aggiungi un disco\" in Archivio; (3)
  \"Informazioni\" e \"Gestisci i dischi\" (la vecchia pagina Dispositivi,
  finché non si unisce a I miei dischi) in Altro; (4) la colonna dei dischi
  accanto alle pagine resta, con il suo interruttore; la barra laterale è
  sempre visibile e il fermo \"barra fissata/mobile\" sparisce; (5) la Home
  mostra numeri chiave, card dei dischi, lavoro in corso e conoscenza; (6)
  Katalog si apre sempre sulla Home; (7) il pannello in basso resta e si chiama
  \"Lavori in corso\"." → "Approvo tutto (Consigliato)".
- Strings: the 45 new strings of phase R1, with their Italian, were approved
  verbatim before the Italian was filled in ("Approvo tutti"). The list is
  under *User-visible text*.
- `KButton.qml`: the main agent asks for it to be added to `MDI-C37` as a
  component of the same family. It is listed there. No owner answer naming
  this file is reported, and the FORK block requires one for any file the
  spec did not list, so it awaited that confirmation (`Q43`; covered by the
  blanket instruction of 2026-10-30).
- `Q39` and `Q40` were not put to the owner. The main agent proposes keeping
  the redesign in this spec and updating the user docs at the end of the
  redesign (R8). Both stay open.

**Blanket authorisation, 2026-10-30.** The fork owner's instruction in the main
session, relayed by the main agent (the agent did not see it itself): "falli
tutti, committa, sybca e pusha senza chiedermelo. Fai tutto fino alla fine
aggiornando tutto con il nuovo stile e ux". The main agent's gloss: do all the
phases; commit, sync and push without asking; update everything to the new
style and UX. What it is recorded as authorising:

- **Phases R2–R8.** The rows `MDI-F116`–`F122` and `MDI-C40` were written by
  the specifications agent on 2026-10-30 from the main agent's phase plan.
  The owner authorised the phases as a whole; he did not see these rows.
  They are bounded to presentation (`MDI-C40`).
- **New strings.** The new strings of R2–R8 (English source and Italian, as
  `MDI-C39` states) are approved by this instruction. There is no
  end-of-step approval for them; this amends D16 for these strings.
- **Commits and pushes** without further questions. That is a process
  decision for the main agent and authorises no requirement.
- **`KButton.qml`** (`Q43`) is covered, on the main agent's reading.

What it does **not** change:

- A behaviour that no row authorises stays NOT IN SPEC and needs its own row
  (`Q45`).
- No existing source string is edited (`MDI-C39`).
- The constraints of this spec and the bounds of `MDI-C38` and `MDI-C40` hold.
- `Q41`, `Q42`, `Q44` and `Q46` are left open as trade-offs for the owner;
  none blocks a phase.

`CLAUDE.md` lines 52 ("Never add, change, or delete any user-visible string
… explicit per-string approval") and 95 (the fork's end-of-step string
approval) do not yet carry this exception.

**Name, controls style and *My drives* (2026-10-29/30).** The fork owner's
decisions, made in the main session and relayed by the main agent with the
request to record them. The agent did not see the replies itself. The code was
committed before the rows were written (`2cdcd326` to `cba65b5b`).

- **Visible name *Teca*** ("Nome visibile", 2026-10-29). K3 shows the name
  *Teca*. Files, settings, the collection format and identifiers keep the name
  Katalog, and About credits Katalog. Existing strings that named the
  application now say *Teca*; strings that name Katalog's own things keep the
  name (`MDI-F123`, `MDI-C41`, *User-visible text*).
- **TecaStyle** (2026-10-30). K3's Qt Quick Controls style is the fork's own
  `TecaStyle`, on every platform, Linux included. For the fork's K3 it
  supersedes the style named by earlier specs (Fusion, Breeze,
  `org.kde.desktop`). Dialogs and the page header bar take the design system's
  colours, and Home shows no Close action (`MDI-C42`, `MDI-F124`).
- ***My drives* header and drive cards** (R7, 2026-10-30): a description line,
  the reminder as an accent-soft row, a visible *Show device info* switch,
  framed collapse and expand buttons, and the label "Used Space: " on the
  used-space tag (`MDI-F125`).
- **Phases R2–R8** reported implemented, with the Italian of every D34 string
  (2026-10-30). The Italian of "AI analysis" was aligned to *Analisi AI*. This
  changes the translation only.

**Browse never empty (D34 follow-up, 2026-10-30).** On 2026-10-30 the fork
owner asked that every feature be easy, explained and simple to use ("a prova
di deficiente"). Offered four options, he chose *Sfoglia mai vuota*, worded to
him as: "Se nessun disco è scelto, Sfoglia mostra i dischi da aprire invece di
'Nessuna cartella selezionata'; i file con risultati AI hanno un'etichetta 'AI'
e una riga spiega che cliccandoli si vedono anteprima, descrizione e
trascrizione." The answer is relayed by the main agent; the agent did not see
it itself. Rows `MDI-F129`–`F131` and `MDI-C44`. This is new behaviour, not a
presentation phase, so `MDI-C44` bounds it in place of `MDI-C40`. The other
three options were not chosen now (`Q48`).

**The owner's answer to `Q51` (2026-10-30).** He was asked: "In 'Sfoglia
l'archivio' ho aggiunto tre cose oltre a quanto descritto nell'opzione che hai
scelto. Quali tengo? (sono già attive; quelle non scelte le tolgo)". The
options were "Scegli un altro disco", "Cartella = suo catalogo" and "'Aggiungi
un disco' se vuoto". He answered: "la più semplice da capire. Metti infotag
comunque. E quando seleziono ina foto da esplora, non si deve aprire la foto in
anteprima, ma solo l'anteprima thumb." The answer is relayed by the main agent;
the agent did not see it itself.

- **Reading of the main session, open for the owner to correct.** All three
  behaviours are kept, since each makes Browse easier to understand, and info
  tags are added: a folder that is a catalog of its own carries the tag
  "Catalog" (`MDI-F132`–`F134`). "La più semplice" is singular, so the answer
  could also name a single option. The owner may still say which one.
- **The last sentence is a request of its own:** a click on a file selects it
  and shows its thumbnail, without opening the file (`MDI-F135`).
- **Strings:** the owner approved the step's six new strings with "Approvo
  tutte" (*User-visible text*).

| # | Decision (2026-10-29, fork owner) |
| --- | ------------------------ |
| D34 | **New design system and new K3 interface** (owner's request, 2026-10-29: "adesso cambiamo completamente il design system e generiamo una interfaccia completamente nuova e con una nomenclatura parlante. con card, infotags, descrizioni e una navigabilità moderna e semplice."). Answers: (1) "Che stile vuoi per il nuovo design system?" → "Control Center (Consigliato)" — card su fondo morbido, angoli arrotondati, un colore d'accento, infotag colorati per stato e tipo; segue chiaro/scuro del Mac; (2) "Come vuoi muoverti nell'app?" → "Barra laterale + Home (Consigliato)" — una barra fissa a sinistra con poche voci raggruppate e descritte; una Home con card dei dischi, lavori in corso e scorciatoie; (3) "Nomenclatura parlante … Vale solo per Katalog 3 (Katalog 2 resta com'è)?" → free answer: "tutto nuovo e con traduzione solo in ita e inglese per ora. cambiamo tutto"; (4) "Come procediamo?" → "Prima un prototipo da vedere (Consigliato)"; (5) design system of the prototype → "Uno nuovo per Katalog (Consigliato)". Prototype: <https://claude.ai/artifact/JVQYDfH6v7wgLsv96WGQpY> (Home, Design system, Sfoglia l'archivio, Cerca nei dischi, Conoscenza dell'archivio, Lavori in corso), with the names of the table *Nomenclature (D34)* and the sidebar groups *Archivio*, *Intelligenza*, *Altro*. After the prototype: «procedi». Effects: the sidebar replaces the side menu of D29 (`MDI-F89`) and the *Tools* place of the *Context* page (`Q19`, `MDI-F107`) once phase R1 is built; D20's "Search, Devices and Settings are unchanged for now" ends with phases R3, R7 and R8; new strings of the redesign are in English and Italian only "per ora" (`MDI-C39`). "Cambiamo tutto" is the whole K3 interface; K2 stays unchanged ("Solo Katalog 3", `Q32`; D1, `MDI-C1`). Text sizes and colours are fixed, as in the prototype, independent of the *Text size* and *Theme* settings (`Q34`, `Q35`). The navigation and Home choices made in code were approved ("Approvo tutto", `Q36`–`Q38`). Phases R1–R8 (*Phasing*). R1 rows: `MDI-O9`, `MDI-F111`–`F115`, `MDI-C18`, `MDI-C37`–`C39`. R2–R8 rows: `MDI-F116`–`F122`, `MDI-C40`. Name, style and *My drives*: `MDI-F123`–`F125`, `MDI-C41`, `MDI-C42`. Follow-up *Browse never empty* (2026-10-30): `MDI-F129`–`F135`, `MDI-C44`. |

D35 was decided by the fork owner on **2026-10-30** in the main session. He was
answering the main agent's question after the AI data of his split catalogs
was found invisible. The reply is relayed by the main agent; the agent did not
see it itself. "Sì, correggi" is also the `MDI-C14` approval of the core change
of `MDI-C43`, as `CLAUDE.md` requires for `core/`.

| # | Decision (2026-10-30, fork owner) |
| --- | ------------------------ |
| D35 | **AI data follows a split catalog** ("Sì, correggi"). When a catalog is split by sub-folder or by file type (`SpecDevicesSplit.md`), the AI data of its files moves to the new catalog that now holds each file, matched by full path. Rows whose file is in none of the new catalogs stay as they are. A row that would collide with an existing row of the new catalog is left in place. Nothing is deleted, and derived files on disk do not move. The old catalog's enrichment profile is copied to each new catalog. The new catalogs' instant search index is built as for any new catalog. Hosted collections have no `mdi_data.db`, so no derived data moves there. Cause: the split gives the files new catalog IDs and removes the old catalog's device. The AI data is keyed by catalog ID and full path, the profile by device ID, and neither was moved. The owner's previews, descriptions, transcripts and classifications therefore vanished from Browse, Search and Home (*Split carry-over (D35)*) (`MDI-F126`–`F128`, `MDI-C43`). |

D36 and D37 come from one request of the fork owner on **2026-10-30**, in the
main session: "cercami bugs e cose complicate da fare e capire. Voglio che
tutte le funzionalità siano facilmente accessibili e gestibili. Voglio accedere
ai cataloghi, a vecchie versioni del catalogo, a versioni del catalogo non
diviso e voglio poter ricomporre un catalogo diviso. Voglio poter andare
indietro nello storico delle azioni e poter navigare "esplora" in maniera
totale, avanti e indietro con breadcrumb. Poi fai un controllo secondo le best
practices della teoria della navigabilità e accesso massimo e perfeziona la UX
e UI rendendole al 100% accessibili e comprensibili."

**D36 — the owner's answers (2026-10-30).** The main session put four
questions to him. His answers are relayed by the main agent; the agent did not
see them itself. The text after each "—" is the main session's description of
the option, not the owner's words.

- "Versioni dei cataloghi e storico delle azioni: come li costruisco?" →
  "Punti di ripristino (Consigliato)" — before every operation that changes a
  catalog (update, split, recompose, delete, edit) Teca saves the catalog's
  file and folder lists in a separate file `teca_history.db` in the collection
  folder (the main database schema does not change); a "Storico" page lists
  each action with date and description, "Ripristina com'era prima" and "Apri
  questa versione come catalogo a parte"; right after an action also
  "Annulla"; AI results re-link by file path.
- "Quante versioni conservo per ogni catalogo?" → "Ultime 10 (Consigliato)" —
  older ones pruned automatically; changeable in Settings.
- Core changes → "Approvo tutte e tre" — (1) new core classes for restore
  points and history (new files in `core/`); (2) "Ricomponi": merge split
  catalogs back into one with files, folders, AI results and enrichment
  profile; (3) fix of the split bug that creates catalogs with NULL
  `catalog_date_updated`, so `MdiFts` considers them stale and rebuilds their
  instant index at every launch (observed by the main agent: `mdi_data.db`
  +80 MB in a day). This answer is the `MDI-C14` approval of the three
  changes. The class and method design is the main agent's (`MDI-C46`,
  *Catalog history (D36) — approved core design*).
- "Il tuo disco UITS è diviso in 22 cataloghi … li riunisco?" → "Sì,
  ricomponi (Consigliato)" — recompose the owner's 22 split catalogs into one
  "CATALOGO BASE UITS DISCO RIGIDO", with a restore point first.

The first answer names the page *Storico*, so it approves the page file
`qt_quick/PageHistory.qml` (the fourth Page exception of `MDI-C18`).

**D37 — navigation and accessibility (2026-10-30).** The rest of the same
request. No question was put to the owner on it. The design is the main
session's, relayed by the main agent, with WCAG 2.2 level AA as the bar. The
specifications agent wrote `MDI-O11`, `MDI-O12`, `MDI-F144`–`F156` and
`MDI-C47` from it; the owner did not see them. Three points change an earlier
owner choice or an upstream row and are flagged for him:

- Removing the legacy page-header Close actions (`MDI-F146`) was option (c)
  of `Q48`, which he left for later on 2026-10-30. The main session reads the
  new request as covering it (`Q55`).
- `Esc` = Back changes `KBS-F1` and `KBS-C1` (*Specs to amend*).
- Text that follows the *Text size* setting (`MDI-F154`) reverses `Q34`
  ("Fisse come nel prototipo"). It is held until the owner confirms it
  (`Q46`).

The findings of the main session's UX audit are kept as open questions, not
approved (`Q48`, `Q56`–`Q64`).

**D36 and D37 — the owner's answers of 2026-10-30, second round.** Three
questions, put by the main session with AskUserQuestion once the step was
built. The answers are relayed by the main agent, with his labels verbatim;
the specifications agent did not see them itself.

- **Text size.** "il 29/10 avevi scelto dimensioni 'fisse come nel prototipo'
  … conviene che seguano l'impostazione 'Dimensione del testo' dell'app …
  Cosa preferisci?" → "Seguano l'impostazione (Consigliato)". This reverses
  `Q34` for text size: `MDI-F154` is approved, and `MDI-F115` and `MDI-C37`
  are amended. Colours stay fixed (`Q35`). Raising the slider's range beyond
  1.2 (`TYP-F9`) was not asked and stays open (`Q46`).
- **Navigation.** "accanto al titolo di ogni pagina ci sono ‹ Indietro e ›
  Avanti, anche dentro Sfoglia cartella per cartella. Esc, Cmd+[ / Cmd+] e i
  tasti del mouse fanno lo stesso. Ho tolto i vecchi pulsanti 'Chiudi' dalle
  pagine raggiungibili dalla barra laterale; restano solo sulle sotto-pagine e
  sulle finestre sovrapposte. Confermi?" → "Confermo (Consigliato)".
  `MDI-F144`–`F146` are approved, the mouse's Back and Forward buttons with
  them. The `KBS-F1` / `KBS-C1` amendment stands as listed (*Specs to amend*).
  The Close removal of `Q48` (c) and `Q55` is confirmed.
- **Strings.** "Approvo tutte", for the step's new strings (about 65:
  navigation, the scope chip, the History page, the history and recompose
  messages). Italian is applied (`MDI-C39`). The root link of Browse's
  breadcrumb is the existing string "All the drives" (*Tutti i dischi*). It
  replaces "Choose another drive", which is no longer used, so `MDI-F132` is
  superseded by `MDI-F145`. That replacement is the main session's; the
  owner's answer approved the strings that carry it.

`MDI-F147`, `MDI-F148` and the accessibility rows other than `MDI-F154` were
not put to the owner.

| # | Decision (2026-10-30, fork owner) |
| --- | ------------------------ |
| D36 | **Catalog history, restore points and recomposition** (owner's request and answers above). Before each action that changes a catalog — update, split, recompose, delete, edit — Teca records a restore point of the catalogs concerned in `teca_history.db`, a separate file in the collection folder; the collection database and its schema do not change (D5). A page *Storico* lists the actions with date and description; each can be restored ("Ripristina com'era prima") or opened as a separate catalog ("Apri questa versione come catalogo a parte"); right after an action, "Annulla" undoes it. AI results follow by file path (`MdiData::moveToCatalogs`, D35). The last 10 restore points per catalog are kept, older ones pruned automatically, the number changeable in Settings. *Ricomponi* merges split catalogs back into one, with files, folders, AI results and profile, after a restore point. A split gives its catalogs a valid update date. Approved core changes (`MDI-C14`): new history classes in new core files, the recomposition, the split date fix. The owner's 22 UITS catalogs are to be recomposed into "CATALOGO BASE UITS DISCO RIGIDO" (`MDI-O10`, `MDI-F136`–`F143`, `MDI-C45`, `MDI-C46`). |
| D37 | **Navigation and accessibility** (owner's request above; the main session's design, not put to the owner). K3 only, no core change. Navigation: Back and Forward over pages and Browse locations; a clickable breadcrumb with Up in Browse; the sidebar mark follows the page shown; no legacy Close on pages reached from the sidebar (`Esc` = Back); a scope chip with *Cambia* on the pages that act on the choice in *I miei dischi*; `Cmd/Ctrl+1…9` and `F6`. Accessibility, WCAG 2.2 AA as the bar: non-text contrast of controls, visible focus, states not by colour alone, keyboard use of *I miei dischi*, accessible names, roles and values, a text alternative for the Inspector image, text that scales (held, `Q46`), limited motion, dimmed disabled buttons, an activity panel that covers nothing (`MDI-O11`, `MDI-O12`, `MDI-F144`–`F156`, `MDI-C47`). **Confirmed by the owner, 2026-10-30 (second round above):** the navigation of `MDI-F144`–`F146`, and text that follows *Text size* (`MDI-F154`). |

**Parity fix, 2026-10-30 (no decision needed).** Browse's *Move file to
Trash* / *Move folder to Trash* and Search results' *Move to Trash* acted at
once, with no confirmation. K2 confirms both. That broke the `CLAUDE.md` rule
that every destructive action has a confirmation dialog matching K2. The fix
reuses K2's strings verbatim (`MDI-F157`).

---

## Scope at a glance

**In scope:** a simplified wizard **mode** of the existing K3 Create page; the
enrichment runner and its lifecycle (start, pause, stop, resume, incremental
re-run); external-tool detection and invocation; provider settings (local
Ollama, optional commercial APIs) with the egress warning and cost estimate;
previews of images, video and audio; video/audio transcripts, topic segments and
topic clusters; image captions and tags; PDF summaries and key excerpts; browsing
all of this in Explore and Search results while the device is disconnected; a
semantic search mode; storage of every derived datum with provenance in new
`mdi_` tables that carry their own version, outside the core schema numbering
(the core schema stays 3.0, D5), kept in their own file `mdi_data.db` in File
and Memory modes (D17), Hosted mode still open (`Q11`); starting
enrichment on existing catalogs (D8); the interaction with the catalog's
*Include metadata* level; an *Activity* page with a core resource monitor and
per-step activity reporting from the runner (D19, D21); the card layout of the
Create page and the thumbnail tiles of Explore (D20, rows pending, `Q18`); an
instant search index for text search (D22); WebP thumbnails, video contact
sheets and technical data (D23); text snippets (D24); archive listings (D25);
quick hashes and their use by the duplicates search (D26); the offline
Inspector and the Explore column view (D27, D28); the simplified side menu
(D29); the list of connected drives with read-only mounting (D30); a domain
context per collection with catalog notes, a classification analysis that uses
it, the context in the other AI analyses of new work, a local knowledge base of
reference documents, re-interpretation on request, and the *Context* page in
the *Tools* submenu (D33, context phase 1); the K3 redesign, phase R1 (D34): a
design system, a fixed sidebar in place of the side menu, a *Home* page, and
speaking names on sidebar entries and page titles, with new strings in English
and Italian only for now; when a catalog is split, the carry-over of its AI
data, its enrichment profile and its instant search index to the new catalogs
(D35); catalog history with restore points in `teca_history.db`, restoring,
opening an earlier version as a separate catalog, and recomposing split
catalogs (D36); navigation with Back, Forward, a breadcrumb in Browse and a
scope chip, and accessibility with WCAG 2.2 AA as the bar (D37).

**Later phase (in scope, `[Backlog]`):** text and Office documents beyond PDF;
characterising information for applications and other files; MCP servers and
online or cloud sources for the context (D33, context phase 2).

**Later phases of the redesign (in scope):** phases R2–R8 of D34 (*Phasing*),
authorised by the owner on 2026-10-30 and bounded to presentation (`MDI-F116`–`F122`,
`MDI-C40`), reported implemented on 2026-10-30. Also in scope: the visible
name *Teca* and the `TecaStyle` controls style (`MDI-F123`–`F125`,
`MDI-C41`, `MDI-C42`), and the D34 follow-up *Browse never empty*
(`MDI-F129`–`F135`, `MDI-C44`).

**Out of scope (non-goals):**

- Any change to K2.
- Any change to what catalog creation or device update do (`MDI-C2`).
- Writing, modifying or deleting anything on the cataloged source (`MDI-C10`).
- Remote speech-to-text: transcription is local only (`MDI-C12`).
- A vector-index or keychain library, Qt Multimedia, or any new linked library
  (`MDI-C8`); each needs its own approval. In-app playback (D7).

- Any change to the core schema 3.0, its tables, its columns or its version
  number (`MDI-C15`, D5).

- Bundling external tools or models in builds (D9, deferred).
- Proposing any of this upstream (D1).
- Reading raw devices, or any drive the system cannot mount (D30, `MDI-C27`).

**Applies to:** K3 (`qt_quick`) and the shared `core/` of this fork only.

---

## Privacy and data-egress stance {#privacy}

1. **Default is offline.** A fresh installation contacts only `localhost`
   (`MDI-C11`). No analysis sends anything anywhere until a provider is set up.
2. **Classification by host, not by product name.** An endpoint is *local* when
   its host is loopback, *local-network* when it is a private-network address,
   *remote* otherwise — the same classification as `SpecHostedSecurity.md`
   (`HST-F2`–`HST-F4`), applied to provider endpoints (`MDI-F26`). An Ollama
   endpoint on a public host is remote.
3. **Remote needs two gestures.** The provider must be enabled for the
   collection or for the job (`MDI-F23`), **and** each job that would send
   content must be confirmed in a dialog stating what is sent, to whom, how much,
   and at what estimated cost (`MDI-F24`). Declining sends nothing.
4. **Minimum content leaves.** Remote analyses receive derived inputs only
   (thumbnails, extracted text, transcript text, sampled frames), never the
   original file (`MDI-C12`).
5. **Every remote call is recorded** with provider, endpoint host, model, date
   and cost in the datum's provenance (`MDI-F52`).
6. **Keys.** An API key is held for the session only or read from an
   environment variable, and never written to any file (D10, `MDI-C13`).
7. **Context and knowledge base.** The context, catalog notes and
   knowledge-base passages go only to the collection's own providers, under the
   rules above (`MDI-F108`). MCP servers and online sources (D33, phase 2) are
   not built; each will need explicit enabling and the same confirmation
   (`MDI-F109`, `MDI-F110`, `MDI-C35`).

---

## Operational requirements — *why / for whom*

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-O1 | A user recognises the content of files on a drive that is not connected — sees an image, a video's frames, hears a clip, reads a summary or a transcript — without mounting the drive. | [Planned — part implemented] |
| MDI-O2 | A user finds files by what they are about, with a question in ordinary language, not only by name or metadata. | [Planned — part implemented] |
| MDI-O3 | A user navigates a long recording by topic: they see where each topic starts and ends and what it covers, and which recordings share a topic. | [Planned — part implemented] |
| MDI-O4 | A user keeps control of their data: by default nothing leaves the machine, and sending content to a commercial service is a deliberate, informed choice made with its cost in view. | [Planned] |
| MDI-O5 | A user can trust and redo a derived result: they can tell what produced it, when, where and at what cost, and can produce it again. | [Planned] |
| MDI-O6 | A user keeps working in Katalog (searching, exploring) while enrichment runs, and can pause it, stop it and resume it later — including after closing Katalog — without redoing finished work. | [Planned — part implemented] |
| MDI-O7 | A user sets up the cataloging of a drive together with its previews and analyses in one guided pass on the screen they already use to create catalogs. | [Implemented — Phase A] |
| MDI-O8 | A user tells Katalog what an archive is — its organisation, period, subject, vocabulary, categories, people and events, and the reference documents that explain it — and the interpretations Katalog makes of its files use that domain's terms, categories and narrative instead of generic descriptions (D33). | [Planned — part implemented in Context 1] |
| MDI-O9 | A user finds their way in Katalog by what each place is for: every place has a name that says what it does and a one-line description, the first screen shows the state of the whole archive — its drives, the work in progress, its knowledge — and states and kinds are recognisable at a glance by coloured tags, in a look that follows the system's light or dark mode (D34). | [Planned — part implemented in R1] |
| MDI-O10 | A user sees what was done to each catalog and when, puts a catalog back as it was before an action, opens an earlier version — the one from before a split included — beside the current one, and puts a split catalog back together, without losing the AI results of its files (D36). | [Planned — part implemented in D36] |
| MDI-O11 | A user always knows where they are and which drive or folder a page acts on, and moves back and forward between pages and folders, and up a folder path, with the mouse or the keyboard (D37). | [Planned — part implemented in D37] |
| MDI-O12 | A user who works with the keyboard alone, with a screen reader, with larger text or with low vision reaches, perceives and operates every K3 feature. WCAG 2.2 level AA is the bar (D37). | [Planned — part implemented in D37] |

## Functional requirements — *what the system does*

### Wizard mode of the Create page {#wizard}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F1 | The K3 Create page offers two modes: the existing form (**standard** mode) and a **wizard** mode. Switching mode does not open another page and does not clear the values common to both (source path, catalog name, storage). In standard mode the page behaves exactly as before this spec; the mode switch is the only visible addition. The last mode used is restored when the page is opened again. | [Implemented — Phase A.1] |
| MDI-F2 | The wizard presents its choices as ordered steps: (1) source drive or folder; (2) destination path for derived files; (3) file types to include; (4) preview options; (5) AI options; (6) a summary of every choice with a *Create* action. The user can go back to any previous step without losing later choices. | [Removed — superseded by `MDI-F60` (D15, 2026-10-28)] |
| MDI-F3 | The file types section offers the file types Image, Video, Audio, Text and Other, each on or off independently. The document type is labelled with the existing catalog file type *Text* (user decision 2026-10-28; reuses an existing translation, no *Document* string). Only files of the selected types are enriched; the catalog itself is created with the same file selection standard mode would use for the same source. | [Implemented — Phase A.1] |
| MDI-F4 | The previews section offers, per type: **Image** — thumbnail on/off. **Video** — poster frame on/off; keyframe strip on/off with a frame count (default 4, range 2–32; default 8 until D23, 2026-10-29); low-resolution clip off / full / partial, where partial takes a start offset and a duration in seconds (default 0 s and 30 s). **Audio** — clip off / full / partial (same parameters); waveform image on/off. | [Implemented — Phase A.1] |
| MDI-F5 | The *AI analysis* section offers a provider choice and the analyses: image caption and tags; document summary and key excerpts; transcript; topic segments; topic clusters; embeddings for semantic search. The default provider is the local one. A commercial provider appears in the list only when it is configured (`MDI-F23`). An analysis whose external tool or model is unavailable is shown disabled with the reason (`MDI-F20`). Topic segments require transcript; topic clusters require topic segments; selecting a dependent analysis selects its prerequisite. Generated text (captions, tags, summaries, segment titles and summaries, topics, cluster labels and descriptions) is in the source's language by default; an option sets it to the UI language instead (D12). Key excerpts stay verbatim (`MDI-F40`). | [Planned — part implemented in Phase B] |
| MDI-F6 | *Create* is refused, with a `Kirigami.InlineMessage` at the top of the form and nothing created, when: the source is empty; the destination path is empty; the destination path is not writable; the destination path is the source or lies inside it; no file type is selected while at least one preview or analysis is selected. The message hides when the offending field is edited (`SpecValidationRules.md`). | [Implemented — Phase A] |
| MDI-F7 | Confirming the wizard creates the catalog through the **existing** catalog-creation path (same entry point, same queue behaviour, same result as standard mode for the same inputs) and saves the enrichment profile for that catalog. Only when the creation **completes successfully** is an enrichment job for that catalog added to the enrichment list (`MDI-F16`). A creation that fails, is cancelled, or is removed from the operation queue creates no enrichment job. | [Implemented — Phase A] |
| MDI-F8 | The enrichment profile is saved per catalog and survives closing and reopening the collection. Later runs on that catalog use it. For any existing catalog — created in wizard mode, in standard mode, or before this feature — the user can, from the catalog's page and from Explore, edit its enrichment profile (or create one when it has none) and start an enrichment job, which is added to the enrichment list (`MDI-F16`) (D8). No new page is created for this (`MDI-C18`). | [Planned — part implemented in Phase A] |
| MDI-F60 | In wizard mode the Create page is **one page without steps**: the standard form's *Catalog definition* section, then the enrichment options in this order — destination path for derived files; file types (`MDI-F3`), one toggle button per type; previews (`MDI-F4`), as switches; an *AI analysis* section (`MDI-F5`) — and the *Create* action. Every choice is visible at once; nothing is hidden behind a *Next* action (D15). The same enrichment options are used by the *Analyze* dialog of an existing catalog (`MDI-F8`). | [Implemented — Phase A] |

### Enrichment runner {#runner}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F9 | The runner processes one file at a time. For each file it runs the profile's analyses in a fixed order and commits each analysis's result and provenance before starting the next. Stopping, a crash or a power loss loses at most the analysis in progress. | [Implemented — Phase A] |
| MDI-F10 | **Stop** ends the running job after the current analysis step is abandoned (its external process is terminated, its HTTP request aborted); results already committed are kept. The job's final status is *Stopped*. | [Planned — part implemented in Phase A] |
| MDI-F11 | **Pause** halts the job before its next analysis step; **Resume** continues from there. While paused, no external process of the job runs, no request is sent and no source file is read — the parallel thumbnail pre-pass (`MDI-C24`) included: it does not start while the job is paused, and its decodes not yet started do not start after Pause (D31). The drive can therefore be disconnected while the job is paused (`MDI-F91`). | [Implemented — D31] |
| MDI-F12 | A job that had not finished when Katalog closed — the running job and the waiting jobs, in their order — continues from the first file without a completed result for each selected analysis when the collection is opened again. A job whose analyses use only local tools and the local provider resumes automatically. Any other job (one using a commercial, remote or local-network provider) is listed as *Paused* and sends nothing until the user resumes it; resuming it is subject to `MDI-F24` (D6). A job that was paused when Katalog closed — by Pause or by `MDI-F28` — is listed as *Paused* whatever its provider (D31). A restored job whose source or destination drive is away waits for it (`MDI-F91`); a pending job leaves the list only when its catalog no longer exists (D31). Quick-hash passes are not kept; `MDI-F84` schedules them again. The list is kept as `MDI-C28` states. | [Implemented — D31] |
| MDI-F13 | **Incremental.** A file is processed for an analysis only when it has no completed datum for that analysis, or when its current size or modified date differs from the values recorded with that datum (`MDI-F52`), or when the profile's parameters for that analysis changed. Every other file is counted as *unchanged* and skipped. Running a job twice in a row with no file change processes zero files. | [Implemented — Phase A] |
| MDI-F14 | When a file has left the catalog (removed by a device update), the next job on that catalog removes its derived data from the database and deletes the derived files recorded for it. Only derived files the runner itself created and recorded are ever deleted. | [Planned] |
| MDI-F15 | While a job runs: a search, an Explore load, a catalog creation and a device update of **another** catalog are not refused because of it. A device update, re-scan or deletion of the catalog **being enriched** first pauses that job; after an update it resumes automatically and picks up the changes through `MDI-F13`; after a deletion the job and the catalog's derived data are removed (`MDI-F57`). | [Planned — part implemented in Phase A] |
| MDI-F16 | Enrichment jobs run one at a time, in the order they were added (except as `MDI-F92` states), from their own list. Adding a job identical to one already running or waiting — same catalog, same kind (enrichment or re-interpretation, `MDI-F103`) and same scope (folder, file type) — is ignored; a re-interpretation of a catalog that already has an enrichment job or another re-interpretation listed is queued as its own item (D33, 2026-10-29). A waiting job can be removed without affecting the running one. This list is **not** the operation queue of `SpecOperationQueue.md` (`MDI-C3`). | [Planned — part implemented in Phase A] |
| MDI-F17 | Enrichment progress is shown in the activity panel, as its own row, distinct from the queue's running entry, so it is visible alongside a running creation, update or search. It shows the standard status message (`MDI-C16`) with operation, status, catalog context, the current analysis as process title, count of total, time to completion and current file, and carries Pause/Resume and Stop. | [Planned — part implemented in Phase A] |
| MDI-F18 | When a job ends, its summary line reports, per analysis: completed, unchanged, skipped (tool or model missing), failed. A *Report* action lists each failed file with the analysis and the reason. The report is kept for the session. | [Planned — part implemented in Phase A] |
| MDI-F19 | Before a job starts, the destination path's free space is checked against an estimate of the derived files' size. With insufficient space the job does not start and the summary reports it; when free space exceeds the estimate by less than 10 %, it starts and the summary warns. | [Planned] |
| MDI-F74 | **Per-step activity reporting.** For each analysis step, the runner reports its start (analysis, file, tool and its version, model, where it runs — local / local-network / remote with the endpoint host) and its end (duration, outcome completed / failed / skipped, and the reason when not completed). These reports feed the Activity page (`MDI-F64`, `MDI-F67`) and are built with the standard builder (`MDI-C16`). | [Planned — part implemented in Activity phase] |
| MDI-F75 | **Progress inside a step.** During a transcription step, the runner reports the audio minutes transcribed out of the file's total. During a model call, it reports tokens per second when the provider returns token counts. | [Planned — part implemented in Activity phase] |
| MDI-F91 | **Waiting for the drive.** When a file of an enrichment job cannot be reached and the catalog's source path (its device path) cannot be reached either, the job does not skip the file: it waits for the drive — no tool runs, no source file is read, no request is sent — and continues by itself with that same file as soon as the source path is reachable again. A file that cannot be reached while the source path is reachable (deleted or moved) is skipped as before. The job waits in the same way for the drive of its destination path when that path does not exist and cannot be created; a destination path not created yet is created as before, and a job the user starts is still refused when its destination cannot be written (`MDI-F6`, `MDI-F8`) (D31). An analysis step during which the source or the destination becomes unreachable is not recorded: like an abandoned step (`MDI-F10`) it neither replaces the file's datum nor deletes its derived files, and it runs again when the drive is back (an exception to `MDI-F22`). Pause and Stop act while waiting (`MDI-F10`, `MDI-F11`); a job paused while waiting stays paused when the drive is back. The activity-panel row (`MDI-F17`) and the Activity page status line (`MDI-F62`) show the waiting state with the catalog and the path waited for (source or destination). A quick-hash pass (`MDI-F84`) does not wait: it ends unfinished and is scheduled again as `MDI-F84` states (D31). | [Implemented — D31] |
| MDI-F92 | **A job waiting for its drive does not block the list.** When the running job waits for its drive (`MDI-F91`) and a waiting enrichment job or quick-hash pass has a reachable source, the job waiting for its drive ends its run, goes to the end of the enrichment list, and the next one with a reachable source runs. When the next run is chosen, enrichment jobs whose source is reachable go first in their order, then quick-hash passes, then jobs whose source is unreachable. When no waiting job has a reachable source the job keeps waiting; it never steps aside for nothing (D31). | [Implemented — D31] |
| MDI-F93 | **Switching collection.** Opening another collection while an enrichment job or quick-hash pass runs ends it as quitting does: the step in progress is abandoned and not recorded (`MDI-F10`); the job and the waiting jobs stay pending in the left collection's `mdi_data.db` and are restored when that collection is opened again (`MDI-F12`). The activity panel and the Activity page then show nothing of the left collection. The pending jobs of the collection being opened are restored as `MDI-F12` states (D31). | [Implemented — D31] |

### Activity page {#activity}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F61 | K3 has an *Activity* page, reachable from the side menu, that shows at every moment what is running, where it runs (local / local network / remote), with which resources and for what (D19). With nothing running it says so. | [Implemented — Activity phase] |
| MDI-F62 | **Status line.** The page shows, for the running enrichment job: the catalog, *file N of M*, the estimated end time, and Pause/Resume and Stop, which act as `MDI-F10` and `MDI-F11`. | [Implemented — Activity phase] |
| MDI-F63 | **Resource tiles.** The page shows tiles for: CPU; GPU; memory used of total; swap; memory pressure; power source; whether the source drive of the running job is connected; data egress, which reads "nothing sent" unless a remote or local-network provider is in use by the running job, and then names the provider's host. A value the resource monitor cannot obtain on the platform is shown as unavailable (`MDI-F70`). | [Planned — part implemented in Activity phase] |
| MDI-F64 | **"Right now" card.** The page shows the analysis step in progress: analysis, file, tool and model, where it runs, its progress, and a detail where available — audio minutes done of total for a transcription, tokens per second for a model call (`MDI-F74`, `MDI-F75`). | [Planned — part implemented in Activity phase] |
| MDI-F65 | **Tools card.** The page shows `ffmpeg`, `ffprobe`, `pdftotext`, `whisper.cpp` with its model, and Ollama with the models currently loaded in its memory and their size (`MDI-F72`), each as found with its version, or not found (`MDI-F20`). | [Planned — part implemented in Activity phase] |
| MDI-F66 | **Enrichment queue.** The page lists the enrichment jobs waiting, in run order (`MDI-F16`). | [Implemented — Activity phase] |
| MDI-F67 | **Log of finished steps.** The page lists finished analysis steps, newest first, each with time, analysis, file, duration, outcome and, when not completed, the reason (`MDI-F74`). How long the log is kept is `Q16`. | [Implemented — Activity phase; `Q16` default applied] |
| MDI-F68 | **Resource history.** The page shows CPU, GPU and memory history charts covering the current session only; the history is not persisted and starts empty at each launch (D21). | [Implemented — Activity phase] |
| MDI-F69 | The bottom activity panel keeps its enrichment row (`MDI-F17`) as a compact summary; activating it opens the Activity page. | [Implemented — Activity phase] |

### Resource monitor {#resource-monitor}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F70 | A resource monitor in `core/` takes a snapshot periodically, on its own thread, of: CPU use; GPU use; memory used and total; swap used; memory pressure; power source; and whether a given device path is connected. A value it cannot obtain on the platform is reported as unavailable, never as zero or a guess. The sampling interval is `Q17`. | [Planned — part implemented in Activity phase; `Q17` default applied] |
| MDI-F71 | The resource monitor reports Katalog's child processes (external tools started by the runner) each with its CPU and memory use. | [Implemented — Activity phase] |
| MDI-F72 | The resource monitor reports the models Ollama has loaded in memory, with their size, by querying the configured Ollama endpoint's `/api/ps`; the query sends no content and is subject to `MDI-C11`. When Ollama is unreachable it reports that, and the other values are unaffected. | [Implemented — Activity phase] |
| MDI-F73 | Windows: values the monitor cannot obtain without a platform-specific API are reported as unavailable and shown as unavailable (`MDI-F63`); nothing fails because of them. | [Implemented — Activity phase] |

### External tools {#tools}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F20 | Katalog detects `ffmpeg`, `ffprobe`, `whisper.cpp` (executable and model file), `pdftotext` and `pdftoppm` on the system search path and in the user-configured paths. The Settings page shows, for each tool, the path found and the version it reports, or that it was not found, and lets the user set or clear a path per tool. Detection runs when Settings opens and before each job. Katalog does not ship these tools or the whisper model; the user installs them (D9). | [Planned — part implemented in Phase A] |
| MDI-F21 | When a tool an analysis needs is missing, that analysis is skipped for the whole job and counted as *skipped (tool missing)* with the tool's name in the summary; the job's other analyses run. Katalog never crashes or freezes because a tool is missing. | [Planned — part implemented in Phase A] |
| MDI-F22 | When a tool fails on one file (non-zero exit code, unreadable output, or no output within its time limit, default 10 minutes per invocation, configurable), the file's analysis is recorded as failed with the reason and the runner moves on to the next file. A timed-out process is terminated. | [Planned — part implemented in Phase A] |

### Providers {#providers}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F23 | Settings holds the providers. **Local:** an Ollama endpoint (default `http://localhost:11434`) and one model per task: text, vision, embeddings. A connection test lists the models the endpoint offers. **Commercial (off by default):** an OpenAI-compatible API (base URL, model per task, key) and the Anthropic API (model per task, key), and a price per million input tokens and per million output tokens entered by the user; no price list is shipped (D14). A key is either entered for the session only, or read from an environment variable; it is never saved (`MDI-C13`, D10). A commercial provider can be used by a job only when it is enabled for the open collection, or enabled for that job in the wizard. | [Planned — part implemented in Phase B] |
| MDI-F24 | Before a job sends any content to a remote or local-network provider, a modal confirmation states: the provider and endpoint host; the kinds of content sent (thumbnails, frames, extracted text, transcript text); the number of files; and the estimated cost (`MDI-F25`). *No* is the default. Confirming starts the job; declining sends nothing, and the job then runs only its analyses that use the local provider or local tools. | [Planned] |
| MDI-F25 | The cost estimate is the estimated input and output tokens of the job's remote analyses multiplied by the provider's prices (`MDI-F23`), shown as an amount with its currency and labelled as an estimate. When no price is entered, the dialog states the cost is unknown; confirming is still possible. | [Planned] |
| MDI-F26 | A provider endpoint is classified local, local-network or remote by its host, with the same rule as `SpecHostedSecurity.md` `HST-F2`–`HST-F4`. A local-network or remote endpoint is subject to `MDI-F24` whatever product it runs. | [Planned — part implemented in Phase B] |
| MDI-F27 | For each remote call, the cost reported by the provider's usage data (or computed from the returned token counts and the entered prices when the provider gives no amount) is recorded on the datum; the job summary shows the total cost. | [Planned] |
| MDI-F28 | When a provider is unreachable or refuses three consecutive requests, the job pauses and its status names the provider and the error. Resuming retries. Katalog does not retry in a loop. | [Planned] |

### Previews {#previews}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F29 | **Image thumbnail:** a JPEG whose longest side is 320 px (configurable 128–1024), aspect ratio kept, orientation tag applied. | [Removed — superseded by `MDI-F78` (D23, 2026-10-29)] |
| MDI-F30 | **Video poster frame:** one frame taken at 10 % of the duration (from `ffprobe`), saved like a thumbnail. | [Implemented — Phase A] |
| MDI-F31 | **Video keyframe strip:** the selected number of frames, evenly spaced over the duration, each saved like a thumbnail and recorded with its timecode. | [Removed — superseded by `MDI-F79` (D23, 2026-10-29)] |
| MDI-F32 | **Video low-resolution clip:** an MP4 (H.264 video, AAC audio) no taller than 360 px, of the whole file or of the selected partial range. | [Implemented — Phase A] |
| MDI-F33 | **Audio clip:** an M4A (AAC) of the whole file or of the selected partial range. **Waveform:** a PNG image of the whole file's amplitude. | [Implemented — Phase A] |
| MDI-F34 | Derived files are written only under the catalog's destination path, in a layout that does not depend on the source file's name or folder (`MDI-C19`). Opening a derived clip or image opens it with the system's default application. | [Implemented — Phase A] |
| MDI-F78 | **Image thumbnail:** a WebP file under the destination path whose longest side is 256 px, quality 75 (about 6 KB), aspect ratio kept, EXIF orientation applied. Where Qt has no WebP writer, a JPEG is written instead. Thumbnails are files, never database BLOBs (D23). The video poster frame (`MDI-F30`) is saved the same way. | [Implemented — increment 3] |
| MDI-F79 | **Video keyframe strip:** one contact-sheet image holding the selected number of frames (`MDI-F4`, default 4), evenly spaced over the duration, side by side, each 144 px high. The datum records, per frame, its timecode, its horizontal position and its width in the sheet (D23). | [Implemented — increment 3] |
| MDI-F80 | **Technical data:** for video and audio files, `ffprobe` gives codec, resolution, frame rate, duration, bit rate and audio stream details, stored as a derived datum and shown as *Technical data* in *Show analysis* and the Inspector (`MDI-F87`) (D23). | [Implemented — increment 3] |

### Video and audio — transcripts, segments, clusters {#transcripts}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F35 | **Transcript:** the audio track is extracted with `ffmpeg` and transcribed locally with `whisper.cpp`. The transcript is stored as an ordered list of passages, each with start and end timecodes and text, plus the detected language. A file without an audio track (checked with `ffprobe` when it is available) is recorded as *skipped (no audio track)*: nothing is extracted, transcribed or sent (2026-10-29). | [Implemented — Phase C] |
| MDI-F36 | **Topic segments:** the text model divides a transcript into consecutive segments. Each segment has a start and end timecode taken from transcript passage boundaries, a title, a summary and a list of topics. Segments cover the transcript without overlapping. | [Implemented — Phase C] |
| MDI-F37 | **Topic clusters:** segments are grouped into clusters, each with a label, a description and its member segments; a segment belongs to at most one cluster. Clustering is per catalog: a cluster groups segments of files of one catalog only (D11). When clusters are recomputed is decided at the start of Phase C (`Q3b`). | [Implemented — Phase C] |
| MDI-F38 | Wherever a segment or transcript passage is shown, its timecodes are shown as `h:mm:ss`. | [Implemented — Phase C] |

### Images — caption and tags {#images}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F39 | **Caption and tags:** the vision model receives the image's thumbnail (never the original) and returns a caption of at most two sentences and a list of tags. Both are stored with the image. These AI tags are kept separate from Katalog's user tags (`Tags` page) and never change them. | [Implemented — Phase B] |

### Documents {#documents}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F40 | **PDF summary and key excerpts:** text is extracted page by page with `pdftotext`; the text model returns a summary and up to 10 key excerpts, each excerpt verbatim from the text with its page number. | [Planned — part implemented in Phase B] |
| MDI-F41 | A PDF with no text layer is recorded as *skipped (no text)*, unless vision reading of scanned pages is selected in the profile: then up to 20 pages are rendered with `pdftoppm` and read by the vision model, subject to `MDI-F24` when that provider is not local. | [Planned] |
| MDI-F42 | **Other documents:** plain-text and Markdown files, then Office formats, receive the same summary and excerpts as PDF, with a location reference where the format has one. The tool for Office formats needs its own approval. | [Backlog] |

### Applications and other files {#apps}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F43 | **Characterising information:** for applications and other binary files, Katalog records where obtainable: version, publisher, architecture, bundle identifier and code-signature status. Each item absent from a file is left empty, not guessed. The means per platform need their own approval. | [Backlog] |

### File contents — text snippet and archives {#contents}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F81 | **Text snippet:** for a file with the extension `txt`, `md`, `csv`, `tsv`, `json`, `xml`, `log`, `srt`, `vtt`, `rtf`, `html`, `pdf`, `docx`, `odt`, `doc`, `pptx`, `odp`, `xlsx` or `ods`, the first 800 characters of its text, cleaned, are stored as a derived datum. A file whose content is binary is skipped. The choice is by extension, whatever file types the profile selects: an exception to `MDI-F3` (D24). | [Implemented — increment 3] |
| MDI-F82 | **Archive contents:** for `zip`/`jar`, `tar` (plain, `.gz`, `.bz2`, `.xz`, `.zst`) and `7z` files, the list of entries is stored: at most 20,000 entries, while all entries are counted. A compressed tar larger than 2 GB is skipped; RAR is not supported. An archive KArchive cannot read (for example a tar made on macOS with pax/xattr headers) is recorded as failed with the reason. Entry names are part of the derived text of the instant search index (`MDI-F76`). The list is shown in *Show analysis* and the Inspector (`MDI-F87`). In a build without KArchive the analysis is skipped with its reason (`MDI-F21`, `MDI-C25`) (D25). | [Implemented — increment 3] |
| MDI-F83 | **Archives in the Explore tree:** an archive with a stored entry list is shown as a folder in the Explore tree, its entries browsable like files (D25). | [Backlog] |

### Browsing derived data offline {#offline}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F44 | In Explore and in Search results, a file that has derived data is marked, and selecting it shows its derived data: thumbnail or frames, clip and waveform, caption and AI tags, summary and excerpts, transcript and segments. This works identically whether the device is connected, disconnected or inactive. | [Planned — part implemented in Phase A] |
| MDI-F45 | When the destination path is unreachable (its drive disconnected, or the collection opened on another machine), the derived text (captions, tags, summaries, excerpts, transcripts, segments) is still shown from the database, and each derived file is replaced by a placeholder saying it is not available. No error dialog is shown. | [Planned — part implemented in Phase A] |
| MDI-F46 | From a segment, *Open at timecode* opens the derived clip, or the original when the device is connected, with the system default application; the timecode is shown for the user to seek, because seeking an external player is not portable. There is no in-app playback (D7). | [Planned — part implemented in Phase A] |
| MDI-F87 | **Inspector.** Explore and Search results have an *Inspector* side panel, shown or hidden by the *Inspector* action and by `Space` when the focus is not in a type-ahead; its visibility is remembered (settings key `View/InspectorVisible`). For the selected file it shows: the disk state, *online* (with the volume name; activating it opens the file) or *offline* (naming the device to connect); the preview or contact sheet; the caption or summary; the text snippet; the technical data; the extended metadata, with an *Open on map* link (OpenStreetMap, in the system browser) when GPS coordinates are present; the quick hash and the SHA-256; the archive entries; and *Show analysis*. With no file selected it says so. It works whether the device is connected or not (`MDI-F44`) (D27). | [Implemented — increment 3] |
| MDI-F88 | **Explore column view.** A *Columns* action switches Explore to a Finder-style column view: one column per folder level, from the same entries as the Explore list, folders first, with thumbnails, navigable with the keyboard. The choice is remembered (settings key `View/ExploreColumns`) (D28). | [Implemented — increment 3] |

### Semantic search {#semantic}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F47 | The Search page offers a **semantic** search mode, distinct from the existing text search. Its query is one natural-language text. Results are ranked by similarity between the query's embedding and the stored embeddings of captions, summaries, excerpts, transcript passages and segments. | [Implemented — Phase D] |
| MDI-F48 | Each semantic result shows the file, the passage that matched, its kind (caption, summary, excerpt, passage, segment), its rank, and, for passages and segments, the timecode. Several matches in the same file are grouped under that file. | [Implemented — Phase D] |
| MDI-F49 | Semantic search covers only the devices of the current selection (`SpecSelection.md`). Stored embeddings made by a model other than the collection's current embeddings model are not compared; their count is reported with the results. | [Planned — part implemented in Phase D] |
| MDI-F50 | The query is embedded by the collection's embeddings provider. When that provider is not local, the first semantic search of the session shows the `MDI-F24` confirmation (without file count or cost estimate); declining runs no search. | [Planned] |
| MDI-F51 | Semantic search returns at most 50 results by default (configurable 10–500), runs in the background, reports progress with the standard builder through the activity panel as a search does (`OPQ-F15`), and can be stopped. | [Planned] |

### Instant search index {#instant-search-index}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F76 | **Instant search index.** Each catalog has a full-text index of its files' names, folders and derived text (text snippet, summary, caption, AI tags, archive entry names). It is rebuilt in the background for every catalog whose index is missing or older than the catalog's last update, after the collection is opened and after each creation or update; the runner updates a file's entry after its analyses. Searches and Explore keep working while it is rebuilt (D22). | [Implemented — increment 3] |
| MDI-F77 | **Text search fast path.** When a catalog's index is current, a K3 text search on that catalog first narrows its files to the index's matches, then applies the unchanged search criteria; the results are identical to the same search without the index. The index is not used, and the search runs as before, for: regular-expression criteria; a term shorter than 3 characters; a `/` in the text outside names-only mode; Hosted mode; a missing or stale index (D22). | [Implemented — increment 3] |

### Quick hash and duplicates {#quick-hash}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F84 | **Quick hash.** Right after a catalog is created or updated, and when the collection is opened, the runner computes a quick hash (`MDI-C26`) for every file of every catalog whose quick hashes do not cover its last update and whose source is reachable, whether or not the catalog has an enrichment profile, as its own pass (*Quick hash*). It is an exception to `MDI-F3` and to the user-started jobs of `MDI-F7`/`F8`. Hosted mode is not covered (D26). | [Implemented — increment 3] |
| MDI-F85 | **Duplicates by quick hash.** A K3 duplicates search on checksum uses a file's quick hash as its key when it has one, and its SHA-256 otherwise. An option *Verify with full checksum* re-keys the candidate duplicates with their SHA-256, stored or computed from the file; the option holds for the session only and is not saved in the search history (D26). | [Implemented — increment 3] |
| MDI-F86 | **Compare two devices by quick hash.** The comparison of two devices on checksum uses quick hashes as `MDI-F85` does (today it uses SHA-256) (D26). | [Backlog] |

### Provenance and storage {#provenance}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F52 | Every derived datum records: the file (or segment / cluster) it belongs to; the analysis; the tool name and version, or the model name and version/digest; the provider name, its class (local / local-network / remote) and endpoint host; the date; the file's size and modified date at processing time; the analysis parameters, including the prompt version; the cost and currency, or none; and the outcome (completed / failed / skipped) with the reason. | [Planned — part implemented in Phase A] |
| MDI-F53 | A user can re-run an analysis on a catalog for all files, or only for files whose last outcome is failed. A re-run replaces the current datum: the replaced data (text, embedding, derived file) is discarded, and only its provenance record is kept as history (D13). | [Planned] |
| MDI-F54 | The prompts used by analyses are versioned; changing a prompt changes its version, and `MDI-F13` then treats the analysis as having changed parameters. | [Implemented — Phase A] |
| MDI-F55 | Derived data is stored in File, Hosted and Memory modes alike. In File and Memory modes it is stored in its own SQLite file, `mdi_data.db`, in the collection folder (D17), so it survives closing Katalog in both modes and is never loaded into the in-memory database. The enrichment profile stays in the collection database; in Memory mode it is written to `mdi_profile.csv` in the collection folder and loaded when the collection is opened, before any query that reads it. Hosted mode: where derived data is stored is open (`Q11`). | [Planned — part implemented in Phase A] |
| MDI-F56 | Collection export (`SpecCollectionExport.md`) carries the derived data to the target mode. Derived files are not copied; the exported records keep pointing to the same destination path. | [Planned] |

### Interaction with catalog settings {#catalog-settings}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F57 | Deleting a catalog deletes its enrichment profile, its jobs, its derived data and the derived files recorded for it; the existing deletion confirmation states that derived files will be deleted too. | [Planned] |
| MDI-F58 | Enrichment is independent of the catalog's *Include metadata* level. It runs with any level, including *None*. Changing the level, including to *None*, never clears, invalidates or re-runs derived data; its clearing scenarios (`SpecCatalogMetadata.md` Scenario 2) apply to file metadata only. See the note below. | [Implemented — Phase A] |
| MDI-F59 | A *Clear derived data* action on a catalog removes all its derived data and derived files after a confirmation dialog naming the catalog and the size to be freed. | [Planned] |

> **Note on *Include metadata* levels (`MDI-F58`).** *Include metadata* governs
> file metadata columns (`image_*`, `video_*`, `audio_*`, `metadata_extended`,
> `metadata_extraction_date`), extracted during creation and update. Enrichment
> is a separate layer with its own tables, its own completion markers and its own
> runner, and it obtains what it needs (duration, dimensions) from `ffprobe`
> itself. Derived data can cost money and hours of computation, so it is never a
> side effect of a metadata setting: lowering the level to *None* to reduce the
> database keeps every caption, transcript and embedding. Removing derived data
> is always the explicit action of `MDI-F59` (or a catalog deletion, `MDI-F57`).

### Splitting a catalog {#split}

D35 (2026-10-30). The split itself is `SpecDevicesSplit.md`. These rows add
only what happens to the fork's data when a catalog is split.

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F126 | **AI data follows its files when a catalog is split (D35).** When a catalog is split by sub-folder or by file type, every derived datum (`MDI-F52`) and every embedding (`MDI-F47`) of the old catalog whose file is held by one of the new catalogs, matched by the file's full path, is moved to that new catalog. Nothing else in it changes: outcome, value, provenance, dates and derived root stay as they were. The file's previews, captions, summaries, transcripts and classifications then show for the new catalog in Browse, Search and Home, and a later job on the new catalog treats them under `MDI-F13` like any other datum. A datum whose file is in none of the new catalogs stays as it is. A datum that would collide with a datum the new catalog already has for the same file and analysis is left in place: nothing is overwritten. Nothing is deleted, and no derived file is moved, renamed or deleted on disk. The removal of the old catalog by the split is not a deletion in the sense of `MDI-F57`: its derived data and derived files are not deleted. Hosted mode stores no derived data (`Q11`), so nothing moves there. | [Implemented — D35] |
| MDI-F127 | **The enrichment profile follows a split catalog (D35).** When a catalog is split, the old catalog's enrichment profile (`MDI-F8`), with its destination path and its settings, is copied to the device of each new catalog. *Analyse with AI* on a new catalog then opens with that profile, and its jobs write under the same destination path. A catalog without a profile gives new catalogs without one. In Memory mode the copies are written to `mdi_profile.csv`, as any saved profile is (`MDI-C5`). | [Implemented — D35] |
| MDI-F128 | **Instant search index after a split (D35).** After a split, the instant search index (`MDI-F76`) of each new catalog is built in the background, as after a catalog creation, so the text-search fast path (`MDI-F77`) applies to it once the index is current. Searches and Browse keep working meanwhile. **Defect found 2026-10-30 (D36):** the new catalogs had an empty update date, so their index never became current and was rebuilt at every launch (`MDI-C22`). Fixed by `MDI-F143`. | [Implemented — D35; date defect fixed by `MDI-F143` (D36)] |
| MDI-F143 | **A split gives its catalogs an update date (D36, answer 3).** Every catalog created by a split, by sub-folder or by file type, has a valid update date (`catalog_date_updated`), never an empty one. Once its instant search index is built (`MDI-F128`) it is current, and it is not rebuilt at the next launch (`MDI-C22`). Which date is `Q54`. | [Implemented — D36] |

### Side menu {#side-menu}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F89 | The K3 side menu's main entries are Selection, Search, Explore, Create, Activity and Settings; a *Tools* submenu holds Devices, Tags, Statistics, Backup and About. The drawer has no *Documentation* and no *Quit* entry; Quit stays available through the standard shortcut. The footer pins are icon buttons with tooltips (`SpecIconButtons.md`). The drawer's *Open* menu (`OPN-F7`, `OPN-F9`) is unchanged (D29). Since D33 the *Tools* submenu also holds *Context* (`MDI-F107`); the main entries do not change. **Superseded by D34:** the sidebar of `MDI-F111` replaced this menu in phase R1 (2026-10-29). | [Removed — superseded by `MDI-F111` (D34, R1)] |

### Connected drives {#connected-drives}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F90 | The K3 Create page shows *Connected drives*: the external drives, mounted or not, with *Refresh*. A drive that is not mounted reads *Not mounted* and offers *Mount read-only*, which asks the system to mount it read-only and, on success, puts its mount point in the source path. When the mount fails and a third-party file-system driver (Paragon, Tuxera, macFUSE) handles the drive, the message names the driver and explains that the system reads NTFS drives on its own; otherwise it gives the system's reason. Windows lists mounted volumes only (D30). | [Implemented — increment 3] |

### Domain context, classification and knowledge base {#context}

D33. Rows `MDI-F94`–`F108` are context phase 1 (local only); `MDI-F109` and
`F110` are context phase 2. Ratified by the fork owner on 2026-10-29 (D33).

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F94 | **Context of the collection.** A collection has one domain *context*, written by the user: organisation; period covered; domain description; narrative style and tone; language of generated text (`MDI-F98`); free notes; a **glossary** (term, preferred form, aliases, definition); **categories** (name, description, one value or several, allowed values as a closed list or an open one); **known entities** (name, kind — person, place, event, organisation or other —, aliases, notes). It is saved only when the user saves it, and survives closing and reopening the collection, in File and Memory modes alike (D33, answers 1 and 4). | [Implemented — Context 1] |
| MDI-F95 | **Catalog notes.** Each catalog can add its own notes to the collection's context. An analysis of a file of that catalog uses the collection's context and that catalog's notes; other catalogs are not affected (D33, answer 1). A catalog adds notes only: no glossary terms, categories, entities or documents of its own (`Q20`, 2026-10-29). | [Implemented — Context 1] |
| MDI-F96 | **Context version.** Each save that changes the context or a catalog's notes gives it a new version, derived from its content, and a date. The version used and the knowledge-base documents used (`MDI-F106`) are recorded with every datum produced with a context (`MDI-F52`). | [Planned — part implemented in Context 1] |
| MDI-F97 | **Draft from the catalog.** An action asks the text model for a proposed context — domain description, glossary, categories, known entities — from a catalog's folder names and a sample of its existing captions, AI tags and summaries. The proposal opens for editing; nothing is saved until the user saves, and discarding it leaves the context unchanged. | [Implemented — Context 1] |
| MDI-F98 | **Context in new analyses.** When a context exists, the requests for caption and tags (`MDI-F39`), summary and excerpts (`MDI-F40`), topic segments (`MDI-F36`) and topic clusters (`MDI-F37`) include its domain description, narrative style and glossary, and the catalog's notes. When the context sets a language, every AI-generated text — caption and tags, summary, segment titles and summaries, cluster labels and descriptions, classification — is in that language, in place of D12's choice (`MDI-F5`); without one, D12 applies (`Q23`, 2026-10-29). Only when a separate option of the context, off by default, is on does a transcript (`MDI-F35`) give the glossary's preferred forms to `whisper.cpp` as its initial prompt, within that program's prompt limit (`Q28`, 2026-10-29). This applies to every such step run after the context is saved: new files, changed files (`MDI-F13`) and, for the classification, re-interpretation (`MDI-F103`). Neither the language nor the option is an analysis parameter (`MDI-F99`). Key excerpts stay verbatim (`MDI-F40`); a transcript stays what was said, in the language spoken (D33, answer 2). | [Planned — part implemented in Context 1] |
| MDI-F99 | **No automatic re-run (D33, answer 2).** Saving, changing or deleting the context, a catalog's notes or a knowledge-base document never re-runs an analysis by itself and never makes an existing datum out of date: the context, its version and the knowledge base are not part of an analysis's parameters for `MDI-F13`. A prompt's version (`MDI-F54`) covers the prompt's own text only, not the context sections added to it, so adding context support to a prompt re-runs nothing. This is an exception to `MDI-F13` and `MDI-F54`. Existing results change only through `MDI-F103` or a re-run (`MDI-F53`). | [Implemented — Context 1] |
| MDI-F100 | **Classification.** A new AI analysis, *classification*, is offered in the *AI analysis* section (`MDI-F5`, `MDI-F60`) and the *Analyze* dialog (`MDI-F8`), shown disabled with its reason while the collection has no context. For each file the text model receives: its name, folder path and modified date; its extended metadata; its derived text in this catalog (caption and AI tags, summary, text snippet, an excerpt of its transcript, its segment titles, its technical data (`MDI-F80`) and its archive entry names (`MDI-F82`), the last two added by the fork owner on 2026-10-29); the context and the catalog's notes; the knowledge-base passages of `MDI-F106`. It returns a title, a description written in the context's terms and narrative style, values for the context's categories, free tags, the entities named, and the knowledge-base documents used. It never reads the original file and never calls the vision model or `whisper.cpp`. It runs after the file's other analyses and before its embeddings (`MDI-F9`) (D33, answer 4). **Which files an enrichment job classifies (`Q22`, 2026-10-29):** only a file that is new — no datum of the profile's other analyses is recorded for it, the quick hash (`MDI-F84`) aside — or changed — its size or modified date differ from those recorded with its data (`MDI-F13`) —, and that has no current classification. A file already analysed is classified only through `MDI-F103`, after its estimate. An exception to `MDI-F13` for this analysis. Its language is that of `MDI-F98`. | [Planned — part implemented in Context 1] |
| MDI-F101 | **Category values and terms (D33, answer 4).** For a category with a closed list, only listed values are stored: a returned value equal to a glossary alias or an entity alias is stored as its preferred form; any other value is dropped and the datum records what was dropped. For a category with an open list, a value matching a glossary term or alias is stored as its preferred form. A single-value category keeps at most one value. Free tags are kept apart from category values. An entity matching a known entity is named by that entity's name; other entities are kept and marked as not in the list. Classification values and tags are kept apart from Katalog's user tags (`Tags` page) and never change them, as `MDI-F39` states for AI tags. | [Planned — part implemented in Context 1] |
| MDI-F102 | **Showing and finding classifications.** *Show analysis* and the Inspector (`MDI-F87`) show a file's classification: title, description, each category with its values, tags, entities, and the knowledge-base documents used, by name. Its title, description, category values, tags and entity names are derived text of the instant search index (`MDI-F76`) and are embedded for semantic search (`MDI-F47`), where the kind shown is *classification* (`MDI-F48`). Filtering a search by category value is `Q26`. | [Implemented — Context 1] |
| MDI-F103 | **Re-interpret with the context (D33, answer 2).** An explicit action on a catalog (owner's wording: «Rielabora con il contesto»; its English label is approved with the step's strings, D16) redoes the classification with the current context for the files of a chosen scope: the whole catalog, one folder with its subfolders, or one or more file types. It redoes the classification only, from the file's existing derived data, and the file's embeddings that include it (`MDI-F102`); no other analysis is redone and the vision model and `whisper.cpp` are not called (`Q21`, 2026-10-29). Files whose classification already uses the current context version are not redone. Before anything runs, a modal confirmation states the number of files and an estimated duration (from the durations of the classification steps measured so far; stated as unknown when there are none); nothing runs until it is confirmed. The work runs as an enrichment job (`MDI-F16`, `MDI-F17`, Pause and Stop); the replaced classification is discarded and its provenance kept (D13, `MDI-F53`). A re-interpretation not finished when Katalog closes or another collection is opened is restored with its scope, as an enrichment job is (`MDI-F12`, `MDI-F93`, `MDI-C36`) (`Q24`, 2026-10-29). | [Implemented — Context 1] |
| MDI-F104 | **Knowledge base: documents (D33, answer 3).** The user adds files and folders to the collection's *knowledge base*: PDF, Word (`doc`, `docx`), OpenDocument (`odt`, `ods`), Excel (`xlsx`), `csv`, `txt`, `md`, and the other text formats of `MDI-F81`. The list shows each document's name, path, state (indexed, waiting, not reachable, or failed with the reason), passage count and indexing date. Documents are only read: never copied, moved, renamed or modified. Their text passages are stored, so the knowledge base is used while their drive is disconnected. Removing a document removes its passages and leaves the file untouched. Archive files (`zip`, `7z`, `tar`) are not knowledge-base sources: they are refused when added one by one and left out when a folder is added; spreadsheets are read as text (`Q27`, 2026-10-29). | [Planned — part implemented in Context 1] |
| MDI-F105 | **Knowledge base: indexing.** Adding documents starts their indexing in the background: text is extracted as for `MDI-F81` (`pdftotext`, KFileMetaData, `textutil` on macOS), split into overlapping passages of about 1,000 characters, and each passage is embedded by the collection's embeddings model (`MDI-F23`). Progress shows in the activity panel and on the Activity page, built with the standard builder (`MDI-C16`); Stop ends it and keeps the documents already indexed. A new indexing skips a document whose size and modified date are unchanged and redoes a changed one. Searches, Explore and the enrichment runner keep working meanwhile. | [Planned — part implemented in Context 1] |
| MDI-F106 | **Knowledge base: retrieval.** For each file classified, the knowledge-base passages most similar to the file's name, path and derived text — at most 5 by default — are given to the text model (`MDI-F100`). Passages embedded by a model other than the collection's current embeddings model are not used, and the document list shows them as needing indexing again. With an empty knowledge base the classification uses the context alone. | [Planned — part implemented in Context 1] |
| MDI-F107 | **Context editor.** K3 lets the user edit the context (`MDI-F94`), each catalog's notes (`MDI-F95`) and the knowledge base (`MDI-F104`), and start *Draft from the catalog* (`MDI-F97`) and the re-interpretation (`MDI-F103`), on a K3 page *Context* reached from the side menu's *Tools* submenu; the six main entries stay as they are (`MDI-F89`). The page is the second exception of `MDI-C18` (`Q19`, 2026-10-29). **Amended by D34:** since phase R1 the page is reached from the sidebar's *Intelligenza* group and titled *Conoscenza dell'archivio* (`MDI-F111`, `MDI-F113`); its content is unchanged until phase R4. | [Implemented — Context 1; place and title in R1 (D34)] |
| MDI-F108 | **Context and egress.** The context, catalog notes and knowledge-base passages are sent only to the collection's text and embeddings providers, and only as `MDI-C11` allows. When such a provider is not local, the `MDI-F24` confirmation of a classification, re-interpretation, draft or knowledge-base indexing names, among the content sent, the context, the knowledge-base passages and the file paths; declining sends nothing. | [Implemented — Context 1] |
| MDI-F109 | **MCP sources (context phase 2, D33 answer 3).** Katalog is a client of MCP servers configured per collection — local servers started as a program (stdio) and remote servers (Streamable HTTP) — and uses the resources and the tools the user allows as further sources for classification, recorded among the datum's sources (`MDI-F96`). Each server is enabled explicitly per collection or per job, with a warning. Open: `Q29`–`Q31`. | [Backlog] |
| MDI-F110 | **Online and cloud sources (context phase 2, D33 answer 3).** Online and cloud knowledge bases are reached through MCP servers, each enabled explicitly with the D3 warning. Any remote use is confirmed as `MDI-F24` states, appears in the Activity page's *Data sent* tile with the host (`MDI-F63`), and each call is recorded in the datum's provenance (`MDI-F52`). | [Backlog] |

### K3 redesign — design system, sidebar and Home (D34) {#redesign}

D34, phase R1. The rows were drafted from the owner's answers and then
amended to his answers of 2026-10-29 (see D34). Phases R2–R8 have their rows
under *Phases R2–R8*. The visible name, the controls style and the *My drives*
header are under *Name, controls style and My drives*.

#### Nomenclature (D34) {#nomenclature}

The fork owner approved the names in the prototype (D34, answer 3, then
«procedi»). *Gestisci i dischi* was approved on 2026-10-29 (choice 3 of
`Q36`). The English names and descriptions are the strings approved on
2026-10-29 (*User-visible text*). "—" means the entry has no description.

| K3 today | Italian name | Italian description | English name | English description |
| --- | --- | --- | --- | --- |
| — (new page) | Home | Panoramica dell'archivio | Home | Archive overview |
| Selection (and Devices from phase R7) | I miei dischi | Dischi e cartelle catalogati | My drives | Catalogued drives and folders |
| Devices (until phase R7) | Gestisci i dischi | — | Manage drives | — |
| Explore | Sfoglia l'archivio | Cartelle, anteprime, dettagli | Browse the archive | Folders, previews, details |
| Search | Cerca nei dischi | Per nome, contenuto o significato | Search the drives | By name, content or meaning |
| Create | Aggiungi un disco | Cataloga un disco o una cartella | Add a drive | Catalogue a drive or a folder |
| Context | Conoscenza dell'archivio | Contesto, glossario, documenti | Archive knowledge | Context, glossary, documents |
| Activity (page and bottom panel) | Lavori in corso | Analisi, risorse, cronologia | Work in progress | Analysis, resources, history |
| Enrichment (*AI analysis* section, *Analyze* dialog) | Analisi AI | — | AI analysis | — |
| Inspector | Dettagli del file | — | File details | — |
| Statistics | Numeri dell'archivio | — | Archive in numbers | — |
| Tags | Etichette | — | Labels | — |
| Backup | Copie di sicurezza | — | Safety copies | — |
| Settings | Impostazioni | — | Settings | — |
| — (new page, D36) | Storico delle azioni | Annulla, versioni precedenti dei cataloghi | Action history | Undo, earlier versions of the catalogs |

Sidebar groups: *Archivio* / "Archive" and *Altro* / "Other" reuse existing
strings; *Intelligenza* / "Intelligence" is new. The application itself is
named *Teca* in every language (`MDI-F123`).

#### Requirements

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F111 | **Sidebar (D34, answer 2; `Q36`).** In place of the side menu of `MDI-F89`, K3 shows a sidebar at the left of the window. It is always visible, with no pinned / floating toggle. From top to bottom it holds: (1) the collection's name, which opens the collection menu — *Open...* (*SQLite Database...*, *Collection Folder...*, *Hosted Database...*, `OPN-F9`), *New...* and the recent collections (`OPN-F7`), unchanged in content; (2) *Home*; (3) the group *Archivio* — *I miei dischi*, *Sfoglia l'archivio*, *Cerca nei dischi*, *Aggiungi un disco*; (4) the group *Intelligenza* — *Conoscenza dell'archivio*, *Lavori in corso*; (5) the group *Altro* — *Numeri dell'archivio*, *Etichette*, *Copie di sicurezza*, *Gestisci i dischi* (the Devices page, until phase R7), *Informazioni* (About); (6) at its foot, *Impostazioni* and a compact card of the running enrichment job (`MDI-F17`). The card opens *Lavori in corso* when activated, as the activity panel's row does (`MDI-F69`). Names are those of the nomenclature table, in the interface language (`MDI-F113`). Each entry shows an icon and its name; *Home* and the entries of *Archivio* and *Intelligenza* also show their one-line description. The entry of the page shown is marked. The *I miei dischi* column beside feature pages stays, with its show / hide toggle (`Q36`, choice 4). Quit stays on its standard shortcut, with no entry (D29). Behaviour at phone width is `Q41`. **Amended (D36, D37, 2026-10-30):** the group *Archivio* also holds *Storico delle azioni* / "Action history" (`MDI-F138`), after *Aggiungi un disco*, with its description. The mark follows the page shown after a Close, a Back or a Forward too (`MDI-F144`). **Defect reported 2026-10-30, fixed the same day:** after a page's Close the mark stayed on the closed page (`openFeaturePage` not reset, main agent's finding); the fix is bounded by this row. | [Implemented — R1; *Storico* entry and mark after Close, Back, Forward — D36/D37] |
| MDI-F112 | **Home page (D34, answer 2; `Q37`).** A K3 page *Home* shows the open collection at a glance: key numbers of the archive; a card per catalogued drive or folder, with info tags for its state (`MDI-F114`); the work in progress (the running enrichment job); the archive knowledge (the context, `MDI-F94`, and the knowledge base, `MDI-F104`); and shortcuts. Its cards and shortcuts only open existing pages, or start actions that already exist elsewhere, with the same behaviour and the same confirmations as there: Home adds no action of its own. **Katalog always opens on Home**: at launch and after a collection is opened. The last page is no longer restored at launch, which amends `SET-F4` for the start page (`Q37`, choice 6). Its texts are the approved strings of R1. **Amended (2026-10-30, fix under `MDI-F129`):** Home's "AI: %1 files" tags and its *Files with AI results* key number count the files with AI results as `MDI-F130` defines them. Before, they counted every file with any completed datum, quick hash included. | [Implemented — R1] |
| MDI-F113 | **Speaking names (D34, answer 3).** The sidebar entries and the page titles use the names of the nomenclature table; the sidebar entries that have a description show it (`MDI-F111`). The Selection page is titled *I miei dischi* and the Devices page *Gestisci i dischi* until phase R7. The bottom activity panel's title is *Lavori in corso* (`Q38`, choice 7). In phase R1 nothing else changes: labels, buttons and menus inside a page change with that page's phase (R2–R8). *Analisi AI* and *Dettagli del file* apply with the phases of the pages that show the enrichment and the Inspector. Names not in the table (Search results, Device edit, Backup mapping and preview, About's own page) are unchanged. Every name and description is a new string or a reused existing one (`MDI-C39`); no existing source string is edited. | [Implemented — R1] |
| MDI-F114 | **Info tags (D34, answer 1).** States and kinds are shown as info tags: a short text on a rounded, coloured background, in one of six tones — neutral, info, ok, warn, bad, and ai for AI-derived data. The tone adds to the text and never replaces it: every tag carries its text, and a tag's text is never elided (`CDT-C1`). The tones take the fixed Katalog palette of `MDI-C37` (`Q35`). In phase R1 info tags appear on Home and in the sidebar's job card. | [Implemented — R1] |
| MDI-F115 | **Look (D34, answers 1 and 5; `Q34`, `Q35`).** The sidebar, Home and every page rebuilt in a phase of D34 use Katalog's new design system in the *Control Center* style: cards on a soft background, rounded corners, one accent colour, coloured info tags (`MDI-F114`). Text sizes and colours are **fixed, as in the prototype**: they follow neither the *Text size* setting (`TYP-F2`) nor the *Theme* setting (`THM-C3`). The look follows the system's light or dark scheme, and a change of that scheme while Katalog runs, without restart. Since 2026-10-30 every page's background is the design system's ground colour, in place of the Selection page's tinted surface (`THM-F19`) and the style's window colour. Apart from that background, a page not yet rebuilt keeps its present look, and keeps following both settings, until its phase (R2–R8). **Amended (owner, 2026-10-30, D37):** text sizes are no longer fixed. They follow the *Text size* setting (`MDI-F154`). The colours stay fixed and still ignore *Theme*. | [Implemented — R1; ground on every page 2026-10-30; text size — D37] |

#### Phases R2–R8 (D34, 2026-10-30)

The owner authorised these phases with his blanket instruction of 2026-10-30
(D34). The specifications agent wrote the rows from the main agent's phase
plan; the owner did not see them. Each row is bounded by `MDI-C40`
(presentation only), `MDI-C37` and `MDI-C38`. The main agent sets a row to
`[Implemented — Rn]` when its phase lands. All seven were reported implemented
on 2026-10-30 (*Implementation status*).

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F116 | **R2 — *Sfoglia l'archivio* and *Dettagli del file*.** Restyled with the design system: `PageExploreFolders.qml`, `PageExploreFiles.qml`, `Inspector.qml`, `ColumnBrowser.qml` and `MdiDetailsDialog.qml`. They become cards and info tags. A file's classification (`MDI-F102`) — category values, AI tags and entities — shows as info tags in the *ai* tone (`MDI-F114`). Each card or panel has one primary action, and its other actions are secondary. The Inspector is titled *Dettagli del file* / "File details". The page actions read "File details" (the Inspector toggle, `MDI-F87`) and "Analyse with AI" (the *Analyze* action, `MDI-F8`); `Main.qml` applied both renames early, on 2026-10-30. Unchanged: every row of `SpecExplore.md`, `MDI-F44`–`F46`, `MDI-F87`, `MDI-F88`, `MDI-F102`, the data shown, the view choice and the Inspector's visibility, and the keyboard rules (`KBS`, `CDT-O3`, `CDT-C7`, `CDT-C8`). `Q18` (the Explore part) is answered by this row. | [Implemented — R2] |
| MDI-F117 | **R3 — *Cerca nei dischi*.** Restyled with the design system: `PageSearchForm.qml`, `PageSearchResultsForm.qml` and `SearchTermList.qml`. The form presents the existing ways to search as described choices: **by name** (the text criteria with their existing *File names only* / *File names or Folder paths* / *Folder path only* option, `SpecSearchList.md`) and **by meaning** (the semantic mode, `MDI-F47`). The other criteria are grouped in described cards, each with a title and a one-line description, holding the same controls. Results show state tags drawn only from data they already hold. Unchanged: every criterion and option, its validation, the search history and its restore (`SEL-F1`, `SEL-F2`), the results' columns, sort, column widths (`CDT-F12`), filters (`SpecSearchResultsFilters.md`), export, context menus, the keyboard shortcuts (`KBS-F5`, `KBS-F6`), the instant index rules (`MDI-F77`, `MDI-C23`), and `MDI-F47`–`F49`. A third way, **by content**, has no existing search option; it is `Q45` and is not authorised by this row. | [Implemented — R3] |
| MDI-F118 | **R4 — *Conoscenza dell'archivio*.** `PageContext.qml` is restyled in three parts as cards: the collection's context (`MDI-F94`), the catalog notes (`MDI-F95`) and the knowledge base (`MDI-F104`). Beside them sit two cards for the page's actions: *Re-interpret with the context* (`MDI-F103`) and the draft from the catalog (`MDI-F97`), labelled "Propose a draft". Unchanged: every field and its save, the draft's open-for-editing and discard behaviour, the re-interpretation's scope and its confirmation with count and estimate, the knowledge-base list with its states, refusals and indexing (`MDI-F104`–`F106`), and the egress confirmation (`MDI-F108`). | [Implemented — R4] |
| MDI-F119 | **R5 — *Lavori in corso*.** `PageActivity.qml` is restyled with the design system. A hero card holds the running job: its status line, Pause/Resume and Stop (`MDI-F62`). Then come the resource tiles (`MDI-F63`), the "right now" card (`MDI-F64`), the tools card (`MDI-F65`), the queue (`MDI-F66`), the log of finished steps with each outcome as an info tag (`MDI-F67`) and the history charts (`MDI-F68`). `OperationQueueView.qml` gets a compact restyle. Unchanged: what each part shows and does, the waiting state (`MDI-F91`), the bottom panel's rows, Stop buttons and Clear (`SpecOperationQueue.md`, `MDI-F17`, `MDI-F69`), and the builder-made status text (`MDI-C16`). | [Implemented — R5] |
| MDI-F120 | **R6 — *Aggiungi un disco*.** `PageCreateForm.qml` and `CreateWizard.qml` are restyled as described cards: numbered sections, all on **one page** (`MDI-F60`, D15 — nothing hidden behind a *Next* action). The connected drives (`MDI-F90`) show as drive cards with state tags and *Mount read-only*. The page's *Create* action reads "Catalogue the drive"; `Main.qml` applied the rename early, on 2026-10-30. Unchanged: both modes and their switch (`MDI-F1`), every field, the validation by inline message (`MDI-F6`, `SpecValidationRules.md`), the creation path and the queue (`MDI-F7`, `MDI-C2`), the empty-source confirmation dialog, and the enrichment options (`MDI-F3`–`F5`). `Q18` (the Create part) is answered by this row: the cards keep the mode switch. | [Implemented — R6] |
| MDI-F121 | **R7 — *I miei dischi* and *Gestisci i dischi*.** Restyled with the design system: `PageSelectionView.qml` and `PageSelectionDelegate.qml` become drive cards with state tags; `PageDevicesView.qml`, `PageDevicesViewDelegate.qml`, `PageDeviceEditForm.qml`, `DeviceTreeComboBox.qml` and `DeviceIdentity.qml` are restyled. Unchanged: what the Selection cards say and do (`SEL-F1`–`F8`, `SEL-C1`–`C11`: the selection reminder, its scroll, the context menu, the second line's content, the shared identity component and its icon rules), the Devices page's views, sort, table and stored view, the edit form's fields and validation, the deletion confirmation, and the bigger-icon setting (`THM-F7`). The two pages stay separate. Merging them, which choice 3 of `Q36` anticipates, is a change of behaviour that this row does not authorise. **Amended (owner, 2026-10-30):** the header and the used-space label of `MDI-F125`. | [Implemented — R7] |
| MDI-F122 | **R8 — *Altro* and *Impostazioni*.** Restyled with the design system: `PageStatisticsForm.qml` (*Numeri dell'archivio*), `PageTagsForm.qml` (*Etichette*), `PageBackupForm.qml`, `PageBackupMappingForm.qml` and `PageBackupPreviewForm.qml` (*Copie di sicurezza*), `PageSettings.qml` and `MetadataDialog.qml`. Unchanged: every row of `SpecBackup.md` (backup is add-only, `BKP-C1`) with its confirmations; the statistics' data and chart (`THM-F14`); the Tags page's behaviour; the Settings tabs, every setting and its key (`SpecSettingsPage.md`, `MDI-C7`); and what the metadata dialog shows. Whether *Theme* and *Text size* stay in Settings once every page ignores them is `Q46`. | [Implemented — R8] |

#### Name, controls style and *My drives* (D34, 2026-10-29/30)

These rows record the owner's decisions as the main agent relayed them (see
D34). The code came before the rows. The constructional rows are `MDI-C41` and
`MDI-C42`.

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F123 | **Visible name *Teca* (owner, "Nome visibile", 2026-10-29).** K3 shows the name *Teca* wherever it names the application. That covers the window title (the application display name), About (display name, plus the credit "Teca is based on Katalog by Stéphane Couturier, released under the GNU GPL."), the title of every dialog, and the name and monogram at the top of the sidebar. On macOS the build is `Teca.app` with the executable `Teca` (CMake `OUTPUT_NAME` and `MACOSX_BUNDLE_BUNDLE_NAME`). Existing strings that named the application now say *Teca*: the first-run welcome; the alpha-warning welcome, reworded as "Welcome to Teca. Teca is based on Katalog: …"; the NTFS mount-failure line; and R1's "Teca reads the drive, NTFS too, without changing it.". Strings that name Katalog's own things keep the name: the "Katalog Colors" theme and the "Katalog / File" and "Katalog / Memory" export formats. Editing these existing strings is the owner's exception to `MDI-C17`, `MDI-C39` and `MDI-C40` ("no existing source string is edited"), and it covers these strings only (*User-visible text*). | [Implemented — 2026-10-30] |
| MDI-F124 | **Dialogs and the page header in the design system (D34, 2026-10-30).** Every `Controls.Dialog` is painted on the design system's surface, with its line colour for frames (`palette.window: KTheme.surface`, `palette.mid: KTheme.line`). Every `Kirigami.Dialog` and `Kirigami.PromptDialog` has a rounded `KTheme.surface` background. The Kirigami page header bar is painted with the ground colour as soon as it is created (`Main.qml` `styleHeader`), and its separator is off. *Home* shows no Close action, and `Esc` still works there (`KBS-F1`, *Specs to amend*). Nothing else in the dialogs changes (`MDI-C40`). **Amended (D37, confirmed by the owner 2026-10-30):** every page reached from the sidebar loses its Close action (`MDI-F146`), and `Esc` is Back (`MDI-F144`). | [Implemented — 2026-10-30; amendment — D37] |
| MDI-F125 | ***My drives* header and drive cards (R7, owner, 2026-10-30).** The header of *I miei dischi* has five parts. (1) A description line, "The drive or folder chosen here is the one searched, browsed and counted." (a new string). (2) The selection reminder, a rounded row on the accent-soft colour. It still has no label of its own, and its behaviour is unchanged (`SEL-F5`, `SEL-F6`, `SEL-C7`). (3) The filter field, now rounded. (4) *Show device info*, now a visible switch whose label is its existing string. Before, it was a bare check box with that string as its tooltip. (5) Collapse and expand as framed icon buttons. On a drive card, the used-space info tag shows the existing K2 label "Used Space: " before its figure. Each part of the card's details is now a tag of its own, and the bare figure read as a second copy of the file size. These choices depart from three earlier rows for the fork's K3. The label goes against `SEL-C11`, which kept the figure unlabelled. The reminder's idle accent-soft surface replaces the transparent idle surface of `THM-F10`. The description line sits above the reminder, which `SEL-F5` places "at the very top" with no label, heading or title before it (*Specs to amend*). Otherwise `MDI-F121` holds. | [Implemented — R7] |

#### Browse never empty (D34 follow-up, 2026-10-30)

The owner's choice of 2026-10-30 (see D34). New behaviour, bounded by
`MDI-C44`. The owner's wording speaks of no drive being chosen. Three points
of `MDI-F129` are the specifications agent's reading of that wording (`Q50`):
entering Browse opens the catalog chosen; a Storage or Virtual choice shows
the catalogs within it; a choice that holds no catalog shows them all.
`MDI-F132`–`F134` were built beyond the option and kept after the owner's
answer to `Q51`, on the main session's reading of it (see D34). `MDI-F135` is
the owner's own request in that answer.

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F129 | **Catalogs to open when Browse has nothing to show.** Entering *Sfoglia l'archivio*, from the sidebar or from Home's *Browse*, opens the catalog chosen in *I miei dischi*. *Sfoglia l'archivio* can have no catalog open: nothing is chosen in *I miei dischi*, or the choice is *All*, a Storage or a Virtual group and no catalog has been opened. The page then shows a card per catalog to open, in place of "No folder selected". The cards cover every catalog of the collection when nothing or *All* is chosen or when the choice holds no catalog, and the catalogs within the choice otherwise. With no catalog in the collection, the page says so. Each card shows the catalog's name, its path, its file count and its count of files with AI results as `MDI-F130` defines them. These are the same figures as on Home's drive cards, and they are absent where Home leaves them out (`MDI-C38`). The card's *Open* action selects that catalog and opens it in Browse with its folders, as Home's *Browse* and the Selection card's *Explore* do (`MDI-F112`, `SEL-F3`, `EXP-F27` (c)). A catalog already open stays open when a Storage or Virtual device is chosen (`EXP-F27` (a)). | [Implemented — D34 follow-up] |
| MDI-F130 | **"AI" tag on files with AI results.** In Browse's file rows, in the list view and in the column view (`MDI-F88`), a file with at least one completed caption, summary, transcript or classification carries the info tag "AI" in the *ai* tone (`MDI-F114`). A file carries none when its AI results are all failed or skipped, or when it has only previews, technical data, a text snippet, an archive listing or a quick hash. The mark of `MDI-F44` is unchanged. | [Implemented — D34 follow-up] |
| MDI-F131 | **What Browse is for.** A description line at the top of *Sfoglia l'archivio* says what the page is for. It also says that clicking a file shows its preview, its AI description and its transcript in *Dettagli del file* (`MDI-F87`). Its wording is a new string (`MDI-C44`). | [Implemented — D34 follow-up] |
| MDI-F132 | **"Choose another drive" (`Q51`).** While a catalog is open in *Sfoglia l'archivio*, a "Choose another drive" link beside its name brings back the card list of `MDI-F129`, with the same cards and the same *Open* action. **Superseded (D37, 2026-10-30):** the root link "All the drives" of Browse's breadcrumb brings the card list back (`MDI-F145`), and the link "Choose another drive" is no longer shown. The behaviour stays; only its place and label change. | [Removed — superseded by `MDI-F145` (D37)] |
| MDI-F133 | **A folder that is a catalog of its own (`Q51`).** In *Sfoglia l'archivio*, a folder can have a path that is the source path of another catalog, as a sub-folder of a split catalog has. In the column and list views such a folder carries the info tag "Catalog" in the *info* tone (`MDI-F114`). Opening it, in the column, tree or list view, opens that other catalog in Browse. The choice in *I miei dischi* does not change, as with Explore's other direct opens, so what Search covers does not change either (`MDI-F49`). | [Implemented — D34 follow-up; kept on the main session's reading, `Q51`] |
| MDI-F134 | **"Add a drive" in an empty collection (`Q51`).** When the collection has no catalog, the card list of `MDI-F129` offers "Add a drive" beside its message. It opens *Aggiungi un disco*. | [Implemented — D34 follow-up; kept on the main session's reading, `Q51`] |
| MDI-F135 | **A click selects a file; a double click opens it (owner, 2026-10-30).** In Browse's list view, a single left click on a file only selects it, and *Dettagli del file* shows it with its thumbnail preview (`MDI-F87`). The file is not opened. It opens with a double click, or with Enter (`CDT-F7`). A single click on a folder still opens the folder. The column view already selected a file on a click and opened it with Enter (`MDI-F88`). Search results are unchanged. The owner's request: "quando seleziono ina foto da esplora, non si deve aprire la foto in anteprima, ma solo l'anteprima thumb." He spoke of photos, and the rule covers every file: that, and the double click to open, are the main session's reading. **Deliberate divergence from K2**, where a single click on a file opens it (`on_Explore_treeView_FileList_clicked`). It changes this click only, which `MDI-F116` had kept unchanged. | [Implemented — D34 follow-up] |

### Catalog history and recomposition (D36) {#history}

The owner's answers of 2026-10-30 (see D36). The core part is approved
(`MDI-C46`). Where a row states a confirmation that the answers did not name,
it applies the `CLAUDE.md` rule for destructive actions.

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F136 | **A restore point before each change of a catalog (D36, answer 1).** Before K3 updates, splits, recomposes (`MDI-F142`), deletes or edits a catalog, it records a *restore point* of each catalog concerned. The restore point holds: its device row and those of its virtual copies; its catalog row; its file and folder lists; its enrichment profile (`MDI-F8`). It also holds the date and a description of the action. Restore points are written to `teca_history.db` (`MDI-C45`); they do not change the collection database. In a Hosted collection without a local collection folder no restore point is taken, and *Storico* says that history is not available there. What happens when a restore point cannot be written is `Q52`. | [Implemented — D36] |
| MDI-F137 | **Ten restore points per catalog (D36, answer 2).** The last 10 restore points of each catalog are kept. When a newer one is recorded, the oldest beyond the limit is removed automatically. The number is a setting in Settings; a new key only adds to the settings file (`MDI-C7`). Restore points that cover several catalogs: `Q52`. | [Implemented in part — D36: the limit is fixed at 10; no Settings control yet] |
| MDI-F138 | **The *Storico* page (D36, answer 1).** A K3 page *Storico*, reached from a sidebar entry (`MDI-F111`), lists the recorded actions of the collection, each with its date and its description. It can also show the actions of one catalog only. Each entry offers *Ripristina com'era prima* (`MDI-F139`) and *Apri questa versione come catalogo a parte* (`MDI-F141`). The page is the fourth exception of `MDI-C18`. Its names and labels are the owner's Italian; their English source is approved with the step's strings (`MDI-C39`). As built: the entry is *Storico delle azioni* / "Action history" in *Archivio*, with the icon `view-history`; the page also holds the proposals of *Ricomponi* (`MDI-F142`). | [Implemented in part — D36: no view of one catalog's actions yet] |
| MDI-F139 | **Restore as it was before (D36, answer 1).** *Ripristina com'era prima* puts the catalogs of an entry back as its restore point recorded them. First, a modal dialog says which catalogs are replaced and by which version. Nothing changes until it is confirmed (`CLAUDE.md`, destructive actions). Then Teca: (1) records a restore point of the current state (`MDI-F136`), so a restore can itself be restored; (2) removes from the collection the catalogs that resulted from the action; (3) inserts the recorded device, catalog, file, folder and profile rows with their original IDs; (4) moves the files' AI data back to the restored catalogs by full path, with the rule of `MDI-F126` (a colliding row stays where it is; nothing is overwritten or deleted). Nothing is read, written, moved or deleted on any drive, and no derived file moves (`MDI-C10`). An original ID already in use: `Q52`. | [Implemented — D36] |
| MDI-F140 | **Annulla right after an action (D36, answer 1).** Right after an action that recorded a restore point ends, Teca offers *Annulla* for it. *Annulla* restores that restore point as `MDI-F139` does. Whether it skips the confirmation of `MDI-F139` is `Q52`. | [Implemented — D36] |
| MDI-F141 | **Open a version as a separate catalog (D36, answer 1).** *Apri questa versione come catalogo a parte* adds the recorded version of a catalog to the collection as a new catalog, with new IDs and the recorded name followed by a suffix (a new string, `MDI-C39`). The current catalog and its AI data do not change. The new catalog gets no AI data: previews, descriptions, transcripts and classifications are not copied. From then on it is an ordinary catalog (consequences: `Q52`). | [Implemented — D36] |
| MDI-F142 | **Recompose a split catalog — *Ricomponi* (D36, answer 3 (2)).** Teca proposes catalogs to put back together, with the name and the source path of the catalog they would form. A modal dialog asks for confirmation before anything changes. Then: (1) a restore point of every catalog concerned is recorded (`MDI-F136`); (2) one new catalog is created, whose source path is the catalogs' common parent path, with sub-directories included and a valid update date (`MDI-F143`); (3) the old catalogs' file and folder rows move to it; (4) their AI data moves to it by full path, with the rule of `MDI-F126`; (5) the enrichment profile is carried over (`MDI-F127`); (6) the old catalogs' devices are removed from the collection. Its instant search index is built in the background, as for a new catalog (`MDI-F128`). Nothing is read, written or deleted on any drive, and no derived file moves. Which catalogs are offered together, the name, and the profile when they differ: `Q53`. First use: the owner's 22 UITS catalogs into "CATALOGO BASE UITS DISCO RIGIDO" (D36, answer 4), done on 2026-10-30 (*Implementation status*). | [Implemented — D36] |

### Navigation (D37) {#navigation}

The main session's design for the owner's request (see D37). The owner
confirmed `MDI-F144`–`F146` on 2026-10-30 (D37, second round); `MDI-F147` and
`MDI-F148` were not put to him. The specifications agent's readings are listed
in `Q55`.

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F144 | **Back and Forward (D37).** Teca keeps a history of the places visited: the pages and, in *Sfoglia l'archivio*, each location (a catalog and a folder). *Back* returns to the previous place and *Forward* to the next one. Going to a new place after a Back drops the places ahead. They are buttons beside the page title, in the page header, and keys: `Esc` for Back; `Cmd+[` and `Cmd+]` on macOS; `Alt+Left` and `Alt+Right`; and the mouse's Back and Forward buttons (owner, 2026-10-30). With nothing behind, Back does nothing and its button is disabled; likewise Forward with nothing ahead. A place that no longer exists is skipped. The history starts empty when Teca starts and when a collection is opened (Teca opens on *Home*, `MDI-F112`), and is not stored. The sidebar marks the entry of the page shown after every Back and Forward (`MDI-F111`). These still hold: `KBS-F2` (on Device Edit and Create, leaving discards unsaved input as Cancel does), `KBS-F3`, `KBS-F4` (a popup keeps its own `Esc`), `KBS-C3` (one press, one step) and `KBS-C4` (`Esc` never stops an operation). | [Implemented — D37] |
| MDI-F145 | **Breadcrumb in Browse (D37).** *Sfoglia l'archivio* shows where it is as a breadcrumb: `All the drives › <catalog> › <folder> › …`. Its root is the existing string "All the drives" (*Tutti i dischi*). Every segment can be activated with the mouse and the keyboard. The root shows the catalogs to open (`MDI-F129`), which replaces the link of `MDI-F132`; the catalog's name opens its root folder; a folder opens that folder. An *Up* action opens the parent folder; at a catalog's root it shows the catalogs to open. Each place reached is a place of `MDI-F144`'s history. | [Implemented — D37] |
| MDI-F146 | **No Close on the sidebar's pages (D37).** The pages reached from a sidebar entry show no page-header Close action; Back (`MDI-F144`) and the sidebar replace it. Other pages keep their Close or Cancel action: the sub-pages and the overlaid windows (owner, 2026-10-30). `Esc` is Back on every page. This amends `KBS-F1`, `KBS-C1` and `MDI-F124` (*Specs to amend*). It was option (c) of `Q48`, confirmed by the owner on 2026-10-30. | [Implemented — D37] |
| MDI-F147 | **Scope chip (D37).** *Cerca nei dischi*, its results, *Sfoglia l'archivio*, *Numeri dell'archivio* and *Copie di sicurezza* show a scope chip: it says what the choice in *I miei dischi* is, and offers *Cambia*, which opens *I miei dischi*. The chip only shows the choice and never changes it. What each page acts on is unchanged (`SpecSelection.md`, `MDI-F49`). Which choice the results page shows: `Q55`. As built: "Search in: %1", "Numbers of: %1" and "Links of: %1" on Search, Numbers and Safety copies. Results have none, since they may reflect an earlier choice (`Q55`); Browse has none, and shows its place by the breadcrumb (`MDI-F145`). **Drift:** the chip also offers "All the drives", which changes the choice (finding, *Implementation status*). | [Implemented in part — D37: three of five pages; drift on "All the drives"] |
| MDI-F148 | **Keyboard jumps (D37).** `Cmd+1` … `Cmd+9` on macOS, and `Ctrl+1` … `Ctrl+9` elsewhere, open the first nine sidebar entries in the sidebar's order. `F6` moves the keyboard focus from the sidebar to the page and back. Neither acts while a popup, menu or dialog is open (as `KBS-F4`). | [Implemented — D37] |

### Accessibility (D37) {#accessibility}

WCAG 2.2 level AA is the bar (D37). The success criteria named are the guide
for each row's test. The main session's design; the owner did not see these
rows, except `MDI-F154`, which he confirmed on 2026-10-30 (D37, second round).

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F149 | **Visible control edges (D37; SC 1.4.11).** In light and in dark, these have a contrast ratio of at least 3:1 against the colours next to them: the boundary of every text field, combo box, spin box, button, check box, radio button and switch; the focus ring; the highlight of the current menu item. The line colour of controls is the token `controlLine` (`MDI-C47`). | [Implemented — D37] |
| MDI-F150 | **Visible focus (D37; SC 2.4.7).** Every control that takes keyboard focus shows a focus ring while it has it. That includes the design-system components (`KButton`, actionable `KCard`s, `KSidebarItem`, the drive cards) and every `TecaStyle` control, in its checked state too. | [Implemented — D37] |
| MDI-F151 | **States not by colour alone (D37; SC 1.4.1).** The sidebar's current entry is marked by a bar and a heavier text weight as well as by colour, and assistive technology reads it as a selected page tab. A checked check box, radio button or switch differs from an unchecked one by more than colour. A disabled `KButton` is dimmed. As built, every disabled control is dimmed, through `palette.disabled` and the `TecaStyle` controls' opacity (`MDI-C42`). | [Implemented — D37] |
| MDI-F152 | ***I miei dischi* by keyboard (D37; SC 2.1.1).** The drive cards of *I miei dischi* can be focused, moved between and chosen with the keyboard alone. Choosing a card with a key gives the same result as a click (`SEL-F1`–`F8` unchanged). | [Implemented — D37] |
| MDI-F153 | **Names, roles and values (D37; SC 1.1.1, 4.1.2).** Every icon-only button and every list and table row gives assistive technology an accessible name, built from existing strings. `KProgressBar` exposes the progress-bar role and its current value. The Inspector's image has a text alternative, built from data the Inspector already shows. | [Implemented — D37] |
| MDI-F154 | **Text that follows *Text size* (D37; SC 1.4.4).** The design system's text sizes are multiplied by `KTheme.textScale`, which follows the *Text size* setting (`TYP-F2`). The sidebar, *Home* and every redesigned page then scale with it. This reverses the owner's choice of `Q34` ("Fisse come nel prototipo") for text size, and amends `MDI-F115` and `MDI-C37`. **Confirmed by the owner, 2026-10-30:** "Seguano l'impostazione (Consigliato)". As built, `Main.qml` binds `KTheme.textScale` to the app's text scale (`cardScale`, the *Text size* value, 0.8 to 1.2). SC 1.4.4 asks for text up to 200 %, beyond the slider's 1.2 (`TYP-F9`); widening the range is still open (`Q46`). | [Implemented — D37; range beyond 1.2 open, `Q46`] |
| MDI-F155 | **Limited motion (D37; SC 2.3.3 as the guide).** Animations are short and few. None is needed to see a state or to use a control. | [Implemented — D37] |
| MDI-F156 | **Nothing behind the activity panel (D37; SC 2.4.11).** The bottom *Lavori in corso* panel takes its own space. No page content and no focused control is ever covered by it. As built, the panel sits below the pages instead of over them. | [Implemented — D37] |

### Destructive actions — K2 parity {#destructive-parity}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F157 | **Move to Trash asks first (K2 parity, `CLAUDE.md`).** In *Sfoglia l'archivio*, *Move file to Trash* and *Move folder to Trash* open a modal confirmation before anything moves. So does *Move to Trash* in Search results. *Cancel* moves nothing. The texts are K2's, verbatim. In Browse: the title "Confirmation", the question `"Move\n%1\nto the trash?"` with the path, then `"Moved to trash:<br/>"` with the path, or "Move to trash failed." (`mainwindow_tab_explore.cpp`, `exploreContextMoveFileToTrash`). In Search results: the title "Confirmation" and "Move this file to the trash?" with the path (`mainwindow_tab_search_ui.cpp`, `searchContextMoveFileToTrash`). The confirm button reads "Move to Trash", an existing string. Search results keep their existing K3 result notifications. Results show as passive notifications (`MDI-C17`). Before 2026-10-30 both acted at once. | [Implemented — 2026-10-30, `dd7881df`] |

## Constructional requirements — *how it is built / limits / MUST-NOTs*

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-C1 | **Fork-only, K3-only (D1).** No file under `qt_widgets/` is changed. Nothing in this spec is proposed upstream. An enriched collection stays a schema 3.0 collection: K2 and upstream K3 3.0 open it normally and ignore the `mdi_` tables and `mdi_` files (D5, `MDI-C15`). | [Planned] |
| MDI-C2 | Catalog creation and device update MUST NOT change behaviour: the runner is not called from, hooked into, or run inside `CatalogJobStoppable`, `CatalogManager` or `DeviceUpdateManager`. The wizard reaches creation only through the existing entry point used by standard mode. | [Implemented — Phase A] |
| MDI-C3 | **Coexistence with `SpecOperationQueue.md`.** The runner executes on its own `QThread`, like the backup run (`BKP-C11`), and is **not** one of the operations `OPQ-C3` and `OPQ-C4` govern: it is not a queue entry, never enters, starts, advances or drains the operation queue, and is unaffected by its Clear. `OPQ-C1` holds because the runner never schedules a creation or an update. Its presence does not count as "an operation running" for the `OPQ-C15` guards of creation, update, search or Explore load. The only interactions are `MDI-F15` (pausing on an update of the same catalog) and `MDI-F17` (its row in the activity panel), which require the `OPQ-C16` amendment listed under *Specs to amend*. | [Implemented — Phase A] |
| MDI-C4 | **Derived-data store (D17).** Derived data (every datum with its provenance, and the embeddings) is written to `mdi_data.db` in the collection folder, never to the collection database. The runner opens its own connection to that file on its own thread; the UI reads it through a separate connection on the main thread; neither uses the collection's main connection for derived data. The file uses WAL journal mode and a busy timeout. The runner commits each analysis's result on its own (`MDI-F9`). A search, Explore load or update running on the main thread MUST NOT fail or stall for more than one file's commit because of the runner. | [Implemented — Phase A] |
| MDI-C5 | **Memory mode.** The in-memory collection database is never shared with the runner's thread. Derived data lives in `mdi_data.db` exactly as in File mode (`MDI-C4`), so no derived table is loaded into the in-memory database. The enrichment profile (`mdi_profile`) stays in the in-memory database, is persisted to `mdi_profile.csv` in the collection folder and is loaded when the collection is opened, before any query on it (Memory Mode Caveat in `CLAUDE.md`). | [Implemented — Phase A] |
| MDI-C6 | **Hosted mode.** The runner opens its own connection with the collection's already validated host settings. Derived-file locations are stored relative to the destination path, which is recorded per catalog, so a second machine can point to its own copy. Whether several machines share one destination path or use one each is `Q11` (deferred). | [Planned] |
| MDI-C7 | External tools are started with `QProcess` using a program path and an argument list — never through a shell or a command string. Standard output and error are captured; the process is terminated on Stop, on time limit and on application exit. Tool paths are application settings (machine-specific); per-collection choices (profile, provider enablement) are stored with the collection. New settings keys only add to `katalog3_prerelease_settings.ini`; existing keys keep their meaning, so a beta settings file stays readable. | [Implemented — Phase A] |
| MDI-C8 | No new linked library and no new Qt or KF6 module: `ffmpeg`, `whisper.cpp` and poppler are external programs; HTTP uses `Qt6::Network`; no vector-index library, no keychain library, no Qt Multimedia. Adding any of these needs explicit user approval and an amendment here first. **Exception (D25, 2026-10-29):** KArchive (`KF6::Archive`), optional, under `MDI-C25`; this exception covers that library only. **Addition (D32, 2026-10-29):** on macOS only, the system program `/usr/bin/textutil` (`-convert txt -stdout`) is an approved external program, for the text of `.doc` files when KFileMetaData returns none; it is started as `MDI-C7` requires, under the time limit of `MDI-F22`, is not detected or configurable (`MDI-F20`), and is a platform-specific program, not a platform-specific API (`MDI-C19`). | [Implemented — Phase A] |
| MDI-C9 | Embeddings are stored in the database with the model name, model version and dimension. Similarity (cosine) is computed in `core/` by scanning stored vectors in batches; the scan is stoppable between batches and reports progress. A performance target is `Q14`, decided in Phase D. | [Planned — part implemented in Phase D] |
| MDI-C10 | The runner MUST NOT write, modify, rename or delete anything on the cataloged source. It reads source files only, and only while their device is connected. All derived files go under the destination path, which is validated as outside the source (`MDI-F6`). | [Implemented — Phase A] |
| MDI-C11 | Katalog MUST NOT send any content, query or metadata off the machine except to a provider enabled and confirmed under `MDI-F23`/`MDI-F24`/`MDI-F50`. With default settings the only network destination is `localhost`. No telemetry is added. | [Implemented — Phase A] |
| MDI-C12 | Remote providers MUST NOT receive an original file — only derived inputs (thumbnails, sampled frames, rendered pages, extracted or transcribed text). Transcription is local only (`whisper.cpp`); no remote speech-to-text. | [Planned — part implemented in Phase C] |
| MDI-C13 | An API key MUST NOT be written to any file, `.ini` settings file, database or log. It is held in memory for the session only, or read from an environment variable at use (D10). Keys and authorization headers are never written to logs or reports. | [Planned] |
| MDI-C14 | **Core boundary.** All SQL, tool invocation, HTTP requests, prompt construction, cost computation and similarity live in `core/`. `qt_quick/` contains no raw `QSqlQuery` and only orchestrates and displays. Every new `core/` class or method still needs explicit user approval before it is written (`CLAUDE.md`); approval of this spec is not approval of a core design. | [Implemented — Phase A] |
| MDI-C15 | **`mdi_` tables outside the core schema (D5).** The core schema stays 3.0: this fork MUST NOT change the core schema version, never stamps 3.1, and MUST NOT edit `runMigration_3_0`. The new tables (at least: enrichment profile, enrichment job, derived datum with provenance, derived file, transcript passage, topic segment, topic cluster and membership, embedding) all have the prefix `mdi_`. The enrichment profile table (`mdi_profile`) and a `mdi_schema` record are in the collection database; the derived-data tables, the enrichment job table `mdi_job` (`MDI-C28`) and their own `mdi_schema` record are in `mdi_data.db` (D17, `MDI-C4`). Their own version ("media intelligence schema version 1") is stored separately from the core schema version, in a single-row `mdi_schema` table or equivalent. A separate migration creates the `mdi_` tables when they are absent and upgrades them when their version is older; it is idempotent and touches only `mdi_` tables. No 3.0 table or column is altered, added to or removed. File and Memory modes add the new `mdi_data.db` to the collection folder; Memory mode also adds `mdi_profile.csv`. No existing file format changes. | [Planned — part implemented in Phase A] |
| MDI-C16 | Every progress and status message is built with `StatusBarMessageBuilder` (`SpecProgressReport.md`); no raw status string. | [Implemented — Phase A] |
| MDI-C17 | **User-visible text.** New strings are written in English while a step is built, reusing existing K2/K3 strings wherever they fit; the fork owner approves all of a step's new strings together at the end of the step, before its commit, and translations run after that approval (D16). Existing strings are never changed without per-string approval. Copy states what is needed, without "Please" or orders. Blocking validation uses `Kirigami.InlineMessage`; decisions (egress confirmation, deletions) use modal dialogs; transient success uses `showPassiveNotification`. No string is approved by this spec; approved strings are listed under *User-visible text*. | [Implemented — process, D16] |
| MDI-C18 | **UI structure.** No new Page: the wizard is a mode of the existing `PageCreateForm.qml`, derived data is shown inside the existing Explore and Search results pages, provider and tool settings inside the existing Settings page. Creating any new `.qml`, `.cpp` or `.h` file needs the user's explicit request first. **Exception (D19, 2026-10-28):** the *Activity* page (`MDI-F61`) is approved as a new Page. **Exception (D33, 2026-10-29):** the *Context* page, `qt_quick/PageContext.qml` (`MDI-F107`), reached from the *Tools* submenu. **Exception (D34, 2026-10-29):** the *Home* page, `qt_quick/PageHome.qml` (`MDI-F112`). These three exceptions cover those pages only. **New files approved by the fork owner (2026-10-28/29), none of them a Page:** `core/mdifts.{h,cpp}` (D22), `core/mdivolumes.{h,cpp}` (D30), `qt_quick/Inspector.qml` (D27), `qt_quick/ColumnBrowser.qml` (D28), and the design-system files of `MDI-C37` (D34, phase R1; `KButton.qml` covered by the owner's instruction of 2026-10-30, `Q43`), and the `TecaStyle` module files of `MDI-C42` (owner, 2026-10-30). **Exception (D36, 2026-10-30):** the *Storico* page, `qt_quick/PageHistory.qml` (`MDI-F138`), a fourth exception that covers this page only; and the core files `core/cataloghistory.{h,cpp}` (`MDI-C46`). | [Planned] |
| MDI-C19 | **Cross-platform.** Tools are resolved with the platform's executable suffix; paths are built with Qt path functions. Derived-file names are short identifiers, not source names, so they stay within Windows path limits and cannot collide. No platform-specific API is used in phases A–D. | [Planned — part implemented in Phase A] |
| MDI-C20 | HTTP requests have a time limit (default 120 s, configurable) and are aborted on Stop. A provider response that cannot be parsed into the expected fields is recorded as a failed datum with the reason, never stored as a result. | [Planned — part implemented in Phase B] |
| MDI-C21 | **Resource monitor sources.** The resource monitor (`MDI-F70`) obtains its values on macOS through the external programs `ps`, `vm_stat`, `sysctl`, `memory_pressure`, `ioreg` and `pmset`; on Linux by reading `/proc` and running `ps`; Ollama's loaded models through its `/api/ps` over `Qt6::Network`. Programs are started as `MDI-C7` requires; no library is linked (`MDI-C8`). On Windows no platform-specific API is used; values not obtainable otherwise are unavailable (`MDI-F73`). The monitor runs on its own thread and never on the UI thread or the runner's thread. | [Planned — part implemented in Activity phase] |
| MDI-C22 | **Instant search index store (D22, D5).** The index is the FTS5 table `mdi_fts` (trigram tokenizer) with `mdi_fts_doc` and `mdi_fts_state` in `mdi_data.db`, written by `core/mdifts.*`, never in the collection database. Its per-catalog stamp is `catalog_date_updated` normalised to `yyyy-MM-dd hh:mm:ss`; an index whose stamp differs from the catalog's is stale and not used. The rebuild runs on its own thread, never on the UI thread. No SQLite trigger maintains it (a trigger cannot write another database file, and an update rewrites all the catalog's rows). FTS5 `detail=column` is not used (it rules out phrase queries). | [Implemented — increment 3] |
| MDI-C23 | **Search fast path (D22).** The prefilter is in `SearchJobStoppable::searchFilesInCatalog`: `mdi_data.db` is attached as `mdi` and the file query is restricted to `file_full_path IN (… MATCH …)`. The prefilter MUST match a superset of the files the search criteria match, so the index never changes a result set. It is active only when the data path is set (`setMdiDataPath`), which K3 does and K2 never does: K2's search is unchanged (`MDI-C1`). | [Implemented — increment 3] |
| MDI-C24 | **Image decoding (D23).** Images are decoded in-process by Qt (`QImageReader`, scaled decode, EXIF auto-transform) in a parallel pre-pass on a `QThreadPool` of at most 4 threads, with QtCore only (no QtConcurrent); `ffmpeg` is used only for formats Qt cannot read. The pre-pass is the one exception to the one-file-at-a-time rule of `MDI-F9`. A replaced derived file is deleted only after the new datum is committed (D13, `MDI-F14`). | [Implemented — increment 3] |
| MDI-C25 | **KArchive, optional (D25).** `KF6::Archive` is linked to `katalog-core` only when CMake finds it, and then defines `KATALOG_HAS_KARCHIVE`; without it the build succeeds and the archive analysis is skipped (`MDI-F82`). | [Implemented — increment 3] |
| MDI-C26 | **Quick hash (D26).** `FileChecksum::computeQuickHash`: a file of at most 192 KB is hashed whole; a larger one as its size (little-endian) + its first 64 KB + 64 KB at size/2 + its last 64 KB; BLAKE2b-256. Quick hashes and the pass stamp (`mdi_pass`) are stored in `mdi_data.db` only: no core column is added (`CKS-C4`, D5), the catalog scan is unchanged, and there is no "Quick" value of the catalog's checksum option. In the duplicates search the quick-hash key carries the prefix `q:` and exists only in `filetemp`; the `file` table and its SHA-256 are never changed. K2 never sets the data path, so its duplicates search is unchanged. | [Implemented — increment 3] |
| MDI-C27 | **Connected drives (D30).** `core/mdivolumes.*` lists drives with external programs (`diskutil` on macOS, `lsblk` and `udisksctl` on Linux), started as `MDI-C7` requires, and lists mounted volumes on Windows. Mounting is always read-only. Katalog MUST NOT open or read a raw device (that would need administrator rights); a drive the system cannot mount is out of scope. | [Implemented — increment 3] |
| MDI-C28 | **Enrichment list store (D31).** The enrichment list — the running job and the waiting jobs — is kept in the table `mdi_job` of `mdi_data.db` (run order, device ID, paused flag), the *enrichment job* table of `MDI-C15`, created when absent by the `mdi_` migration. No core table, no settings key and no new Memory-mode file is added, so a beta settings file, K2 and upstream K3 3.0 are unaffected (`MDI-C1`, `MDI-C7`). It is read and written only through `MdiData::pendingJobs()` and `MdiData::savePendingJobs()`, on the main-thread connection of `MDI-C4`, each write one short transaction; a failed write is logged and changes nothing else. It is written when a job is added, starts, is paused or resumed, ends, is stopped or is removed; quitting Katalog and opening another collection do not clear it (`MDI-F12`, `MDI-F93`). No file position is stored: resuming relies on `MDI-F13`. The provider class that decides D6 is evaluated when a job is restored, from the current profile and provider settings (`MDI-F26`), never stored. Hosted mode: nothing beyond `Q11`. | [Implemented — D31] |
| MDI-C29 | **Waiting state in the runner (D31).** Approved core changes, nothing else: in `MdiEnrichmentJob`, the catalog's source path in `Config`, the waiting state of `MDI-F91` in `run()`, its start and end reported through the existing `activity` signal, and the Pause check of `MDI-F11` in `precomputeThumbnails()`; in `MdiData`, the `mdi_job` table and `pendingJobs()` / `savePendingJobs()` (`MDI-C28`). The source path counts as reachable when it exists and lists at least one entry (a fixed Linux mount point stays as an empty folder when its drive is gone); the destination path, when it exists or can be created. The runner's check runs on the runner's thread only, at most every 2 s while waiting, and reads nothing else from the source; Pause and Stop are checked between checks. The choice among waiting catalogs (`MDI-F92`, and the order of restored jobs, `MDI-F12`) checks their source paths with the same rule on the UI thread, as `Device::loadDevice()`'s own active-state check does: when a job is queued, when the running job starts waiting, every 2 s while it waits, and when the next run is chosen. A source on an unresponsive network volume can then hold the window until the system answers; the fork owner accepted this on 2026-10-29. | [Implemented — D31] |
| MDI-C30 | **Context and knowledge-base store (D33, D17, D5).** The context and the catalog notes (table `mdi_context`: scope — 0 for the collection, otherwise the catalog's device ID —, content as JSON, version, date), the knowledge-base documents (`mdi_kb_document`) and their passages with embeddings (`mdi_kb_passage`: document, index, text, model, vector) are in `mdi_data.db`, created by the `mdi_` migration when absent; they join the table list of `MDI-C15`, and any change to an existing `mdi_` table goes through that migration. No core table or column, no settings key and no new Memory-mode file is added, so K2 and upstream K3 3.0 are unaffected (`MDI-C1`). The context version and the knowledge-base sources of a datum are stored outside the parameters `MdiData::isCurrent` compares (`MDI-F99`). Hosted mode: nothing beyond `Q11`. | [Implemented — Context 1] |
| MDI-C31 | **The context is user data (D33).** *Clear derived data* (`MDI-F59`), a re-run (`MDI-F53`, D13), a quick-hash pass (`MDI-F84`) and the deletion of a catalog MUST NOT delete or change the collection's context or its knowledge base; deleting a catalog removes only that catalog's notes, with its profile (`MDI-F57`). | [Planned — part implemented in Context 1] |
| MDI-C32 | **Core boundary and files (D33, `MDI-C14`).** The context model, the prompt sections built from it, the knowledge base's text extraction, passage splitting, embedding and retrieval, the classification request and the checking of category values are in `core/`. New files for context phase 1: `core/mdicontext.{h,cpp}` and `core/mdiknowledge.{h,cpp}`. Existing core classes changed: `MdiEnrichmentJob` (classification; context sections in prompts; the initial prompt of `whisper.cpp`; retrieval; extended metadata in `listCatalogFiles`), `MdiData` (the tables of `MDI-C30`, the columns of `MDI-C36`, their reads and writes), `MdiFts` (classification text). New K3 file: `qt_quick/PageContext.qml` (`MDI-C18`, D33). Their class and method design was approved by the fork owner on 2026-10-29 (`MDI-C14`) and is listed under *Context phase 1 — core design*. No other new file is approved. | [Implemented — Context 1] |
| MDI-C33 | **Knowledge-base indexing thread.** Indexing runs on its own `QThread` with its own connection to `mdi_data.db`, committing one document at a time; like the runner it is not an operation-queue entry (`MDI-C3`). The `MDI-C4` rule that the main thread does not fail or stall holds. It only reads documents, as `MDI-C10` requires for sources; tools are started as `MDI-C7` requires. Similarity is a cosine scan in `core/` (`MDI-C9`). No vector-index library and no new library or module (`MDI-C8`); the initial prompt of `MDI-F98` is an argument of `whisper.cpp`, an approved tool. | [Implemented — Context 1] |
| MDI-C34 | **Same inputs in every mode.** In Memory mode the classification receives the same inputs as in File mode: a file's extended metadata is read with the catalog's file list (`MdiEnrichmentJob::listCatalogFiles`), on the caller's thread, from the catalog's `.idx` file in Memory mode and from the `file` table otherwise (Memory Mode Caveat, `CLAUDE.md`). | [Implemented — Context 1] |
| MDI-C35 | **MCP client (context phase 2, D33).** JSON-RPC 2.0 in `core/`: stdio servers started as `MDI-C7` requires, HTTP over `Qt6::Network`; no MCP library (`MDI-C8`). Credentials as `MDI-C13` (D10). Katalog calls only the resources and tools the user has allowed; remote use follows `MDI-C11` and `MDI-F24`, and nothing is sent before the confirmation. Which programs may be started as servers, whether a stdio server counts as remote, and sign-in are `Q29`–`Q31`; this row authorises no code until they are decided. | [Backlog] |
| MDI-C36 | **Re-interpretation in the enrichment list (D33, `Q24`).** `mdi_job` (`MDI-C28`) also records each job's kind (enrichment or re-interpretation) and a re-interpretation's scope (folder, file types). The columns are added when missing by the `mdi_` migration (`MDI-C15`); a row without them is an enrichment job, so a list written before D33 reads unchanged. The list is still read and written only through `MdiData::pendingJobs()` and `MdiData::savePendingJobs()`. No core table, no settings key and no new Memory-mode file is added. | [Implemented — Context 1] |
| MDI-C37 | **Design-system files and tokens (D34, phase R1).** New K3 files approved for phase R1, listed here as the `CLAUDE.md` FORK block requires. Components, not Pages: `qt_quick/KTheme.qml` (a QML singleton holding the design tokens), `qt_quick/KCard.qml`, `qt_quick/KInfoTag.qml`, `qt_quick/KSidebarItem.qml`, `qt_quick/KSectionHeader.qml`, `qt_quick/KMetricCard.qml`, `qt_quick/KProgressBar.qml` and `qt_quick/KButton.qml` (`KButton.qml` added at the main agent's request, covered by the owner's instruction of 2026-10-30, `Q43`). A Page: `qt_quick/PageHome.qml` (the third exception of `MDI-C18`). Every colour, radius, spacing and text size of the design system is defined once, in `KTheme.qml`; a page or component MUST NOT hardcode or locally redefine one. **Owner's exception (`Q34`, `Q35`, 2026-10-29: "Fisse come nel prototipo"):** the tokens are a fixed Katalog palette (light and dark sets, chosen by the system's colour scheme) and fixed text sizes. For the design-system components, and for the sidebar and pages built with them, this departs from `TYP-C1`, `TYP-C3`, `TYP-F2`, `TYP-F3`, `TYP-F4`, `TYP-F7`, `TYP-F11` (`SpecTypography.md`) and from `THM-C1`, `THM-C2`, `THM-C3` (the *Theme* setting has no effect on them), `THM-C21` and `THM-C22` (`SpecTheme.md`). **Extension (2026-10-30, owner's instruction "aggiornando tutto con il nuovo stile e ux", D34):** a page rebuilt in a phase of D34 also departs from the presentation rows of the row colours and selection highlight (`THM-F1`–`F4`, `THM-F8`, `THM-F17`, `THM-F18`, `THM-F22`, `THM-C19`, `THM-C23`), the section-title style (`PGL-F5`–`F8`, `PGL-C6`, `PGL-C13`) and `TYP-F6`. These stay binding: `THM-O1` (rows readable one by one), `THM-F7` (bigger icon size), `PGL-O1`, `PGL-F1`, `PGL-F2`, `PGL-C1`, `PGL-C2`, `PGL-C5` and `PGL-C11`. Pages not yet rebuilt stay bound by all those rows. The monospace font is chosen per platform (Menlo on macOS, Consolas on Windows, the generic monospace family elsewhere). No other new file is approved; the files of phases R2–R8 are listed when their rows are drafted. The `TecaStyle` files are approved by `MDI-C42`. The new QML files are added to the K3 QML file lists as the fork's other QML files are (finding under `MDI-C1`). **Amended (owner, 2026-10-30, D37; `Q34` reversed for text size):** the text sizes are no longer fixed. Every size token is multiplied by `KTheme.textScale`, bound to the *Text size* setting (`MDI-F154`). For text size, the departures from `TYP-F2` and `TYP-F7` are withdrawn; those from `TYP-O1`, `TYP-C1`, `TYP-C3`, `TYP-F3`, `TYP-F4` and `TYP-F11` (pixel sizes, several sizes, the system font) remain. The palette stays fixed. **Defect fixed 2026-10-30 (D37):** the token declared as `onAccent` was parsed as a signal handler of `accent`, so it stayed black. Primary-button text had a 2.8:1 contrast in light mode (`MDI-F149`). | [Planned — part implemented in R1: three sizes are still set outside `KTheme.qml` (R1 review); text scale — D37] |
| MDI-C38 | **Bounds of the redesign (D34).** K2 (`qt_widgets/`) is not changed (`MDI-C1`, D1; "Solo Katalog 3", `Q32`). No `core/` change: phase R1 shows only data that K3 reads through existing core methods. A new `AppManager` accessor may only call existing core methods and contains no raw `QSqlQuery`; a worker thread may open its own read-only connection for those methods. A new core method needs the owner's approval first (`MDI-C14`). **The UI thread reads no catalog's file or folder list for Home** (Memory Mode Caveat, `CLAUDE.md`): counts that need one run on a worker thread with its own read-only connections and are cached; Home shows the same values in File and Memory modes; in Hosted mode, counts that need a file list are not shown. No new library, Qt or KF6 module (`MDI-C8`); Kirigami Addons is not used (`PGL-C12`). Messages keep their channels: blocking validation in a `Kirigami.InlineMessage`, transient success in `showPassiveNotification`, decisions in modal dialogs (`MDI-C17`, `SpecValidationRules.md`). Progress and status text is built with `StatusBarMessageBuilder` (`MDI-C16`), whose status values the sidebar's job card reuses. A new settings key only adds to `katalog3_prerelease_settings.ini` (`MDI-C7`). These rows stay binding on every rebuilt page unless amended first: `PGL-C5` (Home is a scrollable page on `ScrollablePageFitted.qml`), `PGL-C11`; `CDT-C1` (no card or tag text is elided or capped in lines); `ICB-F1` (an icon-only button has a tooltip); `CID-F1`–`CID-F5` (the collection's name). `PGL-O3` at phone width is `Q41`; source-path checks on the UI thread for Home are `Q42`. | [Implemented — R1] |
| MDI-C39 | **Languages of the redesign (D34, answer 3; `Q33`).** The redesign's new strings have an English source and are translated into Italian only, "per ora": in every other interface language they show in English until they are translated, at the end of the redesign (`CLAUDE.md` FORK block, D34 exception). This amends D16 and `MDI-C17` ("translations run after that approval") for the redesign's strings only. Otherwise D16 holds: new strings reuse existing K2/K3 strings where they fit, and the fork owner approves all of a step's new strings together at the end of the step, before its commit. A renamed label is a **new** string in place of the old one. No existing source string is edited. An old string that is no longer used stays in the translation files, marked vanished. The fork owner approved the `CLAUDE.md` amendments (`Q33`, 2026-10-29: "Sì, e aggiorna CLAUDE.md"). The fork's other strings keep D16 with all languages. **Exception (visible name, owner, 2026-10-29):** the existing strings that named the application were edited to say *Teca* (`MDI-F123`). Their translations in every language were carried over with only the name replaced. The reworded alpha-warning welcome and R1's string are translated into Italian only. | [Implemented — R1] |
| MDI-C40 | **Bounds of phases R2–R8 (D34, 2026-10-30): presentation only.** A phase changes layout, components, colours, type, icons and label wording, and nothing else. **Kept on every page:**<br>• every behaviour, binding, data shown, validation, confirmation dialog, keyboard and focus rule, stored setting key and message channel, as the page's own spec and this spec state (`SpecExplore.md`, `SpecSearchList.md`, `SpecSearchResults.md`, `SpecSearchResultsFilters.md`, `SpecSelection.md`, `SpecDevicesPage.md`, `SpecBackup.md`, `SpecSettingsPage.md`, `SpecValidationRules.md`, `SpecKeyboardShortcuts.md`, `SpecCardsAndTables.md`, `SpecComboBoxes.md`, `SpecIconButtons.md`, `SpecProgressReport.md`, `SpecOperationQueue.md`, `SpecWindowGeometry.md`);<br>• every piece of information a page shows: what moves into a card or a tag stays readable in full (`CDT-O2`, `CDT-C1`);<br>• tables keep their columns, sort, stored widths and keyboard navigation (`CDT-O3`, `CDT-F12`), and lists stay readable row by row (`THM-O1`);<br>• the user settings that the `MDI-C37` exception does not name: bigger icon size (`THM-F7`), stored views, the Explore view choice (`MDI-F88`) and the Inspector's visibility (`MDI-F87`).<br>**Files and limits:** `MDI-C37` (files and tokens, the owner's exception for sizes and colours) and `MDI-C38` (no core change, no raw SQL, message channels) apply. No file is added beyond those of `MDI-C37` and `MDI-C42`.<br>**Strings:** new strings follow `MDI-C39` (English source, Italian only) and are approved by the owner's instruction of 2026-10-30 (D34). No existing source string is edited, except for the name *Teca* (`MDI-F123`).<br>**New behaviour:** a behaviour that a phase seems to need is NOT IN SPEC until a row authorises it (`Q45`). | [Implemented — phases R2–R8] |
| MDI-C41 | **What keeps the name Katalog (owner, 2026-10-29).** The name *Teca* is a visible name only (`MDI-F123`). It MUST NOT reach the application's identity on disk or in code. The settings files (`katalog3_prerelease_settings.ini`, the collection `.ini`), the collection format, the catalogs, internal identifiers (for example the CMake target `Katalog3` and the desktop file name), the QML module URI and the `KAboutData` component name all stay "Katalog". Settings and data paths therefore do not move, and a beta settings file stays readable (`MDI-C7`). | [Implemented — 2026-10-30] |
| MDI-C42 | **TecaStyle, the redesign's Qt Quick Controls style (owner, 2026-10-30).** `TecaStyle` is a static QML module in `qt_quick/TecaStyle/`. Its files are `Button.qml`, `CheckBox.qml`, `ComboBox.qml`, `Menu.qml`, `MenuItem.qml`, `RadioButton.qml`, `SpinBox.qml`, `Switch.qml`, `TextArea.qml`, `TextField.qml`, `ToolButton.qml` and its `CMakeLists.txt`. It is linked into the K3 executable. It adds no Qt or KF6 module, because `Qt6::QuickControls2` was already linked (`MDI-C8`). Each control derives from its Fusion counterpart and changes only its look: rounded fields and buttons (10 px), check boxes, radio buttons and switches in the accent colour, and rounded menus. Every other control falls back to Fusion (the module imports `QtQuick.Controls.Fusion`). Colours come only from the palette, which `Main.qml` sets from the `KTheme` tokens: window, base, button, text, highlight, `mid` (line), `midlight` (hover), `brightText` (onAccent), placeholder text and tool tips. `main.cpp` selects `TecaStyle` on **every platform**. Before, K3 used the system's `org.kde.desktop` style on Linux and Fusion elsewhere. **For the fork's K3, this supersedes the earlier style choice** wherever a spec names Fusion, Breeze or `org.kde.desktop` as K3's style. Those rows are listed under *Specs to amend* and are not rewritten. Their aim still holds: K3's own surfaces look the same whatever style draws the controls (`THM-F9`, `THM-C14`). **Amended (D37, 2026-10-30):** the palette also sets `dark` from the `controlLine` token, for control edges (`MDI-C47`, `MDI-F149`), and its `disabled` group, with the controls' opacity, dims disabled controls (`MDI-F151`). | [Implemented — 2026-10-30; amendment — D37] |
| MDI-C43 | **Split carry-over in core (D35, `MDI-C14`).** Approved core changes, nothing else. (1) A new method `static int MdiData::moveToCatalogs(const QString &connectionName, int oldCatalogId, const QHash<QString, int> &newCatalogByPath)`. Like `MdiData`'s other methods it works on a connection the caller has opened. In one transaction it moves the old catalog's `mdi_datum` rows with `UPDATE OR IGNORE`, so a row that would collide stays on the old catalog, and its `mdi_embedding` rows with `UPDATE`, for the full paths in the map. It returns the number of `mdi_datum` rows moved. (2) In `Collection::applySplitResult`, only when the mode is not Hosted and `mdi_data.db` exists: open that file with `MdiData::open` on a connection of its own; build the map of full path to new catalog ID from the new catalogs' rows of the `file` table, which hold them right after the split in File and Memory modes; call `moveToCatalogs`; close the connection. (3) Also in `Collection::applySplitResult`: copy the old device's profile to each new catalog's device with `MdiProfile::load` and `MdiProfile::save`, plus `MdiProfile::saveTableToFile` in Memory mode. No other new method and no new file. No change to the `mdi_` tables, to the core schema (`MDI-C15`) or to the split's own steps (`SpecDevicesSplit.md`). No file on disk is touched. The index build of `MDI-F128` reuses the existing rebuild (`MDI-C22`). This code is in core, so a fork build of K2 that splits a collection with `mdi_data.db` carries the data too, with no K2 file changed (`MDI-C1`). Upstream builds do not carry it. **Amended (D36, 2026-10-30):** the split's date fix (`MDI-F143`, `MDI-C46` (3)) is the one approved change to the split's own steps. | [Implemented — D35] |
| MDI-C44 | **Bounds of *Browse never empty* (D34 follow-up).** `MDI-F129`–`F135` are K3 only (`MDI-C1`) and change no core class. The cards and the "AI" tag use data K3 already reads through existing core methods: the figures of Home's drive cards, and the derived data Browse already reads for `MDI-F44`. `MDI-C38`'s rules for Home's counts apply to the cards: no file list is read on the UI thread, and counts run on a worker thread and are cached. A new core method needs the owner's approval first (`MDI-C14`). The views use the design system (`MDI-C37`). No new file. New strings follow `MDI-C39` (English source, Italian translation), and the owner approves them together at the end of the step, before its commit (D16). Existing strings are reused where they fit. "No folder selected" is not edited. Everything else on the page keeps `MDI-F116` and `MDI-C40`. | [Implemented — D34 follow-up] |
| MDI-C45 | **History store (D36).** Restore points and the list of actions are in `teca_history.db`, a SQLite file in the collection folder, never in the collection database. The core schema stays 3.0, and no core table or column changes (D5, `MDI-C15`); restoring writes data rows, not schema. The file name is the owner's choice (D36). It is an exception to D5's `mdi_` prefix for new files, for this file only. The file carries its own version record, apart from the core schema version and the media intelligence schema version. A snapshot holds copies of `device` rows (the catalog's device and its virtual copies), the `catalog` row, `file` and `folder` rows and `mdi_profile` rows. K2 and upstream K3 3.0 ignore the file. **Memory mode** (Memory Mode Caveat, `CLAUDE.md`): a catalog's file and folder lists are loaded from its `.idx` and `.folders.idx` files before they are copied, so a snapshot holds the same content as in File mode; a restore saves the catalogs as Memory mode saves any catalog. **Hosted mode** without a local collection folder: no history (`MDI-F136`). Nothing is written to any catalogued drive (`MDI-C10`). | [Implemented — D36; Hosted not tested] |
| MDI-C46 | **History and recomposition in core (D36, `MDI-C14`).** Approved core changes, nothing else. (1) New files `core/cataloghistory.{h,cpp}`, class `CatalogHistory` (the name is the main agent's proposal). It records entries and snapshots in `teca_history.db`, lists them (all, or one catalog's), restores an entry, opens an entry as a separate catalog, and prunes (`MDI-F136`–`F141`). Re-linking AI data reuses `MdiData::moveToCatalogs` (`MDI-C43`); no new `MdiData` method. (2) A new `Collection` method that recomposes catalogs (`MDI-F142`). (3) The split's fix that gives the new catalogs a valid `catalog_date_updated` (`MDI-F143`): the one change to the split's own steps (`MDI-C43`). K3 takes the restore point (`AppManager`) before it calls the existing operation. `CatalogJobStoppable`, `CatalogManager`, `DeviceUpdateManager` and the split's other steps are not changed, so the operations behave as before (`MDI-C2`). K3 API in `AppManager`: `historyEntries`, `historyForDevice`, `restoreHistoryEntry`, `openHistoryVersion`, `recomposeProposal`, `recomposeCatalogs`, `historyAvailable`; signals `historyChanged` and `undoAvailable`. No raw `QSqlQuery` in `qt_quick/`. New K3 file: `qt_quick/PageHistory.qml` (`MDI-C18`). No other new file, no new library or module (`MDI-C8`). Change (3) is in core, so a fork build of K2 that splits gets the date too, with no K2 file changed (`MDI-C1`); restore points and recomposition are K3 only. New strings follow `MDI-C39` and D16. | [Implemented — D36; core changes beyond this row: finding, *Implementation status*] |
| MDI-C47 | **Bounds of navigation and accessibility (D37).** K3 only (`MDI-C1`); no `core/` change; no new file. Back, Forward and the breadcrumb reach pages and Browse locations through the existing open paths: no second way of opening a catalog (`EXP-C21`), and `Esc` reuses an existing handler (`KBS-C1`'s rule, applied to Back). The history of `MDI-F144` is held in memory, with no settings key. New tokens are defined in `KTheme.qml` (`MDI-C37`): `controlLine`, light `#7D8799`, dark `#6B7590`, which `Main.qml` passes to `TecaStyle` as `palette.dark` (`MDI-C42`). Accessible names reuse existing strings; other new strings follow `MDI-C39`. Accessibility uses Qt Quick's own `Accessible` attached properties: no new Qt or KF6 module (`MDI-C8`). Message channels and the progress builder are unchanged (`MDI-C38`). | [Implemented — D37] |

---

## Phasing {#phasing}

| Phase | Content | Rows |
| ------- | --------- | ------ |
| **A** | Wizard mode; runner and its lifecycle; tool detection; previews; offline browsing of previews; `mdi_` tables and their migration (core schema stays 3.0); provenance; enrichment of existing catalogs; Memory/Hosted support; *Include metadata* independence. | `MDI-F1`, `F3`, `F4`, `F6`–`F22`, `F60`, `F29`–`F34`, `F44`–`F46`, `F52`–`F59`; all `MDI-C` |
| **B** | Providers (local, then commercial with egress confirmation and cost); image caption and tags; PDF summary and excerpts. | `MDI-F5`, `F23`–`F28`, `F39`–`F41` |
| **C** | Transcripts, topic segments, topic clusters. | `MDI-F35`–`F38` |
| **D** | Embeddings and semantic search. | `MDI-F47`–`F51` |
| **E** | Other documents; applications and other files. | `MDI-F42`, `F43` (`[Backlog]`) |
| **Activity** | Activity page, core resource monitor, per-step activity reporting from the runner (D19, D21). Not bound to the A–E order. | `MDI-F61`–`F75`, `MDI-C21` |
| **Increment 3** | Instant search index, WebP thumbnails and contact sheets, technical data, text snippets, archive listings, quick hash and duplicates, Inspector, column view, side menu, connected drives (D22–D30). Not bound to the A–E order. | `MDI-F76`–`F90`, `MDI-C22`–`C27` |
| **Context 1** | Domain context and catalog notes, draft from the catalog, context in new analyses, classification, re-interpretation on request, local knowledge base, the *Context* page in *Tools* (D33). Rows ratified and core design approved (`MDI-C14`) on 2026-10-29. Not bound to the A–E order. | `MDI-O8`, `MDI-F89` (*Context* entry), `MDI-F94`–`F108`, `MDI-C18` (exception), `MDI-C30`–`C34`, `MDI-C36` |
| **Context 2** | MCP servers, online and cloud sources (D33). `[Backlog]` until `Q29`–`Q31` are decided. | `MDI-F109`, `F110`, `MDI-C35` |
| **R1** | Redesign (D34): the design system (tokens and components), the sidebar in place of the side menu, the *Home* page, the new names on sidebar entries and page titles, new strings in English and Italian. Not bound to the A–E order. Coded and committed before the rows were ratified; the rows were then settled by the owner's answers of 2026-10-29 (`Q32`–`Q38`). Implemented, except as stated in the R1 review. | `MDI-O9`, `MDI-F111`–`F115`, `MDI-C18` (exception), `MDI-C37`–`C39`; `MDI-F89` removed; `MDI-F107` amended |
| **R2** | *Sfoglia l'archivio* and *Dettagli del file*: Explore, Inspector, column view, analysis dialog (D34). Authorised 2026-10-30 (D34); presentation only (`MDI-C40`). | `MDI-F116`, `MDI-C40`; `SpecExplore.md`, `MDI-F87`, `MDI-F88`, `MDI-F102`; `Q18` (Explore) |
| **R3** | *Cerca nei dischi*: the Search form, results and term list (D34). Authorised 2026-10-30 (D34); presentation only (`MDI-C40`). *By content*: `Q45`. | `MDI-F117`, `MDI-C40`; `SpecSearchList.md`, `SpecSearchResults.md`, `SpecSearchResultsFilters.md`, `MDI-F47`–`F49` |
| **R4** | *Conoscenza dell'archivio*: the Context page (D34). Authorised 2026-10-30 (D34); presentation only (`MDI-C40`). | `MDI-F118`, `MDI-C40`; `MDI-F94`–`F108` |
| **R5** | *Lavori in corso*: the Activity page and the bottom panel (D34). Authorised 2026-10-30 (D34); presentation only (`MDI-C40`). | `MDI-F119`, `MDI-C40`; `MDI-F61`–`F69`, `SpecOperationQueue.md` |
| **R6** | *Aggiungi un disco*: the Create page, its wizard mode and connected drives (D34). Authorised 2026-10-30 (D34); presentation only (`MDI-C40`). | `MDI-F120`, `MDI-C40`; `MDI-F1`, `MDI-F60`, `MDI-F90`; `Q18` (Create) |
| **R7** | *I miei dischi* and *Gestisci i dischi*: the Selection and Devices pages, restyled, still separate (D34). Authorised 2026-10-30 (D34); presentation only (`MDI-C40`). | `MDI-F121`, `MDI-C40`; `SpecSelection.md`, `SpecDevicesPage.md` |
| **R8** | *Altro* and *Impostazioni*: Statistics, Tags, Backup, Settings, metadata dialog (D34). Authorised 2026-10-30 (D34); presentation only (`MDI-C40`). | `MDI-F122`, `MDI-C40`; `SpecBackup.md`, `SpecSettingsPage.md`; `Q46` |
| **Name and style** | The visible name *Teca*, the `TecaStyle` controls style, dialogs and the page header, and the *My drives* header (D34, 2026-10-29/30). Coded before the rows were written. | `MDI-F123`–`F125`, `MDI-C41`, `MDI-C42`; `MDI-C18`, `MDI-C37`, `MDI-C39`, `MDI-C40`, `MDI-F121` amended |
| **Split carry-over** | The AI data, enrichment profile and instant search index of a split catalog (D35, 2026-10-30). The core change was approved with the decision. Not bound to the A–E order. | `MDI-F126`–`F128`, `MDI-C43`; `SpecDevicesSplit.md` |
| **Browse never empty** | Catalog cards when Browse has nothing to show, the "AI" tag on files, the page's description line (D34 follow-up, 2026-10-30). Then, after `Q51`: "Choose another drive", a folder that is its own catalog, "Add a drive" in an empty collection, and a click that selects a file without opening it. New behaviour, bounded by `MDI-C44`. | `MDI-F129`–`F135`, `MDI-C44`; `SpecExplore.md` (`EXP-F27`), `SpecCardsAndTables.md` (`CDT-F7`), `MDI-F44`, `MDI-F112`, `MDI-F116` |
| **History** | Restore points in `teca_history.db`, the *Storico* page, restore, *Annulla*, opening a version as a separate catalog, *Ricomponi*, the split date fix (D36, 2026-10-30). Core changes approved with the decision (`MDI-C46`). Then the recomposition of the owner's 22 UITS catalogs. | `MDI-O10`, `MDI-F136`–`F143`, `MDI-C45`, `MDI-C46`; `MDI-C18`, `MDI-C43`, `MDI-F111`, `MDI-F128` amended; `SpecDevicesSplit.md`, `SpecDevicesPage.md`, `SpecSettingsPage.md` |
| **Navigation and accessibility** | Back and Forward, the Browse breadcrumb, no Close on sidebar pages, the scope chip, keyboard jumps; contrast, focus, states, keyboard, names, motion, the activity panel (D37, 2026-10-30). K3 only, no core change. `MDI-F154` confirmed by the owner on 2026-10-30. Built 2026-10-30. | `MDI-O11`, `MDI-O12`, `MDI-F144`–`F156`, `MDI-C47`; `MDI-F111`, `MDI-F115`, `MDI-F124`, `MDI-C42` amended; `SpecKeyboardShortcuts.md`, `SpecExplore.md`, `SpecSelection.md`, `SpecTypography.md` |
| **Trash parity** | Confirmation before *Move to Trash* in Browse and Search results, with K2's strings (2026-10-30). | `MDI-F157`; `SpecExplore.md`, `SpecSearchResults.md` |

A phase starts only when the previous one is implemented, and each phase starts
with the core design approval required by `MDI-C14` and the string approvals of
`MDI-C17`. For the redesign phases R2–R8: their rows were written on 2026-10-30 under
the owner's blanket authorisation (D34) and are bounded by `MDI-C40`. They were
reported implemented on 2026-10-30.

---

## User-visible text

New strings are approved together at the end of each step (`MDI-C17`, D16).
Approved strings are listed here as they are approved.

### Approved 2026-10-28 — Phase A.1 (wizard mode)

New strings:

| String | Where |
| --- | --- |
| "Standard" | Mode switch (`MDI-F1`) |
| "Wizard" | Mode switch (`MDI-F1`) |
| "Next" | Wizard navigation (`MDI-F2`) — no longer used (D15) |
| "Back" | Wizard navigation (`MDI-F2`) — no longer used (D15) |
| "Step %1 of %2" | Wizard step indicator (`MDI-F2`) — no longer used (D15) |
| "Destination path" | Section title and field label (`MDI-F60`) |
| "Path for previews and derived data" | Destination path placeholder (`MDI-F60`) |
| "File types" | Section title (`MDI-F3`) |
| "Previews" | Section title (`MDI-F4`) |
| "Summary" | Step title (`MDI-F2`) — no longer used in `CreateWizard.qml` (D15) |
| "Thumbnail" | Image option (`MDI-F4`) |
| "Poster frame" | Video option (`MDI-F4`) |
| "Keyframe strip" | Video option (`MDI-F4`) |
| "Number of frames" | Keyframe strip count (`MDI-F4`) |
| "Clip" | Video and audio option (`MDI-F4`) |
| "Off" | Clip choice (`MDI-F4`) |
| "Full" | Clip choice (`MDI-F4`) |
| "Partial" | Clip choice (`MDI-F4`) |
| "Start (seconds)" | Partial clip start (`MDI-F4`) |
| "Duration (seconds)" | Partial clip duration (`MDI-F4`) |
| "Waveform" | Audio option (`MDI-F4`) |

New validation strings (`Kirigami.InlineMessage`, `MDI-F6`):

| String | Refusal |
| --- | --- |
| "Provide a destination path for the previews and derived data." | Destination path empty |
| "Select a destination path that can be written to." | Destination path not writable |
| "Select a destination path outside the source path." | Destination path is the source or inside it |
| "Select at least one file type, or turn off all previews." | No file type while a preview is selected |

Reused existing strings: "Source", "Source path", "Select the path", "Storage",
"Catalog name", "Create", "Image", "Video", "Audio", "Text", "Other", "On", and
the three existing Create page validations (name, path, storage).

The approval was given by the user in the main session ("ok tutto") and reported
to the specifications agent by the main agent.

### Approved 2026-10-28 — increment 2 (runner, AI analysis, Explore, semantic search)

Approved together at the end of the step (D16), by the fork owner in the main
session; reported to the specifications agent by the main agent.

New strings:

| String | Where |
| --- | --- |
| "AI analysis" | Section title of the enrichment options (`MDI-F5`, `MDI-F60`) |
| "Caption and tags" | AI analysis switch; analysis label in progress and *Show analysis* (`MDI-F39`) |
| "Summary and excerpts" | AI analysis switch; analysis label (`MDI-F40`) |
| "Transcript" | AI analysis switch; analysis label (`MDI-F35`) |
| "Topic segments" | AI analysis switch; analysis label (`MDI-F36`) |
| "Topic clusters" | Explore action and dialog title; analysis label (`MDI-F37`) |
| "Analysed on this computer by the local model (%1). Nothing is sent to the internet." | Note under the AI analysis switches (`MDI-F5`, privacy stance) |
| "Analyze" | Explore action, dialog title and confirm action for an existing catalog (`MDI-F8`) |
| "Show analysis" | Explore file action, semantic result action, details dialog title (`MDI-F44`) |
| "Enrichment" | Operation name in the activity-panel status (`MDI-F17`, `MDI-C16`) |
| "Paused" | Status in the activity-panel status (`MDI-F11`, `MDI-F17`) |
| "By meaning" | Search page section and result list title (`MDI-F47`) |
| "Describe what you are looking for" | Placeholder of the semantic query (`MDI-F47`) |
| "No file matches this description." | Empty semantic result list (`MDI-F48`) |
| "The local model is not reachable: %1" | Passive notification when the query cannot be embedded (`MDI-F50`) |

Reused existing strings: "Pause", "Resume", "Stop", "In Progress", "Completed",
"Cancelled", "Open file", "Expand", "Collapse", "text", "Selected device is not
a catalog", and the A.1 strings above (including the four destination and
file-type validations, reused by the *Analyze* dialog).

### Approved 2026-10-28 — Activity page

Approved together at the end of the step (D16), by the fork owner in the main
session ("ok testi", 2026-10-28); reported to the specifications agent by the
main agent, which did not see the reply itself.

New strings:

| String | Where |
| --- | --- |
| "Activity" | Side-menu entry, page title, activity-panel button (`MDI-F61`, `MDI-F69`) |
| "Nothing running" | Status line with no job (`MDI-F61`) |
| "Checking what is already done" | *Right now* card before a job's first step (`MDI-F64`) |
| "File %1 of %2" | Status line (`MDI-F62`) |
| "Estimated end %1" | Status line (`MDI-F62`) |
| "Waiting: %1" | Waiting jobs, in run order (`MDI-F66`) |
| "%1 cores" | Machine line under the status (`MDI-F63`) |
| "CPU" | Tile, chart legend, program use (`MDI-F63`, `MDI-F68`, `MDI-F71`) |
| "GPU" | Tile, chart legend (`MDI-F63`, `MDI-F68`) |
| "%1 of %2 GB" | Memory tile (`MDI-F63`) |
| "Swap" | Tile (`MDI-F63`) |
| "Memory pressure" | Tile (`MDI-F63`) |
| "Power" | Tile (`MDI-F63`) |
| "Mains" | Power tile value (`MDI-F63`) |
| "Battery" | Power tile value (`MDI-F63`) |
| "Source drive" | Tile (`MDI-F63`) |
| "Connected" | Source drive tile; Ollama state (`MDI-F63`, `MDI-F72`) |
| "Not connected" | Source drive tile; Ollama state (`MDI-F63`, `MDI-F72`) |
| "Data sent" | Egress tile (`MDI-F63`) |
| "Nothing sent" | Egress tile value (`MDI-F63`) |
| "Right now" | Card title (`MDI-F64`) |
| "On this computer" | Where a step runs: local (`MDI-F64`) |
| "On the local network" | Where a step runs: local network (`MDI-F64`) |
| "On the Internet" | Where a step runs: remote (`MDI-F64`) |
| "Since this session started" | Resource history card title (`MDI-F68`) |
| "Programs" | Child-process card title (`MDI-F71`) |
| "Models in memory" | Ollama models card title (`MDI-F65`, `MDI-F72`) |
| "Images: %1 · Texts: %2 · Search by meaning: %3" | Configured model per task (`MDI-F65`) |
| "Tools" | Tools card title (`MDI-F65`) |
| "Not found" | Tool not found (`MDI-F65`, `MDI-F20`) |
| "Previews and clips" | Use of `ffmpeg` (`MDI-F65`) |
| "Durations" | Use of `ffprobe` (`MDI-F65`) |
| "Text of PDF files" | Use of `pdftotext` (`MDI-F65`) |
| "Log" | Log card title (`MDI-F67`) |
| "%1 tokens/s" | Log entry of a model call (`MDI-F67`, `MDI-F75`) |
| "No step finished in this session." | Empty log (`MDI-F67`) |
| "Index for search by meaning" | Analysis label of the embeddings step (`MDI-F64`, `MDI-F67`) |

Reused existing strings: "In Progress", "Memory", "Pause", "Paused", "Resume",
"Stop", "Transcript", "Close".

### Approved 2026-10-29 — increment 3

Approved together at the end of the step (D16), by the fork owner in the main
session on 2026-10-29; reported to the specifications agent by the main agent,
which did not see the reply itself.

New strings:

| String | Where |
| --- | --- |
| "Text snippet" | Analysis label; Inspector and *Show analysis* section (`MDI-F81`, `MDI-F87`) |
| "Instant search index" | Analysis label (`MDI-F76`) |
| "Quick hash" | Analysis label; Inspector (`MDI-F84`, `MDI-F87`) |
| "Verify with full checksum" | Duplicates option on the Search page (`MDI-F85`) |
| "Technical data" | Analysis label; Inspector and *Show analysis* section (`MDI-F80`, `MDI-F87`) |
| "Archive contents" | Analysis label; Inspector and *Show analysis* section (`MDI-F82`, `MDI-F87`) |
| "Inspector" | Panel title and action (`MDI-F87`) |
| "Columns" | Explore action (`MDI-F88`) |
| "No file selected." | Inspector with no selection (`MDI-F87`) |
| "Online – opens with a click" | Inspector disk state (`MDI-F87`) |
| "Offline – %1 is needed to open the file" | Inspector disk state (`MDI-F87`) |
| "Open on map" | Inspector, GPS link (`MDI-F87`) |
| "Connected drives" | Create page list (`MDI-F90`) |
| "Not mounted" | Drive state (`MDI-F90`) |
| "Mount read-only" | Drive action (`MDI-F90`) |
| "Refresh" | Drive list action (`MDI-F90`) |
| "%1 could not mount this drive: its license or its permission in System Settings may need attention. Without it, the system reads NTFS drives on its own, and reading is all Katalog needs." | Mount failure with a third-party driver (`MDI-F90`) |
| "The system could not mount this drive: %1" | Mount failure, other cause (`MDI-F90`) |

Reused existing strings: "Tools", "Extended Metadata", "Checksum", "Show
analysis", "Source path", "Duration", "Files", "Size".

### Approved 2026-10-29 — D31

Approved together at the end of the step (D16), by the fork owner in the main
session on 2026-10-29 ("Approvo entrambi"); reported to the specifications
agent by the main agent, which did not see the reply itself.

New strings:

| String | Where |
| --- | --- |
| "Waiting for the drive" | Enrichment status built with `StatusBarMessageBuilder`, the path waited for as Current Item (`SpecProgressReport.md` Part 2); Activity page status line (`MDI-F91`, `MDI-F62`) |
| "The analysis continues by itself when %1 is back." | Activity page "Right now" card while the job waits; `%1` is the path waited for, source or destination (`MDI-F91`, `MDI-F64`) |

Reused existing strings: "Paused", "Resume", "In Progress".

### Approved 2026-10-29 — D33 (context phase 1)

Approved together at the end of the step (D16), by the fork owner in the main
session on 2026-10-29 ("Approvi i 32 testi nuovi della funzione Contesto …?" →
"Approvo tutti"); reported to the specifications agent by the main agent, which
did not see the reply itself.

New strings:

| String | Where |
| --- | --- |
| "Organisation" | Context field; entity kind (`MDI-F94`) |
| "Period" | Context field (`MDI-F94`) |
| "Domain" | Context field (`MDI-F94`) |
| "Narrative style and terminology" | Context field (`MDI-F94`) |
| "Notes" | Context field (`MDI-F94`) |
| "Suggest the glossary terms to the transcription" | Transcription option, off by default (`MDI-F98`) |
| "Glossary" | Section title (`MDI-F94`) |
| "Term" | Glossary field (`MDI-F94`) |
| "Preferred form" | Glossary field (`MDI-F94`) |
| "Also written as" | Aliases of a term or an entity (`MDI-F94`) |
| "Definition" | Glossary field (`MDI-F94`) |
| "Categories" | Section title (`MDI-F94`) |
| "Values" | Allowed values of a category (`MDI-F94`) |
| "Only these values" | Closed list (`MDI-F94`, `MDI-F101`) |
| "More than one value" | Several values allowed (`MDI-F94`, `MDI-F101`) |
| "Known names" | Section title of the known entities (`MDI-F94`) |
| "Person" | Entity kind (`MDI-F94`) |
| "Place" | Entity kind (`MDI-F94`) |
| "Event" | Entity kind (`MDI-F94`) |
| "Notes for this catalog" | Catalog notes (`MDI-F95`) |
| "Draft from the catalog" | Action (`MDI-F97`) |
| "Reference documents" | Section title of the knowledge base (`MDI-F104`) |
| "Add files" | Knowledge-base action (`MDI-F104`) |
| "Add folder" | Knowledge-base action (`MDI-F104`) |
| "%1 passages" | Document state, indexed (`MDI-F104`) |
| "To be indexed" | Document state, waiting (`MDI-F104`) |
| "Re-interpret with the context" | Action and dialog title (`MDI-F103`) |
| "%1, about %2" | Re-interpretation estimate: files, duration (`MDI-F103`) |
| "Context saved" | Passive notification after *Save* (`MDI-F94`) |
| "Draft ready, not saved yet." | After a draft (`MDI-F97`) |
| "The draft could not be made: %1" | Draft failure (`MDI-F97`) |
| "Classification" | AI analysis switch; analysis label; Inspector and *Show analysis* section (`MDI-F100`, `MDI-F102`) |
| "Context" | Side-menu entry in *Tools*, page title (`MDI-F107`); approved separately, see below |

Reused existing strings: "Archive", "Save", "Close", "Add",
"Remove", "Name", "Language", "Catalog", "Folder", "File type", "All", "Image",
"Video", "Audio", "Text", "Other", "Error", "Files", "Select the path".

"Context" was first listed as reused, but the translation files before D33
(commit `8a0183f7`) have no source string "Context". The owner approved it as
the 33rd new string on 2026-10-29 ("Correzione mia: il testo "Context" (voce
del menu Strumenti e titolo della pagina) non esisteva, quindi è un testo
nuovo, non riusato come ti avevo scritto. … Lo approvi?" → "Approvo
"Context""); relayed by the main agent.

### Approved 2026-10-29 — D34, phase R1 (redesign)

The fork owner approved these strings verbatim, with their Italian, together
at the end of the step (D16, `MDI-C39`), before the Italian was filled in
("Approvo tutti"). Reported by the main agent, which also supplied the list;
the specifications agent did not see the reply itself. Translated into Italian
only, as `MDI-C39` states; every other language shows the English source.

New strings (45):

| String | Italian |
| --- | --- |
| "Home" | "Home" |
| "Archive overview" | "Panoramica dell'archivio" |
| "My drives" | "I miei dischi" |
| "Catalogued drives and folders" | "Dischi e cartelle catalogati" |
| "Browse the archive" | "Sfoglia l'archivio" |
| "Folders, previews, details" | "Cartelle, anteprime, dettagli" |
| "Search the drives" | "Cerca nei dischi" |
| "By name, content or meaning" | "Per nome, contenuto o significato" |
| "Add a drive" | "Aggiungi un disco" |
| "Catalogue a drive or a folder" | "Cataloga un disco o una cartella" |
| "Intelligence" | "Intelligenza" |
| "Archive knowledge" | "Conoscenza dell'archivio" |
| "Context, glossary, documents" | "Contesto, glossario, documenti" |
| "Work in progress" | "Lavori in corso" |
| "Analysis, resources, history" | "Analisi, risorse, cronologia" |
| "Archive in numbers" | "Numeri dell'archivio" |
| "Labels" | "Etichette" |
| "Safety copies" | "Copie di sicurezza" |
| "Manage drives" | "Gestisci i dischi" |
| "Your archive" | "Il tuo archivio" |
| "Search all the drives" | "Cerca in tutti i dischi" |
| "Catalogued files" | "File catalogati" |
| "Drives: %1" | "Dischi: %1" |
| "Files with AI results" | "File con risultati dell'AI" |
| "Previews, descriptions, transcripts" | "Anteprime, descrizioni, trascrizioni" |
| "Each drive is a card: whether it is connected, what it holds and what the AI has understood." | "Ogni disco è una card: se è collegato, cosa contiene e cosa ha già capito l'AI." |
| "All the drives" | "Tutti i dischi" |
| "%1 files" | "%1 file" |
| "AI: %1 files" | "AI: %1 file" |
| "Saved search results" | "Risultati di ricerca salvati" |
| "Previews and descriptions stay visible; the drive is needed to open the files." | "Anteprime e descrizioni restano visibili; per aprire i file serve il disco." |
| "Browse" | "Sfoglia" |
| "Add a drive or a folder" | "Aggiungi un disco o una cartella" |
| "Katalog reads the drive, NTFS too, without changing it." | "Katalog legge il disco, anche NTFS, senza modificarlo." |
| "Details and resources" | "Dettagli e risorse" |
| "Context active" | "Contesto attivo" |
| "No context yet" | "Nessun contesto per ora" |
| "New files are classified with the archive's own language. Files already analysed are re-interpreted when you choose, one folder at a time too." | "I file nuovi vengono classificati con il linguaggio dell'archivio. Quelli già analizzati si rielaborano quando vuoi, anche una cartella alla volta." |
| "Describing the archive lets the AI classify the files with its own terms, categories and names." | "Descrivere l'archivio permette all'AI di classificare i file con i suoi termini, categorie e nomi." |
| "%1 terms" | "%1 termini" |
| "%1 categories" | "%1 categorie" |
| "%1 known names" | "%1 nomi noti" |
| "%1 documents" | "%1 documenti" |
| "Edit the context" | "Modifica il contesto" |
| "Describe the archive" | "Descrivi l'archivio" |

Existing strings reused by R1 (already translated, not edited): "Archive",
"Other", "About", "Settings", "AI analysis", "Open...", "New...", "SQLite
Database...", "Collection Folder...", "Hosted Database...", "Recent
collections", "Selection shown", "Selection hidden", "In Progress", "Paused",
"Waiting for the drive", "Pause", "Resume", "Stop", "Nothing running",
"Connected", "Not connected", "Data sent", "Nothing sent", "On this computer",
"Image", "Video", "Audio", "Text", "Re-interpret with the context",
"Reference documents", "%1 passages", "%1 waiting", "Queue", "Create",
"Update", "Remove", "Clear".

Their existing Italian does not always match the D34 names (`Q44`). "AI
analysis" read *Analisi IA*, not *Analisi AI*. It was aligned to *Analisi AI*
on 2026-10-30, a change to the translation only. "Waiting for the drive" reads
*In attesa dell'unità*, not *disco*. "Connected" reads *Connesso*, while the
approved Home text says *collegato*. "Re-interpret with the context" reads
*Reinterpreta con il contesto*, while the owner's wording was «Rielabora con il
contesto» (D33). "Selection shown" and "Selection hidden" now toggle the *I miei
dischi* column. Changing any of these needs per-string approval (`MDI-C17`).

No longer used by K3 since R1 (kept in the translation files, marked
vanished): the old navigation labels, among them "Selection", "Activity",
"Context", "Statistics", "Tags", "Tools", "Drawer pinned" and "Drawer
floating" — twelve in all, as reported by the main agent.

Since 2026-10-30, "Katalog reads the drive, NTFS too, without changing it." in
this list reads "Teca reads the drive, NTFS too, without changing it."
(`MDI-F123`).

### Recorded 2026-10-30 — D34, the name *Teca* and *My drives*

These are the owner's decisions, relayed by the main agent. The specifications
agent did not see the replies itself. New strings follow `MDI-C39`: English
source, translated into Italian only.

New strings:

| String | Where |
| --- | --- |
| "The drive or folder chosen here is the one searched, browsed and counted." | *My drives* description (`MDI-F125`) |
| "Teca is based on Katalog by Stéphane Couturier, released under the GNU GPL." | About (`MDI-F123`) |

Existing strings reused: "Used Space: " (K2), now on the used-space tag of a
drive card; "Show device info", now the label of the switch (`MDI-F125`).

Existing strings edited for the name only (`MDI-F123`; the owner's exception to
`MDI-C39`):

| Before | After | Translations |
| --- | --- | --- |
| `"<br/><b>Welcome to Katalog!</b>…"` (first-run welcome) | `"<br/><b>Welcome to Teca!</b>…"` | Every language, carried over with only the name replaced |
| "Welcome to Katalog 3. News, questions and feedback are shared on the project discussions:" (alpha-warning welcome) | "Welcome to Teca. Teca is based on Katalog: news, questions and feedback are shared on the Katalog project discussions:" | Reworded, so Italian only |
| "… and reading is all Katalog needs." (NTFS mount failure, `MDI-F90`) | "… and reading is all Teca needs." | Every language, carried over with only the name replaced |
| "Katalog reads the drive, NTFS too, without changing it." (R1, *Home* and *Add a drive*) | "Teca reads the drive, NTFS too, without changing it." | Italian only (an R1 string). Found in the code; it was not in the main agent's list |

The R2–R8 strings that name the application ("What Teca is doing, …", "Teca
searches its catalogs, …", "How Teca looks and behaves …", "The version of
Teca …") are new strings of those phases. They are approved by the owner's
instruction of 2026-10-30. These strings keep the name Katalog: "Katalog
Colors", "Katalog / File" and "Katalog / Memory".

**Italian.** Every D34 string is translated: `Katalog_it_IT.ts` had no
unfinished entry on 2026-10-30. The other languages show the English source
(`MDI-C39`). The Italian of the existing string "AI analysis" was aligned from
*Analisi IA* to *Analisi AI*, since every other Italian string says AI. Only the
translation changed, not the source.

### Approved 2026-10-30 — D34 follow-up *Browse never empty*

The owner approved the step's six new strings on 2026-10-30 ("Approvo tutte",
relayed by the main agent). They follow `MDI-C39`: English source, Italian
translation. They were committed in `faeef817` before this approval, while
`MDI-C44` (D16) asks for the approval before the commit.

| String | Italian | Where |
| --- | --- | --- |
| "The folders and files of a catalogued drive, even when it is not connected. A file tagged AI already has a preview, a description or a transcript: selecting it shows them in File details." | "Le cartelle e i file di un disco catalogato, anche quando non è collegato. Un file con l'etichetta AI ha già un'anteprima, una descrizione o una trascrizione: selezionandolo li vedi in Dettagli del file." | Browse description line (`MDI-F131`) |
| "Choose what to browse" | "Scegli cosa sfogliare" | Title of the card list (`MDI-F129`) |
| "Each card is a catalogued drive or folder. Opening it shows its folders and files." | "Ogni card è un disco o una cartella catalogati. Aprendola vedi le sue cartelle e i suoi file." | Description of the card list (`MDI-F129`) |
| "Nothing is catalogued yet: a drive added and catalogued shows here." | "Non c'è ancora nulla di catalogato: un disco aggiunto e catalogato compare qui." | Card list, collection without catalogs (`MDI-F129`) |
| "AI" | "AI" | File-row tag (`MDI-F130`) |
| "Choose another drive" | "Scegli un altro disco" | Link beside the open catalog's name (`MDI-F132`) |

Existing strings reused: "Connected", "Not connected", "Saved search
results", "%1 files", "AI: %1 files", "Open", "Add a drive" (also the action
of `MDI-F134`) and "Catalog" (the folder tag of `MDI-F133`; Italian
*Catalogo*). "No folder selected" is unchanged.

### Reused 2026-10-30 — *Move to Trash* confirmation (`MDI-F157`)

No new string. K2's strings, verbatim: "Confirmation",
`"Move\n%1\nto the trash?"`, `"Moved to trash:<br/>"`, "Move to trash
failed." (K2 Explore) and "Move this file to the trash?" (K2 Search). The buttons reuse "Move to Trash"
and "Cancel". Search results keep K3's "File moved to trash" and "Could not
move file to trash".

### Approved 2026-10-30 — D36 and D37

The owner approved the step's new strings on 2026-10-30 ("Approvo tutte",
relayed by the main agent; D37, second round): about 65 strings for
navigation, the scope chip, the History page and the history and recompose
messages, all in `AppManager` and QML (none in `core/`). They follow
`MDI-C39`: English source, Italian translation applied. The step was committed
in `fd76dd09` and `06d05ad1`. The full list is in the translation files; the
labels the owner's options named are:

| String | Italian | Where |
| --- | --- | --- |
| "Action history" | "Storico delle azioni" | Sidebar entry and page title (`MDI-F138`) |
| "Undo, earlier versions of the catalogs" | "Annulla, versioni precedenti dei cataloghi" | Its sidebar description |
| "Restore how it was before" | "Ripristina com'era prima" | Entry action and dialog (`MDI-F139`) |
| "Open as a separate catalog" | "Apri come catalogo a parte" | Entry action (`MDI-F141`) |
| "(version of %1)" | "(versione del %1)" | Name suffix of `MDI-F141` |
| "Join into one catalog" | "Ricomponi in un solo catalogo" | *Ricomponi* (`MDI-F142`) |
| "Undo" | "Annulla" | Action after a change (`MDI-F140`) |
| "Change" | "Cambia" | Scope chip (`MDI-F147`) |
| "Search in: %1", "Numbers of: %1", "Links of: %1" | "Cerca in: %1", "Numeri di: %1", "Collegamenti di: %1" | Scope chip (`MDI-F147`) |

Existing string reused: "All the drives" (*Tutti i dischi*), the breadcrumb's
root (`MDI-F145`) and the chip's second link. "Choose another drive" (approved
for `MDI-F132`) is no longer used. Known wording limit: "%1 catalog(s), %2
file(s)" is not plural-aware (`Q64`).

### No longer used (D15)

The one-page wizard (`MDI-F60`) no longer uses "Next", "Back", "Step %1 of %2"
and "Summary" (in `CreateWizard.qml`). They stay in the translation files until
the fork owner decides otherwise.

---

## Implementation status {#implementation-status}

### Phase A, increment 1 (A.1) — approved 2026-10-28

The user approved this increment in the main session ("ok tutto", 2026-10-28);
the main agent reported it to the specifications agent. That reply is the
`MDI-C14` approval for the core changes and the `MDI-C18` request for the new
files listed here. Nothing else is approved by it.

Approved new files:

| File | Content |
| --- | --- |
| `core/mdischema.{h,cpp}` | `MdiSchema::ensure` / `storedVersion`. Creates `mdi_schema(mdi_schema_version)` and `mdi_profile(profile_device_id` primary key, `profile_destination_path`, `profile_settings` JSON, `profile_date_updated)` when absent. |
| `core/mdiprofile.{h,cpp}` | `MdiProfile` load / save / remove. Memory mode: file `mdi_profile.csv` in the collection folder, tab-separated. |
| `qt_quick/CreateWizard.qml` | A component inside `PageCreateForm.qml`, not a Page (`MDI-C18` holds). |

Approved changes to existing files:

- `DatabaseManager::runMigrations` calls `MdiSchema::ensure` after the core
  migrations and, in Memory mode, `MdiProfile::loadFileToTable`. `Collection`
  is not modified: in Memory mode `runMigrations` runs after
  `collection->load()`, so the profile file is loaded there, once the table
  exists.
- `AppManager` (K3): `createCatalogWithEnrichment(...)` wraps the existing
  `createCatalog` (`MDI-C2`, `MDI-F7`). Pending profiles are keyed by the
  catalog's device ID and saved only in `onCatalogCreationCompleted`. A pending
  profile is dropped on error, on Stop (even when the catalog is kept), on
  removal of a queued creation, and on collection switch.
- `AppManager::isPathWritable`: a destination that does not exist yet is
  accepted when its nearest existing ancestor is writable.
  `AppManager::isPathInside`: compares canonical paths, case-insensitive on
  Windows and macOS.
- Settings keys `Create/Mode` and `Create/WizardDestinationPath` in the
  application settings file (added keys only, `MDI-C7`).

Wizard as first built in A.1 (5 steps: Source → Destination path → File types →
Previews → Summary) is superseded by the one-page layout of `MDI-F60` (D15).
The standard form's other options (file type, subdirectories, hidden files,
metadata, checksum, excludes) are hidden in wizard mode and used with their
current values, so the catalog is the same as in standard mode.

What A.1 covered per row (history; current coverage is in the next section):

| Row | Implemented in A.1 | Still planned after A.1 |
| --- | --- | --- |
| `MDI-F1` | All. | — |
| `MDI-F2` | Steps 1–4 and 6 (summary), back and next. | Superseded by `MDI-F60` (D15). |
| `MDI-F3` | All, with *Text* as the document type. | — |
| `MDI-F4` | All. | — |
| `MDI-F6` | All four wizard refusals; source checks reuse the Create page validations. | — |
| `MDI-F7` | Creation through the existing path; profile saved only on success; no profile on failure, Stop or queue removal. | Adding the enrichment job (done in increment 2). |
| `MDI-F8` | Profile saved per catalog; it survives closing and reopening the collection (File, Hosted, Memory). | See increment 2. |
| `MDI-C15` | `mdi_schema` and `mdi_profile` created by an idempotent migration outside the core schema; core schema 3.0 and `runMigration_3_0` untouched; Memory mode adds only `mdi_profile.csv`. | See increment 2. |

### Increment 2 (phases A to D, partial) — reported 2026-10-28

Reported by the fork owner in the main session and checked by the
specifications agent against the code on `work/dev-fork` (commit `000839ef`).
Work split (D18): `core/mdidata.*` and `core/mdienrichmentjob.*` by another
session; `qt_quick/` by the main session. `core/mdistore.*` is an unused draft,
not listed in `core/CMakeLists.txt` and not compiled.

Files added in this increment:

| File | Content |
| --- | --- |
| `core/mdidata.{h,cpp}` | `MdiData`: the store `mdi_data.db` (tables `mdi_schema`, `mdi_datum`, `mdi_embedding`), opened per connection with WAL and a 5 s busy timeout; upsert, incremental check, per-file and per-catalog reads, embeddings, semantic search, `clearCatalog` (not called yet). |
| `core/mdienrichmentjob.{h,cpp}` | `MdiEnrichmentJob`: the runner (worker moved to its own `QThread`), tool discovery, analyses, local Ollama calls, `embedTexts`. |
| `qt_quick/MdiDetailsDialog.qml` | The *Show analysis* and *Topic clusters* dialog, reported by the fork owner as part of the approved increment (`MDI-C18`). |

Settings keys added (application settings file, added keys only, `MDI-C7`):
`MediaIntelligence/FfmpegPath`, `FfprobePath`, `PdftotextPath`, `WhisperPath`,
`WhisperModelPath`, `OllamaUrl` (default `http://localhost:11434`),
`VisionModel` and `TextModel` (default `gemma4`), `EmbedModel` (default
`bge-m3`). There is no Settings UI for them yet.

What is built, per row (rows not listed are not started):

| Row | Built | Still planned |
| --- | --- | --- |
| `MDI-F5` | *AI analysis* switches: caption and tags, summary and excerpts, transcript, topic segments; segments turn the transcript on, turning the transcript off turns segments off; switches disabled when no matching file type is selected; local provider only, with the local-model note. Topic clusters follow segments; embeddings are always on. Summaries are asked in the source's language. | Provider choice; disabling an analysis with the reason when its tool or model is missing; separate clusters and embeddings choices; the UI-language option (D12). Captions are always asked in the UI language (see findings). |
| `MDI-F7` | Profile saved on successful creation, then the enrichment job starts (or waits) automatically. | — |
| `MDI-F8` | *Analyze* action in Explore: a dialog with the enrichment options, filled from the catalog's profile (or the last choices), the same four validations, saves the profile and starts the job (D8, `Q9`). | The same from the catalog's page (Devices). |
| `MDI-F9`, `F11`, `F13` | One file at a time, each analysis committed before the next; Pause before the next step, Resume; incremental on size, modified date, parameters (including prompt version) and presence of the derived files. | — |
| `MDI-F10` | Stop kills the running tool and aborts the HTTP request; the abandoned step is not recorded; finished results are kept. | The final status reads the existing "Cancelled", not *Stopped*. |
| `MDI-F12` | — | Resume after restart: the running job and the waiting list are not kept; reopening needs a new *Analyze* (which is incremental). |
| `MDI-F15` | The runner never blocks or refuses other operations. | Pausing on an update, re-scan or deletion of the catalog being enriched. |
| `MDI-F16` | One job at a time, in order; a catalog already running or waiting is not added twice. | Removing one waiting job (Stop clears the whole waiting list). |
| `MDI-F17` | Own row in the activity panel, built with the builder: operation, status, catalog, current analysis as process with file count, current file; Pause/Resume and Stop. | Time to completion. |
| `MDI-F18` | Final status line with the completed count; failed and skipped analyses shown with their reason in *Show analysis*. | Counts per analysis and outcome; the *Report* action. |
| `MDI-F20`, `F21` | `ffmpeg`, `ffprobe`, `pdftotext`, `whisper-cli` and a ggml model found automatically (configured path, usual locations, search path); a missing tool skips the analysis with a reason and the job goes on. | Settings UI (path, version, set/clear), `pdftoppm`, detection results shown before a job, per-job *skipped (tool missing)* count. |
| `MDI-F22` | Failure with the reason, next file; default limit 10 min per invocation (longer for transcripts of long recordings). | Configurable limit. |
| `MDI-F23`, `F26` | Local Ollama endpoint and models per task through settings keys; endpoints classified local / local-network / remote by host in `core/`, and any non-local endpoint is refused in `core/`. | Settings UI and connection test; commercial providers (`F23`–`F27`); classification through `Database::validateHostname()` (`HST-C1`). |
| `MDI-F29` | 320 px JPEG, aspect ratio kept. | Configurable size (128–1024); orientation not verified. |
| `MDI-F30`–`F34` | Poster at 10 %, keyframes evenly spaced with timecodes, H.264/AAC clip at most 360 px, M4A clip, PNG waveform; derived files under `<destination>/katalog_mdi/<catalog>/<analysis>/<hash>`; clips open with the system application. | — |
| `MDI-F35`–`F38` | ffmpeg 16 kHz mono + whisper.cpp, passages with timecodes and detected language, plus an `.srt` derived file; topic segments by the text model; clusters per catalog, recomputed when the run produced segments or when none exist; timecodes shown as `h:mm:ss`. | `Q3b` (when clusters are recomputed) is still open; the current rule is the implementation's, not a decision. |
| `MDI-F39` | Caption (at most two sentences asked) and tags from the thumbnail or a reduced frame, never the original; kept apart from user tags. | — |
| `MDI-F40` | PDF text with `pdftotext` (first 30 pages); summary, excerpts, tags. | Up to 10 excerpts with their page number (now up to 4, without page). |
| `MDI-F44`–`F46` | Explore: thumbnails replace the file-type icons (offline too); *Show analysis* when the file has data; the same from semantic results; derived text shown from the store when the device is offline. | A marker in the Search results table; a "not available" placeholder for missing derived files (the type icon shows instead); *Open at timecode* per segment (clips open, without seek). |
| `MDI-F47`, `F48` | *By meaning* section on the Search page, one text query, results in their own list above the table, grouped by file (best passages, kind, score, timecode), with *Open file* and *Show analysis*. | — |
| `MDI-F49` | Scoped to the active catalogs of the Selection; only vectors of the current embeddings model are compared. | Reporting how many stored vectors were not compared. |
| `MDI-F52`, `F54` | Provenance per datum: file, analysis, tool (version for ffmpeg), model name, provider, class, host, date, file size and date, parameters with prompt version, outcome and reason. | Model version or digest; cost (no remote provider yet). |
| `MDI-F55`, `C4`, `C5` | `mdi_data.db` in File and Memory modes; runner connection on its thread, separate main-thread read connection (D17). | Hosted mode (`Q11`). |
| `MDI-F58` | Derived data is in its own file; no metadata level touches it. | — |
| `MDI-C9` | Cosine similarity in `core/` over stored unit vectors. | Batches, Stop and progress; the scan runs on the UI thread (see `MDI-F51`). |
| `MDI-C15` | `mdi_schema` + `mdi_profile` in the collection database; `mdi_schema`, `mdi_datum`, `mdi_embedding` in `mdi_data.db`. | Transcript passages, segments and clusters are JSON values of `mdi_datum`, not their own tables, and there is no job or derived-file table: the table list of `MDI-C15` is not met as written (fork owner to decide). |
| `MDI-C19`, `C20` | Derived-file names are hashes; HTTP limit 120 s, aborted on Stop, unparsable answers recorded as failed. | Executable suffix not verified; configurable HTTP limit. |

Core of `MDI-F35`–`F38` (whisper transcripts, topic segments, catalog clusters)
and `MDI-F47`–`F49` (semantic index of summaries, captions, segments and
passages) implemented and tested headless, as reported by the main agent:
commits `cae1cc1e` and `83ccce70`. Core implemented; UI wiring partial (see the
"Still planned" column above).

### Activity phase (D19, D21) — reported 2026-10-28

Reported by the main agent and checked by the specifications agent against the
code on `work/dev-fork`: commit `97ca0888` (core resource monitor
`core/mdisystemmonitor.*`, `activity` signal of the runner) and commits
`1350e36e`, `7116655b`, `e79d6cc6` (K3: `qt_quick/PageActivity.qml`, the
side-menu entry *Activity*, the *Activity* button on the enrichment row of the
bottom panel). The monitor samples only while the page is shown or a job runs.
The resource history keeps 900 samples at 2 s (30 min) for the session; the log
keeps the last 500 finished steps for the session.

Defaults applied by the implementation, not decided by the fork owner (the
owner may change them; `Q16` and `Q17` stay open until then):

- `Q16`: the log of finished steps is kept for the current session only, last
  500 steps.
- `Q17`: sampling interval 2 s; the monitor runs only while the Activity page
  is shown or a job runs.

| Row | Built | Still planned |
| --- | --- | --- |
| `MDI-F61`, `F62`, `F66`, `F69` | All. The waiting jobs show in the status line. | — |
| `MDI-F63` | All tiles; unavailable values read "—". | Egress names the tool, not the provider's host, and comes from the session's finished steps, not from the running job. |
| `MDI-F64` | Analysis, file, tool, model, where, progress; audio minutes for a transcript. | Tokens per second during a model call (shown in the log after the step only). |
| `MDI-F65` | `ffmpeg`, `ffprobe`, `pdftotext` with version; `whisper.cpp` with its model; Ollama with its loaded models and size; not found. | Version of `whisper.cpp` and of Ollama. |
| `MDI-F67` | Newest first, with time, analysis, file, duration, outcome, reason; 500 steps kept (`Q16` default). The page lists the newest 60. | — |
| `MDI-F68` | CPU, GPU and memory charts, session only, not persisted. | — (see findings: 30-minute window). |
| `MDI-F70` | macOS and Linux values; unavailable values absent; 2 s (`Q17` default). Windows: no values, shown as "—". | The source-drive check runs in `AppManager` (`qt_quick/`), not in the core monitor. |
| `MDI-F71`, `F72` | All. | — |
| `MDI-F73` | Windows values are unavailable and read "—". | Not tested on Windows. |
| `MDI-F74` | Start and end per step: analysis, file, tool, model, where, duration, outcome, reason. | Tool version and endpoint host in the report; the reports are not built with `StatusBarMessageBuilder`. |
| `MDI-F75` | Audio minutes of a transcript, from `whisper.cpp` progress. | Tokens per second during a model call (now computed at the end of the call). |
| `MDI-C21` | macOS and Linux sources as listed; monitor on its own thread; Ollama `/api/ps` over `Qt6::Network`. | Tool versions (`MdiSystemMonitor::toolsInfo`) are read on the UI thread through `AppManager::mdiToolsInfo`. |

### Increment 3 (D22–D30) — reported 2026-10-29

Reported by the main agent as built on `work/dev-fork`; the specifications
agent checked the triggers and gates named below against the code, not the
measurements. Rows `MDI-F76`–`F82`, `F84`, `F85`, `F87`–`F90` and
`MDI-C22`–`C27` are implemented; `MDI-F83` and `MDI-F86` are backlog.
`MDI-F29` and `MDI-F31` are superseded by `MDI-F78` and `MDI-F79` (D23).

- **Instant search index (reported measurements).** 312 of 312 identical result
  sets on a real catalog and 120 of 120 on a synthetic catalog of 1,002,310
  files; 1.7 s → 0.17 s per search; the index takes about 900 MB per million
  files.
- **Quick hash.** Checked against an independent implementation (reported).
- **Triggers.** The index rebuild and the quick-hash pass are started from
  `AppManager` (`qt_quick/`) after collection open and after creation or
  update, never from `CatalogJobStoppable`, `CatalogManager` or
  `DeviceUpdateManager` (`MDI-C2` holds). Hosted mode is skipped for both.
- **Settings keys** added (added keys only, `MDI-C7`): `View/InspectorVisible`,
  `View/ExploreColumns`.

### D31 (resume after unplugging the drive) — reported 2026-10-29

Reported by the main agent as built on `work/dev-fork` (uncommitted); the
specifications agent checked the working-tree diff of `core/mdidata.*`,
`core/mdienrichmentjob.*`, `qt_quick/appmanager.*`, `PageActivity.qml` and
`OperationQueueView.qml` against the rows. This section supersedes the
increment 2 rows for `MDI-F11`, `F12` and `F16`.

- **Core harness (reported, synthetic data, 24 of 24 checks pass, three
  consecutive runs).** Resume
  with the drive away: the job waits, processes nothing, and continues after the
  drive is back (1,200 of 1,200 data, 0 skipped or failed); a second run
  processes zero files. Drive unplugged mid-run leaving an empty mount point:
  the job waits, commits nothing, Stop ends it, no failed datum. A deleted file
  with the drive present is skipped without waiting. A quick-hash pass with the
  drive away ends without waiting. A job started paused decodes no thumbnail
  until Resume, then 70 of 70. `mdi_job` round trip: order, paused flag, empty
  after Stop. Destination drive away for a resumed job: the job waits, commits
  nothing, then 70 of 70 once it is back, none failed. A destination folder not
  created yet is created, without waiting.
- **Fix (`MDI-F84`).** A quick-hash pass also computed the catalog's topic
  clusters with the local model at its end, when the catalog had topic segments
  without clusters: the clusters block of `run()` was not gated (seen in the
  owner's data, catalog 2, 2026-10-29 19:00:51). The block now requires
  `!quickHashOnly`; a quick-hash pass computes quick hashes only and asks no
  model, and the clusters are computed by the next enrichment run. Harness still
  24 of 24.
- **Limit (Linux).** A destination under a fixed mount point the user owns
  can still be created when its drive is gone, so the job writes into the empty
  mount point on the system disk instead of waiting (`MDI-F91` waits only for a
  destination that cannot be created).
- **Not verified by the harness** (UI level, manual test charter): restore after
  restart (`MDI-F12`), stepping aside (`MDI-F92`), switching collection
  (`MDI-F93`), and the step whose source is lost mid-step (logic only).
- **Core changes** (`MDI-C29`): `Config::sourceRoot`; the waiting state in
  `run()`, through two private helpers (`driveAway()`, returning the path waited
  for, and `waitForSource()`); the `waiting` event of the `activity` signal,
  with `drivePath`; the Pause check at the top of each
  file iteration, before the chunk pre-pass, and in each queued pre-pass decode;
  `mdi_job` in `MdiData::open`; `MdiData::PendingJob`, `pendingJobs()`,
  `savePendingJobs()`. No settings key, no core table.

| Row | Built | Still planned |
| --- | --- | --- |
| `MDI-F11` | Pause is checked at the top of every file iteration and again after a wait for the drive, before the chunk pre-pass; a queued pre-pass decode returns without reading when the job is paused, and that file takes the sequential path after Resume. | — |
| `MDI-F12` | The running job and the waiting jobs are restored in order when the collection opens (startup and switch); a job paused when Katalog closed comes back *Paused*; with a non-local Ollama endpoint every restored job comes back *Paused* (D6); a restored job without its source or destination drive is launched and waits for it (`startEnrichment(deviceId, resumed)`); jobs whose source is reachable are ordered first (`MDI-F92`). Locality for D6 is decided in `qt_quick/` from the Ollama URL host (`localhost`, `127.0.0.1`, `::1`), not with the core classification of `MDI-F26`: any other loopback form restores *Paused*, the safe side. A job restored *Paused* under D6 and not resumed is saved as paused, so it stays *Paused* after the next restart even if the provider has become local (the safe side). A pending job that cannot start for another lasting reason (no enrichment profile, or a destination that exists but cannot be written) is kept (`AppManager::holdOrDropMdiJob`, held list): saved in `mdi_job` after the running and waiting jobs with its paused flag, listed among the waiting jobs on the Activity page (also when nothing runs), and tried again when the collection is opened again or when the user starts it; only a job whose catalog no longer exists leaves the list (logged). Stop empties the held list; a collection switch leaves it saved in the left collection. | — |
| `MDI-F16` | A request for the catalog already running is now ignored too (before D31 only the waiting list was checked). | Removing one waiting job (Stop clears the whole waiting list). |
| `MDI-F91` | All, the destination drive included (`driveAway()`: the source first, then a destination that does not exist and cannot be created). A step whose drive is lost reports the outcome *stopped* in its activity `end` event (the step log shows the stop icon); it is not recorded. The activity-panel status reads "Waiting for the drive" with the catalog and the path waited for as Current Item; the Activity page status line and "Right now" card show the wait; no busy indicator, no progress bar, no estimated end while waiting. | — |
| `MDI-F92` | All: on a job being queued, on the running job starting to wait, at every monitor sample (2 s) while it waits, and when the next run is chosen. | — (see `MDI-C29`) |
| `MDI-F93` | All. The step log and status of the left collection are cleared. | — |
| `MDI-C28` | All. The list is written when a job is queued, starts, is paused or resumed, ends, is stopped, steps aside, or leaves the list (`MDI-F12`); quitting and switching collection write nothing. | — |
| `MDI-C29` | All. The runner's check is on the runner's thread, every 2 s while waiting; Pause and Stop wake it. The waiting catalogs' source paths are checked on the UI thread by `AppManager::mdiSourceReachable` (a device load and a directory listing per waiting job), as amended. | — |

### Fixes after D31 — reported 2026-10-29

From the owner's real run on catalog 2; the owner asked for both ("correggi").
Checked by the specifications agent against the working-tree diff of
`core/mdienrichmentjob.cpp`.

- **`.doc` text on macOS (D32, `MDI-F81`).** 71 `.doc` files gave "no text
  extracted (application/msword)": KFileMetaData's Office extractor needs
  `catdoc`, absent on macOS. `MdiEnrichmentJob::extractText()` now runs
  `/usr/bin/textutil -convert txt -stdout <file>` through `runTool` (argument
  list, time limit, Stop) for a `.doc` when KFileMetaData returns no text, on
  macOS only (`Q_OS_MACOS`); the datum's tool is `textutil`. Other platforms are
  unchanged.
- **Video without an audio track (`MDI-F35`).** A `.m2v` elementary stream made
  `ffmpeg` fail ("Error opening output files: Invalid argument") and the
  transcript was recorded *failed*. `makeTranscript()` now asks `ffprobe`
  (`-select_streams a -show_entries stream=index`) first; no audio stream gives
  *skipped*, reason "no audio track", with no AI call and nothing written. Without
  `ffprobe`, or when `ffprobe` itself fails, the previous path runs. Outcome
  classification only.

### Context phase 1 — core design (D33) — approved 2026-10-29

The fork owner approved this core design on **2026-10-29** in the main session,
answering an explicit question prompt; reported to the specifications agent by
the main agent, which did not see the reply itself. This is the `MDI-C14`
approval of the core changes of `MDI-C32`; nothing else is approved by it.
Question: "Approvi queste aggiunte al core di Katalog per la fase 1 del
Contesto? • MdiContext (nuovo): salva e legge il contesto; prepara i testi per
l'AI (premessa di dominio, suggerimento per la trascrizione, prompt di
classificazione); riporta le risposte alle tue categorie e ai termini
preferiti; propone la bozza dal catalogo con l'AI locale. • MdiKnowledge
(nuovo): aggiunge file e cartelle di riferimento, li elenca, li toglie solo
dall'elenco (mai dal disco), divide il testo in passaggi e trova i 5 più
pertinenti. • MdiData: tre tabelle nuove in mdi_data.db (contesto, documenti,
passaggi) e tre colonne nella lista dei lavori (tipo di lavoro, cartella, tipo
di file). • MdiEnrichmentJob: l'analisi "classificazione", il contesto nei
prompt, l'opzione per la trascrizione, la rielaborazione su una cartella o un
tipo di file, l'indicizzazione dei documenti su un thread a parte, la lettura
dei metadati estesi. • MdiFts: la classificazione entra nella ricerca
istantanea e nella ricerca per significato." → "Approvo (Consigliato)".

Before asking, the main agent told the owner that `MdiContext` and
`MdiKnowledge` had been started before this approval (commits `3c40288c`,
`920aacbc`, `48d455ef`) and would be removed if it was not given. The owner
approved what each class does; the method names below are the main agent's.

| Class (file) | Approved design |
| --- | --- |
| `MdiContext` (`core/mdicontext.{h,cpp}`, new) | JSON-based static helpers: `load(conn, scope)`, `save(conn, scope, json)`, `remove(conn, scope)`, `version(json)`, `isEmpty(json)`, `language(json, fallback)`, `preamble(context, catalogNotes)` (sections of the caption, summary, segments and clusters prompts), `whisperHint(context)`, `classificationPrompt(...)`, `normaliseClassification(answer, context)` (category values and preferred forms, `MDI-F101`), `draftPrompt(folders, samples, language)`, `parseDraft(answer)`, and a synchronous `askText(ollamaUrl, model, prompt, &error)` for the draft, called on a worker thread, never on the UI thread. |
| `MdiKnowledge` (`core/mdiknowledge.{h,cpp}`, new) | `supportedSuffixes()` (the formats of `MDI-F104`, archives excluded), `addPaths(conn, paths)` (folders expanded to those formats), `documents(conn)`, `removeDocument(conn, id)` (from the list only, never from the disk), `pendingDocuments(conn)`, `pendingCount(conn)`, `storePassages(...)`, `setFailed(...)`, `passages(text, 1000, 150)`, and an in-memory `Index` (`load(conn, model)` once per job, `search(queryVector, 5, minScore)`): no reload per file. |
| `MdiData` | `mdi_context`, `mdi_kb_document`, `mdi_kb_passage` created in `open()`; `mdi_job` gains `job_kind` (`enrich` or `reinterpret`), `job_folder`, `job_file_type`, added with `ALTER TABLE … ADD COLUMN` when missing; `PendingJob` gains kind, folder and file type (`MDI-C36`). |
| `MdiEnrichmentJob` | Knowledge-base indexing: a separate instance in knowledge mode on its own `QThread`, outside the enrichment list, reusing `extractText`, `runTool`, Stop and the `activity` signal (`MDI-F105`, `MDI-C33`). Re-interpretation: a mode with its folder and file-type scope, planning the classification and the embeddings refresh it implies (`MDI-F103`). New analysis `classification`: parameters are the model and the prompt label `classification-v1` only; the context version and the knowledge-base sources are stored in the datum's value (`MDI-F99`, `MDI-C30`). The context's language is used in the prompts while the parameters keep D12's language (`MDI-F98`). The transcription option (`MDI-F98`). `FileItem` gains the extended metadata, read by `listCatalogFiles` in both modes (`MDI-C34`). |
| `MdiFts` | `derivedText` includes the classification text; embeddings gain the kind `classification` (`MDI-F102`). |

Resolved by the main agent on 2026-10-29, checked by the specifications agent
against the rows:

- **Knowledge-base indexing** runs as a separate instance on its own thread,
  outside the enrichment list, so enrichment keeps working: `MDI-F105` and
  `MDI-C33` as written, no amendment.
- **Which files *Analyze* classifies** follows `MDI-F100` exactly: new (no
  datum of the profile's other analyses, the quick hash aside) or changed
  (size or modified date differ), and no current classification.
- **Pending documents at collection open** are indexed again automatically
  only while the embeddings provider is local (D6).
- **Retrieval threshold.** `Index::search` also drops passages below a minimum
  similarity, so fewer than 5 can be given; `MDI-F106` ("at most 5") holds.
  The threshold is an implementation default.
- **Table name.** The passages table is `mdi_kb_passage` (document, index,
  text, model, vector; key document + index), the owner's "passaggi";
  `MDI-C30` and its test line now use that name.
- **Formats.** `supportedSuffixes()` covers `MDI-F104` as ratified, including
  the text formats of `MDI-F81` (checked).

Decided by the fork owner on 2026-10-29 (explicit question prompts, relayed by
the main agent):

- **A re-interpretation for a catalog already listed.** "Se chiedi "Rielabora
  con il contesto" per un catalogo che ha già un'analisi in corso o in coda,
  cosa deve fare Katalog?" → "Metterla in coda a parte (Consigliato)": queued as
  its own item; a duplicate is the same catalog, kind and scope. `MDI-F16`
  amended.
- **`MDI-C1`.** "La nuova pagina "Contesto" va aggiunta anche all'elenco dei
  file nel progetto Katalog 2 (qt_widgets/CMakeLists.txt) …?" → "Sì, come le
  altre (Consigliato)". See *Findings for the fork owner*.

Noted, no decision needed:

- **`mdi_` schema version.** The `mdi_job` columns are added without raising
  the `mdi_` version, following the D31 precedent (accepted by the main
  agent). An older fork build that rewrites the list drops the kind and scope,
  so a re-interpretation comes back as an enrichment job.
- **Draft on the Activity page.** The draft's model call is not an enrichment
  step, so `MDI-F64` and `MDI-F74` do not report it. No row requires it.

### Context phase 1 (D33) — reported 2026-10-29

Reported by the main agent as built on `work/dev-fork` (partly committed by the
owner's editor, partly in the working tree). The specifications agent checked
`core/mdienrichmentjob.cpp`, `core/mdicontext.cpp`, `core/mdiknowledge.cpp`,
`core/mdifts.cpp`, `qt_quick/appmanager.cpp`, `CreateWizard.qml`,
`Inspector.qml`, `MdiDetailsDialog.qml` and `PageContext.qml` against the rows,
not the measurements.

- **Tests (reported).** Core harness with the local Ollama, 20 of 20: context
  storage; closed and single-value categories and preferred forms; knowledge
  base add, index and search; three new files classified with a closed-list
  value ("Carabina 10 m"), a single value ("Classifica"), an Italian description
  and a cited reference document; a second run processes nothing; a
  re-interpretation limited to one folder; the `mdi_job` kind and scope round
  trip. D31 harness still 24 of 24. The Context page loads. A draft on the
  owner's catalog 2 (read-only) proposed years, events, places and names, with
  errors (period 2013–2020, "Ginnastica") that the owner corrects before
  saving, as `MDI-F97` intends.
- **Prompt versions.** `caption-v1`, `summary-v3`, `segments-v1`,
  `clusters-v1` are unchanged; the context is not in any parameter (`MDI-F99`
  holds). The classification's parameters are the text model and
  `classification-v1`.
- **Locality.** The draft (`MdiContext::askText`), the embeddings and the
  classification refuse a non-local endpoint in `core/` (`MDI-F108`,
  `MDI-C11`).
- **Catalog list of the Context page.** `AppManager::contextCatalogs()` calls
  `MdiFts::catalogsNeedingPass` with a pass name that is never recorded, to get
  every catalog without a new core method. Read-only; it works, but depends on
  that method's internals.

| Row | Built | Still planned |
| --- | --- | --- |
| `MDI-O8` | — | Met when the rows below are. |
| `MDI-F16` | Duplicates judged by catalog, kind and scope; a re-interpretation is queued as its own item. | Removing one waiting job (as before). |
| `MDI-F89` | *Context* in the *Tools* submenu; main entries unchanged. | — |
| `MDI-F94`, `F95`, `F97`, `F107` | All. The draft runs on a worker thread, fills only empty fields and adds missing items, and never saves. | — |
| `MDI-F96` | Version (content hash) and date; the version a datum records covers the collection context and the catalog's notes; the classification records it. | Captions, summaries, segments and clusters made with a context do not record its version. |
| `MDI-F98` | Context sections and the context's language in the caption, summary, segments and clusters prompts; the `whisper.cpp` initial prompt only with the option on. | Without a context language the classification is written in the UI language, not the source's (D12), like captions (see *Caption language*). |
| `MDI-F99`, `F108` | All. | — |
| `MDI-F100` | The analysis, its inputs (technical data and archive entry names included, owner 2026-10-29), its order, and the new-or-changed rule (computed before the file's other analyses; quick hash and classification left out). Without a context it is not planned, and the *Classification* switch is disabled. | The switch shows no reason (a reason needs a new string, `MDI-C17`). Its `enabled` test is "the stored context has any key", read once when the form is built, while the core uses `MdiContext::isEmpty`: a context saved with every field empty leaves the switch on although nothing is classified. |
| `MDI-F101` | Closed lists, preferred forms, single values, free tags apart, user tags untouched. | Dropped values are not recorded in the datum; entities not in the list are not marked. |
| `MDI-F102` | All: title, description, categories, tags, entities and reference documents in the Inspector and *Show analysis*; classification text in the instant search index and in semantic search (kind *classification*). | — |
| `MDI-F103` | All: scope by folder and file type; classification and embeddings only; no drive needed; files whose completed classification carries the current context version are skipped, so a repeat or a resume redoes nothing; confirmation with the file count and an estimate, the time left unknown (only the count shown) when no classification was measured; restored with its scope. | — |
| `MDI-F104` | Formats, archives left out, remove from the list only, passages kept offline. | A document that cannot be reached stays *To be indexed*; there is no *not reachable* state in the list. |
| `MDI-F105` | Own runner instance and thread, extraction (all PDF pages, plain text up to 8 MB), passages of 1,000 characters with 150 of overlap, local embeddings, Stop keeps finished documents; status line on the Context page built with the builder. | Progress is not in the activity panel or on the Activity page (the instance's `activity` signal is not connected). A changed document is detected only when it is added again. |
| `MDI-F106` | At most 5 passages above a minimum similarity; passages of another embeddings model not used. | The document list does not show them as needing indexing again. |
| `MDI-C30`, `C32`–`C34`, `C36` | All. | — |
| `MDI-C31` | The runner, a re-run and the quick-hash pass never write the context or the knowledge base. | Deleting a catalog does not remove its notes (`MDI-F57`'s deletion path is not built); *Clear derived data* is not built (`MDI-F59`). |

Finding resolved by the fork owner on 2026-10-29 (explicit question prompt,
relayed by the main agent):

- **Extra classification inputs.** "Per classificare un file, oltre a
  percorso, date, metadati, descrizioni e trascrizioni, Katalog legge anche i
  dati tecnici (durata, formato) e i nomi dei file dentro un archivio zip. La
  specifica non li elenca. Li tengo?" → "Sì, tienili (Consigliato)". Added to
  `MDI-F100`'s inputs.

### Phase R1 (D34) — code committed before the rows were ratified, reviewed 2026-10-29

Commits from `c1f037c8` onward built phase R1 while its rows were being
drafted. They ran from 23:44 on 2026-10-29; the first review went up to
`980650fc`, and this update covers up to `15bec997` (sidebar layout only).
`28ca0c0f`, `c994d57d`, `8c892e90`, `03a60bb5` and `0683c4aa` also carried
this spec's D34 edits, which were committed while they were being written. The main agent
told the fork owner that R1 had been committed before ratification; the owner
then settled `Q32`–`Q38` and approved the choices made in code (D34). The code
was read against the rows, not as a source of requirements.

**Resolved by the owner's answers or by later commits (2026-10-29).**

| Row | First finding | Now |
| --- | --- | --- |
| `MDI-C37` / `MDI-F115` | Pixel text scale and fixed hex palette, outside `SpecTypography.md` and `SpecTheme.md` | Owner's exception ("Fisse come nel prototipo", `Q34`, `Q35`). The monospace font now falls back per platform. |
| `MDI-C38` | `homeOverview()` read every catalog's file list on the UI thread | `0463f88b`: the counts run on a worker thread with their own read-only connections (File: a read-only SQLite connection; Memory: the `.idx` file; Hosted: file-type counts skipped), are cached, and Home refreshes on `homeOverviewChanged`. The UI thread reads no file list. No raw SQL in `qt_quick/`, no core change. |
| `MDI-F111` | Navigation choices made in code (`Q36`) | Approved ("Approvo tutto"); the row now states them. |
| `MDI-F112` | Start page and Home content made in code (`Q37`) | Approved; Katalog always opens on Home, and the last page is no longer restored at launch. |
| `MDI-F113` | Bottom panel retitled; *Manage drives* not in the nomenclature | Approved (`Q36`, `Q38`); both are in the nomenclature table. |
| `MDI-C39` | New strings not approved, `.ts` not updated | The 45 strings were approved with their Italian ("Approvo tutti"). The Italian file is complete; every other `.ts` file lists them unfinished. Twelve old navigation labels are marked vanished. |

**Still unmet or open.**

| Row | Finding |
| --- | --- |
| `MDI-C37` | Three sizes are still set outside `KTheme.qml`: the sidebar group titles in `Main.qml` (11 px, plus their own letter spacing), `KMetricCard.qml` (26 px) and `KSectionHeader.qml` (`titleCard + 1`). `KButton.qml` is covered by the owner's instruction of 2026-10-30 (`Q43`). |
| `MDI-F111` (`PGL-O3`) | The sidebar is `modal: false` at a fixed 264 px width. Phone width is untested, and the exception is the owner's choice (`Q41`). |
| `MDI-C38` (`Q42`) | Home checks each catalog's source path on the UI thread at every refresh. The owner accepted this risk for queued catalogs only (`MDI-C29`). |
| `MDI-C38` (observation) | `countHomeDrive` starts one thread per catalog not yet counted, all at once. A collection with hundreds of catalogs starts hundreds of threads and connections on its first Home. No row bounds this. It is a resource risk for the owner to weigh, not a breach. |
| — (observation) | `KSidebarItem.qml` elides its title and description. No row forbids it (`CDT-C1` covers cards), but a long translation of a sidebar name would be cut. |

### Phases R2–R8, *Teca* and TecaStyle (D34) — reported 2026-10-30

The main agent reported phases R2–R8 implemented (`MDI-F116`–`F122`,
`MDI-C40`), together with the Italian of every D34 string and the rows
`MDI-F123`–`F125`, `MDI-C41` and `MDI-C42`. The work was committed up to
`cba65b5b`, before the last five rows were written. The specifications agent
did not review every page against its row. It checked only the points below,
and read the code against the rows, not as a source of requirements.

Checked against the rows: the `TecaStyle` module and its selection in
`main.cpp` on every platform; no new Qt module (`Qt6::QuickControls2` was
already linked); the palette roles in `Main.qml`; the dialog palettes;
`styleHeader`; the Italian file complete; "AI analysis" reading *Analisi AI*;
the settings file name and the `KAboutData` component name unchanged; the
*My drives* header and the "Used Space: " tag; *by content* absent from R3
(`Q45`).

**Findings for the fork owner (not authorised by any row).**

- **TecaStyle radii.** The style files set their own radii: 10 px for fields,
  buttons and menus, 8 px for tool buttons and spin-box buttons, and 4–7 px
  elsewhere. `MDI-C37` asks for every radius to be defined once, in
  `KTheme.qml`, where the control radius is 12 px. A style module cannot read
  `KTheme`. Accept the style's own radii as an exception, or align them to
  `KTheme` by hand?
- **Executable name on Linux and Windows.** `OUTPUT_NAME "Teca"` is not limited
  to macOS, so the executable is also `Teca` on Linux and `Teca.exe` on
  Windows. The K3 packaging still expects `Katalog`:
  `Katalog_3_Build_macOS.yml` uses `Katalog.app`, and
  `Katalog_3_Build_linux_qt6_appimage.yml` and `Katalog_create_appimage_local.sh`
  use `usr/bin/Katalog`.
- **Packaged colour theme.** For packaged builds, `DPL-C6` copies `Theme.qml`
  into Kirigami's `styles/Fusion` folder. With `TecaStyle` as the Controls
  style, Kirigami may no longer load that file. `Main.qml` now sets
  `Kirigami.Theme` from `KTheme` itself. This is unverified on a packaged
  build.
- **Bug address.** About's bug-report address now points to
  `github.com/archydevil/Katalog/issues` instead of the upstream project. This
  is not among the decisions relayed.
- **Stale comments.** The opening comments of `ComboBoxFitted.qml` and
  `IconButton.qml` still say Fusion is forced on Windows and macOS and Breeze
  is used on Linux. Only comments are affected.
- **German "Used Space: ".** The German translation reads *Espace utilisé:*
  (French) in every context, K2's included. This predates the fork, but the
  drive card now shows it.

### Split carry-over (D35) — finding, core design and report, 2026-10-30

**Finding (2026-10-30).** Splitting a catalog (`SpecDevicesSplit.md`) goes
through `Collection::executeSplitBySubDirectory` / `executeSplitByFileType`,
then `Catalog::executeSplitBySubDirectory` / `executeSplitByFileType`, then
`Collection::applySplitResult`. It moves the file rows to new catalogs with new
catalog IDs and removes the old catalog's device. The fork's derived data
(`mdi_datum` and `mdi_embedding` in `mdi_data.db`, keyed by catalog ID and full
path) and the enrichment profile (`mdi_profile`, keyed by device ID) were not
moved. Every preview, description, transcript and classification of the split
catalogs was therefore invisible in Browse, Search and Home. No row said what a
split does to the fork's data: a gap in the spec, which D35 fills. The main
agent reports the finding as verified on the owner's data.

**Manual relink (2026-10-30).** The main agent reports that the owner's 14,545
analysed files were relinked by hand the same day, after a backup
(`katalog 3/backup_prima_del_ricollegamento_AI_20261030/`). The specifications
agent did not check the relink. `MDI-F126`–`F128` prevent the loss on the next
split.

**Approved core design.** The owner's "Sì, correggi" (D35) is the `MDI-C14`
approval of these changes. The signature is the main agent's.

| Class | Approved design |
| --- | --- |
| `MdiData` | `static int moveToCatalogs(const QString &connectionName, int oldCatalogId, const QHash<QString, int> &newCatalogByPath)`. Takes an open connection, like `clearCatalog` and `forCatalog`. One transaction: `UPDATE OR IGNORE` on `mdi_datum` (a colliding row stays on the old catalog), `UPDATE` on `mdi_embedding`. Returns the number of `mdi_datum` rows moved. |
| `Collection` | `applySplitResult`: when the mode is not Hosted and `mdi_data.db` exists, opens it with `MdiData::open` on its own connection, builds the full path → new catalog ID map from the new catalogs' `file` rows, calls `moveToCatalogs` and closes the connection. Copies the old device's `MdiProfile` to each new catalog's device (`load` / `save`, and `saveTableToFile` in Memory mode). |

**Points for the owner (not authorised by any row).**

- **Catalog notes.** The old catalog's notes (`MDI-F95`, in `mdi_context`
  under its device ID) are not part of D35. They stay under the removed device
  and no longer show (`Q49`).
- **Rows left behind.** These stay under the old catalog ID and show nowhere:
  derived data whose file is in none of the new catalogs, a colliding datum,
  the old quick-hash pass stamp (`mdi_pass`) and the old instant-index rows.
  Nothing deletes them: `MDI-F57` is not built, and it does not apply to a
  split (`MDI-F126`). The new catalogs have no pass stamp, so under `MDI-F84`
  their files are hashed again when their source is reachable.
- **Embeddings.** `mdi_embedding` has no key, so its `UPDATE` never collides.
  When a datum stays on the old catalog because of a collision, the same
  file's embeddings still move. New catalogs get new IDs, so a collision right
  after a split is unlikely.
- **K2 and upstream.** The change is in core, so a fork build of K2 carries
  the data on a split too, with no K2 file changed (`MDI-C1`). Upstream K2 and
  K3 3.0 do not: splitting an enriched collection with them loses the link
  again.
- **Pending jobs.** A waiting job of the old catalog leaves the list, as
  `MDI-F12` states for a catalog that no longer exists. It is not moved to the
  new catalogs.

**Reported implemented (2026-10-30, commit `faeef817`).** The main agent
reports `MDI-F126`–`F128` and `MDI-C43` as built. The specifications agent read
the core diff against `MDI-C43` and found no other new method. The profile is
copied before the old device is removed, and only to each new catalog's own
device. `AppManager::splitCatalogBySubDirectory` and the split by file type
now call `scheduleFtsIndexing()` after the split (`MDI-F128`). **Verification
is at unit level only:** a test of `MdiData::moveToCatalogs` on a synthetic
`mdi_data.db` passed 9 of 9 checks (rows moved per path, collision kept,
unmapped file kept, embeddings moved, empty map changes nothing). The glue in
`Collection::applySplitResult` (the map built from the `file` table, the
profile copy, Memory mode) and the index build after a split were not
exercised end to end. The `MDI-F126`–`F128` charter lines are still to be run
on a real split.

### Browse never empty (D34 follow-up) — reported 2026-10-30

The main agent reports `MDI-F129`–`F131` and `MDI-C44` as built and checked
in the running app (commit `faeef817`). The specifications agent checked the
commit for `MDI-C44`: no core change for these rows (the core files are those
of `MDI-C43`), no new file, and no new `QSqlQuery` in `qt_quick/`. The cards
also show the existing "Connected", "Not connected" and "Saved search results"
tags (presentation).

Fixes made in the same step, inside existing rows:

- **Home's AI counts** (`MDI-F112` amended, `MDI-F129`). Home's "AI: %1 files"
  tags and its *Files with AI results* number counted every file with any
  completed datum, quick hash included, so they always equalled the file
  count. They now use the definition of `MDI-F130`. On the owner's data the
  number now reads 14,001 of 35,002, where it read 14,545.
- **Home's drive-card names** now wrap instead of being cut. Cutting them
  broke `CDT-C1`, which `MDI-C38` keeps binding on Home. Split catalogs differ
  only at the end of their names.

**Built without a row, then kept after `Q51` (2026-10-30).** The main agent
read these as part of the approved option *Sfoglia mai vuota*, since empty
folders were the owner's complaint, but the option as worded to the owner did
not name them. After the owner's answer to `Q51` they are kept as
`MDI-F132`–`F134`, on the main session's reading (see D34). As first built:

- **"Choose another drive."** A link beside the open catalog's name brings
  the card list back while a catalog is open. `MDI-F129` shows the list only
  when no catalog is open.
- **A folder that is its own catalog.** In the column, tree and list views,
  opening a folder whose path is the source path of another catalog opens
  that catalog instead, and selects it in *I miei dischi*. After a split, the
  parent catalog still lists the sub-folders, but their files are in the
  sub-folders' own catalogs, so the folder showed an empty column. Side
  effect: the choice in *I miei dischi* scopes Search (`SpecSelection.md`,
  `MDI-F49`), so browsing into such a folder also changed what Search covers.
  **Changed in `769e12f1`:** the folder now opens its catalog in Browse only,
  and the choice is left unchanged (`MDI-F133`). In `2fdefde7` it gained the
  "Catalog" info tag.
- **"Add a drive"** beside the message of a collection with no catalog. It
  opens *Aggiungi un disco*.

**After `Q51` (commit `2fdefde7`).** The "Catalog" tag (`MDI-F133`) and the
click that selects a file without opening it (`MDI-F135`) were built and
committed. The specifications agent read the QML diff against the two rows. In
the list view, a click on a file selects it, a double click opens it, and a
click on a folder opens the folder. The column and list views show the
"Catalog" tag in the *info* tone and the "AI" tag in the *ai* tone.

### Catalog history (D36) — approved core design, 2026-10-30

The owner's "Approvo tutte e tre" (D36) is the `MDI-C14` approval of these
changes. The design is the main agent's, relayed to the specifications agent.

| Class | Approved design |
| --- | --- |
| `CatalogHistory` (new, `core/cataloghistory.{h,cpp}`) | Records entries (date, action, description, catalogs concerned) and snapshots (`device` rows including virtual copies, `catalog`, `file`, `folder`, `mdi_profile`) in `teca_history.db`. Lists them, for the collection or one device. Restore: a new restore point of the current state, then the result catalogs removed (no disk deletion), the snapshot rows re-inserted with their original IDs, and AI data re-linked with `MdiData::moveToCatalogs`. Open as separate: the snapshot catalog inserted with new IDs and a name suffix, AI data not copied. Pruning to the retention setting. |
| `Collection` | A new recompose method: a new catalog on the common parent path, sub-directories included, a valid update date; file and folder rows, AI data (`moveToCatalogs`) and profile moved; old devices removed; a restore point first. |
| Split | New catalogs get a valid `catalog_date_updated` (`MDI-F143`). |
| `AppManager` (K3) | `historyEntries`, `historyForDevice`, `restoreHistoryEntry`, `openHistoryVersion`, `recomposeProposal`, `recomposeCatalogs`, `historyAvailable`; signals `historyChanged`, `undoAvailable`. Restore-point hooks before update, split, delete, edit and recompose. Hosted without a local folder: history disabled. |

### Recorded 2026-10-30 — D36, D37 and the trash parity fix

- **Defects inside existing rows.** `MDI-F128`: the split left the new
  catalogs' update date empty, so their instant index was rebuilt at every
  launch (main agent's finding). The fix is `MDI-F143`. `MDI-F111`: after a
  page's Close the sidebar kept marking the closed page. The fix is bounded by
  `MDI-F111`.
- **`MDI-F157` (commit `dd7881df`).** The specifications agent read the QML
  diff. Both actions now open a confirmation with K2's strings; *Cancel*
  moves nothing. Two differences from K2 remain. Browse's success message
  shows the original path, where K2 shows the path inside the trash. Failures
  show as passive notifications, where K2 shows a warning dialog.
- **Code ahead of the rows (working tree at the time of recording,
  uncommitted).** `KTheme.qml` already defines `controlLine`, focus tokens and
  `textScale`, with every type size multiplied by it. `textScale` is
  `MDI-F154`, which is held until the owner confirms it (`Q46`).
  `qt_quick/PageHistory.qml` exists and is added to the K3 QML lists,
  including `qt_widgets/CMakeLists.txt` (the accepted practice under `MDI-C1`).
- **Owner's data.** The recomposition of the 22 UITS catalogs (D36, answer 4)
  is to run after `MDI-F142` is built, with its restore point first.

### D36 and D37 — reported 2026-10-30

The main agent reports the step built and checked in the running app on the
owner's collection (commits `fd76dd09`, `06d05ad1`). The specifications agent
read the core diff of `fd76dd09` against `MDI-C46`, and the QML of the scope
chip, the *Undo* notification and the History page.

**Built.**

- **History (`MDI-F136`–`F142`, `MDI-C45`, `MDI-C46`).** Restore points before
  an update, split, recompose, delete or edit; the History page; restore;
  *Undo* in the notification after an action; opening a version as a separate
  catalog; *Ricomponi*. Not built: the Settings control for the limit
  (`MDI-F137`) and the view of one catalog's actions (`MDI-F138`).
- **Owner's data (D36, answer 4).** At 20:48 the 22 UITS catalogs were
  recomposed into one "CATALOGO BASE UITS DISCO RIGIDO" with *Ricomponi in un
  solo catalogo*, after a restore point. It holds 14,545 files. 23,167 AI rows
  (captions, classifications, transcripts) moved to it. The entry offers
  *Ripristina com'era prima*, and *Apri come catalogo a parte* for each part.
- **Split date (`MDI-F143`).** A split gives its catalogs the split catalog's
  update date, or the date of the split when it has none. The owner's 22
  undated catalogs got a date at launch (`Collection::repairUndatedCatalogs`,
  see the findings), and their instant index stopped being rebuilt.
- **Navigation (`MDI-F144`–`F148`).** Back and Forward beside every page title
  (`NavTitle`); `Esc`, `Cmd+[` / `Cmd+]`, `Alt+Left` / `Alt+Right` and the
  mouse's buttons. Browse locations are history entries. Browse has a
  breadcrumb from "All the drives" and *Up*. No Close on the sidebar's pages.
  The scope chip is on Search, Numbers and Safety copies only (`MDI-F147`).
  `Cmd/Ctrl+1…9` and `F6` work.
- **Accessibility (`MDI-F149`–`F156`).** The `controlLine` token; focus rings;
  the menu highlight; the sidebar's bar and its page-tab role; keyboard use of
  the *My drives* cards and the Explore tree; names and roles; `KProgressBar`'s
  role and value; limited motion; the activity panel below the pages; disabled
  controls dimmed. `MDI-F154`: `KTheme.textScale` follows *Text size*.
- **`onAccent` defect (`MDI-C37`, `MDI-F149`).** Fixed by the accessibility
  work: primary-button text was 2.8:1 in light mode.

**Main session's choices on open questions** (built, for the owner to confirm):

- `Q52` (a) when an original ID now belongs to another catalog or device, the
  in-place restore is refused, and opening the version as a separate catalog
  still works. (b) Every version is a full copy of the catalog's rows, about
  1 s per 100,000 rows, on the UI thread, before the action; *update all*
  copies every catalog first. (c) *Undo* restores without the confirmation of
  `MDI-F139`. (d) When a restore point cannot be written, the action goes on
  without one. (e) and (f) were not reported.
- `Q53`: the proposals are the catalogs of a Storage or group, or a split
  catalog with its siblings of the same base name. The suggested name is the
  split's base name, and it can be edited. The profile is the root part's,
  else the first part's that has one.
- `Q54` (a): the split catalog's own update date, else the split's date.
  (b): every undated catalog is dated, at each collection opening.

**Fixed, inside existing rows** (from the main session's read-only bug hunt):

- Browse opened the wrong catalog when two catalogs share a path. It now
  redirects only strictly below the open catalog and ignores ambiguous paths
  (`MDI-F133`).
- The folder list did not reload when the catalog opened had the same path as
  the one shown: a
  joined catalog showed the old root catalog's empty folders, with "0 byte".
  The list and column views now reload on a change of catalog (`MDI-F129`,
  `MDI-F133`).
- A double click on a folder could open a file; now guarded (`MDI-F135`).
- *File details* kept showing a trashed or deleted file. The selection is now
  cleared (`MDI-F87`, `MDI-F157`).
- The catalog-by-folder map is refreshed on every open and on every change of
  the device list. A catalog that has vanished shows the cards (`MDI-F129`,
  `MDI-F133`).
- A tap on a selection card no longer opens its catalog twice (`SEL-F3`,
  `EXP-C21`).
- Home's "Cerca in tutti i dischi" now chooses every drive (`MDI-F112`).
- The sidebar, when it slides over the pages, closes after a choice. The mark
  follows the page after a Close (`MDI-F111`).
- Home no longer recounts in a loop when there is no `mdi_data.db`. It shows
  no duplicate cards for the copies in virtual groups (`MDI-F112`,
  `MDI-C38`).
- A split now moves the AI data before any device change, and is refused
  while the enrichment works on that catalog (`MDI-F126`; see the findings).
- Home's worker thread is joined on quit, with a generation counter
  (`MDI-C38`).
- Explore reloads the AI tags only when the shown catalog's data changed
  (`MDI-F130`).

**Not tested.** History and *Ricomponi* in a Hosted (MySQL) collection; the
K2 build with the core changes (which reach K2: the split date, the split's
new failure path, `moveToCatalogs`). History in Memory mode was not reported.

### Known gaps

- Settings UI for tool paths and models (`MDI-F20`, `MDI-F23`).
- Commercial providers, egress confirmation, cost (`MDI-F23`–`F27`, `F50`).
- `MDI-F57`: deleting a catalog removes neither its profile nor its derived
  data and files.
- `MDI-F56`: collection export does not carry the `mdi_` data.
- `MDI-F59`: no *Clear derived data* action.
- `MDI-F19`: no free-space check.
- `MDI-F18`: no failure *Report* action.
- D31: on Linux a destination under a user-owned fixed mount point is not
  waited for (see the D31 section). A held pending job (`MDI-F12`) is listed
  among the waiting jobs although it runs only after the collection is opened
  again or the user starts it (no new string, owner's choice).
- `MDI-F14`, `MDI-F28`, `MDI-F41`, `MDI-F51` (semantic search is not
  in the background, not stoppable, no progress, at most 30 files), `MDI-F53`.
- Semantic search and search history (`Q8`).
- Hosted mode (`Q11`).
- Context phase 1 (D33): the parts listed as still planned in its section
  (`MDI-F96`, `F98`, `F100`, `F101`, `F104`–`F106`, `MDI-C31`); filtering a search by category
  value is not built (`Q26`, open).
- Failure and skip reasons are core English text, shown untranslated.
- *Open at timecode* only opens the file; there is no seek (D7).
- D35: only `MdiData::moveToCatalogs` was tested (unit level). The split glue
  in `Collection::applySplitResult` and the index build after a split have not
  been run end to end (`MDI-F126`–`F128` charter lines).
- `MDI-F133`: in the parent catalog's list view, a folder tagged "Catalog"
  shows "0 byte" as its size, because its files are counted in its own
  catalog.
- `MDI-F133` and `MDI-F134` are kept on the main session's reading of an
  answer that could also name a single option (`Q51`, D34). `MDI-F132` is
  superseded by `MDI-F145`.
- D36: no Settings control for the number of restore points, fixed at 10
  (`MDI-F137`); no view of one catalog's actions (`MDI-F138`). Every version
  is a full copy of the catalog's rows, about 1 s per 100,000 rows on the UI
  thread before an update, and *update all* copies every catalog first
  (`Q52` (b)). An ID given to another catalog or device since blocks an
  in-place restore; opening the version as a separate catalog still works
  (`Q52` (a)). Hosted mode is untested.
- D37: no scope chip on Search results or Browse (`MDI-F147`, `Q55`). The
  *Text size* range stops at 1.2, short of SC 1.4.4's 200 % (`Q46`).
- "%1 catalog(s), %2 file(s)" is not plural-aware (`Q64`).

### Findings for the fork owner (not authorised by any row)

- **Office and OpenDocument summaries.** The summary also extracts text from
  Office/OpenDocument files with KFileMetaData, and reads plain-text formats
  (`txt`, `md`, `srt`, `vtt`, `csv`, `log`, `rtf`, `html`). Those formats are
  `MDI-F42` (`[Backlog]`, "the tool for Office formats needs its own
  approval"). Keep and promote `MDI-F42`, or remove? Since D32, `textutil`
  also feeds `.doc` text to the summary on macOS: D32 approves the program,
  not summaries of Office formats, so this finding stays open.
- **Skipped analyses and `MDI-F13`.** Only a *completed* datum counts as
  current, so a file whose analysis was *skipped* is processed again by every
  run: "no audio track", "no text", a binary file for the snippet, as well as
  "tool missing". `MDI-F13` says a second run with no file change processes zero
  files. Count a skip that depends only on the file (no audio track, no text,
  binary) as current until the file changes, keeping "tool missing" retried; or
  amend `MDI-F13` to say skipped analyses are retried every run?
- **Summaries of recordings.** Audio and video files get a summary from a
  transcript file found next to the recording (`.txt`, `.pdf`, `.docx`, `.srt`,
  `.vtt`) or from the whisper transcript. No row covers summaries of
  recordings. Add a row, or remove?
- **Captions of videos.** Video files get a caption from the poster frame;
  `MDI-F39` covers images only. Add to `MDI-F39`, or remove?
- **Caption language.** Captions are always asked in the UI language, with no
  option; D12 asks for the source's language by default with a UI-language
  option.
- **`MDI-C1`.** `qt_widgets/CMakeLists.txt` was changed (the two new K3 QML
  files added to its `K3_QML_FILES` list). `MDI-C1` says no file under
  `qt_widgets/` is changed. Accept as a build-list exception, or move the list?
  Commit `e79d6cc6` adds `PageActivity.qml` to the same list. 2026-10-29 (D33):
  the owner approved adding `PageContext.qml` to that list "come le altre"
  ("Sì, come le altre (Consigliato)"). The practice is accepted for the fork's
  QML files; `MDI-C1`'s wording is not amended yet (a build-list exception
  would say so).
- **Transcript progress text.** The runner's transcript progress detail
  (`"%1 / %2 min"`, `MDI-F75`) is raw core English text shown on the Activity
  page: not translatable, not built with `StatusBarMessageBuilder`
  (`MDI-C16`) and not in the approved strings.
- **History window.** The resource history keeps 30 minutes (900 samples at
  2 s) and has gaps while neither the page is shown nor a job runs, under the
  title "Since this session started" (`MDI-F68`). Keep, or cover the whole
  session?
- **Create page in standard mode (increment 3).** *Connected drives*
  (`MDI-F90`) is shown in standard mode too, while `MDI-F1` says the mode switch
  is the only visible addition in standard mode. Amend `MDI-F1`, or show the
  list in wizard mode only?
- **Map link and `MDI-C11`.** *Open on map* (`MDI-F87`) hands the file's GPS
  coordinates to openstreetmap.org through the system browser, on the user's
  click. `MDI-C11` allows no metadata off the machine except to a confirmed
  provider. Add an exception to `MDI-C11`, or remove the link?
- **Mixed duplicate keys.** In `MDI-F85` a file with a quick hash and a copy
  without one (for example on a device whose source was not reachable) get
  different keys (`q:` versus SHA-256), so a pair the SHA-256 search found
  before can be missed. Accept, or fall back to SHA-256 when a group is mixed?
- **Copy rule.** Resolved 2026-10-29: the three strings that gave orders were
  reworded with the owner's approval (`MDI-C17`): "Online – opens with a
  click", "Offline – %1 is needed to open the file", "%1 could not mount this
  drive: its license or its permission in System Settings may need attention.
  Without it, the system reads NTFS drives on its own, and reading is all
  Katalog needs."
- **Map link.** "Open on map" opens openstreetmap.org with the photo's
  coordinates. Owner's decision 2026-10-29: kept as is; the explicit click is
  the consent (`MDI-C11` governs automatic sending, not links the user opens).
- **KArchive and K2.** `KF6::Archive` is linked `PUBLIC` to `katalog-core`, so a
  K2 build links it too when it is found (no K2 code uses it). `MDI-C1` holds
  (no file under `qt_widgets/` changed); a build without it is unaffected.
- **Office text.** `MDI-F81` reads Office/OpenDocument text with KFileMetaData
  for snippets; summaries of the same formats are still the open finding above
  (`MDI-F42`).
- **Reachability checks on the UI thread (D31, `MDI-C29`).** Resolved
  2026-10-29: the owner accepted the UI-thread check of the waiting catalogs'
  drives ("Va bene così"); `MDI-C29` describes it and states the risk of
  network volumes.
- **Pending job when its destination drive is away (D31, `MDI-F12`,
  `MDI-F91`).** Resolved 2026-10-29: the job waits for that drive like the
  source drive and leaves the list only when its catalog no longer exists
  ("Aspetta anche quel disco").
- **Topic clusters in a quick-hash pass (`MDI-F84`).** Resolved 2026-10-29:
  a quick-hash pass called the local model to compute topic clusters, which no
  row authorises; removed, the pass computes quick hashes only (D31 section).
- **Other lasting reasons a pending job could not start (D31, `MDI-F12`).**
  Resolved 2026-10-29: such a job (no enrichment profile, or a destination that
  exists but cannot be written) is no longer removed; it is kept, listed among
  the waiting jobs, and tried again at the next opening or when the user starts
  it. Only a job whose catalog no longer exists leaves the list, as the owner
  chose; no new string.

---

## Open questions {#open-questions}

### Still open (deferred)

| # | Question | Decided when |
| --- | ---------- | -------------- |
| Q3b | When topic clusters are recomputed (the per-catalog scope is decided, D11) (`MDI-F37`). | Start of Phase C |
| Q8 | Semantic search and search history; which other criteria (file type, size, date) combine with semantic mode. | Phase D |
| Q11 | Hosted mode: where derived data is stored (D17 covers File and Memory modes only), and with several machines one shared destination path or one per machine (`MDI-C6`). | Deferred |
| Q14 | Performance target for semantic search (collection size and response time) (`MDI-C9`). | Phase D |
| Q16 | How long the Activity log of finished steps is kept: current session only, or across restarts (`MDI-F67`). Provenance (`MDI-F52`) is kept either way. | Default applied 2026-10-28 (session only, last 500 steps); the fork owner may change it |
| Q17 | Sampling interval of the resource monitor, and whether it runs while the Activity page is closed (`MDI-F70`). | Default applied 2026-10-28 (2 s; only while the page is shown or a job runs); the fork owner may change it |
| Q25 | Collection export (`MDI-F56`) and the context: is the context and knowledge base carried by an export, and can a context be exported and imported as a file to reuse it in another collection? | Before `MDI-F56` is built |
| Q26 | Filtering a search by category value (e.g. *Disciplina* = a value): wanted? Not in context phase 1, which feeds the instant search index and semantic search only (`MDI-F102`). | Owner's choice |
| Q29 | MCP servers started as programs (stdio): approve programs the user names as servers (D4 approves only named tools), and treat every stdio server as remote for D3, since it can reach the internet itself and has no host to classify (`MDI-F26`)? | Before context phase 2 |
| Q30 | MCP calls: restrict Katalog to reading resources and to tools the user has allowed one by one (an MCP tool may write or delete)? | Before context phase 2 |
| Q31 | Remote MCP servers usually sign in with OAuth tokens; under D10 a token is never saved, so the user signs in each session or uses an environment variable. Accept? | Before context phase 2 |
| Q39 | **D34 documentation.** The user feature pages in `docs_src/docs/` (EN, FR, CS) describe today's names, and the FORK block keeps the documentation requirements. Updated in each phase? In which languages, given the IT + EN decision? | Main agent's proposal, not put to the owner: update the user docs at the end of the redesign (R8) |
| Q40 | **D34 spec home.** The redesign is recorded here, the fork's authority, although it reaches beyond media intelligence and amends upstream specs. Keep it here, or move its rows to a separate `SpecRedesign.md`? | Main agent's proposal, not put to the owner: keep it here. Not blocking |
| Q41 | **D34 sidebar at phone width.** The owner approved a sidebar that is always visible, 264 px wide (`Q36`, choice 4). `PGL-O3` (`SpecPageLayout.md`) asks every K3 page to keep its content visible and usable at phone width. Accept a fork exception to `PGL-O3` for the sidebar, or let it collapse on a narrow window? Untested so far. | Owner's choice; not blocking |
| Q42 | **D34 drive checks on the UI thread.** Home checks each catalog's source path on the UI thread to show *Connected* / *Not connected*, every time the overview is refreshed. On an unresponsive network volume this can hold the window, as `MDI-C29` states. The owner accepted that risk for queued catalogs only ("Va bene così", D31). Accept it for Home too, or move the check to the worker thread? | Owner's choice; not blocking |
| Q44 | **D34 wording for later phases** (translator's notes, 2026-10-29). (a) *Backup* → *Copie di sicurezza* / "Safety copies" departs from the translation playbook's "Backup everywhere" (`.claude/agents/translations.md`). (b) *Tags* → "Labels" / *Etichette*, and *Devices* → "drives": the strings inside those pages still say Tags and Devices. (c) Existing Italian strings mix *unità* / *disco*, *IA* / *AI* and *Connesso* / *collegato*. *IA* / *AI* is settled: "AI analysis" became *Analisi AI* on 2026-10-30, a change to the translation only. Which form wins in each case? (d) The redesign's new strings use British spelling ("Catalogue", "Analyse"), while K2/K3 and the translation glossary use "Catalog" and "Analyze". Aligning an existing string needs per-string approval (`MDI-C17`); the blanket instruction of 2026-10-30 approves new strings, not edits to existing ones. | Owner's choice; not blocking |
| Q45 | **D34 search by content** (R3). The phase plan offers three ways to search: by name, by content and by meaning. There is no existing search on file content: the instant index (`MDI-F76`) only narrows a name search and must never change its results (`MDI-F77`, `MDI-C23`). Searching the derived text (snippets, summaries, captions, classifications) would be a new behaviour, so it needs its own row before it is built. Wanted? | Owner's choice. R3 was built without it, offering only by name and by meaning. |
| Q46 | **D34 *Theme* and *Text size* after R8.** Once every page uses the fixed sizes and palette (`MDI-C37`), these two Settings controls change nothing in K3. They stay shared with K2 through the settings file. Remove them from K3's Settings, keep them with no effect, or keep a way to enlarge text? With fixed pixel sizes, K3 no longer follows the system's text size either (`TYP-O1`). **2026-10-30 (D37):** the main session proposes that the design system's sizes follow *Text size* (`MDI-F154`, WCAG SC 1.4.4). That reverses `Q34` for text size and needs the owner's confirmation. Reaching 200 % would also widen `TYP-F9`'s range beyond 1.2. | Owner's choice. R8 kept both controls in Settings. `MDI-F154` held until answered |
| Q47 | **D34 Search action twice** (R3 report). *Cerca nei dischi* offers the search in two places: the toolbar's *Search* action and the primary *Search* button of the search card. Should the page keep both, or only one? | Owner's choice; not blocking |
| Q48 | **Making Teca self-explanatory: three options not chosen** (2026-10-30). The owner chose *Browse never empty* (`MDI-F129`–`F131`) and left three options for later: (a) Home grouped by drive, with one card per Storage holding its folder catalogs and names never cut; (b) a guided "First steps" card; (c) plain-language explanations before every action that changes data, and the removal of the legacy page-header Close buttons. Each is new behaviour or changes existing rows (`MDI-F112`, `MDI-F124`, `KBS-F1`), so each needs its own row before it is built. Names on Home's drive cards already wrap since 2026-10-30 (`CDT-C1`); the grouping of (a) is still open. **UX audit, 2026-10-30:** for (a), Home's "Drives: %1" counts catalogs, so the owner's one split disk reads *Dischi: 23*. For (c), D37 takes up the removal of the Close buttons (`MDI-F146`, `Q55`). | Owner's choice; not blocking |
| Q49 | **Catalog notes on a split** (D35). The old catalog's notes (`MDI-F95`) stay under the removed device and no longer show. Copy them to each new catalog as the profile is copied (`MDI-F127`), or leave them? Copying needs a row and, since it is in core, the owner's approval (`MDI-C14`). | Owner's choice; not blocking |
| Q50 | **Readings of *Sfoglia mai vuota*** (`MDI-F129`). The owner's wording covers no drive being chosen. The row reads three more points into it. (1) Entering Browse opens the catalog chosen in *I miei dischi*. (2) A Storage or Virtual choice shows the catalogs within it. (3) A choice that holds no catalog, like nothing or *All*, shows every catalog. These are the specifications agent's readings, built on 2026-10-30. Confirm them, or show every catalog in all cases? | Readings applied until the owner decides; not blocking |
| Q52 | **History details (D36).** (a) A restore re-inserts rows with their original IDs (`MDI-F139`). If an ID has been given to another device or catalog since, should the restore stop before changing anything, or use new IDs? (b) A restore point copies a catalog's whole file list. For a large catalog (1.5M files, before every update), how much time and space is acceptable, and must the copy run off the UI thread? (c) Does *Annulla* skip the confirmation of `MDI-F139` (`MDI-F140`)? (d) When a restore point cannot be written (disk full, read-only folder), does the action stop or go on (`MDI-F136`)? (e) A version opened as a separate catalog has the source path of the current one. Updating it rescans the drive and replaces the version's content, after a restore point, and `MDI-F133` may tag the folder (`MDI-F141`). Accept? (f) When is a restore point that covers several catalogs (a split, a recomposition) pruned (`MDI-F137`)? | Before `MDI-F136`–`F141` are built |
| Q53 | **What *Ricomponi* puts together (D36).** Only the catalogs of one split by sub-folder (under one parent path)? Also those of a split by file type (same path)? Or any catalogs the user picks? Which name does the new catalog get (the name before the split, e.g. "CATALOGO BASE UITS DISCO RIGIDO")? Which profile, when the old catalogs' profiles differ (`MDI-F142`)? | Before `MDI-F142` is built |
| Q54 | **Split date fix (D36).** (a) Which date do the new catalogs get: the update date of the catalog split (its files are as of that update), or the date of the split (`MDI-F143`)? (b) Catalogs already split with an empty date, other than the owner's 22: repaired once, or left until updated? (c) `mdi_data.db` grew by 80 MB in a day while the index was rebuilt at each launch. Does a rebuild leave old rows behind? If so, that is a defect of `MDI-F76` of its own. | Main agent's design for (a); (c) to check |
| Q55 | **Navigation readings (D37).** The specifications agent's readings in `MDI-F144`–`F148`: the history starts empty at launch and when a collection is opened, and is not stored; a place that no longer exists is skipped; a new place after Back drops the places ahead; *Archivio* in the breadcrumb, and *Up* at a catalog's root, show the catalogs to open; *Cambia* opens *I miei dischi*; `Cmd/Ctrl+1…9` cover the first nine sidebar entries; the keys do nothing while a popup is open. Open: on the results page, does the chip show the current choice or the one the search ran against (`SEL-F2`)? The removal of the page-header Close (`MDI-F146`) was option (c) of `Q48`, left for later: confirm it. | Readings applied until the owner decides |
| Q56 | **Per-card *Reinterpreta*** (UX audit, 2026-10-30). Re-interpretation (`MDI-F103`) starts on *Conoscenza dell'archivio*, where the catalog is chosen again. Offer it on each drive card, with that card's catalog already chosen? New behaviour: needs a row. | Owner's choice |
| Q57 | **Split dialog and result** (UX audit). The split confirmation says "This operation cannot be undone." (`SpecDevicesSplit.md`), which `MDI-F139` makes untrue where history is available. After a split, nothing says what was created. Reword the dialog (an existing string: per-string approval, `MDI-C17`) and add a result card (new behaviour: needs a row)? | Owner's choice |
| Q58 | **One vocabulary** (UX audit). The interface mixes *disco*, *dispositivo*, *catalogo*, *archiviazione* and *unità* for related things. The default tree nodes ("Physical Group", "Local disk", "Virtual device") show in English. Choose one term per concept (`Q44` (c) is part of this), and say whether the default nodes get translated names. Existing strings change only with per-string approval. | Owner's choice |
| Q59 | **One progress display** (UX audit). Progress shows in the bottom panel, the sidebar's job card, *Lavori in corso* and on some pages. Keep one place? Any change goes through `SpecProgressReport.md` and the builder (`MDI-C16`). | Owner's choice |
| Q60 | **Actions only on right-click** (UX audit). Some actions exist only in context menus, which keyboard and touch users may not find. Add a "⋯" button that opens the same menu on cards and rows? Needs a row. | Owner's choice |
| Q61 | **"Tutti attivi"** (UX audit). The label "All active" / *Tutti attivi* does not say what it does. Reword? An existing string: per-string approval. | Owner's choice |
| Q62 | **"Dati inviati" tag** (UX audit). The main session found that the *Data sent* figure does not state accurately what it counts (`MDI-F63`). Correct the figure, or reword it? | Owner's choice |
| Q63 | **Delete dialog wording** (UX audit). The device deletion confirmation reads "dispositivo Catalog": an Italian word with the English device type. Reword with a translated type? Existing string: per-string approval. | Owner's choice |
| Q64 | **Plural forms** (UX audit). "%1 passages" and "%1 terms" are not plural-aware ("1 passages"). Use Qt's `%n` plural forms? Each is a new string in place of the old one (`MDI-C39`). | Owner's choice |

### Decided 2026-10-29/30 — D34 (kept for traceability)

Answers relayed by the main agent (see D34).

| # | Question | Decision |
| --- | ---------- | ---------- |
| Q32 | D34 scope: K3 only, or K2 too. | K3 only; K2 unchanged ("Solo Katalog 3 (Consigliato)") — `MDI-C38`, `MDI-C1`. |
| Q33 | The other languages show English for the new strings; amend `CLAUDE.md`. | Accepted; `CLAUDE.md` amended by the main agent ("Sì, e aggiorna CLAUDE.md (Consigliato)") — `MDI-C39`. |
| Q34 | Text sizes of the design system. | Fixed, as in the prototype, independent of the *Text size* setting ("Fisse come nel prototipo") — `MDI-C37`, `MDI-F115`. |
| Q35 | Colours and the *Theme* setting. | A fixed Katalog palette, light and dark by the system's scheme, independent of the *Theme* setting ("Fisse come nel prototipo") — `MDI-C37`, `MDI-F114`, `MDI-F115`. |
| Q36 | Navigation not in the prototype's sidebar. | The choices made in code: collection menu on the name at the top; *Aggiungi un disco* in *Archivio*; *Informazioni* and *Gestisci i dischi* in *Altro*; the drives column and its toggle stay; the sidebar always visible, no pinned / floating toggle ("Approvo tutto (Consigliato)") — `MDI-F111`, `MDI-F113`. |
| Q37 | Home's content and the start page. | Key numbers, drive cards, work in progress and knowledge; Katalog always opens on Home ("Approvo tutto (Consigliato)") — `MDI-F112`. |
| Q38 | The bottom activity panel. | It stays, titled *Lavori in corso* ("Approvo tutto (Consigliato)") — `MDI-F113`. |
| Q18 | Rows for the Create page cards and the Explore tiles (D20). | `MDI-F120` (the cards keep the mode switch) and `MDI-F116`, written 2026-10-30 under the owner's blanket authorisation (D34). |
| Q43 | `KButton.qml`, a component file not listed. | Covered by the owner's blanket instruction of 2026-10-30 (D34), on the main agent's reading — `MDI-C37`. |
| Q51 | Browse behaviours built without a row: "Choose another drive", a folder that is its own catalog, "Add a drive" in an empty collection. | "la più semplice da capire. Metti infotag comunque. …" (2026-10-30). The main session's reading, open for the owner to correct: all three are kept, with a "Catalog" info tag on such folders — `MDI-F132`–`F134`. The answer's last sentence gave `MDI-F135`. |

### Decided 2026-10-28 (kept for traceability)

| # | Question | Decision |
| --- | ---------- | ---------- |
| Q1 | API key storage. | D10 — never in plain text in any `.ini`; session only or environment variable (options b + c). Phase B onward. |
| Q2 | Automatic resume after restart for local-only jobs. | D6 — local-only jobs resume automatically; others wait for Resume. |
| Q3 | Scope of topic clustering. | D11 — per catalog. (Recompute timing split out as `Q3b`.) |
| Q4 | Language of generated text. | D12 — the source's language, with an option for the UI language. |
| Q5 | In-app playback versus the system default application. | D7 — system default application; no Qt Multimedia in Phase A. |
| Q6 | Concurrency mechanism and Memory mode handover (`MDI-C4`, `MDI-C5`). | D17 — own SQLite file `mdi_data.db` in the collection folder (File and Memory modes), WAL and busy timeout, runner connection on its thread, separate main-thread read connection. Hosted mode stays with `Q11`. |
| Q7 | Re-run history. | D13 — keep only the provenance of replaced data. |
| Q9 | Profile editing and enrichment of existing catalogs. | D8 — from the catalog's page and from Explore. |
| Q10 | Cost prices. | D14 — user-entered only. |
| Q12 | What K2 and upstream K3 3.0 do when opening an enriched collection. | D5 — they open it normally and ignore the `mdi_` tables; no schema bump. |
| Q13 | Schema number collision with a future upstream 3.1. | D5 — no collision: the fork never bumps the core schema; `mdi_` tables carry their own version. |
| Q15 | Distribution of external tools and the whisper model. | D9 — user-installed; bundling and licence review deferred. |

### Decided 2026-10-29 — D33 (kept for traceability)

Answers relayed by the main agent (see D33).

| # | Question | Decision |
| --- | ---------- | ---------- |
| Q19 | Where the context editor lives. | A page *Context* in the *Tools* submenu; the six main entries unchanged ("Pagina nel menu Strumenti (Consigliato)") — `MDI-F107`, `MDI-F89`, `MDI-C18`. |
| Q20 | Catalog additions. | Notes only — `MDI-F95`. |
| Q21 | What re-interpretation redoes. | The classification only ("Solo la classificazione (Consigliato)") — `MDI-F103`. |
| Q22 | First classification of an already enriched catalog. | New or changed files only; the others through re-interpretation ("Solo file nuovi; gli altri con Rielabora (Consigliato)") — `MDI-F100`. |
| Q23 | Output language. | The context's language applies to all AI-generated text; without one, D12 applies — `MDI-F98`. |
| Q24 | Re-interpretation interrupted by quitting. | Resumed after the restart, like an enrichment job — `MDI-F103`, `MDI-C36`. |
| Q27 | Archive files and spreadsheets as knowledge-base sources. | PDF, Word, Excel/CSV and text yes; zip archives no — `MDI-F104`. |
| Q28 | Glossary terms as `whisper.cpp`'s initial prompt. | A separate option, off by default — `MDI-F98`. |

---

## Manual test charter

For each row: set up the stated condition, run the operation, confirm the result.

- **MDI-F1** — Fill source and name in standard mode, switch to wizard and back: the values remain; standard-mode creation gives the same catalog as before this feature. Reopen the page: the last mode is restored.
- **MDI-F2** — Removed (D15); replaced by `MDI-F60`.
- **MDI-F60** — Switch to wizard mode: one page shows *Catalog definition*, destination path, file-type toggle buttons, preview switches and *AI analysis*, with no *Next*/*Back*. Open *Analyze* in Explore: the same options appear.
- **MDI-F3** — The file types shown are Image, Video, Audio, Text and Other; each turns on and off on its own. Create the same source in both modes: identical catalog.
- **MDI-F4** — Keyframe count accepts 2–32 (default 4); a partial clip shows start and duration (default 0 and 30).
- **MDI-F6** — Set the destination path inside the source: *Create* shows the inline message at the top of the form and creates nothing; editing the path hides it. Repeat with an empty path, a read-only path, and no file type with a preview on. A destination that does not exist yet under a writable folder is accepted.
- **MDI-F7** — Cancel a wizard creation while it runs: no enrichment job appears and no profile is saved (even when the catalog is kept). Remove a queued wizard creation: no profile. Let one complete: the profile is saved and the enrichment row appears in the activity panel for that catalog.
- **MDI-F8 (A.1)** — Create a catalog in wizard mode, close and reopen the collection (File and Memory modes): its profile is still there.
- **MDI-F9 / F10** — Stop a job mid-run: files already done keep their results; the status is *Stopped*; no orphan `ffmpeg` process remains.
- **MDI-F11** — Pause: no tool process runs and no request is sent until Resume. Pause just before a 64-image boundary while watching file access (`fs_usage` / `inotifywait`): no image is read until Resume. Unplug and replug the drive while paused, then Resume: the job continues.
- **MDI-F8** — On a catalog created in standard mode, open its page and Explore: the profile can be created or edited and a job started from each; the job appears in the enrichment list.
- **MDI-F12** — Quit during a local-only job, reopen: it resumes by itself without redoing finished files. Quit during a job using a commercial provider, reopen: it is *Paused*, nothing is sent until Resume and its confirmation. Pause, quit, reopen: the job is *Paused*; Resume redoes no finished file. Queue three jobs, quit during the first: the same three come back in the same order. Quit with the drive unplugged, reopen: the job waits for the drive (`MDI-F91`). Make a pending job's existing destination read-only, reopen: the job stays listed among the waiting jobs and is tried again at the next opening; delete its catalog, reopen: it leaves the list.
- **MDI-F13** — Run a job twice with no change: the second processes zero files. Modify one file and update the catalog: only that file is reprocessed.
- **MDI-F14** — Delete a source file, update the catalog, run the job: its derived data and files are gone; no other file in the destination path is touched.
- **MDI-F15** — Run a search and an Explore load during a job: both work. Update the enriched catalog: the job pauses, then resumes after the update.
- **MDI-F16** — Add jobs for two catalogs and the first one again: two jobs, run in order. Ask a re-interpretation of a folder of a catalog whose enrichment job is waiting: both are listed; ask the same re-interpretation again: still one; another folder: a third item.
- **MDI-F17** — Run a job and a search together: the activity panel shows both rows; each Stop stops only its own operation.
- **MDI-F18** — Include a corrupt video: the summary counts one failure; *Report* shows the file and reason.
- **MDI-F19** — Choose a nearly full destination: the job does not start and the summary says why.
- **MDI-F20 / F21** — Remove `ffmpeg` from the path: Settings shows it not found; the wizard disables video analyses with the reason; a job skips them and runs the rest without crashing.
- **MDI-F22** — Set a 1 s time limit on a long video: that file fails with a time-limit reason; the next file is processed.
- **MDI-F23 / F24 / F25** — Enable a commercial provider and start a job using it: the confirmation names provider, content kinds, file count and estimated cost, *No* is the default; declining sends nothing (verified with a network monitor).
- **MDI-F26** — Point the Ollama endpoint at a public host: the job asks for the `MDI-F24` confirmation.
- **MDI-F27** — After a remote job: each datum shows a cost; the summary shows the total.
- **MDI-F28** — Stop Ollama during a job: after three failures the job pauses and names the provider.
- **MDI-F29 / F31** — Removed (D23); replaced by `MDI-F78` and `MDI-F79`.
- **MDI-F30, F32, F33** — Check each derived file's format, size limit and range against the row.
- **MDI-F34 / C19** — Inspect the destination path: names are identifiers, not source names; nothing was written on the source.
- **MDI-F35 / F36 / F38** — Transcribe a recording: passages carry timecodes; segments have title, summary, topics, contiguous `h:mm:ss` ranges. Transcribe a video without an audio track (a `.m2v` stream): the transcript is *skipped (no audio track)*, not failed, and no model is called.
- **MDI-F37** — Cluster segments: each has a label, description, members; no segment in two clusters; no cluster holds segments of two catalogs.
- **MDI-F5 (language)** — Caption and summarise a French source with the UI in English: output is French; with the UI-language option: output is English; excerpts stay verbatim.
- **MDI-F39** — Caption an image: caption is at most two sentences; user tags are unchanged; the request carried the thumbnail only.
- **MDI-F40 / F41** — Summarise a PDF: excerpts appear verbatim on their stated pages. A scanned PDF without vision reading is *skipped (no text)*.
- **MDI-F44 / F45** — Disconnect the device: Explore and Search show the derived data. Disconnect the destination drive as well: texts still show, previews show the placeholder, no error dialog.
- **MDI-F46** — *Open at timecode* opens the clip in the default application and shows the timecode.
- **MDI-F47 / F48 / F49** — Ask a question matching a transcript passage: the file ranks with that passage and timecode; devices outside the selection never appear; switching embeddings model reports the uncompared count.
- **MDI-F50** — With a remote embeddings provider: the first semantic search asks for confirmation; declining runs nothing.
- **MDI-F51** — Stop a semantic search on a large collection: it stops; progress used the builder in the activity panel.
- **MDI-F52 / F54** — Inspect a datum: every provenance field is present; changing a prompt version makes the next run reprocess that analysis.
- **MDI-F53** — Re-run failed only: only failed files are processed. Re-run an analysis: the old result is gone, its provenance record remains as history.
- **MDI-F55** — In File and in Memory mode, enrich, close, reopen: derived data is present and shown; the profile is restored (Memory mode: from `mdi_profile.csv`).
- **MDI-F56** — Export a File collection to Memory mode and back: derived data survives; derived files still open.
- **MDI-F57 / F59** — Delete a catalog, and clear derived data on another: confirmation shown; data and recorded files removed; other files in the destination path untouched.
- **MDI-F58** — Set *Include metadata* to *None* on an enriched catalog and update: every derived datum remains.
- **MDI-C1** — `git diff` of the feature touches nothing under `qt_widgets/`. Open an enriched collection (File and Memory modes) with K2 2.13 and upstream K3 3.0: it opens normally, and the `mdi_` tables and files are left in place.
- **MDI-C2** — Create the same source in standard mode before and after the feature: identical catalog and identical progress messages.
- **MDI-C4 / C5** — Search and open *Show analysis* repeatedly during a job, in File and in Memory mode: no "database is locked" error, no stall; `mdi_data.db` (with its WAL files) is the only new derived-data file in the collection folder, and the collection database receives no derived data.
- **MDI-C7** — Name a source file with spaces, quotes and `;`: tools process it; nothing is executed by a shell.
- **MDI-C11** — With default settings, a network monitor shows connections to `localhost` only during a full job.
- **MDI-C13** — Use a commercial provider once with a session key and once with an environment-variable key: search every `.ini`, the database and logs for the key: absent; after restart a session key must be entered again.
- **MDI-C15** — Open a 3.0 collection: the `mdi_` tables and the `mdi_` version record exist, the core schema version is still 3.0, every 3.0 table and column is unchanged; open it again: the `mdi_` migration changes nothing. In File and Memory modes `mdi_data.db` appears in the collection folder; in Memory mode `mdi_profile.csv` too.
- **MDI-F61 / F69** — Open *Activity* from the side menu and from the activity panel's enrichment row: the same page opens. With nothing running it says so.
- **MDI-F62** — During a job: catalog, *file N of M* and the estimated end are shown; Pause and Stop act as in the activity panel.
- **MDI-F63 / F73** — With the local provider only: egress reads "nothing sent". On Windows, values the monitor cannot obtain read unavailable, nothing else fails. Disconnect the source drive: its tile changes.
- **MDI-F64 / F75** — Transcribe a long recording: the card shows transcript, file, `whisper.cpp` and its model, local, and audio minutes done of total, rising. During a caption: tokens per second appear.
- **MDI-F65 / F72** — With a model loaded in Ollama: the tools card lists it with its size. Stop Ollama: it reads unreachable; the other tiles keep updating.
- **MDI-F66** — Add jobs for two catalogs: both appear in run order.
- **MDI-F67 / F74** — Include a corrupt video: its step appears in the log with duration, outcome *failed* and the reason.
- **MDI-F68** — Watch the charts during a job, restart Katalog: the charts start empty.
- **MDI-F70 / F71 / C21** — During a transcription, the child-process list shows the tool with CPU and memory; on macOS and Linux CPU, memory and swap match the system monitor; no linked library added.
- **MDI-F76 / C22** — Open a collection, then update a catalog: its index is rebuilt in the background while a search runs; `mdi_fts`, `mdi_fts_doc`, `mdi_fts_state` are in `mdi_data.db` only; the collection database is unchanged.
- **MDI-F77 / C23** — Run the same text searches with a current index and with the index removed: identical result sets. A regex search, a 2-character term, a `/` outside names mode, a Hosted collection and a stale index all search as before. K2 search on the same collection: unchanged.
- **MDI-F78 / C24** — Enrich a rotated JPEG: a WebP thumbnail, 256 px longest side, correctly oriented; a format Qt cannot read goes through `ffmpeg`; re-running replaces the file only after the new datum is saved.
- **MDI-F79** — Enrich a video with 4 frames: one sheet, 4 frames 144 px high side by side; the datum lists each frame's timecode, position and width.
- **MDI-F80** — Open the technical data of a video and an audio file: codec, resolution, frame rate, duration, bit rate and audio match `ffprobe`.
- **MDI-F81** — A `.docx`, a `.pdf` and a `.md` get an 800-character snippet even with *Text* off in the profile; a binary file renamed `.txt` gets none. On macOS without `catdoc`, a Word 97–2004 `.doc` gets a snippet with tool `textutil`, and the file is unchanged; on Linux, `.doc` still goes through `catdoc`.
- **MDI-F82 / C25** — List a `zip`, a `.tar.zst` and a `7z`: entries shown, total counted beyond 20,000; a RAR is not supported; a macOS pax tar fails with its reason; a build without KArchive skips the analysis with its reason.
- **MDI-F84 / C26** — Create a catalog without a profile: quick hashes are computed for all its files; the catalog's `file` table and checksum option are unchanged; the hash of a sample file matches an independent implementation. On a catalog with topic segments and no clusters, a quick-hash pass asks no model and leaves the clusters to the next enrichment run.
- **MDI-F85** — Duplicates search on checksum: groups by quick hash; with *Verify with full checksum* only true SHA-256 duplicates remain; the option is absent from the saved search history.
- **MDI-F87** — In Explore and Search results, `Space` toggles the Inspector except while typing ahead; restart: its visibility is kept. Disconnect the device: the badge reads offline with the device name; a photo with GPS shows *Open on map*.
- **MDI-F88** — Switch Explore to *Columns*: folders first, thumbnails, arrow keys move between columns; restart: the view is kept.
- **MDI-F89** — Removed (D34); replaced by `MDI-F111`.
- **MDI-F90 / C27** — With an unmounted external drive: it reads *Not mounted*; *Mount read-only* mounts it read-only and fills the source path. With a failing third-party driver: the message names the driver. No raw-device access is attempted.
- **MDI-F91 / C29** — Unplug the source drive during a job: the waiting state shows with the catalog and source path; no tool process runs; no file is counted skipped or failed; replug: the job continues with the same file. Pull the drive during a transcription: no failed datum, the previous preview is still there, the file is redone after replugging. Delete one source file with the drive connected: it is skipped, the job goes on. Stop while waiting: the job ends. Linux, fixed mount point: unmounting gives the waiting state, not a run of skipped files.
- **MDI-F92** — A (drive unplugged) queued before B (drive connected): A steps aside, B runs, then A waits. Only A queued: A waits, nothing cycles.
- **MDI-F93** — Open another collection during a job: the job ends; nothing of the left collection shows; reopen the first collection: the job and its waiting list are back.
- **MDI-C28** — `mdi_job` holds the list; the collection database and the `.ini` files are unchanged; Stop empties it, quitting does not; K2 and upstream K3 open the collection normally.
- **MDI-F94 / F95** — Write a context with a glossary, a closed category, an open category and two entities; add notes to one catalog; close and reopen (File and Memory modes): all is there; a file of the other catalog is classified without those notes.
- **MDI-F96** — Change one glossary term and save: the version changes; save without a change: it does not. A datum made afterwards records the new version.
- **MDI-F97** — Draft from a catalog: a proposal opens; discard it: the context is unchanged; nothing was saved before *Save*.
- **MDI-F98** — With a context, caption a new image, summarise a new PDF and transcribe a new recording: each datum records the context version; the `whisper.cpp` invocation has no initial prompt; turn on the transcription option and transcribe another recording: the invocation carries the glossary's preferred forms. Set the context's language to Italian with the UI in English: captions, summaries, segments and classifications are in Italian; excerpts stay verbatim. Files enriched before the context was saved are not redone.
- **MDI-F99** — Enrich a catalog, change and save the context, run *Analyze* again: zero files processed. Upgrade to the build with context support on an enriched catalog: zero files processed.
- **MDI-F100** — Classify a catalog with a context: each file has a title, description, category values, tags and entities; no vision call and no `whisper.cpp` process appears in the Activity log for the classification steps; with no context the analysis is disabled with its reason. Turn classification on for an enriched catalog and run *Analyze*: no existing file is classified; add a file and update the catalog: only that file is classified; modify a file: it is classified.
- **MDI-F101** — A closed category receives a value outside its list: it is dropped and the datum records it; a glossary alias is stored as its preferred form; a single-value category holds one value; the `Tags` page is unchanged.
- **MDI-F102** — Search the instant index for a category value and ask a semantic question matching a classification description: the file is found, with kind *classification*; the Inspector shows the classification and its sources.
- **MDI-F103** — Re-interpret one folder: the confirmation shows the file count and an estimate; cancel: nothing runs; confirm: only that folder's files are redone; repeat: zero files; the old classification's provenance remains; no caption, summary or transcript step runs. Quit during a re-interpretation and reopen: it continues with the same folder.
- **MDI-F104 / F105** — Add a folder of PDF, `docx`, `xlsx` and `md` documents: each is indexed with its passage count; a `zip` in the folder is left out, and adding a `zip` alone is refused; the documents' bytes and dates are unchanged; disconnect their drive: classification still uses their passages; Stop during indexing keeps the documents done; index again: unchanged documents are skipped.
- **MDI-F106** — Change the embeddings model: the passages show as needing indexing again and are not used; empty knowledge base: classification runs with the context alone.
- **MDI-F107** — Open *Tools* → *Context*: edit the context, a catalog's notes and the knowledge base; start a draft and a re-interpretation. The side menu's main entries are unchanged.
- **MDI-F108** — With the default local provider, a network monitor shows connections to `localhost` only during classification and indexing.
- **MDI-C30 / C34** — `mdi_context`, `mdi_kb_document`, `mdi_kb_passage` are in `mdi_data.db` only; the collection database, the `.ini` files and the Memory-mode files are unchanged; K2 and upstream K3 open the collection normally. In Memory mode a file's classification receives its extended metadata as in File mode.
- **MDI-C31** — *Clear derived data* and a catalog deletion: the collection's context and knowledge base remain; the deleted catalog's notes are gone.
- **MDI-C33** — Index a large knowledge base while searching and opening *Show analysis*: no "database is locked" error, no stall; no new linked library.
- **MDI-C36** — Queue a re-interpretation of one folder, quit, reopen: `mdi_job` holds its kind and scope and it resumes with them; open a collection whose `mdi_job` predates D33: its jobs come back as enrichment jobs.
- **MDI-F111** — The sidebar is always visible, with no pinned / floating toggle. It shows the collection's name, then *Home*, the groups *Archivio* (four entries), *Intelligenza* (two) and *Altro* (five, with *Gestisci i dischi* and *Informazioni*) in the stated order, and *Impostazioni* and the job card at its foot. Home and the *Archivio* and *Intelligenza* entries show their description. The open page's entry is marked. Click the collection's name: *Open...*, *New...* and the recent collections behave as before (`OPN-F7`, `OPN-F9`). There is no Quit entry, and the Quit shortcut quits. Start a job: the card shows it, and activating it opens *Lavori in corso*. Hide and show the *I miei dischi* column with its toggle.
- **MDI-F112** — Launch Katalog after leaving it on another page: it opens on *Home*. Open another collection: *Home* shows. *Home* shows the key numbers, a card per catalogued drive with its tags, the work in progress and the knowledge. *Browse* on a drive card selects that drive and opens *Sfoglia l'archivio*, as the Selection card's *Explore* does. Every other card and shortcut opens an existing page or acts as it does there. Repeat in File and Memory modes: the same numbers. In Hosted mode the file-type counts are absent.
- **MDI-F113** — With the interface in Italian and in English: the sidebar entries and page titles show the table's names; the Selection page reads *I miei dischi*, the Devices page *Gestisci i dischi*, the bottom panel *Lavori in corso*. In French they show the English source of the new strings. Search results, Device edit and About keep their titles; labels inside pages not yet rebuilt are unchanged.
- **MDI-F114** — Show a tag in each of the six tones: each carries its text, and none ends in "…".
- **MDI-F115** — Switch the system between light and dark while Katalog runs: the sidebar, *Home* and its cards and tags follow without restart. Change *Text size* and *Theme* in Settings: the sidebar and *Home* do not change, while a page not yet rebuilt follows both.
- **MDI-C37** — The redesign's new files are exactly those of `MDI-C37`. Search the components, the sidebar and *Home* for colour, radius, spacing and text-size literals: none outside `KTheme.qml`. On Windows and Linux the monospace text uses Consolas and the generic monospace family.
- **MDI-C38** — `git diff` touches nothing under `core/` and no code under `qt_widgets/` (the K3 QML file list aside). `qt_quick/` holds no new `QSqlQuery`, and no new module is added to `CMakeLists.txt`. In Memory mode, with a large catalog, open *Home* while watching the UI thread: no file list is read on it, the counts appear when the worker finishes, and the window stays responsive. A blocking error on a rebuilt form still shows as an inline message at the top of the form.
- **MDI-C39** — The Italian `.ts` file carries the Italian of the 45 approved strings; every other `.ts` file lists them untranslated. `git diff` of the `.ts` files changes no existing source string. The navigation labels no longer used are marked vanished, not deleted.
- **MDI-F116** — In *Sfoglia l'archivio*: folders, files, the column view, *Dettagli del file* and the analysis dialog show the same data as before the phase. A classified file shows its category values, AI tags and entities as *ai* tags. "File details" toggles the Inspector, and `Space` still does. "Analyse with AI" opens the same dialog as *Analyze* did. Every `SpecExplore.md` charter line passes unchanged.
- **MDI-F117** — Run the same name search and the same meaning search before and after the phase: identical results. Every criterion and option is present, in its card. History restore, column widths, filters, export and `Ctrl+E` behave as before. There is no *by content* search unless `Q45` adds one.
- **MDI-F118** — Edit and save the context, a catalog's notes and the knowledge base; propose a draft and discard it; start a re-interpretation. Each behaves as in the `MDI-F94`–`F108` charter lines.
- **MDI-F119** — During a job, the hero card, the tiles, the right-now card, the tools card, the queue, the log (outcomes as tags) and the charts show what the `MDI-F61`–`F69` charter lines expect. The bottom panel's Stop and Clear act as before.
- **MDI-F120** — Both modes are on one page, with no *Next*, and the mode switch keeps its values. The `MDI-F6` refusals show the inline message. "Catalogue the drive" creates the same catalog as *Create* did. Connected drives show as cards, and *Mount read-only* works as in `MDI-F90`.
- **MDI-F121** — The Selection cards say what they said before; the reminder, its scroll and the context menu act as before. The Devices views, the edit form and the deletion confirmation are unchanged. The bigger-icon setting still enlarges the card icons.
- **MDI-F122** — A backup run, its preview and its confirmations behave as before, and nothing on a target is deleted. Statistics show the same numbers. Every setting saves under its existing key. The metadata dialog shows the same fields.
- **MDI-C40** — For each phase: `git diff` touches only the phase's QML files, `Main.qml`, the K3 QML lists and the translation files; nothing under `core/`; no new file; no `QSqlQuery` in `qt_quick/`. The charter lines of the page's own spec pass unchanged.
- **MDI-F123** — On macOS the build is `Teca.app`. The window title, About and every dialog title read *Teca*, and About shows the credit line. The top of the sidebar shows *Teca*. In French the first-run welcome says *Teca*, and in Italian the alpha-warning welcome is translated. The theme "Katalog Colors" and the export formats "Katalog / File" and "Katalog / Memory" keep their names.
- **MDI-C41** — An existing `katalog3_prerelease_settings.ini` and an existing collection open with their settings, and no new settings file appears. `git diff` shows no change to settings keys, the QML module URI or the `KAboutData` component name.
- **MDI-C42** — On macOS, Windows and Linux (Plasma included), K3 starts with `TecaStyle`. Fields and buttons are rounded, check boxes, radio buttons and switches use the accent colour, and menus are rounded. A control outside the module looks as it does under Fusion. Switch between light and dark: the controls follow. `qt_quick/TecaStyle/` holds no colour literal, and `CMakeLists.txt` adds no Qt or KF6 module.
- **MDI-F124** — Open a `Controls.Dialog` (a deletion confirmation) and a `Kirigami.PromptDialog`, in light and in dark. Both sit on the surface colour with rounded corners. The page header has the ground colour and no separator line. *Home* shows no Close action, and `Esc` behaves as before.
- **MDI-F125** — On *I miei dischi*, the description line shows above the reminder. The reminder is an accent-soft row with no label. Clicking it scrolls to the selected card and leaves the selection unchanged. *Show device info* is a labelled switch, and turning it on shows the info tags. Collapse and expand are framed and keep their tooltips. A Storage card shows "Used Space: " before its used-space figure.
- **MDI-F126 / C43** — Enrich a catalog, then split it by sub-folder. Each file's previews, caption, summary, transcript and classification show for its new catalog in Browse, in text and semantic Search, and on Home. The number of `mdi_datum` rows now on the new catalogs equals the number returned. A listing of the destination path is identical before and after. Rows of files left out of every new catalog are still under the old ID. Run *Analyse with AI* on a new catalog: no completed analysis of an unchanged file is redone. Repeat with a split by file type, and in Memory mode. In a Hosted collection the split behaves as before.
- **MDI-F127** — Split a catalog that has a profile: *Analyse with AI* on each new catalog opens with the same destination path and settings. In Memory mode, close and reopen the collection: the profiles are still there. Split a catalog without a profile: the new catalogs have none.
- **MDI-F128** — After a split, each new catalog's instant index is built in the background. A text search on a new catalog gives the same files with and without the index (`MDI-F77`).
- **MDI-F112 (AI counts)** — On a catalog with quick hashes and no caption, summary, transcript or classification, Home's drive card has no "AI" tag. The *Files with AI results* number equals the sum of the files that carry "AI" in Browse.
- **MDI-F129** — Choose a catalog in *I miei dischi*, then enter *Sfoglia l'archivio* from the sidebar, and again from Home's *Browse*: that catalog opens each time. Open *Sfoglia l'archivio* with nothing chosen in *I miei dischi*: a card per catalog with name, path, file count and AI count, and no "No folder selected". Choose an empty Storage: every catalog shows. In a collection with no catalog, the page says so. *Open* on a card selects that catalog in *I miei dischi* and shows its folders, as Home's *Browse* does. Before opening any catalog, choose a Storage with two catalogs: its two catalogs show as cards. With a catalog open, choose a Storage: the catalog stays open. File and Memory modes show the same figures as Home; Hosted mode hides the figures Home hides.
- **MDI-F130** — In the list view and in the column view, a captioned image, a summarised PDF, a transcribed video and a classified file each carry "AI" in the *ai* tone. A file with only a thumbnail, and a file whose analyses all failed, carry none.
- **MDI-F131** — The description line shows at the top of *Sfoglia l'archivio* in English and in Italian; in French it shows the English source.
- **MDI-F132** — With a catalog open in *Sfoglia l'archivio*, press "Choose another drive": the card list shows; *Open* on another card opens that catalog.
- **MDI-F133** — Split a catalog by sub-folder and open the parent (root) catalog. In the column and list views each sub-folder that is now a catalog carries "Catalog" in the *info* tone. Open one in the column, tree and list views: its own catalog opens, with its files. The choice in *I miei dischi* is unchanged, and a search covers what it covered before. In the list view the folder's size reads "0 byte" (known gap).
- **MDI-F134** — In a collection with no catalog, Browse shows the message and "Add a drive"; pressing it opens *Aggiungi un disco*.
- **MDI-F135** — In Browse's list view, click a photo once: it is selected, *Dettagli del file* shows its thumbnail, and no other application opens. Double-click it, and select it and press Enter: it opens with the system's default application each time. Click a folder once: it opens. In the column view a click selects and Enter opens. In Search results a click behaves as before.
- **MDI-C44** — `git diff` of the step touches nothing under `core/` and adds no file; `qt_quick/` holds no new `QSqlQuery`. In Memory mode, with a large collection and nothing chosen, open Browse: the window stays responsive while the cards' counts appear. The new strings are listed under *User-visible text* with the owner's approval.
- **MDI-F136 / C45** — Update, split, edit and delete a catalog, and recompose split catalogs: each time *Storico* gains an entry with the date and a description, and `teca_history.db` appears in the collection folder. The collection database's schema and the `.ini` files are unchanged. K2 and upstream K3 open the collection normally. Repeat in Memory mode: a restore point holds the same file and folder counts as in File mode. In a Hosted collection without a local folder, *Storico* says history is not available.
- **MDI-F137** — Update one catalog eleven times: *Storico* keeps its last 10 entries. Set the limit to 3 in Settings: the oldest entries beyond it go. An existing settings file still opens.
- **MDI-F138** — Open *Storico* from the sidebar: every entry shows its date and description and both actions. Narrow it to one catalog: only that catalog's entries show.
- **MDI-F139** — Enrich a catalog, split it, then restore the split's entry. The confirmation names the catalogs replaced; *Cancel* changes nothing. Confirm: the original catalog is back with its original ID, files and folders, and its previews and descriptions show in Browse. The split catalogs are gone from the collection, and nothing changed on the drive or in the destination path. *Storico* has a new entry for the restore, and restoring it brings the split back.
- **MDI-F140** — Right after an update, *Annulla* is offered; using it restores the catalog as before the update.
- **MDI-F141** — Open an earlier version as a separate catalog: a new catalog with the suffixed name and the old file list appears; the current catalog and its AI data are unchanged; the new catalog has no "AI" tags.
- **MDI-F142** — Split an enriched catalog by sub-folder, then *Ricomponi*: the proposal names the catalogs, the name and the common parent path; *Cancel* changes nothing. Confirm: one catalog with every file and folder, sub-directories included and an update date; its AI data and profile are there; the old catalogs are gone; a restore point was recorded first, and restoring it brings the split catalogs back. Nothing changed on the drive or in the destination path.
- **MDI-F143** — Split a catalog: every new catalog has an update date. Relaunch twice: their instant index is not rebuilt, and `mdi_data.db` does not grow.
- **MDI-F144** — Go Home → *I miei dischi* → *Sfoglia l'archivio*, open two folders, then press Back four times: each place comes back in reverse order, then Forward replays them. Repeat with `Esc`, `Alt+Left`/`Alt+Right`, and on macOS `Cmd+[`/`Cmd+]`. At the start, Back is disabled and `Esc` does nothing. With a dialog open, `Esc` closes only the dialog. During a running operation, `Esc` stops nothing. After each step the sidebar marks the page shown.
- **MDI-F145** — In Browse, three folders deep: the breadcrumb shows `Archivio › catalog › a › b › c`. Activate `a` with the mouse, and `Archivio` with the keyboard: each opens as stated. *Up* opens the parent folder, and at the root shows the catalogs to open.
- **MDI-F146** — Every page reached from the sidebar has no Close action; Search results and Device edit keep theirs. On Device Edit, `Esc` discards unsaved input as Cancel does.
- **MDI-F147** — Choose a drive in *I miei dischi*: Search, its results, Browse, *Numeri dell'archivio* and *Copie di sicurezza* name it in their chip. *Cambia* opens *I miei dischi*. What each page covers is unchanged.
- **MDI-F148** — `Cmd+1` … `Cmd+9` (`Ctrl` elsewhere) open the first nine sidebar entries in order. `F6` moves focus between the sidebar and the page. With a menu open, neither acts.
- **MDI-F149** — Measure, in light and in dark, the edge of a field, a check box, a switch and a button, the focus ring and a menu's highlighted item against their background: each is at least 3:1.
- **MDI-F150** — Move through a page with `Tab`: every focused control shows a ring, checked controls and the drive cards included.
- **MDI-F151** — In a grey-scale screenshot, the sidebar's current entry and a checked switch are still distinguishable. A screen reader announces the current entry as selected. A disabled `KButton` is dimmed.
- **MDI-F152** — With the keyboard only, reach the drive cards, move between them and choose one: the result is that of a click.
- **MDI-F153** — With VoiceOver (macOS) or Orca (Linux): icon-only buttons and rows are announced by name, a progress bar with its value, and the Inspector image with a text alternative.
- **MDI-F154** — Held (`Q46`). If confirmed: set *Text size* to its maximum; the sidebar, *Home* and redesigned pages grow, and no text is cut.
- **MDI-F155** — Open pages, cards and panels: no animation is needed to see a state or use a control.
- **MDI-F156** — With a job running, scroll every page to its end and `Tab` through it: no content and no focused control is behind *Lavori in corso*.
- **MDI-F157** — In Browse, *Move file to Trash* on a file: the "Confirmation" dialog shows K2's question with the path; *Cancel* leaves the file in place; confirm: it is in the system trash, and the result shows. Repeat with a folder. In Search results, *Move to Trash*: K2's question with the path; *Cancel* moves nothing.
- **MDI-C46** — `git diff` of the step: new core files are only `core/cataloghistory.{h,cpp}`; `MdiData` has no new method; `CatalogJobStoppable`, `CatalogManager` and `DeviceUpdateManager` are unchanged; `qt_quick/` has no new `QSqlQuery`; the only new K3 file is `PageHistory.qml`.
- **MDI-C47** — `git diff` of the step touches nothing under `core/`, adds no file and no module. `KTheme.qml` defines `controlLine` with the stated colours, and TecaStyle reads it as `palette.dark`. No settings key holds the navigation history.

---

## Specs to amend {#specs-to-amend}

Listed here, not yet applied. Each amendment needs the user's approval before the
target spec is edited.

| Spec | What must change |
| ------ | ------------------ |
| `SpecCreate.md` | **Missing (SPEC GAP).** Bootstrap a spec for the existing K3 Create page (standard mode) so the wizard mode is bounded against it: its fields, validation (`SpecValidationRules.md` C1–C4), creation path and queue behaviour. Then add a row for the mode switch referencing `MDI-F1`–`F8`, and a constraint that standard mode is unchanged (`MDI-C2`). |
| `SpecCatalogMetadata.md` | Has no requirement IDs. Add to Scenario 2 (especially 2.8 "Any level → None") a statement that only file metadata columns are cleared and enrichment data is untouched (`MDI-F58`); add the same to "Main user needs". Converting this page to the three-table format is a separate task. |
| `SpecOperationQueue.md` | `OPQ-C16`: extend its reach to the enrichment row in the activity panel (`MDI-F17`), as was done for the Explore load, and update the *Note on the reach of OPQ-C16*. Add a constraint (next free `OPQ-C`) parallel to `OPQ-C18`: an enrichment job is not a queue entry and is not covered by `OPQ-C3`/`OPQ-C4`/`OPQ-C15` (`MDI-C3`). `OPQ-F5`/`OPQ-F7`: state that the enrichment row and its Stop are separate from the queue's running entry. Note on `OPQ-F9`: an update that pauses an enrichment job (`MDI-F15`) changes nothing in queue progression. |
| `SpecVersions.md` | No core schema change: the schema stays 3.0. Add a note that this fork's `mdi_` tables (and `mdi_` Memory-mode files) exist outside the core schema numbering, carry their own "media intelligence schema" version and their own idempotent migration, and are ignored by K2 and upstream K3 3.0 (`MDI-C15`, D5). |
| `SpecExplore.md` | Scope: add the derived-data marker and details view (`MDI-F44`–`F46`) as new `EXP-F` rows (next is `EXP-F35`). `EXP-C13` (21-column layout) must state whether the marker is a new column or a decoration. `EXP-C3`/`EXP-C8` ("no schema change") are scoped to their own rows; add a line saying so. |
| `SpecSearchList.md` | `SRL-C1` is respected: semantic mode passes its single query through `Search::searchText` unchanged. Add a constraint (next `SRL-C12`) that semantic mode shows one query field and no term rows, and that list-as-input does not apply to it. |
| `SpecSearchResults.md` | Scope: add semantic results (`MDI-F47`–`F49`) — columns for passage, kind, rank, timecode, grouping by file — as new `SRS-F` rows (next `SRS-F12`). Amend `SRS-C1` (no new string) for those rows. State whether *Export to CSV* (`SRS-F9`/`F10`) includes semantic results. |
| `SpecSelection.md` | Confirm that device selection scopes semantic search (`MDI-F49`). |
| `SpecCollection.md` | "Data coverage" and "ID remapping": state whether derived tables are imported/updated across collections and how their IDs are remapped. |
| `SpecCollectionExport.md` | `CEX-O1` content list: add derived data. `CEX-F6`/`CEX-F7`: export derived tables (and their Memory-mode files). `CEX-C1` ("only core change allowed") must be amended for the derived-table export. Add that derived files are not copied (`MDI-F56`). |
| `SpecSettingsPage.md` | `SET-F2`: add an "External tools" section (`MDI-F20`) and a providers section (`MDI-F23`) to the Application tab. `SET-F3`: add the per-collection provider enablement to the Collection tab. `SET-C1` ("no change to core") scoped to the tab split. `SET-C2`: the new section titles need string approval. |
| `SpecProgressReport.md` | Component table: a new operation name for enrichment, process titles per analysis, use of Time to Completion (part 10, today metadata extraction only) and Current Item for enrichment and semantic search; a new block in the Message Sources table; a status value for the waiting state of `MDI-F91` (an extended value, like *Loading* / *Verifying*), with the path waited for as Current Item (applied 2026-10-29). Each new string needs approval. |
| `SpecValidationRules.md` | Create page: add wizard rules after C4 for the `MDI-F6` refusals. Dialogs: add the egress confirmation (`MDI-F24`), the *Clear derived data* confirmation (`MDI-F59`) and the extended catalog-deletion text (`MDI-F57`) to the dialogs the button-order rules cover. |
| `SpecHostedSecurity.md` | `HST-C1` (classification only by `Database::validateHostname()`): state that provider endpoints reuse it (`MDI-F26`). |
| `SpecDevicesPage.md` | Catalog deletion text (`MDI-F57`); the location of *Clear derived data*, profile editing and starting enrichment on the catalog's page (`MDI-F8`, D8). |
| `SpecExplore.md` (D8) | In addition to the row above: profile editing and starting enrichment from Explore (`MDI-F8`). |
| `SpecK3Deployment.md` | State that this fork's builds do not bundle external tools or models; they are user-installed and detected (`MDI-F20`, D9). Bundling and licence review deferred. |
| `SpecOperationQueue.md` (D19) | `OPQ-C16` note: the enrichment row of the activity panel opens the Activity page (`MDI-F69`); the panel itself is unchanged otherwise. |
| `SpecProgressReport.md` (D33) | Operation name and process titles for knowledge-base indexing (`MDI-F105`) and for re-interpretation (`MDI-F103`), and the classification's process title. Each new string needs approval (D16). |
| `SpecOperationQueue.md` (D33) | Knowledge-base indexing (`MDI-F105`) is, like the runner, not a queue entry (`MDI-C3`); state it next to the `MDI-C3` constraint listed above. |
| `SpecTheme.md` (D34) | Note the owner's exception of `MDI-C37` (`Q35`). The design-system components, the sidebar and the pages rebuilt in D34 use a fixed Katalog palette, light or dark by the system's scheme, and do not follow the *Theme* setting. For them this departs from `THM-C1`, `THM-C2`, `THM-C3`, `THM-C21` and `THM-C22`. The drawer logo band (`THM-F20`) no longer exists. Pages not yet rebuilt stay bound. |
| `SpecTypography.md` (D34) | Note the owner's exception of `MDI-C37` (`Q34`). The same components and pages use fixed text sizes and do not follow the *Text size* setting. For them this departs from `TYP-C1`, `TYP-C3`, `TYP-F2`, `TYP-F3`, `TYP-F4`, `TYP-F7` and `TYP-F11`. Pages not yet rebuilt stay bound. |
| `SpecCollectionIdentity.md` (D34) | `CID-F1`–`CID-F5` speak of the "drawer header": the collection's name is now at the top of the sidebar (`MDI-F111`). |
| `SpecCollectionOpen.md` (D34) | `OPN-O3`, `OPN-F7`, `OPN-F9` speak of the drawer's *Open* menu. It now opens from the collection's name at the top of the sidebar, with the same content (`MDI-F111`, `Q36`). |
| `SpecSettingsPage.md` (D34) | `SET-F4`: the *Settings* entry is now the sidebar's *Impostazioni*. Katalog no longer restores the last page at launch: it always opens on *Home* (`MDI-F112`, `Q37`). `SET-F5`: the *Hosted Database...* entry is in the collection menu. |
| `SpecSelection.md`, `SpecDevicesPage.md` (D34) | Phase R1: the Selection page is titled *I miei dischi* and keeps its column and toggle; the Devices page is titled *Gestisci i dischi* and is in the sidebar's *Altro* group (`MDI-F111`, `MDI-F113`). Phase R7: the merged page. |
| `CLAUDE.md` (D34, not a spec) | Applied 2026-10-29 by the main agent with the owner's approval (`Q33`): fork exceptions to "K3 labels must stay in sync with K2" and to "30 languages", and a FORK bullet for the K3 redesign. |
| Page specs of R2–R8 (D34) | `SpecExplore.md`, `SpecSearchList.md`, `SpecSearchResults.md`, `SpecSelection.md`, `SpecDevicesPage.md`, `SpecBackup.md`, `SpecSettingsPage.md`: note that a page rebuilt in D34 keeps its behaviour rows (`MDI-C40`), while its presentation follows the design system under `MDI-C37`'s exception. Rows affected: the row colours and selection highlight (`THM-F1`–`F4`, `THM-F8`, `THM-F17`, `THM-F18`, `THM-F22`), the section-title style (`PGL-F5`, `PGL-F8`, `PGL-C6`, `PGL-C13`) and `TYP-F6`. `THM-F19` (the Selection page's tinted surface) no longer applies since 2026-10-30 (`MDI-F115`). |
| `SpecTheme.md`, `SpecPageLayout.md`, `SpecComboBoxes.md`, `SpecSettingsPage.md`, `SpecValidationRules.md`, `SpecExplore.md` (TecaStyle, `MDI-C42`) | These name Breeze / `org.kde.desktop` (Linux) and Fusion (Windows, macOS) as K3's style: `THM-F9`, `THM-F10`, `THM-C14`, the `THM-F12` open point and the `THM-F9`/`F10`/`C14` charter lines; `PGL-C1` and the `PGL-F19`/`F20` charter lines; `CBX-F1`; `SET-F6`; the notes on `D1` and `D7` in `SpecValidationRules.md` ("Fusion is forced on Windows and macOS"); and the `EXP-F24`/`EXP-C27` charter lines. In the fork's K3 the style is `TecaStyle`, on Fusion, on every platform. Add a cross-reference to `MDI-C42`; the rows' aim stays. |
| `SpecK3Deployment.md` (Teca, TecaStyle) | `DPL-C4`, `DPL-C6` ("The Controls style stays Fusion") and *Known limitations* ("Fusion style, not the desktop style"): the fork's K3 selects `TecaStyle` in every build, and whether the bundled `styles/Fusion/Theme.qml` is still loaded has not been verified. The executable and the macOS bundle are named `Teca` (`MDI-F123`), while the K3 packaging scripts still expect `Katalog`. |
| `SpecSelection.md` (R7, `MDI-F125`) | `SEL-C11`: in the fork's K3 the used-space figure carries the label "Used Space: ". `SEL-F5`: the page's description line now sits above the reminder, which still has no label of its own. `THM-F10` (`SpecTheme.md`): the reminder's idle surface is accent-soft, not transparent, and its hover tint comes from `KTheme`. |
| `SpecKeyboardShortcuts.md` (`MDI-F124`) | `KBS-F1` speaks of "the visible Close button": *Home* shows no Close action, and `Esc` still works there. |
| `SpecDevicesSplit.md` (D35) | *Original Catalog* and *Impact Summary*: in the fork, a split also moves the old catalog's derived data to the new catalogs, copies its enrichment profile and builds their instant index; nothing derived is deleted (`MDI-F126`–`F128`, `MDI-C43`). The spec has no requirement IDs; converting it is a separate task. |
| `SpecExplore.md` (*Browse never empty*) | Add these next to `EXP-F27`, whose (a) still holds: entering Browse opens the catalog chosen in *I miei dischi*; the catalog cards shown when no catalog is open (`MDI-F129`), "Choose another drive" (`MDI-F132`) and "Add a drive" in an empty collection (`MDI-F134`); the "AI" tag on file rows (`MDI-F130`); the description line (`MDI-F131`); a folder that is its own catalog, with its "Catalog" tag (`MDI-F133`); a click that selects a file and a double click that opens it in the list view (`MDI-F135`, a deliberate divergence from K2). |
| `SpecCardsAndTables.md` (`MDI-F135`) | `CDT-F7` defines Enter as "the selected row's existing left-click action", which on Explore opens the file. In the fork's Browse list view a left click on a file now only selects it, while Enter and a double click open it. Restate `CDT-F7` for Browse files as "opens the file, as a double click does", and amend its charter line ("Select a file row and press Enter: it opens" still holds). Folder rows and Search results are unchanged. |
| `SpecKeyboardShortcuts.md` (D37) | `KBS-F1`: `Esc` is the Back of `MDI-F144` on every page, and the pages reached from the sidebar have no Close (`MDI-F146`); this replaces the target order of `KBS-F1`. `KBS-C1`: `Esc` calls the Back handler, which reuses the existing open paths (`MDI-C47`). Add rows for Back and Forward (`Cmd+[` / `Cmd+]`, `Alt+Left` / `Alt+Right`), `Cmd/Ctrl+1…9` and `F6` (`MDI-F144`, `MDI-F148`). `KBS-C2` (no new string) does not cover the Back and Forward tooltips. `KBS-O1` stays met. `KBS-F2`, `F3`, `F4`, `C3`, `C4` hold. |
| `SpecTypography.md` (D37, only if `Q46` confirms `MDI-F154`) | `TYP-F2` would apply again to the redesigned pages, through `KTheme.textScale`, which withdraws `MDI-C37`'s exception for text size. `TYP-F9`'s range (0.8 to 1.2) would need to reach 2.0 for WCAG SC 1.4.4. |
| `SpecExplore.md` (D37) | Add rows: the breadcrumb with clickable segments and *Up* (`MDI-F145`); Back and Forward over Browse locations (`MDI-F144`); no Close on Browse (`MDI-F146`); the scope chip (`MDI-F147`). `EXP-C21` holds: no second way of opening a catalog. |
| `SpecSelection.md` (D37) | The scope chip repeats the choice of *I miei dischi* on Search, Results, Browse, Statistics and Backup, with *Cambia* (`MDI-F147`), next to `SEL-F5`'s reminder; it never changes the choice. The cards can be chosen with the keyboard (`MDI-F152`). |
| `SpecExplore.md`, `SpecSearchResults.md` (`MDI-F157`) | Neither spec has a row for *Move to Trash*. Add one in each: a modal confirmation with K2's strings before the move, then the result; *Cancel* moves nothing. Note the two remaining differences from K2 (success path, failure as a passive notification). |
| `SpecDevicesSplit.md` (D36) | *Confirmation* ("This operation cannot be undone.") and *Original Catalog* ("they should duplicate it first") no longer hold where history is available (`MDI-F139`); the dialog's string changes only with per-string approval (`Q57`). The new catalogs get a valid update date (`MDI-F143`). The reverse operation is `MDI-F142`. |
| `SpecDevicesPage.md` (D36) | A restore point is taken before a catalog is updated, edited or deleted (`MDI-F136`). Where *Ricomponi* and a catalog's own history are offered. |
| `SpecSettingsPage.md` (D36) | The setting for the number of restore points per catalog (`MDI-F137`); its tab and key. |
| `SpecOperationQueue.md` (D37) | The bottom panel takes its own space and covers no page content (`MDI-F156`). |
| `SpecTheme.md` (D37) | The `controlLine` token and the 3:1 rule for control edges, focus ring and menu highlight (`MDI-F149`, `MDI-C47`). |

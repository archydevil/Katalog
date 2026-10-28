---
id: SpecMediaIntelligence
title: "Media Intelligence: Previews, AI Enrichment and Semantic Search"
description: "Requirements for the fork-only K3 enrichment of catalogs: previews, transcripts, AI summaries, provenance and semantic search"
version: "3.1"
---

# MEDIA INTELLIGENCE — PREVIEWS, AI ENRICHMENT AND SEMANTIC SEARCH

![Status](https://img.shields.io/badge/Status-Draft-orange) ![Version](https://img.shields.io/badge/Version-fork%203.1-blue) ![Implementation](https://img.shields.io/badge/Implementation-planned-lightgrey) ![Scope](https://img.shields.io/badge/Scope-fork%20only%20(K3)-purple)

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
derived data is stored in the collection database with full provenance. The
result is browsable when the device is disconnected and searchable by meaning
(semantic search).

> **Reading the requirement IDs.** Each requirement has a permanent ID (`MDI-`).
> IDs are never renumbered or reused; a retired requirement is marked
> `[Removed]`, not deleted. Status is one of
> `[Implemented] / [Planned] / [Backlog] / [Removed]`.

### Vocabulary {#vocabulary}

| Term | Meaning |
| ------ | --------- |
| **Enrichment** | Producing derived data for the files of a catalog, after and separately from catalog creation or update. |
| **Analysis** | One kind of enrichment: thumbnail, poster frame, keyframe strip, video clip, audio clip, waveform, transcript, segmentation, clustering, image caption and tags, document summary and excerpts, embeddings, application information. |
| **Derived datum** | One result of one analysis for one file (or one segment / cluster), with its provenance record. |
| **Derived file** | A derived datum that is a file (image, clip, waveform) written under the destination path. |
| **Destination path** | The folder, chosen per catalog, that holds the derived files. It is never on the cataloged source. |
| **Enrichment profile** | The per-catalog choice of file types, preview options, analyses and provider, saved so later runs reuse it. |
| **Enrichment job** | One run of the enrichment runner over one catalog with its profile. |
| **Provider** | The service that runs a model: **local** (Ollama on this machine), **local-network** (Ollama on a private-network host), or **remote** (a commercial API). |

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
(the core schema stays 3.0, D5), in File, Hosted and Memory modes; starting
enrichment on existing catalogs (D8); the interaction with the catalog's
*Include metadata* level.

**Later phase (in scope, `[Backlog]`):** text and Office documents beyond PDF;
characterising information for applications and other files.

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

---

## Operational requirements — *why / for whom*

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-O1 | A user recognises the content of files on a drive that is not connected — sees an image, a video's frames, hears a clip, reads a summary or a transcript — without mounting the drive. | [Planned] |
| MDI-O2 | A user finds files by what they are about, with a question in ordinary language, not only by name or metadata. | [Planned] |
| MDI-O3 | A user navigates a long recording by topic: they see where each topic starts and ends and what it covers, and which recordings share a topic. | [Planned] |
| MDI-O4 | A user keeps control of their data: by default nothing leaves the machine, and sending content to a commercial service is a deliberate, informed choice made with its cost in view. | [Planned] |
| MDI-O5 | A user can trust and redo a derived result: they can tell what produced it, when, where and at what cost, and can produce it again. | [Planned] |
| MDI-O6 | A user keeps working in Katalog (searching, exploring) while enrichment runs, and can pause it, stop it and resume it later — including after closing Katalog — without redoing finished work. | [Planned] |
| MDI-O7 | A user sets up the cataloging of a drive together with its previews and analyses in one guided pass on the screen they already use to create catalogs. | [Planned] |

## Functional requirements — *what the system does*

### Wizard mode of the Create page {#wizard}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F1 | The K3 Create page offers two modes: the existing form (**standard** mode) and a **wizard** mode. Switching mode does not open another page and does not clear the values common to both (source path, catalog name, storage). In standard mode the page behaves exactly as before this spec; the mode switch is the only visible addition. The last mode used is restored when the page is opened again. | [Planned] |
| MDI-F2 | The wizard presents its choices as ordered steps: (1) source drive or folder; (2) destination path for derived files; (3) file types to include; (4) preview options; (5) AI options; (6) a summary of every choice with a *Create* action. The user can go back to any previous step without losing later choices. | [Planned] |
| MDI-F3 | Step 3 offers the file types Image, Video, Audio, Document and Other, each on or off independently. Only files of the selected types are enriched; the catalog itself is created with the same file selection standard mode would use for the same source. | [Planned] |
| MDI-F4 | Step 4 offers, per type: **Image** — thumbnail on/off. **Video** — poster frame on/off; keyframe strip on/off with a frame count (default 8, range 2–32); low-resolution clip off / full / partial, where partial takes a start offset and a duration in seconds (default 0 s and 30 s). **Audio** — clip off / full / partial (same parameters); waveform image on/off. | [Planned] |
| MDI-F5 | Step 5 offers a provider choice and the analyses: image caption and tags; document summary and key excerpts; transcript; topic segments; topic clusters; embeddings for semantic search. The default provider is the local one. A commercial provider appears in the list only when it is configured (`MDI-F23`). An analysis whose external tool or model is unavailable is shown disabled with the reason (`MDI-F20`). Topic segments require transcript; topic clusters require topic segments; selecting a dependent analysis selects its prerequisite. Generated text (captions, tags, summaries, segment titles and summaries, topics, cluster labels and descriptions) is in the source's language by default; an option sets it to the UI language instead (D12). Key excerpts stay verbatim (`MDI-F40`). | [Planned] |
| MDI-F6 | *Create* is refused, with a `Kirigami.InlineMessage` at the top of the wizard and nothing created, when: the source is empty; the destination path is empty; the destination path is not writable; the destination path is the source or lies inside it; no file type is selected while at least one preview or analysis is selected. The message hides when the offending field is edited (`SpecValidationRules.md`). | [Planned] |
| MDI-F7 | Confirming the wizard creates the catalog through the **existing** catalog-creation path (same entry point, same queue behaviour, same result as standard mode for the same inputs) and saves the enrichment profile for that catalog. Only when the creation **completes successfully** is an enrichment job for that catalog added to the enrichment list (`MDI-F16`). A creation that fails, is cancelled, or is removed from the operation queue creates no enrichment job. | [Planned] |
| MDI-F8 | The enrichment profile is saved per catalog and survives closing and reopening the collection. Later runs on that catalog use it. For any existing catalog — created in wizard mode, in standard mode, or before this feature — the user can, from the catalog's page and from Explore, edit its enrichment profile (or create one when it has none) and start an enrichment job, which is added to the enrichment list (`MDI-F16`) (D8). No new page is created for this (`MDI-C18`). | [Planned] |

### Enrichment runner {#runner}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F9 | The runner processes one file at a time. For each file it runs the profile's analyses in a fixed order and commits each analysis's result and provenance before starting the next. Stopping, a crash or a power loss loses at most the analysis in progress. | [Planned] |
| MDI-F10 | **Stop** ends the running job after the current analysis step is abandoned (its external process is terminated, its HTTP request aborted); results already committed are kept. The job's final status is *Stopped*. | [Planned] |
| MDI-F11 | **Pause** halts the job before its next analysis step; **Resume** continues from there. While paused, no external process of the job runs and no request is sent. | [Planned] |
| MDI-F12 | A job that had not finished when Katalog closed continues from the first file without a completed result for each selected analysis when the collection is opened again. A job whose analyses use only local tools and the local provider resumes automatically. Any other job (one using a commercial, remote or local-network provider) is listed as *Paused* and sends nothing until the user resumes it; resuming it is subject to `MDI-F24` (D6). | [Planned] |
| MDI-F13 | **Incremental.** A file is processed for an analysis only when it has no completed datum for that analysis, or when its current size or modified date differs from the values recorded with that datum (`MDI-F52`), or when the profile's parameters for that analysis changed. Every other file is counted as *unchanged* and skipped. Running a job twice in a row with no file change processes zero files. | [Planned] |
| MDI-F14 | When a file has left the catalog (removed by a device update), the next job on that catalog removes its derived data from the database and deletes the derived files recorded for it. Only derived files the runner itself created and recorded are ever deleted. | [Planned] |
| MDI-F15 | While a job runs: a search, an Explore load, a catalog creation and a device update of **another** catalog are not refused because of it. A device update, re-scan or deletion of the catalog **being enriched** first pauses that job; after an update it resumes automatically and picks up the changes through `MDI-F13`; after a deletion the job and the catalog's derived data are removed (`MDI-F57`). | [Planned] |
| MDI-F16 | Enrichment jobs run one at a time, in the order they were added, from their own list. Adding a job for a catalog already listed is ignored. A waiting job can be removed without affecting the running one. This list is **not** the operation queue of `SpecOperationQueue.md` (`MDI-C3`). | [Planned] |
| MDI-F17 | Enrichment progress is shown in the activity panel, as its own row, distinct from the queue's running entry, so it is visible alongside a running creation, update or search. It shows the standard status message (`MDI-C16`) with operation, status, catalog context, the current analysis as process title, count of total, time to completion and current file, and carries Pause/Resume and Stop. | [Planned] |
| MDI-F18 | When a job ends, its summary line reports, per analysis: completed, unchanged, skipped (tool or model missing), failed. A *Report* action lists each failed file with the analysis and the reason. The report is kept for the session. | [Planned] |
| MDI-F19 | Before a job starts, the destination path's free space is checked against an estimate of the derived files' size. With insufficient space the job does not start and the summary reports it; when free space exceeds the estimate by less than 10 %, it starts and the summary warns. | [Planned] |

### External tools {#tools}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F20 | Katalog detects `ffmpeg`, `ffprobe`, `whisper.cpp` (executable and model file), `pdftotext` and `pdftoppm` on the system search path and in the user-configured paths. The Settings page shows, for each tool, the path found and the version it reports, or that it was not found, and lets the user set or clear a path per tool. Detection runs when Settings opens and before each job. Katalog does not ship these tools or the whisper model; the user installs them (D9). | [Planned] |
| MDI-F21 | When a tool an analysis needs is missing, that analysis is skipped for the whole job and counted as *skipped (tool missing)* with the tool's name in the summary; the job's other analyses run. Katalog never crashes or freezes because a tool is missing. | [Planned] |
| MDI-F22 | When a tool fails on one file (non-zero exit code, unreadable output, or no output within its time limit, default 10 minutes per invocation, configurable), the file's analysis is recorded as failed with the reason and the runner moves on to the next file. A timed-out process is terminated. | [Planned] |

### Providers {#providers}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F23 | Settings holds the providers. **Local:** an Ollama endpoint (default `http://localhost:11434`) and one model per task: text, vision, embeddings. A connection test lists the models the endpoint offers. **Commercial (off by default):** an OpenAI-compatible API (base URL, model per task, key) and the Anthropic API (model per task, key), and a price per million input tokens and per million output tokens entered by the user; no price list is shipped (D14). A key is either entered for the session only, or read from an environment variable; it is never saved (`MDI-C13`, D10). A commercial provider can be used by a job only when it is enabled for the open collection, or enabled for that job in the wizard. | [Planned] |
| MDI-F24 | Before a job sends any content to a remote or local-network provider, a modal confirmation states: the provider and endpoint host; the kinds of content sent (thumbnails, frames, extracted text, transcript text); the number of files; and the estimated cost (`MDI-F25`). *No* is the default. Confirming starts the job; declining sends nothing, and the job then runs only its analyses that use the local provider or local tools. | [Planned] |
| MDI-F25 | The cost estimate is the estimated input and output tokens of the job's remote analyses multiplied by the provider's prices (`MDI-F23`), shown as an amount with its currency and labelled as an estimate. When no price is entered, the dialog states the cost is unknown; confirming is still possible. | [Planned] |
| MDI-F26 | A provider endpoint is classified local, local-network or remote by its host, with the same rule as `SpecHostedSecurity.md` `HST-F2`–`HST-F4`. A local-network or remote endpoint is subject to `MDI-F24` whatever product it runs. | [Planned] |
| MDI-F27 | For each remote call, the cost reported by the provider's usage data (or computed from the returned token counts and the entered prices when the provider gives no amount) is recorded on the datum; the job summary shows the total cost. | [Planned] |
| MDI-F28 | When a provider is unreachable or refuses three consecutive requests, the job pauses and its status names the provider and the error. Resuming retries. Katalog does not retry in a loop. | [Planned] |

### Previews {#previews}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F29 | **Image thumbnail:** a JPEG whose longest side is 320 px (configurable 128–1024), aspect ratio kept, orientation tag applied. | [Planned] |
| MDI-F30 | **Video poster frame:** one frame taken at 10 % of the duration (from `ffprobe`), saved like a thumbnail. | [Planned] |
| MDI-F31 | **Video keyframe strip:** the selected number of frames, evenly spaced over the duration, each saved like a thumbnail and recorded with its timecode. | [Planned] |
| MDI-F32 | **Video low-resolution clip:** an MP4 (H.264 video, AAC audio) no taller than 360 px, of the whole file or of the selected partial range. | [Planned] |
| MDI-F33 | **Audio clip:** an M4A (AAC) of the whole file or of the selected partial range. **Waveform:** a PNG image of the whole file's amplitude. | [Planned] |
| MDI-F34 | Derived files are written only under the catalog's destination path, in a layout that does not depend on the source file's name or folder (`MDI-C19`). Opening a derived clip or image opens it with the system's default application. | [Planned] |

### Video and audio — transcripts, segments, clusters {#transcripts}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F35 | **Transcript:** the audio track is extracted with `ffmpeg` and transcribed locally with `whisper.cpp`. The transcript is stored as an ordered list of passages, each with start and end timecodes and text, plus the detected language. | [Planned] |
| MDI-F36 | **Topic segments:** the text model divides a transcript into consecutive segments. Each segment has a start and end timecode taken from transcript passage boundaries, a title, a summary and a list of topics. Segments cover the transcript without overlapping. | [Planned] |
| MDI-F37 | **Topic clusters:** segments are grouped into clusters, each with a label, a description and its member segments; a segment belongs to at most one cluster. Clustering is per catalog: a cluster groups segments of files of one catalog only (D11). When clusters are recomputed is decided at the start of Phase C (`Q3b`). | [Planned] |
| MDI-F38 | Wherever a segment or transcript passage is shown, its timecodes are shown as `h:mm:ss`. | [Planned] |

### Images — caption and tags {#images}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F39 | **Caption and tags:** the vision model receives the image's thumbnail (never the original) and returns a caption of at most two sentences and a list of tags. Both are stored with the image. These AI tags are kept separate from Katalog's user tags (`Tags` page) and never change them. | [Planned] |

### Documents {#documents}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F40 | **PDF summary and key excerpts:** text is extracted page by page with `pdftotext`; the text model returns a summary and up to 10 key excerpts, each excerpt verbatim from the text with its page number. | [Planned] |
| MDI-F41 | A PDF with no text layer is recorded as *skipped (no text)*, unless vision reading of scanned pages is selected in the profile: then up to 20 pages are rendered with `pdftoppm` and read by the vision model, subject to `MDI-F24` when that provider is not local. | [Planned] |
| MDI-F42 | **Other documents:** plain-text and Markdown files, then Office formats, receive the same summary and excerpts as PDF, with a location reference where the format has one. The tool for Office formats needs its own approval. | [Backlog] |

### Applications and other files {#apps}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F43 | **Characterising information:** for applications and other binary files, Katalog records where obtainable: version, publisher, architecture, bundle identifier and code-signature status. Each item absent from a file is left empty, not guessed. The means per platform need their own approval. | [Backlog] |

### Browsing derived data offline {#offline}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F44 | In Explore and in Search results, a file that has derived data is marked, and selecting it shows its derived data: thumbnail or frames, clip and waveform, caption and AI tags, summary and excerpts, transcript and segments. This works identically whether the device is connected, disconnected or inactive. | [Planned] |
| MDI-F45 | When the destination path is unreachable (its drive disconnected, or the collection opened on another machine), the derived text (captions, tags, summaries, excerpts, transcripts, segments) is still shown from the database, and each derived file is replaced by a placeholder saying it is not available. No error dialog is shown. | [Planned] |
| MDI-F46 | From a segment, *Open at timecode* opens the derived clip, or the original when the device is connected, with the system default application; the timecode is shown for the user to seek, because seeking an external player is not portable. There is no in-app playback (D7). | [Planned] |

### Semantic search {#semantic}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F47 | The Search page offers a **semantic** search mode, distinct from the existing text search. Its query is one natural-language text. Results are ranked by similarity between the query's embedding and the stored embeddings of captions, summaries, excerpts, transcript passages and segments. | [Planned] |
| MDI-F48 | Each semantic result shows the file, the passage that matched, its kind (caption, summary, excerpt, passage, segment), its rank, and, for passages and segments, the timecode. Several matches in the same file are grouped under that file. | [Planned] |
| MDI-F49 | Semantic search covers only the devices of the current selection (`SpecSelection.md`). Stored embeddings made by a model other than the collection's current embeddings model are not compared; their count is reported with the results. | [Planned] |
| MDI-F50 | The query is embedded by the collection's embeddings provider. When that provider is not local, the first semantic search of the session shows the `MDI-F24` confirmation (without file count or cost estimate); declining runs no search. | [Planned] |
| MDI-F51 | Semantic search returns at most 50 results by default (configurable 10–500), runs in the background, reports progress with the standard builder through the activity panel as a search does (`OPQ-F15`), and can be stopped. | [Planned] |

### Provenance and storage {#provenance}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F52 | Every derived datum records: the file (or segment / cluster) it belongs to; the analysis; the tool name and version, or the model name and version/digest; the provider name, its class (local / local-network / remote) and endpoint host; the date; the file's size and modified date at processing time; the analysis parameters, including the prompt version; the cost and currency, or none; and the outcome (completed / failed / skipped) with the reason. | [Planned] |
| MDI-F53 | A user can re-run an analysis on a catalog for all files, or only for files whose last outcome is failed. A re-run replaces the current datum: the replaced data (text, embedding, derived file) is discarded, and only its provenance record is kept as history (D13). | [Planned] |
| MDI-F54 | The prompts used by analyses are versioned; changing a prompt changes its version, and `MDI-F13` then treats the analysis as having changed parameters. | [Planned] |
| MDI-F55 | Derived data is stored in File, Hosted and Memory modes alike. In Memory mode it is written to new files with an `mdi_` prefix in the collection folder and read back when the collection is opened, so it survives closing Katalog; it is loaded into its tables before any query that reads it. | [Planned] |
| MDI-F56 | Collection export (`SpecCollectionExport.md`) carries the derived data to the target mode. Derived files are not copied; the exported records keep pointing to the same destination path. | [Planned] |

### Interaction with catalog settings {#catalog-settings}

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-F57 | Deleting a catalog deletes its enrichment profile, its jobs, its derived data and the derived files recorded for it; the existing deletion confirmation states that derived files will be deleted too. | [Planned] |
| MDI-F58 | Enrichment is independent of the catalog's *Include metadata* level. It runs with any level, including *None*. Changing the level, including to *None*, never clears, invalidates or re-runs derived data; its clearing scenarios (`SpecCatalogMetadata.md` Scenario 2) apply to file metadata only. See the note below. | [Planned] |
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

## Constructional requirements — *how it is built / limits / MUST-NOTs*

| ID | Requirement | Status |
| ---- | ------------- | -------- |
| MDI-C1 | **Fork-only, K3-only (D1).** No file under `qt_widgets/` is changed. Nothing in this spec is proposed upstream. An enriched collection stays a schema 3.0 collection: K2 and upstream K3 3.0 open it normally and ignore the `mdi_` tables and `mdi_` files (D5, `MDI-C15`). | [Planned] |
| MDI-C2 | Catalog creation and device update MUST NOT change behaviour: the runner is not called from, hooked into, or run inside `CatalogJobStoppable`, `CatalogManager` or `DeviceUpdateManager`. The wizard reaches creation only through the existing entry point used by standard mode. | [Planned] |
| MDI-C3 | **Coexistence with `SpecOperationQueue.md`.** The runner executes on its own `QThread`, like the backup run (`BKP-C11`), and is **not** one of the operations `OPQ-C3` and `OPQ-C4` govern: it is not a queue entry, never enters, starts, advances or drains the operation queue, and is unaffected by its Clear. `OPQ-C1` holds because the runner never schedules a creation or an update. Its presence does not count as "an operation running" for the `OPQ-C15` guards of creation, update, search or Explore load. The only interactions are `MDI-F15` (pausing on an update of the same catalog) and `MDI-F17` (its row in the activity panel), which require the `OPQ-C16` amendment listed under *Specs to amend*. | [Planned] |
| MDI-C4 | The runner uses its own database connection, opened on its own thread, never the main-thread connection. Its writes are short per-file transactions. A search, Explore load or update running on the main thread MUST NOT fail or stall for more than one file's commit because of the runner. The File-mode mechanism (journal mode, busy timeout) is `Q6`, decided during Phase A implementation and recorded back here. | [Planned] |
| MDI-C5 | **Memory mode.** The in-memory database is not shared with the runner's thread unless `Q6` (decided during Phase A implementation) adopts a mechanism. Whatever is decided, `MDI-F55` holds: derived data persists in `mdi_`-prefixed collection-folder files, and any query on derived tables is preceded by their load, as for `file` and `folder` (Memory Mode Caveat in `CLAUDE.md`). | [Planned] |
| MDI-C6 | **Hosted mode.** The runner opens its own connection with the collection's already validated host settings. Derived-file locations are stored relative to the destination path, which is recorded per catalog, so a second machine can point to its own copy. Whether several machines share one destination path or use one each is `Q11` (deferred). | [Planned] |
| MDI-C7 | External tools are started with `QProcess` using a program path and an argument list — never through a shell or a command string. Standard output and error are captured; the process is terminated on Stop, on time limit and on application exit. Tool paths are application settings (machine-specific); per-collection choices (profile, provider enablement) are stored with the collection. New settings keys only add to `katalog3_prerelease_settings.ini`; existing keys keep their meaning, so a beta settings file stays readable. | [Planned] |
| MDI-C8 | No new linked library and no new Qt or KF6 module: `ffmpeg`, `whisper.cpp` and poppler are external programs; HTTP uses `Qt6::Network`; no vector-index library, no keychain library, no Qt Multimedia. Adding any of these needs explicit user approval and an amendment here first. | [Planned] |
| MDI-C9 | Embeddings are stored in the database with the model name, model version and dimension. Similarity (cosine) is computed in `core/` by scanning stored vectors in batches; the scan is stoppable between batches and reports progress. A performance target is `Q14`, decided in Phase D. | [Planned] |
| MDI-C10 | The runner MUST NOT write, modify, rename or delete anything on the cataloged source. It reads source files only, and only while their device is connected. All derived files go under the destination path, which is validated as outside the source (`MDI-F6`). | [Planned] |
| MDI-C11 | Katalog MUST NOT send any content, query or metadata off the machine except to a provider enabled and confirmed under `MDI-F23`/`MDI-F24`/`MDI-F50`. With default settings the only network destination is `localhost`. No telemetry is added. | [Planned] |
| MDI-C12 | Remote providers MUST NOT receive an original file — only derived inputs (thumbnails, sampled frames, rendered pages, extracted or transcribed text). Transcription is local only (`whisper.cpp`); no remote speech-to-text. | [Planned] |
| MDI-C13 | An API key MUST NOT be written to any file, `.ini` settings file, database or log. It is held in memory for the session only, or read from an environment variable at use (D10). Keys and authorization headers are never written to logs or reports. | [Planned] |
| MDI-C14 | **Core boundary.** All SQL, tool invocation, HTTP requests, prompt construction, cost computation and similarity live in `core/`. `qt_quick/` contains no raw `QSqlQuery` and only orchestrates and displays. Every new `core/` class or method still needs explicit user approval before it is written (`CLAUDE.md`); approval of this spec is not approval of a core design. | [Planned] |
| MDI-C15 | **`mdi_` tables outside the core schema (D5).** The core schema stays 3.0: this fork MUST NOT change the core schema version, never stamps 3.1, and MUST NOT edit `runMigration_3_0`. The new tables (at least: enrichment profile, enrichment job, derived datum with provenance, derived file, transcript passage, topic segment, topic cluster and membership, embedding) all have the prefix `mdi_`. Their own version ("media intelligence schema version 1") is stored separately from the core schema version, in a single-row `mdi_schema` table or equivalent. A separate migration creates the `mdi_` tables when they are absent and upgrades them when their version is older; it is idempotent and touches only `mdi_` tables. No 3.0 table or column is altered, added to or removed. Memory mode adds new `mdi_`-prefixed files to the collection folder and changes no existing file format. | [Planned] |
| MDI-C16 | Every progress and status message is built with `StatusBarMessageBuilder` (`SpecProgressReport.md`); no raw status string. | [Planned] |
| MDI-C17 | **User-visible text.** Every new string needs per-string approval of its exact text before it is written, and existing strings are reused wherever they fit. Copy states what is needed, without "Please" or orders. Blocking validation uses `Kirigami.InlineMessage`; decisions (egress confirmation, deletions) use modal dialogs; transient success uses `showPassiveNotification`. No string is approved by this spec. | [Planned] |
| MDI-C18 | **UI structure.** No new Page: the wizard is a mode of the existing `PageCreateForm.qml`, derived data is shown inside the existing Explore and Search results pages, provider and tool settings inside the existing Settings page. Creating any new `.qml`, `.cpp` or `.h` file needs the user's explicit request first. | [Planned] |
| MDI-C19 | **Cross-platform.** Tools are resolved with the platform's executable suffix; paths are built with Qt path functions. Derived-file names are short identifiers, not source names, so they stay within Windows path limits and cannot collide. No platform-specific API is used in phases A–D. | [Planned] |
| MDI-C20 | HTTP requests have a time limit (default 120 s, configurable) and are aborted on Stop. A provider response that cannot be parsed into the expected fields is recorded as a failed datum with the reason, never stored as a result. | [Planned] |

---

## Phasing {#phasing}

| Phase | Content | Rows |
| ------- | --------- | ------ |
| **A** | Wizard mode; runner and its lifecycle; tool detection; previews; offline browsing of previews; `mdi_` tables and their migration (core schema stays 3.0); provenance; enrichment of existing catalogs; Memory/Hosted support; *Include metadata* independence. | `MDI-F1`–`F4`, `F6`–`F22`, `F29`–`F34`, `F44`–`F46`, `F52`–`F59`; all `MDI-C` |
| **B** | Providers (local, then commercial with egress confirmation and cost); image caption and tags; PDF summary and excerpts. | `MDI-F5`, `F23`–`F28`, `F39`–`F41` |
| **C** | Transcripts, topic segments, topic clusters. | `MDI-F35`–`F38` |
| **D** | Embeddings and semantic search. | `MDI-F47`–`F51` |
| **E** | Other documents; applications and other files. | `MDI-F42`, `F43` (`[Backlog]`) |

A phase starts only when the previous one is implemented, and each phase starts
with the core design approval required by `MDI-C14` and the string approvals of
`MDI-C17`.

---

## User-visible text

No string is approved yet. Each string this feature needs — wizard step titles,
option labels, analysis names, the operation name and process titles for the
status builder, the egress confirmation, placeholders — is proposed with its
exact text, checked first against existing K2/K3 strings for reuse, and written
only after its own approval (`MDI-C17`). Approved strings are listed here as they
are approved.

---

## Open questions {#open-questions}

### Still open (deferred)

| # | Question | Decided when |
| --- | ---------- | -------------- |
| Q3b | When topic clusters are recomputed (the per-catalog scope is decided, D11) (`MDI-F37`). | Start of Phase C |
| Q6 | Concurrency mechanism: File mode journal mode / busy timeout; Memory mode shared in-memory database versus file-based handover (`MDI-C4`, `MDI-C5`). An implementation detail, decided during Phase A implementation and recorded back here. | Phase A |
| Q8 | Semantic search and search history; which other criteria (file type, size, date) combine with semantic mode. | Phase D |
| Q11 | Hosted mode with several machines: one shared destination path or one per machine (`MDI-C6`). | Deferred |
| Q14 | Performance target for semantic search (collection size and response time) (`MDI-C9`). | Phase D |

### Decided 2026-10-28 (kept for traceability)

| # | Question | Decision |
| --- | ---------- | ---------- |
| Q1 | API key storage. | D10 — never in plain text in any `.ini`; session only or environment variable (options b + c). Phase B onward. |
| Q2 | Automatic resume after restart for local-only jobs. | D6 — local-only jobs resume automatically; others wait for Resume. |
| Q3 | Scope of topic clustering. | D11 — per catalog. (Recompute timing split out as `Q3b`.) |
| Q4 | Language of generated text. | D12 — the source's language, with an option for the UI language. |
| Q5 | In-app playback versus the system default application. | D7 — system default application; no Qt Multimedia in Phase A. |
| Q7 | Re-run history. | D13 — keep only the provenance of replaced data. |
| Q9 | Profile editing and enrichment of existing catalogs. | D8 — from the catalog's page and from Explore. |
| Q10 | Cost prices. | D14 — user-entered only. |
| Q12 | What K2 and upstream K3 3.0 do when opening an enriched collection. | D5 — they open it normally and ignore the `mdi_` tables; no schema bump. |
| Q13 | Schema number collision with a future upstream 3.1. | D5 — no collision: the fork never bumps the core schema; `mdi_` tables carry their own version. |
| Q15 | Distribution of external tools and the whisper model. | D9 — user-installed; bundling and licence review deferred. |

---

## Manual test charter

For each row: set up the stated condition, run the operation, confirm the result.

- **MDI-F1** — Fill source and name in standard mode, switch to wizard and back: the values remain; standard-mode creation gives the same catalog as before this feature.
- **MDI-F6** — Set the destination path inside the source: *Create* shows the inline message and creates nothing; editing the path hides it.
- **MDI-F7** — Cancel a wizard creation while it runs: no enrichment job appears. Let one complete: a job appears for that catalog.
- **MDI-F9 / F10** — Stop a job mid-run: files already done keep their results; the status is *Stopped*; no orphan `ffmpeg` process remains.
- **MDI-F11** — Pause: no tool process runs and no request is sent until Resume.
- **MDI-F8** — On a catalog created in standard mode, open its page and Explore: the profile can be created or edited and a job started from each; the job appears in the enrichment list.
- **MDI-F12** — Quit during a local-only job, reopen: it resumes by itself without redoing finished files. Quit during a job using a commercial provider, reopen: it is *Paused*, nothing is sent until Resume and its confirmation.
- **MDI-F13** — Run a job twice with no change: the second processes zero files. Modify one file and update the catalog: only that file is reprocessed.
- **MDI-F14** — Delete a source file, update the catalog, run the job: its derived data and files are gone; no other file in the destination path is touched.
- **MDI-F15** — Run a search and an Explore load during a job: both work. Update the enriched catalog: the job pauses, then resumes after the update.
- **MDI-F16** — Add jobs for two catalogs and the first one again: two jobs, run in order.
- **MDI-F17** — Run a job and a search together: the activity panel shows both rows; each Stop stops only its own operation.
- **MDI-F18** — Include a corrupt video: the summary counts one failure; *Report* shows the file and reason.
- **MDI-F19** — Choose a nearly full destination: the job does not start and the summary says why.
- **MDI-F20 / F21** — Remove `ffmpeg` from the path: Settings shows it not found; the wizard disables video analyses with the reason; a job skips them and runs the rest without crashing.
- **MDI-F22** — Set a 1 s time limit on a long video: that file fails with a time-limit reason; the next file is processed.
- **MDI-F23 / F24 / F25** — Enable a commercial provider and start a job using it: the confirmation names provider, content kinds, file count and estimated cost, *No* is the default; declining sends nothing (verified with a network monitor).
- **MDI-F26** — Point the Ollama endpoint at a public host: the job asks for the `MDI-F24` confirmation.
- **MDI-F27** — After a remote job: each datum shows a cost; the summary shows the total.
- **MDI-F28** — Stop Ollama during a job: after three failures the job pauses and names the provider.
- **MDI-F29–F33** — Check each derived file's format, size limit and range against the row.
- **MDI-F34 / C19** — Inspect the destination path: names are identifiers, not source names; nothing was written on the source.
- **MDI-F35 / F36 / F38** — Transcribe a recording: passages carry timecodes; segments have title, summary, topics, contiguous `h:mm:ss` ranges.
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
- **MDI-F55** — In Memory mode, enrich, close, reopen: derived data is present and shown.
- **MDI-F56** — Export a File collection to Memory mode and back: derived data survives; derived files still open.
- **MDI-F57 / F59** — Delete a catalog, and clear derived data on another: confirmation shown; data and recorded files removed; other files in the destination path untouched.
- **MDI-F58** — Set *Include metadata* to *None* on an enriched catalog and update: every derived datum remains.
- **MDI-C1** — `git diff` of the feature touches nothing under `qt_widgets/`. Open an enriched collection (File and Memory modes) with K2 2.13 and upstream K3 3.0: it opens normally, and the `mdi_` tables and files are left in place.
- **MDI-C2** — Create the same source in standard mode before and after the feature: identical catalog and identical progress messages.
- **MDI-C4** — Search repeatedly in File mode during a job: no "database is locked" error.
- **MDI-C7** — Name a source file with spaces, quotes and `;`: tools process it; nothing is executed by a shell.
- **MDI-C11** — With default settings, a network monitor shows connections to `localhost` only during a full job.
- **MDI-C13** — Use a commercial provider once with a session key and once with an environment-variable key: search every `.ini`, the database and logs for the key: absent; after restart a session key must be entered again.
- **MDI-C15** — Open a 3.0 collection: the `mdi_` tables and the `mdi_` version record exist, the core schema version is still 3.0, every 3.0 table and column is unchanged; open it again: the `mdi_` migration changes nothing. In Memory mode: only new `mdi_`-prefixed files appear in the collection folder.

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
| `SpecProgressReport.md` | Component table: a new operation name for enrichment, process titles per analysis, use of Time to Completion (part 10, today metadata extraction only) and Current Item for enrichment and semantic search; a new block in the Message Sources table. Each new string needs approval. |
| `SpecValidationRules.md` | Create page: add wizard rules after C4 for the `MDI-F6` refusals. Dialogs: add the egress confirmation (`MDI-F24`), the *Clear derived data* confirmation (`MDI-F59`) and the extended catalog-deletion text (`MDI-F57`) to the dialogs the button-order rules cover. |
| `SpecHostedSecurity.md` | `HST-C1` (classification only by `Database::validateHostname()`): state that provider endpoints reuse it (`MDI-F26`). |
| `SpecDevicesPage.md` | Catalog deletion text (`MDI-F57`); the location of *Clear derived data*, profile editing and starting enrichment on the catalog's page (`MDI-F8`, D8). |
| `SpecExplore.md` (D8) | In addition to the row above: profile editing and starting enrichment from Explore (`MDI-F8`). |
| `SpecK3Deployment.md` | State that this fork's builds do not bundle external tools or models; they are user-installed and detected (`MDI-F20`, D9). Bundling and licence review deferred. |

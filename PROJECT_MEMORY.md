# Teca - shared project memory

Updated: 2026-10-31. Audience: Copilot, Codex and Claude Code.

## Pause and resume

The owner asked to **pause development and save shared memory**. This handoff
does not start extraction, create a tools repository, implement an API, change
licences, send a letter or authorise a push. Resume only when asked.

Read [AGENTS.md](AGENTS.md) and the development rules in [CLAUDE.md](CLAUDE.md).
This file is a handoff, not a replacement for the specification gate. Approved
requirements live in [the specifications index](docs_src/docs/development/Specifications.md).
Re-check the worktree and the relevant code before relying on historical
observations below.

## Latest architectural direction - owner selected

The latest conversation changed the direction from "everything inside the
Katalog core" to **independently usable Teca tools/services**:

- Tools are to live in a **separate repository**, provisionally `teca-tools`.
- They should work without Katalog/Teca, accept files from other sources, and
  be callable directly by external applications.
- Contact with the original core should be optional, through a documented
  process/API contract when its catalog/device services are needed.
- The owner selected **specification first**, not immediate extraction.
- A **new standalone UI as an API client is planned later**. The present
  Qt Quick UI has not been replaced or relicensed.

Owner selections: `repo=separate`, `step=spec_first`,
`ui_plan=new_ui_later`.

**Not yet recorded as an approved feature spec:** the previous assistant tried
to write `SpecTecaTools.md`, but the write tool was unavailable. The file is
absent at this handoff. No tools repository, independent executables, tools
server or extraction implementation has been created.

The proposed names (`teca-enrich`, `teca-index`, `teca-people`, `teca-study`,
`teca-publish`, `teca-export`, `teca-serve`), ports, protocols, granularity and
extraction lots were suggestions, not separately approved requirements.

**Do not implement that attempted draft verbatim.** In particular, it offered
dynamic loading of a proprietary library into the GPL executable and described
that as a process boundary. It is not a separate process; prefer evaluating an
actual external-process contract and obtain licensing review before extraction.

## Existing decisions and documents

| Item | Recorded state | Next dependency |
| --- | --- | --- |
| Hosted/cloud P5 | D45 accepted the proposed answers (a)-(l); design, not implemented | Permanent requirements, schema/DDL and core-method listing, approval and gate |
| Original-core API | [SpecTecaApi.md](docs_src/docs/development/SpecTecaApi.md) exists as a draft; HTTP/JSON, local-first, read-only v1 was proposed | Review against the newer standalone-tools direction; approve the detailed contract |
| Standalone tools | Owner selected separate repo, spec first, independent operation | Create and review `SpecTecaTools.md` when work resumes |
| Licensing strategy | Owner selected asking about a commercial licence plus API-based products; rewrite as fallback | Legal/provenance/dependency assessment; no agreement obtained |
| Rewrite fallback | [SpecCleanRoomNotes.md](docs_src/docs/development/SpecCleanRoomNotes.md) is plan B, not started | Reassess its legal claims and estimates before treating it as a protocol |
| Hosted proposal history | [SpecMediaIntelligenceBacklogNotes.md](docs_src/docs/development/SpecMediaIntelligenceBacklogNotes.md) records the P5 design | Reconcile it with separate tools before implementing P5 |

[SpecMediaIntelligence.md](docs_src/docs/development/SpecMediaIntelligence.md)
contains D45 and Q76. D45 includes a companion folder, one AI hub, published AI
data for other computers, read-only DB users, TLS off localhost, K3-only host
classification changes, and macOS keychain use for Hosted credentials. New
strings remain subject to approval and translation. The newer tools direction
may require amendments; do not silently treat it as already integrated into D45.

An Italian licence-request letter was drafted outside the repository in a local
session artifact. It was **not sent**. It is not a commercial licence or proof
that the upstream author owns every required right. Recreate/review it if that
artifact is unavailable on the next machine.

## Licensing and estimate caveats - important corrections

Earlier conversation and draft pages made overly categorical claims. Do not
carry them forward as established facts:

- A socket, HTTP API, separate repository or separate executable is **not by
  itself a legal guarantee** that a work is independent of GPL-covered code.
  Provenance, copied material and the substance of the integration matter.
- Newly added files are not automatically exclusively owned or freely
  relicensable. Audit their origins, headers and dependencies, including UI
  components derived from Qt/KDE styles. Keep existing notices intact.
- Repository `LICENSE.md` contains GPLv3; inspected source headers such as
  [collection.h](core/collection.h) say GPL version 2 or later. Inventory the
  actual applicable licences, rather than declaring every file GPLv3-only.
- Git author aliases do not prove that there is a single copyright holder.
  Contributor rights, externally supplied translations and third-party code
  still need verification before negotiating relicensing.
- Do not assume all Qt modules/KF6 components are LGPL or that buying Qt
  commercially resolves Katalog's licence. Check every shipped dependency,
  model, tool and asset, and the intended distribution channel.
- Reading upstream code does not automatically make every independent rewrite
  a derivative; conversely, a fresh AI session or saved prompts do not prove
  independence. A qualified lawyer should evaluate any clean-room protocol.
- Prior costs (including EUR 50-120k / 4-6 months for an AI-assisted rewrite)
  were **unvalidated planning estimates**, not quotes. Lines of code and commit
  counts do not establish delivery effort, authorship or production readiness.

No legal opinion, licence agreement, proprietary redistribution approval or
validated clean-room assessment has been obtained.

## Actual implementation and historical verification

Snapshot before this memory change: branch `work/dev-fork`,
HEAD `60bfd21e`, then aligned with `origin/work/dev-fork`. This is historical,
not an instruction to reset the branch or discard subsequent changes.

| Commit | Result |
| --- | --- |
| `252506a0` | Published-copy hash checks retry even when refused files retain their size/mtime |
| `d1538465` | Additional Whisper model search locations; Linux mount-root defaults in the remap dialog |
| `5a563f26` | Recorded D45 acceptance |
| `272e1274` | Updated D43/D44 implementation-status cells and reported remaining drift |
| `60bfd21e` | Added API draft and clean-room plan-B notes |

Prior assistant runs on 2026-10-31 reported:

- A full macOS K3 build succeeded. KF6 include directories existed; earlier
  CMake "non-existent include path" failures disappeared on regeneration.
  Their underlying cause was **not established**; do not assume missing headers
  or diagnose sandbox behaviour without fresh evidence.
- An **offscreen** app run with an isolated synthetic published collection
  opened read-only, did not create lock/WAL/SHM files in that folder, refused
  a hash-mismatched newer snapshot, then reloaded after corrected bytes arrived
  with unchanged size and mtime.
- Those results do **not** verify all UI behaviour, page retention visually,
  Linux or Windows runtime, encrypted layers, Hosted support or API operation.
  Temporary fixtures/logs were removed; repeat focused tests if needed.

Status recorded in the D43/D44 implementation report:

- P1/P2a/P3/P4 have code, with partial requirements and drift still present.
- Derived-root remap (`MDI-F208`) is not built; device-path remap is different.
- P2b encryption/keychain primitives exist but are not wired into complete
  protected-layer publishing/unlocking. Do not claim encrypted sharing works.
- P2c reader packaging and P5 Hosted AI support are not built.
- API server, OpenAPI implementation, licence-header cleanup and standalone
  tools are not built.

## Findings awaiting resolution - not fixed by the discussion

The specifications agent recorded these in the D43/D44 status report. They
need current-code confirmation and the gate before changes:

1. A foreign-machine collection lock may be reclaimed silently after five
   minutes (`MDI-C62`).
2. `mdi_data.db` may open read-write for lock/writer-list read-only causes;
   the published-copy path is protected (`MDI-C64`).
3. Shared CLI changes affect K2 (lock/help), despite the K3-only constraint.
4. Read-only opening skips a needed migration instead of refusing the open.
5. CLI exit codes differ from the approved contract.
6. Automatic remap suggestions/auto-publish switch and some Q74/Q75 behaviours
   were implemented without matching ratified requirements.
7. Memory-mode "Publish now" needs verification while operations are running.

The assistant proposed fixes for 1-3 and amendments for other items. The owner
has **not separately accepted that resolution package**. D45 acceptance and
the later "ok" concerned the Hosted design/status recording, not blanket
approval of these changes. Do not expose a network API assuming they are fixed.

## Technical orientation

- `core/` is currently the shared C++ business library; `qt_widgets/` is K2;
  `qt_quick/` is K3/Teca. No code has been moved to external tools yet.
- The media-intelligence code still uses upstream classes, collection queries,
  settings and lifecycle integration. The earlier "16 of 20 modules independent"
  and "five contact points" were a limited include/query scan, **not a dependency,
  provenance or licensing audit**, nor a reliable extraction estimate.
- Preserve File/Memory parity. Memory-mode catalog data requires loading from
  index files before querying. Do not introduce a second writer for existing
  stores or change shipped migrations in place.
- Current K3 CMake source says application version `3.1`; the approved core
  collection schema remains `3.0`. Do not confuse executable and schema versions.
- macOS build recipe used previously:

  ```sh
  cmake -S . -B qt_quick/build/Debug -GNinja \
    -DCMAKE_BUILD_TYPE=Debug -DBUILD_QT_QUICK=ON -DBUILD_QT_WIDGETS=OFF \
    -DCMAKE_PREFIX_PATH="$HOME/kf6;/opt/homebrew"
  cmake --build qt_quick/build/Debug --target Katalog3 -j4
  ```

  Binary: `qt_quick/build/Debug/qt_quick/Teca.app/Contents/MacOS/Teca`.
  Runtime used `QML_IMPORT_PATH="$HOME/kf6/lib/qml"` and
  `QT_PLUGIN_PATH="$HOME/kf6/lib/plugins"`. Test only isolated collections and
  settings, not the owner's personal data.
- Qt6HttpServer was not found in the inspected local installation. Do not
  generalise that to every Homebrew/CI installation or replace it with an
  unreviewed custom HTTP parser. Confirm module availability and licence first.

## First task when the owner resumes

1. Confirm they want to resume, and inspect the actual worktree.
2. Prepare **the standalone-tools specification**, not immediate extraction:
   optional core adapter, standalone input, owned store, external API/CLI,
   privacy/authentication, lifecycle/locking and compatibility boundaries.
3. Review provenance and actual dependencies before claiming modules can move
   under a different licence. Resolve legal caveats with professional advice.
4. Reconcile the tools spec with the API draft and D45. List what needs approval;
   obtain the specifications verdict before creating application code or repos.
5. Address the verified safety blockers before exposing services; implement and
   test extraction incrementally only after its approved design listing.

Keep this file as the single shared handoff. When updating it, distinguish
owner decisions from suggestions, and tested outcomes from source inspection.
Do not infer commit/push permission or implementation approval from this memory.

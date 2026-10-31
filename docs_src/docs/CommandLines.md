---
version: "3.0"
---
# Command Lines
![3.0](https://img.shields.io/badge/Version-3.0-blue)

## Summary
This page describes available command line features and options that can be run from the console.
Without the need of the graphical interface, some tasks can therefore be automated.

These commands run under **Linux** and **macOS**.

![Command line help output showing available actions and options](/img/k2/commandlines_01_help.png)

## Synopsis

```bash
Katalog [ACTION] [OPTIONS] [ARGUMENTS]
```

### Running the `Katalog` command
The examples on this page use `Katalog` as the command name. How to call Katalog from a terminal depends on how it was installed:

- **Flatpak** (Flathub): put the actions and options after the application ID.
  ```bash
  flatpak run io.github.stephanecouturier.Katalog list_catalogs
  ```
  Flatpak also installs a launcher named `io.github.stephanecouturier.Katalog`, which can be called directly when its folder is in the `PATH`.
- **AppImage**: put the actions and options after the AppImage file.
  ```bash
  ./Katalog_linux_v3.0-qt6.AppImage list_catalogs
  ```
- **macOS** (Teca, portable app): the program is inside the application.
  ```bash
  /Applications/Teca.app/Contents/MacOS/Teca list_catalogs
  ```

To type `Katalog` as in the examples, define an alias, for instance in `~/.bashrc`:
```bash
alias Katalog='flatpak run io.github.stephanecouturier.Katalog'
```

The command line uses the same settings file as the window, so without `--collection` it works on the collection last opened in Katalog.

## Actions

The following actions are available as positional arguments:

### `list_catalogs`
Lists all catalogs with their ID, active state, and name.

**Usage:**
```bash
Katalog list_catalogs [OPTIONS]
```

**Example:**
```bash
Katalog list_catalogs --verbose
```

![Command line output listing all catalogs with ID, state, and name](/img/k2/commandlines_02_list.png)

### `update_catalog`
Updates a specific catalog by device ID.

**Usage:**
```bash
Katalog update_catalog <deviceID> [OPTIONS]
```

**Arguments:**
- `deviceID` - The device ID of the catalog to update

**Example:**
```bash
Katalog update_catalog 5
```
![Command line output after updating a specific catalog](/img/k2/commandlines_03_catalog.png)

### `update_all_active`
Updates all active catalogs in the collection.

**Usage:**
```bash
Katalog update_all_active [OPTIONS]
```

**Example:**
```bash
Katalog update_all_active --verbose
```
![Command line output after updating all active catalogs](/img/k2/commandlines_04_all.png)

### `search`
Executes a search using the last search criteria from history, with optional overrides.

**Usage:**
```bash
Katalog search [OPTIONS]
```

**Example:**
```bash
Katalog search --text "vacation" --type image --limit 100
```

### `analyze`
Runs the AI analysis of one catalog with the analysis profile saved for it in Teca: previews, captions, transcripts, summaries and the other analyses the profile selects, in the same order as in the app. As in the app, the analysis is incremental: what is already analyzed is not done again, and a stopped analysis continues where it ended.

**Usage:**
```bash
Katalog analyze <deviceID> [OPTIONS]
```

**Arguments:**
- `deviceID` - The device ID of the catalog to analyze

**Example:**
```bash
Katalog analyze 5
```

Each step that runs prints one line: the file number, the analysis, its result and the file. A step whose program or model is missing (for example the whisper.cpp model for transcripts) is skipped, with the reason on its line. The AI steps need Ollama running on this computer: when it cannot be reached, they fail and run again at the next analysis.

The source of the catalog must be connected. A drive that is not connected is not waited for: the catalog is not analyzed, and the next run continues it.

### `analyze_all`
Runs the AI analysis of every catalog that has a saved analysis profile, one after the other, and prints a summary at the end. A catalog whose drive is not connected is skipped and counted in the summary.

**Usage:**
```bash
Katalog analyze_all [OPTIONS]
```

**Example:**
```bash
Katalog analyze_all --collection "/path/to/collection"
```

### Collection lock and stopping
`update_catalog`, `update_all_active`, `analyze` and `analyze_all` hold the collection lock while they run (the `.teca.lock` file in the collection folder). They do not run while Teca has the collection open or while another command line run uses it: they end with exit code 3 and change nothing.

Ctrl+C, or a stop sent by the system (SIGTERM), ends an analysis cleanly: the analyses done so far are kept, and the next run continues.

## General Options

### `-h, --help`
Displays help information and exits.

### `-v, --version`
Displays version information and exits.

### `-c, --collection <path>`
Specifies the path to the collection folder.

**Example:**
```bash
Katalog search --collection "/path/to/my/collection"
```

### `--verbose`
Enables verbose output for debugging and detailed information.

**Example:**
```bash
Katalog list_catalogs --verbose
```

## Search Options

These options are available when using the `search` action to override search criteria:

### `--limit <number>`
Limits the number of files to display in search results.

**Example:**
```bash
Katalog search --limit 50
```

### `--selectedDeviceID <deviceID>`
Specifies which device ID to search in.
- Default: uses settings file value
- When used with `--collection`: defaults to 0 (All devices)

**Example:**
```bash
Katalog search --selectedDeviceID 2
```

### `--text <search-term>`
Specifies the search text or phrase to look for.

**Example:**
```bash
Katalog search --text "family photos"
```

### `--type <file-type>`
Filters results by file type.

**Valid values:**
- `all` (default)
- `audio`
- `image`
- `text`
- `video`

**Example:**
```bash
Katalog search --type audio
```

### `--size-min <size>`
Sets minimum file size filter.

**Format:** Number followed by unit (e.g., 1MB, 5GB)

**Example:**
```bash
Katalog search --size-min 1MB
```

### `--size-max <size>`
Sets maximum file size filter.

**Format:** Number followed by unit (e.g., 100MB, 2GB)

**Example:**
```bash
Katalog search --size-max 100MB
```

### `--date-after <date>`
Filters files modified after the specified date.

**Format:** YYYY-MM-DD

**Example:**
```bash
Katalog search --date-after 2023-01-01
```

### `--date-before <date>`
Filters files modified before the specified date.

**Format:** YYYY-MM-DD

**Example:**
```bash
Katalog search --date-before 2023-12-31
```

### `--case-sensitive`
Enables case-sensitive text search.

**Example:**
```bash
Katalog search --text "MyFile" --case-sensitive
```

### `--search-in <scope>`
Defines the search scope for text matching.

**Valid values:**
- `filenames` (default)
- `files-and-folders`
- `folder-paths`

**Example:**
```bash
Katalog search --text "documents" --search-in folder-paths
```

### `--text-criteria <criteria>`
Specifies how the search text should be matched.

**Valid values:**
- `all-words` (default)
- `exact-phrase`
- `begins-with`
- `any-word`

**Example:**
```bash
Katalog search --text "vacation photo" --text-criteria exact-phrase
```

### `--exclude <exclude-terms>`
Excludes files containing the specified terms.

**Example:**
```bash
Katalog search --text "photo" --exclude "backup temp"
```

### `--no-history`
Starts with default search criteria instead of loading from search history.

**Example:**
```bash
Katalog search --no-history --text "newfile"
```

## Exit Codes

- **0**: Success
- **1**: Error or failure (invalid option or action, collection not found, catalog not updated or not analyzed)
- **2**: Analysis done, but some steps failed (for example Ollama could not be reached); they run again next time
- **3**: The collection is in use by Teca or by another command line run; nothing was done
- **4**: This computer may not write to the collection; nothing was done
- **130** / **143**: Analysis stopped by Ctrl+C (SIGINT) or by SIGTERM; the analyses done so far are kept
- **-1**: Internal code to continue to GUI mode (not returned to user)

## Examples

### Basic Catalog Management

List all catalogs:
```bash
Katalog list_catalogs
```

Update a specific catalog with verbose output:
```bash
Katalog update_catalog 3 --verbose
```

Update all active catalogs:
```bash
Katalog update_all_active
```

### Search Examples

Simple text search:
```bash
Katalog search --text "vacation"
```

Search for images larger than 5MB:
```bash
Katalog search --type image --size-min 5MB
```

Search for files modified in 2023:
```bash
Katalog search --date-after 2023-01-01 --date-before 2023-12-31
```

Complex search with multiple criteria:
```bash
Katalog search --text "project" --type text --search-in files-and-folders --case-sensitive --limit 200
```

Search in a specific device:
```bash
Katalog search --selectedDeviceID 2 --text "documents" --verbose
```

### Using Custom Collection Path

Search in a different collection:
```bash
Katalog --collection "/path/to/collection" search --text "photos"
```

Update catalogs in a specific collection:
```bash
Katalog --collection "/path/to/collection" update_all_active
```

## Running on a schedule (macOS)
A Mac that stays on can keep the catalogs updated and analyzed by itself, for instance every night, so that other Macs receive an up-to-date copy. Teca installs no scheduler: macOS runs the commands with **launchd**.

This file, saved as `~/Library/LaunchAgents/local.teca.nightly.plist`, updates the active catalogs and then analyzes every catalog with a saved profile, every night at 02:30. `USERNAME` stands for the macOS user name.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>local.teca.nightly</string>
    <key>ProgramArguments</key>
    <array>
        <string>/bin/sh</string>
        <string>-c</string>
        <string>/Applications/Teca.app/Contents/MacOS/Teca update_all_active; /Applications/Teca.app/Contents/MacOS/Teca analyze_all</string>
    </array>
    <key>StartCalendarInterval</key>
    <dict>
        <key>Hour</key>
        <integer>2</integer>
        <key>Minute</key>
        <integer>30</integer>
    </dict>
    <key>StandardOutPath</key>
    <string>/Users/USERNAME/Library/Logs/teca-nightly.log</string>
    <key>StandardErrorPath</key>
    <string>/Users/USERNAME/Library/Logs/teca-nightly.log</string>
</dict>
</plist>
```

It is loaded once with:
```bash
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/local.teca.nightly.plist
```

A run can be started at once, to check it, with:
```bash
launchctl kickstart gui/$(id -u)/local.teca.nightly
```

Good to know:
- Without `--collection`, the commands use the collection last opened in Teca. With `--collection "/path/to/collection"` after each action, they use that one.
- The `;` between the two commands runs the analysis even when an update failed.
- The Mac must be on and its user logged in: a LaunchAgent runs in the user's session. Ollama must be running for the AI steps.
- While Teca has the collection open, both commands end with exit code 3 and change nothing.
- A scheduled run cannot answer the macOS questions about access to folders and external drives. When the catalogs are on external drives or in protected folders, Teca needs that access beforehand, in **System Settings > Privacy & Security** (Full Disk Access, or Files and Folders).
- Every run writes its lines and summary to the log file; the exit codes are listed above.

## Notes

- When no action is specified, Katalog launches in GUI mode
- Search criteria are loaded from search history by default, unless `--no-history` is used
- Command line options override values from search history
- All search operations respect the active state of catalogs
- File size units supported: KB, MB, GB, TB (case-insensitive)
- Date formats must be in YYYY-MM-DD format

## Troubleshooting

**Invalid device ID:**
Ensure the device ID exists by running `Katalog list_catalogs` first.

**Database connection issues:**
Verify the collection path is correct and accessible.

**Search returns no results:**
Try using `--no-history` to start with default criteria, or check if the selected device contains indexed files.

**Permission errors:**
Ensure Katalog has read/write access to the collection folder and database files.

## Development
Some ideas of developments for this screen:
* Add a `--report` option to display a detailed update report after catalog operations
* Add a `--csv` option to export search results to a timestamped CSV file in the collection folder
* For more, see the backlog of [CommandLines development](https://github.com/StephaneCouturier/Katalog/issues?q=commandlines).

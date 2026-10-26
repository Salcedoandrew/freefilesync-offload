<div align="center">
  <img src="assets/banner.svg" width="100%" alt="">
</div>

<h1 align="center">FreeFileSync + Offload</h1>

<p align="center">A fork of FreeFileSync v14.12 that copies a camera card to several drives at once, hashes every byte, reads each copy back, and leaves a report you can check a year later.</p>

<p align="center">
  <a href="https://github.com/Salcedoandrew/freefilesync-offload/releases"><img alt="latest release" src="https://img.shields.io/github/v/release/Salcedoandrew/freefilesync-offload?style=flat-square&labelColor=0B0C0E&color=D8A24A&cache=20261026"></a>
  <img alt="macOS 13+, Apple silicon" src="https://img.shields.io/badge/macOS_13%2B-arm64-0B0C0E?style=flat-square&logo=apple&logoColor=C8C4B8">
  <img alt="107 engine assertions" src="https://img.shields.io/badge/tests-107%20assertions-0B0C0E?style=flat-square">
  <img alt="licence GPLv3" src="https://img.shields.io/badge/licence-GPLv3-0B0C0E?style=flat-square">
</p>

## Download

`Offload-0.10.2-macos-arm64.zip` — 8.3 MB, 22.8 MB unzipped. The app carries its own libraries,
so the machine that runs it needs no Homebrew, wxWidgets or Xcode.

```bash
curl -LO https://github.com/Salcedoandrew/freefilesync-offload/releases/download/v0.10.2/Offload-0.10.2-macos-arm64.zip
curl -LO https://github.com/Salcedoandrew/freefilesync-offload/releases/download/v0.10.2/SHA256SUMS.txt
shasum -a 256 -c SHA256SUMS.txt    # the updater verifies the same file before it replaces the app
ditto -x -k Offload-0.10.2-macos-arm64.zip .
open Offload.app
```

> [!IMPORTANT]
> The build is ad-hoc signed, not notarized. `codesign --verify` passes, but Gatekeeper
> still asks once for a downloaded copy: right-click → **Open** → **Open**, or
> `xattr -d com.apple.quarantine Offload.app`. Notarizing needs an Apple Developer ID,
> which this build does not have. Intel Macs are not supported.

## The problem

A wedding card holds 92.8 GB and 1,284 files, and it has to exist on two drives before the
card is reformatted. Finder copies show a bar and nothing else: no proof the bytes that
landed are the bytes that left, no record of what was copied, and a drive that fills up
halfway through leaves a half-written destination that looks finished.

Offload copies to every destination at once, verifies each one by reading it back, and
writes the record into the destination itself.

## What it does

| You want | It does | How you can check |
|---|---|---|
| Two drives at once | One engine per destination, sharing the source hash | `--self-test CARD D1 D2` prints a verified count per destination |
| Proof, not a progress bar | XXH64 while writing, then every destination read back and compared | `report.csv` and `.ffsoffload.mhl` in each destination root |
| A refusal instead of a half copy | Source measured by the engine's own walk, destination free space checked before the first byte | `Backup 02 is 58.8 GB short` and **Add Transfer** disabled (screenshot 03) |
| A receipt when it finishes | A confirmation window with the file count, the bytes moved and the rate each destination actually sustained | the window itself, and the same numbers in `report.csv` |
| A card that is never modified | Files opened read-only, published through a temp file and rename, identical files skipped | source tree hashes are identical before and after a run |
| Only the files you want | **Skip** patterns (`*.LRV \| MISC/`) applied by the same walk as the fit check, so the count on screen is the count that gets copied | the window's Skip field, or `--exclude` |
| The folder you want, named | **Subfolder** takes `%Date%`, `%Year%`, `%Month%`, `%Day%` and `%Card%`; blank keeps the card's own name | the destination after a run, and the row's own label |
| Staying current | **Check for Updates…** verifies the download against the release checksum, then replaces the app | `--update-test`, `--update-selftest <feed>` |

## Build from source

Needs macOS 13+, Homebrew LLVM (`brew install llvm`), `glib`/`gettext`, and wxWidgets 3.3 —
the engine's `zen` layer uses `wxString` and its translation shim.

**Frame 01 — the engine**

```bash
cd FreeFileSync/Source
make -f Makefile.macos liboffloadapi.a ffsoffload ffsoffload_tests
```

**Frame 02 — the app, or just the archive**

```bash
cd offload/mac
make            # Offload.app, ad-hoc signed
make bundle     # wx/glib/gettext/libc++ copied into Contents/Frameworks
make zip        # Offload-<version>-macos-arm64.zip + its checksum
```

**Frame 03 — run it without the GUI**

```bash
./ffsoffload /Volumes/CARD /Volumes/Backup1 /Volumes/Backup2 \
             --report ~/Desktop/offload-report --mhl

# leave the proxies and the camera's own folders behind, and take a JSON summary
./ffsoffload /Volumes/CARD /Volumes/Backup1 --exclude "*.LRV | *.THM | MISC/" --json
```

## Updates

**Offload menu → Check for Updates…** asks the release feed, and if there is something
newer it replaces this copy of the app itself — no Sparkle, no helper daemon, no admin
prompt.

The order is deliberate: check, download, verify, *then* touch the installed app.

1. `releases/latest` from GitHub, compared as numbers (`0.10.0` is newer than `0.9.0`).
2. The zip is downloaded and its SHA-256 compared against the release's `SHA256SUMS.txt`.
   A single byte of difference stops the update there.
3. The archive is unpacked, and the bundle inside must have the same bundle id, the exact
   version in the tag, and a code signature that passes `codesign --verify --deep --strict`.
4. Only then a small script takes over: it waits for the app to quit, moves the old bundle
   aside, moves the new one in, re-verifies it, deletes the old bundle and reopens the app.
   Any failure puts the old bundle back and reopens that instead, and every attempt leaves
   one line in `~/Library/Application Support/Offload/update/last-swap.log`.

It refuses to run while transfers are in flight, and refuses to run at all from Gatekeeper's
read-only translocated copy (open the app from `/Applications` instead).

> [!WARNING]
> This verifies **integrity, not authorship**. The build is ad-hoc signed, so there is no
> Developer ID to check the download against: a compromised release would still pass every
> check above. Fixing that means notarization or a signed appcast, and both need an Apple
> Developer Program membership.

## Contact sheet

<table>
  <tr>
    <td width="50%"><img src="docs/offload/screenshots/offload-window.png" alt="three transfers, one completed and two copying"><br><sub><b>01</b> One card, three destinations, 536 and 582 MB/sec, one already verified</sub></td>
    <td width="50%"><img src="docs/offload/screenshots/offload-window-live.png" alt="both destinations completed and verified"><br><sub><b>02</b> The mouse test's own fixture: both destinations green, 8 files each</sub></td>
  </tr>
  <tr>
    <td colspan="2"><img src="docs/offload/screenshots/offload-window-too-small.png" alt="a destination refused for being 58.8 GB short"><br><sub><b>03</b> Refused before writing: <i>Backup 02 is 58.8 GB short</i>, and Add Transfer stays disabled</sub></td>
  </tr>
</table>

## How it is built

```text
FreeFileSync/Source/
├── offload/          the engine: copy, hash, verify, report, C ABI
│   ├── engine.cpp    walk the source, fan out to destinations, verify
│   ├── hash.cpp      XXH64, MD5, SHA-1, checked against xxhsum and shasum
│   ├── capi.cpp      the C surface the window speaks to
│   └── mac/          Offload.app: SwiftUI window, dylib bundler, packaging
├── base/             FreeFileSync's comparison and sync (unchanged)
└── afs/              its file-system layer (unchanged apart from the port)
```

The fork adds a new target beside `base/` and calls `afs` directly, so the Offload window
never touches FreeFileSync's `ui/` — which is the layer that still does not build on macOS.
The engine is compiled against Homebrew's LLVM and wxString, which is why the shipped app
bundles 25 dylibs rather than expecting them on the target machine.

The thinking behind it, with the measurements and the refusals, is in
[docs/offload/](docs/offload):

| Document | What it covers |
|---|---|
| [plan.md](docs/offload/plan.md) | the plan, the licence question, and what is left |
| [architecture-map.md](docs/offload/architecture-map.md) | what FreeFileSync gives you, and what breaks on macOS |
| [spec-offshoot-ux.md](docs/offload/spec-offshoot-ux.md) | the workflow this follows |
| [phase0-spike-report.md](docs/offload/phase0-spike-report.md) | does the engine build and run on macOS |
| [phase1-report.md](docs/offload/phase1-report.md) | the headless engine, with numbers |
| [phase2-report.md](docs/offload/phase2-report.md) | the window, the fit check, and the mouse test |

### Cutting a release

Bump `CFBundleShortVersionString` in `offload/mac/Info.plist`, add a `## <version>`
section to `CHANGELOG.md`, and push to `main`. CI runs first; when it goes green, the
`Release` workflow builds that code on macOS, runs the engine tests, and publishes the
zip plus `SHA256SUMS.txt`. It creates the `v<version>` tag itself, on the commit that
changed the version rather than on whatever commit triggered the run, so the tag and the
archive describe the same code. Nothing is published from a red CI run, and nothing is
published when a commit landed *after* the version bump: that run fails and names the
commits it would have left out, so make the bump the last commit (or push the tag
yourself at the commit you mean to release). A `v*` tag pushed by hand still works, an already-released
version is skipped, and re-running the workflow from the Actions tab rebuilds it and
uploads over the existing assets instead of failing.

## Upstream features

What FreeFileSync 14.12 itself does, feature by feature — read out of the shipped
Linux binary by static analysis (REA + Ghidra, October 2026), so this fork can be
compared against it without re-running the analysis. The GUI was never run and the
binary was not modified. The raw notes and decompiles behind this list are kept
with the analysis, not in the repo.

The engine is a three-phase pipeline: **compare** → **plan** → **execute**.
`fff::synchronize(...)` is the entry point; `FolderPairSyncer` walks both trees and
decides each file pair's fate; `ComparisonBuffer` runs the comparison;
`DeletionHandler` deletes with progress; `fff::applyVersioningLimit` caps kept
versions. Backends are the AFS layer (local, Google Drive, FTP, SFTP) over
statically linked libcurl / libssh2 / OpenSSL.

Each feature has a stable ID. Evidence means how strong the claim is:
`decompiled` — the function was decompiled, behaviour confirmed;
`named` — the class/function name survives in the binary, body not decompiled;
`string` — only a config key or UI string was found, so the feature is inferred,
not proven.

### 1. Sync modes

| ID | Feature | What it does | Implementation | Config keys | Evidence |
|---|---|---|---|---|---|
| SYNC-01 | Two-Way sync | Syncs changes in both directions; each side can create, update and delete | `SyncVariant` enum; `sync_twoway` string (data table); `TwoWay` | `TwoWay` | string |
| SYNC-02 | Mirror sync | Makes the target an exact copy of the source (deletes extras on the target) | `SyncVariant` enum; `sync_mirror` string (data table); `Mirror` | `Mirror` | string |
| SYNC-03 | Update sync | Copies new/changed files one way only; never deletes | `SyncVariant` enum; `sync_update` string (data table) | — | string |
| SYNC-04 | Custom directions | Per-folder-pair overrides: each side's create/update/delete behaviour set individually | `CustomDirections`; `FolderPairSyncCfg`; per-pair config function | `CustomDirections`, `Create`, `Update`, `Delete`, `LeftOnly`, `LeftNewer`, `RightOnly`, `RightNewer` | decompiled |
| SYNC-05 | Multiple folder pairs | One config holds several left/right folder pairs synced in one run | `FolderPairSyncCfg` vector; `BaseFolderPair`; `FolderPairs` | `FolderPairs`, `FolderPairsMax`, `MaxFolderPairsShown` | named |
| SYNC-06 | Dependent folder pairs | Pairs whose base folders depend on each other; warned about, not silently run | Warning keys only (see SAFE-02) | `WarnDependentFolderPair`, `WarnDependentBaseFolders` | string |
| SYNC-07 | Sync operations (8-way) | Every file pair gets one operation: create, update, delete or move, on the left or right side | Sync-operation dispatcher; enums `so_create_left/right`, `so_update_left/right`, `so_delete_left/right`, `so_move_left/right` | `so_create_left`, `so_create_right`, `so_update_left`, `so_update_right`, `so_delete_left`, `so_delete_right`, `so_move_left`, `so_move_right` | decompiled |

### 2. Comparison

| ID | Feature | What it does | Implementation | Config keys | Evidence |
|---|---|---|---|---|---|
| CMP-01 | Compare by file time and size | Default comparison: size plus modification time decides whether a file differs | `CompareVariant` enum; `FileTimeTolerance` | `FileTimeTolerance`, `Compare` | string |
| CMP-02 | Compare by file content | Byte-by-byte content comparison instead of time/size | `ComparisonBuffer::compareByContent`; `categorizeFileByContent` | — | named |
| CMP-03 | File-time tolerance | Ignores modification-time differences smaller than N seconds (FAT/exFAT clock skew) | Setting key only | `FileTimeTolerance` | string |
| CMP-04 | Sync-state database | Remembers the last synced state per folder pair, so moved/renamed files and deletions can be detected across runs | `loadLastSynchronousState`, `saveLastSynchronousState`; `database` string in the dispatcher | `database` | named |
| CMP-05 | Categorise by content | After a content check, sorts a file pair into its sync category | `categorizeFileByContent` | — | named |
| CMP-06 | Parallel folder status | Reads folder status in parallel across pairs/filesystems | `getFolderStatusParallel`; `massParallelExecute` | `Threads` | named |

### 3. Moved files, deletion, versioning

| ID | Feature | What it does | Implementation | Config keys | Evidence |
|---|---|---|---|---|---|
| MOVE-01 | Detect moved files | A file moved/renamed on one side is moved (not re-copied + deleted) on the other | `DetectMovedFiles`; sync-run setup | `DetectMovedFiles` | decompiled |
| DEL-01 | Delete to recycle bin | Deleted files go to the OS recycle bin / trash instead of vanishing | `delete_recycler`; `RecycleBin`; `GdriveFileSystem::createRecyclerSession` (Drive trash); `gdriveMoveToTrash` | `delete_recycler`, `RecycleBin`, `Retain deleted and overwritten files in the recycle bin` | decompiled |
| DEL-02 | Delete to versioning folder | Deleted/overwritten files are kept as timestamped old versions | `delete_versioning`; `FileVersioner` (see VER-01) | `delete_versioning`, `VersioningFolder` | decompiled |
| DEL-03 | Delete permanently | Deleted files are removed with no recovery copy | `delete_permanently`; `Delete and overwrite files permanently` | `delete_permanently`, `Permanent` | decompiled |
| DEL-04 | Delete Items confirmation dialog | Shows the items about to be deleted and asks before deleting | "Delete Items" wxWidgets dialog constructor | `delete_recycler` | decompiled |
| DEL-05 | Delete files on one side | Deletes selected files on the left or right side only | `deleteFilesOneSide<LEFT/RIGHT>` | `DeleteLeft`, `DeleteRight` | named |
| DEL-06 | Deletion cleanup | Removes leftover temp/versioning data after a sync | `DeletionHandler::tryCleanup` | — | named |
| VER-01 | Versioning, timestamped folder | Old versions are stored in a folder named with a timestamp | `FileVersioner::revisionFolderImpl`; `TimeStamp-Folder`; format `YYYY-MM-DD hhmmss` | `VersioningFolder`, `TimeStamp-Folder`, `Versioning`, `Style` | decompiled |
| VER-02 | Versioning, timestamped file | Old versions are stored as timestamped file names instead of a folder | `FileVersioner::revisionFolderImpl`; `TimeStamp-File` | `TimeStamp-File`, `Versioning` | decompiled |
| VER-03 | Versioning limits | Caps how many old versions are kept, by count and by age | `fff::applyVersioningLimit`; `VersioningLimitFolder` set | `MinCount`, `MaxCount`, `MaxAge` | named |

### 4. Filters

| ID | Feature | What it does | Implementation | Config keys | Evidence |
|---|---|---|---|---|---|
| FILT-01 | Include / exclude filters | File-name patterns that include or exclude files from sync | `FilterConfig` (held by `ConfigDialog`); `DefaultFilter` | `Filter`, `DefaultFilter`, `Excluded` | named |
| FILT-02 | View filters (action) | In the results grid, show/hide rows by the action that will be taken (10 toggles) | `TreeView::applyActionFilter` (10 bool toggles) | `DefaultViewFilter` | named |
| FILT-03 | View filters (difference) | In the results grid, show/hide rows by difference category (8 toggles) | `TreeView::applyDifferenceFilter` (8 bool toggles) | `DefaultViewFilter` | named |
| FILT-04 | Filter conditions | Per-pair filter condition evaluated during sync setup | `Condition` (in sync-run setup) | `Condition`, `Custom` | decompiled |
| FILT-05 | Case sensitivity | Filter matching can be case-sensitive or not | Setting key only | `CaseSensitive` | string |

### 5. File handling during copy

| ID | Feature | What it does | Implementation | Config keys | Evidence |
|---|---|---|---|---|---|
| FILE-01 | Symlink handling | Symlinks are copied/compared as links, not followed and duplicated; link targets are read and compared | `getSymlinkContentImpl`; `copySymlinkForSameAfsType`; `equalSymlinkContentForSameAfsType`; `GetSymlinkTarget` task | — | named |
| FILE-02 | Copy file permissions | Preserves Unix permissions on copied files | Setting key only | `CopyFilePermissions` | string |
| FILE-03 | Copy locked files | Copies files that are open/locked by another process | Setting key only | `CopyLockedFiles` | string |
| FILE-04 | Fail-safe file copy | Copies via a temp file and renames into place, so a crash cannot leave a partial file at the final name | Setting key only (name implies temp+rename; mechanism not decompiled) | `FailSafeFileCopy` | string |
| FILE-05 | Verify copied files | Re-reads copied files and verifies them after the copy | Setting key only | `VerifyCopiedFiles` | string |
| FILE-06 | Temp-file buffer | Stages temp files during copy | `TempFileBuffer::createTempFiles` | — | named |
| FILE-07 | Copy with progress + retry | Copies report per-chunk progress and retry on failure | `FolderPairSyncer::copyFileWithCallback` (lambdas: per-chunk callback, retry path) | — | named |
| FILE-08 | Overwrite-if-exists control | Controls whether an existing target file is overwritten | Setting key only | `OverwriteIfExists` | string |

### 6. Backends / locations (AFS — AbstractFileSystem)

| ID | Feature | What it does | Implementation | Config keys | Evidence |
|---|---|---|---|---|---|
| BACK-01 | Local filesystem | Syncs local folders and mounted volumes | Native AFS backend; `removeDirectoryImpl`, `getItemTypeIfExistsImpl` | — | named |
| BACK-02 | Google Drive | Syncs with Google Drive: My Drive, shared drives, starred folders, shortcuts, trash, free-space query | `GdriveFileSystem` (full op set: `createFolderPlain`, `removeItemPlainImpl`, `copyFileForSameAfsType`, `copySymlinkForSameAfsType`, `moveAndRenameItemForSameAfsType`, `getFreeDiskSpace`, `createRecyclerSession`); `GdrivePersistentSessions` (accounts/sessions); `getSharedDrives`, `getStarredFolders`, `gdriveCreateShortcutPlain`, `gdriveMoveToTrash`, `gdriveRevokeAccess`, `gdriveRefreshAccess`, `gdriveExchangeAuthCode`, `getChangesDelta`, `getChangesCurrentToken`, `getMyDriveId`, `getGdriveUser` | — | named |
| BACK-03 | FTP | Syncs with FTP servers (upload/download, directory listing, symlinks) | `FtpFileSystem` (full op set incl. `moveAndRenameItemForSameAfsType`); `FtpDirectoryReader::execute`; `ftpFileUpload`, `ftpFileDownload`; `InputStreamFtp`, `OutputStreamFtp` | `FtpLogin` | named |
| BACK-04 | SFTP | Syncs with SFTP/SSH servers, with key-file auth and a state-machine traverser | `SftpFileSystem` (full op set incl. `getServerRealPath`, `copySymlinkForSameAfsType`); `SftpTraverser::TraverserStateMachine`; `SshSession::addSftpChannel`; `InputStreamSftp`, `OutputStreamSftp` | `SftpKeyFile` | named |
| BACK-05 | Cloud folder picker | Browse and pick cloud folders from the GUI; detect server channel limits | `CloudSetupDlg::onBrowseCloudFolder`, `onDetectServerChannelLimit`; `AbstractFolderPickerDlg` (tree navigation, expand, navigate-to-path) | — | named |
| BACK-06 | Network stack | HTTP(S) via libcurl, SSH via libssh2, TLS via OpenSSL — all statically linked | libcurl + libssh2 + OpenSSL 4.0.2; `googleHttpsRequest` | — | named |

### 7. Parallelism and performance

| ID | Feature | What it does | Implementation | Config keys | Evidence |
|---|---|---|---|---|---|
| PERF-01 | Configurable thread count | How many files are copied/compared in parallel | `Threads` setting, read in per-pair config | `Threads` | decompiled |
| PERF-02 | Mass parallel execution | Runs copy/compare work items across a thread pool, per filesystem | `massParallelExecute`; `TaskScheduler<TravContext, GetDirContent, GetItemDetails, GetSymlinkTarget>` (and `GetDirDetails`/`GetLinkTargetDetails` variants) | — | named |
| PERF-03 | Background priority | Runs the sync at low OS priority so the machine stays responsive | Setting key only | `RunWithBackgroundPriority` | string |
| PERF-04 | Five-pass folder walk | Folder-pair sync builds its work list in 5 passes | `FolderPairSyncer::getFolderLevelWorkItems` (5 lambdas = 5 passes) | — | named |

### 8. Batch mode and automation

| ID | Feature | What it does | Implementation | Config keys | Evidence |
|---|---|---|---|---|---|
| BATCH-01 | Batch mode (headless sync) | Runs a saved sync config without the GUI | `Application::runBatchMode`; `FfsBatchConfig` | — | named |
| BATCH-02 | Batch status handling | Progress/error reporting and result display for batch runs | `BatchStatusHandler` (incl. `reportError`, `showResult`); `StatusHandlerFloatingDialog`; `StatusHandlerTemporaryPanel` | — | named |
| BATCH-03 | Post-batch action | Action after a batch run finishes (e.g. do nothing) | `PostBatchAction` enum; `DoNothing` evidenced | `DoNothing` | named |
| BATCH-04 | Batch error handling | How a batch run reacts to errors (stop, ignore, …) | `BatchErrorHandling` | `ErrorWarning`, `ErrorOnly`, `Retry`, `Ignore` | named |
| BATCH-05 | RealTimeSync companion | Separate app that watches folders and triggers a sync on change | Binary `RealTimeSync_x86_64` exists in the release tree (11,983,168 bytes, stripped) | — | not-covered |

### 9. Post-sync actions, logging, notifications

| ID | Feature | What it does | Implementation | Config keys | Evidence |
|---|---|---|---|---|---|
| POST-01 | Post-sync command | Runs a shell command after the sync finishes | `PostSyncCommand`; sync-run setup | `PostSyncCommand`, `CommandHistory` | decompiled |
| POST-02 | Email notification | Sends the sync log by email when the run finishes | `EmailNotification`; `sendLogAsEmailImpl` | `EmailNotification`, `EmailHistory` | named |
| POST-03 | Sounds | Plays a sound on compare/sync finished | `Sounds`, `NotificationSound` | `Sounds`, `NotificationSound`, `CompareFinished`, `SyncFinished` | string |
| LOG-01 | Log files | Writes a log file per run, in a chosen format, to a chosen folder | `saveNewLogFile`; `LogFileFormat`; `getLogFiles` | `LogFiles`, `LogFolder`, `LogFolderHistory`, `Format` | named |
| LOG-02 | In-app log panel | Shows the run log in a grid inside the GUI | `LogPanel::onMsgGridContext` | — | named |
| LOG-03 | Process summary | Summarises the run (files created/updated/deleted, errors) for the log and the result dialog | `ProcessSummary` | `Success`, `Completion`, `Errors`, `Different` | named |

### 10. Safety, warnings, confirmations

| ID | Feature | What it does | Implementation | Config keys | Evidence |
|---|---|---|---|---|---|
| SAFE-01 | Directory locking | Locks the synced directories during a run so a second run cannot interfere | Setting key only | `LockDirectoriesDuringSync`, `WarnDirectoryLockFailed` | string |
| SAFE-02 | Pre-sync warnings | Warns before running when something looks risky: recycle bin unavailable, versioning folder inside the sync, unresolved conflicts, significant difference, not enough disk space, folders differing only by case, folder not existing, dependent folder pairs/base folders | `WarningDialogs` (passed to `fff::synchronize`); `OptionalDialogs` | `WarnRecycleBinNotAvailable`, `WarnVersioningFolderPartOfSync`, `WarnUnresolvedConflicts`, `WarnSignificantDifference`, `WarnNotEnoughDiskSpace`, `WarnFoldersDifferInCase`, `WarnFolderNotExisting`, `WarnDirectoryLockFailed`, `WarnDependentFolderPair`, `WarnDependentBaseFolders` | decompiled |
| SAFE-03 | Confirmations | Asks before swapping sides, starting a sync, saving a config, or mass-invoking a command | `Confirm*` keys | `ConfirmSwapSides`, `ConfirmStartSync`, `ConfirmSaveConfig`, `ConfirmCommandMassInvoke`, `ConfirmExternalCommandMassInvoke` | string |
| SAFE-04 | Sync-overdue notice | Flags a config whose last sync is overdue | Setting key only | `SyncOverdue` | string |
| SAFE-05 | Non-throwing progress callback | A failure in the progress/error callback cannot abort the sync | `fff::synchronize::PcbNoThrow` (local class) | — | named |

### 11. GUI — main dialog and views

| ID | Feature | What it does | Implementation | Config keys | Evidence |
|---|---|---|---|---|---|
| GUI-01 | Main dialog | The main window: folder-pair grid, overview panel, config panel, results grid | `fff::MainDialog` (handlers: `onStartSync`, `onCompare`, `onCfgGridContext`, `onGridContextRim`, `onTreeGridContext`, `onSyncSettingsContext`, `onCompSettingsContext`, `onShowFolderPairOptions`, `onViewFilterContext`, `onGlobalFilterContext`, …) | `MainDialog`, `OverviewPanel`, `ConfigPanel`, `MainWindow`, `Layout`, `Panels`, `Perspective` | named |
| GUI-02 | Results grid + tree view | Shows compared files in a grid and a folder tree, with context menus and column sorting | `GridDataTree`, `GridEventManager`, `TreeView`; `onGridLabelContextRim`, `onCfgGridLabelContext` | `SortByColumn`, `SortAscending`, `Columns`, `ColumnsLeft`, `ColumnsRight`, `CategoryView`, `ActionView`, `PathFormat`, `PathFormatLeft`, `PathFormatRight`, `GridIconSize`, `IconSize` | named |
| GUI-03 | Copy to alternate folder | Copies selected files to a different folder by hand | `copyToAlternateFolderFrom<LEFT/RIGHT>`; `ManualCopyTo` | `ManualCopyTo`, `KeepRelativePaths`, `TargetFolder` | named |
| GUI-04 | Folder picker dialog | Tree-based picker for local and cloud folders | `AbstractFolderPickerDlg` | `Browse directory` | named |
| GUI-05 | Per-pair config dialog | Edits one folder pair: sync direction, versioning, filters, deletion policy | `ConfigDialog` (takes `GlobalPairConfig`, `LocalPairConfig` list, `FilterConfig`, histories) | `ConfigPanel` | named |
| GUI-06 | Options dialog | Global options grid | `OptionsDlg` (grid cell providers, context menu) | `General`, `Appearance`, `ColorTheme`, `Language` | named |
| GUI-07 | Update check | Checks online for a newer version and shows an update dialog | `showUpdateAvailableDialog` | `LastOnlineVersion`, `LastOnlineCheck` | named |
| GUI-08 | Layout and appearance persistence | Remembers window size/position, panel layout, sash offsets, scroll positions, theme, DPI layout, icon size | Settings load/save functions | `Width`, `Height`, `Maximized`, `SashOffset`, `ScrollPos`, `DpiLayouts`, `ColorTheme`, `Light`, `Dark`, `ShowIcons`, `ShowPercentage`, `Stretch`, `Visible` | decompiled |
| GUI-09 | External apps | Opens selected files in external applications / the file manager | `ExternalApps` | `ExternalApps`, `Show in file manager`, `xdg-open` | string |

### 12. Configuration and persistence

| ID | Feature | What it does | Implementation | Config keys | Evidence |
|---|---|---|---|---|---|
| CFG-01 | XML config files | Sync configs and global settings are stored as XML and read/written by the app | `readConfig(XmlIn, GlobalConfig, int)`; `GlobalConfig`; `FfsBatchConfig` | `Configurations`, `Language`, `General` | named |
| CFG-02 | Histories | Remembers recently used folders, versioning folders, log folders, email recipients, commands and configs | History lists in settings load/save | `FolderHistory`, `FolderHistoryLeft`, `FolderHistoryRight`, `VersioningFolderHistory`, `LogFolderHistory`, `EmailHistory`, `CommandHistory`, `Configurations`, `LastSelected`, `LastUsed`, `Last` | decompiled |
| CFG-03 | Translations | UI strings are loaded from translation files | `loadTranslations` | `Language` | named |
| CFG-04 | Size caps | Caps on config sizes (e.g. max history entries, max folder pairs) | Setting key only | `MaxSize`, `MaxFolderPairsShown`, `FolderPairsMax` | string |

### Not covered in this pass

- **Inner copy/compare loop** — identified by name, not decompiled (`fff::synchronize`, `FolderPairSyncer::synchronizeFileInt`, `ComparisonBuffer::compareByContent`). Needs a vtable pointer search.
- **RealTimeSync** — binary present, not analysed (`RealTimeSync_x86_64`, 11,983,168 bytes, stripped).
- **Filter syntax details** — include/exclude pattern syntax, time-span and file-size filters were not recovered from the 8 decompiled functions.
- **Batch file format** — `FfsBatchConfig` exists; the on-disk batch format was not decompiled.
- **Comparison mode enum values** — `CompareVariant` exists; the full set of values was not recovered.
- **Post-batch action values** — only `DoNothing` was seen in the decompiled set.

### Comparing against another app

For each feature ID above, fill in:

| ID | FreeFileSync 14.12 | Target app | Verdict | Target evidence |
|---|---|---|---|---|
| SYNC-01 | Two-Way sync (`SyncVariant`) | ... | present / absent / partial / unknown | file / function / setting |

Verdict rules: `present` = same behaviour confirmed in the target's code;
`partial` = the feature exists but differs in a named way (say how);
`absent` = no equivalent found; `unknown` = not checked yet.

## What it costs

Measured on arm64 macOS 26.5, 2026-10-25 and 2026-10-26, on the packaged build where noted.

| | Cost | Measured with |
|---|---|---|
| Copy 64 MiB to one destination | 1.26 GiB/s | phase 0 `smoke_copy` |
| 191 MB card, 20 files, two destinations, hashed and read back | 0.49 s | `--self-test`, packaged app |
| Peak memory on that run | 19 MB | `/usr/bin/time -l` |
| Engine assertions | 107 pass | `./ffsoffload_tests` |
| Fit-check boundaries | 11 pass | `./Offload --fit-test` |
| Mouse checks against the real window | 12 pass | `make ui-test` |
| Update checks: versions, feed, checksums, a real bundle swap | 38 pass | `--update-test` |
| App bundle | 21 MB, 25 bundled dylibs | `du -sh Offload.app` |

Read-back verification means every byte crosses the bus roughly three times instead of
once, so throughput is bounded by the slowest destination, not by the source.

## Limits

- **No resume journal.** An interrupted job restarts that file; it does not continue mid-file.
- **The updater trusts the release it downloads.** Ad-hoc signing means the checksum proves
  the bytes arrived intact, not who published them.
- **v0.4.0 cannot update itself.** It predates this feature, so that upgrade is manual; every
  build from 0.5.0 on can.
- **The FreeFileSync GUI is not here.** This fork carries the engine and Offload only.
- **arm64 only.** The engine and its Homebrew dependencies are arm64, so an Intel build
  would need a dependency set that does not exist on the build machine.
- **Ad-hoc signed, not notarized.** Gatekeeper asks once; see the note above.
- **No real camera card or drive has been through it yet.** Every number here is a
  synthetic fixture on internal storage.
- **Nothing is upstreamed.** Upstream is a read-only mirror and does not take pull requests.

## Reel

| Version | Date | What changed |
|---|---|---|
| [v0.10.2](https://github.com/Salcedoandrew/freefilesync-offload/releases/tag/v0.10.2) | 2026-10-26 | About Offload names the author and says what the app does, instead of only its version |
| [v0.10.1](https://github.com/Salcedoandrew/freefilesync-offload/releases/tag/v0.10.1) | 2026-10-26 | Reports name the version they were written by: the engine's version comes from the bundle now, not a constant that had gone three releases stale |
| [v0.10.0](https://github.com/Salcedoandrew/freefilesync-offload/releases/tag/v0.10.0) | 2026-10-26 | Skip patterns leave the proxies and card junk behind; a Subfolder field with `%Date%` macros; a fuller `--json` summary; an app icon |
| [v0.9.0](https://github.com/Salcedoandrew/freefilesync-offload/releases/tag/v0.9.0) | 2026-10-26 | Clear Finished in the Transfers header; failed rows stay behind to be read |
| [v0.8.1](https://github.com/Salcedoandrew/freefilesync-offload/releases/tag/v0.8.1) | 2026-10-26 | Documentation only: the release table caught up, and the changelog and table dates now agree |
| [v0.8.0](https://github.com/Salcedoandrew/freefilesync-offload/releases/tag/v0.8.0) | 2026-10-26 | Add queues and Transfer starts; a receipt when a run ends; the download's note names its own version |
| [v0.7.0](https://github.com/Salcedoandrew/freefilesync-offload/releases/tag/v0.7.0) | 2026-10-25 | A damaged photo fails the run; the updater refuses a release it cannot verify; per-row speeds and pause |
| [v0.6.0](https://github.com/Salcedoandrew/freefilesync-offload/releases/tag/v0.6.0) | 2026-10-25 | A hash cache, so a second run does not re-hash the card; broken files flagged rather than filed |
| [v0.5.0](https://github.com/Salcedoandrew/freefilesync-offload/releases/tag/v0.5.0) | 2026-10-25 | Check for Updates, and an update that replaces the app in place |
| [v0.4.0](https://github.com/Salcedoandrew/freefilesync-offload/releases/tag/v0.4.0) | 2026-10-25 | Packaged, self-contained, signed app on the release; mouse-driven UI tests |
| [v0.3.1](https://github.com/Salcedoandrew/freefilesync-offload/releases) | 2026-10-25 | Destination fit check: refuses a drive that cannot hold the card |
| [v0.3.0](https://github.com/Salcedoandrew/freefilesync-offload/releases) | 2026-10-25 | The window, the C ABI, CSV/MHL reports, the `.app` bundle |
| [v0.2.1](https://github.com/Salcedoandrew/freefilesync-offload/releases) | 2026-10-25 | `--hash` selection, pause/resume, a stale-object crash fix |
| [v0.2.0](https://github.com/Salcedoandrew/freefilesync-offload/releases) | 2026-10-25 | The headless engine: 107 assertions, 221 files verified end to end |
| [v0.1.0](https://github.com/Salcedoandrew/freefilesync-offload/releases) | 2026-10-25 | The macOS port: 125 lines, all inside `__APPLE__` |

Dates are the day each release went out, in America/Los_Angeles, the same convention as
the changelog. GitHub stamps releases in UTC, so an evening release shows there on the
following day.

## Licence

[GPLv3](License.txt) — and upstream's own terms, which are narrower than the GPL. Both are
recorded in [NOTICE.md](NOTICE.md) without pretending the conflict is resolved. This is a
modified fork of [FreeFileSync](https://freefilesync.org) v14.12 (upstream commit
`53143e29`), not affiliated with, endorsed by, or distributed by its authors.

<div align="center">
  <img src="assets/rule-filmstrip.svg" width="100%" alt="">
  <sub>Salcedo Andrew · built for the 2 a.m. card dump · <a href="#top">back to the top</a></sub>
</div>

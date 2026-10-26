# FreeFileSync 14.12 — Feature Inventory for AI Agents

> **Purpose:** a machine-readable inventory of FreeFileSync 14.12's features, derived
> from static reverse engineering, so an AI agent can compare this app's features
> against another app (e.g. a fork, a competitor, or a reimplementation plan) without
> re-running the analysis.
>
> **Target:** `FreeFileSync_14.12_Linux_x86_64` — ELF 64-bit x86-64, stripped,
> 25,923,184 bytes, entry point `0x5e0181`, GCC 16.2.0, wxWidgets/GTK3 build.
> **Method:** REA CLI 6.8.0 + Ghidra 12.1.4 headless, static analysis only.
> The GUI was never run and the binary was not modified.
> **Date:** 2026-10-25/26. **Analyst notes:** `REA-NOTES.md` in this folder.
>
> **How to use this file (for an agent doing a comparison):**
> 1. Each feature has a stable ID (e.g. `SYNC-01`). Reference features by ID.
> 2. For each ID, compare the `What it does` and `Implementation` columns against
>    the target app. Mark each: `present`, `absent`, `partial`, or `unknown`,
>    and record the target's evidence (file/function/setting) next to it.
> 3. `Evidence` tells you how strong the FreeFileSync side of the claim is:
>    - `decompiled` — the function was decompiled; behaviour confirmed in pseudocode.
>    - `named` — the class/function name survives in the binary's typeinfo; the
>      feature exists and its signature is known, but the body was not decompiled.
>    - `string` — only a config key or UI string was found; the feature is inferred
>      from the name plus context. Treat as likely, not proven.
>    - `not-covered` — known to exist but not analysed in this pass.
> 4. Do not treat `string`-level rows as proof of exact behaviour — only of existence.
> 5. `Ref` points at the raw evidence in this folder (`REA-NOTES.md`,
>    `ghidra-decompiles2.txt`, `ghidra-findings2.txt`, `demangled-names.txt`).

## Architecture in one paragraph

The sync engine is a three-phase pipeline: **compare** → **plan** → **execute**.
`fff::synchronize(...)` is the top-level entry (takes a start time, 5 booleans, the
folder-pair configs, a filesystems map, a `WarningDialogs&`, and a
`ProcessCallback&`; a local `PcbNoThrow` wrapper keeps callback failures from
aborting the sync). Per folder pair, `FolderPairSyncer` walks both trees
(`getFolderLevelWorkItems`, 5 passes) and decides each file pair's fate
(`synchronizeFileInt<LEFT/RIGHT>`). `ComparisonBuffer` runs the comparison
(`execute`, `compareByContent`, `categorizeFileByContent`). `DeletionHandler`
deletes with progress (`removeFileWithCallback`, `removeDirWithCallback`,
`tryCleanup`). `fff::applyVersioningLimit` caps kept versions. Backends are the
AFS layer (`GdriveFileSystem`, `FtpFileSystem`, `SftpFileSystem`, plus the native
local filesystem) over statically linked libcurl / libssh2 / OpenSSL 4.0.2.
The GUI (wxWidgets/GTK3) sits on top; the engine source files are
`algorithm.cpp`, `comparison.cpp`, `file_hierarchy.cpp`, `synchronization.cpp`,
`versioning.h` (embedded as `__FILE__` strings).

**Known gap:** the inner copy/compare loop functions (`fff::synchronize`,
`FolderPairSyncer::synchronizeFileInt`, `ComparisonBuffer::compareByContent`)
were identified by name but NOT decompiled — Ghidra's reference database has no
data→data references for typeinfo/vtable pointers. Remaining step: a vtable
pointer search (see `REA-NOTES.md` §7). The persistent Ghidra project is saved
for that work.

## 1. Sync modes

| ID | Feature | What it does | Implementation | Config keys | Evidence | Ref |
|---|---|---|---|---|---|---|
| SYNC-01 | Two-Way sync | Syncs changes in both directions; each side can create, update and delete | `SyncVariant` enum; `sync_twoway` string (data table); `TwoWay` | `TwoWay` | string | findings2.txt; decompiles2.txt |
| SYNC-02 | Mirror sync | Makes the target an exact copy of the source (deletes extras on the target) | `SyncVariant` enum; `sync_mirror` string (data table); `Mirror` | `Mirror` | string | findings2.txt; decompiles2.txt |
| SYNC-03 | Update sync | Copies new/changed files one way only; never deletes | `SyncVariant` enum; `sync_update` string (data table) | — | string | findings2.txt |
| SYNC-04 | Custom directions | Per-folder-pair overrides: each side's create/update/delete behaviour set individually | `CustomDirections`; `FolderPairSyncCfg`; `FUN_00622050` (per-pair config) | `CustomDirections`, `Create`, `Update`, `Delete`, `LeftOnly`, `LeftNewer`, `RightOnly`, `RightNewer` | decompiled | decompiles2.txt `FUN_00622050` |
| SYNC-05 | Multiple folder pairs | One config holds several left/right folder pairs synced in one run | `FolderPairSyncCfg` vector; `BaseFolderPair`; `FolderPairs` | `FolderPairs`, `FolderPairsMax`, `MaxFolderPairsShown` | named | demangled-names.txt; decompiles2.txt |
| SYNC-06 | Dependent folder pairs | Pairs whose base folders depend on each other; warned about, not silently run | Warning keys only (see SAFE-02) | `WarnDependentFolderPair`, `WarnDependentBaseFolders` | string | decompiles2.txt |
| SYNC-07 | Sync operations (8-way) | Every file pair gets one operation: create, update, delete or move, on the left or right side | Dispatcher `FUN_009da500`; enums `so_create_left/right`, `so_update_left/right`, `so_delete_left/right`, `so_move_left/right` | `so_create_left`, `so_create_right`, `so_update_left`, `so_update_right`, `so_delete_left`, `so_delete_right`, `so_move_left`, `so_move_right` | decompiled | decompiles2.txt `FUN_009da500` |

## 2. Comparison

| ID | Feature | What it does | Implementation | Config keys | Evidence | Ref |
|---|---|---|---|---|---|---|
| CMP-01 | Compare by file time and size | Default comparison: size plus modification time decides whether a file differs | `CompareVariant` enum (`MainDialog::onCompSettingsContext`); `FileTimeTolerance` | `FileTimeTolerance`, `Compare` | string | demangled-names.txt; decompiles2.txt |
| CMP-02 | Compare by file content | Byte-by-byte content comparison instead of time/size | `ComparisonBuffer::compareByContent`; `categorizeFileByContent` | — | named | demangled-names.txt; REA-NOTES.md §6 |
| CMP-03 | File-time tolerance | Ignores modification-time differences smaller than N seconds (FAT/exFAT clock skew) | Setting key only | `FileTimeTolerance` | string | decompiles2.txt |
| CMP-04 | Sync-state database | Remembers the last synced state per folder pair, so moved/renamed files and deletions can be detected across runs | `loadLastSynchronousState`, `saveLastSynchronousState`; `database` string in the dispatcher | `database` | named | demangled-names.txt; decompiles2.txt `FUN_009da500` |
| CMP-05 | Categorise by content | After a content check, sorts a file pair into its sync category | `categorizeFileByContent` | — | named | demangled-names.txt |
| CMP-06 | Parallel folder status | Reads folder status in parallel across pairs/filesystems | `getFolderStatusParallel`; `massParallelExecute` | `Threads` | named | demangled-names.txt |

## 3. Moved files, deletion, versioning

| ID | Feature | What it does | Implementation | Config keys | Evidence | Ref |
|---|---|---|---|---|---|---|
| MOVE-01 | Detect moved files | A file moved/renamed on one side is moved (not re-copied + deleted) on the other | `DetectMovedFiles`; `FUN_006241e0` (sync-run setup) | `DetectMovedFiles` | decompiled | decompiles2.txt `FUN_006241e0` |
| DEL-01 | Delete to recycle bin | Deleted files go to the OS recycle bin / trash instead of vanishing | `delete_recycler`; `RecycleBin`; `GdriveFileSystem::createRecyclerSession` (Drive trash); `gdriveMoveToTrash` | `delete_recycler`, `RecycleBin`, `Retain deleted and overwritten files in the recycle bin` | decompiled | decompiles2.txt `FUN_009da500`, `FUN_009c0160` |
| DEL-02 | Delete to versioning folder | Deleted/overwritten files are kept as timestamped old versions | `delete_versioning`; `FileVersioner` (see VER-01) | `delete_versioning`, `VersioningFolder` | decompiled | decompiles2.txt `FUN_009da500` |
| DEL-03 | Delete permanently | Deleted files are removed with no recovery copy | `delete_permanently`; `Delete and overwrite files permanently` | `delete_permanently`, `Permanent` | decompiled | decompiles2.txt `FUN_009da500` |
| DEL-04 | Delete Items confirmation dialog | Shows the items about to be deleted and asks before deleting | `FUN_009c0160` (wxWidgets dialog, title `Delete Items`) | `delete_recycler` | decompiled | decompiles2.txt `FUN_009c0160` |
| DEL-05 | Delete files on one side | Deletes selected files on the left or right side only | `deleteFilesOneSide<LEFT/RIGHT>` | `DeleteLeft`, `DeleteRight` | named | demangled-names.txt |
| DEL-06 | Deletion cleanup | Removes leftover temp/versioning data after a sync | `DeletionHandler::tryCleanup` | — | named | demangled-names.txt |
| VER-01 | Versioning, timestamped folder | Old versions are stored in a folder named with a timestamp | `FileVersioner::revisionFolderImpl`; `TimeStamp-Folder`; format `YYYY-MM-DD hhmmss` | `VersioningFolder`, `TimeStamp-Folder`, `Versioning`, `Style` | decompiled | decompiles2.txt `FUN_00629ff0`, `FUN_009da500` |
| VER-02 | Versioning, timestamped file | Old versions are stored as timestamped file names instead of a folder | `FileVersioner::revisionFolderImpl`; `TimeStamp-File` | `TimeStamp-File`, `Versioning` | decompiled | decompiles2.txt `FUN_00629ff0` |
| VER-03 | Versioning limits | Caps how many old versions are kept, by count and by age | `fff::applyVersioningLimit`; `VersioningLimitFolder` set | `MinCount`, `MaxCount`, `MaxAge` | named | demangled-names.txt; decompiles2.txt `FUN_00622050` |

## 4. Filters

| ID | Feature | What it does | Implementation | Config keys | Evidence | Ref |
|---|---|---|---|---|---|---|
| FILT-01 | Include / exclude filters | File-name patterns that include or exclude files from sync | `FilterConfig` (held by `ConfigDialog`); `DefaultFilter` | `Filter`, `DefaultFilter`, `Excluded` | named | demangled-names.txt; decompiles2.txt |
| FILT-02 | View filters (action) | In the results grid, show/hide rows by the action that will be taken (10 toggles) | `TreeView::applyActionFilter` (10 bool toggles) | `DefaultViewFilter` | named | demangled-names.txt |
| FILT-03 | View filters (difference) | In the results grid, show/hide rows by difference category (8 toggles) | `TreeView::applyDifferenceFilter` (8 bool toggles) | `DefaultViewFilter` | named | demangled-names.txt |
| FILT-04 | Filter conditions | Per-pair filter condition evaluated during sync setup | `Condition` (in `FUN_006241e0` sync-run setup) | `Condition`, `Custom` | decompiled | decompiles2.txt `FUN_006241e0` |
| FILT-05 | Case sensitivity | Filter matching can be case-sensitive or not | Setting key only | `CaseSensitive` | string | decompiles2.txt |

## 5. File handling during copy

| ID | Feature | What it does | Implementation | Config keys | Evidence | Ref |
|---|---|---|---|---|---|---|
| FILE-01 | Symlink handling | Symlinks are copied/compared as links, not followed and duplicated; link targets are read and compared | `getSymlinkContentImpl`; `copySymlinkForSameAfsType`; `equalSymlinkContentForSameAfsType`; `GetSymlinkTarget` task | — | named | demangled-names.txt |
| FILE-02 | Copy file permissions | Preserves Unix permissions on copied files | Setting key only | `CopyFilePermissions` | string | decompiles2.txt |
| FILE-03 | Copy locked files | Copies files that are open/locked by another process | Setting key only | `CopyLockedFiles` | string | decompiles2.txt |
| FILE-04 | Fail-safe file copy | Copies via a temp file and renames into place, so a crash cannot leave a partial file at the final name | Setting key only (name implies temp+rename; mechanism not decompiled) | `FailSafeFileCopy` | string | decompiles2.txt |
| FILE-05 | Verify copied files | Re-reads copied files and verifies them after the copy | Setting key only | `VerifyCopiedFiles` | string | decompiles2.txt |
| FILE-06 | Temp-file buffer | Stages temp files during copy | `TempFileBuffer::createTempFiles` | — | named | demangled-names.txt |
| FILE-07 | Copy with progress + retry | Copies report per-chunk progress and retry on failure | `FolderPairSyncer::copyFileWithCallback` (lambdas: per-chunk callback, retry path) | — | named | demangled-names.txt; REA-NOTES.md §6 |
| FILE-08 | Overwrite-if-exists control | Controls whether an existing target file is overwritten | Setting key only | `OverwriteIfExists` | string | decompiles2.txt |

## 6. Backends / locations (AFS — AbstractFileSystem)

| ID | Feature | What it does | Implementation | Config keys | Evidence | Ref |
|---|---|---|---|---|---|---|
| BACK-01 | Local filesystem | Syncs local folders and mounted volumes | Native AFS backend; `removeDirectoryImpl`, `getItemTypeIfExistsImpl` | — | named | demangled-names.txt |
| BACK-02 | Google Drive | Syncs with Google Drive: My Drive, shared drives, starred folders, shortcuts, trash, free-space query | `GdriveFileSystem` (full op set: `createFolderPlain`, `removeItemPlainImpl`, `copyFileForSameAfsType`, `copySymlinkForSameAfsType`, `moveAndRenameItemForSameAfsType`, `getFreeDiskSpace`, `createRecyclerSession`); `GdrivePersistentSessions` (accounts/sessions); `getSharedDrives`, `getStarredFolders`, `gdriveCreateShortcutPlain`, `gdriveMoveToTrash`, `gdriveRevokeAccess`, `gdriveRefreshAccess`, `gdriveExchangeAuthCode`, `getChangesDelta`, `getChangesCurrentToken`, `getMyDriveId`, `getGdriveUser` | `SftpKeyFile` n/a (Drive uses OAuth); `LastOnlineVersion` | named | demangled-names.txt |
| BACK-03 | FTP | Syncs with FTP servers (upload/download, directory listing, symlinks) | `FtpFileSystem` (full op set incl. `moveAndRenameItemForSameAfsType`); `FtpDirectoryReader::execute`; `ftpFileUpload`, `ftpFileDownload`; `InputStreamFtp`, `OutputStreamFtp` | `FtpLogin` | named | demangled-names.txt |
| BACK-04 | SFTP | Syncs with SFTP/SSH servers, with key-file auth and a state-machine traverser | `SftpFileSystem` (full op set incl. `getServerRealPath`, `copySymlinkForSameAfsType`); `SftpTraverser::TraverserStateMachine`; `SshSession::addSftpChannel`; `InputStreamSftp`, `OutputStreamSftp` | `SftpKeyFile` | named | demangled-names.txt |
| BACK-05 | Cloud folder picker | Browse and pick cloud folders from the GUI; detect server channel limits | `CloudSetupDlg::onBrowseCloudFolder`, `onDetectServerChannelLimit`; `AbstractFolderPickerDlg` (tree navigation, expand, navigate-to-path) | — | named | demangled-names.txt |
| BACK-06 | Network stack | HTTP(S) via libcurl, SSH via libssh2, TLS via OpenSSL — all statically linked | libcurl + libssh2 + OpenSSL 4.0.2 (build paths `/mnt/hgfs/Projects/libcurl/...`, `/mnt/hgfs/Projects/libssh2/...`); `googleHttpsRequest` | — | named | REA-NOTES.md §1, §6; demangled-names.txt |

## 7. Parallelism and performance

| ID | Feature | What it does | Implementation | Config keys | Evidence | Ref |
|---|---|---|---|---|---|---|
| PERF-01 | Configurable thread count | How many files are copied/compared in parallel | `Threads` setting, read in per-pair config `FUN_00622050` | `Threads` | decompiled | decompiles2.txt `FUN_00622050` |
| PERF-02 | Mass parallel execution | Runs copy/compare work items across a thread pool, per filesystem | `massParallelExecute`; `TaskScheduler<TravContext, GetDirContent, GetItemDetails, GetSymlinkTarget>` (and `GetDirDetails`/`GetLinkTargetDetails` variants) | — | named | demangled-names.txt |
| PERF-03 | Background priority | Runs the sync at low OS priority so the machine stays responsive | Setting key only | `RunWithBackgroundPriority` | string | decompiles2.txt |
| PERF-04 | Five-pass folder walk | Folder-pair sync builds its work list in 5 passes | `FolderPairSyncer::getFolderLevelWorkItems` (5 lambdas = 5 passes) | — | named | demangled-names.txt; REA-NOTES.md §6 |

## 8. Batch mode and automation

| ID | Feature | What it does | Implementation | Config keys | Evidence | Ref |
|---|---|---|---|---|---|---|
| BATCH-01 | Batch mode (headless sync) | Runs a saved sync config without the GUI | `Application::runBatchMode`; `FfsBatchConfig` | — | named | demangled-names.txt |
| BATCH-02 | Batch status handling | Progress/error reporting and result display for batch runs | `BatchStatusHandler` (incl. `reportError`, `showResult`); `StatusHandlerFloatingDialog`; `StatusHandlerTemporaryPanel` | — | named | demangled-names.txt |
| BATCH-03 | Post-batch action | Action after a batch run finishes (e.g. do nothing) | `PostBatchAction` enum; `DoNothing` evidenced | `DoNothing` | named | demangled-names.txt; decompiles2.txt |
| BATCH-04 | Batch error handling | How a batch run reacts to errors (stop, ignore, …) | `BatchErrorHandling` | `ErrorWarning`, `ErrorOnly`, `Retry`, `Ignore` | named | demangled-names.txt; decompiles2.txt |
| BATCH-05 | RealTimeSync companion | Separate app that watches folders and triggers a sync on change | Binary `RealTimeSync_x86_64` exists in the release tree (11,983,168 bytes, stripped) | — | not-covered | REA-NOTES.md §7 (not analysed) |

## 9. Post-sync actions, logging, notifications

| ID | Feature | What it does | Implementation | Config keys | Evidence | Ref |
|---|---|---|---|---|---|---|
| POST-01 | Post-sync command | Runs a shell command after the sync finishes | `PostSyncCommand`; `FUN_006241e0` (sync-run setup) | `PostSyncCommand`, `CommandHistory` | decompiled | decompiles2.txt `FUN_006241e0` |
| POST-02 | Email notification | Sends the sync log by email when the run finishes | `EmailNotification`; `sendLogAsEmailImpl` | `EmailNotification`, `EmailHistory` | named | demangled-names.txt; decompiles2.txt `FUN_006241e0` |
| POST-03 | Sounds | Plays a sound on compare/sync finished | `Sounds`, `NotificationSound` | `Sounds`, `NotificationSound`, `CompareFinished`, `SyncFinished` | string | decompiles2.txt |
| LOG-01 | Log files | Writes a log file per run, in a chosen format, to a chosen folder | `saveNewLogFile`; `LogFileFormat`; `getLogFiles` | `LogFiles`, `LogFolder`, `LogFolderHistory`, `Format` | named | demangled-names.txt; decompiles2.txt |
| LOG-02 | In-app log panel | Shows the run log in a grid inside the GUI | `LogPanel::onMsgGridContext` | — | named | demangled-names.txt |
| LOG-03 | Process summary | Summarises the run (files created/updated/deleted, errors) for the log and the result dialog | `ProcessSummary` | `Success`, `Completion`, `Errors`, `Different` | named | demangled-names.txt; decompiles2.txt |

## 10. Safety, warnings, confirmations

| ID | Feature | What it does | Implementation | Config keys | Evidence | Ref |
|---|---|---|---|---|---|---|
| SAFE-01 | Directory locking | Locks the synced directories during a run so a second run cannot interfere | Setting key only | `LockDirectoriesDuringSync`, `WarnDirectoryLockFailed` | string | decompiles2.txt |
| SAFE-02 | Pre-sync warnings | Warns before running when something looks risky: recycle bin unavailable, versioning folder inside the sync, unresolved conflicts, significant difference, not enough disk space, folders differing only by case, folder not existing, dependent folder pairs/base folders | `WarningDialogs` (passed to `fff::synchronize`); `OptionalDialogs` | `WarnRecycleBinNotAvailable`, `WarnVersioningFolderPartOfSync`, `WarnUnresolvedConflicts`, `WarnSignificantDifference`, `WarnNotEnoughDiskSpace`, `WarnFoldersDifferInCase`, `WarnFolderNotExisting`, `WarnDirectoryLockFailed`, `WarnDependentFolderPair`, `WarnDependentBaseFolders` | decompiled | decompiles2.txt `FUN_0061a2b0`, `FUN_00636110` |
| SAFE-03 | Confirmations | Asks before swapping sides, starting a sync, saving a config, or mass-invoking a command | `Confirm*` keys | `ConfirmSwapSides`, `ConfirmStartSync`, `ConfirmSaveConfig`, `ConfirmCommandMassInvoke`, `ConfirmExternalCommandMassInvoke` | string | decompiles2.txt |
| SAFE-04 | Sync-overdue notice | Flags a config whose last sync is overdue | Setting key only | `SyncOverdue` | string | decompiles2.txt |
| SAFE-05 | Non-throwing progress callback | A failure in the progress/error callback cannot abort the sync | `fff::synchronize::PcbNoThrow` (local class) | — | named | demangled-names.txt; REA-NOTES.md §6 |

## 11. GUI — main dialog and views

| ID | Feature | What it does | Implementation | Config keys | Evidence | Ref |
|---|---|---|---|---|---|---|
| GUI-01 | Main dialog | The main window: folder-pair grid, overview panel, config panel, results grid | `fff::MainDialog` (handlers: `onStartSync`, `onCompare`, `onCfgGridContext`, `onGridContextRim`, `onTreeGridContext`, `onSyncSettingsContext`, `onCompSettingsContext`, `onShowFolderPairOptions`, `onViewFilterContext`, `onGlobalFilterContext`, …) | `MainDialog`, `OverviewPanel`, `ConfigPanel`, `MainWindow`, `Layout`, `Panels`, `Perspective` | named | demangled-names.txt; decompiles2.txt |
| GUI-02 | Results grid + tree view | Shows compared files in a grid and a folder tree, with context menus and column sorting | `GridDataTree`, `GridEventManager`, `TreeView`; `onGridLabelContextRim`, `onCfgGridLabelContext` | `SortByColumn`, `SortAscending`, `Columns`, `ColumnsLeft`, `ColumnsRight`, `CategoryView`, `ActionView`, `PathFormat`, `PathFormatLeft`, `PathFormatRight`, `GridIconSize`, `IconSize` | named | demangled-names.txt; decompiles2.txt |
| GUI-03 | Copy to alternate folder | Copies selected files to a different folder by hand | `copyToAlternateFolderFrom<LEFT/RIGHT>`; `ManualCopyTo` | `ManualCopyTo`, `KeepRelativePaths`, `TargetFolder` | named | demangled-names.txt; decompiles2.txt |
| GUI-04 | Folder picker dialog | Tree-based picker for local and cloud folders | `AbstractFolderPickerDlg` | `Browse directory` | named | demangled-names.txt |
| GUI-05 | Per-pair config dialog | Edits one folder pair: sync direction, versioning, filters, deletion policy | `ConfigDialog` (takes `GlobalPairConfig`, `LocalPairConfig` list, `FilterConfig`, histories) | `ConfigPanel` | named | demangled-names.txt |
| GUI-06 | Options dialog | Global options grid | `OptionsDlg` (grid cell providers, context menu) | `General`, `Appearance`, `ColorTheme`, `Language` | named | demangled-names.txt |
| GUI-07 | Update check | Checks online for a newer version and shows an update dialog | `showUpdateAvailableDialog` | `LastOnlineVersion`, `LastOnlineCheck` | named | demangled-names.txt; decompiles2.txt |
| GUI-08 | Layout and appearance persistence | Remembers window size/position, panel layout, sash offsets, scroll positions, theme, DPI layout, icon size | `FUN_0061a2b0` + `FUN_00636110` (settings load/save) | `Width`, `Height`, `Maximized`, `SashOffset`, `ScrollPos`, `DpiLayouts`, `ColorTheme`, `Light`, `Dark` (theme values `Light`/`Dark`/`Medium`/`Small`/`Large` seen as strings), `ShowIcons`, `ShowPercentage`, `Stretch`, `Visible` | decompiled | decompiles2.txt `FUN_0061a2b0`, `FUN_00636110` |
| GUI-09 | External apps | Opens selected files in external applications / the file manager | `ExternalApps` | `ExternalApps`, `Show in file manager`, `xdg-open` | string | decompiles2.txt |

## 12. Configuration and persistence

| ID | Feature | What it does | Implementation | Config keys | Evidence | Ref |
|---|---|---|---|---|---|---|
| CFG-01 | XML config files | Sync configs and global settings are stored as XML and read/written by the app | `readConfig(XmlIn, GlobalConfig, int)`; `GlobalConfig`; `FfsBatchConfig` | `Configurations`, `Language`, `General` | named | demangled-names.txt; decompiles2.txt `FUN_0061a2b0` |
| CFG-02 | Histories | Remembers recently used folders, versioning folders, log folders, email recipients, commands and configs | History lists in settings load/save | `FolderHistory`, `FolderHistoryLeft`, `FolderHistoryRight`, `VersioningFolderHistory`, `LogFolderHistory`, `EmailHistory`, `CommandHistory`, `Configurations`, `LastSelected`, `LastUsed`, `Last` | decompiled | decompiles2.txt `FUN_0061a2b0`, `FUN_00636110` |
| CFG-03 | Translations | UI strings are loaded from translation files | `loadTranslations` | `Language` | named | demangled-names.txt |
| CFG-04 | Size caps | Caps on config sizes (e.g. max history entries, max folder pairs) | Setting keys only | `MaxSize`, `MaxFolderPairsShown`, `FolderPairsMax` | string | decompiles2.txt |

## Not covered in this pass

| Area | Status | Notes |
|---|---|---|
| Inner copy/compare loop | Identified by name, not decompiled | `fff::synchronize`, `FolderPairSyncer::synchronizeFileInt<LEFT/RIGHT>`, `ComparisonBuffer::compareByContent` — needs a vtable pointer search (see `REA-NOTES.md` §7). |
| RealTimeSync | Binary present, not analysed | `RealTimeSync_x86_64` (11,983,168 bytes, stripped) was extracted but no Ghidra work was done on it. |
| Filter syntax details | Not in the decompiled set | Include/exclude pattern syntax, time-span and file-size filters were not recovered from the 8 decompiled functions. |
| Batch file format | Not recovered | `FfsBatchConfig` exists; the on-disk batch format was not decompiled. |
| Comparison mode enum values | Partial | `CompareVariant` exists; the full set of values was not recovered from the decompiled code. |
| Post-batch action values | Partial | Only `DoNothing` was seen in the decompiled set. |

## Comparison worksheet (copy this into the target app's review)

For each feature ID above, fill in:

```
| ID | FreeFileSync 14.12 | Target app | Verdict | Target evidence |
|----|--------------------|------------|---------|-----------------|
| SYNC-01 | Two-Way sync (SyncVariant) | ... | present/absent/partial/unknown | file/function/setting |
```

Verdict rules: `present` = same behaviour confirmed in the target's code;
`partial` = the feature exists but differs in a named way (say how);
`absent` = no equivalent found; `unknown` = not checked yet.

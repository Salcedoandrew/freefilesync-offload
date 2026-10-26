# FreeFileSync 14.12 — REA + Ghidra reverse-engineering notes

Target: `/home/hatch/workspace/freefilesync-re/extracted/Bin/FreeFileSync_x86_64`
Date: 2026-10-25/26. Static analysis only; the GUI was never run.
Tooling: REA CLI 6.8.0 (`npx -y rea-agents@latest`), Ghidra 12.1.4, OpenJDK 21.

## 1. Binary overview (readelf / file)

- ELF 64-bit LSB executable, x86-64, dynamically linked, **stripped**.
- Size: 25,923,184 bytes. Entry point: `0x5e0181`.
- `.text` at `0x445400`, ~18 MB of code (`.text` ends at `.fini` `0x164580c`).
- Compilers in `.comment`: GCC 16.2.0 (main) + GCC 4.8.5 (one bundled object).
- `.gnu_debuglink` names `FreeFileSync_x86_64.dbg` (not present — debug info stripped).
- 26 NEEDED shared libraries: full GTK3 stack (libgtk-3, libgdk-3, libatk, libgio,
  libpango, libcairo, libgdk_pixbuf, libfontconfig, libfreetype) + X11/Wayland
  (libX11, libwayland-client, libXxf86vm, libxkbcommon) + libz, libexpat, libm,
  libdl, libpthread, libc. This is the wxWidgets/GTK3 build.
- Dynamic symbols: 2913 total, 1732 undefined FUNC imports.
- Version string `14.12` present in `.rodata`. 230,628 printable strings total.

## 2. Environment / tooling install

- Ghidra 12.1.4: `~/workspace/ghidra/ghidra_12.1.4_PUBLIC/` (876 MB extracted;
  zip at `~/workspace/freefilesync-re/tools/ghidra_12.1.4_PUBLIC_20260921.zip`,
  569,649,598 bytes, size-verified against the GitHub release asset).
- JDK: Temurin OpenJDK 21.0.13+9 at `~/.local/jdk-21.0.13+9/`
  (tarball at `~/workspace/freefilesync-re/tools/temurin-21-jdk.tar.gz`).
  Used the tarball fallback because the host's apt was busy with a long
  os-intent replay holding the dpkg lock; apt was never needed.
- `GHIDRA_INSTALL_DIR=/home/hatch/workspace/ghidra/ghidra_12.1.4_PUBLIC`,
  `JAVA_HOME=/home/hatch/.local/jdk-21.0.13+9`.
- `rea doctor --json`: all `ghidra-*` checks OK (version 12.1.4, headless OK,
  native decompiler OK, Java 21.0.13 64-bit JDK OK). Overall `healthy: false`
  only because Hopper/IDA/JADX/Kuna are missing — none are needed for Ghidra.
- REA CLI: 6.8.0 via `npx -y rea-agents@latest`.
- Host memory is the binding constraint: 7.7 GB total, MemAvailable ~0.9 GB at
  start, CommitLimit ~4.0 GB. Ghidra headless runs with its default 2G heap
  (`GHIDRA_HEADLESS_MAXMEM` unset); the JVM was at ~1.16 GB RSS 1 min into
  import and Committed_AS already exceeded CommitLimit, so an OOM kill during
  full auto-analysis is a live risk — see §3.

## 3. Ghidra analysis

- **First attempt FAILED (disk):** `rea analyze` ran Ghidra headless but died
  with `java.io.IOException: No space left on device` — Ghidra's program
  database was being written to `/tmp`, a 512 MB tmpfs. Fix: `TMPDIR=$HOME/tmp`
  (on the 85 GB disk); REA passes `-Djava.io.tmpdir` from Node's `tmpdir()`,
  so this redirects both REA's session and Ghidra's database. Evidence:
  `/tmp/rea-analyze.json` (failure record, `failure_kind: process`,
  `failure_message: "Ghidra exited before bridge startup"`).
- **Second attempt SUCCEEDED:** `rea analyze ... --provider ghidra --snapshot
  ~/workspace/freefilesync-re/rea-snapshot` completed in ~27 min
  (01:17–01:43). JVM peaked ~1.75 GB RSS on the 2 GB headless heap; the
  Ghidra project reached ~712 MB on disk. Evidence: `/tmp/rea-analyze2.json`
  (evidence `ev_6aaebe74f715f5fa5ff45f7e319d338fd5a63432ca1876754c83d8469abfcfd3`).
- **Snapshot reuse does NOT work for the CLI:** the snapshot is only a 56 KB
  JSON Evidence bundle; the Ghidra project lives in a temp dir
  (`$TMPDIR/rea-ghidra-*`) that REA deletes on exit. Every later REA CLI call
  that needs Ghidra re-runs the full ~27 min import + auto-analysis. (Verified:
  the entry-point decompile below started a fresh JVM + new temp dir.)
  Consequence: per-function REA decompiles are ~27 min each.
- **REA entry-point decompile KILLED by a runtime restart** (proc_7c5ed4e45a6c,
  ~7 min in, no output). The partial temp project
  (`$HOME/tmp/rea-ghidra-UleACl/project/rea-project.gpr`) survived but its
  `.rep` is only 20 KB — the import had finished but analysis was ~6 min in,
  so it is not a useful shortcut.
- **Strategy change (honest):** the REA overview is done and saved, but
  per-function work through REA CLI is impractical here (27 min re-import per
  call + a restart killed one). So the function-level work runs Ghidra
  headless directly against a persistent project
  (`~/workspace/freefilesync-re/ghidra-project/`, project `FFS`), which is
  the same Ghidra 12.1.4 engine and analyzer preset REA used.
- **Direct import, attempt 1 FAILED (missing dir):** analyzeHeadless requires
  the project directory to exist first — it does not create it. Fixed by
  `mkdir -p` before the run.
- **Direct import, attempts 2–3 FAILED (disk):** "No space left on device"
  during "Disassemble Entry Points". Root cause found by watching `df` while
  it ran: Ghidra's temp dir was `/tmp/root-ghidra` on the 512 MB tmpfs, growing
  ~3 MB/s. This Temurin JDK **ignores `$TMPDIR`** (verified with a test
  program: `java.io.tmpdir` is `/tmp` with or without TMPDIR set). Fix:
  `JAVA_TOOL_OPTIONS="-Djava.io.tmpdir=/home/hatch/tmp"` (JVM picks it up
  automatically) + `XDG_CACHE_HOME=$HOME/tmp/xdg-cache` (redirects Ghidra's
  file cache off `/var/tmp`, a 2 GB tmpfs). After the fix the temp dir lands
  at `$HOME/tmp/root-ghidra` (~550 MB) and `/tmp` stays at ~1%.
- **Direct import, attempt 4 KILLED (OOM):** at ~21 min the kernel OOM-killed
  the JVM (launch.sh: "6220 Killed", RSS 1.45 GB). Cause: a parallel C++ build
  of FreeFileSync itself (8× cc1plus, ~1.9 GB, compiling zen/http.cpp,
  afs/gdrive.cpp, afs/ftp.cpp, afs/native.cpp, afs/abstract.cpp for a sibling
  task) drove MemAvailable to ~1.1 GB. The project was left at 20 KB —
  nothing was saved.
- **Direct import, attempt 5 SUCCEEDED:** retried when MemAvailable recovered
  to ~4.5 GB. Finished ~03:05 after ~31 min (slower than REA's 27 min because
  of the memory contention). Log: "REPORT: Save succeeded for:
  /FreeFileSync_x86_64" + "REPORT: Import succeeded". Persistent project at
  `~/workspace/freefilesync-re/ghidra-project/` (617 MB, project `FFS`,
  program `/FreeFileSync_x86_64`). Reopening it for a script takes ~90 s
  (no re-analysis needed with `-noanalysis`).
- **Scripts:** `ffs_find.py` (Python, superseded), `ffs_find.java` (Java port),
  `ffs_find2.java` (config-string anchors), `ffs_decomp_one.java` (decompile
  one address, takes the hex address as a script argument). Java scripts are
  required: this headless install reports "Ghidra was not started with
  PyGhidra. Python is not available" for `.py` scripts, even though
  python3 3.12.3 exists. (PyGhidra was not debugged further; Java scripts
  work and do the same job.)

## 4. Function inventory

- **74,935 procedures** (`procedure_count`), 31,539 strings, 36 segments.
- The count includes statically linked wxWidgets, OpenSSL, libcurl, libssh2.
- Demangled-name inventory (293 typeinfo/lambda names, 172 distinct enclosing
  functions) saved at `demangled-names.txt` / `mangled-names.txt`.
  Key enclosing functions: `fff::synchronize`, `FolderPairSyncer::
  synchronizeFileInt<SelectSide>`, `FolderPairSyncer::copyFileWithCallback`,
  `FolderPairSyncer::getFolderLevelWorkItems`, `ComparisonBuffer::
  compareByContent`, `ComparisonBuffer::execute`, `DeletionHandler::
  removeDirWithCallback` / `removeFileWithCallback` / `tryCleanup`,
  `categorizeFileByContent`, plus the AFS backends (`GdriveFileSystem`,
  `FtpFileSystem`, `InputStreamFtp/Gdrive/Sftp`) and GUI dialogs
  (`ConfigDialog`, `OptionsDlg`, `AbstractFolderPickerDlg`).

## 5. Key functions (decompiled)

All decompiles are Ghidra 12.1.4 native decompiler output, from the persistent
project. Function names are Ghidra's `FUN_<addr>` (binary is stripped).

### Startup chain (decompiled individually with ffs_decomp_one.java)

- **Entry point `0x5e0181`** (`decompile-5e0181.txt`, 42 bytes): the standard
  C runtime stub — calls `__libc_start_main(main=0x5bab70, ...)` then loops
  forever. Nothing FreeFileSync-specific.
- **`main` at `0x5bab70`** (`decompile-5bab70.txt`, 23 bytes): a thin wrapper
  that passes argc to `FUN_0134ffd0`.
- **`FUN_0134ffd0`** (`decompile-134ffd0.txt`, 49 bytes): the wxWidgets app
  bootstrap — creates the app object (`FUN_0134edd0`), initializes it with
  argc/argv (`FUN_0134f820`), then runs the event loop (`FUN_0134f6c0`).
  This is wxWidgets' `wxEntry`, not FreeFileSync logic.

### Sync-config / deletion / dispatch functions

Found by following references from distinctive config strings (code → string
references exist in Ghidra's database; the data → data typeinfo pointers do
not — see §7). Full output in `ghidra-decompiles2.txt` (7,568 lines,
8 functions, 0 decompile failures); summary in `ghidra-findings2.txt`.

1. **`FUN_0061a2b0` (27,763 bytes)** — the biggest function found. Reads/writes
   dozens of app settings: `LastSelected`, `MaxSize`, `Enabled`,
   `VersioningFolderHistory`, `ProgressDialog`, `LogFolderHistory`,
   `EmailHistory`, `CommandHistory`, `MainDialog`, `FolderHistory`,
   `Configurations`, `Language`, `General`. This is the main dialog's
   settings load/save (the app's persistent config).
2. **`FUN_00636110` (16,918 bytes)** — the other half of the main window's
   config persistence: same setting keys plus `OverviewPanel`, `ConfigPanel`,
   `WarnVersioningFolderPartOfSync`, `WarnUnresolvedConflicts`.
3. **`FUN_006241e0` (9,864 bytes)** — the sync-run setup: references
   `Synchronize` ×8, `Errors`, `Different`, `CustomDirections`,
   `PostSyncCommand`, `EmailNotification`, `DetectMovedFiles`, `Custom`,
   `Condition`. This is the code that prepares a synchronization run
   (post-sync command, email notification, moved-file detection, custom
   directions).
4. **`FUN_009da500` (6,342 bytes)** — the sync-operation dispatcher: references
   the operation enums `so_update_right/left`, `so_delete_right/left`,
   `so_create_right/left`, `so_move_right/left`, the deletion methods
   `delete_versioning` / `delete_recycler` / `delete_permanently`, the
   versioning timestamp format `YYYY-MM-DD hhmmss`, and `database`. Given a
   sync operation (create/update/delete/move, left/right) it dispatches to
   the right handler and applies the chosen deletion method. This is the
   closest decompiled function to the actual sync engine's decision point.
5. **`FUN_00622050` (5,429 bytes)** — per-folder-pair sync configuration:
   `VersioningFolder`, `Update`, `Threads`, `RightOnly`/`RightNewer`/
   `LeftOnly`/`LeftNewer`, `MinCount`/`MaxCount`/`MaxAge`, `Delete`/`Create`,
   `Changes`. Which files sync which way, and the versioning limits.
6. **`FUN_00629ff0` (2,461 bytes)** — versioning settings: `VersioningFolder`,
   `TimeStamp-Folder`, `TimeStamp-File`, `Versioning`, `Style`. How old
   versions are kept (timestamped folder vs timestamped file).
7. **`FUN_009c0160` (1,076 bytes)** — wxWidgets "Delete Items" dialog
   constructor: builds the dialog (`L"Delete Items"`, `wxDefaultPosition`),
   looks up the `delete_recycler` string, populates the item list. GUI only.
8. **`FUN_005109ad` (236 bytes)** — a C++ destructor for a sync-config
   object: `g_object_unref` calls and member teardown, with a
   `delete_recycler` branch (mode enum 1) that rounds a value and looks it
   up. Cleanup code, not logic.

Note: the `sync_twoway` / `sync_mirror` / `sync_update` strings are referenced
only from data (a string table / config-enum mapping), not from code — so they
yielded no functions. The typeinfo-name anchors (`fff::synchronize`,
`FolderPairSyncer::synchronizeFileInt`, `ComparisonBuffer::compareByContent`,
`applyVersioningLimit`) DID find their strings
(`ghidra-findings.txt`: `fff::synchronize` at `0x165e981`,
`FolderPairSyncer::synchronizeFileInt<LEFT/RIGHT>` at `0x165f991`/`0x165fa31`,
`applyVersioningLimit` ×4) but following their references found 0 functions,
because Ghidra's reference database has no data → data references for the
typeinfo/vtable pointers. The inner copy/compare loop (`fff::synchronize`,
`FolderPairSyncer::synchronizeFileInt`, `ComparisonBuffer::compareByContent`)
was therefore NOT isolated to a decompile in this pass; see §7 for the
remaining step (vtable pointer search).


## 6. Sync engine — what the binary shows

The binary's own structure (from the embedded typeinfo/lambda names, which
survive stripping) names the sync engine's pieces exactly:

- **Top level: `fff::synchronize(...)`** — takes a start time, 5 booleans,
  a `vector<FolderPairSyncCfg>`, a `vector<SharedRef<BaseFolderPair>>`, a
  map of filesystems, a `WarningDialogs&`, and a `ProcessCallback&`. Inside
  it a local class `PcbNoThrow` (the "process callback that never throws")
  wraps the progress/error reporting so a callback failure cannot abort the
  sync. This is the function the GUI calls when you press Synchronize.
- **Per-folder-pair: `(anonymous namespace)::FolderPairSyncer`** — the
  workhorse. `getFolderLevelWorkItems` walks the two folder trees and builds
  the work list (5 lambdas = the 5 passes); `synchronizeFileInt<LEFT/RIGHT>`
  (one template instantiation per direction) decides what happens to each
  file pair; `copyFileWithCallback` copies a file and reports progress
  (its lambdas handle the per-chunk callback and the retry path).
- **Comparison: `(anonymous namespace)::ComparisonBuffer`** — `execute` runs
  a comparison over the resolved folder pairs; `compareByContent` does the
  byte-by-byte content comparison (the "compare file content" mode, as
  opposed to size/date); `categorizeFileByContent` sorts a file pair into
  its sync category after the content check.
- **Deletion: `(anonymous namespace)::DeletionHandler`** —
  `removeFileWithCallback` / `removeDirWithCallback` delete with progress
  reporting; `tryCleanup` removes leftover temp/versioning data. The
  deletion method is one of three, confirmed by the decompiled dispatcher
  (`FUN_009da500`): recycle bin, versioning folder (timestamped
  `YYYY-MM-DD hhmmss`), or permanent delete.
- **Versioning: `fff::applyVersioningLimit`** — enforces the cap on how many
  old versions are kept (the `MinCount`/`MaxCount`/`MaxAge` settings from
  `FUN_00622050`).
- **Backends (AFS — AbstractFileSystem):** `GdriveFileSystem`,
  `FtpFileSystem`, `SftpFileSystem`, plus `InputStreamFtp/Gdrive/Sftp` and
  the native local filesystem. The binary statically links libcurl and
  libssh2 (source paths `/mnt/hgfs/Projects/libcurl/...`,
  `/mnt/hgfs/Projects/libssh2/...`) and OpenSSL 4.0.2 for the network
  backends. The GUI (wxWidgets/GTK3) sits on top; the engine itself is the
  `base/` code (`algorithm.cpp`, `comparison.cpp`, `file_hierarchy.cpp`,
  `synchronization.cpp`, `versioning.h` — these source paths are embedded
  in the binary as `__FILE__` strings).

In plain language: the sync engine is a three-phase pipeline — **compare**
(the two folder trees, by size/date or by content) → **plan** (each file pair
gets an operation: create/update/delete/move, left or right) → **execute**
(copy with progress + verification, delete via recycle/version/permanent,
enforce versioning limits). The decompiled dispatcher (`FUN_009da500`) is
the execute phase's switchboard; the compare/plan functions are named in
the binary but their decompiles are the remaining step (§7).

## 7. Limitations / unknowns

- Binary is stripped: no symbol names; function identities are inferred from
  string xrefs and control flow, not from symbols. All decompiled functions
  are named `FUN_<addr>`.
- Host RAM is tight (7.7 GB total, ~0.9 GB MemAvailable at start); the JVM
  was OOM-killed once by a parallel C++ build — see §3 for the full story.
  Ghidra headless runs with its default 2G heap (`GHIDRA_HEADLESS_MAXMEM`
  unset). The Temurin JDK ignores `$TMPDIR`; use
  `JAVA_TOOL_OPTIONS="-Djava.io.tmpdir=/home/hatch/tmp"` and
  `XDG_CACHE_HOME=$HOME/tmp/xdg-cache` for any future direct Ghidra run.
- **The inner sync loop was not decompiled.** The typeinfo-name anchors
  located the strings but Ghidra's reference database has no data → data
  references for typeinfo/vtable pointers, so following them found 0
  functions. Remaining step (not done): a vtable pointer search — byte-search
  memory for the 8-byte little-endian pointer to a typeinfo-name string
  (finds the typeinfo struct, name pointer at +8), then for a pointer to
  that struct (finds the vtable's typeinfo slot at vtable−16), then read the
  vtable entries (function pointers at vtable+0, +8, …) and decompile them,
  and use `getReferencesTo(vtable)` for the constructors. The persistent
  project (`~/workspace/freefilesync-re/ghidra-project/`) is ready for this.
- **REA per-function decompiles were not used.** The REA overview worked, but
  every REA CLI call that needs Ghidra re-runs the full ~27 min import (the
  snapshot is only a 56 KB JSON bundle; the project is deleted on exit), and
  one entry-point decompile was killed by a runtime restart. All
  function-level work here is direct Ghidra headless against the persistent
  project — same engine, same analyzer preset.
- Static analysis only; the GUI was never run. No binary modification.

# FreeFileSync architecture map, and where an Offload mode belongs

Target: `github.com/hkneptune/FreeFileSync`, commit `53143e29` (2026-09-24),
tag `v14.12`, cloned shallow (9.4 MB). Everything cited below was read in that
tree; line numbers are from that checkout.

Scale: **134 source files, 57,074 lines of `.cpp`** in `FreeFileSync/Source/`, plus
**6,339 lines of `.cpp`** in `zen/`. Three executable targets share it:
`FreeFileSync` (GUI sync), `RealTimeSync` (folder watcher), and small tools.

**Status:** the layer below `ui/` builds and runs on macOS. A new
`FreeFileSync/Source/offload/` target sits beside `base/` calling `afs` directly — no
`ui/` involvement — and its window is a native app reaching the engine through
`offload/capi.h`. See [phase1-report.md](phase1-report.md) and
[phase2-report.md](phase2-report.md). The `ui/` layer is still unported, and that remains
option A's cost:

```
   offload/mac/*.swift             SwiftUI window (sources | transfers | destinations)
              │  C ABI, no C++ in any signature
   offload/capi.cpp                 one job per source -> destination, one thread each
              │
   offload/*.cpp                    hash, engine, report, CLI, tests
              │
   afs/*  zen/*  base/binary.cpp    the ported layer, unchanged
```

---

## 1. Layer cake

```
   FreeFileSync/Source/ui/*        wxWidgets GUI  (main_dlg 6493 lines, file_grid 2302)
   FreeFileSync/Source/RealTimeSync
              │
   FreeFileSync/Source/base/*      sync engine: comparison, synchronization, algorithm,
              │                    file_hierarchy (FolderPair/FilePair), db_file (2-way state)
              │
   FreeFileSync/Source/afs/*       AbstractFileSystem: native / sftp / ftp / gdrive
              │
   zen/*                           file_io, file_traverser, thread, zstring, string_tools,
   zenXml/*                        sys_error, http, recycler, parallel primitives
   wx+/*                           wxWidgets extensions: grid, darkmode, image_resources
```

Dependencies flow downward only. `base/` never includes `ui/`. This is a clean
separation and it is the reason a new UI mode is feasible at all.

## 2. What already exists and is worth reusing

| Need | Existing component | Evidence |
|---|---|---|
| Path that works on local disk *and* remote | `AbstractPath` = `{AfsDevice, AfsPath}`; one API for native/SFTP/FTP/GDrive | `afs/abstract.h:34`, `:46` |
| Streaming read with progress callback | `AbstractFileSystem::InputStream::tryRead(buf, n, notifyUnbufferedIO)` | `afs/abstract.h:470+` |
| Streaming write + atomic publish | `OutputStream::tryWrite`, `finalize()` → writes `.ffs_tmp`, then renames | `afs/abstract.h:483`, `:492`; temp-file rule at `base/synchronization.cpp:925` |
| Copy directory tree / symlink / permissions | `copyNewFolder`, `copySymlink`, `moveAndRenameItem`, `supportPermissionCopy` | `afs/abstract.h:509–563` |
| Streamed byte-compare of two files | `fff::filesHaveSameContent(pathA, pathB, cb)` — block-wise `memcmp`, handles differing block sizes, short reads | `base/binary.cpp:14` |
| Post-copy verify already wired into sync | `verifyCopiedFiles` option → re-reads and byte-compares | `base/synchronization.cpp:670`, `:2306` |
| Crash-safe copy | `failSafeFileCopy` → temp name then rename | `base/synchronization.cpp:2290` |
| Deterministic ordering, parallelism, thread pool | `zen::parallelFor`, `parallelScope`, `zen::thread` | `zen/thread.h`, `base/parallel_scan.cpp` |
| Progress reporting contract | `PhaseCallback` with `requestUiUpdate`, `reportInfo`, `reportWarning`, `reportError` | `base/process_callback.h:14` |
| Running long work off the UI thread | `runAsync(...)` | `ui/main_dlg.cpp:3247` |
| Folder scan with filters | `scanFolder` / `FileHierarchy` with include/exclude filters | `base/file_hierarchy.h`, `base/path_filter.cpp` |
| Error model | `FileError` carrying sys error code + path + message | `zen/file_error.h` |
| Dark mode, grids, icons, custom controls | `wx+/darkmode.cpp`, `wx+/grid.cpp`, `wx+/image_resources.*` | — |

That is most of an offload engine already written and battle-tested.

## 3. What does **not** exist anywhere in the tree

1. **No hashing at all.** Grep for `xxHash|SHA1|SHA256|MD5` across the whole repo
   returns exactly one hit, and it is a *comment* in `afs/sftp.cpp:251` about
   SSH host-key fingerprints. FFS's idea of "content compare" is a live byte
   comparison between two files (`base/binary.cpp`), not a stored digest. So:
   **xxHash64, MHL emission and per-file digest records are all new code.**
2. **No N-way copy.** The engine is structurally two-sided: `FolderPair`,
   `FilePair`, left/right, `CompareVariant{timeSize, content, size}`
   (`base/structures.h:18`), `SyncConfig` with `SyncDirectionConfig`
   (`base/structures.h:201`), and
   `synchronize(syncStartTime, verifyCopiedFiles, …, syncConfig, folderCmp, …)`
   (`base/synchronization.h:88`) taking **one** `FolderComparison`.
   Offload needs source → {destination₁ … destinationₙ}. Options, cheapest first:
   run one `FolderComparison` per destination (reuses everything, costs n scans),
   or bypass `base/` and drive `afs/` directly (see §5).
3. **No job queue.** The GUI runs one sync at a time (`ui/main_dlg.cpp:4884`,
   `:5212` call `synchronize()`); there is no queue model, no per-job state machine,
   no resume-after-stop record.
4. **No report/MHL writer.** `base/db_file.cpp` writes FFS's own two-way state file;
   nothing emits a human- or archive-readable manifest.

## 4. Two integration strategies

### (A) Offload as a mode inside the existing GUI

Add a mode switch to `MainDialog` (`ui/main_dlg.h:30`), swap the `FolderPairPanel`
centre area for an Offload panel, reuse `ProgressIndicator`/`GuiStatusHandler`.

- Cost: `main_dlg.cpp` is **6,493 lines** with folder-pair-specific assumptions
  (`additionalFolderPairs_`, `folderPairScrapyard_` at `:309–317`), and the layout
  comes from a generated `gui_generated.cpp`. Touching it risks the existing sync
  UI, which the brief says must keep working.
- Gets "one app, two modes" exactly as asked.

### (B) Offload as a new executable in the same tree (recommended)

```
   FreeFileSync/Source/offload/          <- new, ~3-4k lines
        application.cpp   wxApp, dark mode, single main window
        main_dlg.cpp      sources | destinations | queue | per-file status
        offload_engine.cpp   job model, n-way copy, verification, hashing
        hasher.cpp        XXH64 / XXH3-64 / SHA-1 / MD5 wrappers
        mhl.cpp           MHL + CSV + JSON writers
        dedupe.cpp        name+size+mtime rule, -N filename increment
```

Links against the existing `zen` + `afs` sources (the Makefile already builds them
as plain objects, so a second target is a second `cppFiles=` list and one
`exeName`). No file in `ui/`, `base/` or `RealTimeSync/` is modified, so
"no regressions in normal sync" is structurally true rather than merely tested.

Trade-off: the result is a second binary (`FFSOffload.app`), not a tab in FFS.
Given that the published macOS app is **not built from this source tree** and FFS's
own GUI therefore cannot be rebuilt on this Mac without reconstructing the platform
layer (§5), option B is the only route to a working macOS app without that port first.

## 5. The macOS problem (the real risk in this brief)

**macOS builds do exist.** Every release publishes one — at `v14.12`:
`FreeFileSync_14.12_macOS.zip`, 30,756,614 bytes, SHA-256
`8c1aecc9…dda822`, alongside the Windows installer and the Linux tarball
(https://github.com/hkneptune/FreeFileSync/releases). The changelog even carries
macOS-specific fixes ("Fixed bitmap buttons being shrinked (macOS)"). So FreeFileSync
runs on macOS, natively, today.

**But that binary is not built from this tree.** The same tag contains:

| Token | Occurrences in the whole source tree |
|---|---|
| `__APPLE__` | **0** |
| `__WXOSX__` or `__WXMAC__` | **0** |
| `__linux` / `__linux__` | **0** |
| `_WIN32` | **1** |
| `FSEvents` / `kqueue` implementation | **0** (one URL in a comment, `zen/dir_watcher.h:17`) |

A cross-platform application with three shipped platforms cannot contain zero
platform conditionals. The mirror has had its platform layer stripped: it is the
source for the *Linux* build (the download page's source tarball), and the author
builds Windows and macOS from a private superset. That is consistent with there
being no `Darwin` branch in any Makefile and no `_.vcxproj` anywhere either.

Consequence for us: **the published macOS app is not a starting point for a fork.**
We cannot add a mode to a signed binary, and we cannot check out the code that
produced it. Any macOS fork means reconstructing the platform layer from the Linux
one. The concrete gaps:

The public tree is built for Linux/GTK. Evidence:

| Blocker | Evidence |
|---|---|
| Build file is GTK-only | `FreeFileSync/Source/Makefile:26` adds `pkg-config --cflags gtk+-3.0`; `:28` hardcodes `-isystem/usr/include/gtk-3.0`. No `Darwin`/`APPLE` branch anywhere in the repo (grep: zero hits in all Makefiles and shell scripts). |
| `zen` includes glib unconditionally | `zen/sys_error.h:13` → `#include <glib.h>` with no platform guard. Any TU touching `FileError` needs glib present. |
| Directory watcher is inotify-only | `zen/dir_watcher.cpp:52` calls `::inotify_init()`; the header even documents kqueue as the intended macOS backend (`zen/dir_watcher.h:16-17`) but it is not implemented in this tree. Only used by `RealTimeSync` — so not needed for an Offload target. |
| `/proc` used for process facts | `zen/sys_info.cpp:122` (`/proc/sys/kernel/osrelease`), `:216` (`/proc/self/exe`) — needs macOS shims (`sysctl`, `_NSGetExecutablePath`). |
| Missing standard includes that libstdc++ supplies transitively | Probe with libc++: `std::uncaught_exceptions` undeclared at `zen/scope_guard.h:73` (its `<exception>` include is missing; `legacy_compiler.h` only pulls `<version>`/`<string>`), `std::abs` undeclared at `zen/i18n.h:99` (no `<cstdlib>`), `std::is_trivially_destructible_v` undeclared at `zen/globals.h:135` (no `<type_traits>`). Small, real, mechanical fixes. |
| Toolchain on this Mac | `-std=c++23` (Makefile:4) is **rejected by Apple clang 15.0.0** — the only Apple compiler installed (`/Library/Developer/CommandLineTools`, SDK 14.4 while the OS is macOS 26.5.2). Homebrew `llvm 23.1.4` is present and does accept `-std=c++23`. So the build must either use brew LLVM with `-mmacosx-version-min=…`, or the Command Line Tools need updating to a newer Xcode. |
| wxWidgets | Homebrew `wxwidgets 3.3.4` (Cocoa, `wx-config` reports `osx_cocoa-unicode-3.3`) is installed and its headers work with both compilers in isolation. Upstream develops against a pinned wx build of its own, so expect *some* API drift; unknown until compiled. |
| glib is a real dependency, not just a stray include | 69 references in `zen/`: `zen/zstring.cpp:25` (`g_utf8_normalize`), `:98/:157` (`g_unichar_toupper`), `zen/sys_error.cpp:158` (`formatGlibError`), `zen/recycler.cpp:19` (`g_file_new_for_path`, i.e. the Trash). On macOS this means `brew install glib` and linking it — cross-platform and workable, but it is another dependency to carry, and the glib Trash path would replace whatever macOS code the author uses. |

**Conclusion (measured, not estimated):** the engine layer ports in **125 lines across
14 files**. The Phase 0 spike built `libffsengine.a` from `zen` + `afs/native` +
`base/binary`, linked a test binary, and copied 64 MiB at 1.26 GiB/s with a correct
IDENTICAL/MISMATCH verdict. Every gap in the table above was a small local shim, not
a redesign. The port and its evidence are preserved in `offload/patches/` and
[phase0-spike-report.md](phase0-spike-report.md).

What remains unported, and why that is fine for Offload: `ui/` (the 6,493-line dialog
and its wx 3.3 drift — untouched, so FFS's own window still won't build on macOS),
`RealTimeSync/` (inotify), and the FTP/SFTP/GDrive transports.

What does *not* change: for option B we only need `zen` + `afs` + a few `base/`
files, which skips `ui/` (the 6,493-line dialog and its wx drift) and
`RealTimeSync/` (the inotify watcher) entirely. That is the smallest macOS surface
that still gives us a verified multi-destination copy engine.

A Linux dev loop is available today for the engine work — Docker is running
(`orbstack` context), so a throwaway `ubuntu` container with `libwxgtk3.2-dev`,
`libcurl4-openssl-dev`, `libssh2-1-dev`, `libidn2-dev`, `libssl-dev` can build and
unit-test the engine regardless of what macOS does.

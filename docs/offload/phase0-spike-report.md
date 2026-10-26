# Phase 0 spike: does FreeFileSync's engine build and run on this Mac?

**Answer: yes.** Not just "compiles" — a linked binary copied 64 MiB through
FreeFileSync's own file-system layer at 1.26 GiB/s, byte-compared it, and correctly
reported a 4-byte corruption as a mismatch.

Run 2026-10-25 on arm64 macOS 26.5.2, FreeFileSync `v14.12` (`53143e29`).

---

## 1. What the spike actually built

`FreeFileSync/Source/Makefile.macos` → `libffsengine.a` (26 objects, 1.27 MB):
`zen/` (minus the inotify watcher), `afs/abstract.cpp` + `afs/native.cpp`,
`base/binary.cpp` + `base/structures.cpp`, plus two small macOS substitues.

Then `macos/smoke_copy.cpp` links that library and does the real work:

```
copied  67108864 bytes in 0.0508707 s (1258.09 MiB/s)
verify  IDENTICAL
exit=0
```

Cross-checked against the system hash — both sides identical:

```
92f81512565511957004b8d9a483546c170136af8aa5ff410666ede1985744bf  src.bin
92f81512565511957004b8d9a483546c170136af8aa5ff410666ede1985744bf  dst.bin
```

Then four bytes at offset 32 MiB were overwritten:

```
verify  MISMATCH
exit=1
```

So the copy path, the `.ffs_tmp`-then-rename publish, and the byte-level
verification all work on this platform. That is the hardest thing in the brief
working on day one.

## 2. The port is 125 lines

`git diff --stat` against upstream `v14.12`: **14 files, 125 insertions, 1 deletion.**
Plus four new files (makefile, compat header, two stubs) and a smoke test.

| File | Change |
|---|---|
| `zen/mac_compat.h` *(new)* | `<cstdlib>/<exception>/<type_traits>/…` that libstdc++ pulled in transitively; `st_mtim` → `st_mtimespec` |
| `zen/guid.h` | macOS branch: `getentropy()` (needs `<sys/random.h>`); glibc `#error` now Linux-only |
| `zen/thread.cpp` | `pthread_setname_np` instead of `prctl(PR_SET_NAME)` |
| `zen/process_priority.cpp` | `PRIO_DARWIN_BG` instead of Linux `ioprio` syscalls |
| `zen/format_unit.cpp` | `CFCalendarGetFirstWeekday` instead of `nl_langinfo(_NL_TIME_FIRST_WEEKDAY)` |
| `zen/sys_error.cpp` | `strerror()` instead of the Linux errno→message table |
| `zen/socket.h` | `EAI_*` codes guarded per macro; `accept`+`fcntl` instead of `SOCK_CLOEXEC` in `socket()` |
| `zen/file_io.cpp` | `posix_fadvise` guarded out |
| `zen/file_access.cpp`, `afs/native.cpp` | `sys/mount.h` / `sys/statvfs.h` instead of `sys/vfs.h` |
| `zen/process_exec.cpp` | `pipe` + `fcntl(FD_CLOEXEC)` instead of `pipe2` |
| `zen/shutdown.cpp` | `_exit` instead of `std::quick_exit` (absent from macOS libc) |
| `zen/file_path.cpp` | `*_NSGetEnviron()` for `environ` |
| `zen/sys_info.cpp` | dead Linux `netpacket/packet.h` include guarded out |
| `libssh2/libssh2_wrap.h` | `LIBSSH2_ERROR_STORE_OVERFLOW` guarded (newer libssh2 headers) |

Every change is `#if defined(__APPLE__)` / `#if !defined(__APPLE__)` guarded, so the
upstream Linux build is untouched. Nothing was rewritten; nothing was guessed.

## 3. Build flags that matter

```
-std=c++23                               # Apple clang 15 rejects this; brew LLVM 23 accepts
-D_LIBCPP_DISABLE_AVAILABILITY           # else std::from_chars reports "introduced in macOS 26.0"
-mmacosx-version-min=12.0
-include zen/mac_compat.h -include zen/i18n.h   # ORDER MATTERS: compat header first
$(wx-config --cxxflags)                  # Cocoa, osx_cocoa-unicode-3.3
pkg-config glib-2.0 gio-2.0 gobject-2.0  # glib is a real engine dependency
-L$(brew --prefix llvm)/lib/c++          # link brew libc++, not the SDK's 14.4 one
-framework CoreFoundation
```

Toolchain: `/opt/homebrew/opt/llvm/bin/clang++` (23.1.4). The Command Line Tools on
this machine are stale — clang 15.0.0 with SDK 14.4 on macOS 26 — and cannot compile
`-std=c++23` at all. Homebrew LLVM is the working compiler; a newer Xcode would also do.

## 4. What was deliberately left out, and why

| Left out | Why | Consequence |
|---|---|---|
| `zen/dir_watcher.cpp` | inotify-only; upstream's own header says macOS should use kqueue, but that code isn't in the public tree | Only `RealTimeSync` uses it → not needed for Offload |
| `afs/ftp.cpp`, `sftp.cpp`, `gdrive.cpp` | still carry Linux-only `accept4`/`MAX_SFTP_*`/OAuth-socket code | local-only engine; remote destinations are a later, separate job |
| `afs/concrete.cpp` | path-phrase dispatcher that drags in all three transports | build `AbstractPath` via `createItemPathNative()` instead |
| `base/icon_loader.cpp` | loads GTK icon themes | replaced by `macos/icon_loader_stub.cpp`; NSWorkspace icons later |
| `ui/`, `RealTimeSync/` | the GUI is a separate, much larger port | untouched; **FreeFileSync's own window still does not build on macOS** |

That last row is the one to be clear about: this spike proved the *engine* works, not
that FreeFileSync-the-app now runs on your Mac.

## 5. What it means for the plan

The "macOS port risk" that dominated the estimate is now a measured quantity rather
than an unknown: 125 lines, 14 files, one build file, half a day. That collapses
option A's biggest objection too — porting the engine was never the hard part;
porting `ui/` (6,493-line `main_dlg.cpp` + wx 3.3 drift + GTK-isms in `wx+`) still is,
and the spike says nothing about it because it didn't compile a single UI file.

Revised: Phase 1 (engine, headless) can start from a working foundation, so the
engine half of the work is now mostly writing Offload logic — hashing (absent from
FFS entirely), the job model, dedupe, MHL/CSV output — not fighting the platform.

## 6. Reproducing it

The port lives in this workspace, not only in `/tmp`:

- `offload/patches/macos-port.patch` — the 14-file diff against `v14.12`
- `offload/patches/new-files/Makefile.macos` — the build
- `offload/patches/new-files/mac_compat.h` — the force-included shim
- `offload/patches/new-files/macos/*.cpp` — icon stub, transport stub, smoke test

```bash
git clone https://github.com/hkneptune/FreeFileSync.git && cd FreeFileSync
git apply /path/to/macos-port.patch
cp -r /path/to/new-files/* .
make -C FreeFileSync/Source -f Makefile.macos -j8
```

## 7. Still unverified

- Two destination drives at once (needs the Phase 1 job model — the engine copies one
  file per call today, which is exactly the primitive a job loop needs)
- Real camera cards: exFAT/NTFS naming, `.DS_Store`, 12,000-file folders, and cards
  that eject mid-copy
- Throughput on real SSD/HDD/USB rather than the page cache — 1.26 GiB/s here is
  cache-warm memory bandwidth, not disk
- Anything in `ui/`: no FreeFileSync window was compiled on macOS

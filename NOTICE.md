# Notice: what this repository is, and what it changed

## Provenance

This is a **modified copy** of FreeFileSync, taken from the source mirror at
<https://github.com/hkneptune/FreeFileSync> at tag **v14.12**, commit `53143e29`
(2026-09-09). Upstream is a mirror of the FreeFileSync source and does not accept pull
requests; nothing here has been sent upstream.

It is not FreeFileSync, it is not published by the FreeFileSync authors, and it is not
affiliated with or endorsed by them.

## Modifications

Modified on **2026-10-25** by Andrew Salcedo, as required by GPLv3 §5(a).

Added:

- `FreeFileSync/Source/Makefile.macos` — macOS build of the engine layer and the new targets.
- `FreeFileSync/Source/offload/` — the Offload engine (hashing, copy/verify, reports, CLI,
  tests), a C ABI over it, and the macOS window (Swift).
- `FreeFileSync/Source/macos/` — three macOS-only substitutes.
- `zen/mac_compat.h` — force-included compatibility header.
- `README.md`, `CHANGELOG.md`, `NOTICE.md`, `docs/offload/`, `.gitignore` — this fork's
  documentation.

Changed (each inside `#if defined(__APPLE__)` guards unless noted):

- `FreeFileSync/Source/afs/native.cpp`
- `zen/file_access.cpp`, `zen/file_io.cpp`, `zen/file_path.cpp`, `zen/format_unit.cpp`,
  `zen/guid.h`, `zen/process_exec.cpp`, `zen/process_priority.cpp`, `zen/shutdown.cpp`,
  `zen/socket.h`, `zen/sys_error.cpp`, `zen/sys_info.cpp`, `zen/thread.cpp`
- `libssh2/libssh2_wrap.h`

Nothing in `FreeFileSync/Source/ui/` was touched; the FreeFileSync sync window is unchanged
and still does not build on macOS.

`Changelog.txt` is upstream's own changelog and is left as it was.

## Licence

The FreeFileSync source is distributed under the **GNU General Public License v3**; the full
text, together with the third-party licences, is in [License.txt](License.txt). Every source
file carries its original copyright header, and those headers have been kept.

Because GPLv3 applies, this repository as a whole is distributed under **GPLv3**. If you
convey it, keep the notices, keep this file, and mark your own changes with a date.

**Read the vendor's terms as well.** `License.txt` opens with a restriction the vendor adds
on top of the GPL:

> The FreeFileSync standard and Donation Edition are for **private use** only… **Commercial
> use**, government use, and other non-private uses require the purchase of the FreeFileSync
> Business Edition.

That is their posted term. It sits awkwardly against GPLv3 §10, which forbids further
restrictions — but it is their position, and if this code touches paid work, buy the
Business Edition or ask the vendor in writing. This notice records what the vendor says; it
is not legal advice, and it does not resolve that conflict for you.

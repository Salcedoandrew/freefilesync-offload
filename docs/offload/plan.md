# Plan: an Offload mode for a FreeFileSync fork, built on macOS

Read with: [spec-offshoot-ux.md](spec-offshoot-ux.md) (what the workflow is) and
[architecture-map.md](architecture-map.md) (what FFS gives us, and what breaks on
macOS).

**Status 2026-10-25: Phases 0, 1 and 2 are done and measured.** The engine builds,
copies, hashes and verifies on this Mac from a clean `v14.12` clone, and the window is a
native macOS app driven through a C ABI — screenshots in [screenshots/](screenshots).
What is left is Phase 3 polish (HTML report, icon) and Phase 4 hardening against real
drives. Evidence: [phase0-spike-report.md](phase0-spike-report.md),
[phase1-report.md](phase1-report.md), [phase2-report.md](phase2-report.md).

---

## 0. Two decisions I cannot make for you

### Decision 1 — licence posture (read this first)

The code is GPLv3, and every source file says so:

> `// * This file is part of the FreeFileSync project. It is distributed under    *`
> `// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *`

But `License.txt` adds a restriction on top:

> "The FreeFileSync standard and Donation Edition are for **private use** only…
> **Commercial use**, government use, and other non-private uses require the
> purchase of the FreeFileSync Business Edition."
> — https://freefilesync.org/faq.php#business

What that means in practice, stated plainly:

- **Building and running a modified fork yourself, without distributing it, is fine.**
  GPLv3 §2 explicitly allows running and modifying covered works "that you do not
  convey, without conditions".
- **Publishing the fork** (a public branch, a PR, a release, a DMG handed to a
  friend) is *conveying*. GPLv3 then applies: ship the complete corresponding
  source, keep all notices, mark your changes with a date (§5a), and licence the
  whole thing under GPLv3 (§5c). Do not present it as FreeFileSync or use the
  vendor's name as your product identity.
- **Wedding work is commercial use.** The vendor's own posted terms say commercial
  use needs the Business Edition, and the mirror maintainer states the cut-off
  explicitly in the repository description: *"Last free version for commercial use:
  [v12.5]"* (https://github.com/hkneptune/FreeFileSync/releases). Current HEAD is
  **v14.12**. That statement sits awkwardly against GPLv3 §10 ("you may not impose
  any further restrictions"), but it is their posted term and I am not going to
  characterise your legal exposure for you. If this tool touches paid client work,
  either buy the Business Edition or take the from-scratch route below — and if it
  matters commercially, ask the vendor in writing.
- **There is no upstream PR to open.** The mirror says so itself: *"This repository
  is just a mirror of the FreeFileSync source code. Please do not send pull
  requests."* The brief's "PR describing the changes" can only target your own fork
  (or the v12.5 tag if we ever needed a GPL-clean base, which would mean giving up
  14 versions of fixes).

**The from-scratch alternative.** Everything on the deferred list is deferred because
it is hard; what we actually need — copy a card to N disks while hashing, read back,
compare, write a manifest — is roughly 2,000 lines with no FFS code in it. Writing it
fresh (Swift + AppKit, or wxWidgets if we want to reuse FFS's UI helpers) removes
both the GPL obligation and the private-use question entirely, and removes the macOS
port risk, because we would never have to compile FFS. The cost is that we rebuild
file handling that FFS already has and we throw away any chance of "one app, two
modes". That is Decision 2's option C.

### Decision 2 — how the fork is shaped

| | Shape | Gets you | Costs |
|---|---|---|---|
| **A** | Port FFS's GUI to macOS, add Offload as a mode inside `MainDialog` | Literally the brief: one app, new mode, all sync features | Port the 6,493-line `main_dlg.cpp` + wx drift + glib → weeks, largest unknown |
| **B** | New `FFSOffload` executable in the same tree, reusing `zen` + `afs`; FFS itself untouched | Native macOS app, FFS engine reuse, zero regression risk to sync, ~1.5–2 weeks | Second binary; FFS sync still not on macOS |
| **C** | Write it from scratch, no FFS code | Clean licence, no port risk, full control, fastest to a *working* macOS app | Rebuilds file I/O; not a fork |

My recommendation: **B if you want the fork specifically, C if what you want is a
tool you can use on Saturday.** A only makes sense if "FreeFileSync running on my Mac
with all its sync features" is itself a goal — and given that the public tree cannot
currently compile on macOS at all, A is the option most likely to stall.

The rest of this plan assumes **B**, with A and C noted where they diverge.

---

## 1. Phase 0 — feasibility spike ✅ DONE

Result: **the engine builds, links and runs on this Mac.** Full detail and the exact
patch list in [phase0-spike-report.md](phase0-spike-report.md). The short version:

- 28 files swept in `zen/` + `afs/`: **25 clean, 3 excluded by design** (inotify
  watcher, and the FTP/SFTP/GDrive transports, which an offload job doesn't need).
- `base/`: 13 of 14 clean; the one failure is `icon_loader.cpp` (GTK themes), stubbed.
- The whole port is **125 added lines across 14 files**, every one inside an
  `#if defined(__APPLE__)` guard, so upstream's Linux build is untouched.
- `libffsengine.a` links; a test binary copied **64 MiB at 1.26 GiB/s**, verified
  **IDENTICAL**, then detected a **4-byte corruption as MISMATCH** (exit 1).
- Port is preserved in this workspace: `offload/patches/macos-port.patch` +
  `offload/patches/new-files/`.

What this does *not* prove: `ui/` was never compiled, so FreeFileSync's own window
still does not build on macOS. That was always option A's cost, and it is unchanged.

---

## 2. Phase 1 — engine, headless ✅ DONE (measured in [phase1-report.md](phase1-report.md))

Built as a CLI first, so it could be tested without wx. What landed:

- `offload::runJob()`: source scan (`zen::traverseFolder`, junk-filtered) → one worker
  per destination → copy → read-back verify → per-file records → report.
- Hashing: XXH64 (default, 4.55 GiB/s measured), MD5 and SHA-1 selectable with
  `--hash`. Hash **while** streaming, so the source is read once.
- Publish: `AFS::getOutputStream(...)->finalize()` — FFS's own `.ffs_tmp` + rename, so
  a half-written file is never visible.
- Verify: `--verify size` or `--verify hash` (read back and compare digests).
  `--reverify` hashes what is already on the destination without copying.
- Duplicate Detect: skip on name+size+mtime; a clashing name becomes `name-1.ext`,
  never an overwrite.
- Pause / resume / cancel in the engine (`std::atomic<bool>`), driven from the CLI by
  `SIGUSR1` and `SIGINT`. Verified on a real 1.5 GiB run: 0 bytes written while paused.
- Reports: CSV + JSON anywhere (`--report DIR`), MHL 1.1 at each destination root.
- 100 assertions, including the corrupted-destination case and source immutability.

Still open from the original Phase 1 list: **a per-job journal** (`.offload-state.json`)
so a killed job resumes without re-hashing what it already verified. Today `--reverify`
and Duplicate Detect cover the practical cases; a journal is the next improvement, not a
gap that loses data.

**Exit criteria:** met on the real CLI — 221 files to two destinations, every one
`verified`, `.mhl` at both roots, CSV/JSON beside the log, card unchanged afterwards.

## 3. Phase 2 — the window ✅ DONE (measured in [phase2-report.md](phase2-report.md))

Decision taken: **option B, the native shell** — not wxWidgets. The reference layout is a
custom-drawn dark three-pane window; wx would have to be painted by hand to match it *and*
carry the unported `ui/` tree. The window is SwiftUI/AppKit over a small C ABI
(`offload/capi.h`) with one transfer per thread, which also makes per-row pause and
cancel trivial. FFS's own window is untouched, so nothing regresses.

What landed (`offload/mac/`, ~2,000 lines of Swift + a 429-line C ABI):

- **Sources | Transfers | Destinations**, dark, single window — drag a card or drive onto
  either side pane, or use the dashed add tile. Capacity on every tile.
- One row per `source → destination`: `source → dest · subfolder`, colour-coded bar
  (blue copying, light-blue verifying, green verified, red failed, grey cancelled),
  current file, moving-average rate, ETA, and per-row pause/cancel.
- **Add N Transfers** creates the `sources × destinations` product; **Paused** stops all.
- **Fit check.** Each source is measured with the engine's own walk and junk rules; a
  destination that cannot hold it is ringed amber, labelled `Short by 58.8 GB`, and the
  Add button refuses to start anything. An ejected destination reads `Not connected`.
  An unmeasurable source is *not* refused — it warns that fit was not checked.
- Live progress from the engine (`Options::onProgress`, throttled to ~10 Hz) rather than
  polling the filesystem.
- Writes `.ffsoffload.mhl` at each destination plus `report.csv`/`report.json` into
  `~/Pictures/Offload Reports/<date> <source> to <dest>/`.

**Exit criteria:** met. The window is driven by real mouse events (`make ui-test`, 12/12
checks: tiles select, Add creates one row per destination, per-row pause/resume/cancel
reach the engine), the transfer path verifies 33 files end to end, and the app bundle
builds, launches and stays up. The mouse test caught one real bug on its first run —
folder devices were named after the volume, so every folder read "Macintosh HD".
What is still not exercised: drag & drop from Finder. See the report's limitations.

## 4. Phase 3 — reports and packaging (1–1.5 days)

- `.mhl` (MHL 1.1 XML) at each destination root — the archive-interoperable artefact.
- CSV and JSON logs beside it (our addition; greppable years later).
- HTML report per job: summary, destination table with verification mode and hash
  algorithm, per-file list, failures first. (PDF in v2 via `wxPdfDocument` or skip.)
- `.app` bundle, universal or arm64-only, ad-hoc signing, a proper icon, and a
  README with build steps.

## 5. Phase 4 — hardening (1 day, parallel to 3)

Tests that must exist before this ships:

| Test | Asserts |
|---|---|
| Corrupted destination **must** fail | flip one byte in a copied file → job reports orange, file is `mismatch`, exit code non-zero |
| Truncated destination file | short write is detected by digest, not by size alone |
| Duplicate skip | identical name+size+mtime is skipped, not re-copied |
| Duplicate collision | same name, different size → `file-1.mov`, original untouched |
| Source immutability | source tree's mtimes, sizes and digests unchanged after a run; no writes opened |
| Interrupted copy → resume | kill mid-job, resume, no file is left half-verified, `.part` cleaned up |
| Two destinations, one fails | the good destination still completes and verifies; failure is attributed to the right disk |
| Unplug destination mid-copy | red state, job stops, no crash, partial files marked |

The corruption test is the one that proves the product works; everything else is
hygiene.

---

## 6. Total, and what could blow it up

| Phase | Estimate |
|---|---|
| 0 spike | ✅ done — half a day, 125 lines |
| 1 engine | ✅ done — ~2,300 lines, 100 assertions passing |
| 2 window | ✅ done — ~2,000 lines of Swift over a C ABI, driven by real mouse events in tests |
| 3 reports + packaging | ~0.5 day — CSV/JSON/MHL and the `.app` exist; HTML report and an icon left |
| 4 hardening | 1 day (overlapping) — corruption, dedupe, immutability, multi-destination, pause/cancel tests exist; unplug/truncate cases are untested |
| **Total (option B)** | **~1.5 working days left**, with the platform and product risk retired |

Risks, in order:

1. ~~**Phase 0 comes back ugly.**~~ Retired: it came back clean, 125 lines.
2. **The stale toolchain.** Apple clang 15 / SDK 14.4 on macOS 26 cannot compile
   C++23 at all; the build depends on Homebrew LLVM 23, and linking needs brew's
   libc++ (`-L$(brew --prefix llvm)/lib/c++`). Workable, but it is a dependency to
   document, and a newer Xcode would remove it.
3. **Throughput.** Reading a 400 GB card twice (once to hash, once to verify) doubles
   wall time. Mitigation: hash during the copy (already the design), and use the
   mtime+size MHL shortcut so a second run trusts earlier digests.
4. **Scope creep** — cascading transfers, ASC MHL 2.x, thumbnails, S3. All explicitly
   out of v1.

Not doing, in v1: cascading transfers, ASC MHL 2.x, Clip Awareness, presets as
shareable files, PDF reports, C4 checksums, S3/FTP destinations (the `afs` layer
would give us SFTP nearly free, but it is not a wedding-card problem).

---

## 7. What I need from you next

1. **Try it on a real card and two real drives.** The window and its mouse handling work,
   but every run so far is on synthetic fixtures in `/tmp`. The two things synthetic data cannot tell me are
   whether the ETA stays honest against a spinning archive drive, and whether the pane
   layout survives a card with 3,000 clips.
2. ~~The "fits / does not fit" pre-flight check~~ Done: the window measures the source
   with the engine's own walk and refuses to start on a destination that is short (or
   gone). Verified against a real 60 MB disk image that could not hold a 94.1 MB card.
3. Where the fork should live on disk — this workspace is an unrelated repo (a REA
   checkout), and the FreeFileSync clone is currently a throwaway in `/tmp`.
   I would put it at `~/dev/ffs-offload` unless you say otherwise.

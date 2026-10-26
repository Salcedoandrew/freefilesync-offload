# Changelog

Everything this fork adds on top of **FreeFileSync v14.12** (upstream commit
`53143e29`, 2026-09-09). Newest first. All work landed on 2026-10-25.

Two things it does not do: the FreeFileSync GUI still does not build on macOS, and
nothing here is upstreamed (upstream is a read-only mirror).

---

## 0.4.0 — 2026-10-25

**Mouse-driven verification, and the bug it found.**

### Added
- `--ui-serve <state.json>`: opens the real window on a fixture and publishes its click
  targets and live state every 150 ms.
- `clickdriver`: posts genuine `CGEvent` clicks at the published points and asserts what
  changed in that state. Driven by `make ui-test`.
- Test anchors (`.testAnchor` → `AnchorFrames`) on every clickable view, so the driver
  never guesses a control's position.

### Fixed
- **Device tiles were named after the volume, not the folder.** Every folder on the boot
  disk appeared as `Macintosh HD`. A folder is now named after itself; only a mount point
  takes the volume's name.

### Verified
- `12/12 mouse checks pass`: tiles select and deselect, **Add Transfers** creates one row
  per destination, per-row pause/resume/cancel reach the engine, Add with nothing selected
  does nothing, and the window still responds afterwards.
- The same 12 checks pass from a clean `v14.12` clone + patch, so the test is part of the
  reproducible artefact.
- Pause and cancel assertions read the engine's own flags, not the row's label.

### Notes
- SwiftUI ignores synthetic `NSEvent`s handed to its hosting view (proved with a 20-line
  standalone app), so clicks must be real `CGEvent`s. macOS drops those from an untrusted
  process, which is why the test needs Accessibility permission for the app that runs it.
- Still untested: dragging a card in from Finder.

---

## 0.3.1 — 2026-10-25

**Destination fit check.**

### Added
- Each source is measured with the engine's own walk and junk rules
  (`offload::measureSource`, exposed as `offload_source_measure`), so the fit check and the
  copy cannot disagree about what will be copied.
- A destination that cannot hold the source is ringed amber, labelled `Short by 58.8 GB`,
  and blocks **Add Transfer**; free space is re-read at the moment of the decision.
- An ejected destination reads `Not connected` and blocks too.
- A source that cannot be measured warns `Size unknown … — fit not checked` rather than
  refusing, because "unknown" is not the same claim as "doesn't fit".

### Verified
- Refused against a real volume: a 60 MB APFS disk image cannot hold a 94.1 MB card —
  `fit: … is 32 MB short — refusing before writing anything`, exit 1, zero bytes written.
- `11/11` boundary checks (`--fit-test`): exactly fits, one byte short, shortfall
  arithmetic, empty source, unmeasured source, volume reporting nothing, blocking policy.
- `107/107` engine assertions, including `testMeasureSource`: measured bytes and file count
  equal the verified records from a real run, with `.DS_Store` and an AppleDouble sidecar
  present.

---

## 0.3.0 — 2026-10-25

**The Offload window.**

### Added
- Native macOS app (`FreeFileSync/Source/offload/mac/`, SwiftUI + AppKit) with the
  three-pane layout: **Sources | Transfers | Destinations**, dark by default.
- Drag a card or drive onto either pane, or use the dashed add tile. Capacity on every tile.
- One row per `source → destination`: colour-coded bar (blue copying, light-blue verifying,
  green verified, red failed, grey cancelled), current file, moving-average rate, ETA, and
  per-row pause/cancel. **Add N Transfers** builds the `sources × destinations` product;
  **Paused** stops everything.
- `offload/capi.h`: a 429-line C ABI (`start`/`poll`/`pause`/`cancel`/`destroy`) so Swift
  never touches C++. One transfer per thread, so per-row controls are local.
- Live progress from the engine (`Options::onProgress`, throttled to ~10 Hz) instead of
  polling the filesystem.
- Reports: `.ffsoffload.mhl` at each destination root, plus `report.csv`/`report.json` in
  `~/Pictures/Offload Reports/<date> <source> to <dest>/`.
- `.app` bundle, ad-hoc signed, built by `offload/mac/Makefile`; `make screenshot` renders
  the window offscreen for documentation.

### Verified
- Window rendered (1800×1132) and captured from its own view hierarchy.
- 33 files to a destination: every one `verified`, MHL and CSV on disk.
- 700 MB clip: 0.66 s wall, 19.1 MB peak RSS.
- App bundle launches and stays up.

### Notes
- The window is a native shell, not wxWidgets: the reference layout is custom-drawn, and
  porting FreeFileSync's `ui/` tree to macOS was the one genuinely large unknown left.

---

## 0.2.1 — 2026-10-25

**Hash choice and pause/resume.**

### Added
- `--hash xxh64|md5|sha1` (default XXH64), with the algorithm carried into the CSV header,
  the JSON field and the MHL element name (`<xxh64>`, `<md5>`, `<sha1>`).
- MD5 and SHA-1 streaming implementations verified against `md5 -q` and `shasum -a 1`,
  including the 55/56- and 63/64-byte padding boundaries.
- Pause (`SIGUSR1`) and cancel (`SIGINT`) in the CLI; both now also drive the window.

### Fixed
- **Stale objects.** The makefile had no header dependencies, so editing `job.h` left
  objects compiled against the old `JobReport` layout — the real cause of an intermittent
  segfault that looked like a memory bug.
- Unrolled MD5/SHA-1 round groups: MD5 went from 16.8 s to 11.3 s on the benchmark.

### Verified
- All digests match independent ground truth; pause wrote 0 bytes during 2 s; cancel
  finished the file in flight and exited 1.

---

## 0.2.0 — 2026-10-25

**The headless Offload engine.**

### Added
- `offload::runJob()`: scan the source (`zen::traverseFolder`, junk-filtered) → one worker
  per destination → copy → read-back verify → per-file records → report.
- Streaming XXH64 (4.55 GiB/s measured), hashed while copying so the source is read once.
- Publish through FreeFileSync's own `AFS::getOutputStream(...)->finalize()` — the
  `.ffs_tmp`-then-rename path, so a half-written file is never visible.
- Duplicate Detect (name + size + mtime): identical files are skipped, a clashing name
  becomes `name-1.ext` — never an overwrite.
- `--verify size|hash`, `--reverify`, `--dry-run`, `--report DIR`, `--mhl`.
- Reports: CSV, JSON, and MHL 1.1 (Media Hash List) at each destination root.
- 100 assertions, including the corrupted-destination case and source immutability.

### Verified
- 221 files to two destinations: every one `verified`, `.mhl` at both roots, both reports
  beside the log, card unchanged afterwards.
- One byte flipped in a copied file: 1 mismatch, both digests named, exit 1.

---

## 0.1.0 — 2026-10-25

**macOS engine port.**

### Added
- `FreeFileSync/Source/Makefile.macos`: builds `libffsengine.a` (zen, afs, base/binary,
  base/structures) and the Offload targets without GTK. glib/gio/gobject, CoreFoundation
  and wx's translation shim are the real dependencies.
- `zen/mac_compat.h`, force-included before `zen/i18n.h` (libc++ does not pull the std
  headers libstdc++ pulled transitively).
- macOS substitutes for two Linux-only pieces: `macos/icon_loader_stub.cpp` (upstream loads
  GTK icon themes) and `macos/afs_unported_stub.cpp`.

### Verified
- 28 files swept in `zen/` + `afs/`: 25 clean, 3 excluded by design (inotify watcher, and
  the FTP/SFTP/GDrive transports, which an offload job does not need). `base/`: 13 of 14.
- The port is 125 added lines across 14 files, every one inside `#if defined(__APPLE__)`,
  so upstream's Linux build is untouched.
- `libffsengine.a` links; a smoke binary copied 64 MiB at 1.26 GiB/s, verified IDENTICAL,
  then detected a 4-byte corruption as MISMATCH (exit 1).

---

## Known limitations

- FreeFileSync's own GUI (`ui/`) does not build on macOS. This fork carries the engine and
  Offload only.
- Writes have not been tried against a real card or real drives; everything so far is
  synthetic fixtures.
- The build needs Homebrew LLVM (the Apple clang on this machine rejects `-std=c++23`).
- Free space shrinking mid-transfer surfaces as an ordinary write error, not a pre-flight.
- No Finder drag-and-drop test; no resume journal for a killed job (Duplicate Detect and
  `--reverify` cover the practical cases).

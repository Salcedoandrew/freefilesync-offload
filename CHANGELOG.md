# Changelog

Everything this fork adds on top of **FreeFileSync v14.12** (upstream commit
`53143e29`, 2026-09-09). Newest first. All work landed on 2026-10-25.

Two things it does not do: the FreeFileSync GUI still does not build on macOS, and
nothing here is upstreamed (upstream is a read-only mirror).

---

## 0.5.0 — unreleased

**Check for Updates, and an update that actually replaces the app.**

### Added
- **A menu bar.** There was none, so `⌘Q` did nothing and there was nowhere to put this.
  About, Check for Updates…, Hide, Quit.
- `Updater.swift`: reads `releases/latest`, compares versions component-wise
  (`0.10.0` > `0.9.0`, `1.0` == `1.0.0`), downloads the release zip and verifies it against
  the release's `SHA256SUMS.txt`, unpacks it with `ditto`, and requires the bundle id, the
  tagged version and a passing `codesign --verify --deep --strict` before going further.
- `UpdateInstaller`: the swap script waits for the app to quit, moves the old bundle aside,
  moves the new one in, re-verifies it, deletes the old bundle, clears quarantine and
  reopens. On any failure it restores the old bundle and reopens that. One line per attempt
  in `~/Library/Application Support/Offload/update/last-swap.log`; the script deletes itself.
- Refusals: transfers still running, a Gatekeeper-translocated copy (read-only), and a folder
  the user cannot write to.
- Footer chip with the version to install and download progress; `--check-update`,
  `--update-test` and `--update-selftest <feed>` for the terminal.
- `make zip` now writes `SHA256SUMS.txt`, which is what the updater checks against.

### Fixed
- **A failed source scan no longer reports "Completed".** The engine used to swallow a
  scan error and hand the window a report that looked like success; the window now fails
  the transfer and shows the reason, and the CLI prints it too. The same hole in
  `--self-test` is fixed.
- **Copies now publish atomically.** Each file is written to a temporary `.ffs_tmp` name
  and renamed into place only once its hash is verified, so a crash mid-copy can no
  longer leave a partial file at its real name. Stale temp files are cleaned up at the
  start of a run and skipped by the scanner.

### Verified
- `38/38 update checks pass` (`--update-test`): version ordering incl. `0.10.0` > `0.9.0` and
  `1.0` == `1.0.0`; feed decoding; pre-release and zip-less releases refused; `shasum` output
  parsed; SHA-256 equal to `shasum -a 256` for a 3 MB file; the script's own contents; a real
  swap of two bundles on disk; and a swap of a deliberately unsigned bundle that **restores
  the old version and leaves an app in place**.
- End to end against a local feed: 0.4.0 → 0.4.1, 6.6 MB, checksum matched, staged, spawned,
  swapped. The replaced bundle is 0.4.1, the old one is gone, `codesign --verify` passes, and
  it runs: `11/11 fit checks` with 22 libraries loading from its own `Contents/Frameworks`.
- A zip with one byte appended was refused: `checksum mismatch … Nothing was replaced`, exit 3,
  installed version still 0.4.0, no backup left behind.
- Live check against GitHub: `current 0.4.0, latest 0.4.0 — up to date`.

### Notes
- **Integrity, not authenticity.** Ad-hoc signing gives the updater nothing to verify identity
  against, so a compromised release would pass. Notarization or a signed appcast is the fix.
- **v0.4.0 has no updater**, so its upgrade is manual; 0.5.0 onwards can update itself.
- Untested: a swap interrupted by a power cut (the script only restores on its own failures),
  and an install owned by another admin account (refused by the writability check).

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

### Packaged
- **`make bundle` / `bundle-dylibs.sh`**: the binary linked Homebrew's wx, glib, gettext,
  pcre2 and LLVM `libc++` by absolute path, so the app only ran on the machine that built
  it. The script walks the load commands, copies 25 dylibs into `Contents/Frameworks`,
  rewrites them to `@rpath`, and re-signs inside-out. wx publishes each library under three
  symlink names and images reference different links in the chain, so copies are deduped by
  file (`stat -L`, since macOS `stat` is `lstat` by default) — without that the same code
  was bundled twice under two install names.
- **`make zip`**: `Offload-0.4.0-macos-arm64.zip`, 6.5 MB, containing the app, a
  `HOW-TO-OPEN.txt` and nothing else.
- `Info.plist` said `0.2.0` while the changelog shipped 0.4.0; it now says 0.4.0.

### Verified on the packaged build
- Unzipped to `/tmp` and run from there: `11/11 fit checks`, `self-test PASS` (3 files,
  two destinations, 3/3 verified on each), window renders 1800×1132.
- `DYLD_PRINT_LIBRARIES` shows every image loading from `Offload.app/Contents/Frameworks`;
  `otool -L` across the bundle has no `/opt/homebrew` or `/tmp` path left.
- `codesign --verify --deep --strict`: valid; `Signature=adhoc`, `TeamIdentifier=not set`.
- `spctl -a` on a quarantined copy: **rejected** — expected for an ad-hoc signature with no
  Developer ID and no notarization. The archive says so in `HOW-TO-OPEN.txt`, and
  right-click → Open clears it. Notarizing is the only thing that removes the prompt, and
  that needs an Apple Developer Program membership, which this machine has no certificate
  for (`security find-identity -v -p codesigning` → 0 valid identities).
- arm64 only: the engine and its Homebrew dependencies are arm64, so a universal build
  would need an x86_64 dependency set that does not exist here.

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

# Changelog

Everything this fork adds on top of **FreeFileSync v14.12** (upstream commit
`53143e29`, 2026-09-09). Newest first. All work landed on 2026-10-25 and 2026-10-26.

Every date here is the calendar day a release went out, in America/Los_Angeles, so the
same date as the release page read from here. GitHub timestamps its releases in UTC,
which puts the evening releases of the 25th on the 26th.

Two things it does not do: the FreeFileSync GUI still does not build on macOS, and
nothing here is upstreamed (upstream is a read-only mirror).

---

## 0.10.3 — 2026-10-26

**The README now carries the upstream feature list, and About Offload has the email.**

### Added
- A new "Upstream features" section in the README: all 76 things FreeFileSync 14.12 does, each with a stable ID, where it lives in the binary, its config keys and how strongly the claim is evidenced, plus a worksheet for comparing another app against it. Read out of the shipped binary by static analysis, so the fork can be compared against upstream without re-running the analysis.
- The About panel now shows gcastroandrew@gmail.com, as a mailto link under the website.

### Fixed
- The Credits.rtf added with the About box never displayed: the panel's credits are built in code (Entry.swift), which overrides the bundle file. It is removed, and the Makefile no longer copies it.

---

## 0.10.2 — 2026-10-26

**About Offload says what the app is, and who made it.**

### Added
- The About panel was the stock one, so it said "Offload" and a version number and
  stopped there. It now carries the author, a link to heartfeltmedia.co, the sentence
  the README leads with — copies to every destination at once, hashes every byte, reads
  each copy back, leaves a report you can check a year later — and a copyright line.

### Notes
- The colours are the system label and link colours, so the panel reads correctly if it
  is ever shown in light mode; the window itself stays dark.

---

## 0.10.1 — 2026-10-26

**The reports stop naming a version that is three releases old.**

### Fixed
- `report.json`, `report.csv` and the MHL each carry the engine's version, and the
  engine had `0.7.0` typed into it as a constant. Every report this fork has written
  since 0.7.0 therefore named the wrong version — the kind of thing nobody notices
  until they open one a year later and try to match it to a release. The version now
  comes from the build, read out of the bundle's `Info.plist`, which is already the
  single source of truth the release workflow reads. One version bump, one place, and
  no constant left to remember.

### Notes
- The MHL `<version>` element and the JSON `"tool": "ffsoffload <version>"` field
  both change with this release. Anything keyed on the old string now sees the real
  version instead of a fossil.
- A build that cannot read the plist stamps `0.0.0-unknown` rather than inventing a
  plausible-looking version.

---

## 0.10.0 — 2026-10-26

**A card can be copied without its proxies, and filed where you will look for it.**

### Added
- **Skip patterns.** A Skip field in the Transfers panel takes patterns like
  `*.LRV | *.THM | MISC/`, and the engine applies them inside the one walk that plans
  the copy — so the fit check beside each destination counts what will actually be
  copied, not what the card is holding. `*` and `?` are wildcards, a trailing `/`
  prunes a whole folder, and matching ignores case, because cards mix `IMG_1.THM` and
  `img_1.thm` freely. On the command line it is `--exclude`.
- **A Subfolder field with date macros.** `%Date%` (2026-10-26), `%Year%`, `%Month%`,
  `%Day%` and `%Card%`, with `/` nesting folders, so a shoot files itself under
  `2026/10/26` without being typed. Blank keeps the card's own name, which is what it
  did before the field existed.
- **A fuller `--json` summary.** It now carries the totals a script should not have to
  add up itself: verified, written, skipped, mismatch, failed, broken, bytes, the
  report path, the verify mode and hash, and the skip patterns that shaped the run.
- **An app icon**, so the app is not a blank page in the Dock and in Finder.

### Notes
- A Skip field that matches every file is refused rather than run. "Copied 0 files,
  OK" is exactly how a card gets formatted for nothing, so the job fails by name.
- The patterns are captured when a row is queued, not when Transfer is pressed:
  editing the field afterwards cannot change a plan already pressed Add on, and the
  report records the patterns the run really used.
- Typing in Skip re-walks the sources once the typing stops, so the fit check follows
  the field instead of answering a question the next run will not ask.
- The matcher has no wx or glib dependency, so it builds and runs on its own:
  `make -f Makefile.macos filter-test` runs 29 checks in about a second, and CI runs
  them. The engine suite gained a Skip section that drives the real job, including
  that the fit check and the copy agree file for file.
- The updater's feed decision moved out of `performCheck` into `Updater.decision`, and
  is now covered directly instead of being assumed: an 0.8.1 copy is offered the
  feed's 0.9.0 with its archive and checksum, a current copy is told it is current, a
  feed that lags behind never offers a downgrade, and 0.10.0 beats 0.9.0.

---

## 0.9.0 — 2026-10-26

**A finished run can be swept out of the window in one click.**

### Added
- **Clear Finished.** Completed rows used to sit in the Transfers list for the life of the
  window, one cross each. A small Clear Finished button now appears in the Transfers
  header once anything is over, and removes the rows that completed or were cancelled.
  A failed row stays behind on purpose: its text is the only place that names which files
  failed, so a button that quietly erased that after a bad card would be the wrong
  default; the row's own cross still clears it.

### Notes
- The mouse test now covers the queue and the clear: a queued row has no job to pause, a
  run started under the Pause switch comes up paused, and Clear Finished drops the
  cancelled row while keeping the paused one.

## 0.8.1 — 2026-10-26

**Documentation only: no engine or window code changed in this release.**

### Fixed
- **The release table was four versions behind.** It stopped at v0.4.0, so 0.5.0 through
  0.8.0 were missing from the one place the README lists the releases; each row now links
  its own tag.
- **Two dates disagreed** between that table and this file. Both now date a release by the
  local day it went out, and say so, because GitHub stamps releases in UTC and pushes an
  evening release onto the following day.

## 0.8.0 — 2026-10-26

**Add queues the copies, Transfer starts them, and the run ends with a receipt that says
what moved and how fast.**

### Added
- **A Transfer button.** Add now queues rows as `Ready`, with no engine job behind them;
  the footer grows a Transfer button that starts them and disappears once nothing is
  waiting. It takes the accent while rows are queued, with Add stepping back to a neutral
  fill, so there is only ever one obvious thing to press.
- **A confirmation window.** When every row a run started has finished, a sheet reports
  the files and bytes moved, the rate the run sustained, and one line per destination
  with its own count, rate and report folder. The rate is bytes over the engine's own
  elapsed time, and the run's duration is its slowest row because rows copy side by side.
  Damaged files turn the headline amber and are counted separately.
- `--render summary` draws that window offscreen with a sample run, so it can be
  screenshotted without copying anything.

### Fixed
- **The download's own instructions named the wrong version.** `HOW-TO-OPEN.txt` was
  copied verbatim into the archive, so the 0.7.0 zip shipped a note whose header,
  checksum command and limitations heading all still said 0.4.0. The note now carries
  `@VERSION@` and `@ARCH@` placeholders that `make zip` fills from `Info.plist` and
  `uname`, and the build refuses to package a note that does not name the archive it is
  sitting in.

### Notes
- A queued row has no Pause button, because there is no job to hold back; cancelling one
  retires the plan instead of leaving it to start on the next Transfer press. The
  updater's "transfers are running" check ignores queued rows, since nothing is written.
- A release is now cut only after CI goes green, and a run whose newest commit is not the
  version bump is refused rather than published without those commits.

## 0.7.0 — 2026-10-25

**A damaged photo now fails the run instead of disappearing into it, the updater cannot
install a release it cannot check, and every transfer row says how fast it is going and
how much has moved.**

### Added
- **Transfer stats.** Each row reports bytes/sec, files/sec, `n of m files`, bytes done
  of total and the ETA while it runs, then the counts and elapsed time when it finishes.
  Pause/resume is a labelled **Pause**/**Continue** pill instead of a bare glyph, and a
  paused row drops the rate and the ETA rather than leaving frozen numbers on screen.
- A test that drives the updater's checksum gate in both directions: a release with no
  sums asset is refused, one with a sums asset passes and hands its file back.

### Fixed
- **A damaged source file no longer lets the job claim success.** It is still copied
  faithfully and still carries a plain-language reason in the report, but `allVerified()`
  now returns false when a destination counted a damaged file: the CLI says how many
  were damaged and exits 1, and the window names them instead of blaming the source scan.
  The exit code is what a script checks before reformatting a card, so one corrupt JPEG
  now stops that.
- **A truncated report reads as incomplete.** `DestReport` carries `expectedFiles`, pinned
  from the source count before any worker starts, and `allVerified()` requires exactly
  that many records — a destination that dies mid-run no longer looks finished. Covered
  by the new `testTruncatedReport`.
- **The updater refuses to install without a checksum.** A release that published no
  `SHA256SUMS.txt` used to skip verification entirely and install anyway; it is now
  refused with `checksumMissing`, and the checksum asset's HTTP status is checked
  instead of ignored.
- **The C ABI no longer hands Swift pointers into live strings.** `offload_job_current_file`,
  `offload_job_error` and `offload_job_report_csv` copy into a per-thread buffer while the
  lock is held; the worker reassigning `currentFile` could previously free the buffer while
  Swift was reading it.

### Notes
- The app's self-test fixture now writes a real CR3 header in front of its random payload.
  Random bytes under a `.CR3` name are a damaged file, and that fixture exists to exercise
  the transfer path.
- `--allow-broken` is deliberately not here: a card known to hold a bad file can only be
  recorded as failed for now.

## 0.6.0 — 2026-10-25

**A hash cache, so re-checking a card does not re-hash everything, and a
broken-file check, so a corrupt photo is flagged instead of silently filed.**

### Added
- **Hash cache.** Each destination now keeps a `.ffsoffload-cache` file at its
  root: the hash of every file verified there, keyed by file name, size and
  modification date, with the algorithm recorded alongside. A second run
  skips what is already there *and* confirms it from the cache instead of
  trusting name, size and date alone. `--reverify` never takes a hash from
  the cache: it always re-hashes every file, so a content change that keeps
  size and date is still caught, and a mismatch drops the stale entry so it
  can never be trusted again. The cache format is versioned, so a future
  format change never silently trusts old entries, and the cache file itself
  is never copied as data. A cache that cannot be saved is a warning, never
  a failure: the next run just re-hashes.
- **Broken-file check.** During the source scan, photo and RAW files are
  judged at the header level only — JPEG (SOI marker and the marker after it),
  PNG (signature, IHDR, and the IEND chunk at the end), TIFF (byte order,
  magic 42, first directory inside the file), CR3 (`ftyp` box branded `crx`),
  NEF and ARW (TIFF headers) — so the scan stays fast with no full decode.
  A file that fails is flagged `broken` in the CSV/JSON report with a
  plain-language reason and a warning on the terminal, and it is never counted
  as verified-good. It is still copied faithfully and hash-verified like any
  other file: losing a corrupt photo would be worse than copying it.
- The summary line now shows the broken count and the cache hits, and the
  per-file report gains `broken`, `broken_reason` and `hash_confirmed`
  columns; the JSON report gains a `broken` count and cache hit/miss counts.

### Notes
- **Same size and date, different bytes, is invisible to the cache — but not
  to `--reverify`.** If a file on a destination changes content without its
  size or modification date moving, the already-present check still confirms
  it from the cache: that is the same trust Duplicate Detect already places
  in the date, and the price of not re-hashing every file on every run.
  `--reverify` always re-hashes, so it catches the change and drops the
  stale entry; delete `.ffsoffload-cache` on a destination to force a full
  re-hash of the already-present check too.
- A dry run reads the cache but never writes one.

## 0.5.0 — 2026-10-25

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

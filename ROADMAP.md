# Roadmap

A running audit log for this fork: what must be fixed, and what might be worth
building. One entry per audit, newest first.

Each entry records the commit it judged, what was actually checked, and what could
not be checked. Anything here is a claim about one commit, not a standing promise —
if an entry and the source disagree, the source is right.

| Field | Meaning |
|---|---|
| **Commit** | the `HEAD` the audit read |
| **Checked** | how the claims were reached — static reading, or a build and tests |
| **P0** | fix before the next release: a wrong result, a lost backup, or a security hole |
| **P1** | fix soon: a real defect or a documented promise the code does not keep |
| **Later** | worth building, not blocking anything |

---

## 2026-10-26 — audit 1

**Commit** `81cb7b9` (0.6.0) · **Checked** static reading only, see
[limits](#what-this-audit-could-not-check).

The engine's core is in better shape than its age suggests: the hashing is checked
against real `xxhsum`/`shasum` vectors including the 55/56 and 63/64 padding
boundaries, the publish path is a genuine temp-then-rename with no second temp hidden
inside `finalize()`, a failed source scan is a failure rather than an empty success,
and the fit check and the copy share one walk so they cannot disagree. The nine
problems below are mostly at the seams — the C ABI, the updater, and what the word
"verified" is allowed to mean — which is where seams usually are.

### P0 — must fix

**1. The C ABI hands Swift pointers into `std::string`s that are still changing.**
`offload_job_current_file`, `offload_job_error` and `offload_job_report_csv`
(`offload/capi.cpp:277`, `:286`, `:295`) return `job->currentFile.c_str()` after the
`lock_guard` has gone out of scope. The worker thread keeps reassigning
`currentFile` in `publishProgress` at up to 10 Hz, and
`Engine.swift:38-51` immediately does `String(cString:)` on the pointer it was given.
A reassignment can free the buffer mid-read: a use-after-free that shows up as a
garbled file name at best and a crash at worst.

The fix is already in this file for a different reason — `g_measureError` is
`thread_local` and copied under lock (`capi.cpp:124`). Do the same for all three:
copy into a `thread_local std::string` while holding `mu`, return that. The API
stays `const char*`, the Swift side does not change.

**2. The updater installs without verifying anything if the release has no
`SHA256SUMS.txt`.** `Updater.swift:540` is `if let sumsAsset = release.sums` — when
the asset is absent the whole checksum block is skipped and the install proceeds.
`UpdateError.checksumMissing` exists (`:546`) but is only thrown when the file is
published *and* lacks an entry for the zip. The README promises the opposite: "The
zip is downloaded and its SHA-256 compared against the release's `SHA256SUMS.txt`. A
single byte of difference stops the update there."

This is the one code path in the project that replaces the installed app. Make a
missing sums file a hard refusal — `guard let sumsAsset = release.sums else { throw
UpdateError.checksumMissing }` — and keep the existing mismatch and missing-entry
refusals. Note what this does *not* buy: with an ad-hoc signature the checksum still
proves only that the bytes arrived intact, not who wrote them (see item 14 below).

**3. "OK: every file verified" can print while broken files are in the report.**
`tally()` deliberately refuses to count a broken file as verified (`engine.cpp:644`,
the `if (!f.broken)`), but `allVerified()` (`:649`) never looks at `broken` at all, so
a card with a truncated JPEG copies, reports, and exits — while `main.cpp:313` prints
a sentence that is not true and any script wrapping the CLI sees `0`.

The intent per the changelog is that a broken file is "still copied faithfully", and
losing a corrupt photo would be worse than copying it — that part is right and should
stay. What is wrong is that the *job* then claims full verification. Pick one and
make it explicit in the same commit:

- a broken file makes `allVerified()` false and the exit code non-zero, with a
  `--allow-broken` escape hatch for automation that wants to proceed anyway; or
- the broken count is excluded from the promise, and the summary line stops saying
  "every file verified" when the count is above zero.

The first is safer for a tool whose whole point is a report you can trust a year
later; the second is a one-line wording change. Either way the current state is a
contradiction, and the test at `tests.cpp:758` locks it in as intended behaviour.

**4. A truncated report reads as success.** `allVerified()` counts failures; it never
counts how many files it expected. In `runJob`'s worker catch (`engine.cpp:777`) a
destination that throws mid-run only gets a failure record `if (d.files.empty())`, so
a destination that managed 400 of 1,280 files and then hit an exception reports 400
records, zero failures, and `ok == true`. The window goes green and the card can be
reformatted.

Today this is latent — the per-file paths catch `FileError` themselves, so the throw
has to come from `traverseFolder`, `zen::utfTo` or `bad_alloc` — but "latent" is the
wrong standard for the one check that stands between a card and a reformat. Give
`allVerified()` the expected file count (it already has `totalFiles()`, and the
source list is fixed before any worker starts) and require every destination to
account for all of it, and/or always append a terminal failure record in that catch.

### P1 — fix soon

**5. The new code compiles with every warning switched off.**
`Makefile.macos:30` passes `-Wno-everything` to the whole build. That was a
reasonable price for dragging 28 files of upstream `zen`/`afs` through a port; it is
the wrong price for `offload/*.cpp`, which is this fork's own 3,200 lines of C++
(plus 1,000 of tests) and its only real asset. The cost is visible already:
`engine.cpp:644` is a statement whose body is `;` — a leftover from when verified
bytes were counted separately — and it compiles silently.

Split the flags: keep `-Wno-everything` on the `zen_`, `afs_` and `base_` rules, and
give `offload_` `-Wall -Wextra` (minus the two or three warnings upstream headers
force). Delete the dead statement while you are there.

**6. The binary targets macOS 12 but the bundle requires 13.**
`Makefile.macos:27` builds with `-mmacosx-version-min=12.0`; `Info.plist` says
`LSMinimumSystemVersion 13.0` and the README says macOS 13+. The plist gate means
nothing actually runs on 12 today, so this is not a shipped bug — but it means the
compiler is allowed to emit for a deployment target the product does not support, and
one stray API away from a launch crash that no test would catch. Align the flag to
`13.0`.

**7. Every job walks the destination tree twice, and the first walk is not the one it
needs.** `runDestination` calls `removeStaleTemps` (`engine.cpp:343`) and then
`scanTree` over the same root (`:351`). Both are full recursive traversals, and the
temp cleanup exists purely to stop a leftover `name.ffs_tmp` from failing an
`O_EXCL` open. On the 6 TB archive drive this tool is meant for, that is two full
directory walks before the first byte moves — the first thing the user waits for, and
it scales with everything else on the drive rather than with the card.

Fold the temp removal into the scan that is already happening (delete on match), and
scope it to the job's subfolder — `dest/<cardname>` — instead of the destination root.
As written it also deletes `*.ffs_tmp` files belonging to anything else living at that
root, which is only fine while the destination is a dedicated drive.

**8. Adding a card that is already listed re-measures the wrong device.**
`Model.swift:392` does `if let device = sources.last { startMeasurement(of: device.id) }`
unconditionally. `add()` returns early when the path is already in the list, so
`last` is then some *other* source, which gets kicked into a fresh measurement — and
the card just dropped on the window is not measured at all. Harmless-ish (the stale
re-measure settles, and `remeasureStaleSources` will catch up within 30 s), but the
fit check is the feature the README leads with; it should not have a path where a
newly added source sits in `.measuring` while a different one is re-walked. Have
`add()` return the id it added or found, and measure that.

**9. Small things, same pass.**
The report directory is stamped to the second and keyed on source→dest
(`Model.swift`, `stampFormatter`), so two runs of the same pair started within a
second overwrite each other's `report.csv`. While in the README: the badge and cost
table both still say "107 engine assertions" while `offload/tests.cpp` now holds 197
`check()` calls, so the shop window is describing the 0.4.0 suite. Regenerate the
number rather than hand-editing it a third time.

### What this audit could not check

No build, no tests, no app. This host is Linux x86_64 with no clang, no `swiftc`, no
glib and no wxWidgets, and the engine needs Homebrew LLVM, `wxString` and Cocoa — so
every claim above comes from reading the source at `81cb7b9`, not from running it.
The 107 engine assertions, the fit test, the update tests and the mouse test were not
executed here and are reported as unverified, not as passing. CI (`.github/workflows/ci.yml`,
`macos-latest`) is the only place they run.

Specifically untested in the suite, in the order I would close them — these are the
gaps the plan's Phase 4 table still lists:

| Gap | Why it matters |
|---|---|
| Destination unplugged mid-copy | The red state, the partial file and the "no crash" claim are all unexercised. This is a real camera-card failure mode. |
| Destination fills mid-job | The fit check makes it unlikely, not impossible: another process writes too, and the free-space number is up to two seconds stale. |
| Truncated destination file | Corruption tests flip a byte; they do not test a short write caught by digest rather than size. |
| A 3,000-clip card | Every run so far is a synthetic fixture; the pane layout and the 10 Hz progress path have never seen a real card's file count. |
| A spin-up archive drive | ETA honesty and throughput against something slower than internal SSD. |

### Later — worth building

Ordered by what I would do next, not by what is most fun.

**10. Surface the report in the window.** `Transfer.reportPath` is published
(`Model.swift:177`) and the UI never reads it — the CSV/JSON land in
`~/Pictures/Offload Reports/…` and the only way to find them is knowing the path. A
"Reveal report" button on a finished row, and an expandable list of the files that
failed, would close the gap between "leaves a report you can check a year later" and
what a user can actually do at 2 a.m. Small: a button over an existing string.

**11. Expose verification mode and hash algorithm in the GUI.** The CLI takes
`--verify size` and `--hash md5|sha1`; `EngineJob` hardcodes
`OFFLOAD_VERIFY_HASH` and XXH64 (see `Engine.swift` defaults, which no view ever
overrides). MD5 is what a lot of archive/MHL tooling expects to see, and a size-only
pass is the right choice for a fast throwaway copy. Two popups, no engine work.

**12. Retry a file once before calling it failed.** A single `EIO` from a marginal
USB cable currently fails that file and exits non-zero, which on a 92 GB card means
starting over. One retry with a short backoff, counted in the report as
`retried`, converts most transient failures into a completed job. Bounded, visible,
and it does not weaken the verification — the retried file is still hashed and read
back.

**13. A resume journal.** The last item still open from the original Phase 1 list
(`docs/offload/plan.md`): `.offload-state.json` per destination so a killed job
resumes without re-hashing what it already proved. The hash cache (0.6.0) covers the
`--reverify` and Duplicate Detect cases; what it does not cover is the middle of a
large file. "No resume journal" is already listed honestly in the README's Limits,
and it is the largest remaining gap between this and a tool you would trust
unattended overnight.

**14. Make the update feed prove authorship, not just integrity.** This is the
honest hole the README already names: ad-hoc signing plus a checksum from the same
release proves the bytes arrived intact, not who published them. Two routes —
notarization with a Developer ID (also removes the Gatekeeper right-click on first
run, and the `xattr -d` line in the README) or a signed appcast with a public key
compiled into the app. Notarization needs an Apple Developer Program membership;
the signed appcast does not, and would be my first choice given that constraint.

**15. A sanitizer job in CI.** A nightly `-fsanitize=address,undefined` engine run
and a thread-sanitizer run over the C ABI would have caught P0-1 the day it was
written. Cheap: one workflow, one extra flag in the `offload_` rules, no new deps.

**16. MHL interoperability pass.** The report is MHL 1.1-shaped and lives at the
destination root as `.ffsoffload.mhl` — a dotfile, which the spec's own convention
(`ROOT.mhl`, named for the card) does not use, and hidden files are the first thing
some ingest tools skip. Validate the output against a real MHL checker once
(Pomfort/ShotPut/silverstack) and rename if the checker complains. This is the
interoperable artefact in the whole design; it is worth one afternoon of certainty.

**17. Still deferred, still not v1.** Cascading transfers, ASC MHL 2.x, shareable
presets, HTML report and a proper app icon (the last two leftover from Phase 3),
S3/SFTP destinations — the `afs` layer would give SFTP nearly free, but it is not a
wedding-card problem.

### Correction — main moved past this audit

This entry judged `81cb7b9`. While it was being written, two commits landed on main,
so the citations above were re-checked against `16fcdf5` and the line numbers moved
by three. Nothing in P0–P1 changed status:

- **0.6.0 is released** (`16fcdf5 docs: release 0.6.0`), not unreleased as the header
  first said. That matters for priority, not for the findings: the four P0s are in a
  shipped build.
- **`--reverify` no longer trusts the cache** (`068a808`), which removes the caveat
  this entry had in item 9. Re-verify now hashes the destination fresh every time and
  drops a stale entry on mismatch; the size+mtime blind spot survives only in the
  already-present check, which is what the cache is for. Item 9 is now just the report
  timestamp and the stale README numbers.
- The hash cache therefore covers Duplicate Detect only — read item 13's first sentence
  that way.
- P0-1 through P0-4 were re-checked at the new line numbers: `allVerified()` still
  ignores `broken` and still counts no expected files, and `capi.cpp` still returned
  three `c_str()` pointers past the unlock. All four stood at that point.

### Fixed the same day

**P0-1 — done.** `capi.cpp` now copies each of the three strings into a per-thread
buffer while `job->mu` is held and returns that buffer (`copyOut`, the same idea
`g_measureError` already used), so the pointer Swift reads after the unlock can no
longer be freed under it by the worker's next progress tick. The C signature and the
Swift side are unchanged. Not compiled here — same macOS-toolchain gap as the rest of
this entry, so `make -f Makefile.macos` and CI are the check.

One consequence worth knowing: the buffer is per-thread and singular, so a pointer
returned by one getter stays valid only until the *same* thread calls another one.
Swift copies each into a `String` immediately (`Engine.swift:38-51`), which is what
makes that safe today; code that wants to hold two of these pointers at once must copy
first.

**P0-2 — done.** The updater now refuses to install when a release publishes no
`SHA256SUMS.txt` (`guard let sumsAsset = release.sums else { throw .checksumMissing }`),
and the checksum download's HTTP status is checked instead of ignored. A release that
cannot be verified no longer replaces the installed app.

**P0-3 — done, and it changed behaviour on purpose.** Damaged source files still copy
faithfully and still carry a plain-language reason in the report, but they now stop the
job claiming success: `allVerified()` returns false when any destination counted a
damaged file, the CLI prints `N damaged source file(s) were copied but never counted as
verified` and exits 1, and the window says the same in its own words rather than the
misleading "the source could not be read". The consequence is that one corrupt JPEG
makes the run fail — which is the point, because the exit code is what a script checks
before reformatting a card. The `--allow-broken` escape hatch sketched in P0-3 above was
deliberately not added yet: without it, a card known to hold a bad file can only be
recorded as failed.

**P0-4 — done.** `DestReport` now carries `expectedFiles`, pinned from the source count
before any worker starts, and `allVerified()` refuses a report that does not hold exactly
that many records — so a destination that dies mid-run reads as incomplete however clean
its counters look. A new `testTruncatedReport` covers the short report, the complete one,
the broken-file block and the job-level error.

The app's own fixtures had to follow P0-3: `makeUITestFixture` and the CI self-test wrote
random bytes into files named `.CR3`, which the media check reads as damaged. Both now
write a real CR3 header first, so the fixture exercises the transfer path instead of the
damaged-file path.

**Transfer stats — new, not from this audit.** Each row now reports rate, items/sec,
`N of M files`, `bytes of bytes` and the ETA while it runs, then the counts and elapsed
time when it finishes; pause/resume is a labelled `Pause`/`Continue` pill instead of an
8-point glyph. A paused row drops the rate and the ETA rather than leaving frozen numbers
on screen.

**Verified by CI, not here.** This environment has no macOS toolchain, so everything
above was written and reviewed static-only. CI run 12 on `00b7a77` then built all of it:
engine tests (including the new `testTruncatedReport`), the Swift app, the fit test, the
update tests and the app self-test — every step green. That covers compilation and the
engine's behaviour, but not the updater's no-checksum refusal: the update self-test
never performs a real install. So the rule moved out of `performInstall` into
`UpdateInstaller.checksumAsset(for:)` — one owner for it — and the update self-test now
exercises both directions: refused when the release publishes no sums asset, accepted
when it does. What is still untested is a whole install, which needs a network and a
writable bundle.

---

## Format for the next audit

Append a new `## <date> — audit N` section above this one when a new audit runs, with
the commit it judged and what it actually executed. Keep the P0/P1/Later split; move
an item down rather than deleting it when it is fixed, so the log shows what was
believed and when. If an earlier entry turns out to be wrong, say so in the new entry
instead of editing the old one.

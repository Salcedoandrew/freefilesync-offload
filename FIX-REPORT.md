# Offload engine — review fixes

Branch: `fix/offload-review-bugs`. All work is local; nothing was pushed.
`License.txt`, `Info.plist` and the app name/bundle configuration are untouched.

Build + test (macOS, existing toolchain, no sudo, no system installs):

```
cd FreeFileSync/Source
make -f Makefile.macos -j8 all      # libffsengine.a, ffsoffload, ffsoffload_tests, liboffloadapi.a
./ffsoffload_tests
```

**Final test count: 361/361 checks passed, 28 test groups, exit 0.**
The Swift sources that mirror the containment rule also pass `swiftc -typecheck`
(the app is not rebuilt: only the engine and the test binary are required here).

---

## P0-1 — an already-present file counted as success without reading bytes

`engine.cpp` (Duplicate Detect branch of `runDestination`, `allVerified`), `job.h`.

* On a **cache miss** the engine now hashes both sides before skipping: the source
  and the file that is already on the destination. Equal digests → the file is
  reported `alreadyPresent` with `hashConfirmed = true` (and the proof is stored in
  the hash cache, so the next run needs no read). Different digests → the file is
  **not** skipped; it falls through to the existing name-clash rule and is copied
  under the next free name, leaving the differing file untouched. If either side
  cannot be read, the file is reported failed rather than guessed.
* `allVerified()` now walks every record and accepts only `verified`, or
  `alreadyPresent` whose hash is confirmed. A copy that was never read back is
  rejected *when the run promised to read back* (`JobReport::readBackVerify`, set
  from the verify mode), so `--verify size` and `--dry-run` keep exiting 0 instead
  of becoming permanently "failed". A dry run hashes nothing, so its planned skips
  are exempt from the confirmation rule.
* Progress planning counts the two hash reads a cache-miss skip costs.

Regression test: **`testPresentProofNeedsHash`**
(cache dropped + identical bytes → still skipped, `hashConfirmed`, cache rebuilt;
cache dropped + same size/date but different bytes → copied as `…-1.JPG`, original
never overwritten; unit checks that an unconfirmed skip, and a copy from a
read-back run, do not verify).

## P0-2 — `..` collapsed lexically before resolving symlinks

`engine.cpp::canonicalRoot` (+ new `resolveExisting`), `mac/Model.swift::canonicalPath`.

The path is now walked left to right and each component is resolved with
`realpath()` as it is appended, so `cur` is canonical at every step and `..` only
has to drop its last component. `alias/..` (alias → `CARD/DCIM`) therefore
canonicalises to `CARD` instead of the lexically innocent parent, and the
destination is refused. Only the existing prefix is resolved, so a destination that
does not exist yet still canonicalises. The Swift side mirrors this: instead of
`URL.standardized.resolvingSymlinksInPath()` (which collapses `..` first) it walks
the components, resolving each existing prefix with `realpath(3)`.

Regression test: **`testSymlinkEscape`**
(`alias/..` refused, `alias/newdir` refused, ordinary sibling destination still
accepted, `runJob` refuses before writing, card left intact).
Caveat: the Swift mirror has no automated test — there is no Swift test harness in
the tree; it is covered by `swiftc -typecheck` plus review.

## P1-1 — directories deeper than 64 levels silently dropped

`engine.cpp`: `scanTree` takes a `depthError` out-parameter and reports the folder
at which the limit was hit; `runJob` turns that into a job error and runs no
destination at all, and `measureSource` fails (so the fit check refuses the card)
instead of reporting a partial size.

Regression test: **`testDeepNesting`** (70-level tree: job error, no destination
runs, nothing copied, `measureSource` fails with a reason).

## P1-2 — uint64 overflow wrapped when parsing the cache

`cache.cpp`: `parseU64` and `parseI64` reject a digit when
`v > (UINT64_MAX - digit) / 10`, and `parseI64` also range-checks the final value
against `INT64_MAX`/`INT64_MIN` before converting. A wrapping line is dropped like
any other malformed line; the rest of the cache still loads.

Regression test: **`testCacheNumberOverflow`** (2^64 size rejected, overflowing
mtime rejected, good line survives, `UINT64_MAX` itself still accepted and does not
wrap to 0).

## P1-3 — no fsync before rename, verify read could come from the page cache

`zen/file_io.{h,cpp}`: `FileInputPlain` gained an `uncached` constructor overload
that sets `F_NOCACHE` on macOS and `POSIX_FADV_DONTNEED` on Linux at open.
`engine.cpp::hashFile` (the verify/cache-miss reader) uses it, so a verification
read is answered by the device, not by the pages the copy just wrote.
`engine.cpp::copyAndHash` now fsyncs the published temp file before the rename and
fsyncs the parent directory right after it, so neither the data nor the rename can
be lost while the final name already looks verified.

Regression test: **`testVerifyReadBypassesCache`** (one byte changed in place after
the copy → `--reverify` reports the mismatch with the digest of the *changed*
bytes; a direct uncached read returns what the file holds now).

Limitation: fsync ordering itself is not observable from a unit test without a
crash harness; the test covers the readable half (uncached reader, publish
behaviour, no temp left behind). Write-side `F_NOCACHE` is not set: the output fd
belongs to AFS's `OutputStreamNative`, and adding a flag there would change the
abstract AFS interface for every transport (FTP/SFTP/GDrive), which is well outside
this fix. Durability comes from fsync plus the uncached read-back.

## P1-4 — signal handler called `std::atomic::store`

`main.cpp`: the handler now only assigns `volatile sig_atomic_t` flags
(`g_pauseSignal`, `g_cancelSignal`). `startSignalBridge()` copies them into the
atomics the engine polls, once per 50 ms, on a normal thread.

Regression test: **`testCliSignals`** — builds on the real CLI: starts
`./ffsoffload` with 800 files, `SIGUSR1` must produce the pause report, `SIGINT`
must end the run with exit 1 and a `FAILED` summary, every published copy must be
complete and the card untouched. (Skipped, not failed, if `./ffsoffload` is not
built.) The CLI banner is now flushed, which is also what makes startup observable
when output is redirected.

## P2-1 — `FinalizeResult` discarded, mtime failure ignored

`engine.cpp::copyAndHash` keeps the `AFS::FinalizeResult`. If the modification time
could not be set, the temp file is deleted, nothing is renamed, the file is
reported `failed` with the reason, and no cache entry is written. `afs/native.cpp`
already reports `errorModTime` correctly; the engine was the part dropping it.

Regression test: **`testMtimeFailureDeletesTemp`** (all files failed, nothing
published, no temp and no cache left behind, then a clean retry succeeds).
The failure has to be injected: no filesystem we can use refuses `utimensat` on a
file we just created, so `setForceMtimeFailureForTest()` (engine.h) exercises the
decision path. It is `false` in production.

## P2-2 — unlocked worker prints, and cleaning other jobs' live temps

`main.cpp`: one `std::mutex` guards the per-file line printer and the pause/resume
announcements, so two destinations cannot splice a line.
`cache.h/cache.cpp` + `engine.cpp`: publish temps are named
`<name>.<pid>-<random>.ffs_tmp` (`makePublishTempPath`, shared by file copies and
the cache's own atomic write). At startup only temps whose owning pid is gone (or
whose name predates this scheme) are deleted; a temp owned by a live process is
left alone, so a concurrent job's copy is neither corrupted nor blocked.
The cache's own temp uses the same naming rule.

Regression test: **`testForeignLiveTempPreserved`** (a temp owned by a live helper
process survives, a temp owned by a reaped process is cleaned up, only the live one
remains, the copy lands intact; two publish temps for the same file differ and
carry the owning pid). The print lock is not asserted by a test; it is covered by
`testCliSignals`' output and by review.

---

## Nothing else was changed

* No new dependencies, no new build targets, no changes to `License.txt`, the app
  name, `Info.plist` or bundle configuration.
* `--verify size`, `--dry-run` and `--reverify` keep their previous exit semantics
  (see `JobReport::readBackVerify`).
* Consequence of P0-1 worth knowing: the first run against an existing destination
  whose cache was lost now reads both sides of every already-present file. That is
  the price of the proof, and those proofs are written back, so the next run is
  cheap again.

# Phase 1: the verified copy engine

**Result: the offload engine works end to end on this Mac.** A card-shaped source tree
goes to N destinations in parallel, every byte is hashed in both directions, the reports
are written, pause/resume and cancel behave, and the source is provably untouched.

Run 2026-10-25 on arm64 macOS 26.5.2, FreeFileSync `v14.12`, built by
`Makefile.macos` with Homebrew LLVM 23.

What this phase does **not** include: the GUI. `ui/` has never been compiled on macOS
(see [architecture-map.md](architecture-map.md#5-the-macos-problem-the-real-risk-in-this-brief)).
Everything below is the engine plus a CLI that drives it, which is what a window would
call into.

---

## 1. What was built

New code only — no FreeFileSync source file was modified for the engine itself, so the
fork keeps a clean seam between "port fixes" and "offload feature".

| File | Lines | Job |
|---|---|---|
| `offload/hash.h` `.cpp` | 45 + 460 | XXH64, MD5, SHA-1, and a `Hasher` facade |
| `offload/job.h` | 84 | job/report data model, counters, `tally()` |
| `offload/engine.h` `.cpp` | 47 + 505 | scan, parallel copy, verify, dedupe, pause/cancel |
| `offload/report.h` `.cpp` | 22 + 220 | CSV, JSON, MHL 1.1 writers |
| `offload/main.cpp` | 272 | CLI, signals, terminal output |
| `offload/tests.cpp` | 420 | 100 assertions |
| **total** | **~2,300** | |

Saved in this workspace as readable source ([src/](src)) and as a patch against a clean
`v14.12` clone ([patches/full-port-and-offload.patch](patches/full-port-and-offload.patch),
26 files, 2,997 diff lines including the Phase 0 port).

The engine calls FreeFileSync's own `AbstractFileSystem` (`afs/native.cpp`) rather than
`open()/read()/write()` directly, so it inherits the `.ffs_tmp`-then-rename publish that
makes a half-written file impossible to mistake for a finished one.

---

## 2. Verification, and why it is trustworthy

The hash is the whole product; a hasher that is subtly wrong would report corrupt copies
as good. So each algorithm is checked against an independent implementation, and the
values are hard-coded into the test suite:

| Algorithm | Ground truth | Status |
|---|---|---|
| XXH64 | `xxhsum -H1` (seed 0) | 6 vectors + 7 chunk sizes |
| MD5 | `md5 -q` / hashlib | 5 vectors incl. 55/56-byte padding split |
| SHA-1 | `shasum -a 1` | 7 vectors incl. 55/56/65-byte boundaries |

Cross-checked live against the tools on empty, 1-byte, 55-byte, 64-byte and 3 MB files —
all six combinations matched exactly. Streaming in 1, 7, 64, 65 and 4096-byte chunks
produces the same digest as one-shot for all three algorithms.

`ffsoffload_tests` → **100/100 checks pass**, covering:

- copy + read-back verify, and the source tree's contents unchanged afterwards
- Duplicate Detect (name+size+mtime) skips what is already there; a same-name different
  file gets `name-1.ext` instead of being overwritten
- **corruption**: one flipped byte on a destination is caught with both digests named,
  exit 1
- one destination deleting its folder mid-job does not stop the other
- card junk (`.DS_Store`, `._FILE` AppleDouble, `$RECYCLE.BIN`, `.Spotlight-V100`) skipped
- pause: a paused job processes no file; after resume all four files verify and none is
  written twice
- cancel mid-job: the file in flight finishes, the rest are recorded as `cancelled`, and
  the run does not claim success
- `--hash sha1` reaches the CSV header, the JSON field and the MHL element name

## 3. End-to-end acceptance, on the real CLI

Synthetic card: 224 files, 84 MiB, `DCIM/100CANON` plus a `.mov` at the root and
AppleDouble junk deliberately included.

```
1) card -> two destinations
D1: 221 verified, 221 written, 0 already present, 0 mismatch, 0 failed  (83.9 MiB)
D2: 221 verified, 221 written, 0 already present, 0 mismatch, 0 failed  (83.9 MiB)
OK: every file verified                                    exit=0

2) same command again
D1: 0 verified, 0 written, 221 already present ...          exit=0
D2: 0 verified, 0 written, 221 already present ...          exit=0

3) one byte flipped in D2, then --reverify
D2: 220 verified, 0 written, 0 already present, 1 mismatch
  FAIL DCIM/100CANON/IMG_0001.JPG [mismatch] hash differs:
       source 0e6fe1ed96248fb1, destination be62ac9815aef931   exit=1

4) source tree hash before == after                          unchanged
5) .ffsoffload.mhl at each destination root (221 entries), report.csv, report.json
```

Three of the 224 source files were not copied, and that is the junk filter working:
`.DS_Store`, `._clip_0001.mov`, `._IMG_0001.JPG`.

Pause and cancel were tested on the real binary with signals, on 1.5 GiB:

```
pause at t=0.4s (475 MiB written)   -> 0 KiB written during the next 2 s
resume                              -> all 6 files verified, exit 0
SIGINT at t=0.5s                    -> 4 files published+verified, 3 recorded cancelled, exit 1
```

The interrupted run leaves finished files in place and says which ones it did not do.
Nothing is ever deleted, and nothing half-written is visible.

---

## 4. Throughput (measured, 1.5 GiB, warm cache, single destination)

| Work | Time | Effective rate |
|---|---|---|
| copy + XXH64 + read-back XXH64 | 0.91 s | ~3.3 GiB/s end to end |
| copy + MD5 + read-back MD5 | 11.30 s | ~280 MB/s of hashing |
| copy + SHA-1 + read-back SHA-1 | 16.29 s | ~195 MB/s of hashing |
| hash only (`--reverify`, 3.0 GiB hashed) XXH64 | 0.66 s | **4.55 GiB/s** |
| hash only MD5 | 10.98 s | 0.27 GiB/s |
| hash only SHA-1 | 15.93 s | 0.19 GiB/s |

Read that as: **the default path is not the bottleneck.** A 128 GB card takes about
40 s of hashing with XXH64 — the card reader will be the limit, not us. XXH64 is
scalar C here and already runs at 4.5 GiB/s, faster than most source media.

MD5/SHA-1 are hand-written scalar implementations, unrolled into four round groups
(that change alone took MD5 from 16.8 s to 11.3 s and SHA-1 from 19.8 s to 16.3 s on
this benchmark). They are ~4–6x slower than a good scalar implementation and ~15x
slower than CommonCrypto, which uses the ARM SHA extensions. They stay legacy options:
a 128 GB card on SHA-1 costs about 11 minutes of extra CPU. If MD5/SHA-1 ever becomes a
default for someone, the fix is to call `CC_SHA1`/`CC_MD5` behind `#if defined(__APPLE__)`
and keep the scalar path for Linux — the test vectors already pin both to the same
digests, so that swap is safe and measurable.

---

## 5. Decisions worth knowing about

**Streaming hash covers the bytes written.** The digest is computed as the copy runs, and
the read-back hash is computed from the destination file, so "verified" means *the bytes
on disk hash the same as the bytes read from the card*. FreeFileSync has no hashing at
all — its "verify copied files" is a live byte comparison — so this is new code, and it
also means a destination that silently returns different bytes cannot pass.

**Duplicate Detect is name+size+mtime, as Offshoot documents it.** Fast, and wrong in
exactly one case: bit rot that preserves size and date. That is what `--reverify` is for;
it hashes what is already on the destination and never copies.

**A same-name file with different content is never overwritten.** It becomes
`IMG_0001-1.JPG`. Overwriting is not recoverable; a second copy is.

**Pause parks between blocks, not between files.** A pause does not throw away a
partially copied file — the copy continues from the next block after resume.

**One worker per destination.** A slow drive cannot throttle a fast one, and each
destination's failures are isolated in its own report.

---

## 6. What is still missing from the brief

| Brief item | State |
|---|---|
| one-to-many parallel copy | done |
| checksum verify after copy, XXH64 default, MD5/SHA-1 optional | done |
| per-file status, final summary | done (CLI) |
| report per job, CSV **and** MHL, at each destination | done |
| never modify source, skip identical files | done, tested |
| pause / resume / cancel | done in engine + `SIGUSR1` / `SIGINT` |
| transfer queue with per-file progress, speed, ETA | **not done** — engine reports per file; no aggregate progress/ETA model yet |
| single-window GUI, sources left / destinations right / queue below, dark mode | **not done** — `ui/` still unbuilt on macOS |
| screenshots of the new UI | **not done**, blocked on the above |
| build instructions | done for the CLI: `make -f Makefile.macos` (see Phase 0 report) |

So the next two pieces are (a) a progress/ETA model in the engine that a UI can consume
without polling, and (b) the window. (b) is the risky one: it needs either wxWidgets
compiled against this ported engine, or a native AppKit/SwiftUI shell talking to the
engine through a thin C API. That choice is worth making deliberately rather than by
drift, since it decides whether "all existing FreeFileSync features" ever come along.

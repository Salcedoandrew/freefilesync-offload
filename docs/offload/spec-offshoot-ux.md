# Offshoot workflow spec (reconstructed from public docs)

Purpose: a factual description of what Hedge OffShoot does, so we can rebuild the
*workflow* in a FreeFileSync fork without copying any of its code, assets, icons or
branding. Everything below is quoted or derived from Hedge's own public pages
(Oct 2026). Nothing here was observed by running the app.

Legend: **[D]** documented on the cited page. **[I]** my inference — treat as a
hypothesis to validate, not a requirement.

---

## 1. Main window: two views

**[D]** OffShoot has a **Disks** view and a **Transfers** view.

> "OffShoot displays all available disks and volumes connected to your computer.
> You can set any available disk as a Source or a Destination."
> — https://docs.hedge.video/offshoot/overview

- **Disks view** — every mounted volume appears as a tile; assign a tile to
  SOURCES or DESTINATIONS. Sources and destinations can be a whole disk/volume, a
  folder, or (source side only) selected files/collections.
> "OffShoot displays all available disks and volumes connected to your computer."
- **Transfers view** — the running/finished job list.

**[D]** Default mapping is source root → destination root, with the option to pick
specific source and destination folders.

**[D]** Multi-destination is a first-class concept:
> "Transfer files to a single destination, or multiple destinations."
> "Create as many copies as you need, at the same time. OffShoot transfers multiple
> sources to all destinations, fully utilizing your bandwidth."
> — https://docs.hedge.co/products/offshoot

**[D]** Jobs are computed, not hand-made. The Add Transfer button shows the
multiplication:
> "OffShoot multiplies the number of SOURCES by the number of DESTINATIONS, then
> displays how many transfers you'll make in the Add Transfer button."

**[I]** There is no documented numeric cap on destinations. A reasonable v1 limit
for us is a configurable max (default 4) enforced by disk-space checks rather than
by policy.

**[D]** **Cascading transfers** is an optional mode that serialises: copy to the
fastest destination first, then copy *from that destination* to the slower ones.
> "Cascading allows you to copy to your fastest destination and then copy from that
> destination to other destinations."
> — https://docs.hedge.video/offshoot/features/cascading-transfers

**[I]** For a two-drive wedding workflow, direct parallel copy is the normal path;
cascading matters when one destination is much slower (e.g. a spinning archive
drive). Defer to v2.

---

## 2. Transfer queue and progress

**[D]** A running job row shows: `SOURCE → DESTINATION` name, current filename, a
moving-average data rate, and an estimated duration. New jobs can be queued while
another runs.

**[D]** Progress-bar colours carry meaning:

| Colour | Meaning |
|---|---|
| Grey | indexing / preparation (incl. indexing source MHLs) |
| Dark blue | copying |
| Light blue | verification, validation, integrity checks, sidecar creation, logging |
| Green | completed without issues |
| Red | hardware issue (device disconnected) or source checksum error |
| Orange | file-integrity issue (destination checksum mismatch) |

> — https://docs.hedge.video/offshoot/overview and
> https://docs.hedge.video/offshoot/faq

**[D]** Controls are **Stop** and **Resume** — not pause/cancel:
> "When a transfer is running, stop it with the Stop button."
> "Once stopped, you can resume the transfer with the Resume button."
> — https://docs.hedge.video/offshoot/features/stop-and-resume

**[D]** Resume restarts the job, skips files already on the destination under the
Duplicate Detect rule, and recopies partially-copied or unverified files.

**[I]** "Stop" therefore behaves like *pause*: the job persists and is resumable.
I found no documented "delete/cancel job" control. We will implement
pause / resume / cancel explicitly, since a queue with no way to discard a job is a
usability trap.

---

## 3. Verification — the thing that actually matters

**[D]** Three verification modes, chosen per transfer:

| Mode | Source check | Destination check | Notes |
|---|---|---|---|
| **Transfer** | size | size | fastest; not recommended if you erase the card |
| **Source** | independent checksum (re-read) | size only | source problem stops the job; destination problems complete with warnings |
| **Source & Destination** | independent checksum | independent checksum by reading back the copies, plus metadata | ~2× the time of Transfer |

> — https://docs.hedge.video/offshoot/features/verification

**[D]** Default hash is **XXH64BE** (64-bit xxHash, big-endian digest form):
> "XXH64BE Checksums are always generated from reading source files and stored in
> Transfer Logs and MHLs."
> "It's also possible to generate one or all legacy checksums (MD5, SHA1, C4)."

Note for our brief: that is **XXH64 in BE byte order**, not xxHash3 and not a
hand-waved "xxHash64". If our MHL output is to interoperate with Silverstack /
Hedge archives, the byte order matters. **[I]** We should implement XXH64 (both
digest orders) and optionally XXH3-64 for speed, with the digest encoding written
into the report so a reader never has to guess.

**[D]** Source MHL reuse: in Source and Source & Destination modes, if a source
`.mhl` covers a file and its size + modification date match, OffShoot trusts the
MHL checksum instead of re-reading the card. After a successful Transfer/Source
pass it silently re-verifies the newly written destination MHL and may recopy from
the source or another destination if it finds a problem.

**[D]** A missing-file check runs at the end of every transfer; misses produce an
alert and a log warning.

**[I]** This is the highest-value idea in the whole product: *`mtime` + size match
⇒ trust the recorded hash*. It turns "verify 400 GB from a card" into "verify the
card once, then verify each destination read-back". We should implement it.

---

## 4. Artefacts: logs, MHL, reports

**[D]** Every transfer writes a **Transfer Log**, always locally, optionally also to
the destination:
- macOS: `~/Library/Application Support/Hedge/Transfer Logs`
- Windows: `%APPDATA%\Hedge\Transfer Logs`

**[D]** An `.mhl` Media Hash List is written to the **root of the destination
folder** when enabled; destination logs/MHLs can be turned off in settings.

**[D]** ASC MHL and legacy MHL are Pro features per the logging docs:
> — https://docs.hedge.video/offshoot/features/logging

**[D]** **Reports** are separate from logs, generated on demand, and combine any
number of transfer logs:
> "we let you decide when to do so - you can combine any amount of Transfer Logs
> into a single Report"
> "Reports are not only available as a PDF, but also as a full-fledged HTML page."
> — https://hedge.co/blog/offshoot-reports

**[D]** Report contents: summary (source count, destination count, clip count, file
count, total data), a sources section (whether each source was backed up N times,
verification mode used), a destinations section, source details, preset elements
used, a Files section for non-clip files, hidden-file summary, linked transfer logs,
and optional thumbnails/metadata when Clip Awareness is on.

**[D] Correction to the brief:** reports are **PDF and HTML**, not CSV. There is no
documented CSV export. The machine-readable artefact is the MHL.

---

## 5. Organisation, presets, duplicate rules

**[D]** Organization settings (filters, ignore bundles, omit empty folders,
flatten, labels, file renaming, generated destination folders) save as **presets**
(`.hedge` text files), selectable per transfer.
> — https://docs.hedge.video/offshoot/features/organization

**[D]** Folder/file naming supports elements: source name, counter, date/time, file
extension, file creation date/time; `/` in a format string produces nested folders.

**[D]** Elements (Project, Location, …) are reviewed before transfer, used in
labels/filenames/paths, and logged in the Transfer Log, Event Log and MHL.

**[D]** **Duplicate Detect**: skip an existing destination file when name, size
**and** modification date all match; a same-name file that differs in size or date
is copied with an incremented filename.
> — https://docs.hedge.video/offshoot/features/duplicate-detect

**[I]** The "incremented filename" behaviour is a deliberate choice we should copy:
it never silently overwrites, and never silently skips a genuinely different file.

**[D]** Disk-space fit check exists (an alert when a transfer "does not fit"),
including a macOS APFS purgeable-space caveat. **[D]** Source eject exists as a
manual disk-menu action and as a setting; **[I]** auto-eject after a fully verified
job is the expected photographer workflow and is worth having.

---

## 6. What we will actually build (v1 scope)

Derived from the above, sized for a wedding photographer with two drives:

1. **Single window, two panes.** Left: source card(s) — drag-and-drop, or pick a
   volume. Right: destination list, 1..N rows, each with its own target folder and
   free-space readout. Below both: the job queue.
2. **Add Transfer** computes `sources × destinations` jobs and shows the count,
   total bytes, and per-destination fit check before anything is written.
3. **Copy to all destinations in parallel**, one worker per destination, so a slow
   drive never throttles the others. Show source→destination, current file, moving
   average rate, ETA.
4. **Verification modes**: Transfer (size), Source, Source & Destination
   (read-back hash compare). Default **Source & Destination** — the brief asks for
   checksum verification we can trust, so the safe mode is the default.
5. **Default hash XXH64BE**, with XXH3-64, SHA-1 and MD5 selectable. Per-file status:
   `copied → verified → failed/mismatch`.
6. **Duplicate Detect** with the name+size+mtime rule and `-1` filename increment.
7. **Pause / Resume / Cancel** per job; resume skips completed-and-verified files.
8. **Artefacts per job**: `.mhl` at each destination root, plus a CSV and JSON log
   (our addition — cheap, and greppable for a "did I really copy this wedding"
   audit years later). Reports as HTML in v2.
9. **Never touch the source.** No deletes, no renames, no metadata writes. Open
   source handles read-only. This is a hard invariant with a test.
10. **Colour-coded completion** matching the table in §2 (grey/blue/green/orange/red),
    because a photographer reads the colour, not the log.

### Explicitly deferred

Cascading transfers, ASC MHL 2.x, Clip Awareness / thumbnails, presets as shareable
files, S3 destinations, Connect push notifications, PDF reports, C4 checksums.

### Deltas from Offshoot (deliberate)

- CSV/JSON log in addition to MHL.
- Hash algorithm and digest byte order recorded per file, not implied.
- Free and open source; no licence key, no per-seat tier.
- Nothing is uploaded anywhere.

# Reverse engineering — upstream FreeFileSync 14.12

Static analysis of the upstream FreeFileSync 14.12 Linux binary, done 2026-10-25/26
with REA + Ghidra 12.1.4. The GUI was never run; the binary was not modified.
This documents what the upstream app does, so the fork can be compared against it
feature by feature.

## Start here

- **`FEATURES.md`** — the feature inventory, written for an AI agent to read:
  every upstream feature with a stable ID, what it does, where it lives in the
  binary (class/function), the config keys, and how strong the evidence is.
  Includes a comparison worksheet at the end.
- **`REA-NOTES.md`** — the full write-up: binary overview, how Ghidra was set up,
  the 8 decompiled functions, the sync-engine picture, and the known gaps.

## Raw evidence

- `ghidra-decompiles2.txt` — the 8 decompiled functions (7,568 lines).
- `decompile-5e0181.txt`, `decompile-5bab70.txt`, `decompile-134ffd0.txt` —
  entry point, `main`, and the wxWidgets bootstrap.
- `ghidra-findings.txt`, `ghidra-findings2.txt` — how the functions were found
  (typeinfo-name anchors, then config-string anchors).
- `demangled-names.txt`, `mangled-names.txt` — the 293 typeinfo/lambda names
  that survive stripping (172 distinct enclosing functions).
- `ffs_find.java`, `ffs_find2.java`, `ffs_decomp_one.java` — the Ghidra headless
  scripts used (Java; Python scripts do not work in this headless install).
  `ffs_find.py` is the superseded Python version.
- `rea-snapshot` — the REA evidence bundle (56 KB JSON).

## Not in this folder (too big for the repo)

- The persistent Ghidra project (617 MB) and the Ghidra 12.1.4 install (876 MB).
- The extracted binary tree (60 MB) and the installers.
- These live on the analysis machine; `REA-NOTES.md` §2–§3 records the exact
  paths and the environment needed to redo the work.

## Known gap

The inner copy/compare loop (`fff::synchronize`,
`FolderPairSyncer::synchronizeFileInt`, `ComparisonBuffer::compareByContent`)
was identified by name but not decompiled — it needs a vtable pointer search.
See `REA-NOTES.md` §7 for the remaining step.

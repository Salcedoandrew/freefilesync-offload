# ffs_find.py — Ghidra headless script (Jython)
# Finds candidate functions for FreeFileSync's sync engine by following
# references from embedded typeinfo-name strings, then decompiles them.
#
# Usage (after import+analysis):
#   analyzeHeadless <projDir> <projName> -process FreeFileSync_x86_64 \
#       -scriptPath <dir> -postScript ffs_find.py
#
# Output: /home/hatch/workspace/freefilesync-re/ghidra-findings.txt
# and per-function decompiles in the same directory.

from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.program.util import DefinedDataIterator

OUT_DIR = "/home/hatch/workspace/freefilesync-re"

# (label, distinctive byte fragment of the embedded typeinfo-name string)
ANCHORS = [
    ("fff::synchronize", "ZN3fff11synchronizeE"),
    ("FolderPairSyncer::synchronizeFileInt<LEFT>", "16FolderPairSyncer18synchronizeFileIntILN3fff10SelectSideE0"),
    ("FolderPairSyncer::synchronizeFileInt<RIGHT>", "16FolderPairSyncer18synchronizeFileIntILN3fff10SelectSideE1"),
    ("ComparisonBuffer::compareByContent", "17ComparisonBuffer16compareByContent"),
    ("ComparisonBuffer::execute", "17ComparisonBuffer7execute"),
    ("DeletionHandler::removeFileWithCallback", "15DeletionHandler23removeFileWithCallback"),
    ("applyVersioningLimit", "20applyVersioningLimit"),
    ("categorizeFileByContent", "24categorizeFileByContent"),
]


def find_bytes(pattern):
    """Return list of addresses where the ASCII pattern occurs in memory."""
    mem = currentProgram.getMemory()
    pat = pattern.encode("ascii")
    hits = []
    for block in mem.getBlocks():
        if not block.isInitialized():
            continue
        try:
            data = bytes(block.getData().getBytes()) if False else None
        except Exception:
            data = None
        # stream the block in chunks to avoid huge allocations
        start = block.getStart()
        end = block.getEnd()
        addr = start
        chunk = 1 << 20
        while addr < end:
            size = min(chunk, end.getOffset() - addr.getOffset() + 1)
            try:
                buf = bytearray(size)
                got = mem.getBytes(addr, buf)
            except Exception:
                break
            if got > 0:
                idx = bytes(buf[:got]).find(pat)
                while idx >= 0:
                    hits.append(addr.add(idx))
                    idx = bytes(buf[:got]).find(pat, idx + 1)
            addr = addr.add(size)
    return hits


def refs_to(addr, max_hops=3):
    """Follow getReferencesTo up to max_hops, collecting functions found."""
    seen_addrs = set([addr])
    frontier = [addr]
    funcs = {}
    for hop in range(max_hops):
        nxt = []
        for a in frontier:
            try:
                for ref in getReferencesTo(a):
                    src = ref.getFromAddress()
                    f = getFunctionContaining(src)
                    if f is not None:
                        funcs[str(f.getEntryPoint())] = f
                    if src not in seen_addrs:
                        seen_addrs.add(src)
                        nxt.append(src)
            except Exception:
                pass
        frontier = nxt
        if not frontier:
            break
    return funcs


def decompile(func, out):
    decomp = DecompInterface()
    decomp.openProgram(currentProgram)
    res = decomp.decompileFunction(func, 60, ConsoleTaskMonitor())
    if res is not None and res.getDecompiledFunction() is not None:
        out.write("=" * 78 + "\n")
        out.write("FUNCTION %s  (%s)\n" % (func.getName(), func.getEntryPoint()))
        out.write("=" * 78 + "\n")
        out.write(res.getDecompiledFunction().getC() + "\n\n")
    else:
        out.write("DECOMPILE FAILED for %s (%s)\n\n" % (func.getName(), func.getEntryPoint()))
    decomp.dispose()


def main():
    lines = []
    lines.append("FreeFileSync 14.12 — Ghidra function hunt (anchors -> candidate functions)")
    lines.append("Program: %s" % currentProgram.getName())
    lines.append("")
    all_funcs = {}
    for label, frag in ANCHORS:
        hits = find_bytes(frag)
        lines.append("ANCHOR %s : %d string hit(s)" % (label, len(hits)))
        for h in hits[:4]:
            lines.append("  string at %s" % h)
            funcs = refs_to(h)
            for ep, f in sorted(funcs.items()):
                lines.append("  -> candidate function at %s (size %d)" % (ep, f.getBody().getNumAddresses()))
                all_funcs[ep] = (label, f)
    lines.append("")
    lines.append("TOTAL distinct candidate functions: %d" % len(all_funcs))
    with open(OUT_DIR + "/ghidra-findings.txt", "w") as fh:
        fh.write("\n".join(lines) + "\n")
    # decompile the candidates (cap at 12 to bound output)
    with open(OUT_DIR + "/ghidra-decompiles.txt", "w") as out:
        for i, (ep, (label, f)) in enumerate(sorted(all_funcs.items())):
            if i >= 12:
                break
            out.write("### anchor: %s\n" % label)
            decompile(f, out)
    print("WROTE %s/ghidra-findings.txt and ghidra-decompiles.txt (%d functions)" % (OUT_DIR, min(12, len(all_funcs))))


main()

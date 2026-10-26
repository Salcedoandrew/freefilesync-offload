// ffs_find.java — Ghidra headless script (Java)
// Finds candidate functions for FreeFileSync's sync engine by following
// references from embedded typeinfo-name strings, then decompiles them.
//
// Usage (after import+analysis):
//   analyzeHeadless <projDir> <projName> -process FreeFileSync_x86_64 \
//       -noanalysis -scriptPath <dir> -postScript ffs_find.java
//
// Output: /home/hatch/workspace/freefilesync-re/ghidra-findings.txt
// and decompiles in /home/hatch/workspace/freefilesync-re/ghidra-decompiles.txt
//
// (Java port of ffs_find.py — PyGhidra/Python is not available in this
//  headless install, so the same logic runs as a Java GhidraScript.)

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.*;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.util.task.ConsoleTaskMonitor;

public class ffs_find extends GhidraScript {

    private static final String OUT_DIR = "/home/hatch/workspace/freefilesync-re";

    // (label, distinctive byte fragment of the embedded typeinfo-name string)
    private static final String[][] ANCHORS = {
        {"fff::synchronize", "ZN3fff11synchronizeE"},
        {"FolderPairSyncer::synchronizeFileInt<LEFT>", "16FolderPairSyncer18synchronizeFileIntILN3fff10SelectSideE0"},
        {"FolderPairSyncer::synchronizeFileInt<RIGHT>", "16FolderPairSyncer18synchronizeFileIntILN3fff10SelectSideE1"},
        {"ComparisonBuffer::compareByContent", "17ComparisonBuffer16compareByContent"},
        {"ComparisonBuffer::execute", "17ComparisonBuffer7execute"},
        {"DeletionHandler::removeFileWithCallback", "15DeletionHandler23removeFileWithCallback"},
        {"applyVersioningLimit", "20applyVersioningLimit"},
        {"categorizeFileByContent", "24categorizeFileByContent"},
    };

    private List<Address> findBytes(String pattern) {
        List<Address> hits = new ArrayList<>();
        Memory mem = currentProgram.getMemory();
        byte[] pat = pattern.getBytes(java.nio.charset.StandardCharsets.US_ASCII);
        for (MemoryBlock block : mem.getBlocks()) {
            if (!block.isInitialized()) {
                continue;
            }
            Address start = block.getStart();
            Address end = block.getEnd();
            Address addr = start;
            int chunk = 1 << 20;
            while (addr.compareTo(end) < 0) {
                long remaining = end.getOffset() - addr.getOffset() + 1;
                int size = (int) Math.min(chunk, remaining);
                byte[] buf = new byte[size];
                int got;
                try {
                    got = mem.getBytes(addr, buf);
                } catch (Exception e) {
                    break;
                }
                if (got > 0) {
                    int idx = indexOf(buf, got, pat, 0);
                    while (idx >= 0) {
                        hits.add(addr.add(idx));
                        idx = indexOf(buf, got, pat, idx + 1);
                    }
                }
                addr = addr.add(size);
            }
        }
        return hits;
    }

    private static int indexOf(byte[] buf, int len, byte[] pat, int from) {
        outer:
        for (int i = from; i + pat.length <= len; i++) {
            for (int j = 0; j < pat.length; j++) {
                if (buf[i + j] != pat[j]) {
                    continue outer;
                }
            }
            return i;
        }
        return -1;
    }

    private Map<String, Function> refsTo(Address addr, int maxHops) {
        Map<String, Function> funcs = new TreeMap<>();
        Set<Address> seen = new HashSet<>();
        seen.add(addr);
        List<Address> frontier = new ArrayList<>();
        frontier.add(addr);
        for (int hop = 0; hop < maxHops; hop++) {
            List<Address> nxt = new ArrayList<>();
            for (Address a : frontier) {
                for (Reference ref : getReferencesTo(a)) {
                    Address src = ref.getFromAddress();
                    Function f = getFunctionContaining(src);
                    if (f != null) {
                        funcs.put(f.getEntryPoint().toString(), f);
                    }
                    if (!seen.contains(src)) {
                        seen.add(src);
                        nxt.add(src);
                    }
                }
            }
            frontier = nxt;
            if (frontier.isEmpty()) {
                break;
            }
        }
        return funcs;
    }

    private void decompile(Function func, PrintWriter out) {
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        DecompileResults res = decomp.decompileFunction(func, 60, new ConsoleTaskMonitor());
        out.println("================================================================================");
        out.println("FUNCTION " + func.getName() + "  (" + func.getEntryPoint() + ")");
        out.println("================================================================================");
        if (res != null && res.getDecompiledFunction() != null) {
            out.println(res.getDecompiledFunction().getC());
        } else {
            out.println("DECOMPILE FAILED for " + func.getName() + " (" + func.getEntryPoint() + ")");
        }
        out.println();
        decomp.dispose();
    }

    @Override
    public void run() throws Exception {
        List<String> lines = new ArrayList<>();
        lines.add("FreeFileSync 14.12 - Ghidra function hunt (anchors -> candidate functions)");
        lines.add("Program: " + currentProgram.getName());
        lines.add("");
        Map<String, String> allLabels = new TreeMap<>();
        Map<String, Function> allFuncs = new TreeMap<>();
        for (String[] anchor : ANCHORS) {
            String label = anchor[0];
            String frag = anchor[1];
            List<Address> hits = findBytes(frag);
            lines.add("ANCHOR " + label + " : " + hits.size() + " string hit(s)");
            for (Address h : hits.subList(0, Math.min(4, hits.size()))) {
                lines.add("  string at " + h);
                Map<String, Function> funcs = refsTo(h, 3);
                for (Map.Entry<String, Function> e : funcs.entrySet()) {
                    Function f = e.getValue();
                    lines.add("  -> candidate function at " + e.getKey() +
                            " (size " + f.getBody().getNumAddresses() + ")");
                    allFuncs.put(e.getKey(), f);
                    allLabels.put(e.getKey(), label);
                }
            }
        }
        lines.add("");
        lines.add("TOTAL distinct candidate functions: " + allFuncs.size());
        try (PrintWriter fh = new PrintWriter(new FileWriter(OUT_DIR + "/ghidra-findings.txt"))) {
            for (String l : lines) {
                fh.println(l);
            }
        }
        int count = 0;
        try (PrintWriter out = new PrintWriter(new FileWriter(OUT_DIR + "/ghidra-decompiles.txt"))) {
            for (Map.Entry<String, Function> e : allFuncs.entrySet()) {
                if (count >= 12) {
                    break;
                }
                out.println("### anchor: " + allLabels.get(e.getKey()));
                decompile(e.getValue(), out);
                count++;
            }
        }
        println("WROTE " + OUT_DIR + "/ghidra-findings.txt and ghidra-decompiles.txt (" + count + " functions)");
    }
}

// ffs_find2.java — Ghidra headless script (Java)
// Finds FreeFileSync sync-engine functions via references from distinctive
// user-visible / config strings (code -> string references ARE created by
// Ghidra's disassembler for RIP-relative loads, unlike the data->data
// typeinfo pointers that the first script chased).
//
// Usage (after import+analysis):
//   analyzeHeadless <projDir> <projName> -process FreeFileSync_x86_64 \
//       -noanalysis -scriptPath <dir> -postScript ffs_find2.java
//
// Output: /home/hatch/workspace/freefilesync-re/ghidra-findings2.txt
// and decompiles in /home/hatch/workspace/freefilesync-re/ghidra-decompiles2.txt

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

public class ffs_find2 extends GhidraScript {

    private static final String OUT_DIR = "/home/hatch/workspace/freefilesync-re";

    // (label, distinctive string the sync engine / config references)
    private static final String[][] ANCHORS = {
        {"sync mode: sync_twoway", "sync_twoway"},
        {"sync mode: sync_mirror", "sync_mirror"},
        {"sync mode: sync_update", "sync_update"},
        {"versioning folder", "VersioningFolder"},
        {"recycle bin", "RecycleBin"},
        {"delete_recycler", "delete_recycler"},
        {"delete_versioning", "delete_versioning"},
        {"WarnRecycleBinNotAvailable", "WarnRecycleBinNotAvailable"},
        {"UpdateLeft", "UpdateLeft"},
        {"UpdateRight", "UpdateRight"},
        {"TwoWay", "TwoWay"},
        {"CustomDirections", "CustomDirections"},
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
        lines.add("FreeFileSync 14.12 - Ghidra function hunt v2 (config strings -> code functions)");
        lines.add("Program: " + currentProgram.getName());
        lines.add("");
        Map<String, Function> allFuncs = new TreeMap<>();
        Map<String, String> allLabels = new TreeMap<>();
        for (String[] anchor : ANCHORS) {
            String label = anchor[0];
            String frag = anchor[1];
            List<Address> hits = findBytes(frag);
            lines.add("ANCHOR " + label + " : " + hits.size() + " string hit(s)");
            for (Address h : hits.subList(0, Math.min(3, hits.size()))) {
                lines.add("  string at " + h);
                Reference[] refs = getReferencesTo(h);
                lines.add("  references to string: " + refs.length);
                for (Reference ref : refs) {
                    Address src = ref.getFromAddress();
                    Function f = getFunctionContaining(src);
                    if (f != null) {
                        String ep = f.getEntryPoint().toString();
                        lines.add("  -> function at " + ep + " (size " + f.getBody().getNumAddresses() + ")");
                        allFuncs.put(ep, f);
                        allLabels.put(ep, label);
                    } else {
                        lines.add("  -> ref from " + src + " (not in a function)");
                    }
                }
            }
        }
        lines.add("");
        lines.add("TOTAL distinct candidate functions: " + allFuncs.size());
        try (PrintWriter fh = new PrintWriter(new FileWriter(OUT_DIR + "/ghidra-findings2.txt"))) {
            for (String l : lines) {
                fh.println(l);
            }
        }
        int count = 0;
        try (PrintWriter out = new PrintWriter(new FileWriter(OUT_DIR + "/ghidra-decompiles2.txt"))) {
            for (Map.Entry<String, Function> e : allFuncs.entrySet()) {
                if (count >= 12) {
                    break;
                }
                out.println("### anchor: " + allLabels.get(e.getKey()));
                decompile(e.getValue(), out);
                count++;
            }
        }
        println("WROTE " + OUT_DIR + "/ghidra-findings2.txt and ghidra-decompiles2.txt (" + count + " functions)");
    }
}

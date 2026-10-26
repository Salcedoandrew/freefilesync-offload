// ffs_decomp_one.java — Ghidra headless script (Java)
// Decompiles the function containing a given address, for a single address
// passed as a script argument (hex, e.g. 5e0181).
//
// Usage:
//   analyzeHeadless <projDir> <projName> -process FreeFileSync_x86_64 \
//       -noanalysis -scriptPath <dir> -postScript "ffs_decomp_one.java 5e0181"
//
// Output: /home/hatch/workspace/freefilesync-re/decompile-<addr>.txt

import java.io.FileWriter;
import java.io.PrintWriter;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.util.task.ConsoleTaskMonitor;

public class ffs_decomp_one extends GhidraScript {

    private static final String OUT_DIR = "/home/hatch/workspace/freefilesync-re";

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("usage: ffs_decomp_one.java <hex-address>");
            return;
        }
        String hex = args[0].replaceFirst("^0x", "");
        Address addr = toAddr(Long.parseLong(hex, 16));
        Function f = getFunctionContaining(addr);
        if (f == null) {
            // maybe the address itself is a function entry not yet in the listing
            f = getFunctionAt(addr);
        }
        String outPath = OUT_DIR + "/decompile-" + hex + ".txt";
        try (PrintWriter out = new PrintWriter(new FileWriter(outPath))) {
            out.println("Address requested: 0x" + hex);
            if (f == null) {
                out.println("NO FUNCTION at or containing 0x" + hex);
                println("NO FUNCTION at 0x" + hex);
                return;
            }
            out.println("FUNCTION " + f.getName() + "  (" + f.getEntryPoint() + ")");
            out.println("Size: " + f.getBody().getNumAddresses() + " bytes");
            out.println("================================================================================");
            DecompInterface decomp = new DecompInterface();
            decomp.openProgram(currentProgram);
            DecompileResults res = decomp.decompileFunction(f, 60, new ConsoleTaskMonitor());
            if (res != null && res.getDecompiledFunction() != null) {
                out.println(res.getDecompiledFunction().getC());
            } else {
                out.println("DECOMPILE FAILED");
            }
            decomp.dispose();
            println("WROTE " + outPath + " (" + f.getName() + " @ " + f.getEntryPoint() + ")");
        }
    }
}

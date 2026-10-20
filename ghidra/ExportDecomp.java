// ExportDecomp.java: write Ghidra's pseudo-C for every function in code segments 1-34 to
// re/decomp/segNN/<off>_<name>.c. Reference output only (docs/PLAN.md: diff tests are the arbiter).
// Script argument 1 (optional): output root, default <cwd>/re/decomp.
//@category Stars
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;

public class ExportDecomp extends GhidraScript {
    @Override
    public void run() throws Exception {
        String root = getScriptArgs().length > 0 ? getScriptArgs()[0] : System.getProperty("user.dir") + "/re/decomp";
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        int ok = 0, fail = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            Address e = f.getEntryPoint();
            if (f.isThunk() || f.isExternal() || !(e instanceof SegmentedAddress)) continue;
            int s = ((SegmentedAddress) e).getSegment();
            if (s < 0x1000 || s > 0x1000 + 8 * 33 || ((s - 0x1000) % 8) != 0) continue;
            int seg = (s - 0x1000) / 8 + 1, off = ((SegmentedAddress) e).getSegmentOffset();
            DecompileResults r = di.decompileFunction(f, 60, monitor);
            String body;
            if (r != null && r.decompileCompleted() && r.getDecompiledFunction() != null) { body = r.getDecompiledFunction().getC(); ok++; }
            else { body = "/* decompilation failed: " + (r == null ? "null" : r.getErrorMessage()) + " */\n"; fail++; }
            String name = f.getName().replaceAll("[^A-Za-z0-9_]", "_");
            Path p = Paths.get(root, String.format("seg%02d", seg), String.format("%04x_%s.c", off, name));
            Files.createDirectories(p.getParent());
            Files.write(p, body.getBytes(StandardCharsets.UTF_8));
        }
        println("DECOMP ok=" + ok + " failed=" + fail + " root=" + root);
    }
}

// LabelCodeData.java: label the data tables stored in code segments (docs/code-segment-data.md, part A).
// Run: analyzeHeadless re/ghidra stars -process stars26i.exe -noanalysis -scriptPath ghidra
//   -postScript LabelCodeData.java
// Reads the table rows "| seg:0xSTART-0xEND | ..." of the markdown file (script argument 0 overrides
// the path) and gives the start of each range the label CODE_DATA_<seg>_<off>. Idempotent. Data types
// are left alone: the shapes in the file are provisional.
//@category Stars
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import java.nio.file.*;
import java.util.regex.*;

public class LabelCodeData extends GhidraScript {
    @Override
    public void run() throws Exception {
        String path = getScriptArgs().length > 0 ? getScriptArgs()[0] : System.getProperty("user.dir") + "/docs/code-segment-data.md";
        Pattern any = Pattern.compile("^\\|\\s*(\\d+):0x([0-9a-f]+)-0x([0-9a-f]+)\\s*\\|");
        SymbolTable st = currentProgram.getSymbolTable();
        boolean partA = false;
        int made = 0, kept = 0;
        for (String line : Files.readAllLines(Paths.get(path))) {
            if (line.startsWith("## A.")) { partA = true; continue; }
            if (line.startsWith("## B.")) partA = false;
            if (!partA) continue;
            Matcher m = any.matcher(line);
            if (!m.find()) continue;
            int seg = Integer.parseInt(m.group(1)), off = Integer.parseInt(m.group(2), 16);
            Address a = currentProgram.getAddressFactory().getAddress(String.format("%04x:%04x", 0x1000 + 8 * (seg - 1), off));
            String name = String.format("CODE_DATA_%d_%04x", seg, off);
            boolean have = false;
            for (Symbol s : st.getSymbols(a)) if (s.getName().equals(name)) have = true;
            if (have) { kept++; continue; }
            st.createLabel(a, name, SourceType.IMPORTED);
            made++;
        }
        println("CODEDATA labels created=" + made + " kept=" + kept);
    }
}

// ApplyStructTypes.java: apply the /Stars struct types (ghidra/DefineStructs.java) to the 4-byte DGROUP
// data items that hold a far pointer to a game record table, so decompiler output and later exports
// show field names. Only an existing 4-byte item is retyped (as a 4-byte pointer), so its label and its
// exported C type (uint32_t) do not change and src/globals.h stays byte-identical. A table stored inline
// in DGROUP, or a far pointer split into two labelled words, is left alone (retyping it would swallow
// the labels src/ uses).
// Run after DefineStructs.java: analyzeHeadless re/ghidra stars -process stars26i.exe -noanalysis
//   -scriptPath ghidra -postScript ApplyStructTypes.java
//@category Stars
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;

public class ApplyStructTypes extends GhidraScript {
    private static final int DGROUP = 0x1110;
    // offset, struct, pointer depth (1 = Struct *, 2 = Struct **), evidence
    private static final Object[][] TARGETS = {
        {0x00ba, "Fleet", 2, "DS:0xBA/0xBC: far pointer to FARPTR[fleet count], each -> Fleet (14:0x0508)"},
        {0x177a, "ScoreRecord", 1, "DS:0x177A/0x177C: far pointer to ScoreRecord[player_count] (14:0x0508, 23:0x3b78)"},
    };

    @Override
    public void run() throws Exception {
        Listing lst = currentProgram.getListing();
        DataTypeManager dtm = currentProgram.getDataTypeManager();
        for (Object[] t : TARGETS) {
            int off = (Integer) t[0];
            Address a = currentProgram.getAddressFactory().getAddress(String.format("%04x:%04x", DGROUP, off));
            DataType st = dtm.getDataType(new CategoryPath("/Stars"), (String) t[1]);
            Data d = lst.getDataAt(a);
            if (st == null || d == null || d.getLength() != 4) { println("APPLY skip 0x" + Integer.toHexString(off) + ": no struct or not a 4-byte item"); continue; }
            DataType dt = st;
            for (int i = 0; i < (Integer) t[2]; i++) dt = new PointerDataType(dt, 4, dtm);
            String label = d.getLabel();
            lst.clearCodeUnits(a, a.add(3), false);
            lst.createData(a, dt);
            Data nd = lst.getDataAt(a);
            if (label != null && !label.equals(nd.getLabel())) {   // keep the label the export and src/ use
                currentProgram.getSymbolTable().createLabel(a, label, ghidra.program.model.symbol.SourceType.USER_DEFINED);
            }
            nd = lst.getDataAt(a);
            println("APPLY 0x" + Integer.toHexString(off) + " " + d.getLabel() + " -> " + nd.getDataType().getName() + " len " + nd.getLength() + " label " + nd.getLabel());
        }
    }
}

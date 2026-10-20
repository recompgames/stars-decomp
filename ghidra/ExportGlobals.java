// ExportGlobals.java: write the labelled data items of DGROUP (segment 35, Ghidra segment 0x1110)
// as CSV "name,offset,type" for tools/gen_globals.py, which turns it into src/globals.h.
// Type: int8_t..uint32_t from the data type's size and signedness (undefined data counts as
// unsigned); arrays become "type[N]"; strings, structs and other sizes become uint8_t[N].
// A fourth column "comment" is the end-of-line comment of the item (the evidence for its name).
// Script argument 1 (optional): output file, default <cwd>/re/globals.csv.
//@category Stars
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import java.io.PrintWriter;
import java.nio.file.*;

public class ExportGlobals extends GhidraScript {
    private static final int DGROUP = 0x1110;    // Ghidra segment of NE segment 35 (selector 0x0118)

    private static String scalar(DataType dt, int len) {
        boolean signed = dt instanceof AbstractIntegerDataType && ((AbstractIntegerDataType) dt).isSigned();
        switch (len) {
            case 1: return signed ? "int8_t" : "uint8_t";
            case 2: return signed ? "int16_t" : "uint16_t";
            case 4: return signed ? "int32_t" : "uint32_t";
            default: return null;
        }
    }

    @Override
    public void run() throws Exception {
        String out = getScriptArgs().length > 0 ? getScriptArgs()[0] : System.getProperty("user.dir") + "/re/globals.csv";
        Path p = Paths.get(out);
        if (p.getParent() != null) Files.createDirectories(p.getParent());
        int n = 0;
        try (PrintWriter w = new PrintWriter(Files.newBufferedWriter(p))) {
            w.println("name,offset,type,comment");
            for (Data d : currentProgram.getListing().getDefinedData(true)) {
                Address a = d.getMinAddress();
                if (!(a instanceof SegmentedAddress) || ((SegmentedAddress) a).getSegment() != DGROUP) continue;
                String label = d.getLabel();
                if (label == null) continue;
                DataType dt = d.getDataType();
                String type;
                if (dt instanceof Array) {
                    Array arr = (Array) dt;
                    String el = scalar(arr.getDataType(), arr.getElementLength());
                    type = el != null ? el + "[" + arr.getNumElements() + "]" : "uint8_t[" + d.getLength() + "]";
                } else {
                    String s = scalar(dt, d.getLength());
                    type = s != null ? s : "uint8_t[" + d.getLength() + "]";
                }
                String cm = d.getComment(CodeUnit.EOL_COMMENT);
                cm = cm == null ? "" : "\"" + cm.replace("\"", "\"\"").replace("\n", " ") + "\"";
                w.println(label.replaceAll("[^A-Za-z0-9_]", "_") + "," + String.format("0x%04x", ((SegmentedAddress) a).getSegmentOffset()) + "," + type + "," + cm);
                n++;
            }
        }
        println("GLOBALS rows=" + n + " file=" + out);
    }
}

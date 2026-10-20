// RenameGlobals.java: give meaningful names to DGROUP data labels from ghidra/globals_names.csv.
// Run: analyzeHeadless re/ghidra stars -process stars26i.exe -noanalysis -scriptPath ghidra
//   -postScript RenameGlobals.java [names.csv]
// CSV columns: offset,name,type,evidence. offset is hex inside DGROUP (Ghidra segment 0x1110); name is
// the label without the g_ prefix; evidence becomes the end-of-line comment of the data item (tools/
// gen_globals.py prints it above the macro). type is empty for an item that already exists (its data
// type is kept); for an offset with no data item it is int8_t..uint32_t and the item is created.
// Idempotent. Never touches an item whose address is inside a larger item: it logs and skips.
//@category Stars
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.nio.file.*;
import java.util.*;

public class RenameGlobals extends GhidraScript {
    private static final int DGROUP = 0x1110;

    private static List<String> split(String line) {
        List<String> f = new ArrayList<>();
        StringBuilder sb = new StringBuilder();
        boolean q = false;
        for (int i = 0; i < line.length(); i++) {
            char c = line.charAt(i);
            if (q) {
                if (c == '"') { if (i + 1 < line.length() && line.charAt(i + 1) == '"') { sb.append('"'); i++; } else q = false; }
                else sb.append(c);
            } else if (c == '"') q = true;
            else if (c == ',') { f.add(sb.toString()); sb.setLength(0); }
            else sb.append(c);
        }
        f.add(sb.toString());
        return f;
    }

    private DataType typeOf(String t) {
        switch (t) {
            case "int8_t": return new SignedByteDataType();
            case "uint8_t": return new ByteDataType();
            case "int16_t": return new ShortDataType();
            case "uint16_t": return new UnsignedShortDataType();
            case "int32_t": return new IntegerDataType();
            case "uint32_t": return new UnsignedIntegerDataType();
            default: return null;
        }
    }

    @Override
    public void run() throws Exception {
        String path = getScriptArgs().length > 0 ? getScriptArgs()[0] : System.getProperty("user.dir") + "/ghidra/globals_names.csv";
        Listing lst = currentProgram.getListing();
        SymbolTable st = currentProgram.getSymbolTable();
        int renamed = 0, created = 0, same = 0, skipped = 0;
        boolean first = true;
        for (String line : Files.readAllLines(Paths.get(path))) {
            if (first) { first = false; continue; }
            if (line.trim().isEmpty()) continue;
            List<String> f = split(line);
            int off = Integer.decode(f.get(0).trim());
            String name = f.get(1).trim(), type = f.get(2).trim(), ev = f.get(3).trim();
            Address a = currentProgram.getAddressFactory().getAddress(String.format("%04x:%04x", DGROUP, off));
            Data d = lst.getDataContaining(a);
            if (d != null && !d.getMinAddress().equals(a)) {
                println("RENAME skip 0x" + Integer.toHexString(off) + " " + name + ": inside " + d.getMinAddress());
                skipped++;
                continue;
            }
            if (d == null || !d.isDefined()) {
                DataType dt = typeOf(type);
                if (dt == null) { println("RENAME skip 0x" + Integer.toHexString(off) + " " + name + ": no data item and no type"); skipped++; continue; }
                lst.createData(a, dt);
                d = lst.getDataAt(a);
                created++;
            } else if (!type.isEmpty()) {
                println("RENAME note 0x" + Integer.toHexString(off) + ": item exists, type column ignored");
            }
            String cur = d.getLabel();
            if (cur != null && cur.equals(name)) same++;
            else {
                Symbol s = st.getPrimarySymbol(a);
                if (s != null) { s.setName(name, SourceType.USER_DEFINED); }
                else st.createLabel(a, name, SourceType.USER_DEFINED);
                renamed++;
            }
            lst.setComment(a, CodeUnit.EOL_COMMENT, ev);
        }
        println("RENAME renamed=" + renamed + " created=" + created + " unchanged=" + same + " skipped=" + skipped);
    }
}

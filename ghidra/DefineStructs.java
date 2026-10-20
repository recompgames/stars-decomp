// DefineStructs.java: create the packed game structs of src/stars.h as Ghidra data types in the
// category /Stars of the program's Data Type Manager, so a data type can be applied to a global or a
// local and later exports print field names.
// Run: analyzeHeadless re/ghidra stars -process stars26i.exe -noanalysis -scriptPath ghidra
//   -postScript DefineStructs.java [path/to/stars.h]
// Parsing: a struct is "struct Name {" ... "};" at the top level of the header; its size is the
// STATIC_SIZE(struct Name, 0x..) line after it. Each field line carries a comment starting "+0xOFF";
// that offset places the field. Members of an anonymous union keep the first member only (a later
// member that overlaps one already placed is skipped). Field types: uint8_t, int8_t, uint16_t, int16_t,
// uint32_t, FARPTR (4 bytes) and "struct Other" (defined earlier in the header); arrays use [N].
// Idempotent: an existing type of the same name in /Stars is replaced. Nothing is applied to globals.
//@category Stars
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import java.nio.file.*;
import java.util.*;
import java.util.regex.*;

public class DefineStructs extends GhidraScript {
    private static final Pattern STRUCT = Pattern.compile("^struct (\\w+) \\{$");
    private static final Pattern SIZE = Pattern.compile("^STATIC_SIZE\\(struct (\\w+), (0x[0-9A-Fa-f]+|\\d+)\\);");
    private static final Pattern FIELD = Pattern.compile(
        "^\\s+(struct \\w+|\\w+) (\\w+)(?:\\[(\\w+)\\])?;\\s*/\\* \\+(0x[0-9A-Fa-f]+) \\(\\d+\\)(?:[^:]*:)?(.*)\\*/");

    private DataType base(String t, DataTypeManager dtm) {
        switch (t) {
            case "uint8_t": return new ByteDataType();
            case "int8_t": return new SignedByteDataType();
            case "uint16_t": return new UnsignedShortDataType();
            case "int16_t": return new ShortDataType();
            case "uint32_t": case "FARPTR": return new UnsignedIntegerDataType();
            default:
                if (t.startsWith("struct ")) {
                    return dtm.getDataType(new CategoryPath("/Stars"), t.substring(7));
                }
                return null;
        }
    }

    @Override
    public void run() throws Exception {
        String path = getScriptArgs().length > 0 ? getScriptArgs()[0] : System.getProperty("user.dir") + "/src/stars.h";
        List<String> lines = Files.readAllLines(Paths.get(path));
        Map<String, Integer> sizes = new HashMap<>();
        for (String l : lines) {
            Matcher m = SIZE.matcher(l);
            if (m.find()) sizes.put(m.group(1), Integer.decode(m.group(2)));
        }
        DataTypeManager dtm = currentProgram.getDataTypeManager();
        CategoryPath cat = new CategoryPath("/Stars");
        int made = 0;
        for (int i = 0; i < lines.size(); i++) {
            Matcher sm = STRUCT.matcher(lines.get(i));
            if (!sm.find()) continue;
            String name = sm.group(1);
            Integer size = sizes.get(name);
            if (size == null) { println("DEFINE skip " + name + ": no STATIC_SIZE"); continue; }
            StructureDataType st = new StructureDataType(cat, name, size);
            int covered = 0, placed = 0;
            for (int j = i + 1; j < lines.size() && !lines.get(j).startsWith("};"); j++) {
                Matcher fm = FIELD.matcher(lines.get(j));
                if (!fm.find()) continue;
                int off = Integer.decode(fm.group(4));
                if (off < covered) continue;   // later member of a union
                DataType dt = base(fm.group(1), dtm);
                if (dt == null) { println("DEFINE " + name + "." + fm.group(2) + ": unknown type " + fm.group(1)); continue; }
                int n = 1;
                if (fm.group(3) != null) n = Integer.decode(fm.group(3));
                DataType comp = n == 1 && fm.group(3) == null ? dt : new ArrayDataType(dt, n, dt.getLength());
                st.replaceAtOffset(off, comp, comp.getLength(), fm.group(2), fm.group(5).trim());
                covered = off + comp.getLength();
                placed++;
            }
            dtm.addDataType(st, DataTypeConflictHandler.REPLACE_HANDLER);
            println("DEFINE /Stars/" + name + " size 0x" + Integer.toHexString(size) + " fields " + placed);
            made++;
        }
        println("DEFINE done: " + made + " structs");
    }
}

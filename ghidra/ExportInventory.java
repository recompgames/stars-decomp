// ExportInventory.java: write docs/functions.csv (NE segment numbering, never Ghidra's).
// Columns: seg,off,name,size,ret,callers,callees,apis,fp,origin,status,notes
// Manual columns origin, status, notes are preserved on re-export (merge by seg,off).
//@category Stars
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

public class ExportInventory extends GhidraScript {
    static final String HEADER = "seg,off,name,size,ret,callers,callees,apis,fp,origin,status,notes";
    // NE segments whose functions are the Microsoft C runtime (evidence: docs/inventory.md).
    static final Set<Integer> CRT_SEGS = new HashSet<>(Arrays.asList(34));

    static int neSeg(Address a) {
        if (!(a instanceof SegmentedAddress)) return -1;
        int s = ((SegmentedAddress) a).getSegment();
        if (s < 0x1000 || s > 0x1000 + 8 * 33 || ((s - 0x1000) % 8) != 0) return -1;
        return (s - 0x1000) / 8 + 1;
    }
    static int off(Address a) { return ((SegmentedAddress) a).getSegmentOffset(); }
    static String key(int seg, int off) { return seg + ":0x" + String.format("%04x", off); }

    @Override
    public void run() throws Exception {
        String repo = System.getProperty("user.dir");
        String out = getScriptArgs().length > 0 ? getScriptArgs()[0] : repo + "/docs/functions.csv";
        Path outPath = Paths.get(out);
        Map<String, String[]> old = new HashMap<>();
        if (Files.exists(outPath)) {
            String text = new String(Files.readAllBytes(outPath), StandardCharsets.UTF_8);
            List<List<String>> table = parseCsvText(text);
            for (int i = 1; i < table.size(); i++) {
                List<String> c = table.get(i);
                if (c.size() < 12) continue;
                old.put(c.get(0) + ":" + c.get(1), new String[] {c.get(9), c.get(10), c.get(11)});
            }
        }
        FunctionManager fm = currentProgram.getFunctionManager();
        Listing listing = currentProgram.getListing();
        MemoryBlock ext = currentProgram.getMemory().getBlock("EXTERNAL");
        ReferenceManager rm = currentProgram.getReferenceManager();

        // Collect functions in code segments 1-34, sorted.
        List<Function> funcs = new ArrayList<>();
        for (Function f : fm.getFunctions(true)) {
            int s = neSeg(f.getEntryPoint());
            if (s >= 1 && s <= 34 && !f.isExternal()) funcs.add(f);
        }
        funcs.sort(Comparator.<Function>comparingInt(f -> neSeg(f.getEntryPoint())).thenComparingInt(f -> off(f.getEntryPoint())));

        // Units = functions plus "range" rows (see below). Everything is keyed by "seg:0xoff".
        class Unit {
            int seg, off, size;
            String name, note = "";
            Function f;            // null for a range row
            AddressSetView body;
            TreeSet<String> ce = new TreeSet<>(), ap = new TreeSet<>();
            boolean near, far, x87;
            String k() { return key(seg, off); }
        }
        List<Unit> units = new ArrayList<>();
        Map<String, TreeSet<String>> callers = new HashMap<>();
        boolean[][] covered = new boolean[35][];
        for (Function f : funcs) {
            Address e = f.getEntryPoint();
            Unit u = new Unit();
            u.f = f; u.name = f.getName(); u.body = f.getBody();
            int seg = neSeg(e);
            u.seg = seg; u.off = off(e);
            int lo = off(e), hi = -1;
            // size = the run of the body that starts at the entry. Ghidra ends a range after an
            // unconditional jump and leaves the 0x90 NOP padding (1-3 bytes) out of the body, so the
            // run is extended over such gaps when the next range is also in this body. Bodies of
            // different functions are disjoint and a gap must belong to no other function, so runs
            // never overlap. Detached fragments past other bytes are still not counted.
            List<AddressRange> rgs = new ArrayList<>();
            for (AddressRange rg : f.getBody()) rgs.add(rg);
            for (AddressRange rg : rgs) if (rg.contains(e)) hi = off(rg.getMaxAddress());
            boolean grew = true;
            while (grew) {
                grew = false;
                for (AddressRange rg : rgs) {
                    int rlo = off(rg.getMinAddress());
                    if (neSeg(rg.getMinAddress()) != seg || rlo <= hi + 1 || rlo > hi + 4) continue;
                    boolean pad = true;
                    for (int o = hi + 1; o < rlo && pad; o++) {
                        Address ga = rg.getMinAddress().subtract(rlo - o);
                        pad = (getByte(ga) & 0xff) == 0x90 && (fm.getFunctionContaining(ga) == null || fm.getFunctionContaining(ga) == f);
                    }
                    if (pad) { hi = off(rg.getMaxAddress()); grew = true; break; }
                }
            }
            u.size = hi - lo + 1;
            if (f.isThunk() && f.getThunkedFunction(true) != null) {
                Address ta = f.getThunkedFunction(true).getEntryPoint();
                if (neSeg(ta) > 0) u.note = "thunk: JMP " + key(neSeg(ta), off(ta));
            }
            if (covered[seg] == null) covered[seg] = new boolean[(int) currentProgram.getMemory().getBlock("Code" + seg).getSize() + 1];
            for (int o = lo; o <= hi; o++) covered[seg][o] = true;
            units.add(u);
        }
        // Range rows: bytes of a C runtime segment that no function run covers (outside 0x90 padding) get a row
        // of their own, so nothing in the segment is silently unaccounted for. The row says what Ghidra holds there.
        for (int seg : CRT_SEGS) {
            MemoryBlock blk = currentProgram.getMemory().getBlock("Code" + seg);
            if (blk == null || covered[seg] == null) continue;
            int n = (int) blk.getSize();
            int o = 0;
            while (o < n) {
                if (covered[seg][o]) { o++; continue; }
                int s = o;
                while (o < n && !covered[seg][o]) o++;
                int e2 = o - 1;
                // 0x90 (NOP) and 0x00 bytes are alignment padding between functions; the segment header (offset 0) is kept whole.
                while (s > 0 && s <= e2 && isPad(getByte(blk.getStart().add(s)))) s++;
                while (e2 >= s && isPad(getByte(blk.getStart().add(e2)))) e2--;
                if (s > e2) continue;
                Unit u = new Unit();
                u.seg = seg; u.off = s; u.size = e2 - s + 1; u.f = null;
                u.name = "RANGE_" + seg + "_" + String.format("%04x", s);
                AddressSet as = new AddressSet(blk.getStart().add(s), blk.getStart().add(e2));
                u.body = as;
                int ins = 0, bodyBytes = 0;
                Set<String> owners = new TreeSet<>();
                for (int q = s; q <= e2; q++) {
                    Address qa = blk.getStart().add(q);
                    CodeUnit cu = listing.getCodeUnitContaining(qa);
                    if (cu instanceof Instruction) ins++;
                    Function g = fm.getFunctionContaining(qa);
                    if (g != null) { bodyBytes++; owners.add(g.getName()); }
                }
                u.note = "No function run covers this range (" + u.size + " bytes at " + key(seg, s) + "): Ghidra holds "
                    + ins + " of them as instructions and " + (u.size - ins) + " as undefined bytes"
                    + (bodyBytes > 0 ? "; " + bodyBytes + " lie in the detached body of " + owners : "; none lies in a Ghidra function")
                    + ". No function boundary is established; classify by hand (code with x87 emulator fixups, a shared tail or data).";
                units.add(u);
            }
        }
        units.sort(Comparator.<Unit>comparingInt(u -> u.seg).thenComparingInt(u -> u.off));
        for (Unit u : units) {
            for (Instruction ins : listing.getInstructions(u.body, true)) {
                String m = ins.getMnemonicString();
                if (m.equals("RET")) u.near = true;
                else if (m.equals("RETF")) u.far = true;
                if (m.startsWith("F")) u.x87 = true;
                for (Reference r : ins.getReferencesFrom()) {
                    RefType t = r.getReferenceType();
                    if (!(t.isCall() || (t.isJump() && !t.isComputed() && !t.isConditional()))) continue;
                    Address to = r.getToAddress();
                    if (ext != null && ext.contains(to)) {
                        Function x = fm.getFunctionAt(to);
                        if (x != null) u.ap.add(x.getParentNamespace().getName() + "." + x.getName());
                        continue;
                    }
                    if (!t.isCall()) continue; // only calls link functions; jumps stay inside a function
                    Function g = fm.getFunctionContaining(to);
                    if (g == null || g == u.f || neSeg(g.getEntryPoint()) < 1) continue;
                    String gk = key(neSeg(g.getEntryPoint()), off(g.getEntryPoint()));
                    u.ce.add(gk);
                    callers.computeIfAbsent(gk, z -> new TreeSet<>()).add(u.k());
                }
            }
        }

        StringBuilder sb = new StringBuilder(HEADER).append('\n');
        int kept = 0;
        for (Unit u : units) {
            String k = u.k();
            String[] man = old.get(u.seg + ":" + String.format("0x%04x", u.off));
            String origin = man != null ? man[0] : (CRT_SEGS.contains(u.seg) ? "crt" : "game");
            String status = man != null ? man[1] : "todo";
            String notes = man != null && !isAutoNote(man[2]) ? man[2] : u.note;
            if (man != null) kept++;
            sb.append(u.seg).append(',').append(String.format("0x%04x", u.off)).append(',')
              .append(csv(u.name)).append(',').append(u.size).append(',')
              .append(u.near && u.far ? "mixed" : u.near ? "near" : u.far ? "far" : "none").append(',')
              .append(String.join(" ", callers.getOrDefault(k, new TreeSet<>()))).append(',')
              .append(String.join(" ", u.ce)).append(',')
              .append(String.join(" ", u.ap)).append(',')
              .append(u.x87 ? 1 : 0).append(',').append(origin).append(',').append(status).append(',')
              .append(csv(notes)).append('\n');
        }
        // A manual-column row whose function is gone must not vanish silently.
        Set<String> present = new HashSet<>();
        for (Unit u : units) present.add(u.seg + ":" + String.format("0x%04x", u.off));
        int lost = 0;
        for (Map.Entry<String, String[]> en : old.entrySet()) {
            if (present.contains(en.getKey())) continue;
            String[] m = en.getValue();
            boolean custom = !m[1].equals("todo") || !(m[2].isEmpty() || isAutoNote(m[2])) || !(m[0].equals("game") || m[0].equals("crt"))
                || m[0].equals(CRT_SEGS.contains(Integer.parseInt(en.getKey().split(":")[0])) ? "game" : "crt");
            println("INVENTORY row disappeared: " + en.getKey() + " origin=" + m[0] + " status=" + m[1] + " notes=" + m[2]);
            if (custom) lost++;
        }
        if (lost > 0) throw new RuntimeException("INVENTORY " + lost + " vanished row(s) carry non-default manual values; not writing " + out);
        Files.createDirectories(outPath.toAbsolutePath().getParent());
        Files.write(outPath, sb.toString().getBytes(StandardCharsets.UTF_8));
        println("INVENTORY wrote " + units.size() + " rows (" + funcs.size() + " functions) to " + out + " (manual columns kept for " + kept + ")");
    }

    // Notes this script writes itself (regenerated every run, so they are not manual values).
    static boolean isAutoNote(String n) { return n.startsWith("No function run covers") || n.startsWith("thunk: JMP "); }

    static boolean isPad(byte b) { return b == (byte) 0x90 || b == 0; }

    static String csv(String s) {
        if (s.indexOf(',') < 0 && s.indexOf('"') < 0 && s.indexOf('\n') < 0) return s;
        return "\"" + s.replace("\"", "\"\"") + "\"";
    }

    // Parses a whole CSV text: quoted fields may contain commas, doubled quotes and newlines.
    static List<List<String>> parseCsvText(String text) {
        List<List<String>> rows = new ArrayList<>();
        List<String> cur = new ArrayList<>();
        StringBuilder f = new StringBuilder();
        boolean q = false, any = false;
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            if (q) {
                if (c == '"') { if (i + 1 < text.length() && text.charAt(i + 1) == '"') { f.append('"'); i++; } else q = false; }
                else f.append(c);
            } else if (c == '"') { q = true; any = true; }
            else if (c == ',') { cur.add(f.toString()); f.setLength(0); any = true; }
            else if (c == '\n' || c == '\r') {
                if (c == '\r' && i + 1 < text.length() && text.charAt(i + 1) == '\n') i++;
                if (any || f.length() > 0) { cur.add(f.toString()); rows.add(cur); }
                cur = new ArrayList<>(); f.setLength(0); any = false;
            } else { f.append(c); any = true; }
        }
        if (any || f.length() > 0) { cur.add(f.toString()); rows.add(cur); }
        return rows;
    }
}

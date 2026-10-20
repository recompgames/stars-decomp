// InventoryFacts.java: static facts for docs/inventory.md, written to re/facts.txt (one record per line,
// fields separated by "|"). Addresses are NE seg:off. Facts: DOS3Call sites and AH, CD 21 byte pairs,
// writes into code segments, SendMessage/SendDlgItemMessage message IDs, window class names, resource access.
// Script argument 1 (optional): output file, default <cwd>/re/facts.txt.
//@category Stars
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.nio.file.*;
import java.util.*;
import java.util.regex.*;

public class InventoryFacts extends GhidraScript {
    PrintWriter out;
    Listing listing;
    FunctionManager fm;
    MemoryBlock ext;
    Map<String, Integer> argWords = new HashMap<>();   // "MODULE.NAME" (upper case) -> words popped by callee

    // getScalar() proved unreliable on some instructions; read the operand objects instead.
    static Scalar scalarOf(Instruction ins, int idx) {
        if (idx >= ins.getNumOperands()) return null;
        for (Object o : ins.getOpObjects(idx)) if (o instanceof Scalar) return (Scalar) o;
        return null;
    }

    static int seg(Address a) { int s = ((SegmentedAddress) a).getSegment(); return (s - 0x1000) / 8 + 1; }
    static int off(Address a) { return ((SegmentedAddress) a).getSegmentOffset(); }
    static String ne(Address a) { return seg(a) + ":0x" + String.format("%04x", off(a)); }

    String apiName(Address to) {
        if (ext == null || !ext.contains(to)) return null;
        Function x = fm.getFunctionAt(to);
        return x == null ? null : x.getParentNamespace().getName() + "." + x.getName();
    }

    @Override
    public void run() throws Exception {
        String repo = System.getProperty("user.dir");
        String path = getScriptArgs().length > 0 ? getScriptArgs()[0] : repo + "/re/facts.txt";
        listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager();
        ext = currentProgram.getMemory().getBlock("EXTERNAL");
        loadArgBytes(Paths.get(repo, "runtime/win16_api.inc"));
        out = new PrintWriter(new FileWriter(path));
        coverage();
        x87();
        dos3call();
        int21pairs();
        codeWrites();
        messages();
        classes();
        resources();
        out.close();
        println("FACTS written " + path);
    }

    void loadArgBytes(Path inc) throws IOException {
        Pattern p = Pattern.compile("^W16API\\((\\w+), (\\d+), \"(\\w+)\", (\\w+), (\\d+), (\\w+)\\)");
        for (String l : Files.readAllLines(inc)) {
            Matcher m = p.matcher(l);
            if (m.find()) argWords.put((m.group(1) + "." + m.group(3)).toUpperCase(), Integer.parseInt(m.group(5)) / 2);
        }
    }

    // ---- per-segment coverage: COVER|seg|memory size|instruction bytes|defined data bytes|undefined bytes ----
    void coverage() {
        for (int n = 1; n <= 34; n++) {
            MemoryBlock b = currentProgram.getMemory().getBlock("Code" + n);
            if (b == null) continue;
            long ins = 0, data = 0, undef = 0;
            for (CodeUnit cu : listing.getCodeUnits(new AddressSet(b.getStart(), b.getEnd()), true)) {
                if (cu instanceof Instruction) ins += cu.getLength();
                else if (cu instanceof Data && ((Data) cu).isDefined()) data += cu.getLength();
                else undef += cu.getLength();
            }
            out.println("COVER|" + n + "|" + b.getSize() + "|" + ins + "|" + data + "|" + undef);
        }
    }

    // ---- x87 instructions Ghidra decodes: X87|MNEMONIC|seg|count ----
    void x87() {
        TreeMap<String, Integer> cnt = new TreeMap<>();
        for (Instruction ins : listing.getInstructions(true)) {
            String m = ins.getMnemonicString();
            if (!m.startsWith("F") || !(ins.getAddress() instanceof SegmentedAddress)) continue;
            int s = seg(ins.getAddress());
            if (s < 1 || s > 34) continue;
            cnt.merge(m + "|" + s, 1, Integer::sum);
        }
        for (Map.Entry<String, Integer> e : cnt.entrySet()) out.println("X87|" + e.getKey() + "|" + e.getValue());
    }

    // ---- helpers over instruction streams ----
    List<Instruction> callSites(String api) {
        List<Instruction> r = new ArrayList<>();
        for (Instruction ins : listing.getInstructions(true)) {
            if (!ins.getFlowType().isCall() && !ins.getFlowType().isJump()) continue;
            for (Reference ref : ins.getReferencesFrom()) {
                String n = apiName(ref.getToAddress());
                if (n != null && n.equalsIgnoreCase(api)) r.add(ins);
            }
        }
        return r;
    }

    String ctx(Instruction ins, int back) {
        LinkedList<String> l = new LinkedList<>();
        Instruction p = ins;
        for (int i = 0; i <= back && p != null; i++) { l.addFirst(ne(p.getAddress()) + " " + p); p = p.getPrevious(); }
        return String.join(" ; ", l);
    }

    // value of AH just before 'ins' (linear back-scan), or null
    String ahBefore(Instruction ins) {
        Instruction p = ins.getPrevious();
        for (int i = 0; i < 40 && p != null; i++, p = p.getPrevious()) {
            String m = p.getMnemonicString();
            // The C runtime wraps every DOS call as: TEST word ptr CS:[0x10],1 ; JZ <INT 21h> ; CALLF DOS3Call.
            if (m.equals("JZ") && p.getPrevious() != null && p.getPrevious().toString().startsWith("TEST word ptr CS:[0x10]")) { p = p.getPrevious(); continue; }
            String o0 = p.getNumOperands() > 0 ? p.getDefaultOperandRepresentation(0) : "";
            Scalar s1 = p.getNumOperands() > 1 ? scalarOf(p, 1) : null;
            if (m.equals("MOV") && o0.equals("AH")) return s1 != null ? String.format("0x%02x", s1.getUnsignedValue()) : "dynamic: " + p;
            if (m.equals("MOV") && o0.equals("AX")) return s1 != null ? String.format("0x%02x", (s1.getUnsignedValue() >> 8) & 0xff) : "dynamic: " + p;
            if (m.equals("XOR") && (o0.equals("AX") || o0.equals("AH")) && p.getDefaultOperandRepresentation(1).equals(o0)) return "0x00";
            if (m.equals("CALL") || m.equals("CALLF") || m.equals("INT") || m.equals("CBW") || m.equals("CWD") || m.equals("XCHG") || m.equals("LODSB") || m.equals("LODSW"))
                return "dynamic: " + m + " clobbers AX before any constant (" + p + ")";
            if ((m.equals("ADD") || m.equals("SUB") || m.equals("AND") || m.equals("OR") || m.equals("MOV") || m.equals("SHL") || m.equals("SHR") || m.equals("INC") || m.equals("DEC") || m.equals("LEA") || m.equals("NEG") || m.equals("NOT") || m.equals("POP"))
                    && o0.equals("AX")) return "dynamic: AX computed (" + p + ")";
            if (p.getFlowType().isTerminal() || p.getFlowType().isJump()) return "dynamic: reached block boundary (" + p + ")";
        }
        return "dynamic: no assignment within 40 instructions";
    }

    void dos3call() {
        List<Instruction> sites = callSites("KERNEL.DOS3CALL");
        out.println("DOS3|count|" + sites.size());
        for (Instruction ins : sites) {
            Function f = fm.getFunctionContaining(ins.getAddress());
            out.println("DOS3|" + ne(ins.getAddress()) + "|" + (f == null ? "-" : ne(f.getEntryPoint()) + " " + f.getName()) + "|" + ahBefore(ins) + "|" + ctx(ins, 8));
        }
    }

    // ---- CD 21 pairs in code segments ----
    void int21pairs() throws Exception {
        int n = 0;
        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            if (!b.getName().startsWith("Code")) continue;
            byte[] buf = new byte[(int) b.getSize()];
            b.getBytes(b.getStart(), buf);
            for (int o = 0; o + 1 < buf.length; o++) {
                if ((buf[o] & 0xff) != 0xcd || (buf[o + 1] & 0xff) != 0x21) continue;
                n++;
                Address a = b.getStart().add(o);
                CodeUnit cu = listing.getCodeUnitContaining(a);
                String kind;
                if (cu instanceof Instruction && cu.getAddress().equals(a) && ((Instruction) cu).getMnemonicString().equals("INT")) kind = "real INT 21h instruction";
                else if (cu instanceof Instruction) kind = "inside instruction at " + ne(cu.getAddress()) + " (" + cu + ")";
                else if (cu instanceof Data && ((Data) cu).isDefined()) kind = "defined data " + cu;
                else kind = "undefined bytes, not disassembled";
                Function f = fm.getFunctionContaining(a);
                out.println("CD21|" + ne(a) + "|" + kind + "|" + (f == null ? "no function" : f.getName()) + "|" + hex(buf, Math.max(0, o - 6), Math.min(buf.length, o + 8)));
            }
        }
        out.println("CD21|count|" + n);
    }

    static String hex(byte[] b, int lo, int hi) {
        StringBuilder sb = new StringBuilder();
        for (int i = lo; i < hi; i++) sb.append(String.format("%02x ", b[i] & 0xff));
        return sb.toString().trim();
    }

    // ---- writes into code segments (self-modifying code) ----
    void codeWrites() {
        int n = 0, cs = 0;
        for (Instruction ins : listing.getInstructions(true)) {
            for (Reference r : ins.getReferencesFrom()) {
                RefType t = r.getReferenceType();
                if (!(t.isWrite())) continue;
                MemoryBlock b = currentProgram.getMemory().getBlock(r.getToAddress());
                if (b != null && b.getName().startsWith("Code")) { n++; out.println("CODEWRITE|" + ne(ins.getAddress()) + "|" + ins + "|-> " + ne(r.getToAddress())); }
            }
            String s = ins.toString();
            if (s.contains("CS:")) {
                cs++;
                String o0 = ins.getNumOperands() > 0 ? ins.getDefaultOperandRepresentation(0) : "";
                String mn = ins.getMnemonicString();
                boolean dstMem = o0.contains("CS:");
                boolean writer = mn.matches("MOV|ADD|SUB|ADC|SBB|AND|OR|XOR|INC|DEC|NEG|NOT|SHL|SHR|SAR|SAL|ROL|ROR|RCL|RCR|POP|XCHG|XADD|CMPXCHG|SETcc|FST|FSTP|FIST|FISTP|FSTCW|FNSTCW|FSTSW|FNSTSW|FBSTP|MOVS|STOS|BTS|BTR|BTC");
                out.println("CSOVERRIDE|" + ne(ins.getAddress()) + "|" + s + "|" + (dstMem && writer ? "WRITE to code segment" : dstMem ? "read-only (" + mn + " does not write its memory operand)" : "read-only use (source operand)"));
            }
        }
        out.println("CODEWRITE|count|" + n);
        out.println("CSOVERRIDE|count|" + cs);
    }

    // ---- constant message IDs ----
    // A word position found by walking back from a call.
    static class PushRef { Instruction ins; int half; boolean crossed; }   // half 0 = low (pushed last) word, 1 = high word of a dword push

    // 16-bit code: an operand-size prefix (0x66) makes PUSH push two words.
    static int pushWords(Instruction p) {
        try {
            for (byte x : p.getBytes()) {
                int b = x & 0xff;
                if (b == 0x66) return 2;
                if (b != 0x26 && b != 0x2e && b != 0x36 && b != 0x3e && b != 0x64 && b != 0x65 && b != 0x67) break;
            }
        } catch (Exception e) { /* fall through */ }
        return 1;
    }

    // Finds the push that supplies the k-th word counted back from the call (k=1 is the last word pushed),
    // skipping the arguments of nested calls (Pascal callees pop their own; cdecl callers ADD SP,n).
    PushRef pushFromEnd(Instruction call, int k) {
        crossed = false;
        int words = 0, skip = 0;
        Instruction p = call.getPrevious();
        for (int i = 0; i < 120 && p != null; i++, p = p.getPrevious()) {
            String m = p.getMnemonicString();
            if (m.equals("CALL") || m.equals("CALLF")) {
                int w = 0; boolean known = false;
                for (Reference r : p.getReferencesFrom()) {
                    if (!r.getReferenceType().isCall()) continue;
                    String a = apiName(r.getToAddress());
                    if (a != null) { Integer x = argWords.get(a.toUpperCase()); if (x != null) { w = x; known = true; } }
                    else { Integer x = retWords(r.getToAddress()); if (x != null) { w = x; known = true; } }
                }
                if (!known) return null;
                skip += w;
            } else if (m.equals("ADD") && p.getDefaultOperandRepresentation(0).equals("SP") && scalarOf(p, 1) != null) {
                skip += (int) scalarOf(p, 1).getUnsignedValue() / 2;
            } else if (m.equals("PUSH")) {
                int w = pushWords(p);
                if (skip >= w) { skip -= w; }
                else if (skip > 0) return null;
                else {
                    if (words + w >= k) { PushRef r = new PushRef(); r.ins = p; r.half = k - words - 1; r.crossed = crossed; return r; }
                    words += w;
                }
            } else if (m.equals("PUSHA") || m.equals("ENTER") || m.startsWith("RET") || m.equals("IRET") || m.equals("HLT")) {
                return null;
            } else if (p.getFlowType().isJump()) {
                // Argument expressions such as "cond ? a : b" compile to forward jumps inside the push
                // sequence. Cross a jump only when it lands between itself and the call.
                Address t = null;
                for (Reference r : p.getReferencesFrom()) if (r.getReferenceType().isJump()) t = r.getToAddress();
                if (t == null || t.compareTo(p.getAddress()) <= 0 || t.compareTo(call.getAddress()) > 0) return null;
                crossed = true;
            }
        }
        return null;
    }

    boolean crossed = false;

    Integer retWords(Address target) {
        Function g = fm.getFunctionContaining(target);
        if (g == null) return null;
        for (Instruction ins : listing.getInstructions(g.getBody(), true)) {
            String m = ins.getMnemonicString();
            if (m.equals("RET") || m.equals("RETF")) return ins.getNumOperands() > 0 && scalarOf(ins, 0) != null ? (int) scalarOf(ins, 0).getUnsignedValue() / 2 : 0;
        }
        return null;
    }

    // constant 16-bit value supplied by the referenced word, else null
    String pushValue(PushRef pr) {
        if (pr == null) return null;
        Instruction push = pr.ins;
        String rep0 = push.getDefaultOperandRepresentation(0);
        boolean memOperand = rep0.contains("[") || rep0.contains("ptr");
        Scalar s = memOperand ? null : scalarOf(push, 0);
        if (s != null) {
            long v = s.getUnsignedValue();
            if (pushWords(push) == 2) v = pr.half == 1 ? (v >> 16) & 0xffff : v & 0xffff;
            else v &= 0xffff;
            return String.format("0x%x", v);
        }
        if (pushWords(push) == 2) return null;
        String reg = push.getDefaultOperandRepresentation(0);
        Instruction p = push.getPrevious();
        for (int i = 0; i < 12 && p != null; i++, p = p.getPrevious()) {
            if (p.getNumOperands() > 0 && p.getDefaultOperandRepresentation(0).equals(reg)) {
                if (p.getMnemonicString().equals("MOV") && scalarOf(p, 1) != null) return String.format("0x%x", scalarOf(p, 1).getUnsignedValue() & 0xffff);
                if (p.getMnemonicString().equals("XOR") && p.getDefaultOperandRepresentation(1).equals(reg)) return "0x0";
                return null;
            }
            if (p.getFlowType().isTerminal() || p.getFlowType().isJump() || p.getFlowType().isCall()) return null;
        }
        return null;
    }

    // "[0xNNN]" direct DS operand of a word push -> initial word stored there, else null
    Integer directDsWord(PushRef pr) {
        if (pr == null || pushWords(pr.ins) != 1) return null;
        String r = pr.ins.getDefaultOperandRepresentation(0);
        Matcher m = Pattern.compile("^word ptr \\[(0x[0-9a-f]+)\\]$").matcher(r);
        if (!m.find()) return null;
        try {
            Address a = toAddr(String.format("1110:%04x", Integer.decode(m.group(1))));
            return currentProgram.getMemory().getShort(a) & 0xffff;
        } catch (Exception e) { return null; }
    }

    String pushDesc(PushRef pr) { return pr == null ? "unresolved" : pr.ins.toString() + (pushWords(pr.ins) == 2 ? " (dword, " + (pr.half == 1 ? "high" : "low") + " word)" : " (word)") + (pr.crossed ? " [crossed branch]" : ""); }

    void messages() {
        String[][] apis = {{"USER.SENDMESSAGE", "5"}, {"USER.SENDDLGITEMMESSAGE", "6"}};
        // SendMessage(hwnd, msg, wParam, lParam32): msg is word 2 of 5 -> 4th from end.
        // SendDlgItemMessage(hdlg, id, msg, wParam, lParam32): msg is word 3 of 6 -> 4th from end.
        for (String[] a : apis) {
            List<Instruction> sites = callSites(a[0]);
            out.println("MSG|" + a[0] + "|count|" + sites.size());
            for (Instruction ins : sites) {
                PushRef push = pushFromEnd(ins, 4);
                String v = pushValue(push);
                Function f = fm.getFunctionContaining(ins.getAddress());
                out.println("MSG|" + a[0] + "|" + ne(ins.getAddress()) + "|" + (f == null ? "-" : f.getName()) + "|" + (v == null ? "dynamic" : v) + "|" + pushDesc(push) + "|" + ctx(ins, 10));
            }
        }
    }

    // Immediate most recently stored to [BP+disp] (word) before 'ins', scanning back up to 300 instructions.
    Integer storedImm(Instruction ins, int disp) {
        Instruction p = ins.getPrevious();
        String want = "[BP + " + (disp < 0 ? "-0x" + Integer.toHexString(-disp) : "0x" + Integer.toHexString(disp)) + "]";
        for (int i = 0; i < 300 && p != null; i++, p = p.getPrevious()) {
            if (p.getMnemonicString().equals("MOV") && p.getNumOperands() == 2 && p.getDefaultOperandRepresentation(0).equals("word ptr " + want)) {
                String r1 = p.getDefaultOperandRepresentation(1);
                Scalar sc = scalarOf(p, 1);
                if (sc != null && !r1.contains("[")) return (int) (sc.getUnsignedValue() & 0xffff);
                return null;
            }
        }
        return null;
    }

    // ---- window classes ----
    String cstr(int segSel, int offset) {
        try {
            Address a = toAddr(String.format("%04x:%04x", segSel, offset));
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < 64; i++) { int b = currentProgram.getMemory().getByte(a.add(i)) & 0xff; if (b == 0) break; sb.append(b >= 32 && b < 127 ? (char) b : '?'); }
            return sb.toString();
        } catch (Exception e) { return null; }
    }

    void classes() {
        List<Instruction> reg = callSites("USER.REGISTERCLASS");
        out.println("CLASS|REGISTERCLASS|count|" + reg.size());
        for (Instruction ins : reg) {
            Function f = fm.getFunctionContaining(ins.getAddress());
            // RegisterClass(WNDCLASS far *): the struct is a BP-relative local (LEA AX,[BP+d] before the call).
            // WNDCLASS offsets: lpfnWndProc +2 (off) +4 (seg), lpszClassName +22 (off) +24 (seg).
            Integer d = null;
            Instruction q = ins.getPrevious();
            for (int i = 0; i < 6 && q != null && d == null; i++, q = q.getPrevious()) {
                if (q.getMnemonicString().equals("LEA")) {
                    Matcher m = Pattern.compile("\\[BP \\+ (-?0x[0-9a-f]+)\\]").matcher(q.getDefaultOperandRepresentation(1));
                    if (m.find()) d = Integer.decode(m.group(1));
                }
            }
            String name = "undetermined", proc = "undetermined";
            if (d != null) {
                Integer co = storedImm(ins, d + 22), po = storedImm(ins, d + 2), ps = storedImm(ins, d + 4);
                if (co != null) { String c = cstr(0x1110, co); name = c == null ? "undetermined" : c; }
                if (po != null && ps != null && ps >= 0x1000) proc = ((ps - 0x1000) / 8 + 1) + ":" + String.format("0x%04x", po);
            }
            out.println("CLASS|REGISTERCLASS|" + ne(ins.getAddress()) + "|" + (f == null ? "-" : ne(f.getEntryPoint()) + " " + f.getName()) + "|" + name + "|" + proc);
        }
        // CreateWindow(lpClassName32, lpWindowName32, style32, x, y, cx, cy, hParent, hMenu, hInst, lpParam32) = 15 words
        // class name offset = word 2 -> 14th from end.
        List<Instruction> cw = callSites("USER.CREATEWINDOW");
        out.println("CLASS|CREATEWINDOW|count|" + cw.size());
        for (Instruction ins : cw) {
            PushRef offPush = pushFromEnd(ins, 14);
            PushRef segPush = pushFromEnd(ins, 15);
            String o = pushValue(offPush);
            String sv = pushValue(segPush);
            String name = null;
            boolean segIsDs = segPush != null && (segPush.ins.getDefaultOperandRepresentation(0).equals("DS") || (sv != null && Integer.decode(sv) == 0x1110));
            if (o != null && segIsDs) name = cstr(0x1110, Integer.decode(o));
            Integer viaVar = (o == null && segIsDs) ? directDsWord(offPush) : null;
            if (viaVar != null) { name = "via variable " + offPush.ins.getDefaultOperandRepresentation(0) + " initial=" + String.format("0x%x", viaVar) + " -> " + cstr(0x1110, viaVar); o = "var"; }
            Function f = fm.getFunctionContaining(ins.getAddress());
            out.println("CLASS|CREATEWINDOW|" + ne(ins.getAddress()) + "|" + (f == null ? "-" : f.getName()) + "|" + (o == null ? "dynamic" : o) + "|" + (name == null ? "?" : name) + "|" + ctx(ins, 22));
        }
    }

    // ---- resource access (string tables) ----
    void resources() {
        for (String api : new String[] {"KERNEL.FINDRESOURCE", "KERNEL.LOADRESOURCE", "KERNEL.LOCKRESOURCE", "KERNEL.ACCESSRESOURCE", "USER.LOADBITMAP", "USER.LOADSTRING"}) {
            List<Instruction> sites = callSites(api);
            out.println("RES|" + api + "|count|" + sites.size());
            for (Instruction ins : sites) {
                Function f = fm.getFunctionContaining(ins.getAddress());
                out.println("RES|" + api + "|" + ne(ins.getAddress()) + "|" + (f == null ? "-" : ne(f.getEntryPoint()) + " " + f.getName()) + "|" + ctx(ins, 12));
            }
        }
    }
}

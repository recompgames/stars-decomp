// Label.java: make sure every NE export and the entry point is a function with its export name.
// Run after import (see docs/inventory.md): analyzeHeadless re/ghidra stars -process stars26i.exe
//   -scriptPath ghidra -postScript Label.java
// Idempotent. Logs every import reference that does not resolve to a named external function.
//@category Stars
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.*;
import java.util.*;

public class Label extends GhidraScript {
    @Override
    public void run() throws Exception {
        FunctionManager fm = currentProgram.getFunctionManager();
        SymbolTable st = currentProgram.getSymbolTable();
        int exports = 0, created = 0, renamed = 0, ordinalOnly = 0;
        AddressIterator it = st.getExternalEntryPointIterator();
        while (it.hasNext()) {
            Address a = it.next();
            String want = null;
            for (Symbol s : st.getSymbols(a)) {
                String n = s.getName();
                if (s.getSymbolType() == SymbolType.FUNCTION && !n.startsWith("Ordinal_")) want = n;
                else if (want == null && !n.startsWith("Ordinal_")) want = n;
            }
            boolean isEntry = a.equals(entryPointAddress());
            if (isEntry) want = "entry";
            if (want == null) { ordinalOnly++; println("LABEL no export name at " + a); continue; }
            exports++;
            Function f = fm.getFunctionAt(a);
            if (f == null) {
                f = createFunction(a, want);
                if (f == null) { println("LABEL cannot create function " + want + " at " + a); continue; }
                created++;
            }
            if (!f.getName().equals(want)) { f.setName(want, SourceType.IMPORTED); renamed++; }
        }
        println("LABEL entries=" + exports + " created=" + created + " renamed=" + renamed + " unnamed=" + ordinalOnly);

        recoverFunctions();
        settleAnalysis();

        // Every reference into the EXTERNAL block (import thunks) must resolve to a named function.
        MemoryBlock ext = currentProgram.getMemory().getBlock("EXTERNAL");
        int total = 0, bad = 0;
        Set<String> seen = new TreeSet<>();
        for (Instruction ins : currentProgram.getListing().getInstructions(true)) {
            for (Reference r : ins.getReferencesFrom()) {
                Address t = r.getToAddress();
                if (ext == null || !ext.contains(t)) continue;
                total++;
                Function f = fm.getFunctionAt(t);
                if (f == null || f.getName().startsWith("Ordinal_") || f.getName().startsWith("FUN_")) {
                    bad++;
                    println("UNRESOLVED import ref " + ins.getAddress() + " -> " + t + " " + (f == null ? "(no function)" : f.getName()));
                } else {
                    seen.add(f.getParentNamespace().getName() + "." + f.getName());
                }
            }
        }
        println("LABEL import refs=" + total + " unresolved=" + bad + " distinct imports used=" + seen.size());
    }

    private Address entryPointAddress() {
        // The NE entry point is the only external entry point named "entry" by the loader.
        for (Symbol s : currentProgram.getSymbolTable().getSymbols("entry")) return s.getAddress();
        return null;
    }

    // Auto-analysis leaves some code undisassembled. Recover it from evidence only:
    //  (a) the target of a direct call that lies in no function;
    //  (c) undefined bytes that follow a non-call instruction with fall-through (disassembly continued);
    //  (b) a compiler prologue (ENTER n,0 then PUSH SI/DI, or PUSH BP; MOV BP,SP) in bytes that are
    //      neither instructions nor defined data.
    // Repeated until nothing new is found, so the result is stable (idempotent) across runs.
    private void recoverFunctions() throws Exception {
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        MemoryBlock ext = currentProgram.getMemory().getBlock("EXTERNAL");
        int totalA = 0, totalB = 0, totalC = 0, failed = 0;
        for (int pass = 1; pass <= 20; pass++) {
            TreeSet<Address> byCall = new TreeSet<>();
            for (Instruction ins : listing.getInstructions(true)) {
                if (!ins.getFlowType().isCall()) continue;
                for (Reference r : ins.getReferencesFrom()) {
                    Address t = r.getToAddress();
                    if (!r.getReferenceType().isCall() || (ext != null && ext.contains(t))) continue;
                    MemoryBlock b = currentProgram.getMemory().getBlock(t);
                    if (b == null || !b.getName().startsWith("Code")) continue;
                    if (fm.getFunctionContaining(t) == null) byCall.add(t);
                }
            }
            // (c) Fall-through into undefined bytes: Ghidra's flow disassembly sometimes stops in the middle of a
            // routine (seen in segment 34 at `8B 46 06` and `8B EC`). Continue the disassembly there. Calls are
            // skipped because the callee may not return (the bytes after such a call can be data).
            TreeMap<Address, Address> byFlow = new TreeMap<>();
            for (Instruction ins : listing.getInstructions(true)) {
                if (!ins.getFlowType().hasFallthrough() || ins.getFlowType().isCall()) continue;
                Address n = ins.getFallThrough();
                if (n == null) n = ins.getMaxAddress().next();
                if (n == null) continue;
                MemoryBlock nb = currentProgram.getMemory().getBlock(n);
                if (nb == null || !nb.getName().startsWith("Code")) continue;
                CodeUnit cu = listing.getCodeUnitAt(n);
                if (cu instanceof Instruction) continue;
                if (cu instanceof Data && ((Data) cu).isDefined()) continue;
                byFlow.put(n, ins.getAddress());
            }
            TreeSet<Address> byPrologue = new TreeSet<>();
            for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
                if (!b.getName().startsWith("Code")) continue;
                byte[] buf = new byte[(int) b.getSize()];
                b.getBytes(b.getStart(), buf);
                for (int o = 0; o + 5 < buf.length; o++) {
                    int b0 = buf[o] & 0xff, b1 = buf[o + 1] & 0xff, b2 = buf[o + 2] & 0xff, b3 = buf[o + 3] & 0xff, b4 = buf[o + 4] & 0xff;
                    boolean enter = b0 == 0xc8 && b3 == 0 && (b4 == 0x56 || b4 == 0x57);
                    boolean push = b0 == 0x55 && b1 == 0x8b && b2 == 0xec;
                    if (!enter && !push) continue;
                    int start = o;
                    if (push) {
                        // MS C far prologues: [MOV AX,DS (8C D8) | PUSH DS; POP AX (1E 58)]; NOP (90); INC BP (45);
                        // PUSH BP; MOV BP,SP. The entry is the first byte of that sequence, not the PUSH BP.
                        if (o >= 1 && (buf[o - 1] & 0xff) == 0x45) {
                            start = o - 1;
                            if (start >= 3 && (buf[start - 1] & 0xff) == 0x90
                                && (((buf[start - 3] & 0xff) == 0x8c && (buf[start - 2] & 0xff) == 0xd8)
                                    || ((buf[start - 3] & 0xff) == 0x1e && (buf[start - 2] & 0xff) == 0x58))) start -= 3;
                        }
                    }
                    Address a = b.getStart().add(start);
                    boolean free = true;
                    for (int q = start; q <= o && free; q++) {
                        CodeUnit cu = listing.getCodeUnitContaining(b.getStart().add(q));
                        if (cu instanceof Instruction) free = false;
                        else if (cu instanceof Data && ((Data) cu).isDefined()) free = false;
                    }
                    if (!free) continue;
                    if (fm.getFunctionContaining(a) != null) continue;
                    byPrologue.add(a);
                }
            }
            if (byCall.isEmpty() && byPrologue.isEmpty() && byFlow.isEmpty()) break;
            int made = 0;
            for (Map.Entry<Address, Address> en : byFlow.entrySet()) {
                if (listing.getCodeUnitAt(en.getKey()) instanceof Instruction) continue;
                Function g = fm.getFunctionContaining(en.getValue());
                String gname = g == null ? null : g.getName();
                Address gentry = g == null ? null : g.getEntryPoint();
                disassemble(en.getKey());
                if (listing.getInstructionAt(en.getKey()) == null) {
                    // Instructions decoded from a wrong alignment overlap the bytes; clear that stray code
                    // (it belongs to no function) up to the next function or defined data, then retry.
                    Address cur = en.getKey(), last = null;
                    for (int guard = 0; guard < 512 && cur != null; guard++) {
                        CodeUnit cu = listing.getCodeUnitContaining(cur);
                        if (cu == null || fm.getFunctionContaining(cur) != null) break;
                        if (cu instanceof Data && ((Data) cu).isDefined()) break;
                        last = cu.getMaxAddress();
                        cur = last.next();
                    }
                    if (last != null) clearListing(en.getKey(), last);
                    disassemble(en.getKey());
                }
                if (listing.getInstructionAt(en.getKey()) == null) { failed++; continue; }
                totalC++; made++;
                if (g != null) {
                    // Re-create the function so that its body includes the newly disassembled code.
                    fm.removeFunction(gentry);
                    Function h = createFunction(gentry, null);
                    if (h != null && gname != null && !gname.equals(h.getName())) h.setName(gname, SourceType.IMPORTED);
                }
            }
            for (Address a : byCall) { if (make(a)) { totalA++; made++; } else failed++; }
            for (Address a : byPrologue) { if (fm.getFunctionContaining(a) == null && make(a)) { totalB++; made++; } else if (fm.getFunctionContaining(a) == null) failed++; }
            println("LABEL recover pass " + pass + ": call-target candidates=" + byCall.size() + " fall-through candidates=" + byFlow.size() + " prologue candidates=" + byPrologue.size() + " created=" + made);
            if (made == 0) break;
        }
        println("LABEL recovered functions: by call target=" + totalA + " by prologue=" + totalB + "; fall-through continuations=" + totalC + " failed=" + failed);
    }

    private boolean make(Address a) {
        try {
            disassemble(a);
            return createFunction(a, null) != null;
        } catch (Exception e) {
            return false;
        }
    }

    // Switch tables and other computed jumps/calls are resolved by analyzers that Ghidra runs on the
    // changes a script made. Run them now so the call graph that ExportInventory reads is already
    // complete on the first run (otherwise the inventory would change between run 1 and run 2).
    private void settleAnalysis() throws Exception {
        analyzeChanges(currentProgram);
        println("LABEL analyzed changes (switch tables, computed calls)");
    }
}

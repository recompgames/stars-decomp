/* Decompiled code segment 26 (NE segment 26, selector 0x00D0).
 * Original: stars26i.exe. Oracle: the original machine code, diffed by
 * tests/scenarios/test_seg26.c; Ghidra's pseudo-C in re/decomp/seg26 is only a reading aid.
 *
 * Segment 26 is the planet production screens: the production queue dialog of the selected planet
 * (template 0x5d: two lists, the items that can be added and the planet's queue, buttons to add, remove, move
 * and clear, a cost panel under the lists) and the ZIP production dialog (template 0x59: four saved queues):
 *   26:0x0000 production_dialog          run the production queue dialog of the selected planet
 *   26:0x0100 production_open_queue      copy the planet's queue and list what can be added to it
 *   26:0x0810 production_close_queue     commit or discard the edited queue
 *   26:0x08e2 production_dialog_proc     dialog procedure of the production queue dialog
 *   26:0x0e74 production_dialog_command  its WM_COMMAND handler (add, remove, clear, load a ZIP preset, ...)
 *   26:0x1a40 production_init_lists      title, lists and research check box of the dialog
 *   26:0x1b5a production_paint_cost_panel  the cost panel under the lists
 *   26:0x1f42 production_fill_item_list  the list box of items that can be added
 *   26:0x2058 production_item_name       the name of a production item
 *   26:0x21e4 production_item_cost       the cost of a production item (iron, boron, germanium, resources)
 *   26:0x29da production_simulate_queue  turns until a queue entry is finished
 *   26:0x2d26 ZIPPRODDLG                 dialog procedure of the ZIP production dialog
 *   26:0x33e4 zip_prod_enable_buttons    enable the reset and rename buttons of a ZIP preset
 *   26:0x3428 zip_prod_fill_preset_list  show a ZIP preset in the item list
 * Names: the export name ZIPPRODDLG is kept; Ghidra's others are all generic (FUN_10c8_<off>), so the C names describe
 * what each function does (CONVENTIONS rule f); they are descriptive, not the game's. "Item", "class" and the
 * auto-build items (class 1, index 0 to 6: automatic mines, factories, defenses, alchemy, terraforming, packets)
 * are read from the code and the item tables (2:0x50f8), not confirmed against the game's own wording. The old
 * fn_26_<off> names are kept as aliases in the comment above each function. 26:0x21e4 is larger than the inventory
 * says: the jump table at 26:0x2650 ends the listed 1132 bytes, the case bodies up to 26:0x29d9 belong to it.
 *
 * The functions mirror their original frames (CONVENTIONS rule d): FRAME_ENTER_REGS(n, pushes) with
 * n the ENTER size and one push for each PUSH SI / PUSH DI. A callee that mirrors a frame (rule d)
 * is reached through rt_call_far, code that is not decompiled yet through CALL_ORIG. Near calls
 * inside the segment are PUSH CS; CALL near, i.e. far calls with a 4-byte return address. Memory is
 * read and written in 16-bit pieces: an offset is a uint16_t and wraps like the original's.
 * DS = SS = DGROUP at every call. */
#include <string.h>

#include "src/stars.h"

static inline uint8_t rd8(uint16_t seg, uint16_t off) { return *(uint8_t *)LIN(MK_FP(seg, off)); }
static inline void wr8(uint16_t seg, uint16_t off, uint8_t v) { *(uint8_t *)LIN(MK_FP(seg, off)) = v; }
static inline uint16_t rd16(uint16_t seg, uint16_t off) {
  uint16_t v;
  memcpy(&v, LIN(MK_FP(seg, off)), 2);
  return v;
}
static inline void wr16(uint16_t seg, uint16_t off, uint16_t v) { memcpy(LIN(MK_FP(seg, off)), &v, 2); }
static inline uint32_t rd32(uint16_t seg, uint16_t off) {
  uint32_t v;
  memcpy(&v, LIN(MK_FP(seg, off)), 4);
  return v;
}
static inline void wr32(uint16_t seg, uint16_t off, uint32_t v) { memcpy(LIN(MK_FP(seg, off)), &v, 4); }
/* DGROUP (DS) bytes, words and dwords. */
static inline uint8_t drd8(uint16_t off) { return rd8(MEM_SEL_DGROUP, off); }
static inline void dwr8(uint16_t off, uint8_t v) { wr8(MEM_SEL_DGROUP, off, v); }
static inline uint16_t drd16(uint16_t off) { return rd16(MEM_SEL_DGROUP, off); }
static inline void dwr16(uint16_t off, uint16_t v) { wr16(MEM_SEL_DGROUP, off, v); }
static inline uint32_t drd32(uint16_t off) { return rd32(MEM_SEL_DGROUP, off); }
static inline void dwr32(uint16_t off, uint32_t v) { wr32(MEM_SEL_DGROUP, off, v); }
static inline uint32_t dsfp(uint16_t off) { return MK_FP(MEM_SEL_DGROUP, off); }

/* Stack locals of the current frame, at the original BP offsets. */
#define L8(o) BP_LOCAL(uint8_t, (o))
#define L16(o) BP_LOCAL(uint16_t, (o))
#define L32(o) BP_LOCAL(uint32_t, (o))
/* A far pointer to the stack local at BP + o (SS = DGROUP at every call), and its near offset. */
static inline uint32_t bpfp(int16_t o) { return MK_FP(rt_ss, (uint16_t)(rt_bp + o)); }
static inline uint16_t bpoff(int16_t o) { return (uint16_t)(rt_bp + o); }
/* PUSH word / dword on the emulated stack (the variable arguments of _wsprintf and the like). */
static inline void pushw(uint16_t v) {
  rt_sp = (uint16_t)(rt_sp - 2);
  wr16(rt_ss, rt_sp, v);
}
/* A by-name call that can run guest code (a dialog or window procedure, original machine code in the diff
 * tests): the original has pushed the call's argument bytes and the 4-byte far return address, so the
 * procedure runs that many bytes lower than the C frame's SP; rule d. */
#define API_SP(argbytes, expr)                                  \
  ({                                                            \
    uint16_t api_sp_ = rt_sp;                                   \
    rt_sp = (uint16_t)(api_sp_ - (uint16_t)(argbytes) - 4u);    \
    __typeof__(expr) api_r_ = (expr);                           \
    rt_sp = api_sp_;                                            \
    api_r_;                                                     \
  })
/* A far call (PUSH CS; CALL near, or LCALL) of a function that is decompiled and mirrors a frame. */
static inline uint32_t far_call(uint16_t seg, uint16_t off, const uint16_t *args, int16_t nargs) {
  return rt_call_far(MK_FP(SEG_SEL(seg), off), FAR_CDECL, args, nargs);
}
/* The code of this segment is called from here as segment 26. */
static inline uint32_t call26(uint16_t off, const uint16_t *args, int16_t nargs) { return far_call(26, off, args, nargs); }

/* 12:0x05be typed_array_free(p): frees the block of a typed array (the lists of this segment). */
static void free_typed_array(uint32_t p) {
  const uint16_t a[2] = {FP_OFF(p), FP_SEG(p)};
  (void)far_call(12, 0x05be, a, 2);
}

/* ---- the planet being edited and the production lists ----
 * DGROUP keeps a working copy of the selected planet's record (struct Planet, 0x38 bytes) at
 * DS:0x4c08 (cursor_planet); its +0x34 far pointer (DS:0x4c3c) is the planet's production queue
 * (ProdQueueHdr, then ProdItem entries of 4 bytes). While the dialog is open DS:0xcd2 (g_production_list)
 * is the edited copy of that queue. DS:0x5a62 / DS:0x6686 are the near pointer to and the entry count of
 * the list of items that can still be added ("available items": one ProdItem each, the quantity field
 * being how many may be added, 0x3ff for no limit). */
/* The window of the production dialog while it is open (DS:0xcd6, 0 otherwise) and the flag that the dialog is running (DS:0xcd8). */
#define production_dialog_hwnd NEAR_AT(uint16_t, 0x0cd6)
#define production_dialog_running NEAR_AT(uint16_t, 0x0cd8)
#define CURSOR_PLANET 0x4c08
#define CURSOR_PLANET_QUEUE 0x4c3c
/* Bits of a ProdItem (struct ProdItem in stars.h): quantity bits 0-9, item index bits 10-16, class bits
 * 17-19 (class 1 = planetary items, class 2 = ship and starbase designs). */
enum {
  PQ_QTY_MASK = 0x03ff,
  PQ_ITEM_W0_MASK = 0xfc00, /* the low 6 bits of the item index in w0; bit 6 is bit 0 of w1 */
  PQ_CLASS_MASK = 0x000e,   /* in w1 */
  PQ_CLASS_ITEM = 1,
  PQ_CLASS_DESIGN = 2,
  PQ_QTY_ANY = 0x03ff       /* "no limit" in the list of available items */
};
static inline struct ProdItem *pq_at(uint16_t base, uint16_t i) {
  return FAR_REC(struct ProdItem, MEM_SEL_DGROUP, (uint16_t)(base + (uint16_t)(i << 2)));
}
static inline void pq_set_qty(struct ProdItem *e, uint16_t qty) {
  e->w0 = (uint16_t)((e->w0 & ~PQ_QTY_MASK) | (qty & PQ_QTY_MASK));
}
static inline void pq_set_item(struct ProdItem *e, uint16_t item) {
  e->w0 = (uint16_t)((e->w0 & ~PQ_ITEM_W0_MASK) | ((uint16_t)(item << 10) & PQ_ITEM_W0_MASK));
  e->w1 = (uint16_t)((e->w1 & 0xfffe) | ((item >> 6) & 1));
}
static inline void pq_set_class(struct ProdItem *e, uint16_t cls) {
  e->w1 = (uint16_t)((e->w1 & ~PQ_CLASS_MASK) | ((uint16_t)(cls << 1) & PQ_CLASS_MASK));
}
/* Appends an entry to the list of available items (DS:0x5a62, count DS:0x6686). */
static void pq_append(uint16_t base, uint16_t qty, uint16_t item, uint16_t cls) {
  struct ProdItem *e = pq_at(base, g_entry_list_count_maybe);
  pq_set_qty(e, qty);
  pq_set_item(e, item);
  pq_set_class(e, cls);
  g_entry_list_count_maybe = (uint16_t)(g_entry_list_count_maybe + 1);
}

/* production_close_queue: 26:0x0810 (Ghidra FUN_10c8_0810, alias fn_26_0810), 210 bytes.
 * PUSH BP; MOV BP,SP (no locals, no register pushes). FAR CDECL, one argument at [BP+6]: nonzero when
 * the dialog was accepted (OK), 0 when it was cancelled. No result.
 * Accepted: the planet's old queue (DS:0x4c3c) is freed, the edited list DS:0xcd2 becomes the planet's
 * queue (freed first when it holds no entry), the planet record is stored with 7:0x0358 (-1 then the
 * planet id, buffer DS:0x4c08) and, unless DS:0x15fe is set, the screen is refreshed (9:0x431c, 9:0x07e0).
 * Cancelled: bit 7 of word DS:0x4c22 (the "contribute only leftover resources to research" check box of
 * the planet) is set from bit 5 of DS:0x0790 and the edited list is freed. Either way DS:0xcd2 is cleared and,
 * when bit 3 of DS:0x0791 is set and the current player is player 0, 31:0x61b0 is called after bit 2
 * of DS:0x53f1 is set. */
DECOMP(26, 0x0810, production_close_queue, FAR_CDECL)
void production_close_queue(int16_t accepted)
{
  FRAME_ENTER(0);
  if (accepted != 0) {
    free_typed_array(g_list_data_ptr_maybe);
    if (g_production_list != 0 && rd8(FP_SEG(g_production_list), (uint16_t)(FP_OFF(g_production_list) + 3)) == 0) {
      free_typed_array(g_production_list);
      g_production_list = 0;
    }
    g_list_data_ptr_maybe = g_production_list;
    g_production_list = 0;
    const uint16_t a1[2] = {0xffff, CURSOR_PLANET};
    (void)CALL_ORIG(7, 0x0358, FAR_CDECL, 2, a1[0], a1[1]);
    (void)CALL_ORIG(7, 0x0358, FAR_CDECL, 2, drd16(CURSOR_PLANET), CURSOR_PLANET);
    if (g_busy_flag_maybe == 0) {
      (void)CALL_ORIG(9, 0x431c, FAR_CDECL, 5, 0, 0, 0, 0, 0);
      (void)CALL_ORIG(9, 0x07e0, FAR_CDECL, 2, 0, 0x0040);
    }
  } else {
    /* DS:0x4c22 bit 7 = DS:0x0790 bit 5 (the word at 0x4c20 is rewritten with its own value) */
    uint16_t bit = (uint16_t)(((drd8(0x0790) & 0x20) >> 5) << 7);
    dwr16(0x4c22, (uint16_t)(drd16(0x4c22) ^ ((bit ^ drd16(0x4c22)) & 0x80)));
    free_typed_array(g_production_list);
  }
  g_production_list = 0;
  if ((g_ui_flags_b_maybe & 8) != 0 && g_current_player == 0) {
    g_frame_flags_b_maybe = (uint8_t)(g_frame_flags_b_maybe | 4);
    (void)CALL_ORIG(31, 0x61b0, FAR_CDECL, 0);
  }
  FRAME_LEAVE();
}

/* production_open_queue: 26:0x0100 (Ghidra FUN_10c8_0100, alias fn_26_0100), 1808 bytes.
 * ENTER 0x14,0; PUSH DI; PUSH SI. FAR CDECL, one argument at [BP+6]: the near address of a 0x100-byte
 * buffer for the list of available items (0 = reuse the buffer of DS:0x5a62). No result.
 * Opens the production queue of the selected planet (the record copy DS:0x4c08):
 *  1. bit 5 of DS:0x0790 is set from bit 7 of word DS:0x4c22 (the research check box state);
 *  2. DS:0xcd2 gets a new typed array (12:0x0568, element size 4, heap 0) holding a copy of the
 *     planet's queue DS:0x4c3c (its header byte +3 is the entry count; 2 entries of room when the
 *     planet has no queue);
 *  3. the buffer is cleared and DS:0x5a62 / DS:0x6686 (entry count) describe it; then the available
 *     items are appended: (a) when the planet has a starbase (bit 1 of flags DS:0x4c0d) whose hull
 *     (2:0x507c) has a ship capacity (+0x34), each of the 16 default designs DS:0x417a (stride 0x8d,
 *     flags byte +0x7c without bits 1 and 7) whose size (+0x28) fits, as class 2 items 0 to 15 with
 *     no limit; (b) each of the 10 designs of the player's starbase array (DS:0x10c + 4 * player),
 *     not hidden (+0x7c bits 1 and 7) and not the planet's current starbase design, as class 2 items
 *     0x10 + n, quantity 1; (c) class 1 item 0xd, quantity 1, when 2:0x50f8 accepts the item (0x8000,
 *     0xe); (d) class 1 items 0xe to 0x11 when 9:0x4f76 gives a positive mass driver level;
 *     (e) class 1 items 7, 8 and 9 with the quantity the planet may still add (9:0x4bec, 9:0x49ea,
 *     9:0x4d0e minus the 12-bit counts held in the planet copy); (f) class 1 item 0xb with no limit;
 *     (g) class 1 item 0x1b, quantity 1, when the planet copy's 5-bit field at DS:0x4c20 bits 12 to 16
 *     is full and the player's race value 14 (28:0x21ee) is not 8; (h) class 1 item 0xc with the count
 *     9:0x51bc; (i) class 1 items 0 to 6 with no limit, except items 0 to 2 for race value 8 and
 *     items 4 and 5 for race value 3;
 *  4. every entry of the planet's queue copy is matched with the available item of the same class
 *     and item index: its quantity is cut down to what is available and taken off the available
 *     quantity (unless that is unlimited); an entry with no match becomes an empty item 0xb of class 1;
 *  5. 31:0x61b0 is called when bit 3 of DS:0x0791 is set and the current player is player 0.
 * The request record for 2:0x50f8 lives at BP-0xe (kind word 0x8000, item byte 0xe, the table pointer
 * is written back by the callee at BP-0xa). */
DECOMP(26, 0x0100, production_open_queue, FAR_CDECL)
void production_open_queue(uint16_t items)
{
  FRAME_ENTER_REGS(0x14, 2);
  /* bit 5 of DS:0x0790 := bit 7 of the word DS:0x4c22 (a word xor: the high byte is rewritten as read) */
  {
    uint16_t w = drd16(0x0790);
    uint16_t bit = (uint16_t)(((drd16(0x4c22) >> 7) & 1) << 5);
    dwr16(0x0790, (uint16_t)(w ^ ((bit ^ (uint8_t)w) & 0x20)));
  }
  if (items == 0) items = g_entry_list_ptr_maybe;
  uint32_t queue = g_list_data_ptr_maybe;
  uint16_t room = (queue != 0) ? rd8(FP_SEG(queue), (uint16_t)(FP_OFF(queue) + 3)) : 2;
  {
    const uint16_t a[3] = {4, room, 0};
    g_production_list = (uint32_t)far_call(12, 0x0568, a, 3);
  }
  uint8_t count = 0;
  if (queue != 0) {
    uint16_t dst_seg = FP_SEG(g_production_list), dst = (uint16_t)(FP_OFF(g_production_list) + 4);
    uint16_t src_seg = FP_SEG(queue), src = (uint16_t)(FP_OFF(queue) + 4);
    for (uint16_t k = 0; k < (uint16_t)(room * 2); k++) {
      wr16(dst_seg, (uint16_t)(dst + 2 * k), rd16(src_seg, (uint16_t)(src + 2 * k)));
    }
    count = (uint8_t)room;
  }
  wr8(FP_SEG(g_production_list), (uint16_t)(FP_OFF(g_production_list) + 3), count);
  g_entry_list_count_maybe = 0;
  g_entry_list_ptr_maybe = items;
  for (uint16_t k = 0; k < 0x40; k++) dwr32((uint16_t)(items + 4 * k), 0);

  /* (a) the default designs a starbase of this planet can build */
  if ((drd8(0x4c0d) & 2) != 0) {
    uint32_t sb_designs = drd32((uint16_t)(g_current_player * 4 + 0x10c));
    uint16_t a1[1] = {rd16(FP_SEG(sb_designs), (uint16_t)(FP_OFF(sb_designs) + (uint16_t)((drd8(0x4c34) & 0xf) * 0x8d)))};
    uint32_t hull = far_call(2, 0x507c, a1, 1);
    if (rd16(FP_SEG(hull), (uint16_t)(FP_OFF(hull) + 0x34)) != 0) {
      uint16_t design_no = 0;
      for (uint16_t rec = 0x41f5; rec < 0x4ac5; rec = (uint16_t)(rec + 0x8d), design_no++) {
        if ((drd8((uint16_t)(rec + 1)) & 0x82) != 0) continue;
        uint32_t own = drd32((uint16_t)(g_current_player * 4 + 0x10c));
        uint16_t a2[1] = {rd16(FP_SEG(own), (uint16_t)(FP_OFF(own) + (uint16_t)((drd8(0x4c34) & 0xf) * 0x8d)))};
        uint32_t hull2 = far_call(2, 0x507c, a2, 1);
        if (rd16(FP_SEG(hull2), (uint16_t)(FP_OFF(hull2) + 0x34)) < drd16((uint16_t)(rec - 0x53))) continue;
        pq_append(items, PQ_QTY_ANY, design_no, PQ_CLASS_DESIGN);
      }
    }
  }
  /* (b) the player's own starbase designs */
  for (uint16_t rec = 0, n = 0; (int16_t)rec < 0x582; rec = (uint16_t)(rec + 0x8d), n++) {
    uint32_t arr = drd32((uint16_t)(g_current_player * 4 + 0x10c));
    uint16_t flags = rd16(FP_SEG(arr), (uint16_t)(FP_OFF(arr) + rec + 0x7b));
    if ((flags & 0x0200) != 0 || (flags & 0x8000) != 0) continue;
    if ((drd8(0x4c34) & 0xf) == n && (drd8(0x4c0d) & 2) != 0) continue;
    pq_append(items, 1, (uint16_t)(n + 0x10), PQ_CLASS_DESIGN);
  }
  /* (c) the item 0xe of kind 0x8000 */
  L16(-0xe) = 0x8000;
  L16(-0xc) = (uint16_t)((L16(-0xc) & 0xff00) | 0x0e);
  {
    const uint16_t a[1] = {bpoff(-0xe)};
    if ((uint16_t)far_call(2, 0x50f8, a, 1) == 1) pq_append(items, 1, 0x0d, PQ_CLASS_ITEM);
  }
  /* (d) four items per mass driver level */
  if ((int16_t)CALL_ORIG(9, 0x4f76, FAR_CDECL, 3, CURSOR_PLANET, MEM_SEL_DGROUP, 0) > 0) {
    for (uint16_t n = 0; (int16_t)n < 4; n++) pq_append(items, PQ_QTY_ANY, (uint16_t)(n + 0x0e), PQ_CLASS_ITEM);
  }
  /* (e) items 7, 8 and 9: what the planet may still add */
  {
    int16_t room7 = (int16_t)((uint16_t)CALL_ORIG(9, 0x4bec, FAR_CDECL, 3, CURSOR_PLANET, MEM_SEL_DGROUP, g_current_player) -
                              (uint16_t)((drd16(0x4c1e) >> 4) & 0x0fff));
    if (room7 > 0) pq_append(items, (uint16_t)((uint16_t)room7 < 0x3fc ? (uint16_t)room7 : 0x3fc), 7, PQ_CLASS_ITEM);
    uint32_t d1c = drd32(0x4c1c);
    int16_t room8 = (int16_t)((uint16_t)CALL_ORIG(9, 0x49ea, FAR_CDECL, 3, CURSOR_PLANET, MEM_SEL_DGROUP, g_current_player) -
                              (uint16_t)((d1c >> 8) & 0x0fff));
    if (room8 > 0) pq_append(items, (uint16_t)((uint16_t)room8 < 0x3fc ? (uint16_t)room8 : 0x3fc), 8, PQ_CLASS_ITEM);
    int16_t room9 = (int16_t)((uint16_t)CALL_ORIG(9, 0x4d0e, FAR_CDECL, 3, CURSOR_PLANET, MEM_SEL_DGROUP, g_current_player) -
                              (uint16_t)(drd16(0x4c20) & 0x0fff));
    if (room9 > 0) pq_append(items, (uint16_t)room9, 9, PQ_CLASS_ITEM);
  }
  /* (f) item 0xb is always offered */
  pq_append(items, PQ_QTY_ANY, 0x0b, PQ_CLASS_ITEM);
  /* (g) item 0x1b */
  if ((drd32(0x4c20) & 0x1f000) == 0x1f000) {
    uint16_t row = (uint16_t)(g_current_player * 0xc0 + 0x5a64);
    if ((uint16_t)CALL_ORIG(28, 0x21ee, FAR_CDECL, 2, row, 0x0e) != 8) pq_append(items, 1, 0x1b, PQ_CLASS_ITEM);
  }
  /* (h) item 0xc */
  {
    uint16_t cnt = (uint16_t)CALL_ORIG(9, 0x51bc, FAR_CDECL, 2, CURSOR_PLANET, MEM_SEL_DGROUP);
    if ((int16_t)cnt > 0) pq_append(items, cnt, 0x0c, PQ_CLASS_ITEM);
  }
  /* (i) items 0 to 6, some withheld by the race value */
  for (uint16_t n = 0; (int16_t)n < 7; n++) {
    uint16_t row = (uint16_t)(g_current_player * 0xc0 + 0x5a64);
    uint16_t race = (uint16_t)CALL_ORIG(28, 0x21ee, FAR_CDECL, 2, row, 0x0e);
    if (race == 8 && (n == 0 || n == 1 || n == 2)) continue;
    race = (uint16_t)CALL_ORIG(28, 0x21ee, FAR_CDECL, 2, row, 0x0e);
    if (race == 3 && (n == 4 || n == 5)) continue;
    pq_append(items, PQ_QTY_ANY, n, PQ_CLASS_ITEM);
  }

  /* 4. match the queue copy against the available items */
  {
    uint16_t list_seg = FP_SEG(g_production_list);
    uint16_t ent = (uint16_t)(FP_OFF(g_production_list) + 4);
    for (int16_t k = 0; (int16_t)rd8(FP_SEG(g_production_list), (uint16_t)(FP_OFF(g_production_list) + 3)) > k;
         k++, ent = (uint16_t)(ent + 4)) {
      struct ProdItem *e = FAR_REC(struct ProdItem, list_seg, ent);
      int16_t j = 0;
      for (; j < (int16_t)g_entry_list_count_maybe; j++) {
        const struct ProdItem *a = pq_at(g_entry_list_ptr_maybe, (uint16_t)j);
        uint32_t same = (((uint32_t)e->w1 << 16) | e->w0) ^ (((uint32_t)a->w1 << 16) | a->w0);
        if (((e->w1 ^ a->w1) & PQ_CLASS_MASK) == 0 && (same & PRODITEM_ITEM_DWORD_MASK) == 0) break;
      }
      if (j < (int16_t)g_entry_list_count_maybe) {
        struct ProdItem *a = pq_at(g_entry_list_ptr_maybe, (uint16_t)j);
        if ((a->w0 & PQ_QTY_MASK) < (e->w0 & PQ_QTY_MASK)) pq_set_qty(e, a->w0);
        a = pq_at(g_entry_list_ptr_maybe, (uint16_t)j);
        if ((a->w0 & PQ_QTY_MASK) != PQ_QTY_ANY) {
          if ((a->w0 & PQ_QTY_MASK) < (e->w0 & PQ_QTY_MASK)) {
            a->w0 = (uint16_t)(a->w0 & PQ_ITEM_W0_MASK);
          } else {
            pq_set_qty(a, (uint16_t)(a->w0 - e->w0));
          }
        }
      } else {
        pq_set_class(e, PQ_CLASS_ITEM);
        pq_set_item(e, 0x0b);
        pq_set_qty(e, 0);
      }
    }
  }
  if ((g_ui_flags_b_maybe & 8) != 0 && g_current_player == 0) (void)CALL_ORIG(31, 0x61b0, FAR_CDECL, 0);
  FRAME_LEAVE();
}

/* production_dialog: 26:0x0000 (Ghidra FUN_10c8_0000, alias fn_26_0000), 256 bytes.
 * ENTER 0x11a,0; PUSH SI. FAR CDECL, one argument at [BP+6]: nonzero skips the dialog and accepts.
 * Returns 0 when a throw back to its catch point happened, else 1 (AX only).
 * Registers a catch buffer (BP-0x1a, 18 bytes) in DS:0x006e with 34:0x0800 (a setjmp) and keeps the
 * previous DS:0x006e at BP-8. Landing there again (the error paths of the file functions throw back)
 * frees the edited list DS:0xcd2, ends the dialog window DS:0xcd6 and shows message 0x20/0x10
 * (8:0x8d94) and returns 0. Otherwise: unless skipped, the queue is opened with 26:0x0100 (buffer
 * at BP-0x11a, 256 bytes), the dialog template 0x5d runs with the procedure 26:0x08e2 (through
 * MakeProcInstance, instance DS:0x54f4, parent window DS:0x2806, DS:0xcd8 = 1 while it runs) and
 * its result (1 = OK) is handed to 26:0x0810 to commit or discard the queue; when that result is
 * non-zero and the selected object kind DS:0x4b74 is 1, 9:0x07e0 refreshes the screen. The old
 * catch point is restored on every path. */
DECOMP(26, 0x0000, production_dialog, FAR_CDECL)
uint16_t production_dialog(int16_t skip_dialog)
{
  FRAME_ENTER_REGS(0x11a, 1);
  uint16_t saved_catch = g_catch_buffer;
  g_catch_buffer = bpoff(-0x1a);
  L16(-8) = saved_catch;
  if ((uint16_t)CALL_ORIG(34, 0x0800, FAR_CDECL, 1, bpoff(-0x1a)) != 0) {
    if (g_production_list != 0) free_typed_array(g_production_list);
    g_production_list = 0;
    if (production_dialog_hwnd != 0) (void)EndDialog(production_dialog_hwnd, 0);
    production_dialog_hwnd = 0;
    production_dialog_running = 0;
    (void)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x20, 0x10);
    g_catch_buffer = L16(-8);
    FRAME_LEAVE();
    return 0;
  }
  uint16_t result;
  if (skip_dialog != 0) {
    result = 1;
  } else {
    const uint16_t a[1] = {bpoff(-0x11a)};
    (void)call26(0x0100, a, 1);
    production_dialog_running = 1;
    uint32_t proc = MakeProcInstance(MK_FP(SEG_SEL(26), 0x08e2), g_app_instance);
    L16(-4) = (uint16_t)proc;
    L16(-2) = (uint16_t)(proc >> 16);
    L16(-6) = (uint16_t)API_SP(12, DialogBox(g_app_instance, 0x0000005d, g_main_window, proc));
    FreeProcInstance(L32(-4));
    production_dialog_hwnd = 0;
    production_dialog_running = 0;
    result = L16(-6);
  }
  {
    const uint16_t a[1] = {result};
    (void)call26(0x0810, a, 1);
  }
  if (result != 0) {
    L16(-6) = result;
    if (g_selection_kind_maybe == 1) (void)CALL_ORIG(9, 0x07e0, FAR_CDECL, 2, 0, 8);
  }
  g_catch_buffer = L16(-8);
  FRAME_LEAVE();
  return 1;
}

/* strcpy of the NUL-terminated string at sseg:soff to dseg:doff, NUL included (REPNE SCASB for the
 * length, then REP MOVSW and REP MOVSB). Offsets wrap in 16 bits. */
static void copy_string(uint16_t dseg, uint16_t doff, uint16_t sseg, uint16_t soff) {
  uint16_t n = 0;
  while (rd8(sseg, (uint16_t)(soff + n)) != 0) n++;
  for (uint16_t k = 0; k <= n; k++) wr8(dseg, (uint16_t)(doff + k), rd8(sseg, (uint16_t)(soff + k)));
}
/* strcat: appends the string at sseg:soff to the one at dseg:doff. */
static void append_string(uint16_t dseg, uint16_t doff, uint16_t sseg, uint16_t soff) {
  uint16_t end = doff;
  while (rd8(dseg, end) != 0) end++;
  copy_string(dseg, end, sseg, soff);
}

/* The text buffer the item names are built in (DS:0x5866, shared with the other dialogs of the game). */
#define NAME_BUF 0x5866

/* production_item_name: 26:0x2058 (Ghidra FUN_10c8_2058, alias fn_26_2058), 396 bytes.
 * ENTER 0xa,0; PUSH DI; PUSH SI. FAR CDECL, a far pointer to a ProdItem at [BP+6]. Returns AX = 0x5866,
 * the near address of the name it builds in DS:0x5866. The item index is bits 10 to 16 of the entry,
 * the class bits 1 to 3 of its second word.
 * Class 2 (ship designs): index 0 to 15 is a default design (DS:0x417a + 0x8d * n, name at +8; an
 * empty name when bit 1 of its flags byte +0x7c is set); index 16 up is the player's starbase design
 * n - 16 (array DS:0x10c + 4 * player; empty name when bit 1 of +0x7c is set) and, when the planet has
 * a starbase (bit 1 of DS:0x4c0d), " downgrade" (DS:0xcec) or " upgrade" (DS:0xcf9) is appended
 * according to the sign of the planet's current starbase design hull word minus that design's.
 * Other classes: index 0x12 to 0x1a name the records of 2:0x50ca (name at +8); the rest are strings of
 * the table 8:0x8dc0 (index + 0x7e, and 0x50c for index 0x1b). */
DECOMP(26, 0x2058, production_item_name, FAR_CDECL)
uint16_t production_item_name(uint32_t item_ptr)
{
  FRAME_ENTER_REGS(0xa, 2);
  const struct ProdItem *it = FAR_REC(struct ProdItem, FP_SEG(item_ptr), FP_OFF(item_ptr));
  uint16_t w1 = it->w1;
  uint16_t item = prod_item_index(it);
  if ((w1 & PQ_CLASS_MASK) == (PQ_CLASS_DESIGN << 1)) {
    if (item < 0x10) {
      uint16_t rec = (uint16_t)(item * 0x8d);
      if ((drd8((uint16_t)(rec + 0x41f6)) & 2) != 0) {
        dwr8(NAME_BUF, 0);
      } else {
        copy_string(MEM_SEL_DGROUP, NAME_BUF, MEM_SEL_DGROUP, (uint16_t)(rec + 0x4182));
      }
    } else {
      uint16_t rec = (uint16_t)((uint16_t)(item - 0x10) * 0x8d);
      uint32_t arr = drd32((uint16_t)(g_current_player * 4 + 0x10c));
      uint16_t aseg = FP_SEG(arr), aoff = FP_OFF(arr);
      if ((rd8(aseg, (uint16_t)(aoff + rec + 0x7c)) & 2) != 0) {
        dwr8(NAME_BUF, 0);
      } else {
        copy_string(MEM_SEL_DGROUP, NAME_BUF, aseg, (uint16_t)(aoff + rec + 8));
        if ((drd8(0x4c0d) & 2) != 0) {
          uint32_t own = drd32((uint16_t)(g_current_player * 4 + 0x10c));
          uint16_t cur_hull = rd16(FP_SEG(own), (uint16_t)(FP_OFF(own) + (uint16_t)((drd8(0x4c34) & 0xf) * 0x8d)));
          int16_t diff = (int16_t)(cur_hull - rd16(FP_SEG(own), (uint16_t)(rec + FP_OFF(own))));
          if (diff > 0) append_string(MEM_SEL_DGROUP, NAME_BUF, MEM_SEL_DGROUP, 0x0cec); /* " downgrade" */
          else if (diff < 0) append_string(MEM_SEL_DGROUP, NAME_BUF, MEM_SEL_DGROUP, 0x0cf9); /* " upgrade" */
        }
      }
    }
  } else if (item >= 0x12 && item <= 0x1a) {
    const uint16_t a[1] = {(uint16_t)(item - 0x12)};
    uint32_t r = far_call(2, 0x50ca, a, 1);
    copy_string(MEM_SEL_DGROUP, NAME_BUF, FP_SEG(r), (uint16_t)(FP_OFF(r) + 8));
  } else {
    (void)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, (item == 0x1b) ? 0x050c : (uint16_t)(item + 0x7e), NAME_BUF);
  }
  FRAME_LEAVE();
  return NAME_BUF;
}

/* production_fill_item_list: 26:0x1f42 (Ghidra FUN_10c8_1f42, alias fn_26_1f42), 278 bytes.
 * ENTER 0x5a,0; PUSH DI; PUSH SI. FAR CDECL, the list box window at [BP+6]. No result.
 * Empties the list box (message 0x405) and adds a line (message 0x401) for every entry of the list of
 * available items (DS:0x5a62, count DS:0x6686) whose quantity is not 0. A line is six characters of
 * marker and blanks followed by the name from 26:0x2058: the marker is '*' for the default designs
 * (class 2, index below 16), '#' for the starbase designs, 'I' for the automatic items (class 1, index
 * 0 to 6, with the text DS:0xcde appended to the name) and ' ' for the other items. */
DECOMP(26, 0x1f42, production_fill_item_list, FAR_CDECL)
void production_fill_item_list(uint16_t list_hwnd)
{
  FRAME_ENTER_REGS(0x5a, 2);
  for (uint16_t k = 0; k < 3; k++) wr16(rt_ss, bpoff((int16_t)(-0x56 + 2 * k)), 0x2020);
  (void)SendMessage(list_hwnd, 0x0405, 0, 0);
  int16_t counter = 0;
  if ((int16_t)g_entry_list_count_maybe > 0) {
    uint16_t at = 0;
    do {
      const struct ProdItem *e = pq_at(g_entry_list_ptr_maybe, (uint16_t)(at >> 2));
      if ((e->w0 & PQ_QTY_MASK) != 0) {
        const uint16_t a[2] = {(uint16_t)(g_entry_list_ptr_maybe + at), MEM_SEL_DGROUP};
        uint16_t name = (uint16_t)far_call(26, 0x2058, a, 2);
        copy_string(rt_ss, bpoff(-0x50), MEM_SEL_DGROUP, name);
        e = pq_at(g_entry_list_ptr_maybe, (uint16_t)(at >> 2));
        uint16_t w0 = e->w0, w1 = e->w1;
        uint8_t marker;
        if ((w1 & PQ_CLASS_MASK) == (PQ_CLASS_DESIGN << 1)) {
          marker = ((w1 & 1) == 0 && (uint16_t)(w0 & PQ_ITEM_W0_MASK) < 0x4000) ? '*' : '#';
        } else if ((w1 & 1) == 0 && (uint16_t)(w0 & PQ_ITEM_W0_MASK) < 0x1c00) {
          marker = 'I';
          append_string(rt_ss, bpoff(-0x50), MEM_SEL_DGROUP, 0x0cde); /* "Auto Build" */
        } else {
          marker = ' ';
        }
        wr8(rt_ss, bpoff(-0x56), marker);
        (void)SendMessage(list_hwnd, 0x0401, 0, bpfp(-0x56));
      }
      at = (uint16_t)(at + 4);
      counter++;
    } while (counter < (int16_t)g_entry_list_count_maybe);
  }
  FRAME_LEAVE();
}

/* production_zip_enable_buttons: 26:0x33e4 (Ghidra FUN_10c8_33e4, alias fn_26_33e4), 67 bytes.
 * PUSH BP; MOV BP,SP; PUSH SI. FAR CDECL (dialog window [BP+6], preset number [BP+8]). Enables the
 * buttons 0x817 and 0x41b of the ZIP production dialog when the preset is in use (DS:0x2583 + 0x28 * n)
 * and is not preset 0, else disables them. AX = the result of the last EnableWindow. */
DECOMP(26, 0x33e4, zip_prod_enable_buttons, FAR_CDECL)
uint16_t zip_prod_enable_buttons(uint16_t hdlg, int16_t preset)
{
  FRAME_ENTER_REGS(0, 1);
  const struct ZipPreset *z = FAR_REC(struct ZipPreset, MEM_SEL_DGROUP, (uint16_t)(DS_ZIP_PRESETS + (uint16_t)(preset * 0x28)));
  uint16_t on = (z->in_use != 0 && preset > 0) ? 1 : 0;
  (void)EnableWindow(GetDlgItem(hdlg, 0x817), on);
  uint16_t ret = EnableWindow(GetDlgItem(hdlg, 0x41b), on);
  FRAME_LEAVE();
  return ret;
}

/* production_init_lists: 26:0x1a40 (Ghidra FUN_10c8_1a40, alias fn_26_1a40), 282 bytes.
 * ENTER 0x5e,0; PUSH DI; PUSH SI. FAR CDECL, the dialog window at [BP+6]. No result.
 * Fills the controls of the production queue dialog: the title (the string 0x32d of 8:0x8df4 with the
 * planet name from 7:0x1cce(planet id DS:0x4c08), formatted into BP-0x5c and set with SetWindowText),
 * the list of available items (control 0x416, 26:0x1f42; the extra word 0xffff pushed with its argument is
 * ignored), the queue list (control 0x417, 9:0x431c with the edited list DS:0xcd2), and the selection:
 * item 0 of the available items, and in the queue list the entry after the last one that is not an
 * automatic item (class 1, index below 7), or the first when there is none. Last the "contribute
 * only leftover resources to research" check box (control 0x8b) gets bit 7 of DS:0x4c22.
 * 9:0x431c gets five words (hwnd, the edited list as two words, a dword 0) and the stack is balanced. */
DECOMP(26, 0x1a40, production_init_lists, FAR_CDECL)
void production_init_lists(uint16_t hdlg)
{
  FRAME_ENTER_REGS(0x5e, 2);
  uint16_t name = (uint16_t)CALL_ORIG(7, 0x1cce, FAR_CDECL, 1, drd16(CURSOR_PLANET));
  {
    /* _wsprintf(BP-0x5c, string 0x32d, far pointer to the name): the name pointer was pushed before the call of 8:0x8df4 */
    const uint16_t sp0 = rt_sp;
    pushw(MEM_SEL_DGROUP);
    pushw(name);
    uint16_t fmt = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x032d);
    (void)_wsprintf(bpfp(-0x5c), dsfp(fmt), MK_FP(rt_ss, (uint16_t)(sp0 - 4)));
    rt_sp = sp0;
  }
  SetWindowText(hdlg, bpfp(-0x5c));
  uint16_t sel = 0xffff;
  {
    const uint16_t a[2] = {GetDlgItem(hdlg, 0x416), sel};
    (void)call26(0x1f42, a, 2);
  }
  (void)SendMessage(GetDlgItem(hdlg, 0x416), 0x0407, 0, 0);
  {
    uint32_t list = g_production_list;
    uint16_t at = (uint16_t)(FP_OFF(list) + 4);
    uint8_t count = rd8(FP_SEG(g_production_list), (uint16_t)(FP_OFF(g_production_list) + 3));
    if (count != 0) {
      uint16_t i = 0;
      do {
        const struct ProdItem *e = FAR_REC(struct ProdItem, FP_SEG(list), at);
        if ((e->w1 & PQ_CLASS_MASK) != (PQ_CLASS_ITEM << 1) ||
            ((((uint32_t)e->w1 << 16) | e->w0) & PRODITEM_ITEM_DWORD_MASK) >= 0x1c00)
          sel = i;
        at = (uint16_t)(at + 4);
        i++;
      } while ((int16_t)count > (int16_t)i);
    }
  }
  (void)CALL_ORIG(9, 0x431c, FAR_CDECL, 5, GetDlgItem(hdlg, 0x417), (uint16_t)g_production_list,
                  (uint16_t)(g_production_list >> 16), 0, 0);
  (void)SendMessage(GetDlgItem(hdlg, 0x417), 0x0407, (uint16_t)(sel + 1), 0);
  (void)SendMessage(GetDlgItem(hdlg, 0x8b), 0x0401, (uint16_t)((drd16(0x4c22) >> 7) & 1), 0);
  FRAME_LEAVE();
}

/* zip_prod_fill_preset_list: 26:0x3428 (Ghidra FUN_10c8_3428, alias fn_26_3428), 315 bytes.
 * ENTER 0x48,0; PUSH DI; PUSH SI. FAR CDECL (dialog window [BP+6], near pointer to a ZipPreset
 * [BP+8]). No result. Redraws the item list (control 0x417) of the ZIP production dialog for a preset:
 * the client rectangle of the dialog (its top and bottom replaced by DS:0xd08 and DS:0xd08 + DS:0x267a)
 * is invalidated, the list is emptied (message 0x405) and, when the preset is in use (+0xd) and has
 * items (+0xf), one line is added (message 0x401) per item word at +0x10: the string 0x7e + (item &
 * 0x3f), formatted with the quantity item >> 6 through the format string 0x4b4 unless the item's high
 * bits give 1 (item & 0xffc0 == 0x40) or the item is 3, which show the plain string. A preset that is
 * not in use or empty shows the string 0x4b3. */
DECOMP(26, 0x3428, zip_prod_fill_preset_list, FAR_CDECL)
void zip_prod_fill_preset_list(uint16_t hdlg, uint16_t preset)
{
  FRAME_ENTER_REGS(0x48, 2);
  uint16_t hlist = GetDlgItem(hdlg, 0x417);
  GetClientRect(hdlg, bpfp(-0xe));
  L16(-0xc) = drd16(0x0d08);
  L16(-8) = (uint16_t)(L16(-0xc) + drd16(0x267a));
  InvalidateRect(hdlg, bpfp(-0xe), 1);
  (void)SendMessage(hlist, 0x0405, 0, 0);
  const struct ZipPreset *z = FAR_REC(struct ZipPreset, MEM_SEL_DGROUP, preset);
  if (z->in_use != 0 && z->count != 0) {
    (void)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, 0x04b4, bpoff(-0x1e));
    uint16_t at = (uint16_t)(preset + 0x10);
    for (uint16_t i = 0; (int16_t)(FAR_REC(struct ZipPreset, MEM_SEL_DGROUP, preset)->count) > (int16_t)i; i++, at = (uint16_t)(at + 2)) {
      (void)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, (uint16_t)((drd8(at) & 0x3f) + 0x7e), bpoff(-0x46));
      uint16_t item = drd16(at);
      if ((item & 0xffc0) == 0x40 || (item & 0x3f) == 3) {
        copy_string(MEM_SEL_DGROUP, NAME_BUF, rt_ss, bpoff(-0x46));
      } else {
        const uint16_t sp0 = rt_sp;
        pushw((uint16_t)(item >> 6));
        pushw(rt_ss);
        pushw(bpoff(-0x46));
        (void)_wsprintf(dsfp(NAME_BUF), bpfp(-0x1e), MK_FP(rt_ss, rt_sp));
        rt_sp = sp0;
      }
      (void)SendMessage(hlist, 0x0401, 0, dsfp(NAME_BUF));
    }
  } else {
    const uint16_t sp0 = rt_sp;
    pushw(hlist);
    pushw(0x0401);
    pushw(0);
    uint16_t str = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x04b3);
    rt_sp = sp0;
    (void)SendMessage(hlist, 0x0401, 0, dsfp(str));
  }
  FRAME_LEAVE();
}

/* production_simulate_queue: 26:0x29da (Ghidra FUN_10c8_29da, alias fn_26_29da), 844 bytes.
 * ENTER 0x6c,0; PUSH DI; PUSH SI. FAR CDECL, no result:
 *   [BP+6]  far pointer to the planet record (struct Planet) to simulate,
 *   [BP+0xa] far pointer to the production queue to simulate (0 = the planet's own queue, stored back
 *           into this argument slot),
 *   [BP+0xe] target: the index of the queue entry whose completion is wanted, or -1 / -2 (below 0:
 *           no entry, the answer is the planet's resources),
 *   [BP+0x10] near pointer to a word: the first turn in which something of the queue gets built,
 *   [BP+0x12] near pointer to a word: the turn in which the target entry is finished.
 * Works on copies on its stack: the planet record (BP-0x6c, 14 dwords), a copy of the queue (a typed
 * array, BP-0x38, freed at the end) and a pseudo entry at BP-0x18 that stands for the queue index -1.
 * Each simulated turn (BP-0xa, 1 to 99) takes the planet's mineral output (5:0x3a72 into BP-0x24,
 * minerals added to the copy and the three stocks saved at BP-0x34), its resources (9:0x4de8) less the
 * research share (the player's byte DS:0x5a9c + 0xc0 * owner percent, unless bit 7 of the copy's word
 * +0x1a: research only from leftovers), and builds the queue entries in order with 23:0x0756 (planet
 * copy, entry, pseudo entry or 0, saved stocks, "an entry before it was held back" flag BP-0xc,
 * status word BP-0x14, 0), which returns how many it built and a status; the count adds to the copy's
 * mine field (items 0 and 8: bits 8 to 19 of the dword +0x14) or factory field (items 1 and 7: bits 20
 * to 31). An auto-build alchemy entry (class 1, item 3) that is not the last one holds the later
 * entries back, and as the last one it becomes quantity 0x3fc. Status 5 or more ends the pass. The
 * target's answer: out1 = the first turn it built something (count > 0), out2 = the turn it completed
 * (status 0 or 1: this turn; 2: the turn before, only after it first built); -1/-1 for an alchemy
 * entry that blocks the target. For a negative target out1 = the resources (-1: plus the research
 * share). After 99 turns both answers are 100 when still 0. Between turns the planet copy is advanced
 * with 7:0x46fc. */
DECOMP(26, 0x29da, production_simulate_queue, FAR_CDECL)
void production_simulate_queue(uint32_t planet, uint32_t queue, int16_t target, uint16_t first_turn, uint16_t done_turn)
{
  FRAME_ENTER_REGS(0x6c, 2);
  const uint16_t pseg = FP_SEG(planet), poff = FP_OFF(planet);
  if (BP_LOCAL(uint32_t, 0xa) == 0) {
    BP_LOCAL(uint32_t, 0xa) = rd32(pseg, (uint16_t)(poff + 0x34));
  }
  queue = BP_LOCAL(uint32_t, 0xa);
  for (uint16_t k = 0; k < 14; k++) L32((int16_t)(-0x6c + 4 * k)) = rd32(pseg, (uint16_t)(poff + 4 * k));
  {
    const uint16_t a[3] = {4, rd8(FP_SEG(BP_LOCAL(uint32_t, 0xa)), (uint16_t)(FP_OFF(BP_LOCAL(uint32_t, 0xa)) + 2)), 0};
    L32(-0x38) = (uint32_t)far_call(12, 0x0568, a, 3);
  }
  {
    uint16_t qseg = FP_SEG(BP_LOCAL(uint32_t, 0xa)), qoff = FP_OFF(BP_LOCAL(uint32_t, 0xa));
    uint16_t bytes = (uint16_t)(rd8(qseg, (uint16_t)(qoff + 3)) << 2);
    uint16_t lseg = FP_SEG(L32(-0x38)), loff = FP_OFF(L32(-0x38));
    for (uint16_t k = 0; k < bytes; k++) wr8(lseg, (uint16_t)(loff + 4 + k), rd8(qseg, (uint16_t)(qoff + 4 + k)));
    wr8(lseg, (uint16_t)(loff + 3), rd8(qseg, (uint16_t)(qoff + 3)));
    L16(-0x12) = rd8(lseg, (uint16_t)(loff + 3));
  }
  L16(-0x18) = (uint16_t)(L16(-0x18) & PQ_ITEM_W0_MASK);
  dwr16(done_turn, 0);
  dwr16(first_turn, 0);
  L16(-0xa) = 1;
  for (;;) {
    {
      const uint16_t a[6] = {bpoff(-0x6c), rt_ss, bpoff(-0x24), 0xffff, 0xffff, 1};
      (void)far_call(5, 0x3a72, a, 6);
    }
    for (uint16_t k = 0; k < 3; k++) L32((int16_t)(-0x34 + 4 * k)) = L32((int16_t)(-0x50 + 4 * k));
    int32_t resources = (int16_t)(uint16_t)CALL_ORIG(9, 0x4de8, FAR_CDECL, 3, bpoff(-0x6c), rt_ss, rd16(pseg, (uint16_t)(poff + 2)));
    L32(-0x28) = (uint32_t)resources;
    if ((L16(-0x52) & 0x80) == 0) {
      int32_t pct = (int8_t)drd8((uint16_t)(rd16(pseg, (uint16_t)(poff + 2)) * 0xc0 + 0x5a9c));
      int32_t research = (int32_t)((uint32_t)pct * (uint32_t)resources) / 100;
      L32(-8) = (uint32_t)research;
      L32(-0x28) = (uint32_t)((int32_t)L32(-0x28) - research);
    } else {
      L32(-8) = 0;
    }
    L16(-0xc) = 0;
    int16_t index = -1;
    uint16_t slot = 0xfffc;
    if ((int16_t)L16(-0x12) > index) {
      do {
        uint16_t eseg, eoff;
        if (slot == 0xfffc) {
          eseg = rt_ss;
          eoff = bpoff(-0x18);
        } else {
          eseg = FP_SEG(L32(-0x38));
          eoff = (uint16_t)(FP_OFF(L32(-0x38)) + slot + 4);
        }
        L16(-0xe) = eseg;
        struct ProdItem *e = FAR_REC(struct ProdItem, eseg, eoff);
        if ((e->w0 & PQ_QTY_MASK) == 0) goto next_entry;
        if (((((uint32_t)e->w1 << 16) | e->w0) & PRODITEM_ITEM_DWORD_MASK) == 0x0c00 && (e->w1 & PQ_CLASS_MASK) == (PQ_CLASS_ITEM << 1)) {
          if ((int16_t)(L16(-0x12) - 1) <= index) {
            e->w0 = (uint16_t)((e->w0 & 0xfffc) | 0x03fc); /* the last alchemy entry: quantity 0x3fc */
          } else if (target == index) {
            dwr16(done_turn, 0xffff);
            dwr16(first_turn, 0xffff);
            goto done;
          } else {
            L16(-0xc) = 1;
            goto next_entry;
          }
        }
        L16(-2) = (uint16_t)CALL_ORIG(23, 0x0756, FAR_CDECL, 9, bpoff(-0x6c), rt_ss, eoff, L16(-0xe),
                                      (index == -1) ? 0 : bpoff(-0x18), bpoff(-0x34), L16(-0xc), bpoff(-0x14), 0);
        if (target == index) {
          if ((int16_t)L16(-2) > 0 && drd16(first_turn) == 0) dwr16(first_turn, L16(-0xa));
          if (L16(-0x14) == 2) {
            if (drd16(first_turn) == 0) goto done;
            dwr16(done_turn, (uint16_t)(L16(-0xa) - 1));
            goto done;
          }
          if (L16(-0x14) == 0 || L16(-0x14) == 1) {
            dwr16(done_turn, L16(-0xa));
            goto done;
          }
        }
        L16(-0xc) = 0;
        e = FAR_REC(struct ProdItem, L16(-0xe), eoff);
        if ((e->w1 & PQ_CLASS_MASK) == (PQ_CLASS_ITEM << 1)) {
          uint16_t item = prod_item_index(e);
          if (item == 8 || item == 0) {
            /* mines: the 12-bit field at bits 8 to 19 of the dword +0x14 of the planet copy grows by the count built */
            uint32_t d = L32(-0x58);
            uint32_t sum = d + ((uint32_t)(int32_t)(int16_t)L16(-2) << 8);
            L32(-0x58) = d ^ ((sum ^ d) & 0x000fff00u);
          } else if (item == 1 || item == 7) {
            /* factories: the 12-bit field at bits 20 to 31 */
            uint16_t hi = L16(-0x56);
            uint16_t sum = (uint16_t)(hi + (uint16_t)(L16(-2) << 4));
            L16(-0x56) = (uint16_t)(sum ^ ((sum ^ hi) & 0xf));
          }
        }
        if ((int16_t)L16(-0x14) >= 5) break;
      next_entry:
        slot = (uint16_t)(slot + 4);
        index++;
      } while (index < (int16_t)L16(-0x12));
    }
    if (target < 0) {
      dwr16(first_turn, (uint16_t)L32(-0x28));
      if (target == -1) dwr16(first_turn, (uint16_t)(drd16(first_turn) + (uint16_t)L32(-8)));
      goto done;
    }
    for (uint16_t k = 0; k < 3; k++) L32((int16_t)(-0x50 + 4 * k)) = L32((int16_t)(-0x34 + 4 * k));
    {
      const uint16_t a[3] = {bpoff(-0x6c), rt_ss, 1};
      (void)CALL_ORIG(7, 0x46fc, FAR_CDECL, 3, a[0], a[1], a[2]);
    }
    L16(-0xa) = (uint16_t)(L16(-0xa) + 1);
    if ((int16_t)L16(-0xa) >= 100) {
      if (drd16(first_turn) == 0) dwr16(first_turn, 100);
      dwr16(done_turn, 100);
      goto done;
    }
  }
done:
  if (L32(-0x38) != 0) free_typed_array(L32(-0x38));
  FRAME_LEAVE();
  (void)queue;
}

/* One of the four cost words {iron, boron, germanium, resources} of an item, dword k of the result at DS:out. */
static inline void cost_out(uint16_t out, uint16_t k, uint32_t v) { dwr32((uint16_t)(out + 4 * k), v); }
/* 28:0x21ee(player record, attribute n) and 28:0x2230(player record, trait n): the player's attribute and trait flags. */
static inline uint16_t player_attr(uint16_t row, uint16_t n) { return (uint16_t)CALL_ORIG(28, 0x21ee, FAR_CDECL, 2, row, n); }
static inline uint16_t player_trait(uint16_t row, uint16_t n) { return (uint16_t)CALL_ORIG(28, 0x2230, FAR_CDECL, 2, row, n); }
#define PLAYER_ROW(p) ((uint16_t)((uint16_t)(p) * 0xc0 + DS_PLAYERS))

/* The saving a replaced part gives back (26:0x21e4): out -= min(value, out), both 16-bit unsigned. */
static inline uint16_t part_saving(uint16_t remaining, uint16_t value) { return (uint16_t)(remaining - (value < remaining ? value : remaining)); }

/* production_item_cost: 26:0x21e4 (Ghidra FUN_10c8_21e4, alias fn_26_21e4), 2038 bytes. The inventory lists
 * only the first 1132 bytes (up to the jump table cs:0x2650 for the items of class 1); the case bodies
 * 0x2688 to 0x29d9 belong to the same function and are written here too.
 * ENTER 0x56,0; PUSH DI; PUSH SI. FAR CDECL, no result: [BP+6] far pointer to the planet record, [BP+0xa]
 * far pointer to a ProdItem, [BP+0xe] near pointer to 4 dwords (iron, boron, germanium, resources), [BP+0x10]
 * the player, [BP+0x12] non-zero for the cost of one unit (0: multiplied by the quantity of the entry).
 * Class 2 (ship design; index 0 to 15 the player's ship designs DS:0xbe, from 16 the starbase designs
 * DS:0x10c, minus 16): a design hidden by bit 1 of its flags byte +0x7c costs 0 and returns at once. The
 * cost words come from 7:0x3b60. A starbase design built at a planet that has a starbase
 * (flags +5 bit 1) is paid as an upgrade: with a different hull the cost is the larger of half the cost and the
 * cost less half of the old design's; with the same hull the old hull's cost (10:0x7a92 of the 2:0x507c
 * hull record) is taken off and the slots (4 bytes from design +0x3a: part category word, part index
 * byte, count byte) are compared one by one with the old design's: the saving of a replaced part is
 * 70 percent less than its old cost, (different category: new cost - max(old * 7 / -10 + new, new * 3
 * / 10)), or with the same category 80 percent / 20 percent, or the cheaper of equal parts. Starbase
 * designs cost a fifth less for the player traits 3 or race value 8 and are rounded half up to a
 * half; plain designs are the words as read.
 * Class 1 items (the jump table, index 0 to 0x1b): 0 and 8 cost only the attribute 5 of the player in the resource
 * slot; 1 and 7 the trait 0x1f based mix; 2 and 9 the cost of the item 9 of kind 0x8000
 * (2:0x50f8, 3/5 for race value 4); 3 and 0xb 25 or 100 resources by trait 6; 4, 5 and 0xc 70 or 100 resources
 * by trait 1 (half for race value 3); 6 and 0x11 and 0xe to 0x10 fixed costs by race value; 0xd and 0x12
 * to 0x1b the part costs of 10:0x7a92 for the items of kind 0x8000 index 0xe and index - 0x12. */
DECOMP(26, 0x21e4, production_item_cost, FAR_CDECL)
void production_item_cost(uint32_t planet, uint32_t item, uint16_t out, int16_t player, int16_t one_unit)
{
  FRAME_ENTER_REGS(0x56, 2);
  const uint16_t pseg = FP_SEG(planet), poff = FP_OFF(planet);
  const uint16_t iseg = FP_SEG(item), ioff = FP_OFF(item);
  const uint16_t prow = PLAYER_ROW(player);
  const uint16_t race = player_attr(PLAYER_ROW(rd16(pseg, (uint16_t)(poff + 2))), 0x0e);
  uint16_t w0 = rd16(iseg, ioff), w1 = rd16(iseg, (uint16_t)(ioff + 2));
  const uint16_t idx = (uint16_t)(((w0 & PQ_ITEM_W0_MASK) >> 10) | ((w1 & 1) << 6));
  const uint32_t qty = (uint32_t)(w0 & PQ_QTY_MASK);
  if ((w1 & PQ_CLASS_MASK) == (PQ_CLASS_DESIGN << 1)) {
    bool starbase_design = idx >= 0x10;
    uint32_t table = starbase_design ? drd32((uint16_t)(player * 4 + 0x10c)) : drd32((uint16_t)(player * 4 + 0xbe));
    uint16_t n = starbase_design ? (uint16_t)(idx - 0x10) : idx;
    const uint16_t tseg = FP_SEG(table);
    const uint16_t rec = (uint16_t)(n * 0x8d + FP_OFF(table));
    if ((rd8(tseg, (uint16_t)(rec + 0x7c)) & 2) != 0) {
      for (uint16_t k = 0; k < 4; k++) cost_out(out, k, 0);
      FRAME_LEAVE();
      return;
    }
    (void)CALL_ORIG(7, 0x3b60, FAR_CDECL, 4, player, rec, tseg, bpoff(-0x32));
    if (starbase_design && (rd8(pseg, (uint16_t)(poff + 5)) & 2) != 0) {
      uint32_t own = drd32((uint16_t)(player * 4 + 0x10c));
      const uint16_t cseg = FP_SEG(own);
      const uint16_t cur = (uint16_t)((rd8(pseg, (uint16_t)(poff + 0x2c)) & 0xf) * 0x8d + FP_OFF(own));
      (void)CALL_ORIG(7, 0x3b60, FAR_CDECL, 4, player, cur, cseg, bpoff(-0x42));
      if (rd16(tseg, rec) != rd16(cseg, cur)) {
        /* a different hull: the larger of half the cost and the cost less half of the old design's */
        for (uint16_t k = 0; k < 4; k++) {
          uint16_t cost = L16((int16_t)(-0x32 + 2 * k));
          uint16_t half = (uint16_t)(cost >> 1);
          int16_t old = (int16_t)L16((int16_t)(-0x42 + 2 * k));
          int16_t less = (int16_t)(cost - (uint16_t)(int16_t)((old - (old < 0 ? -1 : 0)) >> 1));
          L16((int16_t)(-0x32 + 2 * k)) = (less >= (int16_t)half) ? (uint16_t)less : half;
        }
      } else {
        uint32_t hull = far_call(2, 0x507c, (const uint16_t[1]){rd16(cseg, cur)}, 1);
        L16(-0x2a) = 0;
        L16(-0x26) = (uint16_t)hull;
        L16(-0x24) = (uint16_t)(hull >> 16);
        (void)CALL_ORIG(10, 0x7a92, FAR_CDECL, 3, player, bpoff(-0x2a), bpoff(-0x3a));
        for (uint16_t k = 0; k < 4; k++) L16((int16_t)(-0x32 + 2 * k)) = (uint16_t)(L16((int16_t)(-0x32 + 2 * k)) - L16((int16_t)(-0x3a + 2 * k)));
        uint16_t slots = rd8(cseg, (uint16_t)(cur + 0x7a));
        if ((int16_t)slots > 0) {
          uint16_t this_slot = (uint16_t)(rec + 0x3c), cur_slot = (uint16_t)(cur + 0x3c);
          for (; slots != 0; slots--, this_slot = (uint16_t)(this_slot + 4), cur_slot = (uint16_t)(cur_slot + 4)) {
            if (rd8(cseg, (uint16_t)(cur_slot + 1)) == 0 || rd8(tseg, (uint16_t)(this_slot + 1)) == 0) continue;
            L32(-0x2a) = rd32(cseg, (uint16_t)(cur_slot - 2));
            (void)far_call(2, 0x50f8, (const uint16_t[1]){bpoff(-0x2a)}, 1);
            (void)CALL_ORIG(10, 0x7a92, FAR_CDECL, 3, player, bpoff(-0x2a), bpoff(-0x3a));
            L32(-0x2a) = rd32(tseg, (uint16_t)(this_slot - 2));
            (void)far_call(2, 0x50f8, (const uint16_t[1]){bpoff(-0x2a)}, 1);
            (void)CALL_ORIG(10, 0x7a92, FAR_CDECL, 3, player, bpoff(-0x2a), bpoff(-0x22));
            const uint16_t same_category = (rd16(tseg, (uint16_t)(this_slot - 2)) == rd16(cseg, (uint16_t)(cur_slot - 2)));
            const uint16_t same_part = (uint8_t)(rd8(tseg, this_slot) ^ rd8(cseg, cur_slot)) == 0;
            for (uint16_t k = 0; k < 4; k++) {
              uint16_t a = (uint16_t)(rd8(cseg, (uint16_t)(cur_slot + 1)) * L16((int16_t)(-0x3a + 2 * k)));
              L16((int16_t)(-0x3a + 2 * k)) = a;
              uint16_t b = (uint16_t)(rd8(tseg, (uint16_t)(this_slot + 1)) * L16((int16_t)(-0x22 + 2 * k)));
              L16((int16_t)(-0x22 + 2 * k)) = b;
              uint16_t value;
              if (!same_category || !same_part) {
                /* a part of another category: 70 / 30 percent; another part of the same category: 80 / 20 */
                int16_t t1 = (int16_t)((int16_t)((int32_t)(int16_t)(same_category ? (uint16_t)(a << 3) : (uint16_t)(a * 7)) / -10) + (int16_t)b);
                int16_t u = (int16_t)((int32_t)(int16_t)(same_category ? (uint16_t)(b * 2) : (uint16_t)(b * 3)) / 10);
                int16_t best = (t1 >= u) ? t1 : u;
                value = (uint16_t)(b - (uint16_t)best);
              } else {
                int16_t diff = (int16_t)(b - a);
                value = (uint16_t)(b - (uint16_t)(diff >= 0 ? diff : 0));
              }
              L16((int16_t)(-0x32 + 2 * k)) = part_saving(L16((int16_t)(-0x32 + 2 * k)), value);
            }
          }
        }
      }
    }
    if (starbase_design) {
      if (player_trait(PLAYER_ROW(player), 3) != 0 || player_attr(PLAYER_ROW(player), 0x0e) == 8) {
        for (uint16_t k = 0; k < 4; k++) {
          uint16_t v = L16((int16_t)(-0x32 + 2 * k));
          L16((int16_t)(-0x32 + 2 * k)) = (uint16_t)(v - v / 5);
        }
      }
      for (uint16_t k = 0; k < 4; k++) cost_out(out, k, (uint16_t)((uint16_t)(L16((int16_t)(-0x32 + 2 * k)) + 1) >> 1));
    } else {
      for (uint16_t k = 0; k < 4; k++) cost_out(out, k, L16((int16_t)(-0x32 + 2 * k)));
    }
    goto scale;
  }
  if (idx > 0x1b) goto scale;
  switch (idx) {
    case 0:
    case 8:
      for (uint16_t k = 0; k < 3; k++) cost_out(out, k, 0);
      cost_out(out, 3, (uint32_t)(int32_t)(int16_t)player_attr(prow, 5));
      break;
    case 1:
    case 7: {
      uint16_t attr = player_trait(prow, 0x1f);
      if ((g_ui_flags_b_maybe & 8) != 0) {
        uint32_t v = (uint32_t)(int32_t)(int16_t)(uint16_t)(2 - attr);
        for (uint16_t k = 0; k < 3; k++) cost_out(out, k, v);
      } else {
        cost_out(out, 0, 0);
        cost_out(out, 1, 0);
        cost_out(out, 2, (uint32_t)(int32_t)(int16_t)(uint16_t)(4 - attr));
      }
      cost_out(out, 3, (uint32_t)(int32_t)(int16_t)player_attr(prow, 2));
      break;
    }
    case 2:
    case 9: {
      L16(-0x2a) = 0x8000;
      L16(-0x28) = (uint16_t)((L16(-0x28) & 0xff00) | 0x09);
      (void)far_call(2, 0x50f8, (const uint16_t[1]){bpoff(-0x2a)}, 1);
      const uint16_t tseg = L16(-0x24), toff = L16(-0x26);
      for (uint16_t k = 0; k < 3; k++) cost_out(out, k, (uint32_t)(int32_t)(int16_t)rd16(tseg, (uint16_t)(toff + 0x2c + 2 * k)));
      cost_out(out, 3, rd16(tseg, (uint16_t)(toff + 0x2a)));
      if (player_attr(prow, 0x0e) == 4) {
        for (uint16_t k = 0; k < 4; k++) {
          uint32_t x = drd32((uint16_t)(out + 4 * k));
          cost_out(out, k, (x * 3u) / 5u);
        }
      }
      break;
    }
    case 3:
    case 0x0b:
      for (uint16_t k = 0; k < 3; k++) cost_out(out, k, 0);
      cost_out(out, 3, (uint32_t)(int32_t)(int16_t)(uint16_t)((player_trait(prow, 6) == 0 ? 0x4b : 0) + 0x19));
      break;
    case 4:
    case 5:
    case 0x0c: {
      for (uint16_t k = 0; k < 3; k++) cost_out(out, k, 0);
      cost_out(out, 3, player_trait(prow, 1) == 0 ? 100 : 0x46);
      if (race == 3) cost_out(out, 3, drd32((uint16_t)(out + 12)) >> 1);
      break;
    }
    case 6:
    case 0x11: {
      int16_t v = (race == 6) ? 0x19 : (race == 7) ? 0x30 : 0x2c;
      for (uint16_t k = 0; k < 3; k++) cost_out(out, k, (uint32_t)(int32_t)v);
      cost_out(out, 3, (uint32_t)(int32_t)((race == 6) ? 5 : 10));
      break;
    }
    case 0x0d:
      L16(-0x2a) = 0x8000;
      L16(-0x28) = (uint16_t)((L16(-0x28) & 0xff00) | 0x0e);
      (void)far_call(2, 0x50f8, (const uint16_t[1]){bpoff(-0x2a)}, 1);
      (void)CALL_ORIG(10, 0x7a92, FAR_CDECL, 3, player, bpoff(-0x2a), bpoff(-0x32));
      for (uint16_t k = 0; k < 4; k++) cost_out(out, k, L16((int16_t)(-0x32 + 2 * k)));
      break;
    case 0x0e:
    case 0x0f:
    case 0x10: {
      uint16_t v = (race == 6) ? 0x46 : (race == 7) ? 0x78 : 0x6e;
      for (uint16_t k = 0; k < 3; k++) cost_out(out, k, (uint32_t)(int32_t)(int16_t)(((uint16_t)(idx - k) == 0x0e) ? v : 0));
      cost_out(out, 3, (uint32_t)(int32_t)((race == 6) ? 5 : 10));
      break;
    }
    default: { /* 0x12 to 0x1b: the parts of kind 0x8000, index 0x1b being read as 0x12 */
      uint16_t part = (uint16_t)((idx == 0x1b) ? 0 : idx - 0x12);
      L16(-0x2a) = 0x8000;
      L16(-0x28) = (uint16_t)((L16(-0x28) & 0xff00) | (uint8_t)part);
      (void)far_call(2, 0x50f8, (const uint16_t[1]){bpoff(-0x2a)}, 1);
      (void)CALL_ORIG(10, 0x7a92, FAR_CDECL, 3, player, bpoff(-0x2a), bpoff(-0x32));
      for (uint16_t k = 0; k < 4; k++) cost_out(out, k, L16((int16_t)(-0x32 + 2 * k)));
      break;
    }
    case 0x0a:
      break;
  }
scale:
  if (one_unit == 0) {
    for (uint16_t k = 0; k < 4; k++) cost_out(out, k, qty * drd32((uint16_t)(out + 4 * k)));
  }
  FRAME_LEAVE();
}

/* The near address of string `id` of the table 8:0x8df4. The original has already pushed `pending` words (the
 * arguments of the API call that uses the string), which the callee's frame lies below. */
static uint16_t string_with_pending(uint16_t id, uint16_t pending) {
  const uint16_t sp0 = rt_sp;
  rt_sp = (uint16_t)(sp0 - 2 * pending);
  uint16_t s = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, id);
  rt_sp = sp0;
  return s;
}
/* strlen of the string at seg:off. */
static uint16_t string_length(uint16_t seg, uint16_t off) {
  uint16_t n = 0;
  while (rd8(seg, (uint16_t)(off + n)) != 0) n++;
  return n;
}

/* production_paint_cost_panel: 26:0x1b5a (Ghidra FUN_10c8_1b5a, alias fn_26_1b5a), 1000 bytes.
 * ENTER 0x92,0; PUSH DI; PUSH SI. FAR CDECL, no result: the dialog window [BP+6] and a device context
 * [BP+8] (0 = take one with GetDC and release it at the end; the context is also stored back into [BP+8]).
 * Paints the "cost of the selected item" panel of the production dialog under the two lists: for the
 * list 0x416 (available items, pass 0) and the list 0x417 (the queue, pass 1) whose selection is the
 * item (LB_GETCURSEL, message 0x409; pass 1 skips an empty selection): the item is the sel-th available
 * entry with quantity (one unit) or the (sel-1)-th entry of the edited list DS:0xcd2; its cost comes from
 * 26:0x21e4 and is printed as a heading (string 0x32e), then four lines (iron, boron, germanium with
 * unit "kT", resources) in the colours of the table DS:0x418, labels from the near pointers of DS:0x464, the amount
 * right aligned with 8:0x9398 and, in pass 1, a description line (string 0x32f with the byte
 * (item word 1 >> 4) & 0x7f, plus the text 9:0x2112 builds in DS:0x5866). The window DS:0x4c78 / 0x4c7c
 * keeps the rectangle of the diamond marker (5:0x34fc, brush DS:0x5a5c) drawn at the end beside the
 * string 0xce. */
DECOMP(26, 0x1b5a, production_paint_cost_panel, FAR_CDECL)
void production_paint_cost_panel(uint16_t hdlg, uint16_t hdc)
{
  FRAME_ENTER_REGS(0x92, 2);
  uint16_t release = 0;
  if (hdc == 0) {
    release = 1;
    hdc = GetDC(hdlg);
  }
  (void)SelectObject(hdc, drd16(0x292e));
  uint16_t unit_text = string_with_pending(0x02ed, 1);
  uint16_t width = (uint16_t)GetTextExtent(hdc, dsfp(unit_text), 2);
  (void)SelectObject(hdc, drd16(0x2930));
  (void)SetBkColor(hdc, drd32(0x253a));
  BP_LOCAL(uint16_t, 8) = hdc;
  for (uint16_t pass = 0; (int16_t)pass < 2; pass++) {
    const uint16_t list_id = (pass == 0) ? 0x416 : 0x417;
    GetWindowRect(GetDlgItem(hdlg, list_id), bpfp(-0x1c));
    ScreenToClient(hdlg, bpfp(-0x1c));
    ScreenToClient(hdlg, bpfp(-0x18));
    L32(-0xa) = SendMessage(GetDlgItem(hdlg, list_id), 0x0409, 0, 0);
    if ((int16_t)(L32(-0xa) >> 16) < 0) continue;
    if (L32(-0xa) == 0 && pass == 1) continue;
    uint32_t entry;
    if (pass == 0) {
      uint16_t i = 0;
      if ((int16_t)g_entry_list_count_maybe > 0) {
        uint16_t at = g_entry_list_ptr_maybe;
        for (;;) {
          if ((drd16(at) & PQ_QTY_MASK) != 0) {
            uint32_t before = L32(-0xa);
            L32(-0xa) = before - 1;
            if (before == 0) break;
          }
          at = (uint16_t)(at + 4);
          i++;
          if (!((int16_t)i < (int16_t)g_entry_list_count_maybe)) break;
        }
      }
      uint32_t e = drd32((uint16_t)(g_entry_list_ptr_maybe + (uint16_t)(i << 2)));
      entry = (e & 0xffff0000u) | (uint16_t)(((uint16_t)e & 0xfc01) | 1);
    } else {
      L32(-0xa) = L32(-0xa) - 1;
      uint16_t at = (uint16_t)((uint16_t)L32(-0xa) << 2);
      entry = rd32(FP_SEG(g_production_list), (uint16_t)(FP_OFF(g_production_list) + at + 4));
    }
    L32(-0x14) = entry;
    {
      const uint16_t a[7] = {CURSOR_PLANET, MEM_SEL_DGROUP, bpoff(-0x14), rt_ss, bpoff(-0x2c), g_current_player, 0};
      (void)call26(0x21e4, a, 7);
    }
    L16(-0x16) = (uint16_t)(drd16(0x4b6e) + 4);
    (void)SelectObject(hdc, drd16(0x2930));
    {
      uint16_t heading = string_with_pending(0x032e, 3);
      (void)TextOut(hdc, (int16_t)L16(-0x1c), (int16_t)L16(-0x16), dsfp(heading), 0x12);
    }
    L16(-0x1c) = (uint16_t)(L16(-0x1c) + 0x14);
    L16(-0x18) = (uint16_t)(L16(-0x18) - 0x14);
    uint16_t line = 0;
    for (uint16_t cost = bpoff(-0x2c); cost <= bpoff(-0x20); cost = (uint16_t)(cost + 4), line++) {
      const uint16_t row = (cost == bpoff(-0x20)) ? 5 : line;
      L16(-0x16) = (uint16_t)(L16(-0x16) + drd16(0x267a));
      (void)SelectObject(hdc, drd16(0x2930));
      (void)SetTextColor(hdc, drd32((uint16_t)(0x418 + 4 * row)));
      const uint16_t label = drd16((uint16_t)(0x464 + 2 * row));
      (void)TextOut(hdc, (int16_t)L16(-0x1c), (int16_t)L16(-0x16), dsfp(label), (int16_t)lstrlen(dsfp(label)));
      (void)SelectObject(hdc, drd16(0x292e));
      (void)SetTextColor(hdc, drd32(0x4ac0));
      const uint16_t sp0 = rt_sp;
      const uint32_t amount = rd32(rt_ss, cost);
      pushw((uint16_t)(amount >> 16));
      pushw((uint16_t)amount);
      uint16_t n = (uint16_t)_wsprintf(dsfp(NAME_BUF), dsfp(drd16(0x0170)), MK_FP(rt_ss, rt_sp));
      rt_sp = sp0;
      (void)CALL_ORIG(8, 0x9398, FAR_CDECL, 6, hdc, (uint16_t)(L16(-0x18) - width - 2), L16(-0x16), NAME_BUF, n, drd16(0x288a));
      if (cost <= bpoff(-0x24)) {
        uint16_t unit = string_with_pending(0x02ed, 3);
        (void)TextOut(hdc, (int16_t)(uint16_t)(L16(-0x18) - width), (int16_t)L16(-0x16), dsfp(unit), 2);
      }
    }
    if (pass != 0) {
      uint16_t h = drd16(0x267a);
      L16(-0x16) = (uint16_t)(L16(-0x16) + (uint16_t)((int16_t)(uint16_t)(3 * h) / 2));
      (void)SelectObject(hdc, drd16(0x2930));
      const uint16_t v = (uint16_t)((L16(-0x12) >> 4) & 0x7f);
      const uint16_t sp0 = rt_sp;
      pushw(v);
      uint16_t fmt = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x032f);
      L16(-6) = (uint16_t)_wsprintf(bpfp(-0x90), dsfp(fmt), MK_FP(rt_ss, (uint16_t)(sp0 - 2)));
      rt_sp = sp0;
      (void)CALL_ORIG(9, 0x2112, FAR_CDECL, 7, CURSOR_PLANET, MEM_SEL_DGROUP, (uint16_t)g_production_list,
                      (uint16_t)(g_production_list >> 16), L16(-0xa), 0, 0);
      copy_string(rt_ss, (uint16_t)(bpoff(-0x90) + L16(-6)), MEM_SEL_DGROUP, NAME_BUF);
      (void)TextOut(hdc, (int16_t)(uint16_t)(L16(-0x1c) - 0x14), (int16_t)L16(-0x16), bpfp(-0x90),
                    (int16_t)string_length(rt_ss, bpoff(-0x90)));
      (void)SelectObject(hdc, drd16(0x292e));
    }
  }
  GetClientRect(hdlg, bpfp(-0x1c));
  {
    uint16_t h = drd16(0x267a);
    uint16_t cx = (uint16_t)(h | 1);
    uint16_t half5 = (uint16_t)((int16_t)(uint16_t)(5 * h) / 2);
    L16(-0x1a) = (uint16_t)((uint16_t)(L16(-0x16) - half5) - 0x0c);
    L16(-0x16) = (uint16_t)(cx + L16(-0x1a));
    L16(-0x1c) = 6;
    L16(-0x18) = (uint16_t)(h + 6);
  }
  {
    const uint16_t a[3] = {hdc, bpoff(-0x1c), drd16(0x5a5c)};
    (void)far_call(5, 0x34fc, a, 3);
  }
  dwr32(0x4c78, L32(-0x1c));
  dwr32(0x4c7c, L32(-0x18));
  (void)SelectObject(hdc, drd16(0x2930));
  uint16_t n = (uint16_t)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, 0x00ce, NAME_BUF);
  (void)TextOut(hdc, (int16_t)(uint16_t)(L16(-0x18) + 4), (int16_t)L16(-0x1a), dsfp(NAME_BUF), (int16_t)n);
  if (release != 0) (void)ReleaseDC(hdlg, hdc);
  FRAME_LEAVE();
}

/* Signed halving as the compiler writes it for "x / 2" (CDQ; SUB AX,DX; SAR AX,1): truncates toward zero. */
static inline int16_t half(int16_t x) { return (int16_t)((x - (x < 0 ? -1 : 0)) >> 1); }
/* One SetWindowPos of a control of the production dialog, SWP_NOZORDER (4): moved and sized. */
static void place_control(uint16_t hdlg, uint16_t id, int16_t x, int16_t y, int16_t cx, int16_t cy) {
  (void)SetWindowPos(GetDlgItem(hdlg, id), 0, x, y, cx, cy, 4);
}

/* production_dialog_init: the WM_INITDIALOG part of 26:0x08e2 (the layout of the production dialog). The ids at
 * 0x42e, 0x42f, 1, 2 are the four buttons under the lists, the ids 0x418, 0x419, 0x439, 0x43a, 0x42d and 0x76 the
 * six buttons beside them, 0x8b the research check box, 0x416 the list of available items and 0x417 the
 * queue. All sizes derive from the client rectangle and the text height DS:0x267a. */
static void production_dialog_layout(uint16_t hwnd) {
  static const uint16_t ids[10] = {0x42e, 0x42f, 1, 2, 0x418, 0x419, 0x439, 0x43a, 0x42d, 0x76};
  production_dialog_hwnd = hwnd;
  if ((int16_t)drd16((uint16_t)(g_current_player * 0xc0 + 0x5a66)) <= 1) {
    (void)EnableWindow(GetDlgItem(hwnd, 0x42f), 0);
    (void)EnableWindow(GetDlgItem(hwnd, 0x42e), 0);
  }
  int16_t width, height;
  const int16_t text_h = (int16_t)drd16(0x267a);
  if ((drd8(0x0791) & 0xc0) >= 0x40) {
    width = 0x2f8;
    height = 0x244;
  } else {
    width = 0x262;
    height = (int16_t)(uint16_t)((uint16_t)(text_h + 1) * 0x18);
  }
  (void)SetWindowPos(hwnd, 0, 0, 0, width, height, 6);
  GetClientRect(hwnd, bpfp(-0x14));
  const int16_t right = (int16_t)L16(-0x10);
  const int16_t mid_x = (int16_t)(right >> 1);
  const int16_t row_y = (int16_t)(half((int16_t)(uint16_t)(text_h * -0x11)) + (int16_t)L16(-0xe) - 0x18);
  int16_t x0 = (int16_t)((int16_t)((int32_t)(int16_t)(uint16_t)(right * 11) / 0x14) + 0x10);
  int32_t span = (int16_t)(right - x0);
  const int16_t quarter = (int16_t)(((span + ((span >> 31) & 3)) >> 2));
  const int16_t line_h = half((int16_t)(uint16_t)(text_h * 3));
  int16_t bottom = (int16_t)(L16(-0xe) - (uint16_t)(line_h + 6));
  place_control(hwnd, 0x8b, 6, bottom, (int16_t)(x0 - 0xc), line_h);
  for (uint16_t i = 0; i < 4; i++) {
    place_control(hwnd, ids[i], x0, bottom, (int16_t)(quarter - 6), line_h);
    x0 = (int16_t)(x0 + quarter);
  }
  const int16_t wide = (int16_t)(quarter + 0x18);
  x0 = (int16_t)(mid_x - (int16_t)((int16_t)(wide - 6) >> 1));
  int16_t step = (int16_t)((int32_t)(int16_t)(row_y - (int16_t)(uint16_t)(text_h * 9)) / 5);
  step = (int16_t)(step + line_h - 3);
  int16_t y = 8;
  if (step > 0x32) {
    y = (int16_t)(half((int16_t)(uint16_t)((uint16_t)(step - 0x32) * 5)) + 8);
    step = 0x32;
  }
  for (uint16_t i = 4; i < 10; i++) {
    place_control(hwnd, ids[i], x0, y, (int16_t)(wide - 6), line_h);
    y = (int16_t)(y + step);
  }
  place_control(hwnd, 0x416, 6, 6, (int16_t)(x0 - 0xc), row_y);
  GetWindowRect(GetDlgItem(hwnd, 0x416), bpfp(-0x1c));
  place_control(hwnd, 0x417, (int16_t)(wide + x0), 6, (int16_t)(x0 - 0xc), (int16_t)(L16(-0x16) - L16(-0x1a)));
  ScreenToClient(hwnd, bpfp(-0x18));
  dwr16(0x4b6e, L16(-0x16));
}

/* production_dialog_proc: 26:0x08e2 (Ghidra FUN_10c8_08e2, alias fn_26_08e2), 1425 bytes. FAR PASCAL (hwnd [BP+0xE],
 * msg [BP+0xC], wParam [BP+0xA], lParam [BP+6]), ENTER 0xf0,0; PUSH DI; PUSH SI. The dialog procedure of the
 * production queue dialog (template 0x5d, started by 26:0x0000). Returns AX:
 *   WM_PAINT 0x0f       paints with 26:0x1b5a (the context of BeginPaint, the client rectangle at BP-0x14)
 *   WM_ERASEBKGND 0x14  fills the client rectangle with the brush DS:0x10
 *   WM_CTLCOLOR 0x19    the check box 0x8b and the static controls (type 6) get the colour DS:0x253a and the brush DS:0x10
 *   WM_SETCURSOR 0x20   the cursor DS:0x4ac8 over the diamond marker rectangle DS:0x4c78
 *   WM_DRAWITEM 0x2b    owner-drawn lists: 9:0x3f16 (item -1: -2) or 9:0x3f3c (draw, select or focus: 4)
 *   WM_MEASUREITEM 0x2c item height DS:0x267a + 2
 *   WM_INITDIALOG 0x110 lays the controls out and fills the lists (26:0x1a40)
 *   WM_COMMAND 0x111    26:0x0e74
 *   WM_LBUTTONDOWN 0x201 and WM_RBUTTONDOWN 0x204 over the marker: the left button shows the help text
 *   (24:0x091e), the right button a pop-up of the ZIP production presets in use (24:0x0e24) with the entry
 *   "edit presets" (string 0x4af, which runs the ZIP production dialog 26:0x2d26 on a copy of the presets
 *   at BP-0xf0) or the chosen preset (26:0x0e74 command 0x816). */
DECOMP(26, 0x08e2, production_dialog_proc, FAR_PASCAL)
uint16_t production_dialog_proc(uint16_t hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam)
{
  FRAME_ENTER_REGS(0xf0, 2);
  uint16_t ret = 0;
  switch (msg) {
  case 0x002b: { /* WM_DRAWITEM */
    const uint16_t ioff = FP_OFF(lParam), iseg = FP_SEG(lParam);
    ret = 1;
    if (rd16(iseg, (uint16_t)(ioff + 4)) == 0xffff) {
      (void)CALL_ORIG(9, 0x3f16, FAR_CDECL, 3, ioff, iseg, 0xfffe);
    } else {
      uint16_t action = rd16(iseg, (uint16_t)(ioff + 6));
      if (action == 1 || action == 2 || action == 4) (void)CALL_ORIG(9, 0x3f3c, FAR_CDECL, 3, ioff, iseg, 4);
    }
    break;
  }
  case 0x002c: /* WM_MEASUREITEM */
    wr16(FP_SEG(lParam), (uint16_t)(FP_OFF(lParam) + 8), (uint16_t)(drd16(0x267a) + 2));
    ret = 1;
    break;
  case 0x000f: { /* WM_PAINT */
    uint16_t hdc = BeginPaint(hwnd, bpfp(-0x50));
    GetClientRect(hwnd, bpfp(-0x14));
    const uint16_t a[4] = {hwnd, hdc, bpoff(-0x14), 0xffff};
    (void)call26(0x1b5a, a, 4);
    EndPaint(hwnd, bpfp(-0x50));
    ret = 1;
    break;
  }
  case 0x0014: /* WM_ERASEBKGND */
    GetClientRect(hwnd, bpfp(-0x14));
    (void)FillRect(wParam, bpfp(-0x14), g_brush_btnface);
    ret = 1;
    break;
  case 0x0019: /* WM_CTLCOLOR */
    if (GetDlgItem(hwnd, 0x8b) == (uint16_t)lParam || (uint16_t)(lParam >> 16) == 6) {
      (void)SetBkColor(wParam, drd32(0x253a));
      ret = g_brush_btnface;
    }
    break;
  case 0x0020: /* WM_SETCURSOR */
    GetCursorPos(bpfp(-4));
    ScreenToClient(hwnd, bpfp(-4));
    if (PtInRect(dsfp(0x4c78), L32(-4)) != 0) {
      (void)SetCursor(drd16(0x4ac8));
      ret = 1;
    }
    break;
  case 0x0110: /* WM_INITDIALOG */
    production_dialog_layout(hwnd);
    {
      const uint16_t a[1] = {hwnd};
      (void)call26(0x1a40, a, 1);
    }
    if ((drd8(0x0791) & 0xc0) == 0x40 && drd16(0x0cdc) == 0xffff) dwr16(0x0cdc, 0);
    (void)CALL_ORIG(8, 0x97a2, FAR_CDECL, 3, hwnd, 0x0cda, 1);
    ret = 1;
    break;
  case 0x0111: { /* WM_COMMAND */
    const uint16_t a[4] = {hwnd, wParam, (uint16_t)lParam, (uint16_t)(lParam >> 16)};
    (void)call26(0x0e74, a, 4);
    break;
  }
  case 0x0201: /* WM_LBUTTONDOWN */
  case 0x0204: { /* WM_RBUTTONDOWN */
    L16(-0xc) = (uint16_t)lParam;
    L16(-0xa) = (uint16_t)(lParam >> 16);
    if (PtInRect(dsfp(0x4c78), lParam) == 0) break;
    if (msg == 0x0201) {
      dwr32(0x0bc0, 0x00b4000a);
      dwr16(0x0bc4, 0x4c86);
      (void)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, 0x00cf, 0x4c86);
      (void)CALL_ORIG(24, 0x091e, FAR_CDECL, 3, hwnd, L16(-0xc), L16(-0xa));
      break;
    }
    L16(-8) = 0x00ff;
    uint16_t count = 0, at = bpoff(-0x20);
    for (uint16_t preset = 0x2583; preset < 0x2623; preset = (uint16_t)(preset + 0x28)) {
      if (drd8(preset) != 0) {
        wr16(rt_ss, at, (uint16_t)(preset - 0x0d));
        at = (uint16_t)(at + 2);
        count++;
      }
    }
    uint16_t n = count;
    wr16(rt_ss, (uint16_t)(bpoff(-0x20) + 2 * n), bpoff(-8));
    uint16_t more = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x04af);
    n++;
    wr16(rt_ss, (uint16_t)(bpoff(-0x20) + 2 * n), more);
    n++;
    int16_t sel = (int16_t)(uint16_t)CALL_ORIG(24, 0x0e24, FAR_CDECL, 8, hwnd, L16(-0xc), L16(-0xa), n, 0, bpoff(-0x20), 0xffff, 1);
    if ((uint16_t)(n - (uint16_t)sel - 1) == 0) {
      for (uint16_t k = 0; k < 40; k++) wr32(rt_ss, (uint16_t)(bpoff(-0xf0) + 4 * k), drd32((uint16_t)(0x2576 + 4 * k)));
      uint32_t proc = MakeProcInstance(MK_FP(SEG_SEL(26), 0x2d26), g_app_instance);
      L16(-4) = (uint16_t)proc;
      L16(-2) = (uint16_t)(proc >> 16);
      L16(-6) = (uint16_t)API_SP(12, DialogBox(g_app_instance, 0x00000059, hwnd, proc));
      FreeProcInstance(L32(-4));
      if (L16(-6) == 0) {
        for (uint16_t k = 0; k < 40; k++) dwr32((uint16_t)(0x2576 + 4 * k), rd32(rt_ss, (uint16_t)(bpoff(-0xf0) + 4 * k)));
      }
    } else if (sel >= 0) {
      uint16_t index = 0;
      for (uint16_t preset = 0x2583; preset < 0x2623; preset = (uint16_t)(preset + 0x28), index++) {
        if (drd8(preset) != 0) {
          uint16_t was = (uint16_t)sel;
          sel--;
          if (was == 0) break;
        }
      }
      const uint16_t a[4] = {hwnd, 0x0816, index, (uint16_t)((int16_t)index < 0 ? 0xffff : 0)};
      (void)call26(0x0e74, a, 4);
    }
    break;
  }
  default:
    break;
  }
  FRAME_LEAVE();
  return ret;
}

/* Same part of the item: class (bits 1 to 3 of w1) and item index, as 26:0x0e74 compares an entry of the edited
 * list with an entry of the list of available items (quantities ignored). */
static bool same_item(const struct ProdItem *a, const struct ProdItem *b) {
  uint32_t da = ((uint32_t)a->w1 << 16) | a->w0, db = ((uint32_t)b->w1 << 16) | b->w0;
  return ((a->w1 ^ b->w1) & PQ_CLASS_MASK) == 0 && ((da ^ db) & PRODITEM_ITEM_DWORD_MASK) == 0;
}
/* The edited list (DS:0xcd2): its entry count byte and the 16-bit offsets of its entries. */
static inline uint16_t list_off(void) { return FP_OFF(g_production_list); }
static inline uint16_t list_seg(void) { return FP_SEG(g_production_list); }
static inline uint8_t list_count(void) { return rd8(list_seg(), (uint16_t)(list_off() + 3)); }
/* The controls' screen refresh at the end of most commands (26:0x0e74 offset 0x19ef): the rectangle under the
 * lists (top DS:0x4b6e, 7 text heights tall, moved right by 0x82) is invalidated and the cost panel repainted. */
static void production_refresh_panel(uint16_t hdlg) {
  GetClientRect(hdlg, bpfp(-0x1e));
  L16(-0x1c) = drd16(0x4b6e);
  const uint16_t h = drd16(0x267a);
  L16(-0x18) = (uint16_t)(L16(-0x1c) + (uint16_t)(7 * h));
  L16(-0x1e) = (uint16_t)(L16(-0x1e) + 0x82);
  InvalidateRect(hdlg, bpfp(-0x1e), 1);
  const uint16_t a[4] = {hdlg, 0, bpoff(-0x1e), 0xffff};
  (void)call26(0x1b5a, a, 4);
}
/* Redraw the lists of items: 26:0x1f42 for the available items (offset 0x19da). */
static void production_refresh_available(uint16_t hdlg) {
  const uint16_t a[2] = {GetDlgItem(hdlg, 0x416), 0xffff};
  (void)call26(0x1f42, a, 2);
}
static void production_set_queue_list(uint16_t hdlg) {
  /* 9:0x431c(list 0x417, the edited list DS:0xcd2, 0, 0): five words pushed, ten bytes removed */
  (void)CALL_ORIG(9, 0x431c, FAR_CDECL, 5, GetDlgItem(hdlg, 0x417), (uint16_t)g_production_list,
                  (uint16_t)(g_production_list >> 16), 0, 0);
}
/* The amount a click on "add" / "remove" moves: Ctrl adds 100, Shift 10, both all (0x3fc), none one. */
static bool key_down(int16_t vk) { return ((uint16_t)GetAsyncKeyState(vk) & 0xfffe) != 0; }

/* production_dialog_command: 26:0x0e74 (Ghidra FUN_10c8_0e74, alias fn_26_0e74), 3019 bytes.
 * ENTER 0x28,0; PUSH DI; PUSH SI. FAR CDECL, no result: the WM_COMMAND handler of the production queue dialog
 * (called by 26:0x08e2 with the dialog window [BP+6], the control id [BP+8], the control window [BP+0xA]
 * and the notification code [BP+0xC]; 26:0x0e74 is also called by 26:0x08e2 for the pop-up of presets
 * with the id 0x816 and the preset number in the third word). By id:
 *   1, 2    OK / Cancel: DS:0xcd6 cleared, 8:0x97a2(dlg, 0xcda, 0), EndDialog(dlg, id == 1)
 *   0x76    Help (WinHelp, command 1, topic 0x423)
 *   0x8b    the research check box: bit 7 of DS:0x4c22 := its state; the queue list is redrawn with its selection
 *   0x416, 0x417  list notifications: code 1 (selection change) refreshes the panel; code 2 (double click)
 *           adds (available items list, as 0x418) or removes (queue list, as 0x419)
 *   0x418   add: the selected available item goes into the queue after the selection (Ctrl 100, Shift 10,
 *           both 0x3fc of it, else one), merging with the neighbour of the same item (at most 0x3fc, the
 *           auto alchemy item at most 1); the available quantity is reduced; beep when the queue has 40 entries
 *   0x419   remove: the selected queue entry gives its quantity back to the available items
 *   0x42d   clear the queue (every quantity goes back); 0x816 the same and load ZIP production preset
 *           [BP+0xA] (its items become class 1 entries, automatic items 0 to 6 of the old queue are dropped,
 *           the research flag comes from the preset)
 *   0x42e, 0x42f  previous / next planet (26:0x0810 commits, 9:0x2ed8 selects, 26:0x0100 reopens, 26:0x1a40)
 *   0x439, 0x43a  move the selected queue entry up / down
 * Most commands end by refreshing the panel under the lists (see production_refresh_panel). */
DECOMP(26, 0x0e74, production_dialog_command, FAR_CDECL)
void production_dialog_command(uint16_t hdlg, uint16_t id, uint16_t ctl, uint16_t code)
{
  FRAME_ENTER_REGS(0x28, 2);
  (void)ctl;
  switch (id) {
  case 1:
  case 2:
    production_dialog_hwnd = 0;
    (void)CALL_ORIG(8, 0x97a2, FAR_CDECL, 3, hdlg, 0x0cda, 0);
    (void)EndDialog(hdlg, id == 1 ? 1 : 0);
    goto done;
  case 0x76:
    (void)WinHelp(hdlg, dsfp(drd16(0x010a)), 1, 0x00000423);
    goto done;
  case 0x8b: {
    uint16_t list = GetDlgItem(hdlg, 0x417);
    uint16_t checked = (uint16_t)SendMessage(GetDlgItem(hdlg, 0x8b), 0x0400, 0, 0);
    dwr16(0x4c22, (uint16_t)(drd16(0x4c22) ^ (((uint16_t)(checked << 7) ^ drd16(0x4c22)) & 0x80)));
    uint32_t sel = SendMessage(list, 0x0409, 0, 0);
    (void)CALL_ORIG(9, 0x431c, FAR_CDECL, 5, list, (uint16_t)g_production_list, (uint16_t)(g_production_list >> 16), 0, 0);
    (void)SendMessage(list, 0x0407, (uint16_t)sel, 0);
    goto refresh;
  }
  case 0x416:
  case 0x417:
    if (code == 1) goto refresh;
    if (code != 2) goto done;
    if (id != 0x416) goto remove_entry;
    /* fall through: a double click in the available items list adds */
  case 0x418: {
    uint32_t sel = SendMessage(GetDlgItem(hdlg, 0x416), 0x0409, 0, 0);
    if ((int16_t)(sel >> 16) < 0) goto done;
    uint16_t di = 0;
    if ((int16_t)g_entry_list_count_maybe > 0) {
      uint16_t at = g_entry_list_ptr_maybe;
      do {
        if ((drd16(at) & PQ_QTY_MASK) != 0) {
          uint32_t before = sel;
          sel--;
          if (before == 0) break;
        }
        at = (uint16_t)(at + 4);
        di++;
      } while ((int16_t)di < (int16_t)g_entry_list_count_maybe);
    }
    const uint16_t avail_off = (uint16_t)(di << 2);
    uint32_t entry = drd32((uint16_t)(avail_off + g_entry_list_ptr_maybe));
    uint16_t entry_w0 = (uint16_t)entry, entry_w1 = (uint16_t)(entry >> 16);
    if (key_down(0x11)) {
      uint16_t qty = (uint16_t)(entry_w0 & PQ_QTY_MASK), amount;
      if (key_down(0x10)) amount = (qty < 0x3fc) ? qty : 0x3fc;
      else amount = (qty < 100) ? qty : 100;
      entry_w0 = (uint16_t)(entry_w0 ^ ((amount ^ entry_w0) & PQ_QTY_MASK));
    } else if (key_down(0x10)) {
      uint16_t qty = (uint16_t)(entry_w0 & PQ_QTY_MASK);
      uint16_t amount = (qty < 10) ? qty : 10;
      entry_w0 = (uint16_t)(entry_w0 ^ ((amount ^ entry_w0) & PQ_QTY_MASK));
    } else {
      entry_w0 = (uint16_t)((entry_w0 & 0xfc01) | 1);
    }
    entry = ((uint32_t)entry_w1 << 16) | entry_w0;
    {
      struct ProdItem *a = FAR_REC(struct ProdItem, MEM_SEL_DGROUP, (uint16_t)(avail_off + g_entry_list_ptr_maybe));
      if ((a->w0 & PQ_QTY_MASK) != PQ_QTY_ANY) a->w0 = (uint16_t)(a->w0 ^ (((uint16_t)(a->w0 - entry_w0) ^ a->w0) & PQ_QTY_MASK));
    }
    int16_t n = list_count();
    uint32_t qsel = SendMessage(GetDlgItem(hdlg, 0x417), 0x0409, 0, 0);
    int16_t at_entry; /* the entry after which the item goes (di) */
    if ((int16_t)(qsel >> 16) >= 0) at_entry = (int16_t)((uint16_t)qsel - 1);
    else {
      at_entry = (int16_t)(n - 1);
      if (at_entry < 0) at_entry = 0;
    }
    int16_t merged = -1;
    if (n > at_entry) {
      if (at_entry >= 0) {
        const struct ProdItem *e = FAR_REC(struct ProdItem, list_seg(), (uint16_t)(list_off() + 4 + (uint16_t)(at_entry << 2)));
        struct ProdItem probe = {entry_w0, entry_w1};
        if (same_item(e, &probe)) merged = at_entry;
      }
      if (merged < 0) {
        at_entry++;
        if (at_entry < n) {
          const struct ProdItem *e = FAR_REC(struct ProdItem, list_seg(), (uint16_t)(list_off() + 4 + (uint16_t)(at_entry << 2)));
          struct ProdItem probe = {entry_w0, entry_w1};
          if (same_item(e, &probe)) merged = at_entry;
        }
      }
    }
    if (merged >= 0) {
      struct ProdItem *e = FAR_REC(struct ProdItem, list_seg(), (uint16_t)(list_off() + 4 + (uint16_t)(merged << 2)));
      uint32_t sum = (uint32_t)(e->w0 & PQ_QTY_MASK) + (uint32_t)(entry_w0 & PQ_QTY_MASK);
      uint16_t qty = (sum > 0x3fc) ? 0x3fc : (uint16_t)sum;
      e->w0 = (uint16_t)(e->w0 ^ ((qty ^ e->w0) & PQ_QTY_MASK));
      e = FAR_REC(struct ProdItem, list_seg(), (uint16_t)(list_off() + 4 + (uint16_t)(merged << 2)));
      uint32_t full = ((uint32_t)e->w1 << 16) | e->w0;
      if ((e->w0 & PQ_QTY_MASK) > 1 && (full & PRODITEM_ITEM_DWORD_MASK) == 0x0c00 && (e->w1 & PQ_CLASS_MASK) == (PQ_CLASS_ITEM << 1))
        e->w0 = (uint16_t)((e->w0 & 0xfc01) | 1);
      di = (uint16_t)merged;
    } else {
      di = (uint16_t)at_entry;
      if (n >= 0x28) {
        (void)MessageBeep(0);
        goto refresh;
      }
      if (rd8(list_seg(), (uint16_t)(list_off() + 2)) == (uint8_t)n) {
        const uint16_t a[3] = {list_off(), list_seg(), (uint16_t)(n + 4)};
        g_production_list = (uint32_t)far_call(12, 0x0528, a, 3);
      }
      if ((int16_t)n != (int16_t)di) {
        uint16_t base = (uint16_t)(list_off() + (uint16_t)(di << 2));
        (void)CALL_ORIG(34, 0x0c98, FAR_CDECL, 5, (uint16_t)(base + 8), list_seg(), (uint16_t)(base + 4), list_seg(),
                        (uint16_t)((uint16_t)(n - di) << 2));
      }
      wr32(list_seg(), (uint16_t)(list_off() + (uint16_t)(di << 2) + 4), entry);
      wr8(list_seg(), (uint16_t)(list_off() + 3), (uint8_t)(list_count() + 1));
    }
    production_set_queue_list(hdlg);
    (void)SendMessage(GetDlgItem(hdlg, 0x417), 0x0407, (uint16_t)(di + 1), 0);
    if ((drd16((uint16_t)(avail_off + g_entry_list_ptr_maybe)) & PQ_QTY_MASK) != 0) goto refresh;
    production_refresh_available(hdlg);
    goto refresh;
  }
  case 0x419:
  remove_entry : {
    uint32_t sel = SendMessage(GetDlgItem(hdlg, 0x417), 0x0409, 0, 0);
    if ((int16_t)(sel >> 16) < 0) goto done;
    if ((int16_t)(sel >> 16) == 0 && (uint16_t)sel == 0) goto done;
    uint16_t cnt = list_count();
    sel--;
    const uint16_t slot = (uint16_t)((uint16_t)sel << 2);
    uint32_t entry = rd32(list_seg(), (uint16_t)(list_off() + slot + 4));
    uint16_t entry_w0 = (uint16_t)entry, entry_w1 = (uint16_t)(entry >> 16);
    uint16_t di = 0;
    if ((int16_t)g_entry_list_count_maybe > 0) {
      uint16_t at = g_entry_list_ptr_maybe;
      do {
        const struct ProdItem *a = FAR_REC(struct ProdItem, MEM_SEL_DGROUP, at);
        struct ProdItem probe = {entry_w0, entry_w1};
        if (same_item(a, &probe)) break;
        at = (uint16_t)(at + 4);
        di++;
      } while ((int16_t)di < (int16_t)g_entry_list_count_maybe);
    }
    const uint16_t avail_off = (uint16_t)(di << 2);
    const uint16_t refresh_available = ((drd16((uint16_t)(avail_off + g_entry_list_ptr_maybe)) & PQ_QTY_MASK) == 0) ? 1 : 0;
    uint16_t amount;
    if (key_down(0x11)) amount = key_down(0x10) ? 0x3fc : 100;
    else amount = key_down(0x10) ? 10 : 1;
    uint16_t limit = amount;
    if ((entry_w1 & PQ_CLASS_MASK) == (PQ_CLASS_ITEM << 1) && ((((uint32_t)entry_w1 << 16) | entry_w0) & PRODITEM_ITEM_DWORD_MASK) == 0x0c00)
      limit = 0x3fc;
    int16_t moved = (int16_t)(entry_w0 & PQ_QTY_MASK);
    if (moved > (int16_t)limit) moved = (int16_t)limit;
    {
      struct ProdItem *a = FAR_REC(struct ProdItem, MEM_SEL_DGROUP, (uint16_t)(avail_off + g_entry_list_ptr_maybe));
      if ((a->w0 & PQ_QTY_MASK) != PQ_QTY_ANY) a->w0 = (uint16_t)(a->w0 ^ (((uint16_t)(a->w0 + (uint16_t)moved) ^ a->w0) & PQ_QTY_MASK));
    }
    {
      struct ProdItem *e = FAR_REC(struct ProdItem, list_seg(), (uint16_t)(list_off() + slot + 4));
      e->w0 = (uint16_t)(e->w0 ^ (((uint16_t)(e->w0 - (uint16_t)moved) ^ e->w0) & PQ_QTY_MASK));
    }
    if ((rd16(list_seg(), (uint16_t)(list_off() + slot + 4)) & PQ_QTY_MASK) == 0) {
      int32_t next = (int32_t)sel + 1;
      if (next >= (int32_t)cnt) {
        sel--;
      } else {
        uint16_t base = (uint16_t)(slot + list_off());
        (void)CALL_ORIG(34, 0x0c98, FAR_CDECL, 5, (uint16_t)(base + 4), list_seg(), (uint16_t)(base + 8), list_seg(),
                        (uint16_t)((uint16_t)(cnt - (uint16_t)sel - 1) << 2));
      }
      wr8(list_seg(), (uint16_t)(list_off() + 3), (uint8_t)(list_count() - 1));
    }
    production_set_queue_list(hdlg);
    if ((int16_t)(sel >> 16) >= 0) (void)SendMessage(GetDlgItem(hdlg, 0x417), 0x0407, (uint16_t)((uint16_t)sel + 1), 0);
    if (refresh_available != 0) production_refresh_available(hdlg);
    goto refresh;
  }
  case 0x42d:
  case 0x816: {
    /* every queue entry gives its quantity back to the available items */
    if (list_count() != 0) {
      uint16_t at = (uint16_t)(list_off() + 4);
      for (int16_t k = 0; (int16_t)list_count() > k; k++, at = (uint16_t)(at + 4)) {
        uint16_t di = 0;
        const struct ProdItem *q = FAR_REC(struct ProdItem, list_seg(), at);
        if ((int16_t)g_entry_list_count_maybe > 0) {
          do {
            if (same_item(q, FAR_REC(struct ProdItem, MEM_SEL_DGROUP, (uint16_t)(g_entry_list_ptr_maybe + (uint16_t)(di << 2))))) break;
            di++;
          } while ((int16_t)di < (int16_t)g_entry_list_count_maybe);
        }
        struct ProdItem *a = FAR_REC(struct ProdItem, MEM_SEL_DGROUP, (uint16_t)(g_entry_list_ptr_maybe + (uint16_t)(di << 2)));
        if ((a->w0 & PQ_QTY_MASK) != PQ_QTY_ANY) a->w0 = (uint16_t)(a->w0 ^ (((uint16_t)(a->w0 + q->w0) ^ a->w0) & PQ_QTY_MASK));
      }
    }
    if (id != 0x816) {
      wr8(list_seg(), (uint16_t)(list_off() + 3), 0);
    } else {
      const uint16_t preset = (uint16_t)(ctl * 0x28 + DS_ZIP_PRESETS);
      int16_t room = (int16_t)(drd8((uint16_t)(preset + 0xf)) + list_count());
      if (room < 1) room = 1;
      const uint16_t a[3] = {4, (uint16_t)room, 0};
      uint32_t fresh = far_call(12, 0x0568, a, 3);
      const uint16_t fseg = FP_SEG(fresh), foff = FP_OFF(fresh);
      for (uint16_t k = 0; k < (uint16_t)((uint16_t)room << 2); k++) wr8(fseg, (uint16_t)(foff + 4 + k), 0);
      uint16_t filled = 0;
      if (list_count() != 0) {
        uint16_t dst = (uint16_t)(foff + 4);
        uint16_t src = 0;
        for (int16_t k = 0; (int16_t)list_count() > k; k++, src = (uint16_t)(src + 4)) {
          const struct ProdItem *q = FAR_REC(struct ProdItem, list_seg(), (uint16_t)(list_off() + src + 4));
          if ((q->w1 & PQ_CLASS_MASK) == (PQ_CLASS_ITEM << 1) &&
              ((((uint32_t)q->w1 << 16) | q->w0) & PRODITEM_ITEM_DWORD_MASK) < 0x1c00)
            continue;
          wr32(fseg, dst, rd32(list_seg(), (uint16_t)(list_off() + src + 4)));
          dst = (uint16_t)(dst + 4);
          filled++;
        }
      }
      if (drd8((uint16_t)(preset + 0xf)) != 0) {
        uint16_t dst = (uint16_t)(foff + 4 + (uint16_t)(filled << 2));
        uint16_t item = (uint16_t)(preset + 0x10);
        for (int16_t k = 0; (int16_t)drd8((uint16_t)(preset + 0xf)) > k; k++, item = (uint16_t)(item + 2)) {
          if (player_attr(PLAYER_ROW(g_current_player), 0x0e) == 8 && (drd8(item) & 0x3f) <= 2) continue;
          if (player_attr(PLAYER_ROW(g_current_player), 0x0e) == 3 && ((drd8(item) & 0x3f) == 4 || (drd8(item) & 0x3f) == 5)) continue;
          struct ProdItem *e = FAR_REC(struct ProdItem, fseg, dst);
          pq_set_class(e, PQ_CLASS_ITEM);
          pq_set_item(e, (uint16_t)(drd8(item) & 0x3f));
          e->w0 = (uint16_t)(e->w0 ^ (((uint16_t)(drd16(item) >> 6) ^ e->w0) & PQ_QTY_MASK));
          dst = (uint16_t)(dst + 4);
          filled++;
        }
      }
      wr8(fseg, (uint16_t)(foff + 3), (uint8_t)filled);
      free_typed_array(g_production_list);
      g_production_list = fresh;
      {
        uint16_t kind = drd8((uint16_t)(preset + 0xe));
        dwr16(0x4c22, (uint16_t)(drd16(0x4c22) ^ (((uint16_t)(kind << 7) ^ drd16(0x4c22)) & 0x80)));
      }
      (void)SendMessage(GetDlgItem(hdlg, 0x8b), 0x0401, (uint16_t)((drd16(0x4c22) >> 7) & 1), 0);
    }
    production_set_queue_list(hdlg);
    (void)SendMessage(GetDlgItem(hdlg, 0x417), 0x0407, 0, 0);
    {
      const uint16_t a[2] = {GetDlgItem(hdlg, 0x416), 0xffff};
      (void)call26(0x1f42, a, 2);
    }
    (void)SendMessage(GetDlgItem(hdlg, 0x416), 0x0407, 0, 0);
    goto refresh;
  }
  case 0x42e:
  case 0x42f: {
    const uint16_t step = (id == 0x42f) ? 1 : 0xffff;
    {
      const uint16_t a[1] = {1};
      (void)call26(0x0810, a, 1);
    }
    if ((int16_t)GetKeyState(0x10) >= 0) {
      (void)CALL_ORIG(9, 0x2ed8, FAR_CDECL, 2, step, 0);
    } else {
      uint16_t r = (uint16_t)CALL_ORIG(9, 0x3082, FAR_CDECL, 2, drd16(CURSOR_PLANET), id == 0x42f ? 1 : 0);
      (void)CALL_ORIG(9, 0x2ed8, FAR_CDECL, 2, 0, r);
    }
    {
      const uint16_t a[1] = {0};
      (void)call26(0x0100, a, 1);
    }
    {
      const uint16_t a[1] = {hdlg};
      (void)call26(0x1a40, a, 1);
    }
    GetClientRect(hdlg, bpfp(-0x1e));
    L16(-0x1c) = drd16(0x4b6e);
    L16(-0x18) = (uint16_t)(L16(-0x1c) + (uint16_t)(9 * drd16(0x267a)));
    InvalidateRect(hdlg, bpfp(-0x1e), 1);
    goto done;
  }
  case 0x439: {
    uint32_t sel = SendMessage(GetDlgItem(hdlg, 0x417), 0x0409, 0, 0);
    if ((int16_t)(sel >> 16) < 0) goto done;
    if ((int16_t)(sel >> 16) == 0 && (uint16_t)sel <= 1) goto done;
    uint16_t above = (uint16_t)((uint16_t)sel - 2);
    uint16_t at = (uint16_t)(list_off() + (uint16_t)(above << 2));
    uint32_t moved = rd32(list_seg(), (uint16_t)(at + 4));
    wr32(list_seg(), (uint16_t)(at + 4), rd32(list_seg(), (uint16_t)(at + 8)));
    wr32(list_seg(), (uint16_t)(list_off() + (uint16_t)(above << 2) + 8), moved);
    production_set_queue_list(hdlg);
    (void)SendMessage(GetDlgItem(hdlg, 0x417), 0x0407, (uint16_t)(above + 1), 0);
    goto refresh_selection;
  }
  case 0x43a: {
    uint32_t sel = SendMessage(GetDlgItem(hdlg, 0x417), 0x0409, 0, 0);
    uint16_t cnt = list_count();
    if ((int16_t)(sel >> 16) < 0) goto done;
    if ((int16_t)(sel >> 16) == 0 && (uint16_t)sel == 0) goto done;
    if ((int32_t)cnt < (int32_t)sel) goto done;
    if ((int32_t)cnt == (int32_t)sel) goto done;
    uint16_t below = (uint16_t)((uint16_t)sel - 1);
    uint16_t at = (uint16_t)(list_off() + (uint16_t)(below << 2));
    uint32_t moved = rd32(list_seg(), (uint16_t)(at + 4));
    wr32(list_seg(), (uint16_t)(at + 4), rd32(list_seg(), (uint16_t)(at + 8)));
    wr32(list_seg(), (uint16_t)(list_off() + (uint16_t)(below << 2) + 8), moved);
    production_set_queue_list(hdlg);
    (void)SendMessage(GetDlgItem(hdlg, 0x417), 0x0407, (uint16_t)(below + 2), 0);
    goto refresh_selection;
  }
  default:
    goto done;
  }
refresh_selection:
refresh:
  production_refresh_panel(hdlg);
done:
  FRAME_LEAVE();
}

/* Copies the string at DS:src to DS:dst doubling every '&' (a menu accelerator mark), then the NUL. */
static void copy_doubling_ampersands(uint16_t dst, uint16_t src) {
  while (drd8(src) != 0) {
    uint8_t c = drd8(src);
    src++;
    dwr8(dst++, c);
    if (c == '&') dwr8(dst++, c);
  }
  dwr8(dst, 0);
}
/* _wsprintf(DS:NAME_BUF, string `id` of 8:0x8df4, one word `value`): the word is pushed before the call of
 * 8:0x8df4 and stays on the stack as the variable argument. */
static void format_name_with_number(uint16_t id, uint16_t value) {
  const uint16_t sp0 = rt_sp;
  pushw(value);
  uint16_t fmt = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, id);
  (void)_wsprintf(dsfp(NAME_BUF), dsfp(fmt), MK_FP(rt_ss, (uint16_t)(sp0 - 2)));
  rt_sp = sp0;
}
#define ZIP_PRESET(i) FAR_REC(struct ZipPreset, MEM_SEL_DGROUP, (uint16_t)(DS_ZIP_PRESETS + (uint16_t)((i) * 0x28)))
#define ZIP_SELECTED 0x51b2 /* DS:0x51b2: the preset the ZIP production dialog shows */

/* ZIPPRODDLG: 26:0x2d26 (Ghidra ZIPPRODDLG, alias fn_26_2d26), 1725 bytes. FAR PASCAL (hwnd [BP+0xE], msg [BP+0xC],
 * wParam [BP+0xA], lParam [BP+6]), ENTER 0x40,0; PUSH DI; PUSH SI. The dialog procedure of the ZIP production dialog
 * (template 0x59, run from 26:0x08e2): four saved production queues ("presets", struct ZipPreset at DS:0x2576, the
 * radio buttons 0x431 to 0x434) whose contents 26:0x3428 lists in the list 0x417. Returns AX:
 *   WM_PAINT 0x0f       a frame around the radio buttons, the heading (string 0x4b0) and, for a preset in use, its kind text
 *                       (string 0x4b5 + kind)
 *   WM_ERASEBKGND 0x14  fills the client rectangle with the brush DS:0x10
 *   WM_CTLCOLOR 0x19    the four radio buttons get the dialog colour
 *   WM_INITDIALOG 0x110 title (string 0x22c), sizes the dialog under the list, selects preset 0, writes the names
 *                       (an unused preset is "Preset n", string 0x4ad) onto the radio buttons
 *   WM_COMMAND 0x111    radio buttons 0x431 to 0x434 select a preset (DS:0x51b2); OK / Cancel (1, 2) end the dialog (DS:0xd08 =
 *                       0xffff); rename (0x816 from the save button, 0x41b) asks for a name with the dialog 16:0x063e (template
 *                       0x7e3; preset 0 cannot be renamed) and, for 0x816, stores the auto-build items of the
 *                       edited queue DS:0xcd2 (class 1, index below 7; at most 12) and the research check box of
 *                       DS:0x4c22 in the preset; reset (0x817) clears the preset and gives it the name "Preset n";
 *                       help (0x76, topic 0x452)
 * DS:0x794 bit 5 is set after the rename and reset commands, and 31:0x61b0 is called at the end of OK / Cancel and
 * INITDIALOG when bit 3 of DS:0x791 is set. */
DECOMP(26, 0x2d26, ZIPPRODDLG, FAR_PASCAL)
uint16_t ZIPPRODDLG(uint16_t hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam)
{
  FRAME_ENTER_REGS(0x40, 2);
  uint16_t ret = 0;
  switch (msg) {
  case 0x000f: { /* WM_PAINT */
    L16(-0xa) = BeginPaint(hwnd, bpfp(-0x2a));
    GetClientRect(hwnd, bpfp(-8));
    GetWindowRect(GetDlgItem(hwnd, 0x431), bpfp(-0x3a));
    ScreenToClient(hwnd, bpfp(-0x3a));
    GetWindowRect(GetDlgItem(hwnd, 0x434), bpfp(-8));
    ScreenToClient(hwnd, bpfp(-4));
    L16(-0x36) = L16(-4);
    L16(-0x34) = L16(-2);
    (void)CALL_ORIG(8, 0x96e6, FAR_CDECL, 3, bpoff(-0x3a), drd16(0x267a), (uint16_t)((int16_t)drd16(0x267a) >> 1));
    (void)CALL_ORIG(8, 0x9958, FAR_CDECL, 3, L16(-0xa), bpoff(-0x3a), 0xffff);
    (void)SelectObject(L16(-0xa), drd16(0x2930));
    (void)SetBkColor(L16(-0xa), drd32(0x253a));
    L16(-0x40) = (uint16_t)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, 0x04b0, NAME_BUF);
    (void)TextOut(L16(-0xa), (int16_t)(L16(-0x3a) + 8), (int16_t)(L16(-0x38) - (uint16_t)((int16_t)drd16(0x267a) >> 1)),
                  dsfp(NAME_BUF), (int16_t)L16(-0x40));
    L16(-0x38) = (uint16_t)(L16(-0x34) + 8);
    if (ZIP_PRESET(drd16(ZIP_SELECTED))->in_use != 0) {
      L16(-0x40) = (uint16_t)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2,
                                       (uint16_t)(ZIP_PRESET(drd16(ZIP_SELECTED))->kind + 0x4b5), NAME_BUF);
      (void)TextOut(L16(-0xa), (int16_t)L16(-0x3a), (int16_t)drd16(0x0d08), dsfp(NAME_BUF), (int16_t)L16(-0x40));
    }
    EndPaint(hwnd, bpfp(-0x2a));
    ret = 1;
    break;
  }
  case 0x0014: /* WM_ERASEBKGND */
    GetClientRect(hwnd, bpfp(-8));
    (void)FillRect(wParam, bpfp(-8), g_brush_btnface);
    ret = 1;
    break;
  case 0x0019: { /* WM_CTLCOLOR */
    L16(-0x2c) = 0x431;
    for (;;) {
      if ((int16_t)L16(-0x2c) > 0x434) break;
      if (GetDlgItem(hwnd, L16(-0x2c)) == (uint16_t)lParam) break;
      L16(-0x2c) = (uint16_t)(L16(-0x2c) + 1);
    }
    if ((int16_t)L16(-0x2c) <= 0x434) {
      (void)SetBkColor(wParam, drd32(0x253a));
      ret = g_brush_btnface;
    }
    break;
  }
  case 0x0110: { /* WM_INITDIALOG */
    {
      const uint16_t sp0 = rt_sp;
      rt_sp = (uint16_t)(sp0 - 2);
      uint16_t title = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x022c);
      rt_sp = sp0;
      SetWindowText(hwnd, dsfp(title));
    }
    GetWindowRect(hwnd, bpfp(-8));
    GetClientRect(hwnd, bpfp(-0x3a));
    L16(-0x3c) = (uint16_t)(L16(-2) - L16(-0x34) - L16(-6));
    GetWindowRect(GetDlgItem(hwnd, 0x417), bpfp(-0x3a));
    MapWindowPoints(0, hwnd, bpfp(-0x3a), 2);
    dwr16(0x0d08, (uint16_t)(L16(-0x34) + 2));
    L16(-0x3c) = (uint16_t)(L16(-0x3c) + (uint16_t)(L16(-0x34) + drd16(0x267a) + 6));
    (void)SetWindowPos(hwnd, 0, 0, 0, (int16_t)(L16(-4) - L16(-8)), (int16_t)L16(-0x3c), 6);
    (void)CheckRadioButton(hwnd, 0x431, 0x434, 0x431);
    {
      const uint16_t a[2] = {hwnd, 0};
      (void)call26(0x33e4, a, 2);
    }
    dwr16(ZIP_SELECTED, 0);
    {
      const uint16_t a[2] = {hwnd, (uint16_t)(drd16(ZIP_SELECTED) * 0x28 + DS_ZIP_PRESETS)};
      (void)call26(0x3428, a, 2);
    }
    for (L16(-0x2c) = 0x431; (int16_t)L16(-0x2c) <= 0x434; L16(-0x2c) = (uint16_t)(L16(-0x2c) + 1)) {
      uint16_t n = (uint16_t)(L16(-0x2c) - 0x431);
      if (ZIP_PRESET(n)->in_use == 0) {
        format_name_with_number(0x04ad, (uint16_t)(n + 1));
      } else {
        copy_doubling_ampersands(NAME_BUF, (uint16_t)(DS_ZIP_PRESETS + n * 0x28));
      }
      SetWindowText(GetDlgItem(hwnd, L16(-0x2c)), dsfp(NAME_BUF));
    }
    (void)CALL_ORIG(8, 0x97a2, FAR_CDECL, 3, hwnd, 0x0d04, 1);
    if ((drd8(0x0791) & 8) != 0) (void)CALL_ORIG(31, 0x61b0, FAR_CDECL, 0);
    ret = 1;
    break;
  }
  case 0x0111: { /* WM_COMMAND */
    const uint16_t id = wParam;
    const uint16_t code = (uint16_t)(lParam >> 16);
    if (code == 0 && id >= 0x431 && id <= 0x434) {
      dwr16(ZIP_SELECTED, (uint16_t)(id - 0x431));
      {
        const uint16_t a[2] = {hwnd, drd16(ZIP_SELECTED)};
        (void)call26(0x33e4, a, 2);
      }
      {
        const uint16_t a[2] = {hwnd, (uint16_t)(drd16(ZIP_SELECTED) * 0x28 + DS_ZIP_PRESETS)};
        (void)call26(0x3428, a, 2);
      }
      break;
    }
    if (id == 1 || id == 2) {
      (void)CALL_ORIG(8, 0x97a2, FAR_CDECL, 3, hwnd, 0x0d04, 0);
      (void)EndDialog(hwnd, id == 1 ? 1 : 0);
      dwr16(0x0d08, 0xffff);
      if ((drd8(0x0791) & 8) != 0) (void)CALL_ORIG(31, 0x61b0, FAR_CDECL, 0);
      ret = 1;
      break;
    }
    if (id == 0x816 || id == 0x41b) {
      if (ZIP_PRESET(drd16(ZIP_SELECTED))->in_use != 0) {
        copy_string(MEM_SEL_DGROUP, NAME_BUF, MEM_SEL_DGROUP, (uint16_t)(drd16(ZIP_SELECTED) * 0x28 + DS_ZIP_PRESETS));
      } else {
        format_name_with_number(0x04ae, drd16(ZIP_SELECTED));
      }
      uint32_t proc = MakeProcInstance(MK_FP(SEG_SEL(16), 0x063e), g_app_instance);
      L16(-0x3a) = (uint16_t)proc;
      L16(-0x38) = (uint16_t)(proc >> 16);
      if (drd16(ZIP_SELECTED) != 0) {
        if (API_SP(12, DialogBox(g_app_instance, 0x000007e3, g_main_window, proc)) == 0) goto rename_done;
        if (drd8(NAME_BUF) == 0) format_name_with_number(0x04ae, drd16(ZIP_SELECTED));
        copy_string(MEM_SEL_DGROUP, (uint16_t)(drd16(ZIP_SELECTED) * 0x28 + DS_ZIP_PRESETS), MEM_SEL_DGROUP, NAME_BUF);
        copy_doubling_ampersands(0x58a6, NAME_BUF);
        SetWindowText(GetDlgItem(hwnd, (uint16_t)(drd16(ZIP_SELECTED) + 0x431)), dsfp(0x58a6));
      }
      if (id == 0x816) {
        ZIP_PRESET(drd16(ZIP_SELECTED))->in_use = 1;
        uint16_t stored = 0;
        for (int16_t k = 0; (int16_t)list_count() > k; k++) {
          const struct ProdItem *e = FAR_REC(struct ProdItem, list_seg(), (uint16_t)(list_off() + (uint16_t)(k << 2) + 4));
          if ((e->w1 & PQ_CLASS_MASK) == (PQ_CLASS_ITEM << 1) &&
              ((((uint32_t)e->w1 << 16) | e->w0) & PRODITEM_ITEM_DWORD_MASK) < 0x1c00) {
            struct ZipPreset *z = ZIP_PRESET(drd16(ZIP_SELECTED));
            z->items[stored] = (uint16_t)((uint16_t)(e->w0 << 6) | ((e->w0 >> 10) & 0x3f)); /* quantity << 6 | item index */
            stored++;
            if ((int16_t)stored >= 12) break;
          }
        }
        ZIP_PRESET(drd16(ZIP_SELECTED))->count = (uint8_t)stored;
        ZIP_PRESET(drd16(ZIP_SELECTED))->kind = (uint8_t)((drd16(0x4c22) >> 7) & 1);
        const uint16_t a[2] = {hwnd, (uint16_t)(drd16(ZIP_SELECTED) * 0x28 + DS_ZIP_PRESETS)};
        (void)call26(0x3428, a, 2);
      }
      {
        const uint16_t a[2] = {hwnd, drd16(ZIP_SELECTED)};
        (void)call26(0x33e4, a, 2);
      }
    rename_done:
      FreeProcInstance(L32(-0x3a));
      (void)SetFocus(hwnd);
      dwr8(0x0794, (uint8_t)(drd8(0x0794) | 0x20));
      break;
    }
    if (id == 0x817) {
      ZIP_PRESET(drd16(ZIP_SELECTED))->in_use = 0;
      format_name_with_number(0x04ad, (uint16_t)(drd16(ZIP_SELECTED) + 1));
      SetWindowText(GetDlgItem(hwnd, (uint16_t)(drd16(ZIP_SELECTED) + 0x431)), dsfp(NAME_BUF));
      {
        const uint16_t a[2] = {hwnd, (uint16_t)(drd16(ZIP_SELECTED) * 0x28 + DS_ZIP_PRESETS)};
        (void)call26(0x3428, a, 2);
      }
      dwr8(0x0794, (uint8_t)(drd8(0x0794) | 0x20));
      break;
    }
    if (id == 0x76) {
      (void)WinHelp(hwnd, dsfp(drd16(0x010a)), 1, 0x00000452);
      ret = 1;
    }
    break;
  }
  default:
    break;
  }
  FRAME_LEAVE();
  return ret;
}

/* KERNEL part 1 (task stars-0z4.2): every call goes through the harness thunk of
 * ("KERNEL", ordinal), so the dispatcher registration, the argument decoding (PASCAL word
 * order) and the result registers are all under test. Run from the repo root. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "harness/harness.h"
#include "harness/kernel_thunks.h"
#include "runtime/include/win16.h"
#include "runtime/kernel.h"
#include "runtime/mem.h"
#include "test.h"

#define EXE "original/stars26i.exe"
#define HINST MEM_SEL_DGROUP

#define HI(fp) ((uint16_t)((uint32_t)(fp) >> 16))
#define LO(fp) ((uint16_t)((uint32_t)(fp) & 0xFFFFu))

/* ---- calling helpers ---- */

static uint32_t thunk_for(uint16_t ord, const char *name, int kind, uint16_t argbytes, int ret) {
  uint32_t fp = harness_thunk_far("KERNEL", ord);
  if (!fp) fp = harness_add_import("KERNEL", ord, name, kind, argbytes, ret, NULL);
  return fp;
}

/* PASCAL call of KERNEL.ord; args in push order. Returns DX:AX. */
static uint32_t kcall(uint16_t ord, const char *name, uint16_t argbytes, int ret,
                      const uint16_t *a, int n) {
  uint32_t fp = thunk_for(ord, name, HK_PASCAL, argbytes, ret);
  uint32_t r;
  CHECK(fp != 0);
  r = harness_call_far(fp, HCONV_PASCAL, a, n);
  CHECK_EQ(harness_last_error(), 0);
  return r;
}

static uint16_t g_alloc_h[64];
static int g_nalloc;

/* A global block holding a copy of s (NUL included). */
static uint32_t far_str(const char *s) {
  uint16_t h = GlobalAlloc(GMEM_MOVEABLE, strlen(s) + 1 + 64);
  uint32_t fp = GlobalLock(h);
  CHECK(h != 0 && fp != 0);
  g_alloc_h[g_nalloc++] = h;
  memcpy(LIN(fp), s, strlen(s) + 1);
  return fp;
}

/* ---- independent NE resource reader (does not use runtime/ne.c) ---- */

static uint16_t r16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t r32(const uint8_t *p) { return (uint32_t)r16(p) | ((uint32_t)r16(p + 2) << 16); }

/* Finds a resource by NE type word and name (string) or id word (name == NULL).
 * Returns the file offset and size, or 0. */
static uint32_t raw_find(const uint8_t *img, uint16_t type_word, const char *name, uint16_t id_word,
                         uint32_t *size) {
  const uint8_t *ne = img + r32(img + 0x3C);
  const uint8_t *tab = ne + r16(ne + 0x24);
  unsigned shift = r16(tab);
  const uint8_t *p = tab + 2;
  while (r16(p) != 0) {
    uint16_t t = r16(p), cnt = r16(p + 2);
    const uint8_t *e = p + 8;
    for (uint16_t i = 0; i < cnt; i++, e += 12) {
      uint16_t nid = r16(e + 6);
      int hit;
      if (t != type_word) continue;
      if (name) {
        const uint8_t *s = tab + nid;
        hit = !(nid & 0x8000) && s[0] == strlen(name) && memcmp(s + 1, name, s[0]) == 0;
      } else {
        hit = nid == id_word;
      }
      if (hit) {
        *size = (uint32_t)r16(e + 2) << shift;
        return (uint32_t)r16(e) << shift;
      }
    }
    p = e;
  }
  return 0;
}

/* ---- tests ---- */

static void test_start_and_version(void) {
  Regs16 io;
  uint32_t fp = thunk_for(91, "InitTask", HK_REGISTER, 0, HR_NONE);
  uint32_t v;
  memset(&io, 0, sizeof io);
  io.si = 0x1234; io.cx = 0x5555;
  harness_call(FP_SEG(fp), FP_OFF(fp), HCONV_PASCAL, NULL, 0, &io);
  CHECK_EQ(harness_last_error(), 0);
  CHECK_EQ(io.ax, 1);
  CHECK_EQ(io.di, 0x0118);
  CHECK_EQ(io.cx, 16384); /* stack size in the NE header */
  CHECK_EQ(io.si, 0);
  CHECK_EQ(io.dx, 1);     /* SW_SHOWNORMAL */
  /* Empty command tail: Wine's InitTask16 returns BX = 0x80 (the length byte, 0), which reads
   * as an empty C string. The C runtime builds argv from PSP:0x81 itself. */
  CHECK_EQ(io.bx, 0x80);
  CHECK_EQ(strlen((const char *)LIN(MK_FP(io.es, io.bx))), 0);
  CHECK_EQ(io.es, kernel_psp_selector());
  CHECK(io.es != 0 && desc_limit(io.es) >= 0xFF);
  CHECK_EQ(*(uint8_t *)LIN(MK_FP(io.es, 0x80)), 0); /* empty command tail */
  CHECK_EQ(*(uint8_t *)LIN(MK_FP(io.es, 0x81)), 0x0D);
  CHECK_EQ(*(uint16_t *)LIN(MK_FP(io.es, 0)), 0x20CD); /* PSP starts with INT 20h */

  v = kcall(3, "GetVersion", 0, HR_RET32, NULL, 0);
  CHECK_EQ(v & 0xFFFF, 0x0A03);
  CHECK_EQ(v >> 16, 0x0616); /* DOS 6.22, unmeasured: see runtime/kernel.c */

  /* Yield and WaitEvent return normally with the stack balanced. WaitEvent returns FALSE
   * (Wine: TRUE only after it had to wait; the game ignores the value). */
  { uint16_t a[1] = {0}; CHECK_EQ(kcall(30, "WaitEvent", 2, HR_RET16, a, 1) & 0xFFFF, 0); }
  CHECK_EQ(kcall(29, "Yield", 0, HR_RET16, NULL, 0) & 0xFFFF, 0);
}

static void test_module(void) {
  uint32_t buf = far_str("");
  uint16_t a[4] = {HINST, HI(buf), LO(buf), 64};
  uint32_t n = kcall(49, "GetModuleFileName", 8, HR_RET16, a, 4) & 0xFFFF;
  CHECK_EQ(n, strlen("C:\\STARS\\STARS26I.EXE"));
  CHECK(strcmp((char *)LIN(buf), "C:\\STARS\\STARS26I.EXE") == 0);
  /* truncation: nSize 6 keeps 5 characters plus the NUL */
  a[3] = 6;
  n = kcall(49, "GetModuleFileName", 8, HR_RET16, a, 4) & 0xFFFF;
  CHECK_EQ(n, 5);
  CHECK(strcmp((char *)LIN(buf), "C:\\ST") == 0);
  a[0] = 0x0777; /* not a module we know */
  CHECK_EQ(kcall(49, "GetModuleFileName", 8, HR_RET16, a, 4) & 0xFFFF, 0);
  a[0] = 0; a[3] = 64; /* hModule 0 is the current task */
  n = kcall(49, "GetModuleFileName", 8, HR_RET16, a, 4) & 0xFFFF;
  CHECK_EQ(n, strlen("C:\\STARS\\STARS26I.EXE"));
  CHECK(strcmp((char *)LIN(buf), "C:\\STARS\\STARS26I.EXE") == 0);
  { /* the whole nSize buffer must fit in the segment, not just its first byte */
    uint16_t sh = GlobalAlloc(GMEM_MOVEABLE, 8);
    uint32_t sb = GlobalLock(sh);
    uint16_t sa[4] = {HINST, HI(sb), LO(sb), 64};
    CHECK(sh != 0 && sb != 0 && desc_limit(sh) < 63);
    memset(LIN(sb), 'Q', 8);
    CHECK_EQ(kcall(49, "GetModuleFileName", 8, HR_RET16, sa, 4) & 0xFFFF, 0);
    CHECK_EQ(*(uint8_t *)LIN(sb), 'Q'); /* nothing written */
    GlobalUnlock(sh);
    GlobalFree(sh);
  }

  { /* MakeProcInstance(lpProc, hInst) returns lpProc unchanged */
    uint32_t proc = MK_FP(0x0008, 0x1234);
    uint16_t m[3] = {HI(proc), LO(proc), HINST};
    uint16_t f[2] = {HI(proc), LO(proc)};
    CHECK_EQ(kcall(51, "MakeProcInstance", 6, HR_RET32, m, 3), proc);
    kcall(52, "FreeProcInstance", 4, HR_RET16, f, 2);
  }
}

static void test_global(void) {
  uint16_t h, a[3];
  uint32_t fp, sz;
  uint16_t arg;
  /* GlobalAlloc(GHND, 100): flags, then the size high word first */
  a[0] = GHND; a[1] = 0; a[2] = 100;
  h = (uint16_t)kcall(15, "GlobalAlloc", 6, HR_RET16, a, 3);
  CHECK(h != 0);
  arg = h;
  sz = kcall(20, "GlobalSize", 2, HR_RET32, &arg, 1);
  CHECK(sz >= 100);
  fp = kcall(18, "GlobalLock", 2, HR_RET32, &arg, 1);
  CHECK_EQ(fp, MK_FP(h, 0));
  CHECK_EQ(*(uint8_t *)LIN(fp + 99), 0); /* zero-initialised */
  *(uint8_t *)LIN(fp) = 0xA5;
  CHECK_EQ(kcall(19, "GlobalUnlock", 2, HR_RET16, &arg, 1) & 0xFFFF, 0);
  { /* GlobalReAlloc(h, 4000, GMEM_ZEROINIT): contents kept */
    uint16_t r[4] = {h, 0, 4000, GMEM_ZEROINIT};
    uint16_t h2 = (uint16_t)kcall(16, "GlobalReAlloc", 8, HR_RET16, r, 4);
    CHECK_EQ(h2, h);
    CHECK(kcall(20, "GlobalSize", 2, HR_RET32, &arg, 1) >= 4000);
    fp = GlobalLock(h);
    CHECK_EQ(*(uint8_t *)LIN(fp), 0xA5);
    GlobalUnlock(h);
  }
  /* LockSegment / UnlockSegment move the lock count */
  CHECK_EQ(kcall(23, "LockSegment", 2, HR_RET16, &arg, 1) & 0xFFFF, h);
  CHECK(GlobalUnlock(h) == false); /* count 1 -> 0 */
  kcall(23, "LockSegment", 2, HR_RET16, &arg, 1);
  kcall(23, "LockSegment", 2, HR_RET16, &arg, 1);
  CHECK(kcall(24, "UnlockSegment", 2, HR_RET16, &arg, 1) != 0); /* count 2 -> 1: still locked */
  CHECK_EQ(kcall(24, "UnlockSegment", 2, HR_RET16, &arg, 1) & 0xFFFF, 0); /* 1 -> 0 */
  CHECK_EQ(kcall(17, "GlobalFree", 2, HR_RET16, &arg, 1) & 0xFFFF, 0);
  CHECK_EQ(GlobalSize(h), 0);
}

/* Plain functions called directly (no thunk, no harness decoding). */
static void test_direct(void) {
  KInitTaskRegs r;
  uint16_t h = GlobalAlloc(GMEM_MOVEABLE, 64), l;
  CHECK(h != 0);
  /* LockSegment(-1) with DS = DGROUP: DGROUP is fixed, returns it without a count or a crash */
  CHECK_EQ(LockSegment(0xFFFF, 0x0118), 0x0118);
  CHECK_EQ(LockSegment(0xFFFF, h), h); /* -1 means the caller's DS: a global block here */
  CHECK_EQ(LockSegment(h, 0x0118), h); /* count 2 */
  CHECK(UnlockSegment(0xFFFF, h) != 0); /* 2 -> 1: still locked */
  CHECK_EQ(UnlockSegment(0xFFFF, h), 0); /* 1 -> 0 */
  CHECK_EQ(UnlockSegment(0xFFFF, h), 0); /* already unlocked: no underflow */
  GlobalFree(h);
  InitTask(&r);
  CHECK_EQ(r.bx, 0x80);
  CHECK_EQ(r.es, kernel_psp_selector());
  l = kernel_LocalAlloc(LMEM_NODISCARD | LMEM_ZEROINIT, 16);
  CHECK(l != 0 && *(uint8_t *)LINN(l + 15) == 0);
  CHECK_EQ(LocalFree(l), 0);
  CHECK_EQ(WaitEvent(0), 0);
}

static void test_local(void) {
  uint16_t a[2], r[3], h, arg;
  mem_local_init(harness_stack_top(), (uint16_t)(harness_stack_top() + 0x800));
  a[0] = LMEM_ZEROINIT; a[1] = 32;
  h = (uint16_t)kcall(5, "LocalAlloc", 4, HR_RET16, a, 2);
  CHECK(h != 0);
  arg = h;
  CHECK(kcall(10, "LocalSize", 2, HR_RET16, &arg, 1) >= 32);
  CHECK_EQ(*(uint8_t *)LINN(h + 31), 0);
  *(uint8_t *)LINN(h) = 0x5A;
  r[0] = h; r[1] = 100; r[2] = LMEM_ZEROINIT;
  h = (uint16_t)kcall(6, "LocalReAlloc", 6, HR_RET16, r, 3);
  CHECK(h != 0);
  CHECK_EQ(*(uint8_t *)LINN(h), 0x5A);
  arg = h;
  CHECK(kcall(10, "LocalSize", 2, HR_RET16, &arg, 1) >= 100);
  CHECK_EQ(kcall(7, "LocalFree", 2, HR_RET16, &arg, 1) & 0xFFFF, 0);

  /* The game's C runtime: LocalAlloc(LMEM_NODISCARD) at 34:2cea and
   * LocalReAlloc(h, n, MOVEABLE|NODISCARD|ZEROINIT) at 34:2d60. */
  a[0] = 0x20; a[1] = 24;
  h = (uint16_t)kcall(5, "LocalAlloc", 4, HR_RET16, a, 2);
  CHECK(h != 0);
  memset(LINN(h), 0x77, 24);
  r[0] = h; r[1] = 200; r[2] = 0x62;
  h = (uint16_t)kcall(6, "LocalReAlloc", 6, HR_RET16, r, 3);
  CHECK(h != 0);
  CHECK_EQ(*(uint8_t *)LINN(h), 0x77);
  CHECK_EQ(*(uint8_t *)LINN(h + 23), 0x77);
  CHECK_EQ(*(uint8_t *)LINN(h + 199), 0); /* ZEROINIT of the grown part */
  arg = h;
  CHECK_EQ(kcall(7, "LocalFree", 2, HR_RET16, &arg, 1) & 0xFFFF, 0);
  a[0] = 0x10; a[1] = 8; /* LMEM_NOCOMPACT */
  h = (uint16_t)kcall(5, "LocalAlloc", 4, HR_RET16, a, 2);
  CHECK(h != 0);
  arg = h;
  kcall(7, "LocalFree", 2, HR_RET16, &arg, 1);

  { /* real discardable allocation still aborts (unsupported, not silently ignored) */
    pid_t pid;
    int st = 0;
    fflush(NULL);
    pid = fork();
    if (pid == 0) {
      uint16_t d[2] = {LMEM_DISCARDABLE, 8};
      freopen("/dev/null", "w", stderr);
      kcall(5, "LocalAlloc", 4, HR_RET16, d, 2);
      _exit(0); /* reached only if the call did not abort */
    }
    CHECK(pid > 0 && waitpid(pid, &st, 0) == pid);
    CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGABRT);
  }
}

static void test_strings(void) {
  uint32_t s = far_str("Stars!"), d1 = far_str("xxxxxxxxxxxxxxxx"), d2 = far_str("");
  uint16_t la[2] = {HI(s), LO(s)};
  uint16_t cp[4];
  CHECK(FP_SEG(s) != FP_SEG(d1) && FP_SEG(d1) != FP_SEG(d2));
  CHECK_EQ(kcall(90, "lstrlen", 4, HR_RET16, la, 2) & 0xFFFF, 6);
  { uint32_t e = far_str(""); uint16_t ea[2] = {HI(e), LO(e)};
    CHECK_EQ(kcall(90, "lstrlen", 4, HR_RET16, ea, 2) & 0xFFFF, 0); }
  { uint16_t na[2] = {0, 0}; CHECK_EQ(kcall(90, "lstrlen", 4, HR_RET16, na, 2) & 0xFFFF, 0); }
  cp[0] = HI(d1); cp[1] = LO(d1); cp[2] = HI(s); cp[3] = LO(s);
  CHECK_EQ(kcall(88, "lstrcpy", 8, HR_RET32, cp, 4), d1);
  CHECK(strcmp((char *)LIN(d1), "Stars!") == 0);
  CHECK_EQ(*((char *)LIN(d1) + 7), 'x'); /* only the string and its NUL were written */
  cp[0] = HI(d1); cp[1] = LO(d1); cp[2] = HI(s); cp[3] = LO(s);
  CHECK_EQ(kcall(89, "lstrcat", 8, HR_RET32, cp, 4), d1);
  CHECK(strcmp((char *)LIN(d1), "Stars!Stars!") == 0);
  cp[0] = HI(d2); cp[1] = LO(d2); /* lstrcat onto an empty buffer */
  kcall(89, "lstrcat", 8, HR_RET32, cp, 4);
  CHECK(strcmp((char *)LIN(d2), "Stars!") == 0);
}

static const uint8_t *g_img;
static size_t g_img_len;

static void test_resources(void) {
  uint32_t nm = far_str("STARSMENU"), lc = far_str("stARsMeNu");
  uint16_t fr[5] = {HINST, HI(nm), LO(nm), 0, RT_MENU};
  uint16_t hres, hdata;
  uint32_t size, off, rsize, p;
  uint16_t sa[2], ra[2];

  off = raw_find(g_img, 0x8000 | RT_MENU, "STARSMENU", 0, &rsize);
  CHECK(off != 0 && rsize > 0);

  hres = (uint16_t)kcall(60, "FindResource", 10, HR_RET16, fr, 5);
  CHECK(hres != 0);
  fr[1] = HI(lc); fr[2] = LO(lc); /* resource names are case-insensitive */
  CHECK_EQ(kcall(60, "FindResource", 10, HR_RET16, fr, 5) & 0xFFFF, hres);

  sa[0] = HINST; sa[1] = hres;
  size = kcall(65, "SizeofResource", 4, HR_RET32, sa, 2);
  CHECK(size >= rsize);

  ra[0] = HINST; ra[1] = hres;
  hdata = (uint16_t)kcall(61, "LoadResource", 4, HR_RET16, ra, 2);
  CHECK(hdata != 0);
  p = kcall(62, "LockResource", 2, HR_RET32, &hdata, 1);
  CHECK(p != 0);
  /* bytes equal the raw resource in the file (independent reader, not runtime/ne.c) */
  CHECK(off + rsize <= g_img_len);
  CHECK_MEM(LIN(p), g_img + off, rsize);
  CHECK_EQ(kcall(63, "FreeResource", 2, HR_RET16, &hdata, 1) & 0xFFFF, 0);

  { /* dialog 90 exists (integer ids), 9999 does not */
    uint32_t doff, dsz;
    uint16_t d[5] = {HINST, 0, 90, 0, RT_DIALOG};
    uint16_t hd = (uint16_t)kcall(60, "FindResource", 10, HR_RET16, d, 5);
    CHECK(hd != 0);
    doff = raw_find(g_img, 0x8000 | RT_DIALOG, NULL, 0x8000 | 90, &dsz);
    CHECK(doff != 0);
    ra[1] = hd;
    hdata = (uint16_t)kcall(61, "LoadResource", 4, HR_RET16, ra, 2);
    p = kcall(62, "LockResource", 2, HR_RET32, &hdata, 1);
    CHECK(p != 0 && dsz > 0);
    CHECK_MEM(LIN(p), g_img + doff, dsz);
    kcall(63, "FreeResource", 2, HR_RET16, &hdata, 1);
    d[2] = 9999;
    CHECK_EQ(kcall(60, "FindResource", 10, HR_RET16, d, 5) & 0xFFFF, 0);
    d[2] = 90; d[4] = RT_BITMAP; /* wrong type */
    CHECK_EQ(kcall(60, "FindResource", 10, HR_RET16, d, 5) & 0xFFFF, 0);
    d[0] = 0x0777; d[4] = RT_DIALOG; /* wrong module */
    CHECK_EQ(kcall(60, "FindResource", 10, HR_RET16, d, 5) & 0xFFFF, 0);
  }
  { /* "#90" names an integer id */
    uint32_t hs = far_str("#90");
    uint16_t d[5] = {HINST, HI(hs), LO(hs), 0, RT_DIALOG};
    CHECK(kcall(60, "FindResource", 10, HR_RET16, d, 5) != 0);
  }
  { /* AllocResource(hInst, hRsrc, 0) sizes the block like the resource */
    uint16_t al[4] = {HINST, hres, 0, 0};
    uint16_t hb = (uint16_t)kcall(66, "AllocResource", 8, HR_RET16, al, 4);
    CHECK(hb != 0);
    CHECK(GlobalSize(hb) >= rsize);
    kcall(63, "FreeResource", 2, HR_RET16, &hb, 1);
    CHECK_EQ(GlobalSize(hb), 0);
    al[2] = 0; al[3] = 300; /* explicit size */
    hb = (uint16_t)kcall(66, "AllocResource", 8, HR_RET16, al, 4);
    CHECK(GlobalSize(hb) >= 300);
    GlobalFree(hb);
  }
}

static void test_misc(void) {
  uint32_t env = kcall(131, "GetDOSEnvironment", 0, HR_RET32, NULL, 0);
  const char *e = LIN(env);
  size_t i = 0, total = 0;
  int saw_path = 0;
  CHECK(env != 0);
  while (e[i]) {
    if (strncmp(e + i, "PATH=", 5) == 0) saw_path = 1;
    i += strlen(e + i) + 1;
  }
  total = i + 1;
  CHECK(saw_path);
  CHECK_EQ(e[total - 1], 0);
  CHECK_EQ(e[total - 2], 0); /* ends with two NULs */

  { uint16_t d = 2; CHECK_EQ(kcall(136, "GetDriveType", 2, HR_RET16, &d, 1) & 0xFFFF, DRIVE_FIXED); }
  { uint16_t d = 0; CHECK_EQ(kcall(136, "GetDriveType", 2, HR_RET16, &d, 1) & 0xFFFF, DRIVE_REMOVABLE); }
  { uint16_t d = 9; CHECK_EQ(kcall(136, "GetDriveType", 2, HR_RET16, &d, 1) & 0xFFFF, DRIVE_UNKNOWN); }
}

static char g_fatal_msg[512];
static int g_fatal_calls;
static void fatal_hook(const char *m) {
  g_fatal_calls++;
  snprintf(g_fatal_msg, sizeof g_fatal_msg, "%s", m);
}

/* Runs synthetic code: MOV AX,0x1111; PUSH each word; CALL FAR thunk; MOV AX,0x2222; RETF.
 * AX == 0x2222 afterwards means the API returned into the caller. *sp_at_entry gets the
 * SP expected inside the handler (stack top minus the pushed words and the far return). */
static uint16_t stub_sel(uint32_t thunk, const uint16_t *w, int n) {
  uint8_t code[64];
  size_t k = 0;
  uint16_t sel;
  code[k++] = 0xB8; code[k++] = 0x11; code[k++] = 0x11;
  for (int i = 0; i < n; i++) { code[k++] = 0x68; code[k++] = (uint8_t)w[i]; code[k++] = (uint8_t)(w[i] >> 8); }
  code[k++] = 0x9A;
  code[k++] = (uint8_t)thunk; code[k++] = (uint8_t)(thunk >> 8);
  code[k++] = (uint8_t)(thunk >> 16); code[k++] = (uint8_t)(thunk >> 24);
  code[k++] = 0xB8; code[k++] = 0x22; code[k++] = 0x22;
  code[k++] = 0xCB;
  sel = harness_alloc_code(code, k);
  CHECK(sel != 0);
  return sel;
}

static Regs16 run_stub(uint32_t thunk, const uint16_t *w, int n, uint16_t *sp_in_handler) {
  Regs16 io;
  uint16_t sel = stub_sel(thunk, w, n);
  memset(&io, 0, sizeof io);
  harness_call(sel, 0, HCONV_CDECL, NULL, 0, &io);
  if (sp_in_handler) *sp_in_handler = (uint16_t)(harness_stack_top() - 4 - 2 * n - 4);
  return io;
}

static void test_fatal(void) {
  uint32_t msg = far_str("Out of stuff");
  uint16_t fa[3] = {0, HI(msg), LO(msg)};
  uint16_t code = 0x1234, sp_h;
  Regs16 io;
  uint32_t fx = thunk_for(1, "FatalExit", HK_STUB, 0, HR_NONE);
  uint32_t fe = thunk_for(137, "FatalAppExit", HK_PASCAL, 6, HR_RET16);
  kernel_fatal_hook = fatal_hook;

  /* FatalAppExit: the hook runs, the run stops, the game never sees the return. */
  io = run_stub(fe, fa, 3, &sp_h);
  CHECK_EQ(g_fatal_calls, 1);
  CHECK(strstr(g_fatal_msg, "Out of stuff") != NULL);
  CHECK(kernel_fatal_pending);
  CHECK(io.ax != 0x2222);      /* the MOV AX,0x2222 after the call did not run (AX is the RET16 slot) */
  CHECK(harness_last_error() != 0);
  /* FatalExit (STUB entry, argbytes 0, but takes one word): the adapter decodes it from the
   * stack and leaves SP exactly as at entry (nothing popped; the run is over). */
  io = run_stub(fx, &code, 1, &sp_h);
  CHECK_EQ(g_fatal_calls, 2);
  CHECK(strstr(g_fatal_msg, "1234") != NULL);
  CHECK_EQ(io.ax, 0x1111);
  CHECK_EQ(io.sp, sp_h);
  CHECK(harness_last_error() != 0);

  { /* the message copy is bounded: 300 characters give at most 256 */
    uint16_t h = GlobalAlloc(GMEM_MOVEABLE, 400);
    uint32_t p = GlobalLock(h);
    uint16_t a2[3];
    CHECK(h != 0 && p != 0);
    memset(LIN(p), 'A', 300);
    *((char *)LIN(p) + 300) = 0;
    a2[0] = 0; a2[1] = HI(p); a2[2] = LO(p);
    run_stub(fe, a2, 3, NULL);
    CHECK_EQ(strlen(g_fatal_msg), strlen("FatalAppExit: ") + 256);
    GlobalUnlock(h);
    GlobalFree(h);
  }
  { /* an unterminated string stops at the segment limit; the neighbouring block is not read */
    uint16_t ha = GlobalAlloc(GMEM_MOVEABLE, 16), hb = GlobalAlloc(GMEM_MOVEABLE, 16);
    uint32_t pa = GlobalLock(ha), pb = GlobalLock(hb);
    uint16_t a2[3];
    CHECK(ha && hb && pa && pb);
    memset(LIN(pa), 'A', (size_t)desc_limit(ha) + 1);
    memset(LIN(pb), 'Z', (size_t)desc_limit(hb) + 1);
    a2[0] = 0; a2[1] = HI(pa); a2[2] = LO(pa);
    run_stub(fe, a2, 3, NULL);
    CHECK(strchr(g_fatal_msg, 'Z') == NULL);
    CHECK_EQ(strlen(g_fatal_msg), strlen("FatalAppExit: ") + (size_t)desc_limit(ha) + 1);
    GlobalUnlock(ha); GlobalUnlock(hb);
    GlobalFree(ha); GlobalFree(hb);
  }
  kernel_fatal_hook = NULL;
}

/* A fatal call inside a NESTED run (an API handler calling game code through harness_call)
 * must abort the whole run: the outer game code never continues, and the depth-0 call
 * reports an error. */
static uint16_t g_inner_sel;
static void h_outer(HCall *c) {
  (void)c;
  harness_call(g_inner_sel, 0, HCONV_CDECL, NULL, 0, NULL);
}

static void test_fatal_nested(void) {
  uint32_t msg = far_str("Nested fatal");
  uint16_t fa[3] = {0, HI(msg), LO(msg)};
  uint32_t fe = thunk_for(137, "FatalAppExit", HK_PASCAL, 6, HR_RET16);
  uint32_t outer = harness_add_import("TESTMOD", 9, "OuterFatal", HK_PASCAL, 0, HR_NONE, h_outer);
  Regs16 io;
  kernel_fatal_hook = fatal_hook;
  g_fatal_calls = 0;
  g_inner_sel = stub_sel(fe, fa, 3);
  io = run_stub(outer, NULL, 0, NULL);
  CHECK_EQ(g_fatal_calls, 1);
  CHECK(io.ax != 0x2222);              /* the outer code after the API call did not run */
  CHECK(harness_last_error() != 0);
  CHECK_EQ(harness_depth(), 0);
  kernel_fatal_hook = NULL;
}

int main(void) {
  FILE *fp;
  long len;
  uint8_t *buf;

  fp = fopen(EXE, "rb");
  if (!fp) { fprintf(stderr, "cannot open %s (run from the repo root)\n", EXE); return 1; }
  fseek(fp, 0, SEEK_END); len = ftell(fp); fseek(fp, 0, SEEK_SET);
  buf = malloc((size_t)len);
  if (!buf || fread(buf, 1, (size_t)len, fp) != (size_t)len) return 1;
  fclose(fp);
  g_img = buf; g_img_len = (size_t)len;

  harness_set_quiet(true);
  if (harness_load(EXE) != 0) return 1;
  if (kernel_load(buf, (size_t)len) != 0) { fprintf(stderr, "kernel_load failed\n"); return 1; }
  kernel_register();

  test_start_and_version();
  test_module();
  test_global();
  test_local();
  test_direct();
  test_strings();
  test_resources();
  test_misc();
  test_fatal();
  test_fatal_nested();

  kernel_shutdown();
  harness_shutdown();
  free(buf);
  return TEST_DONE();
}

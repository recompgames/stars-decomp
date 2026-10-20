/* KERNEL part 1 (task stars-0z4.2). Behaviour from the Windows 3.1 SDK; Wine's dlls/krnl386
 * as reference. Plain C, no harness dependency: the PASCAL stack decoding lives in
 * harness/kernel_thunks.c. */
#include "runtime/kernel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/include/win16.h"
#include "runtime/mem.h"
#include "runtime/ne.h"
#include "runtime/vfs.h"

void (*kernel_fatal_hook)(const char *msg);
int kernel_fatal_pending;
int kernel_exit_pending;
int kernel_exit_code;

/* Values fixed at kernel_load; the one piece that changes while running (resource load
 * state) lives in the arena at g_res_off. */
static struct {
  uint8_t *image;
  size_t image_len;
  NeFile ne;
  int loaded;
  uint16_t hinst;
  uint16_t psp, env;
  uint32_t res_off; /* arena offset of ResState[ne.n_resources] */
} g;

typedef struct {
  uint16_t h;    /* global handle from LoadResource, 0 when not loaded */
  uint16_t refs; /* LoadResource calls not yet matched by FreeResource */
} ResState;

#define MODULE_PATH "C:\\STARS\\STARS26I.EXE"
/* Recorded choice: the DOS version of the reference environment has not been measured,
 * so GetVersion's high word is DOS 6.22 (major 6, minor 22) as the task specifies. */
#define DOS_VERSION_WORD KERNEL_DOS_VERSION_WORD
#define DOS_ENV "PATH=C:\\WINDOWS\0COMSPEC=C:\\COMMAND.COM\0"

static ResState *res_state(void) { return (ResState *)(mem_arena() + g.res_off); }

/* ---- start-up ---- */

void InitTask(KInitTaskRegs *r) {
  const uint8_t *psp = g.psp ? mem_arena() + desc_base(g.psp) : NULL;
  r->ax = 1;
  /* Wine: BX = 0x80 when the command line is empty (the length byte, which is 0 and reads as
   * an empty C string); the C runtime's argv builder reads PSP:0x81 itself. */
  r->bx = (psp && psp[0x80]) ? 0x81 : 0x80;
  r->cx = g.ne.hdr.stack_size;
  r->dx = 1;
  r->si = 0;
  r->di = g.hinst;
  r->es = g.psp;
}

/* One task and no event queue: nothing to wait for. Wine returns FALSE when the event was
 * already signalled and TRUE after waiting; the game ignores the value. */
uint16_t WaitEvent(uint16_t hTask) {
  (void)hTask;
  return 0;
}
void Yield(void) {}
uint32_t GetVersion(void) { return ((uint32_t)DOS_VERSION_WORD << 16) | W16_WINVER; }

/* ---- module ---- */

/* Copies at most nSize - 1 characters; the whole nSize buffer must be addressable. */
uint16_t GetModuleFileName(HANDLE16 hModule, FARPTR buf, uint16_t size) {
  size_t n = strlen(MODULE_PATH);
  if ((hModule != 0 && hModule != g.hinst) || !buf || size == 0) return 0;
  if (!mem_check(buf, size)) return 0;
  if (n > (size_t)size - 1) n = (size_t)size - 1;
  memcpy(LIN(buf), MODULE_PATH, n);
  *((char *)LIN(buf) + n) = 0;
  return (uint16_t)n;
}

/* Exported procedures do not load DS and callbacks always run with DS = SS = DGROUP, so no
 * prolog thunk is needed. */
FARPTR MakeProcInstance(FARPTR lpProc, HANDLE16 hInstance) {
  (void)hInstance;
  return lpProc;
}
void FreeProcInstance(FARPTR lpProc) { (void)lpProc; }

/* ---- global and local memory ---- */

/* mem.c implements a fixed local heap: NOCOMPACT/NODISCARD only say "do not discard other
 * blocks to satisfy this", and MOVEABLE on LocalReAlloc says "may move", which mem.c does
 * already. LMEM_DISCARDABLE and the rest still reach mem.c and abort. */
uint16_t kernel_LocalAlloc(uint16_t flags, uint16_t bytes) {
  return LocalAlloc((uint16_t)(flags & ~(LMEM_NOCOMPACT | LMEM_NODISCARD)), bytes);
}
uint16_t kernel_LocalReAlloc(uint16_t hMem, uint16_t bytes, uint16_t flags) {
  return LocalReAlloc(hMem, bytes, (uint16_t)(flags & ~(LMEM_NOCOMPACT | LMEM_NODISCARD | LMEM_MOVEABLE)));
}

HANDLE16 LockSegment(uint16_t seg, uint16_t ds) {
  if (seg == 0xFFFF) seg = ds;
  if (seg & ~7u) GlobalLock(seg);
  return (seg & ~7u) ? seg : 0;
}
uint16_t UnlockSegment(uint16_t seg, uint16_t ds) {
  if (seg == 0xFFFF) seg = ds;
  return (GlobalSize(seg) && GlobalUnlock(seg)) ? 1 : 0;
}

/* ---- resources ---- */

static int ieq(const char *a, const char *b) {
  for (; *a && *b; a++, b++) {
    int x = *a, y = *b;
    if (x >= 'a' && x <= 'z') x -= 32;
    if (y >= 'a' && y <= 'z') y -= 32;
    if (x != y) return 0;
  }
  return *a == *b;
}

/* A resource name or type argument: MAKEINTRESOURCE (high word 0), "#123" or a string. */
static int parse_res_arg(FARPTR p, uint16_t *num, const char **str) {
  const char *s;
  if (FP_SEG(p) == 0) {
    *num = FP_OFF(p);
    *str = NULL;
    return 1;
  }
  s = LIN(p);
  if (s[0] == '#') {
    unsigned long v = 0;
    const char *d = s + 1;
    if (*d >= '0' && *d <= '9') {
      for (; *d >= '0' && *d <= '9'; d++) v = v * 10 + (unsigned long)(*d - '0');
      if (!*d && v <= 0xFFFF) {
        *num = (uint16_t)v;
        *str = NULL;
        return 1;
      }
    }
  }
  *str = s;
  return 1;
}

/* hRsrc = 1-based index into the NE resource table; 0 is "not found". */
static const NeResource *res_by_handle(uint16_t hinst, uint16_t hrsrc) {
  if (!g.loaded || hinst != g.hinst || hrsrc == 0 || hrsrc > g.ne.n_resources) return NULL;
  return &g.ne.resources[hrsrc - 1];
}

HANDLE16 FindResource(HANDLE16 hinst, FARPTR name, FARPTR type) {
  uint16_t tnum = 0, nnum = 0;
  const char *tstr, *nstr;
  if (!g.loaded || hinst != g.hinst || !type || !name) return 0;
  parse_res_arg(type, &tnum, &tstr);
  parse_res_arg(name, &nnum, &nstr);
  for (size_t i = 0; i < g.ne.n_resources; i++) {
    const NeResource *r = &g.ne.resources[i];
    if (tstr ? !(r->type_name && ieq(r->type_name, tstr)) : (r->type_name || r->type != tnum))
      continue;
    if (nstr ? !(r->name && ieq(r->name, nstr)) : (r->name || r->id != nnum)) continue;
    return (HANDLE16)(i + 1);
  }
  return 0;
}

uint32_t SizeofResource(HANDLE16 hinst, HANDLE16 hrsrc) {
  const NeResource *r = res_by_handle(hinst, hrsrc);
  return r ? r->size : 0;
}

/* LoadResource: the first call copies the resource into a movable global block; later
 * calls return the same block and count a reference. */
HANDLE16 LoadResource(HANDLE16 hinst, HANDLE16 hrsrc) {
  const NeResource *r = res_by_handle(hinst, hrsrc);
  ResState *st;
  if (!r) return 0;
  if (r->offset > g.image_len || r->size > g.image_len - r->offset) return 0;
  st = &res_state()[hrsrc - 1];
  if (!st->h) {
    uint16_t h = GlobalAlloc(GMEM_MOVEABLE, r->size ? r->size : 1);
    if (!h) return 0;
    memcpy(mem_arena() + desc_base(h), g.image + r->offset, r->size);
    st->h = h;
    st->refs = 0;
  }
  st->refs++;
  return st->h;
}

/* LockResource returns a far pointer to the loaded copy. */
FARPTR LockResource(HANDLE16 hResData) { return GlobalLock(hResData); }

/* FreeResource: drops one reference of a LoadResource block and frees it at zero; any other
 * block (AllocResource) is freed at once. Returns 0 on success. */
uint16_t FreeResource(HANDLE16 h) {
  ResState *st = g.loaded ? res_state() : NULL;
  for (size_t i = 0; st && i < g.ne.n_resources; i++) {
    if (st[i].h == h && h) {
      if (--st[i].refs == 0) {
        st[i].h = 0;
        return GlobalFree(h) ? 1 : 0;
      }
      return 0;
    }
  }
  return GlobalFree(h) ? 1 : 0;
}

/* AllocResource(hInst, hRsrc, dwSize): an empty (zeroed) movable block; dwSize 0 means the
 * size of the resource. */
HANDLE16 AllocResource(HANDLE16 hinst, HANDLE16 hrsrc, uint32_t size) {
  const NeResource *r = res_by_handle(hinst, hrsrc);
  if (!size && r) size = r->size;
  if (!size) return 0;
  return GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
}

HFILE16 AccessResource(HANDLE16 hinst, HANDLE16 hrsrc) {
  const NeResource *r = res_by_handle(hinst, hrsrc);
  uint16_t h;
  uint32_t pos;
  if (!r) return HFILE_ERROR;
  if (vfs_open(MODULE_PATH, VFS_READ, 0, &h)) return HFILE_ERROR;
  if (vfs_seek(h, (int32_t)r->offset, 0, &pos)) {
    vfs_close(h);
    return HFILE_ERROR;
  }
  return (HFILE16)h;
}

/* ---- strings (far pointers) ---- */

static uint32_t far_len(FARPTR p) {
  uint32_t n = 0;
  while (*(const char *)LIN(MK_FP(FP_SEG(p), (uint16_t)(FP_OFF(p) + n)))) n++;
  return n;
}

uint16_t lstrlen(FARPTR p) { return p ? far_len(p) & 0xFFFF : 0; }

FARPTR lstrcpy(FARPTR dst, FARPTR src) {
  if (!src || !dst) return 0;
  /* byte by byte, so a segment limit is checked at every byte */
  for (uint32_t i = 0;; i++) {
    char ch = *(const char *)LIN(MK_FP(FP_SEG(src), (uint16_t)(FP_OFF(src) + i)));
    *(char *)LIN(MK_FP(FP_SEG(dst), (uint16_t)(FP_OFF(dst) + i))) = ch;
    if (!ch) break;
  }
  return dst;
}

FARPTR lstrcat(FARPTR dst, FARPTR src) {
  uint32_t n;
  if (!src || !dst) return 0;
  n = far_len(dst);
  for (uint32_t i = 0;; i++) {
    char ch = *(const char *)LIN(MK_FP(FP_SEG(src), (uint16_t)(FP_OFF(src) + i)));
    *(char *)LIN(MK_FP(FP_SEG(dst), (uint16_t)(FP_OFF(dst) + n + i))) = ch;
    if (!ch) break;
  }
  return dst;
}

/* ---- misc ---- */

FARPTR GetDOSEnvironment(void) { return g.env ? MK_FP(g.env, 0) : 0; }

/* Drive 0 = A:, 1 = B: (removable), 2 = C: (fixed); the rest are not present. */
uint16_t GetDriveType(uint16_t d) {
  return d < 2 ? DRIVE_REMOVABLE : d == 2 ? DRIVE_FIXED : DRIVE_UNKNOWN;
}

/* After the hook returns the game must not continue: kernel_fatal_pending is sticky and the
 * host adapter stops the program. */
void kernel_fatal(const char *msg) {
  if (kernel_fatal_hook) {
    kernel_fatal_hook(msg);
    kernel_fatal_pending = 1;
    return;
  }
  fprintf(stderr, "%s\n", msg);
  abort();
}

void FatalExit(int16_t code) {
  char msg[64];
  snprintf(msg, sizeof msg, "FatalExit code 0x%04X", (unsigned)(uint16_t)code);
  kernel_fatal(msg);
}

void FatalAppExit(uint16_t uAction, FARPTR text) {
  static const char prefix[] = "FatalAppExit: ";
  char msg[sizeof prefix + 256];
  size_t n = sizeof prefix - 1;
  (void)uAction;
  memcpy(msg, prefix, n);
  /* bounded far-string copy: stops at NUL, at 256 characters, or at the segment limit */
  for (unsigned i = 0; text && i < 256; i++) {
    FARPTR p = MK_FP(FP_SEG(text), (uint16_t)(FP_OFF(text) + i));
    char ch;
    if (!mem_check(p, 1)) break;
    ch = *(const char *)LIN(p);
    if (!ch) break;
    msg[n++] = ch;
  }
  msg[n] = 0;
  kernel_fatal(msg);
}

uint16_t kernel_psp_selector(void) { return g.psp; }

/* Everything kernel_shutdown does except freeing the VFS (kernel_load keeps host-supplied files). */
static void unload(void) {
  kernel_fatal_pending = 0;
  kernel_exit_pending = 0;
  kernel_exit_code = 0;
  if (g.loaded && g.res_off && mem_arena()) rt_free(mem_arena() + g.res_off);
  ne_free(&g.ne);
  free(g.image);
  memset(&g, 0, sizeof g);
}

int kernel_load(const uint8_t *image, size_t len) {
  uint8_t *psp;
  char *env;
  unload();
  g.image = malloc(len ? len : 1);
  if (!g.image) return -1;
  memcpy(g.image, image, len);
  g.image_len = len;
  if (ne_parse(g.image, len, &g.ne) != NE_OK) {
    unload();
    return -2;
  }
  g.loaded = 1;
  /* the .exe is visible in the VFS at the path GetModuleFileName returns (AccessResource) */
  /* the VFS is not reset: files, clock, DTA and vectors the host set up before loading stay;
   * only the .exe is (re)written at its path. The state is created on first use. */
  if (vfs_put(MODULE_PATH, image, len)) {
    unload();
    return -3;
  }
  g.hinst = (uint16_t)(g.ne.hdr.auto_data_seg << 3);

  g.env = GlobalAlloc(GPTR, 256);
  g.psp = GlobalAlloc(GPTR, 256);
  if (g.ne.n_resources) {
    ResState *st = rt_alloc(g.ne.n_resources * sizeof(ResState));
    if (!st) {
      unload();
      return -3;
    }
    memset(st, 0, g.ne.n_resources * sizeof(ResState));
    g.res_off = mem_off(st);
  }
  if (!g.env || !g.psp) {
    unload();
    return -3;
  }
  env = (char *)mem_arena() + desc_base(g.env);
  memcpy(env, DOS_ENV, sizeof DOS_ENV); /* includes the NUL ending COMSPEC; the array adds the final NUL */
  psp = mem_arena() + desc_base(g.psp);
  psp[0] = 0xCD; /* INT 20h */
  psp[1] = 0x20;
  psp[0x2C] = (uint8_t)(g.env & 0xFF);
  psp[0x2D] = (uint8_t)(g.env >> 8);
  psp[0x80] = 0; /* empty command tail */
  psp[0x81] = 0x0D;
  return 0;
}

void kernel_shutdown(void) {
  unload();
  vfs_shutdown();
}



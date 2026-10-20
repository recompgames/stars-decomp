/* KERNEL part 2 (task stars-0z4.3): OpenFile, _lread/_lwrite/_lclose, DOS3Call, the private
 * profile functions and AccessResource, every call through the harness thunk of
 * ("KERNEL", ordinal) so the dispatcher registration, argument decoding and result registers
 * are under test. Run from the repo root. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/harness.h"
#include "harness/kernel_thunks.h"
#include "runtime/include/win16.h"
#include "runtime/kernel.h"
#include "runtime/kernel_file.h"
#include "runtime/mem.h"
#include "runtime/vfs.h"
#include "test.h"

#define EXE "original/stars26i.exe"
#define HINST MEM_SEL_DGROUP
#define DGROUP MEM_SEL_DGROUP

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

static uint16_t g_alloc_h[512];
static int g_nalloc;

static uint32_t far_alloc(size_t n) {
  uint16_t h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, n);
  uint32_t fp = GlobalLock(h);
  CHECK(h != 0 && fp != 0);
  g_alloc_h[g_nalloc++] = h;
  return fp;
}
static uint32_t far_str(const char *s) {
  uint32_t fp = far_alloc(strlen(s) + 1 + 16);
  memcpy(LIN(fp), s, strlen(s) + 1);
  return fp;
}

/* DGROUP scratch (the DOS3Call thunk runs with DS = DGROUP, so DS:DX buffers live there). */
static void reset_local(void) { mem_local_init(harness_stack_top(), (uint16_t)(harness_stack_top() + 0x800)); }
static uint16_t near_alloc(size_t n) {
  uint16_t o = kernel_LocalAlloc(LMEM_ZEROINIT, (uint16_t)n);
  CHECK(o != 0);
  return o;
}
static uint16_t near_str(const char *s) {
  uint16_t o = near_alloc(strlen(s) + 1);
  memcpy(LINN(o), s, strlen(s) + 1);
  return o;
}

/* ---- INT 21h through the DOS3Call thunk ---- */

/* CF is preset to 1 so that a success path has to clear it. */
static Regs16 R(uint8_t ah, uint8_t al) {
  Regs16 r;
  memset(&r, 0, sizeof r);
  r.ax = (uint16_t)(ah << 8 | al);
  r.flags = 1;
  return r;
}
static int dos(Regs16 *io) {
  uint32_t fp = thunk_for(102, "DOS3Call", HK_REGISTER, 0, HR_NONE);
  CHECK(fp != 0);
  harness_call(FP_SEG(fp), FP_OFF(fp), HCONV_PASCAL, NULL, 0, io);
  CHECK_EQ(harness_last_error(), 0);
  return io->flags & 1;
}
/* A call that must succeed / must fail with a DOS error. */
static void dos_ok(Regs16 *io) { CHECK_EQ(dos(io), 0); }
static void dos_err(Regs16 *io, uint16_t err) {
  CHECK_EQ(dos(io), 1);
  CHECK_EQ(io->ax, err);
}
static Regs16 path_call(uint8_t ah, uint8_t al, const char *path) {
  Regs16 r = R(ah, al);
  r.ds = DGROUP;
  r.dx = near_str(path);
  return r;
}

static HFILE16 open_file(const char *name, uint16_t style, uint32_t ofs) {
  uint32_t n = far_str(name);
  uint16_t a[5] = {HI(n), LO(n), HI(ofs), LO(ofs), style};
  return (HFILE16)kcall(74, "OpenFile", 10, HR_RET16, a, 5);
}
static uint16_t l_close(HFILE16 h) {
  uint16_t a = (uint16_t)h;
  return (uint16_t)kcall(81, "_lclose", 2, HR_RET16, &a, 1);
}
static uint16_t l_read(HFILE16 h, uint32_t buf, uint16_t n) {
  uint16_t a[4] = {(uint16_t)h, HI(buf), LO(buf), n};
  return (uint16_t)kcall(82, "_lread", 8, HR_RET16, a, 4);
}
static uint16_t l_write(HFILE16 h, uint32_t buf, uint16_t n) {
  uint16_t a[4] = {(uint16_t)h, HI(buf), LO(buf), n};
  return (uint16_t)kcall(86, "_lwrite", 8, HR_RET16, a, 4);
}

static uint16_t ofs_err(uint32_t ofs) { return *(uint16_t *)LIN(ofs + OFS_NERRCODE); }
static const char *ofs_path(uint32_t ofs) { return (const char *)LIN(ofs + OFS_PATHNAME); }

/* ---- OpenFile and the _l functions ---- */

static void test_openfile(void) {
  uint32_t ofs = far_alloc(OFSTRUCT_SIZE);
  uint32_t buf = far_alloc(64);
  HFILE16 h, h2;
  const uint8_t *d = NULL;
  size_t n = 0;
  int rc = 0;

  /* create, write 10 bytes, close; reopen read-only and read the same bytes back */
  h = open_file("C:\\STARS\\TEST.HST", OF_CREATE, ofs);
  CHECK(h >= 5);
  CHECK_EQ(*(uint8_t *)LIN(ofs + OFS_CBYTES), OFSTRUCT_SIZE);
  CHECK_EQ(*(uint8_t *)LIN(ofs + OFS_FFIXEDDISK), 1);
  CHECK_EQ(ofs_err(ofs), 0);
  CHECK(strcmp(ofs_path(ofs), "C:\\STARS\\TEST.HST") == 0);
  memcpy(LIN(buf), "ABCDEFGHIJ", 10);
  CHECK_EQ(l_write(h, buf, 10), 10);
  CHECK_EQ(l_close(h), 0);
  CHECK_EQ(l_close(h), 0xFFFF); /* HFILE_ERROR */
  CHECK_EQ(vfs_get("C:\\STARS\\TEST.HST", &d, &n), 0);
  CHECK_EQ(n, 10);

  memset(LIN(ofs), 0xEE, OFSTRUCT_SIZE);
  h = open_file("C:\\STARS\\TEST.HST", OF_READ, ofs);
  CHECK(h >= 5);
  CHECK(strcmp(ofs_path(ofs), "C:\\STARS\\TEST.HST") == 0);
  CHECK_EQ(ofs_err(ofs), 0);
  memset(LIN(buf), 0, 64);
  CHECK_EQ(l_read(h, buf, 64), 10);
  CHECK_MEM(LIN(buf), "ABCDEFGHIJ", 10);
  CHECK_EQ(l_read(h, buf, 64), 0); /* at the end */
  CHECK_EQ(l_write(h, buf, 1), 0xFFFF); /* opened read-only */
  CHECK_EQ(l_close(h), 0);

  /* a missing file: HFILE_ERROR and DOS error 2, with the path still filled in */
  h = open_file("missing.xy", OF_READ, ofs);
  CHECK_EQ(h, HFILE_ERROR);
  CHECK_EQ(ofs_err(ofs), 2);
  CHECK(strcmp(ofs_path(ofs), "C:\\STARS\\MISSING.XY") == 0);
  CHECK_EQ(open_file("missing.xy", OF_WRITE, ofs), HFILE_ERROR); /* write does not create */
  CHECK_EQ(ofs_err(ofs), 2);
  CHECK_EQ(open_file("C:\\NOPE\\X.TXT", OF_READ, ofs), HFILE_ERROR);
  CHECK_EQ(ofs_err(ofs), 3);
  CHECK_EQ(open_file("D:\\X.TXT", OF_READ, ofs), HFILE_ERROR);
  CHECK_EQ(ofs_err(ofs), 15);

  /* a relative name resolves against C:\STARS, and the spelling does not matter */
  { const char *names[] = {"test.hst", "Test.Hst", "c:\\stars\\test.hst", "C:/stars/test.hst",
                           "..\\stars\\TEST.HST", "\\stars\\test.hst"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
      h = open_file(names[i], OF_READ | OF_SHARE_DENY_NONE, ofs); /* OF_SHARE_* is ignored */
      CHECK(h >= 5);
      CHECK(strcmp(ofs_path(ofs), "C:\\STARS\\TEST.HST") == 0);
      CHECK_EQ(l_read(h, buf, 3), 3);
      CHECK_MEM(LIN(buf), "ABC", 3);
      l_close(h);
    }
  }

  /* two handles on one file have their own file pointers */
  h = open_file("test.hst", OF_READ, ofs);
  h2 = open_file("test.hst", OF_READWRITE, ofs);
  CHECK(h >= 5 && h2 >= 5 && h != h2);
  CHECK_EQ(l_read(h, buf, 4), 4);
  CHECK_EQ(l_read(h2, buf, 2), 2);
  CHECK_MEM(LIN(buf), "AB", 2);
  CHECK_EQ(l_read(h, buf, 2), 2);
  CHECK_MEM(LIN(buf), "EF", 2);
  memcpy(LIN(buf), "xy", 2);
  CHECK_EQ(l_write(h2, buf, 2), 2);
  l_close(h);
  l_close(h2);
  rc = vfs_get("TEST.HST", &d, &n);
  CHECK_EQ(rc, 0);
  if (rc == 0) CHECK_MEM(d, "ABxyEFGHIJ", 10);

  /* OF_CREATE truncates an existing file */
  h = open_file("test.hst", OF_CREATE | OF_SHARE_EXCLUSIVE, ofs);
  CHECK(h >= 5);
  l_close(h);
  CHECK_EQ(vfs_get("TEST.HST", &d, &n), 0);
  CHECK_EQ(n, 0);

  /* OF_PARSE: only fills the structure */
  memset(LIN(ofs), 0, OFSTRUCT_SIZE);
  CHECK_EQ(open_file("parse.me", OF_PARSE, ofs), 0);
  CHECK(strcmp(ofs_path(ofs), "C:\\STARS\\PARSE.ME") == 0);
  CHECK_EQ(vfs_get("PARSE.ME", &d, &n), -1);

  /* OF_EXIST. UNVERIFIED: returning 1 is from memory; the game is believed never to use this
   * flag. Only success vs HFILE_ERROR is asserted. */
  CHECK(open_file("test.hst", OF_EXIST, ofs) != HFILE_ERROR);
  CHECK_EQ(open_file("nothere.1", OF_EXIST, ofs), HFILE_ERROR);
  CHECK_EQ(ofs_err(ofs), 2);

  /* OF_REOPEN uses the path stored in the OFSTRUCT */
  open_file("test.hst", OF_PARSE, ofs);
  h = open_file("ignored.name", OF_REOPEN | OF_READ, ofs);
  CHECK(h >= 5);
  CHECK(strcmp(ofs_path(ofs), "C:\\STARS\\TEST.HST") == 0);
  l_close(h);

  /* OF_DELETE. UNVERIFIED: success value from memory; the game is believed never to use this
   * flag. Only success vs HFILE_ERROR is asserted. */
  CHECK(open_file("test.hst", OF_DELETE, ofs) != HFILE_ERROR);
  CHECK_EQ(vfs_get("TEST.HST", &d, &n), -1);
  CHECK_EQ(open_file("test.hst", OF_DELETE, ofs), HFILE_ERROR);
  CHECK_EQ(ofs_err(ofs), 2);

  /* handle numbering: the lowest free number from 5 */
  h = open_file("h1.tmp", OF_CREATE, ofs);
  h2 = open_file("h2.tmp", OF_CREATE, ofs);
  CHECK_EQ(h, 5);
  CHECK_EQ(h2, 6);
  l_close(h);
  CHECK_EQ(open_file("h3.tmp", OF_CREATE, ofs), 5);
  CHECK_EQ(l_read(99, buf, 1), 0xFFFF);
  CHECK_EQ(l_write(99, buf, 1), 0xFFFF);
  l_close(5);
  l_close(6);
}

/* ---- DOS3Call, one test per AH value of docs/inventory.md section 5 (+ 2Ch) ---- */

static void test_dos_dta_and_vectors(void) { /* 1Ah, 2Fh, 25h, 35h */
  Regs16 r;
  uint16_t b1 = near_alloc(64);
  r = R(0x2F, 0);
  dos_ok(&r);
  CHECK_EQ(r.es, kernel_psp_selector()); /* default DTA: PSP:0080h */
  CHECK_EQ(r.bx, 0x80);
  r = R(0x1A, 0);
  r.ds = DGROUP;
  r.dx = b1;
  dos_ok(&r);
  r = R(0x2F, 0);
  r.es = 0;
  r.bx = 0;
  dos_ok(&r);
  CHECK_EQ(r.es, DGROUP);
  CHECK_EQ(r.bx, b1);

  r = R(0x35, 0x24);
  dos_ok(&r);
  CHECK_EQ(r.es, 0); /* never set */
  CHECK_EQ(r.bx, 0);
  r = R(0x25, 0x24);
  r.ds = DGROUP;
  r.dx = 0x1234;
  dos_ok(&r);
  r = R(0x35, 0x24);
  dos_ok(&r);
  CHECK_EQ(r.es, DGROUP);
  CHECK_EQ(r.bx, 0x1234);
  r = R(0x35, 0x23);
  dos_ok(&r);
  CHECK_EQ(r.bx, 0); /* a different vector */
}

static void test_dos_clock_version_disk(void) { /* 2Ah, 2Ch, 30h, 36h */
  Regs16 r;
  rt_clock_set(1995, 12, 25, 10, 20, 30);
  r = R(0x2A, 0);
  dos_ok(&r);
  CHECK_EQ(r.cx, 1995);
  CHECK_EQ(r.dx >> 8, 12);
  CHECK_EQ(r.dx & 0xFF, 25);
  CHECK_EQ(r.ax & 0xFF, 1); /* Monday */
  r = R(0x2C, 0);
  dos_ok(&r);
  CHECK_EQ(r.cx >> 8, 10);
  CHECK_EQ(r.cx & 0xFF, 20);
  CHECK_EQ(r.dx >> 8, 30);
  CHECK_EQ(r.dx & 0xFF, 0);
  rt_clock_set(1996, 2, 29, 23, 59, 58); /* the clock is the only source: it changes the answer */
  r = R(0x2A, 0);
  dos_ok(&r);
  CHECK_EQ(r.cx, 1996);
  CHECK_EQ(r.dx, (2 << 8) | 29);
  CHECK_EQ(r.ax & 0xFF, 4); /* Thursday */

  r = R(0x30, 0);
  dos_ok(&r);
  CHECK_EQ(r.ax & 0xFF, 6);  /* major, like GetVersion's 0x0616 */
  CHECK_EQ(r.ax >> 8, 22);   /* minor */
  CHECK_EQ(r.bx >> 8, 0xFF); /* OEM: MS-DOS */

  /* UNVERIFIED geometry: these 36h values need a reference capture. The game uses the total
   * size for a machine fingerprint (DS:0x564c), so they matter. Pinned as currently returned. */
  r = R(0x36, 0);
  r.dx = 3;
  dos_ok(&r);
  CHECK_EQ(r.ax, 32);
  CHECK_EQ(r.bx, 16384);
  CHECK_EQ(r.cx, 512);
  CHECK_EQ(r.dx, 32768);
  r = R(0x36, 0);
  r.dx = 0; /* default drive = C: */
  dos_ok(&r);
  CHECK_EQ(r.cx, 512);
  r = R(0x36, 0);
  r.dx = 4; /* D: does not exist: AX = FFFFh */
  dos(&r);
  CHECK_EQ(r.ax, 0xFFFF);
}

static void test_dos_dirs(void) { /* 39h, 3Ah, 3Bh */
  Regs16 r;
  const uint8_t *d;
  size_t n;
  uint8_t at;
  reset_local();
  r = path_call(0x39, 0, "NEWDIR");
  dos_ok(&r);
  CHECK_EQ(vfs_getattr("C:\\STARS\\NEWDIR", &at), 0);
  CHECK_EQ(at, VFS_ATTR_DIR);
  r = path_call(0x39, 0, "newdir");
  dos_err(&r, 5); /* exists */
  r = path_call(0x39, 0, "NOPE\\SUB");
  dos_err(&r, 3);
  r = path_call(0x3B, 0, "newdir");
  dos_ok(&r);
  CHECK_EQ(vfs_put("X.TXT", "x", 1), 0); /* relative to the new current directory */
  CHECK_EQ(vfs_get("C:\\STARS\\NEWDIR\\X.TXT", &d, &n), 0);
  r = path_call(0x3B, 0, "C:\\NOSUCH");
  dos_err(&r, 3);
  r = path_call(0x3A, 0, "C:\\STARS\\NEWDIR");
  dos_err(&r, 5); /* current directory */
  r = path_call(0x3B, 0, "..");
  dos_ok(&r);
  r = path_call(0x3A, 0, "NEWDIR");
  dos_err(&r, 5); /* not empty */
  r = path_call(0x41, 0, "NEWDIR\\X.TXT");
  dos_ok(&r);
  r = path_call(0x3A, 0, "NEWDIR");
  dos_ok(&r);
  r = path_call(0x3A, 0, "NEWDIR");
  dos_err(&r, 3);
  r = path_call(0x3B, 0, "\\STARS");
  dos_ok(&r);
}

static void test_dos_files(void) { /* 40h, 41h, 42h, 43h */
  Regs16 r;
  uint32_t ofs = far_alloc(OFSTRUCT_SIZE);
  HFILE16 h = open_file("dos.dat", OF_CREATE, ofs);
  uint16_t data;
  const uint8_t *d = NULL;
  size_t n = 0;
  int rc = 0;
  reset_local();
  data = near_str("0123456789ABCDEF");

  r = R(0x40, 0);
  r.bx = (uint16_t)h;
  r.cx = 16;
  r.ds = DGROUP;
  r.dx = data;
  dos_ok(&r);
  CHECK_EQ(r.ax, 16);
  r = R(0x40, 0);
  r.bx = 1; /* stdout: a device, the bytes are discarded but counted */
  r.cx = 5;
  r.ds = DGROUP;
  r.dx = data;
  dos_ok(&r);
  CHECK_EQ(r.ax, 5);
  r = R(0x40, 0);
  r.bx = 77; /* not open */
  r.cx = 1;
  r.ds = DGROUP;
  r.dx = data;
  dos_err(&r, 6);

  /* lseek: AL=2 from the end returns DX:AX = file size */
  r = R(0x42, 2);
  r.bx = (uint16_t)h;
  dos_ok(&r);
  CHECK_EQ(r.dx, 0);
  CHECK_EQ(r.ax, 16);
  r = R(0x42, 0); /* from the start */
  r.bx = (uint16_t)h;
  r.cx = 0;
  r.dx = 4;
  dos_ok(&r);
  CHECK_EQ(r.ax, 4);
  r = R(0x42, 1); /* from the current position, negative offset FFFF:FFFE = -2 */
  r.bx = (uint16_t)h;
  r.cx = 0xFFFF;
  r.dx = 0xFFFE;
  dos_ok(&r);
  CHECK_EQ(r.ax, 2);
  CHECK_EQ(r.dx, 0);
  r = R(0x42, 2); /* beyond the end with a 32-bit offset: 16 + 0x00010000 */
  r.bx = (uint16_t)h;
  r.cx = 1;
  r.dx = 0;
  dos_ok(&r);
  CHECK_EQ(r.dx, 1);
  CHECK_EQ(r.ax, 16);
  /* UNVERIFIED: error 25 for a seek before the start is a guess */
  r = R(0x42, 0); /* before the start */
  r.bx = (uint16_t)h;
  r.cx = 0xFFFF;
  r.dx = 0xFFFF;
  dos_err(&r, 25);
  r = R(0x42, 3);
  r.bx = (uint16_t)h;
  dos_err(&r, 1);
  r = R(0x42, 0);
  r.bx = 88;
  dos_err(&r, 6);

  /* write at a position after seeking */
  r = R(0x42, 0);
  r.bx = (uint16_t)h;
  r.dx = 2;
  dos_ok(&r);
  r = R(0x40, 0);
  r.bx = (uint16_t)h;
  r.cx = 3;
  r.ds = DGROUP;
  r.dx = data;
  dos_ok(&r);
  l_close(h);
  rc = vfs_get("DOS.DAT", &d, &n);
  CHECK_EQ(rc, 0);
  if (rc == 0) CHECK_MEM(d, "0101256789ABCDEF", 3 + 3);

  /* a read-only handle refuses 40h */
  h = open_file("dos.dat", OF_READ, ofs);
  r = R(0x40, 0);
  r.bx = (uint16_t)h;
  r.cx = 1;
  r.ds = DGROUP;
  r.dx = data;
  dos_err(&r, 5);
  l_close(h);

  /* 43h: AL=0 get, AL=1 set */
  r = path_call(0x43, 0, "dos.dat");
  dos_ok(&r);
  CHECK_EQ(r.cx, VFS_ATTR_ARCHIVE);
  r = path_call(0x43, 0, "C:\\WINDOWS");
  dos_ok(&r);
  CHECK_EQ(r.cx, VFS_ATTR_DIR);
  r = path_call(0x43, 1, "dos.dat");
  r.cx = VFS_ATTR_RO;
  dos_ok(&r);
  r = path_call(0x43, 0, "dos.dat");
  dos_ok(&r);
  CHECK_EQ(r.cx, VFS_ATTR_RO);
  r = path_call(0x41, 0, "dos.dat");
  dos_err(&r, 5); /* read-only */
  r = path_call(0x43, 1, "dos.dat");
  r.cx = 0;
  dos_ok(&r);
  r = path_call(0x43, 0, "nothere.zz");
  dos_err(&r, 2);
  r = path_call(0x43, 0, "NODIR\\X.ZZ");
  dos_err(&r, 3);
  r = path_call(0x43, 5, "dos.dat");
  dos_err(&r, 1);

  /* 41h */
  r = path_call(0x41, 0, "dos.dat");
  dos_ok(&r);
  CHECK_EQ(vfs_get("DOS.DAT", &d, &n), -1);
  r = path_call(0x41, 0, "dos.dat");
  dos_err(&r, 2);
  r = path_call(0x41, 0, "NODIR\\dos.dat");
  dos_err(&r, 3);
}

static void test_dos_rename(void) { /* 56h */
  Regs16 r;
  const uint8_t *d;
  size_t n;
  reset_local();
  CHECK_EQ(vfs_put("OLD.TXT", "o", 1), 0);
  CHECK_EQ(vfs_put("OTHER.TXT", "p", 1), 0);
  r = path_call(0x56, 0, "old.txt");
  r.es = DGROUP;
  r.di = near_str("new.txt");
  dos_ok(&r);
  CHECK_EQ(vfs_get("NEW.TXT", &d, &n), 0);
  CHECK_EQ(vfs_get("OLD.TXT", &d, &n), -1);
  r = path_call(0x56, 0, "old.txt");
  r.es = DGROUP;
  r.di = near_str("x.txt");
  dos_err(&r, 2);
  r = path_call(0x56, 0, "new.txt");
  r.es = DGROUP;
  r.di = near_str("other.txt");
  dos_err(&r, 5); /* target exists */
  r = path_call(0x56, 0, "new.txt");
  r.es = DGROUP;
  r.di = near_str("NOPE\\x.txt");
  dos_err(&r, 3);
  vfs_unlink("NEW.TXT");
  vfs_unlink("OTHER.TXT");
}

static void test_dos_find(void) { /* 4Eh, 4Fh */
  Regs16 r;
  uint16_t dta1, dta2;
  const uint8_t *p, *q;
  reset_local();
  dta1 = near_alloc(43);
  dta2 = near_alloc(43);
  CHECK_EQ(vfs_mkdir("FD"), 0);
  vfs_put("FD\\A.TXT", "1", 1);
  vfs_put("FD\\B.TXT", "22", 2);
  vfs_put("FD\\C.DAT", "333", 3);
  vfs_mkdir("FD\\SUB");
  p = LINN(dta1);
  q = LINN(dta2);

  r = R(0x1A, 0);
  r.ds = DGROUP;
  r.dx = dta1;
  dos_ok(&r);
  r = path_call(0x4E, 0, "fd\\*.txt");
  dos_ok(&r);
  CHECK(strcmp((const char *)p + 30, "A.TXT") == 0);
  CHECK_EQ(p[26], 1); /* size, little endian */
  CHECK_EQ(p[21], VFS_ATTR_ARCHIVE);

  /* a second search with its own DTA runs independently of the first */
  r = R(0x1A, 0);
  r.ds = DGROUP;
  r.dx = dta2;
  dos_ok(&r);
  r = path_call(0x4E, 0, "fd\\*.*");
  dos_ok(&r);
  CHECK(strcmp((const char *)q + 30, "A.TXT") == 0);
  r = R(0x4F, 0);
  dos_ok(&r);
  CHECK(strcmp((const char *)q + 30, "B.TXT") == 0);
  CHECK_EQ(q[26], 2);

  r = R(0x1A, 0);
  r.ds = DGROUP;
  r.dx = dta1;
  dos_ok(&r);
  r = R(0x4F, 0);
  dos_ok(&r);
  CHECK(strcmp((const char *)p + 30, "B.TXT") == 0);
  r = R(0x4F, 0);
  dos_err(&r, 18); /* no more files */

  r = R(0x1A, 0);
  r.ds = DGROUP;
  r.dx = dta2;
  dos_ok(&r);
  r = R(0x4F, 0);
  dos_ok(&r);
  CHECK(strcmp((const char *)q + 30, "C.DAT") == 0);
  r = R(0x4F, 0);
  dos_err(&r, 18);

  r = path_call(0x4E, 0, "fd\\*.zzz");
  dos_err(&r, 2);
  r = path_call(0x4E, 0, "nodir\\*.*");
  dos_err(&r, 3);
  r = path_call(0x4E, 0, "fd\\S*"); /* attribute mask includes directories */
  r.cx = 0x10;
  dos_ok(&r);
  CHECK(strcmp((const char *)q + 30, "SUB") == 0);
  CHECK_EQ(q[21], VFS_ATTR_DIR);
  r = path_call(0x4E, 0, "fd\\S*"); /* ... and without it they are skipped */
  r.cx = 0;
  dos_err(&r, 2);
  /* volume-label search (attribute exactly 08h): the VFS has no labels, so no ordinary file
   * is returned and the answer is "no more files" */
  vfs_put("C:\\VLTEST.TXT", "v", 1);
  r = path_call(0x4E, 0, "C:\\*.*");
  r.cx = 0x08;
  dos_err(&r, 18);
}

static void test_dos_exit(void) { /* 4Ch */
  Regs16 r = R(0x4C, 7);
  uint32_t fp = thunk_for(102, "DOS3Call", HK_REGISTER, 0, HR_NONE);
  CHECK_EQ(kernel_exit_pending, 0);
  harness_call(FP_SEG(fp), FP_OFF(fp), HCONV_PASCAL, NULL, 0, &r);
  CHECK(harness_last_error() != 0); /* the run was aborted: the program is over */
  CHECK_EQ(kernel_exit_pending, 1);
  CHECK_EQ(kernel_exit_code, 7);
  kernel_exit_pending = 0;
  kernel_exit_code = 0;
  r = R(0x30, 0); /* the harness is usable again */
  dos_ok(&r);
}

static char g_fatal_msg[128];
static void on_fatal(const char *m) { snprintf(g_fatal_msg, sizeof g_fatal_msg, "%s", m); }

static void test_dos_unlisted(void) {
  Regs16 r = R(0x3D, 0); /* open file: the game never calls it */
  uint32_t fp = thunk_for(102, "DOS3Call", HK_REGISTER, 0, HR_NONE);
  /* lenient: CF=1, AX=1 (invalid function) */
  dos_err(&r, 1);
  /* strict: aborts through kernel_fatal */
  kernel_fatal_hook = on_fatal;
  harness_set_strict(true);
  r = R(0x3D, 0);
  harness_call(FP_SEG(fp), FP_OFF(fp), HCONV_PASCAL, NULL, 0, &r);
  CHECK(harness_last_error() != 0);
  CHECK_EQ(kernel_fatal_pending, 1);
  CHECK(strstr(g_fatal_msg, "AH=0x3D") != NULL);
  harness_set_strict(false);
  kernel_fatal_hook = NULL;
  kernel_fatal_pending = 0;
  /* a listed value is fine in strict mode */
  harness_set_strict(true);
  r = R(0x30, 0);
  dos_ok(&r);
  harness_set_strict(false);
}

/* The plain function without the harness: the same results. */
static void test_dos_direct(void) {
  KDosRegs r;
  memset(&r, 0, sizeof r);
  r.ax = 0x2A00;
  r.flags = 1;
  rt_clock_set(1995, 12, 25, 10, 20, 30);
  DOS3Call(&r);
  CHECK_EQ(r.flags & 1, 0);
  CHECK_EQ(r.cx, 1995);
  memset(&r, 0, sizeof r);
  r.ax = 0x7700;
  kernel_dos_strict = 0;
  DOS3Call(&r);
  CHECK_EQ(r.flags & 1, 1);
  CHECK_EQ(r.ax, 1);
}

/* ---- profile ---- */

static uint16_t gpi(const char *app, const char *key, int16_t def, const char *file) {
  uint32_t a = far_str(app), k = far_str(key), f = far_str(file);
  uint16_t w[7] = {HI(a), LO(a), HI(k), LO(k), (uint16_t)def, HI(f), LO(f)};
  return (uint16_t)kcall(127, "GetPrivateProfileInt", 14, HR_RET16, w, 7);
}
static uint16_t gps(const char *app, const char *key, const char *def, uint32_t out, uint16_t size,
                    const char *file) {
  uint32_t a = app ? far_str(app) : 0, k = key ? far_str(key) : 0, d = def ? far_str(def) : 0;
  uint32_t f = far_str(file);
  uint16_t w[11] = {HI(a), LO(a), HI(k), LO(k), HI(d), LO(d), HI(out), LO(out), size, HI(f), LO(f)};
  return (uint16_t)kcall(128, "GetPrivateProfileString", 22, HR_RET16, w, 11);
}
static uint16_t wps(const char *app, const char *key, const char *val, const char *file) {
  uint32_t a = far_str(app), k = key ? far_str(key) : 0, v = val ? far_str(val) : 0, f = far_str(file);
  uint16_t w[8] = {HI(a), LO(a), HI(k), LO(k), HI(v), LO(v), HI(f), LO(f)};
  return (uint16_t)kcall(129, "WritePrivateProfileString", 16, HR_RET16, w, 8);
}
static int file_is(const char *path, const char *want) {
  const uint8_t *d;
  size_t n;
  if (vfs_get(path, &d, &n)) return 0;
  if (n != strlen(want) || memcmp(d, want, n)) {
    fprintf(stderr, "file %s is [%.*s], want [%s]\n", path, (int)n, (const char *)d, want);
    return 0;
  }
  return 1;
}

static void test_profile(void) {
  uint32_t out = far_alloc(64);
  const uint8_t *d;
  size_t n;

  /* a file name without a path refers to C:\WINDOWS */
  CHECK_EQ(wps("Stars", "Key", "Val", "stars.ini"), 1);
  CHECK(file_is("C:\\WINDOWS\\STARS.INI", "[Stars]\r\nKey=Val\r\n"));
  CHECK_EQ(vfs_get("C:\\STARS\\STARS.INI", &d, &n), -1);
  CHECK_EQ(gps("Stars", "Key", "dflt", out, 64, "stars.ini"), 3);
  CHECK(strcmp((char *)LIN(out), "Val") == 0);
  CHECK_EQ(gps("STARS", "KEY", "dflt", out, 64, "STARS.INI"), 3); /* case-insensitive */
  CHECK(strcmp((char *)LIN(out), "Val") == 0);
  /* a missing key / section / file returns the default */
  CHECK_EQ(gps("Stars", "Nope", "dflt", out, 64, "stars.ini"), 4);
  CHECK(strcmp((char *)LIN(out), "dflt") == 0);
  CHECK_EQ(gps("Nope", "Key", "dflt", out, 64, "stars.ini"), 4);
  CHECK_EQ(gps("Stars", "Key", "dflt", out, 64, "nofile.ini"), 4);
  CHECK_EQ(gps("Stars", "Key", NULL, out, 64, "nofile.ini"), 0); /* NULL default = empty */
  CHECK_EQ(((char *)LIN(out))[0], 0);
  /* the buffer limits the result (size - 1 characters + NUL) */
  CHECK_EQ(gps("Stars", "Key", "dflt", out, 3, "stars.ini"), 2);
  CHECK(strcmp((char *)LIN(out), "Va") == 0);
  CHECK_EQ(gps("Stars", "Nope", "dflt", out, 3, "stars.ini"), 2);
  CHECK(strcmp((char *)LIN(out), "df") == 0);
  CHECK_EQ(gps("Stars", "Key", "dflt", out, 0, "stars.ini"), 0);

  /* an explicit path is used as is */
  CHECK_EQ(wps("S", "K", "V", "C:\\STARS\\MY.INI"), 1);
  CHECK(file_is("C:\\STARS\\MY.INI", "[S]\r\nK=V\r\n"));
  CHECK_EQ(gps("S", "K", "", out, 64, "c:\\stars\\my.ini"), 1);

  /* other lines are preserved: comments, other sections, LF-only endings, blank lines */
  { const char *src = "; comment\n[Other]\nA=1\n\n[Stars]\nKey = Old  \nOther2=2\n\n[Last]\nZ=9";
    vfs_put("C:\\WINDOWS\\P.INI", src, strlen(src)); }
  CHECK_EQ(gps("stars", "key", "", out, 64, "p.ini"), 3); /* value is trimmed */
  CHECK(strcmp((char *)LIN(out), "Old") == 0);
  CHECK_EQ(wps("Stars", "Key", "New", "p.ini"), 1); /* replace in place */
  CHECK(file_is("C:\\WINDOWS\\P.INI",
                "; comment\n[Other]\nA=1\n\n[Stars]\nKey=New\r\nOther2=2\n\n[Last]\nZ=9\r\n"));
  /* UNVERIFIED: the blank line before an appended section is not checked against a reference;
   * the game only reads back its own values. Needs a reference capture. */
  CHECK_EQ(wps("Stars", "Added", "7", "p.ini"), 1); /* appended before the blank line */
  CHECK(file_is("C:\\WINDOWS\\P.INI",
                "; comment\n[Other]\nA=1\n\n[Stars]\nKey=New\r\nOther2=2\nAdded=7\r\n\n[Last]\nZ=9\r\n"));
  CHECK_EQ(wps("Last", "Y", "8", "p.ini"), 1); /* last section, file end */
  CHECK(file_is("C:\\WINDOWS\\P.INI",
                "; comment\n[Other]\nA=1\n\n[Stars]\nKey=New\r\nOther2=2\nAdded=7\r\n\n[Last]\nZ=9\r\nY=8\r\n"));
  CHECK_EQ(wps("Fresh", "N", "1", "p.ini"), 1); /* a new section at the end */
  CHECK(file_is("C:\\WINDOWS\\P.INI",
                "; comment\n[Other]\nA=1\n\n[Stars]\nKey=New\r\nOther2=2\nAdded=7\r\n\n[Last]\nZ=9\r\nY=8\r\n[Fresh]\r\nN=1\r\n"));
  CHECK_EQ(wps("Stars", "Added", NULL, "p.ini"), 1); /* NULL string deletes the key */
  CHECK_EQ(gps("Stars", "Added", "gone", out, 64, "p.ini"), 4);
  CHECK_EQ(wps("Other", NULL, NULL, "p.ini"), 1); /* NULL key deletes the section */
  CHECK_EQ(gps("Other", "A", "gone", out, 64, "p.ini"), 4);
  CHECK(file_is("C:\\WINDOWS\\P.INI",
                "; comment\n[Stars]\nKey=New\r\nOther2=2\n\n[Last]\nZ=9\r\nY=8\r\n[Fresh]\r\nN=1\r\n"));
  CHECK_EQ(gps("Last", "Z", "", out, 64, "p.ini"), 1);
  CHECK(strcmp((char *)LIN(out), "9") == 0);

  /* GetPrivateProfileInt */
  { const char *src = "[N]\r\nA=300\r\nB=-5\r\nC=12abc\r\nD=abc\r\nE= 42 \r\nF=\r\n";
    vfs_put("C:\\WINDOWS\\I.INI", src, strlen(src)); }
  CHECK_EQ(gpi("N", "A", 9, "i.ini"), 300);
  CHECK_EQ(gpi("N", "B", 9, "i.ini"), 0xFFFB);
  CHECK_EQ(gpi("N", "C", 9, "i.ini"), 12);
  CHECK_EQ(gpi("N", "D", 9, "i.ini"), 0);
  CHECK_EQ(gpi("N", "E", 9, "i.ini"), 42);
  CHECK_EQ(gpi("N", "F", 9, "i.ini"), 0);
  CHECK_EQ(gpi("N", "Missing", 9, "i.ini"), 9);   /* a missing key returns the default */
  CHECK_EQ(gpi("N", "Missing", -1, "i.ini"), 0xFFFF);
  CHECK_EQ(gpi("n", "a", 9, "I.INI"), 300);
  CHECK_EQ(gpi("N", "A", 9, "nofile.ini"), 9);
  /* out-of-range values clamp (Wine16 behaviour, recalled from memory, not verified) */
  { const char *big = "[N]\r\nP=70000\r\nM=-70000\r\nE=65535\r\nL=-32768\r\n";
    vfs_put("C:\\WINDOWS\\J.INI", big, strlen(big)); }
  CHECK_EQ(gpi("N", "P", 9, "j.ini"), 65535);
  CHECK_EQ(gpi("N", "M", 9, "j.ini"), 0x8000); /* -32768 */
  CHECK_EQ(gpi("N", "E", 9, "j.ini"), 65535);
  CHECK_EQ(gpi("N", "L", 9, "j.ini"), 0x8000);
}

/* ---- AccessResource ---- */

static const uint8_t *g_img;
static size_t g_img_len;

static void test_access_resource(void) {
  uint32_t nm = far_str("STARSMENU");
  uint16_t fr[5] = {HINST, HI(nm), LO(nm), 0, RT_MENU};
  uint16_t hres = (uint16_t)kcall(60, "FindResource", 10, HR_RET16, fr, 5);
  uint16_t sa[2] = {HINST, hres};
  uint32_t size = kcall(65, "SizeofResource", 4, HR_RET32, sa, 2);
  uint16_t hdata = (uint16_t)kcall(61, "LoadResource", 4, HR_RET16, sa, 2);
  uint32_t lp = kcall(62, "LockResource", 2, HR_RET32, &hdata, 1);
  uint32_t buf = far_alloc(size + 16);
  HFILE16 h;
  const uint8_t *d = NULL;
  size_t n = 0;
  int rc = 0;
  char name[64];
  uint32_t nb = far_alloc(64);
  uint16_t gm[4] = {HINST, HI(nb), LO(nb), 64};

  CHECK(hres != 0 && size > 0 && size < 0xFFF0 && lp != 0);
  /* the .exe is in the VFS at the path GetModuleFileName returns */
  kcall(49, "GetModuleFileName", 8, HR_RET16, gm, 4);
  snprintf(name, sizeof name, "%s", (char *)LIN(nb));
  CHECK(strcmp(name, "C:\\STARS\\STARS26I.EXE") == 0);
  rc = vfs_get(name, &d, &n);
  CHECK_EQ(rc, 0);
  CHECK_EQ(n, g_img_len);
  if (rc == 0) CHECK_MEM(d, g_img, n);

  { uint16_t a[2] = {HINST, hres};
    h = (HFILE16)kcall(64, "AccessResource", 4, HR_RET16, a, 2); }
  CHECK(h >= 5);
  CHECK_EQ(l_read(h, buf, (uint16_t)size), (uint16_t)size);
  CHECK_MEM(LIN(buf), LIN(lp), size); /* the same bytes as LockResource */
  CHECK_EQ(l_close(h), 0);
  { uint16_t a[2] = {HINST, 0};
    CHECK_EQ((uint16_t)kcall(64, "AccessResource", 4, HR_RET16, a, 2), 0xFFFF); }
  { uint16_t a[2] = {HINST, 9999};
    CHECK_EQ((uint16_t)kcall(64, "AccessResource", 4, HR_RET16, a, 2), 0xFFFF); }
}

/* kernel_load keeps what the host put in the VFS before it (files, clock) and replaces only
 * the .exe at C:\STARS\STARS26I.EXE. */
static void test_load_keeps_vfs(void) {
  const uint8_t *d = NULL;
  size_t n = 0;
  int rc, y, mo, dd, h, mi, sec;
  CHECK_EQ(vfs_put("C:\\STARS\\KEEP.DAT", "keepme", 6), 0);
  CHECK_EQ(vfs_put("C:\\STARS\\STARS26I.EXE", "stale", 5), 0);
  rt_clock_set(1996, 7, 4, 1, 2, 4);
  CHECK_EQ(kernel_load(g_img, g_img_len), 0);
  rc = vfs_get("C:\\STARS\\KEEP.DAT", &d, &n);
  CHECK_EQ(rc, 0);
  if (rc == 0) { CHECK_EQ(n, 6); CHECK_MEM(d, "keepme", 6); }
  rc = vfs_get("C:\\STARS\\STARS26I.EXE", &d, &n);
  CHECK_EQ(rc, 0);
  if (rc == 0) { CHECK_EQ(n, g_img_len); CHECK_MEM(d, g_img, n); }
  rt_clock_get(&y, &mo, &dd, &h, &mi, &sec);
  CHECK(y == 1996 && mo == 7 && dd == 4 && h == 1 && mi == 2 && sec == 4);
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
  reset_local();

  test_openfile();
  test_dos_dta_and_vectors();
  test_dos_clock_version_disk();
  test_dos_dirs();
  test_dos_files();
  test_dos_rename();
  test_dos_find();
  test_dos_exit();
  test_dos_unlisted();
  test_dos_direct();
  test_profile();
  test_access_resource();
  test_load_keeps_vfs();

  kernel_shutdown();
  harness_shutdown();
  free(buf);
  return TEST_DONE();
}

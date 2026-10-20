/* Virtual file system (task stars-0z4.3): runtime/vfs.c on its own, no harness. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "runtime/mem.h"
#include "runtime/vfs.h"
#include "test.h"

static void test_resolve(void) {
  char o[VFS_PATH_MAX];
  CHECK_EQ(vfs_resolve("test.hst", o, 0), 0);
  CHECK(strcmp(o, "C:\\STARS\\TEST.HST") == 0); /* relative to the current directory C:\STARS */
  CHECK_EQ(vfs_resolve("c:\\stars\\Test.Hst", o, 0), 0);
  CHECK(strcmp(o, "C:\\STARS\\TEST.HST") == 0);
  CHECK_EQ(vfs_resolve("C:/stars/test.hst", o, 0), 0);
  CHECK(strcmp(o, "C:\\STARS\\TEST.HST") == 0);
  CHECK_EQ(vfs_resolve("..\\windows\\.\\win.ini", o, 0), 0);
  CHECK(strcmp(o, "C:\\WINDOWS\\WIN.INI") == 0);
  CHECK_EQ(vfs_resolve("\\", o, 0), 0);
  CHECK(strcmp(o, "C:\\") == 0);
  CHECK_EQ(vfs_resolve("c:\\\\stars\\\\x.y\\", o, 0), 0); /* doubled and trailing separators */
  CHECK(strcmp(o, "C:\\STARS\\X.Y") == 0);
  CHECK_EQ(vfs_resolve("D:\\X.TXT", o, 0), 15);          /* only drive C: exists */
  CHECK_EQ(vfs_resolve("..\\..\\X", o, 0), 3);           /* above the root */
  CHECK_EQ(vfs_resolve("TOOLONGNAME.TXT", o, 0), 3);     /* not 8.3 */
  CHECK_EQ(vfs_resolve("A.LONG", o, 0), 3);
  CHECK_EQ(vfs_resolve("A.B.C", o, 0), 3);
  CHECK_EQ(vfs_resolve("A*.TXT", o, 0), 3);              /* wildcards only when asked */
  CHECK_EQ(vfs_resolve("A*.TXT", o, 1), 0);
  CHECK_EQ(vfs_resolve("*\\X", o, 1), 3);                /* ... and only in the last component */
}

static void test_files(void) {
  const uint8_t *d = NULL;
  size_t n = 0;
  int rc = 0;
  uint16_t a, b, c;
  uint32_t got, pos;
  uint8_t buf[32];
  /* Regression for the case-insensitivity mutation: created in one spelling, found in others. */
  CHECK_EQ(vfs_put("C:\\STARS\\TEST.HST", "0123456789", 10), 0);
  rc = vfs_get("c:\\stars\\test.hst", &d, &n);
  CHECK_EQ(rc, 0);
  CHECK_EQ(n, 10);
  if (rc == 0) CHECK_MEM(d, "0123456789", 10);
  CHECK_EQ(vfs_get("Test.Hst", &d, &n), 0);
  CHECK_EQ(vfs_get("nothere.xy", &d, &n), -1);
  CHECK(d >= mem_arena() && d < mem_arena() + mem_size()); /* file data lives in the arena */

  /* handles start at 5; each has its own file pointer */
  CHECK_EQ(vfs_open("test.hst", VFS_READ, 0, &a), 0);
  CHECK_EQ(a, 5);
  CHECK_EQ(vfs_open("TEST.HST", VFS_READWRITE, 0, &b), 0);
  CHECK_EQ(b, 6);
  CHECK_EQ(vfs_read(a, buf, 4, &got), 0);
  CHECK_EQ(got, 4);
  CHECK_MEM(buf, "0123", 4);
  CHECK_EQ(vfs_read(b, buf, 2, &got), 0);
  CHECK_MEM(buf, "01", 2);
  CHECK_EQ(vfs_read(a, buf, 100, &got), 0);
  CHECK_EQ(got, 6); /* stops at the end */
  CHECK_EQ(vfs_read(a, buf, 100, &got), 0);
  CHECK_EQ(got, 0);
  CHECK_EQ(vfs_write(a, "x", 1, &got), 5); /* read-only handle */
  CHECK_EQ(vfs_write(b, "XY", 2, &got), 0); /* overwrites at b's pointer (2) */
  rc = vfs_get("TEST.HST", &d, &n);
  CHECK_EQ(rc, 0);
  if (rc == 0) CHECK_MEM(d, "01XY456789", 10);
  CHECK_EQ(vfs_seek(b, 0, 2, &pos), 0);
  CHECK_EQ(pos, 10);
  CHECK_EQ(vfs_seek(b, 5, 1, &pos), 0); /* past the end is allowed */
  CHECK_EQ(pos, 15);
  CHECK_EQ(vfs_write(b, "Z", 1, &got), 0); /* gap is zero filled */
  CHECK_EQ(vfs_get("TEST.HST", &d, &n), 0);
  CHECK_EQ(n, 16);
  CHECK_EQ(d[12], 0);
  CHECK_EQ(d[15], 'Z');
  CHECK_EQ(vfs_seek(b, -100, 1, &pos), 25);
  CHECK_EQ(vfs_seek(b, 0, 7, &pos), 1);
  CHECK_EQ(vfs_unlink("TEST.HST"), 5); /* open */
  CHECK_EQ(vfs_close(a), 0);
  CHECK_EQ(vfs_close(a), 6);
  CHECK_EQ(vfs_read(a, buf, 1, &got), 6);
  CHECK_EQ(vfs_open("TEST.HST", VFS_READ, 0, &c), 0);
  CHECK_EQ(c, 5); /* lowest free handle is reused */
  vfs_close(b);
  vfs_close(c);
  CHECK_EQ(vfs_unlink("TEST.HST"), 0);
  CHECK_EQ(vfs_unlink("TEST.HST"), 2);
  CHECK_EQ(vfs_unlink("NODIR\\TEST.HST"), 3);
  CHECK_EQ(vfs_open("MISSING.XY", VFS_READ, 0, &a), 2);
  CHECK_EQ(vfs_open("NODIR\\X.Y", VFS_READ, 1, &a), 3);
  CHECK_EQ(vfs_open("C:\\WINDOWS", VFS_READ, 0, &a), 5); /* a directory is not a file */

  /* a large file grows the backing store */
  { static uint8_t big[100000];
    for (size_t i = 0; i < sizeof big; i++) big[i] = (uint8_t)(i * 7);
    CHECK_EQ(vfs_put("BIG.DAT", big, sizeof big), 0);
    CHECK_EQ(vfs_open("BIG.DAT", VFS_READWRITE, 0, &a), 0);
    CHECK_EQ(vfs_write(a, big, 70000, &got), 0);
    CHECK_EQ(got, 70000);
    rc = vfs_get("BIG.DAT", &d, &n);
    CHECK_EQ(rc, 0);
    CHECK_EQ(n, 100000);
    if (rc == 0) CHECK_MEM(d, big, 100000);
    vfs_close(a);
  }
  /* empty file */
  CHECK_EQ(vfs_put("EMPTY.TXT", NULL, 0), 0);
  CHECK_EQ(vfs_get("EMPTY.TXT", &d, &n), 0);
  CHECK_EQ(n, 0);
  /* too many open files */
  { uint16_t h[64]; int opened = 0, err = 0;
    for (int i = 0; i < 64 && !err; i++) { err = vfs_open("EMPTY.TXT", VFS_READ, 0, &h[i]); if (!err) opened++; }
    CHECK_EQ(err, 4);
    CHECK(opened >= 20);
    for (int i = 0; i < opened; i++) vfs_close(h[i]);
  }
}

typedef struct { char names[16][16]; int dir[16]; uint32_t size[16]; int n; } Lst;
static void on_ent(const char *name, int is_dir, uint32_t size, void *ud) {
  Lst *l = ud;
  strcpy(l->names[l->n], name);
  l->dir[l->n] = is_dir;
  l->size[l->n++] = size;
}

static void test_dirs(void) {
  Lst l = {0};
  char o[VFS_PATH_MAX];
  uint8_t at;
  uint16_t h;
  CHECK_EQ(vfs_list("C:\\", on_ent, &l), 2); /* WINDOWS, STARS */
  CHECK(strcmp(l.names[0], "WINDOWS") == 0 && l.dir[0]);
  CHECK(strcmp(l.names[1], "STARS") == 0 && l.dir[1]);
  CHECK_EQ(vfs_list("C:\\NOPE", on_ent, &l), -1);
  vfs_getcwd(o);
  CHECK(strcmp(o, "C:\\STARS") == 0);

  CHECK_EQ(vfs_mkdir("GAMES"), 0);
  CHECK_EQ(vfs_mkdir("games"), 5);          /* exists */
  CHECK_EQ(vfs_mkdir("NO\\SUB"), 3);
  CHECK_EQ(vfs_mkdir("C:\\"), 5);
  CHECK_EQ(vfs_put("GAMES\\A.M1", "a", 1), 0);
  CHECK_EQ(vfs_rmdir("GAMES"), 5);          /* not empty */
  CHECK_EQ(vfs_chdir("games"), 0);
  vfs_getcwd(o);
  CHECK(strcmp(o, "C:\\STARS\\GAMES") == 0);
  CHECK_EQ(vfs_open("a.m1", VFS_READ, 0, &h), 0); /* relative to the new directory */
  vfs_close(h);
  CHECK_EQ(vfs_rmdir("."), 5);               /* current directory */
  CHECK_EQ(vfs_chdir("A.M1"), 3);            /* a file is not a directory */
  CHECK_EQ(vfs_chdir("..\\NOPE"), 3);
  CHECK_EQ(vfs_chdir("\\STARS"), 0);

  CHECK_EQ(vfs_rename("GAMES", "OLD"), 0);   /* a directory moves with its contents */
  CHECK_EQ(vfs_getattr("OLD\\A.M1", &at), 0);
  CHECK_EQ(vfs_getattr("GAMES\\A.M1", &at), 3);
  CHECK_EQ(vfs_put("B.TXT", "b", 1), 0);
  CHECK_EQ(vfs_rename("B.TXT", "OLD\\A.M1"), 5); /* target exists */
  CHECK_EQ(vfs_rename("B.TXT", "NOPE\\B.TXT"), 3);
  CHECK_EQ(vfs_rename("NOTHERE", "X"), 2);
  CHECK_EQ(vfs_rename("B.TXT", "OLD\\C.TXT"), 0);
  CHECK_EQ(vfs_getattr("B.TXT", &at), 2);

  CHECK_EQ(vfs_getattr("OLD", &at), 0);
  CHECK_EQ(at, VFS_ATTR_DIR);
  CHECK_EQ(vfs_getattr("OLD\\C.TXT", &at), 0);
  CHECK_EQ(at, VFS_ATTR_ARCHIVE);
  CHECK_EQ(vfs_setattr("OLD\\C.TXT", VFS_ATTR_RO), 0);
  CHECK_EQ(vfs_unlink("OLD\\C.TXT"), 5);      /* read-only */
  CHECK_EQ(vfs_open("OLD\\C.TXT", VFS_WRITE, 0, &h), 5);
  CHECK_EQ(vfs_open("OLD\\C.TXT", VFS_READ, 0, &h), 0);
  vfs_close(h);
  CHECK_EQ(vfs_setattr("OLD\\C.TXT", 0), 0);
  CHECK_EQ(vfs_unlink("OLD\\C.TXT"), 0);
  CHECK_EQ(vfs_unlink("OLD\\A.M1"), 0);
  CHECK_EQ(vfs_rmdir("OLD"), 0);
  CHECK_EQ(vfs_rmdir("OLD"), 3);
}

static void test_find(void) {
  VfsSearch s;
  VfsEnt e;
  CHECK_EQ(vfs_mkdir("F"), 0);
  vfs_put("F\\GAME.HST", "1", 1);
  vfs_put("F\\GAME.M1", "22", 2);
  vfs_put("F\\GAME.M2", "333", 3);
  vfs_put("F\\README", "4444", 4);
  vfs_put("F\\HID.DAT", "5", 1);
  vfs_setattr("F\\HID.DAT", VFS_ATTR_HIDDEN);
  vfs_mkdir("F\\SUB");

  CHECK_EQ(vfs_find_first("F\\*.M?", 0, &s, &e), 0);
  CHECK(strcmp(e.name, "GAME.M1") == 0);
  CHECK_EQ(e.size, 2);
  CHECK_EQ(e.attr, VFS_ATTR_ARCHIVE);
  CHECK_EQ(vfs_find_next(&s, &e), 0);
  CHECK(strcmp(e.name, "GAME.M2") == 0);
  CHECK_EQ(vfs_find_next(&s, &e), 18);

  CHECK_EQ(vfs_find_first("f\\*.*", 0, &s, &e), 0); /* no hidden, no directories */
  CHECK(strcmp(e.name, "GAME.HST") == 0);
  CHECK_EQ(vfs_find_next(&s, &e), 0);
  CHECK_EQ(vfs_find_next(&s, &e), 0);
  CHECK(strcmp(e.name, "GAME.M2") == 0);
  CHECK_EQ(vfs_find_next(&s, &e), 0);
  CHECK(strcmp(e.name, "README") == 0); /* "*.*" matches a name with no extension */
  CHECK_EQ(vfs_find_next(&s, &e), 18);

  CHECK_EQ(vfs_find_first("F\\*", VFS_ATTR_DIR | VFS_ATTR_HIDDEN, &s, &e), 0);
  { int n = 1; while (vfs_find_next(&s, &e) == 0) n++; CHECK_EQ(n, 6); }
  CHECK_EQ(vfs_find_first("F\\GAME.", 0, &s, &e), 3);   /* "GAME." is not a valid name */
  CHECK_EQ(vfs_find_first("F\\NONE.*", 0, &s, &e), 2);
  CHECK_EQ(vfs_find_first("NODIR\\*.*", 0, &s, &e), 3);
  CHECK_EQ(vfs_find_first("F\\SUB", VFS_ATTR_DIR, &s, &e), 0); /* a plain name finds the directory */
  CHECK(strcmp(e.name, "SUB") == 0);
  CHECK_EQ(e.attr, VFS_ATTR_DIR);
}

static void test_clock_and_dos_state(void) {
  int y, mo, d, h, mi, s;
  rt_clock_get(&y, &mo, &d, &h, &mi, &s);
  CHECK(y == 1980 && mo == 1 && d == 1 && h == 0 && mi == 0 && s == 0); /* deterministic default */
  CHECK_EQ(rt_clock_weekday(), 2); /* 1980-01-01 was a Tuesday */
  rt_clock_set(1995, 12, 25, 10, 20, 30);
  rt_clock_get(&y, &mo, &d, &h, &mi, &s);
  CHECK(y == 1995 && mo == 12 && d == 25 && h == 10 && mi == 20 && s == 30);
  CHECK_EQ(rt_clock_weekday(), 1); /* Monday */
  rt_clock_set(2000, 2, 29, 0, 0, 0);
  CHECK_EQ(rt_clock_weekday(), 2); /* Tuesday */

  CHECK_EQ(vfs_get_dta(), 0);
  vfs_set_dta(0x01180040);
  CHECK_EQ(vfs_get_dta(), 0x01180040);
  vfs_set_vector(0x24, 0x12345678);
  CHECK_EQ(vfs_get_vector(0x24), 0x12345678);
  CHECK_EQ(vfs_get_vector(0x23), 0);

  /* file timestamps come from the runtime clock */
  rt_clock_set(1995, 12, 25, 10, 20, 30);
  vfs_put("STAMP.TXT", "s", 1);
  { VfsSearch se; VfsEnt e;
    CHECK_EQ(vfs_find_first("STAMP.TXT", 0, &se, &e), 0);
    CHECK_EQ(e.date, ((1995 - 1980) << 9) | (12 << 5) | 25);
    CHECK_EQ(e.time, (10 << 11) | (20 << 5) | 15);
  }
}

/* The whole file system lives in the arena: a second life after shutdown starts clean. */
static void test_lifecycle(void) {
  const uint8_t *d;
  size_t n;
  CHECK_EQ(vfs_get("STAMP.TXT", &d, &n), 0);
  vfs_init();
  CHECK_EQ(vfs_get("STAMP.TXT", &d, &n), -1);
  vfs_shutdown();
  CHECK_EQ(vfs_put("NEW.TXT", "n", 1), 0); /* first use creates the state again */
  CHECK_EQ(vfs_get("NEW.TXT", &d, &n), 0);
}

/* Item 6: a directory rename that cannot complete changes nothing. */
static void test_rename_dir_atomic(void) {
  char deep[VFS_PATH_MAX + 40] = "C:\\STARS\\RD1";
  const uint8_t *d = NULL;
  size_t n = 0;
  int i;
  for (i = 0; i < 7; i++) strcat(deep, "\\AAAAAAAA"); /* 11 + 63 = 74 characters */
  strcat(deep, "\\X.Y");                                /* 78: fits under RD1, not under RD1LONG */
  CHECK_EQ(vfs_put("C:\\STARS\\RD1\\SHORT.TXT", "s", 1), 0);
  CHECK_EQ(vfs_put(deep, "x", 1), 0);
  CHECK_EQ(vfs_rename("C:\\STARS\\RD1", "C:\\STARS\\RD1LONG"), 5);
  CHECK_EQ(vfs_get("C:\\STARS\\RD1\\SHORT.TXT", &d, &n), 0); /* old tree intact */
  CHECK_EQ(vfs_get(deep, &d, &n), 0);
  CHECK_EQ(vfs_list("C:\\STARS\\RD1LONG", NULL, NULL), -1);     /* nothing appeared under the new name */
  CHECK_EQ(vfs_get("C:\\STARS\\RD1LONG\\SHORT.TXT", &d, &n), -1);
  /* a rename that fits still works */
  CHECK_EQ(vfs_rename("C:\\STARS\\RD1", "C:\\STARS\\RD2"), 0);
  CHECK_EQ(vfs_get("C:\\STARS\\RD2\\SHORT.TXT", &d, &n), 0);
  CHECK_EQ(vfs_get("C:\\STARS\\RD1\\SHORT.TXT", &d, &n), -1);
}

/* Item 4: st() re-initialises when the state block's magic is wrong (the arena was reused at
 * the same address without vfs_shutdown). The test finds the block by scanning for the magic. */
static void test_stale_magic(void) {
  const uint8_t *a = mem_arena();
  const uint8_t *d = NULL;
  size_t n = 0, off, found = 0;
  const uint32_t magic = 0x53564653u;
  vfs_init();
  CHECK_EQ(vfs_put("STALE.TXT", "s", 1), 0);
  for (off = 16; off + 4 <= mem_size(); off += 4) {
    uint32_t v;
    memcpy(&v, a + off, 4);
    if (v == magic) { found = off; break; }
  }
  CHECK(found != 0);
  if (found) memset((uint8_t *)a + found, 0, 4); /* arena is writable memory owned by mem.c */
  CHECK_EQ(vfs_get("STALE.TXT", &d, &n), -1);     /* fresh state: the stale file is gone */
  CHECK_EQ(vfs_put("FRESH.TXT", "f", 1), 0);      /* ... and the new state is usable */
  CHECK_EQ(vfs_get("FRESH.TXT", &d, &n), 0);
  vfs_shutdown();
}

/* Item 2: find-first with attribute exactly 0x08 searches for a volume label; there is none. */
static void test_volume_label(void) {
  VfsSearch se;
  VfsEnt e;
  vfs_init();
  CHECK_EQ(vfs_put("C:\\VL.TXT", "v", 1), 0);
  CHECK_EQ(vfs_find_first("C:\\*.*", 0x08, &se, &e), 18);
  CHECK_EQ(vfs_find_first("C:\\*.*", 0x10, &se, &e), 0); /* directories still found */
  CHECK_EQ(vfs_find_first("C:\\*.*", 0x00, &se, &e), 0); /* ordinary files too */
  vfs_shutdown();
}

int main(void) {
  mem_init(0);
  vfs_init();
  test_resolve();
  test_files();
  test_dirs();
  test_find();
  test_clock_and_dos_state();
  test_lifecycle();
  test_rename_dir_atomic();
  test_stale_magic();
  test_volume_label();
  vfs_shutdown();
  mem_shutdown();
  return TEST_DONE();
}

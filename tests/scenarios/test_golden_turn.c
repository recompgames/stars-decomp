/* Golden master for turn generation (docs/PLAN.md decision 9, section 4). Public (decision 8): this
 * file holds no game bytes. The game files it produces are local-only; the ORIGINAL run's copies are
 * saved under tests/golden/turn_N/ (git-ignored) for inspection, and the test does not need them.
 *
 * The user interface does not run. The original stars26i.exe is started from its entry point
 * (34:0x001A) on the Win16 runtime, so that the C runtime start-up and WinMain initialise DGROUP and
 * the heap exactly as in a real run. The first time GetMessage finds nothing to do (user_wait_hook;
 * the "Register Serial Number" dialog is up) the hook below calls the two functions directly, the
 * way a DIFF calls a single function, and then lets the game quit:
 *
 *   15:0x47f0  creates a new game with fixed settings. It clears the game header (DS:0x70), sets
 *              two players (a human template and the AI template from 15:0x694a), the universe size
 *              and flags, seeds the random generator (8:0x8672 with 0x499602d2), removes the old
 *              files of the base name in DS:0x5764 (8:0x8df4 formats the names, 15:0x0922 deletes)
 *              and calls 15:0x1334, which places the planets and the players' homeworlds and writes
 *              the universe file (.xy) with the host and player files (.hst, .m1, .m2). It is the
 *              creation function of the tutorial game (called from 31:0x5f14); the New Game dialog
 *              (15:0x4efe, 15:0x55a6, 15:0x6286) fills the same globals from its controls and
 *              ends in 15:0x1334 through 15:0x6ab2.
 *   22:0x0000  generates a turn: it loads the game of DS:0x5764 (14:0x2992, 14:0x0508), reads the
 *              orders files, runs the turn (22:0x0f6e and the segments 23, 28, 33, ...), writes
 *              the files (14:0x35b0) and returns nonzero on success. Reached from the Generate
 *              menu command (4:0x1f40), the host timer (4:0x4dba, decompiled) and the command line
 *              (4:0x0528).
 *
 * Obstacles (a) (the game quits right after the New Game commit) and (b) (a repaint loop in segment
 * 28) of the first tracker do not arise: neither the New Game dialog nor the frame window procedure
 * runs.
 *
 * Both runs use the same inputs: a fresh runtime, the clock set to a fixed date (the header salts
 * use GetTickCount) and the seed above. ORIGINAL runs the exe as it is. DECOMP runs the same,
 * but every FAR_CDECL function that has a C twin (build/registry.c) is diverted to the C code
 * (difftest_divert_decomp), so the files can only match when the C functions that a turn reaches
 * behave as the originals. Decompiled functions the turn does not reach cannot be checked here.
 *
 * Tests: golden_turn_1 (ORIGINAL vs DECOMP, new game + 1 turn, every file byte for byte),
 * golden_turn_deterministic (two ORIGINAL runs agree), golden_turn_2 (the same after two turns). */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "harness/commdlg_thunks.h"
#include "harness/difftest.h"
#include "harness/gdi_thunks.h"
#include "harness/harness.h"
#include "harness/kernel_thunks.h"
#include "harness/toolhelp_win87em_thunks.h"
#include "harness/user_thunks.h"
#include "runtime/gdi.h"
#include "runtime/kernel.h"
#include "runtime/user.h"
#include "runtime/vfs.h"
#include "test.h"

#define EXE "original/stars26i.exe"
#define BASE "GOLD" /* the game's base name (DS:0x5764): files GOLD.XY, GOLD.HST, GOLD.M1, ... */
#define MAXF 32

typedef struct {
  char name[16];
  uint8_t *data;
  size_t len;
} GFile;

typedef struct {
  GFile f[MAXF];
  int n;
  int ok;               /* the run reached the end of the hook */
  int hook_runs;
  int turns_wanted;
  uint32_t turn_ret[8]; /* AX of each 22:0x0000 */
  int turn_before;      /* the turn number in the .hst after the new game, -1 when unreadable */
  int turn_after;
  int diverted;         /* functions diverted to C (DECOMP) */
  unsigned c_calls;     /* calls that ran as C */
} World;

static World *g_w;

static void collect_cb(const char *name, int is_dir, uint32_t size, void *ud) {
  World *w = ud;
  char path[64];
  const uint8_t *d;
  size_t len;
  if (is_dir || w->n >= MAXF || strncmp(name, BASE ".", sizeof BASE) != 0) return;
  snprintf(path, sizeof path, "C:\\STARS\\%s", name);
  if (vfs_get(path, &d, &len) != 0 || len != size) return;
  snprintf(w->f[w->n].name, sizeof w->f[w->n].name, "%s", name);
  w->f[w->n].data = malloc(len ? len : 1);
  memcpy(w->f[w->n].data, d, len);
  w->f[w->n].len = len;
  w->n++;
}

/* The turn number of the host file: the file header block (type 8, size 0x10, not scrambled) is the
 * first block and its "turn" word is at body offset 0x0A (docs/FILE_FORMATS.md section 3). */
static int hst_turn(void) {
  const uint8_t *d;
  size_t len;
  if (vfs_get("C:\\STARS\\" BASE ".HST", &d, &len) != 0 || len < 2 + 0x10) return -1;
  return d[2 + 0x0A] | (d[2 + 0x0B] << 8);
}

static bool hook(void) {
  World *w = g_w;
  if (w->hook_runs++) return false;
  strcpy((char *)LINN(0x5764), BASE);
  rt_call_far(MK_FP(MEM_SEL_SEG(15), 0x47f0), FAR_CDECL, NULL, 0);
  w->turn_before = hst_turn();
  for (int i = 0; i < w->turns_wanted && i < 8; i++)
    w->turn_ret[i] = rt_call_far(MK_FP(MEM_SEL_SEG(22), 0x0000), FAR_CDECL, NULL, 0) & 0xFFFF;
  w->turn_after = hst_turn();
  vfs_list("C:\\STARS", collect_cb, w);
  w->ok = 1;
  return false;
}

static void world_free(World *w) {
  for (int i = 0; i < w->n; i++) free(w->f[i].data);
  memset(w, 0, sizeof *w);
}

/* One complete run in a fresh runtime: new game, then `turns` turns. mode is RT_MODE_ORIGINAL or
 * RT_MODE_DECOMP. Returns 0 when the run reached the hook's end. */
static int run_world_here(int mode, int turns, World *w) {
  FILE *fp = fopen(EXE, "rb");
  long len;
  uint8_t *img;
  memset(w, 0, sizeof *w);
  w->turns_wanted = turns;
  w->turn_before = w->turn_after = -1;
  if (!fp) return -1;
  fseek(fp, 0, SEEK_END);
  len = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  img = malloc((size_t)len);
  if (!img || fread(img, 1, (size_t)len, fp) != (size_t)len) {
    fclose(fp);
    free(img);
    return -1;
  }
  fclose(fp);
  if (harness_load(EXE) != 0) {
    free(img);
    return -1;
  }
  if (gdi_init() != 0 || user_init() != 0 || kernel_load(img, (size_t)len) != 0) {
    free(img);
    return -1;
  }
  free(img);
  mem_local_init(harness_stack_top(), (uint16_t)(harness_stack_top() + 8192u));
  kernel_register();
  gdi_register();
  user_register();
  commdlg_register();
  toolhelp_win87em_register();
  rt_clock_set(1995, 6, 15, 12, 0, 0);
  harness_set_strict(true);
  rt_set_mode(mode);
  if (mode == RT_MODE_DECOMP) w->diverted = difftest_divert_decomp();
  g_w = w;
  user_wait_hook = hook;
  harness_call(MEM_SEL_SEG(34), 0x001A, HCONV_CDECL, NULL, 0, NULL);
  user_wait_hook = NULL;
  w->c_calls = difftest_divert_calls();
  user_shutdown();
  kernel_shutdown();
  harness_shutdown();
  rt_set_mode(RT_MODE_ORIGINAL);
  return w->ok ? 0 : -1;
}


static int write_all(int fd, const void *p, size_t n) {
  const uint8_t *b = p;
  while (n) {
    ssize_t k = write(fd, b, n);
    if (k <= 0) return -1;
    b += k;
    n -= (size_t)k;
  }
  return 0;
}

static int read_all(int fd, void *p, size_t n) {
  uint8_t *b = p;
  while (n) {
    ssize_t k = read(fd, b, n);
    if (k <= 0) return -1;
    b += k;
    n -= (size_t)k;
  }
  return 0;
}

/* The runtime keeps process-wide state that harness_shutdown does not reset (a second world in the
 * same process fails in user_shutdown), so every world runs in a child process of its own and sends
 * its files back through a pipe. Returns 0 when the child ended normally and reached the hook's end. */
static int run_world(int mode, int turns, World *w) {
  int fd[2], status = 0, rc = -1;
  pid_t pid;
  memset(w, 0, sizeof *w);
  w->turn_before = w->turn_after = -1;
  fflush(NULL);
  if (pipe(fd) != 0) return -1;
  pid = fork();
  if (pid < 0) return -1;
  if (pid == 0) {
    int r = run_world_here(mode, turns, w);
    close(fd[0]);
    if (r == 0) {
      if (write_all(fd[1], w, sizeof *w) != 0) _exit(3);
      for (int i = 0; i < w->n; i++)
        if (write_all(fd[1], w->f[i].data, w->f[i].len) != 0) _exit(3);
    }
    close(fd[1]);
    fflush(NULL);
    _exit(r == 0 ? 0 : 2);
  }
  close(fd[1]);
  {
    World got;
    if (read_all(fd[0], &got, sizeof got) == 0 && got.n >= 0 && got.n <= MAXF) {
      int good = 1;
      for (int i = 0; i < got.n && good; i++) {
        got.f[i].data = malloc(got.f[i].len ? got.f[i].len : 1);
        if (!got.f[i].data || read_all(fd[0], got.f[i].data, got.f[i].len) != 0) good = 0;
      }
      if (good) {
        *w = got;
        rc = 0;
      }
    }
  }
  close(fd[0]);
  if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    fprintf(stderr, "golden: the %s run did not end normally (status 0x%x)\n",
            mode == RT_MODE_DECOMP ? "DECOMP" : "ORIGINAL", status);
    rc = -1;
  }
  return rc;
}

static const GFile *find_file(const World *w, const char *name) {
  for (int i = 0; i < w->n; i++)
    if (strcmp(w->f[i].name, name) == 0) return &w->f[i];
  return NULL;
}

/* Compares every file of a with b and b with a. Prints the first differing offset per file. Returns
 * the number of files that differ or are missing on one side. */
static int compare_worlds(const World *a, const World *b, const char *what) {
  int bad = 0;
  for (int i = 0; i < a->n; i++) {
    const GFile *x = &a->f[i], *y = find_file(b, x->name);
    size_t k, m;
    if (!y) {
      fprintf(stderr, "golden %s: %s missing in the second run\n", what, x->name);
      bad++;
      continue;
    }
    m = x->len < y->len ? x->len : y->len;
    for (k = 0; k < m && x->data[k] == y->data[k]; k++) {}
    if (k < m || x->len != y->len) {
      fprintf(stderr, "golden %s: %s differs at offset %zu (sizes %zu and %zu)\n", what, x->name, k, x->len,
              y->len);
      bad++;
    }
  }
  for (int i = 0; i < b->n; i++)
    if (!find_file(a, b->f[i].name)) {
      fprintf(stderr, "golden %s: %s missing in the first run\n", what, b->f[i].name);
      bad++;
    }
  return bad;
}

/* Saves the files under tests/golden/<dir>/ (git-ignored). Failures are ignored: it is for
 * inspection only. */
static void save_world(const World *w, const char *dir) {
  char path[256];
  mkdir("tests/golden", 0777);
  snprintf(path, sizeof path, "tests/golden/%s", dir);
  if (mkdir(path, 0777) != 0 && errno != EEXIST) return;
  for (int i = 0; i < w->n; i++) {
    FILE *fp;
    snprintf(path, sizeof path, "tests/golden/%s/%s", dir, w->f[i].name);
    fp = fopen(path, "wb");
    if (!fp) continue;
    fwrite(w->f[i].data, 1, w->f[i].len, fp);
    fclose(fp);
  }
}

static size_t total_bytes(const World *w) {
  size_t t = 0;
  for (int i = 0; i < w->n; i++) t += w->f[i].len;
  return t;
}

/* The run produced a game: the four files, a turn that advanced, a nonzero return. */
static void check_game(const World *w, int turns) {
  CHECK(find_file(w, BASE ".XY") != NULL);
  CHECK(find_file(w, BASE ".HST") != NULL);
  CHECK(find_file(w, BASE ".M1") != NULL);
  CHECK(find_file(w, BASE ".M2") != NULL);
  CHECK_EQ(w->turn_after, w->turn_before + turns);
  for (int i = 0; i < turns; i++) CHECK(w->turn_ret[i] != 0);
}

static void golden_turn(int turns, const char *dir, const char *what) {
  static World o, d;
  CHECK_EQ(run_world(RT_MODE_ORIGINAL, turns, &o), 0);
  CHECK_EQ(run_world(RT_MODE_DECOMP, turns, &d), 0);
  check_game(&o, turns);
  check_game(&d, turns);
  /* the diversion must be live, or equality would prove nothing */
  CHECK(d.diverted > 100);
  CHECK(d.c_calls > 0);
  CHECK_EQ(compare_worlds(&o, &d, what), 0);
  printf("%s: %d files, %zu bytes; %d functions diverted, %u calls ran as C\n", what, o.n, total_bytes(&o),
         d.diverted, d.c_calls);
  save_world(&o, dir);
  world_free(&o);
  world_free(&d);
}

static void golden_turn_deterministic(void) {
  static World a, b;
  CHECK_EQ(run_world(RT_MODE_ORIGINAL, 1, &a), 0);
  CHECK_EQ(run_world(RT_MODE_ORIGINAL, 1, &b), 0);
  check_game(&a, 1);
  CHECK_EQ(compare_worlds(&a, &b, "golden_turn_deterministic"), 0);
  world_free(&a);
  world_free(&b);
}

int main(void) {
  golden_turn(1, "turn_1", "golden_turn_1");
  golden_turn_deterministic();
  golden_turn(2, "turn_2", "golden_turn_2");
  return TEST_DONE();
}

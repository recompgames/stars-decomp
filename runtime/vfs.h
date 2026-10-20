/* Virtual file system (task stars-0z4.3). Plain C, no harness dependency.
 *
 * One drive, C:, with case-insensitive 8.3 names; C:\WINDOWS and C:\STARS exist at start-up
 * and C:\STARS is the current directory. All state (nodes, file data, the handle table, the
 * current directory, the DOS vectors and DTA, the clock) lives in the arena (rt_alloc), so a
 * snapshot of the arena includes it. Nothing here touches the host file system: hosts move
 * files in and out through vfs_put / vfs_get.
 *
 * The state block is created on first use; vfs_shutdown (called by kernel_shutdown) frees it.
 * DOS error codes are returned as positive numbers (0 = success): 1 invalid function,
 * 2 file not found, 3 path not found, 4 too many open files, 5 access denied, 6 invalid
 * handle, 15 invalid drive, 18 no more files, 25 seek error. */
#ifndef STARS_RUNTIME_VFS_H
#define STARS_RUNTIME_VFS_H

#include <stddef.h>
#include <stdint.h>

#define VFS_PATH_MAX 80   /* full path buffer: "C:\" + directories + name + NUL */
#define VFS_FIRST_HANDLE 5 /* DOS handles 0-4 are the standard devices */

enum { VFS_READ = 0, VFS_WRITE = 1, VFS_READWRITE = 2 };
enum { VFS_ATTR_RO = 0x01, VFS_ATTR_HIDDEN = 0x02, VFS_ATTR_SYSTEM = 0x04, VFS_ATTR_DIR = 0x10,
       VFS_ATTR_ARCHIVE = 0x20 };

/* Lifecycle. vfs_init resets to the start-up tree; calling it is optional (first use
 * creates the state). Needs mem_init. */
void vfs_init(void);
void vfs_shutdown(void);

/* Runtime clock: deterministic, set by tests and hosts. Default 1980-01-01 00:00:00. */
void rt_clock_set(int year, int month, int day, int hour, int minute, int second);
void rt_clock_get(int *year, int *month, int *day, int *hour, int *minute, int *second);
int rt_clock_weekday(void); /* 0 = Sunday */
/* Milliseconds since 1980-01-01 00:00:00 modulo 2^32 (the date clock; GetTickCount subtracts its value at user_init), always a multiple of
 * 1000: the clock has one-second resolution. */
uint32_t rt_clock_ms(void);

/* ---- host interface ---- */

/* Creates or replaces the file at path (missing parent directories are created).
 * Returns 0, or -1 (bad path, a directory in the way, or out of memory). */
int vfs_put(const char *path, const void *data, size_t len);
/* Points *data at the file's bytes (inside the arena; valid until the next change of that
 * file) and *len at its size. Returns 0, or -1 when the file does not exist. */
int vfs_get(const char *path, const uint8_t **data, size_t *len);
/* Calls cb for every entry of the directory in creation order. Returns the number of
 * entries, or -1 when the directory does not exist. */
typedef void (*VfsListFn)(const char *name, int is_dir, uint32_t size, void *ud);
int vfs_list(const char *dir, VfsListFn cb, void *ud);

/* ---- DOS-level operations ---- */

/* Normalises path (drive letter, ".", "..", slashes, upper-case 8.3) against the current
 * directory into out[VFS_PATH_MAX]. wild allows * and ? in the last component. */
int vfs_resolve(const char *path, char *out, int wild);

int vfs_open(const char *path, int mode, int create, uint16_t *h);
int vfs_read(uint16_t h, void *buf, uint32_t n, uint32_t *got);
int vfs_write(uint16_t h, const void *buf, uint32_t n, uint32_t *put);
int vfs_seek(uint16_t h, int32_t off, int whence, uint32_t *newpos);
int vfs_close(uint16_t h);
int vfs_handle_size(uint16_t h, uint32_t *size);
int vfs_unlink(const char *path);
int vfs_mkdir(const char *path);
int vfs_rmdir(const char *path);
int vfs_chdir(const char *path);
void vfs_getcwd(char *out);
int vfs_rename(const char *from, const char *to);
int vfs_getattr(const char *path, uint8_t *attr);
int vfs_setattr(const char *path, uint8_t attr);

/* Directory search. The caller keeps the VfsSearch (DOS keeps it in the DTA). */
typedef struct VfsSearch {
  char pattern[13];
  uint8_t attr;
  uint16_t dir;  /* node index of the directory */
  uint16_t next; /* next node index to examine */
} VfsSearch;
typedef struct VfsEnt {
  char name[13];
  uint8_t attr;
  uint16_t time, date; /* DOS packed */
  uint32_t size;
} VfsEnt;
int vfs_find_first(const char *spec, uint8_t attr, VfsSearch *s, VfsEnt *e);
int vfs_find_next(VfsSearch *s, VfsEnt *e);

/* DOS state kept with the file system (arena resident). */
uint32_t vfs_get_dta(void);
void vfs_set_dta(uint32_t fp);
uint32_t vfs_get_vector(uint8_t n);
void vfs_set_vector(uint8_t n, uint32_t fp);

#endif /* STARS_RUNTIME_VFS_H */

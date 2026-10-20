/* Segmented memory model (docs/PLAN.md section 3, decision 1).
 *
 * One arena holds everything: the descriptor table (GDT), segment contents,
 * the global and local heaps, and the runtime heap. Far pointers are 32-bit
 * selector:offset values; LIN() maps one to a host pointer through the GDT
 * stored in the arena. Nothing in the arena holds a host pointer, only
 * selectors and arena offsets, so a snapshot is a plain memory copy.
 *
 * Fixed layout (binding, shared with the Unicorn harness):
 *   linear 0x00500            harness boot stub (reserved)
 *   linear 0x10000-0x1FFFF    GDT, 8192 descriptors x 8 bytes, 386 format
 *   selector 0                null
 *   selector n << 3           NE segment n (1-35); DGROUP (35) = 0x0118
 *   selector 0x0120           import thunk segment
 *   selector 0x0128           runtime callback segment
 *   selectors 0x0130-0x01F8   reserved; dynamic selectors start at 0x0200
 *   linear 0x100000 upward    segment contents, global heap; the runtime heap
 *                             (rt_alloc) lives at the top of the arena and is
 *                             covered by no selector
 * A descriptor is present when bit 7 of its access byte is set.
 */
#ifndef STARS_RUNTIME_MEM_H
#define STARS_RUNTIME_MEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MEM_BOOT_LINEAR 0x00500u
#define MEM_GDT_LINEAR 0x10000u
#define MEM_GDT_ENTRIES 8192u
#define MEM_HEAP_LINEAR 0x100000u
#define MEM_DEFAULT_ARENA (64u * 1024 * 1024)

#define MEM_SEL_SEG(n) ((uint16_t)((n) << 3))
#define MEM_SEL_DGROUP MEM_SEL_SEG(35)
#define MEM_SEL_THUNK 0x0120u
#define MEM_SEL_CALLBACK 0x0128u
#define MEM_SEL_FIRST_DYNAMIC 0x0200u

/* Values from Windows (Wine spec): selector step and shift for huge memory. */
#define MEM_AHINCR 8
#define MEM_AHSHIFT 3

/* __WINFLAGS value (KERNEL.178), shared by the harness and the browser build.
 * Owner-decided 2026-10-15: WF_PMODE 0x01 | WF_CPU486 0x08 | WF_ENHANCED 0x20 |
 * WF_80x87 0x400 = 0x0429. It deliberately overrides the 1043 in win16_api.inc. */
#define WINFLAGS_DEFAULT 0x0429

/* x87 control word full runs use. Owner-decided 2026-10-15 (stars-ckz): 0x1330, the word
 * Wine's WIN87EM init programs: PC = 11b (64-bit), RC = 00b (nearest even), IM/DM/ZM/OM
 * unmasked (invalid, denormal, divide by zero, overflow trap to the game's handler at
 * 34:0x1db4), UM/PM masked. Shared by the harness start-up and runtime/win87em.c BX=0/2.
 * An inference about the real Win3.1 word, not a measurement (docs/CONVENTIONS.md). */
#define FPU_CW_DEFAULT 0x1330

#define MK_FP(sel, off) ((((uint32_t)(sel)) << 16) | ((uint32_t)(uint16_t)(off)))
#define FP_SEG(fp) ((uint16_t)((uint32_t)(fp) >> 16))
#define FP_OFF(fp) ((uint16_t)((uint32_t)(fp) & 0xFFFFu))

/* GlobalAlloc / LocalAlloc flags (Windows values). */
#define GMEM_FIXED 0x0000
#define GMEM_MOVEABLE 0x0002
#define GMEM_ZEROINIT 0x0040
#define GHND (GMEM_MOVEABLE | GMEM_ZEROINIT)
#define GPTR (GMEM_FIXED | GMEM_ZEROINIT)
#define LMEM_FIXED 0x0000
#define LMEM_ZEROINIT 0x0040

/* Arena. mem_init(0) uses MEM_DEFAULT_ARENA. bytes must be a multiple of 4096,
 * larger than 2 * MEM_HEAP_LINEAR and below 4 GB (aborts otherwise). mem_shutdown frees it. */
void mem_init(size_t bytes);
void mem_shutdown(void);
uint8_t *mem_arena(void);
size_t mem_size(void);
uint8_t *mem_gdt(void);
uint32_t mem_rt_base(void); /* first linear address of the runtime heap */

/* Descriptors. mem_check rejects any range reaching mem_rt_base(). base is an arena offset; limit is inclusive (G=0, up to 0xFFFFF).
 * The low 3 bits of a selector (TI/RPL) are ignored. */
void desc_set(uint16_t sel, uint32_t base, uint32_t limit, uint8_t access);
uint32_t desc_base(uint16_t sel);
uint32_t desc_limit(uint16_t sel);
/* sel_alloc returns the first of count consecutive selectors (step 8), or 0. */
uint16_t sel_alloc(unsigned count);
void sel_free(uint16_t sel, unsigned count);

/* Pointers. LIN/LINN abort with a message naming selector, offset and limit
 * when the byte is out of bounds. mem_check never aborts. */
bool mem_check(uint32_t fp, uint32_t len);
void *LIN(uint32_t fp);
void *LINN(uint16_t near_off); /* DGROUP-relative */

/* Global heap. Handle == selector; GlobalLock returns MK_FP(handle, 0).
 * Blocks over 64 KB get consecutive selectors (step MEM_AHINCR). GlobalReAlloc
 * keeps the handle, but a growth needing more selectors than are free right
 * after the handle fails (returns 0), as does any allocation that does not fit. */
uint16_t GlobalAlloc(uint16_t flags, uint32_t bytes);
uint16_t GlobalReAlloc(uint16_t h, uint32_t bytes, uint16_t flags);
uint16_t GlobalFree(uint16_t h); /* 0 on success, h on failure */
uint32_t GlobalLock(uint16_t h);
bool GlobalUnlock(uint16_t h); /* true while still locked */
uint32_t GlobalSize(uint16_t h); /* 0 on invalid handle */

/* Local heap inside DGROUP. mem_local_init(start, end) sets the heap to the
 * DGROUP offsets [start, end), after the static data and the stack. Only
 * LMEM_FIXED and LMEM_ZEROINIT are supported; any other flag aborts. The KERNEL API
 * wrappers (runtime/kernel.h kernel_LocalAlloc/kernel_LocalReAlloc) drop the no-op flags
 * NOCOMPACT/NODISCARD (and MOVEABLE for realloc) first. */
void mem_local_init(uint16_t start, uint16_t end);
uint16_t LocalAlloc(uint16_t flags, uint16_t bytes);
uint16_t LocalReAlloc(uint16_t off, uint16_t bytes, uint16_t flags);
uint16_t LocalFree(uint16_t off); /* 0 on success */
uint16_t LocalSize(uint16_t off);

/* Runtime-internal allocator. The returned pointer is for immediate use; keep
 * arena offsets (mem_off), never the pointer, inside arena memory. */
void *rt_alloc(size_t bytes);
void rt_free(void *p);
uint32_t mem_off(const void *p);

#endif /* STARS_RUNTIME_MEM_H */

/*
 * (C) 2025-2026 Dennis van der Boon
 */

#ifndef V3D_TYPES_H
#define V3D_TYPES_H

#include <exec/types.h> /* ULONG/UWORD/VOID for LE32/LE16/NOP below */
#include <string.h> /* memcpy, used by v3d_hw.c's v3d_buffer_write via V3D_memcpy */

/*
 * Shared base types for the MiniGLV3D backend. Deliberately independent of
 * Warp3D.h and of MiniGL's gl.h -- the backend must not depend on either,
 * only the other direction (MiniGL depends on the backend).
 *
 * v3d_mem mirrors PoC/v3d_structs.h's struct of the same name. This is
 * the canonical definition -- v3d_device.h's allocator uses it from here,
 * with no dependency on PoC/.
 */

typedef unsigned char  v3d_u8;
typedef unsigned short v3d_u16;
typedef unsigned int   v3d_u32;
typedef int             v3d_i32;

/*
 * v3d_hw.h (lifted from PoC/v3d_v3d.h, the register/CLE-struct layer)
 * takes its base types from this file, so the full base-typedef set is
 * defined here, not just the four above: v3d_hw.h references
 * v3d_address/v3d_uintptr/v3d_u64/etc. The typedefs below come from
 * PoC/v3d_structs.h:4-25.
 */
#ifndef _STDINT_H
typedef unsigned long  uint32_t;
typedef unsigned long  uintptr_t;
#endif
typedef signed int     v3d_int;
typedef unsigned long long v3d_u64;
typedef unsigned long  v3d_uint;
typedef float           v3d_float;
typedef unsigned int   v3d_address;   /* hardware uses 32-bit addresses */
typedef unsigned int   v3d_uintptr;
typedef unsigned short v3d_f187;      /* fixed point, 1 sign + 8 whole + 7 fraction bits */
typedef unsigned int   v3d_u14_8;     /* fixed point unsigned */
typedef unsigned short v3d_u4_8;
typedef signed short   v3d_s8_8;
typedef v3d_u64         v3d_qpu_instruction;

/*
 * LE32/LE16/NOP -- byte-swap and no-op primitives, needed because the V3D
 * GPU is little-endian and the Amiga 68k CPU is big-endian; v3d_hw.c's
 * register reads (e.g. v3d_get_render_frame_count) go through LE32.
 * VBCC inline-asm "pragma" functions, copied verbatim from
 * PoC/v3d_structs.h:79-81 -- Warp3D-free, pure CPU primitives.
 *
 * GCC branch: unlike the LVO stubs in lvocall_compat.h, these have no
 * external calling-convention to match (nothing outside this codebase
 * invokes them via a fixed register) -- so no explicit register
 * pinning is needed, just a plain read-write ("+d") data-register
 * constraint and the same instruction text.
 */
#ifdef __VBCC__
ULONG LE32(__reg("d0")ULONG) = "\trol.w\t#8,d0\n\tswap\td0\n\trol.w\t#8,d0\n";
UWORD LE16(__reg("d0")UWORD) = "\trol.w\t#8,d0\n";
VOID NOP(VOID) = "\tnop\n";
#elif defined(__GNUC__)
static __inline__ ULONG LE32(ULONG v)
{
	__asm__ ("rol.w #8,%0\n\tswap %0\n\trol.w #8,%0" : "+d"(v));
	return v;
}
static __inline__ UWORD LE16(UWORD v)
{
	__asm__ ("rol.w #8,%0" : "+d"(v));
	return v;
}
static __inline__ VOID NOP(VOID)
{
	__asm__ volatile ("nop");
}
#else
#error "v3d_types.h's LE32/LE16/NOP need a __VBCC__ or __GNUC__ branch for this compiler"
#endif

/* Matches PoC/v3d_structs.h's ALIGN_UP macro exactly (align must be a power of 2). */
#define V3D_ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))

typedef struct v3d_mem {
    unsigned size;
    unsigned handle;
    unsigned busaddr;   /* GPU bus address */
    void*    hostptr;   /* CPU-side mapped pointer, for filling the buffer */
} v3d_mem;

/* GPU-side mailbox memory-allocate flags -- v3d_device.c's v3d_mem_alloc needs these. Matches PoC/v3d_structs.h:83-92. */
enum {
    MEM_FLAG_DISCARDABLE = 1 << 0,      /* can be resized to 0 at any time; use for cached data */
    MEM_FLAG_NORMAL = 0 << 2,            /* normal allocating alias; don't use from ARM */
    MEM_FLAG_DIRECT = 1 << 2,            /* 0xC alias uncached */
    MEM_FLAG_COHERENT = 2 << 2,          /* 0x8 alias, non-allocating in L2 but coherent */
    MEM_FLAG_L1_NONALLOCATING = (MEM_FLAG_DIRECT | MEM_FLAG_COHERENT), /* allocating in L2 */
    MEM_FLAG_ZERO = 1 << 4,              /* initialise buffer to all zeros */
    MEM_FLAG_NO_INIT = 1 << 5,           /* don't initialise (default is initialise to all ones) */
    MEM_FLAG_HINT_PERMALOCK = 1 << 6     /* likely to be locked for long periods of time */
};

/*
 * A single append-only control-list buffer (binning list, render list,
 * generic/implicit tile list, or state_buf): PoC's v3d_static_buffer
 * (v3d_structs.h:94-99) plus the overflowed flag.
 */
typedef struct v3d_static_buffer {
    v3d_u8* start;
    int used;
    int capacity;
    /* Sticky per-buffer overflow flag.
     * Most of binning_buf/render_buf/tile_list_buf's packet writers
     * (v3d_commands.c, via V3D_BUFFER_ALLOC_OPERATION) only NULL-check the
     * first field they write -- every field after that dereferences the same pointer
     * unconditionally. When one of those packets overflows,
     * v3d_buffer_claim_memory (v3d_hw.c) hands back a throwaway scratch
     * pointer instead of NULL, so those unconditional writes stay
     * memory-safe, and sets this flag so gl_FramePresent (context.c) can
     * detect it and drop the whole frame instead of submitting a
     * corrupted/truncated control list to the GPU.
     * v3d_u8 (not v3d_bool) -- v3d_bool is typedef'd in v3d_hw.h, which
     * itself includes this file, not the other way around. */
    v3d_u8 overflowed;
} v3d_static_buffer;

#endif /* V3D_TYPES_H */

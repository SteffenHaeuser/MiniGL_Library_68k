/*
 * (C) 2025-2026 Dennis van der Boon
 */

/*
 * DEFAULT implementation of v3d_mem_alloc/v3d_mem_free -- see
 * v3d_device.c's V3D_MEM_USE_MAILBOX comment for the rationale. Backed by
 * AmigaOS AllocVec/FreeVec instead of the RPi mailbox's GPU-memory
 * allocator. The caller must flush the ARM-side CPU cache around V3D's
 * accesses (v3d_context_flush_for_dma, v3d_context.c/h, before each submit;
 * one-time flushes in v3d_context_init for zbuffer/tile_state/tile_alloc,
 * and by the code that writes shader and texture data). Motivation: the
 * mailbox only has ~16MB of GPU memory reserved, a real ceiling as
 * MiniGLV3D's allocations grow; AllocVec'd memory doesn't have that cap.
 *
 * Only the BUILD decides which implementation gets linked in: this one,
 * or the mailbox version (v3d_device.c), the opt-in fallback via
 * V3D_MEM_USE_MAILBOX.
 *
 * Alignment: AllocVec's own guaranteed alignment isn't documented as
 * anything stronger than the platform's natural minimum, while several CL
 * packet fields need more -- e.g. the shader-code address field is
 * right-shifted by 3 (needs 8-byte alignment) and relies on
 * v3d_mem_alloc's own alignment with no extra caller-side re-alignment
 * step (unlike zbuffer/tile_state/tile_alloc, which v3d_context.c
 * over-allocates and re-aligns to 4096 itself, and texture data, which
 * v3d_texture.c over-allocates and re-aligns to 256 itself).
 * V3D_ALLOCVEC_ALIGN (64) is used as a conservative baseline, whatever
 * caller-side re-alignment an allocation does or doesn't do. Achieved via
 * the standard over-allocate-and-align-up technique: AllocVec never
 * accepts an alignment parameter, so the raw (unaligned) pointer AllocVec
 * returns is stashed in m->handle -- like the mailbox version's
 * m->handle, an opaque value v3d_mem_free needs, not something any other
 * code reads -- and the aligned pointer within that block becomes
 * m->hostptr.
 */

#include <exec/execbase.h>
#include <exec/memory.h>
#include <proto/exec.h>

#include "../include/v3d_device.h"
#include "v3d_debug.h"

#define V3D_ALLOCVEC_ALIGN 64

void v3d_mem_free(V3DDevice* device, v3d_mem* m)
{
    struct ExecBase* const SysBase = device->sysbase;

    if (m->handle)
    {
        device->BytesAllocated -= m->size;
        D(("AllocVec-freeing %lu bytes, raw = %lx aligned = %lx\n", m->size, m->handle, (ULONG)m->hostptr));
        FreeVec((APTR)m->handle);
    }
    m->size = 0;
    m->handle = 0;
    m->busaddr = 0;
    m->hostptr = 0;
}

int v3d_mem_alloc(V3DDevice* device, v3d_mem* m, unsigned size)
{
    struct ExecBase* const SysBase = device->sysbase;
    APTR raw;
    ULONG aligned;

    m->size = 0;
    m->handle = 0;
    m->busaddr = 0;
    m->hostptr = 0;

    /* MEMF_CLEAR makes AllocVec zero the whole block, at the source,
     * instead of relying on every v3d_mem_alloc caller to remember its own
     * memset. */
    raw = AllocVec(size + V3D_ALLOCVEC_ALIGN, MEMF_PUBLIC | MEMF_REVERSE | MEMF_CLEAR);
    if (!raw)
    {
        D(("Failed to AllocVec %lu bytes (allocvec test)\n", size));
        return -1;
    }

    aligned = ((ULONG)raw + V3D_ALLOCVEC_ALIGN - 1) & ~(V3D_ALLOCVEC_ALIGN - 1);

    m->size = size;
    m->handle = (unsigned)raw;      /* raw AllocVec pointer -- needed by FreeVec, not by any CL/hardware code */
    m->hostptr = (void*)aligned;    /* the aligned pointer within the raw block */
    m->busaddr = (unsigned)aligned; /* no separate GPU bus-address concept for AllocVec memory; matches hostptr like the mailbox path effectively does after its own BUS_TO_PHYS translation */

    device->BytesAllocated += m->size;
    D(("AllocVec'd %lu bytes, raw = %lx aligned = %lx\n", size, (ULONG)raw, aligned));
    return 0;
}

/*
 * (C) 2025-2026 Dennis van der Boon
 */

#include "../include/v3d_clbuf.h"
#include "v3d_hw.h"
#include "v3d_debug.h"

#include <exec/execbase.h>
#include <proto/exec.h>

void* v3d_cl_claim_grow(V3DDevice* device, v3d_mem* mem, v3d_static_buffer* buf, v3d_uintptr size, V3DFrame* frame)
{
    v3d_mem new_mem;
    v3d_u32 new_capacity;
    int numBytesFree;

    /* This is state_buf's growth path: unlike binning_buf/render_buf/
     * tile_list_buf, which only get v3d_buffer_claim_memory's scratch
     * fallback on overflow, state_buf really grows. The "does this fit"
     * check is made here, from buf->capacity/buf->used:
     * v3d_buffer_claim_memory (v3d_hw.c) never returns NULL for a claim
     * that fits in its 512-byte scratch fallback, so testing its result
     * would never detect the overflow, and the claim would land in the
     * scratch buffer shared by every buffer that overflows instead of
     * growing state_buf. */
    numBytesFree = buf->capacity - buf->used;
    if (numBytesFree >= (int)size)
    {
        return v3d_buffer_claim_memory(buf, size);
    }

    /* Buffer full -- grow. Double, but ensure the new capacity actually
     * fits the requested claim on top of what's already used. */
    new_capacity = (v3d_u32)buf->capacity * 2;
    if (new_capacity < (v3d_u32)buf->used + size)
    {
        new_capacity = (v3d_u32)buf->used + (v3d_u32)size;
    }

    D(("v3d_cl_claim_grow: GROWING buf=%lx old_capacity=%ld used=%ld requested=%ld new_capacity=%ld\n",
       (ULONG)buf, (LONG)buf->capacity, (LONG)buf->used, (LONG)size, (LONG)new_capacity));

    if (v3d_mem_alloc(device, &new_mem, new_capacity) < 0)
    {
        return 0;
    }
    g_mglv3d_state_buf_grows++;

    /* Flush the old block before retiring it. Growth drops the frame at 14
     * of the 20 call sites -- those inside draw.c's two capacity-checked
     * windows, the index-buffer claim and the fragment-uniform sequence of
     * gl_EmitPrimitiveV3DEx. At the other six (draw.c's default-values,
     * position and texcoord claims, and v3d_texture.c's two) the frame is
     * submitted, and every earlier draw's data plus the texture-state cache
     * still point into this old block. v3d_context_flush_for_dma flushes only
     * the block buf->start points to at present time -- the new one -- so
     * without this flush the GPU could read the old block's contents stale
     * from the CPU cache. [start, used) covers every claim ever returned from
     * it (pure bump allocator); used can exceed capacity by up to 31 bytes
     * after AlignBuffer, hence the clamp. Nothing writes into the old block
     * after this: claims are populated immediately, never held across
     * another. */
    {
        struct ExecBase* const SysBase = device->sysbase;
        ULONG len = (ULONG)((buf->used < buf->capacity) ? buf->used : buf->capacity);
        if (len)
            CachePreDMA(buf->start, &len, 0);
    }

    /* The old block's content is not copied: buf->used restarts at 0 and
     * this claim comes from the start of the new block. The old block is
     * handed to v3d_frame_defer_free instead of being freed here: it goes on
     * the retire list and is released once the frame's render has completed
     * (see v3d_frame.h). */
    /* No explicit memset here -- v3d_mem_alloc's own AllocVec call already
     * uses MEMF_CLEAR, so this memory arrives pre-zeroed. */
    v3d_frame_defer_free(frame, mem);
    *mem = new_mem;

    buf->start = (v3d_u8*)mem->hostptr;
    buf->capacity = (int)mem->size; /* use the allocated size v3d_mem_alloc recorded, not our requested new_capacity: the V3D_MEM_USE_MAILBOX v3d_mem_alloc rounds it up to 16 bytes */
    buf->used = 0;

    return v3d_buffer_claim_memory(buf, size);
}

void v3d_cl_reset(v3d_mem* mem, v3d_static_buffer* buf)
{
    buf->used = 0;
    buf->capacity = (int)mem->size;
    buf->overflowed = 0;
}

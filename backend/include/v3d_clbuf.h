/*
 * (C) 2025-2026 Dennis van der Boon
 */

#ifndef V3D_CLBUF_H
#define V3D_CLBUF_H

#include "v3d_types.h"
#include "v3d_device.h"
#include "v3d_frame.h"

/*
 * Growable control-list buffer append. Wraps v3d_buffer_claim_memory
 * (v3d_hw.c, a fixed-capacity bump allocator) with grow-on-overflow. It
 * checks the fit itself; when the claim does not fit, it allocates a
 * bigger block and claims from the start of it. Nothing already claimed
 * is copied into the new block.
 *
 * Callers must fully populate a claimed struct immediately after claiming
 * it, never hold the pointer across another claim on the same buffer.
 * Growing reallocates the buffer's backing memory, which would invalidate
 * an earlier claim's pointer if one were still in use when growth
 * happens.
 *
 * v3d_cl_reset takes both *mem and *buf (not just *buf) because it must
 * restore buf->capacity from mem->size, not just reset buf->used to 0.
 *
 * AlignBuffer (v3d_commands.c, originally ported verbatim from the PoC)
 * must only advance buffer->used, like v3d_buffer_align, and never touch
 * capacity: a shrinking capacity makes v3d_cl_claim_grow's capacity check
 * fail and reallocate a buffer that did not need to grow.
 *
 * The old block is not freed immediately: v3d_frame_defer_free puts it on
 * the retire list, which releases it once the frame's render has completed
 * -- see v3d_frame.h. An EARLIER draw call in the same frame may already have
 * baked a busaddr inside the old block into a CL packet, and the GPU does
 * not consume that packet until the frame's binning+render are submitted
 * and waited on, so the old block must stay valid until then. Freeing it
 * immediately would let AmigaOS hand that memory to something else while
 * the GPU could still DMA into/out of it via the stale address,
 * corrupting whatever now occupied it.
 */

/* Claims `size` bytes from *buf (backed by *mem), growing *mem/*buf first if the buffer is full. Returns 0 if growth itself fails (real out-of-memory). frame is where the old block's free gets deferred to (see v3d_frame_defer_free). */
void* v3d_cl_claim_grow(V3DDevice* device, v3d_mem* mem, v3d_static_buffer* buf, v3d_uintptr size, V3DFrame* frame);

/* v3d_cl_claim_grow with its fits-case inline: the same test, and the same
 * arithmetic its v3d_buffer_claim_memory call performs. A claim that does not
 * fit calls v3d_cl_claim_grow, which tests again and grows. */
#ifdef __GNUC__
static __inline__ void* v3d_cl_claim_fast(V3DDevice* device, v3d_mem* mem, v3d_static_buffer* buf, v3d_uintptr size, V3DFrame* frame)
{
    if (buf->capacity - buf->used >= (int)size)
    {
        void* data = buf->start + buf->used;
        buf->used += size;
        return data;
    }
    return v3d_cl_claim_grow(device, mem, buf, size, frame);
}
#endif

/* Resets a buffer to empty AND restores capacity to mem's true allocated size -- call at the start of each frame. Does NOT free/reallocate (a buffer that grew stays grown, at its new size). */
void v3d_cl_reset(v3d_mem* mem, v3d_static_buffer* buf);

#endif /* V3D_CLBUF_H */

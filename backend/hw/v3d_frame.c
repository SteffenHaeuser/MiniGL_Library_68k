/*
 * (C) 2025-2026 Dennis van der Boon
 */

#include "../include/v3d_frame.h"
#include "v3d_hw.h"
#include "v3d_debug.h"

#include <exec/execbase.h>
#include <proto/exec.h>

static ULONG divRoundUp(ULONG a, ULONG b)
{
    return (a + b - 1) / b;
}

/* Default D32F. Set only by mglChooseZBufferDepth and read only by
 * v3d_context_init, so a new value takes effect at the next context
 * creation -- see the declaration in v3d_frame.h for why it must be a
 * pre-context global and why it is named Z-BUFFER depth, not screen depth. */
int g_v3d_requested_zbuffer_bits = 32;

/* Monotonic CL-restart counter. Bumped at EVERY build_slot flip
 * (gl_FramePresent and the doing_intermediate_pass path, context.c) and used
 * as the invalidation key for every cache that holds addresses into a CL
 * buffer.
 *
 * PROCESS-GLOBAL, NOT a context field, and NEVER reset -- both properties are
 * load-bearing:
 *
 *  - build_slot is a 2-slot toggle, so an intermediate render pass that flips
 *    it mid-frame with no draws after it hands the next frame the same slot
 *    value. A cache keyed on slot parity then believes its addresses are still
 *    live when the buffer has been reset underneath them.
 *  - A per-context counter would restart at 0 when a context is destroyed and
 *    re-created, while draw.c's cache statics survive, so a fresh context
 *    could match a stale key and reuse addresses into the destroyed
 *    context's state_buf.
 *
 * Monotonic and global means no cache can ever observe a repeated value. */
v3d_u32 g_v3d_cl_generation = 0;

void v3d_frame_compute_pool_sizes(v3d_u16 width, v3d_u16 height,
                                   int zbuffer_bits,
                                   v3d_u32* zbuffer_size,
                                   v3d_u32* tile_state_size,
                                   v3d_u32* tile_alloc_size,
                                   v3d_u32* tilesX,
                                   v3d_u32* tilesY)
{
    const v3d_u32 tile_width = 64;
    const v3d_u32 tile_height = 64;
    const v3d_u32 layer_count = 1;         /* no layered rendering yet */
    const v3d_u32 tsda_per_tile_size = 256; /* 64 on V3D ver < 40; this project targets V3D 4.2 */
    v3d_u32 size;

    *tilesX = divRoundUp(width, tile_width);
    *tilesY = divRoundUp(height, tile_height);

    /* zbuffer: matches PoC/v3d_cle.c:634's shape.
     *
     * The trailing factor is BYTES PER PIXEL of the depth format, and it must
     * track V3D_OUTPUT_IMAGE_FORMAT_* / V3D_INTERNAL_TYPE_DEPTH* in
     * gl_FramePresent (context.c): 2 for D16, 4 for D32F.
     *
     * Why D32F rather than D16: D16 loses a 1.0 world-unit separation between
     * z=300 and z=1000 under a typical first-person game frustum -- well
     * inside real map scale.
     *
     * Why D32F and not D24, which is the same 4 bytes: on precision the two
     * are close -- a float has 2^23 values in [0.5,1) and D24 has exactly the
     * same 2^24/2 there, so they are IDENTICAL across the far field where
     * perspective depth crowds everything; the float's extra
     * precision all sits near 0, in the near field that already had plenty.
     * D32F is equal-or-better everywhere and keeps the door open for
     * reversed-Z later (clear to 0, GEQUAL, far->0), which is where a float
     * buffer genuinely beats an integer one -- that would be a much larger
     * change, touching the depth convention in the driver and every game.
     *
     * PLATFORM PRECEDENT: D16 is BELOW the Amiga baseline, not at it.
     * Warp3D's R200 driver allocates 4 bytes per pixel (24-bit Z, 8-bit
     * stencil) by default and only drops to 16 when an env/config flag asks
     * or an app requests W3D_H_ZBUFFER with W3D_H_FAST, i.e. 16-bit is the
     * explicit DEGRADED option. Warp3D's software renderer uses a float
     * buffer outright. Base MiniGL never sets W3D_H_ZBUFFER at all, so every
     * MiniGL app gets the driver default -- 24-bit on R200 hardware. So a
     * 4-byte Z buffer matches the platform norm rather than exceeding it.
     *
     * The V3D_ALIGN_UP(...,64) terms are the tile alignment and are NOT
     * optional. They are independent of the depth format.
     *
     * The tiling divisor is the same for both formats:
     * strideorub is align(height,8)/8, derived from
     * 2 * v3d_utile_height(cpp), and MESA's v3d_utile_height returns 4 for
     * BOTH cpp=2 and cpp=4 -- so the UIF block height is 8 either way. Only
     * v3d_utile_width differs (8 for cpp=2, 4 for cpp=4), which the hardware
     * handles itself. */
    *zbuffer_size = V3D_ALIGN_UP(width, 64) * V3D_ALIGN_UP(height, 64)
                    * (v3d_u32)((zbuffer_bits == 16) ? 2 : 4);

    /* tile_state (TSDA): purely tile-count-dependent, matches PoC/v3d_cle.c:636 exactly */
    *tile_state_size = layer_count * (*tilesY) * (*tilesX) * tsda_per_tile_size;

    /* tile_alloc: matches MESA's alloc_tile_state() exactly --
     * NOT PoC/v3d_cle.c:629-633, which adds an extra "+= 1024*1024" on top of this same
     * formula. That extra margin isn't needed once OOM spill handling exists (see v3d_frame.h). */
    size = layer_count * (*tilesX) * (*tilesY) * 64;
    size = V3D_ALIGN_UP(size, 4096);
    size += 8192;
    size += 512 * 1024;
    *tile_alloc_size = size;
}

v3d_u32 g_mglv3d_bin_spills = 0;
v3d_u32 g_mglv3d_bin_spill_fails = 0;
v3d_u32 g_mglv3d_state_buf_grows = 0;
v3d_u32 g_mglv3d_frames_dropped = 0;
v3d_u32 g_mglv3d_retire_leaks = 0;

/* RETIRE LIST -- blocks the GPU may still read: spill blocks
 * handed to the binner, and state_buf blocks replaced by v3d_cl_claim_grow.
 * Each carries the sequence number of the job that uses it (s_build_seq when
 * it was added); v3d_frame_retire releases it once a render wait for that job
 * or a later one has SUCCEEDED -- the texture park list's rule and place
 * (v3d_texture_drain_parked, MGLFlushPendingRender).
 *
 * Why a sequence tag and not "everything on the list": the frame being BUILT
 * adds deferred blocks, and MGLFlushPendingRender can run mid-build (GLFinish,
 * readback); those blocks belong to a job not yet submitted and must stay.
 * A dropped frame never increments s_build_seq, so its blocks simply retire
 * with the next submitted job -- nothing names them, so that is safe.
 *
 * Process-global and never reset, like g_v3d_cl_generation and the texture
 * park list: the driver is single-context, and v3d_frame_end empties the list
 * at teardown. 32 entries: at most 8 spills per job plus a few state_buf
 * growths (each doubles the buffer, so they are rare). */
#define V3D_RETIRE_MAX 32
typedef struct {
    v3d_mem mem;
    v3d_u32 seq;
    int     is_spill;
} v3d_retire_entry;

static v3d_retire_entry s_retire[V3D_RETIRE_MAX];
static int     s_retire_count = 0;
static v3d_u32 s_build_seq = 1;   /* job the blocks being added now belong to */
static v3d_u32 s_render_seq = 0;  /* last job whose render was submitted */
static int     s_job_spills = 0;  /* spills given to the current binning job */

/* A new spill block: 4096-aligned like the tile_alloc pool it extends, and
 * cache-flushed ONCE, over the whole allocation before alignment moves
 * hostptr -- the same order as v3d_context_init's pools. Without the flush,
 * dirty lines from AllocVec's MEMF_CLEAR could later be evicted over tile
 * lists the binner has written. The CPU never touches the block again, so
 * pooled reuse needs no flush. */
static int alloc_spill_block(V3DDevice* device, v3d_mem* m)
{
    struct ExecBase* const SysBase = device->sysbase;
    v3d_uintptr raw, aligned;
    v3d_u32 delta;

    if (v3d_mem_alloc(device, m, V3D_SPILL_BLOCK_SIZE + 4096) < 0)
        return 0;
    {
        ULONG len = (ULONG)m->size;
        CachePreDMA(m->hostptr, &len, 0);
    }
    raw     = (v3d_uintptr)m->hostptr;
    aligned = V3D_ALIGN_UP(raw, 4096);
    delta   = (v3d_u32)(aligned - raw);
    m->busaddr = V3D_ALIGN_UP(m->busaddr, 4096);
    m->hostptr = (void*)aligned;
    m->size   -= delta;                  /* v3d_mem_free subtracts ->size */
    device->BytesAllocated -= delta;
    return 1;
}

void v3d_frame_begin(V3DFrame* frame, v3d_u32 tilesX, v3d_u32 tilesY)
{
    frame->tilesX = tilesX;
    frame->tilesY = tilesY;
    frame->spill_count = 0;
    s_job_spills = 0;
}

void v3d_frame_end(V3DDevice* device, V3DFrame* frame)
{
    int i;
    for (i = 0; i < s_retire_count; i++)
    {
        v3d_mem_free(device, &s_retire[i].mem);
    }
    s_retire_count = 0;
    for (i = 0; i < frame->spill_count; i++)
    {
        v3d_mem_free(device, &frame->spill_blocks[i]);
    }
    frame->spill_count = 0;
    s_job_spills = 0;
}

void v3d_frame_binning_job_begin(V3DFrame* frame)
{
    (void)frame;
    s_job_spills = 0;
}

void v3d_frame_render_submitted(V3DFrame* frame)
{
    (void)frame;
    s_render_seq = s_build_seq++;
}

void v3d_frame_retire(V3DDevice* device, V3DFrame* frame)
{
    int i = 0;
    while (i < s_retire_count)
    {
        if ((int)(s_retire[i].seq - s_render_seq) <= 0)
        {
            v3d_mem m = s_retire[i].mem;
            int spill = s_retire[i].is_spill;

            s_retire[i] = s_retire[--s_retire_count];   /* swap-remove; re-test slot i */
            if (spill && frame->spill_count < V3D_MAX_SPILL_BLOCKS)
                frame->spill_blocks[frame->spill_count++] = m;
            else
                v3d_mem_free(device, &m);
            continue;
        }
        i++;
    }
}

v3d_mem* v3d_frame_add_spill_block(V3DDevice* device, V3DFrame* frame)
{
    v3d_retire_entry* e;

    if (s_job_spills >= V3D_MAX_SPILL_BLOCKS || s_retire_count >= V3D_RETIRE_MAX)
    {
        g_mglv3d_bin_spill_fails++;
        return 0;
    }

    e = &s_retire[s_retire_count];
    if (frame->spill_count > 0)
    {
        e->mem = frame->spill_blocks[--frame->spill_count];
    }
    else if (!alloc_spill_block(device, &e->mem))
    {
        g_mglv3d_bin_spill_fails++;
        return 0;
    }
    e->seq = s_build_seq;
    e->is_spill = 1;
    s_retire_count++;
    s_job_spills++;
    g_mglv3d_bin_spills++;
    return &e->mem;
}

void v3d_frame_defer_free(V3DFrame* frame, v3d_mem* mem)
{
    (void)frame;
    if (s_retire_count >= V3D_RETIRE_MAX)
    {
        g_mglv3d_retire_leaks++;
        E(("v3d_frame_defer_free: retire list full -- leaking a %lu-byte block rather than freeing it early\n",
           (ULONG)mem->size));
        return;
    }
    s_retire[s_retire_count].mem = *mem;
    s_retire[s_retire_count].seq = s_build_seq;
    s_retire[s_retire_count].is_spill = 0;
    s_retire_count++;
}

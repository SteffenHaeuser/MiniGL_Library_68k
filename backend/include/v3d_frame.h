/*
 * (C) 2025-2026 Dennis van der Boon
 */

#ifndef V3D_FRAME_H
#define V3D_FRAME_H

#include "v3d_types.h"
#include "v3d_device.h"

/*
 * MiniGLV3D -- persistent per-context GPU memory pools + binner
 * out-of-memory (OOM) spill handling.
 *
 * Sizing formulas for zbuffer/tile_state/tile_alloc match PoC/v3d_cle.c's
 * CL_Draw_Triangle (lines ~627-636) with two deliberate changes: the
 * zbuffer's bytes-per-pixel factor follows zbuffer_bits (the PoC fixes it
 * at 2; see v3d_frame_compute_pool_sizes), and tile_alloc's size drops the
 * PoC's extra "+= 1024*1024" margin, matching MESA's own
 * production alloc_tile_state(), which uses exactly tilesX*tilesY*64,
 * align(4096), +8192, +512*1024, with no further margin; MESA's comments
 * explain each term (8192 = the PTB's first two automatic chunk allocations,
 * so OOM isn't hit immediately even with zero real geometry; 512*1024 = perf
 * margin, not correctness). Real robustness beyond that initial size comes
 * from the OOM-interrupt spill mechanism below, not from over-provisioning
 * upfront -- so the PoC's extra 1MB isn't needed once spill handling exists.
 *
 * OOM handling: V3D_CTL_INT_STS bit V3D_INT_OUTOMEM (v3d_regs.h, cross-
 * referenced against PoC/linuxregs.c's Linux kernel v3d register header)
 * signals the binner ran out of tile allocation memory mid-frame. Response
 * (confirmed against MESA's own ISR, src/broadcom/simulator/v3dx_simulator.c
 * v3d_isr_core): allocate a new block, write its address/size to
 * V3D_BPOA/V3D_BPOS, and the binner resumes automatically -- this can chain
 * (if the spill itself overflows, the same interrupt fires again). MESA's
 * ISR also calls a GMP (GPU Memory Protection -- a V3D-hardware-internal
 * feature, unrelated to AmigaOS's own lack of memory protection) reload
 * step here; that's not replicated since GMP_CFG's PROT_ENABLE bit is never
 * set anywhere in v3d_init/power_on_V3D, so GMP filtering should be at its
 * power-on-disabled default -- a documented assumption, not a certainty.
 */

#define V3D_SPILL_BLOCK_SIZE (256 * 1024)  /* matches MESA's own chosen chunk size */
#define V3D_MAX_SPILL_BLOCKS 8               /* spill blocks one binning job may take; past it, fail cleanly rather than loop forever. Also the size of V3DFrame's free-block pool. */

/* spill_blocks/spill_count hold the POOL of FREE spill blocks kept for reuse.
 * The blocks in use are on the retire list in v3d_frame.c until the GPU is
 * done with them. V3DFrame sits inside V3DContext, inside GLcontext_t, whose
 * layout only ever grows at the end. */
typedef struct V3DFrame {
    v3d_u32 tilesX, tilesY;

    v3d_mem spill_blocks[V3D_MAX_SPILL_BLOCKS];
    int spill_count;
} V3DFrame;

/* Event counters, monotonic, never reset. For diagnostics and hardware tests. */
extern v3d_u32 g_mglv3d_bin_spills;      /* spill blocks handed to the binner */
extern v3d_u32 g_mglv3d_bin_spill_fails; /* OOM that could not be served: the job's binning fails */
extern v3d_u32 g_mglv3d_state_buf_grows; /* v3d_cl_claim_grow reallocations */
extern v3d_u32 g_mglv3d_frames_dropped;  /* gl_FramePresent drop exits (never submitted) */
extern v3d_u32 g_mglv3d_retire_leaks;    /* blocks not tracked because the retire list was full */

/* Requested Z-BUFFER precision in bits -- 32 (D32F, the default) or 16 (D16).
 *
 * NOTE this is Z-buffer depth, NOT screen depth. "Depth" is overloaded on
 * AmigaOS, where SA_Depth/CYBRMATTR_DEPTH mean the screen's bits-per-pixel.
 * The public setter, mglChooseZBufferDepth, has "ZBuffer" in its name for
 * the same reason.
 *
 * Read once by v3d_context_init and copied into V3DContext::zbuffer_bits;
 * everything after that reads the context field, never this. It has to be a
 * pre-context global because the zbuffer is ALLOCATED during context init, so
 * the format must be known before a context exists -- the same reason GLUT's
 * glutInitDisplayMode/glutInitDisplayString must precede glutCreateWindow.
 *
 * D16 is offered because callers may want the memory, but it is a downgrade,
 * BELOW the Warp3D baseline this driver replaces (see
 * v3d_frame_compute_pool_sizes). D32F is the default. */
extern int g_v3d_requested_zbuffer_bits;

/* Computes zbuffer/tile_state/tile_alloc sizes and tile counts for a width x
 * height draw region. zbuffer_bits selects the Z format (16 or 32). */
void v3d_frame_compute_pool_sizes(v3d_u16 width, v3d_u16 height,
                                   int zbuffer_bits,
                                   v3d_u32* zbuffer_size,
                                   v3d_u32* tile_state_size,
                                   v3d_u32* tile_alloc_size,
                                   v3d_u32* tilesX,
                                   v3d_u32* tilesY);

/* Spill blocks and replaced state_buf blocks go on a retire list, tagged with
 * the sequence number of the job that uses them; v3d_frame_retire releases
 * them after a SUCCESSFUL render wait for that job -- the rule and place of
 * texture renaming's parked memory (MGLFlushPendingRender, context.c). Spill
 * blocks go back to the frame's pool for the next job; old state_buf blocks
 * are freed. */

/* Records tilesX/tilesY and empties the spill pool. Called only from
 * v3d_context_init. Does NOT touch the persistent pools -- those live in
 * V3DContext and are sized/allocated once at context-create/resize. */
void v3d_frame_begin(V3DFrame* frame, v3d_u32 tilesX, v3d_u32 tilesY);

/* Frees every block on the retire list and in the spill pool, whatever the
 * GPU state (teardown frees the pools they point into anyway). Idempotent.
 * Called only from v3d_context_free. */
void v3d_frame_end(V3DDevice* device, V3DFrame* frame);

/* Start of a binning job: resets the per-job spill count. */
void v3d_frame_binning_job_begin(V3DFrame* frame);

/* A render job was just submitted: blocks tagged so far belong to it. */
void v3d_frame_render_submitted(V3DFrame* frame);

/* After a SUCCESSFUL render wait: release every block of the waited-for job
 * or earlier. Never call it after a failed wait -- the job may still run. */
void v3d_frame_retire(V3DDevice* device, V3DFrame* frame);

/*
 * Hands out one spill block for the binner (from the pool, or newly
 * allocated: 4096-aligned like tile_alloc, and cache-flushed once) and puts
 * it on the retire list. Returns NULL if this binning job already has
 * V3D_MAX_SPILL_BLOCKS, the retire list is full, or the allocation fails --
 * caller should treat this as a hard binning failure (see
 * v3d_wait_binning_timeout, v3d_submit_timeout.c). The returned pointer is
 * valid until the next v3d_frame_retire.
 */
v3d_mem* v3d_frame_add_spill_block(V3DDevice* device, V3DFrame* frame);

/*
 * Retires an already-allocated block (state_buf's old backing memory after
 * v3d_cl_claim_grow reallocates it) instead of freeing it immediately: an
 * EARLIER draw call in the frame being built may already have baked an
 * address inside it into a CL packet, and the GPU does not read that packet
 * until the frame is submitted, so the block must stay valid until that
 * job's render has completed. If the retire list is full the block is
 * leaked (counted in g_mglv3d_retire_leaks), never freed early.
 */
void v3d_frame_defer_free(V3DFrame* frame, v3d_mem* mem);

#endif /* V3D_FRAME_H */

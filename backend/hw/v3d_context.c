/*
 * (C) 2025-2026 Dennis van der Boon
 */

#include "../include/v3d_context.h"
#include "v3d_debug.h"
#include "v3d_submit_timeout.h"

#include <exec/execbase.h>
#include <proto/exec.h>

/*
 * zbuffer/tile_state/tile_alloc need their base address 4096-aligned.
 * Matches PoC/v3d_cle.c:674-680 exactly: over-allocate by 4096 bytes (done
 * by the caller, requesting size+4096) then align the returned
 * busaddr/hostptr up within that slack. v3d_mem_free needs only ->handle to
 * release the block (see v3d_mem_allocvec.c, and v3d_device.c for the
 * V3D_MEM_USE_MAILBOX build), so aligning busaddr/hostptr in place
 * here is safe -- it doesn't break the ability to free the allocation later.
 * Binning/render/tile-list CL buffers do NOT need this: PoC's own bcl
 * pointer (v3d_cle.c:682) gets no alignment adjustment.
 */
/*
 * ->size must shrink by however far hostptr moved, so that it describes the
 * region starting at the NEW hostptr. The tile-allocation pool goes to the
 * binner as base=hostptr, length=->size (CT0QMA/CT0QMS, see
 * v3d_submit_binning's caller), so an unchanged ->size could run past the
 * end of the real AllocVec block and hand the binner memory the driver does
 * not own.
 *
 * The subtraction is exact rather than conservative: hostptr + (size - delta)
 * == old_hostptr + size, which is inside the block by construction. Callers
 * request their real requirement + 4096 (see v3d_context_init), so what
 * survives here is at least what they asked for.
 *
 * BytesAllocated is decremented by the same delta to keep the books balanced:
 * v3d_mem_alloc added the full size and v3d_mem_free subtracts ->size, which is
 * now smaller, so without this the shutdown "Still %lu bytes allocated!" check
 * (v3d_device.c) would report a phantom leak.
 *
 * busaddr is aligned independently of hostptr. On the AllocVec path the two
 * are the same value (v3d_mem_allocvec.c sets both to `aligned`), so one delta describes
 * both; the mailbox path could in principle differ, and ->size follows
 * hostptr, which is what every CL consumer here actually uses.
 */
static void align_mem_4096(V3DDevice* device, v3d_mem* m)
{
    v3d_uintptr raw     = (v3d_uintptr)m->hostptr;
    v3d_uintptr aligned = V3D_ALIGN_UP(raw, 4096);
    v3d_u32     delta   = (v3d_u32)(aligned - raw);

    m->busaddr = V3D_ALIGN_UP(m->busaddr, 4096);
    m->hostptr = (void*)aligned;

    if (m->size > delta)
        m->size -= delta;
    else
        m->size = 0;

    if (device)
        device->BytesAllocated -= delta;
}

static void free_partial(V3DDevice* device, V3DContext* context, int stage)
{
    /* stage = how many allocations succeeded before one failed; unwind
     * exactly those, in reverse. Each of the 4 CL buffers allocates 2
     * slots, and each slot has its own stage number. */
    if (stage > 10) v3d_mem_free(device, &context->state_mem[1]);
    if (stage > 9)  v3d_mem_free(device, &context->state_mem[0]);
    if (stage > 8)  v3d_mem_free(device, &context->tile_list_mem[1]);
    if (stage > 7)  v3d_mem_free(device, &context->tile_list_mem[0]);
    if (stage > 6)  v3d_mem_free(device, &context->render_mem[1]);
    if (stage > 5)  v3d_mem_free(device, &context->render_mem[0]);
    if (stage > 4)  v3d_mem_free(device, &context->binning_mem[1]);
    if (stage > 3)  v3d_mem_free(device, &context->binning_mem[0]);
    if (stage > 2) v3d_mem_free(device, &context->tile_alloc_mem);
    if (stage > 1) v3d_mem_free(device, &context->tile_state_mem);
    if (stage > 0) v3d_mem_free(device, &context->zbuffer_mem);
}

int v3d_context_init(V3DContext* context, V3DDevice* device, v3d_u16 width, v3d_u16 height)
{
    v3d_u32 zbuffer_size, tile_state_size, tile_alloc_size, tilesX, tilesY;

    context->framebuffer = 0;
    context->width = width;
    context->height = height;
    context->y_offset = 0;

    /* Resolution-scaled render-wait bound: render cost scales with the tile
     * count, so the bound does too -- see v3d_submit_timeout.h. */
    g_v3d_render_timeout_iterations = ((v3d_u32)width * (v3d_u32)height) >> 2;

    D(("v3d_context_init: enter, width=%ld height=%ld render_timeout_iterations=%lu\n", (LONG)width, (LONG)height, (ULONG)g_v3d_render_timeout_iterations));

    /* Latch the requested Z-buffer precision ONCE, here, and validate it: the
     * pools below are sized from it and cannot be re-sized later. Anything
     * that is not exactly 16 falls back to D32F, so a caller passing garbage
     * gets the safe, higher-precision format rather than an undersized
     * zbuffer -- an undersized one would be a GPU write past the allocation. */
    context->zbuffer_bits = (g_v3d_requested_zbuffer_bits == 16) ? 16 : 32;

    v3d_frame_compute_pool_sizes(width, height, context->zbuffer_bits,
                                  &zbuffer_size, &tile_state_size,
                                  &tile_alloc_size, &tilesX, &tilesY);

    D(("v3d_context_init: zbuffer_bits=%ld zbuffer_size=%lu tile_state_size=%lu tile_alloc_size=%lu tilesX=%ld tilesY=%ld\n",
       (LONG)context->zbuffer_bits,
       (ULONG)zbuffer_size, (ULONG)tile_state_size, (ULONG)tile_alloc_size, (LONG)tilesX, (LONG)tilesY));

    /* +4096 on each: slack for align_mem_4096 below, matching PoC/v3d_cle.c:615-654's
     * "size + 4096" allocations exactly.
     *
     * Every pool must be zeroed right after allocation, before alignment adjusts
     * the start pointer -- matches PoC's own memset(bcl, 0, 4*4096) (v3d_cle.c:709).
     * Memory that is not cleared can carry stale content across separate
     * program runs on this hardware. The default v3d_mem_alloc (AllocVec,
     * v3d_mem_allocvec.c) does the zeroing with MEMF_CLEAR, so no pool below
     * needs its own memset; the V3D_MEM_USE_MAILBOX allocator (v3d_device.c)
     * does not request zeroed memory (no MEM_FLAG_ZERO). */
    if (v3d_mem_alloc(device, &context->zbuffer_mem, zbuffer_size + 4096) < 0)
    {
        E(("v3d_context_init: zbuffer_mem alloc FAILED (size=%lu) -- returning -1\n", (ULONG)(zbuffer_size + 4096)));
        return -1;
    }
    D(("v3d_context_init: zbuffer_mem alloc OK\n"));
    /* No explicit memset here -- v3d_mem_alloc's own AllocVec call already
     * uses MEMF_CLEAR, so this memory arrives pre-zeroed. */
    /* One-time flush (see v3d_context_flush_for_dma's comment, v3d_context.h)
     * -- the CPU never writes to this again, V3D owns it as scratch from
     * here on, covering the whole zeroed block before alignment moves
     * hostptr within it. Local `len` (not &context->...->size directly)
     * matches mbox_transaction's own established pattern (v3d_device.c) --
     * CachePreDMA's length parameter is non-const, so passing a pointer
     * straight into the struct risks it being overwritten. */
    {
        ULONG len = (ULONG)context->zbuffer_mem.size;
        CachePreDMA(context->zbuffer_mem.hostptr, &len, 0);
    }
    align_mem_4096(device, &context->zbuffer_mem);

    if (v3d_mem_alloc(device, &context->tile_state_mem, tile_state_size + 4096) < 0)
    {
        E(("v3d_context_init: tile_state_mem alloc FAILED (size=%lu) -- returning -2\n", (ULONG)(tile_state_size + 4096)));
        free_partial(device, context, 1);
        return -2;
    }
    D(("v3d_context_init: tile_state_mem alloc OK\n"));
    /* No explicit memset here -- v3d_mem_alloc's own AllocVec call already
     * uses MEMF_CLEAR, so this memory arrives pre-zeroed. */
    {
        ULONG len = (ULONG)context->tile_state_mem.size;
        CachePreDMA(context->tile_state_mem.hostptr, &len, 0);
    }
    align_mem_4096(device, &context->tile_state_mem);

    if (v3d_mem_alloc(device, &context->tile_alloc_mem, tile_alloc_size + 4096) < 0)
    {
        E(("v3d_context_init: tile_alloc_mem alloc FAILED (size=%lu) -- returning -3\n", (ULONG)(tile_alloc_size + 4096)));
        free_partial(device, context, 2);
        return -3;
    }
    D(("v3d_context_init: tile_alloc_mem alloc OK\n"));
    /* No explicit memset here -- v3d_mem_alloc's own AllocVec call already
     * uses MEMF_CLEAR, so this memory arrives pre-zeroed. */
    {
        ULONG len = (ULONG)context->tile_alloc_mem.size;
        CachePreDMA(context->tile_alloc_mem.hostptr, &len, 0);
    }
    align_mem_4096(device, &context->tile_alloc_mem);

    /* Each of these 4 buffers allocates 2 slots -- see v3d_context.h's own
     * comment on binning_buf etc. gl_FramePresent alternates build_slot
     * between them every frame, so both slots are allocated and initialised
     * here, up front, and the flip itself never allocates. */
    if (v3d_mem_alloc(device, &context->binning_mem[0], V3D_INITIAL_CL_BUFFER_SIZE) < 0)
    {
        E(("v3d_context_init: binning_mem[0] alloc FAILED (size=%lu) -- returning -4\n", (ULONG)V3D_INITIAL_CL_BUFFER_SIZE));
        free_partial(device, context, 3);
        return -4;
    }
    D(("v3d_context_init: binning_mem[0] alloc OK\n"));
    /* No explicit memset here -- v3d_mem_alloc's own AllocVec call already
     * uses MEMF_CLEAR, so this memory arrives pre-zeroed. */

    if (v3d_mem_alloc(device, &context->binning_mem[1], V3D_INITIAL_CL_BUFFER_SIZE) < 0)
    {
        E(("v3d_context_init: binning_mem[1] alloc FAILED -- returning -4\n"));
        free_partial(device, context, 4);
        return -4;
    }
    D(("v3d_context_init: binning_mem[1] alloc OK\n"));
    /* No explicit memset here -- v3d_mem_alloc's own AllocVec call already
     * uses MEMF_CLEAR, so this memory arrives pre-zeroed. */

    if (v3d_mem_alloc(device, &context->render_mem[0], V3D_INITIAL_RENDER_BUFFER_SIZE) < 0)
    {
        E(("v3d_context_init: render_mem[0] alloc FAILED -- returning -5\n"));
        free_partial(device, context, 5);
        return -5;
    }
    D(("v3d_context_init: render_mem[0] alloc OK\n"));
    /* No explicit memset here -- v3d_mem_alloc's own AllocVec call already
     * uses MEMF_CLEAR, so this memory arrives pre-zeroed. */

    if (v3d_mem_alloc(device, &context->render_mem[1], V3D_INITIAL_RENDER_BUFFER_SIZE) < 0)
    {
        E(("v3d_context_init: render_mem[1] alloc FAILED -- returning -5\n"));
        free_partial(device, context, 6);
        return -5;
    }
    D(("v3d_context_init: render_mem[1] alloc OK\n"));
    /* No explicit memset here -- v3d_mem_alloc's own AllocVec call already
     * uses MEMF_CLEAR, so this memory arrives pre-zeroed. */

    if (v3d_mem_alloc(device, &context->tile_list_mem[0], V3D_INITIAL_TILE_LIST_BUFFER_SIZE) < 0)
    {
        E(("v3d_context_init: tile_list_mem[0] alloc FAILED -- returning -6\n"));
        free_partial(device, context, 7);
        return -6;
    }
    D(("v3d_context_init: tile_list_mem[0] alloc OK\n"));
    /* No explicit memset here -- v3d_mem_alloc's own AllocVec call already
     * uses MEMF_CLEAR, so this memory arrives pre-zeroed. */

    if (v3d_mem_alloc(device, &context->tile_list_mem[1], V3D_INITIAL_TILE_LIST_BUFFER_SIZE) < 0)
    {
        E(("v3d_context_init: tile_list_mem[1] alloc FAILED -- returning -6\n"));
        free_partial(device, context, 8);
        return -6;
    }
    D(("v3d_context_init: tile_list_mem[1] alloc OK\n"));
    /* No explicit memset here -- v3d_mem_alloc's own AllocVec call already
     * uses MEMF_CLEAR, so this memory arrives pre-zeroed. */

    if (v3d_mem_alloc(device, &context->state_mem[0], V3D_INITIAL_CL_BUFFER_SIZE) < 0)
    {
        E(("v3d_context_init: state_mem[0] alloc FAILED -- returning -7\n"));
        free_partial(device, context, 9);
        return -7;
    }
    D(("v3d_context_init: state_mem[0] alloc OK\n"));
    /* No explicit memset here -- v3d_mem_alloc's own AllocVec call already
     * uses MEMF_CLEAR, so this memory arrives pre-zeroed. */

    if (v3d_mem_alloc(device, &context->state_mem[1], V3D_INITIAL_CL_BUFFER_SIZE) < 0)
    {
        E(("v3d_context_init: state_mem[1] alloc FAILED -- returning -7\n"));
        free_partial(device, context, 10);
        return -7;
    }
    D(("v3d_context_init: state_mem[1] alloc OK -- all pools allocated\n"));
    /* No explicit memset here -- v3d_mem_alloc's own AllocVec call already
     * uses MEMF_CLEAR, so this memory arrives pre-zeroed. */

    context->binning_buf[0].start = (v3d_u8*)context->binning_mem[0].hostptr;
    context->binning_buf[0].used = 0;
    context->binning_buf[0].capacity = V3D_INITIAL_CL_BUFFER_SIZE;
    context->binning_buf[0].overflowed = 0;
    context->binning_buf[1].start = (v3d_u8*)context->binning_mem[1].hostptr;
    context->binning_buf[1].used = 0;
    context->binning_buf[1].capacity = V3D_INITIAL_CL_BUFFER_SIZE;
    context->binning_buf[1].overflowed = 0;

    context->render_buf[0].start = (v3d_u8*)context->render_mem[0].hostptr;
    context->render_buf[0].used = 0;
    context->render_buf[0].capacity = V3D_INITIAL_RENDER_BUFFER_SIZE;
    context->render_buf[0].overflowed = 0;
    context->render_buf[1].start = (v3d_u8*)context->render_mem[1].hostptr;
    context->render_buf[1].used = 0;
    context->render_buf[1].capacity = V3D_INITIAL_RENDER_BUFFER_SIZE;
    context->render_buf[1].overflowed = 0;

    context->tile_list_buf[0].start = (v3d_u8*)context->tile_list_mem[0].hostptr;
    context->tile_list_buf[0].used = 0;
    context->tile_list_buf[0].capacity = V3D_INITIAL_TILE_LIST_BUFFER_SIZE;
    context->tile_list_buf[0].overflowed = 0;
    context->tile_list_buf[1].start = (v3d_u8*)context->tile_list_mem[1].hostptr;
    context->tile_list_buf[1].used = 0;
    context->tile_list_buf[1].capacity = V3D_INITIAL_TILE_LIST_BUFFER_SIZE;
    context->tile_list_buf[1].overflowed = 0;

    context->state_buf[0].start = (v3d_u8*)context->state_mem[0].hostptr;
    context->state_buf[0].used = 0;
    context->state_buf[0].capacity = V3D_INITIAL_CL_BUFFER_SIZE;
    context->state_buf[0].overflowed = 0;
    context->state_buf[1].start = (v3d_u8*)context->state_mem[1].hostptr;
    context->state_buf[1].used = 0;
    context->state_buf[1].capacity = V3D_INITIAL_CL_BUFFER_SIZE;
    context->state_buf[1].overflowed = 0;

    context->current_buf = 0;
    context->build_slot = 0;
    context->render_pending = 0;
    context->render_lastframe = 0;

    context->ez_state = 0;       /* V3D_EZ_UNDECIDED */
    context->first_ez_state = 0;
    context->depth_ez_dir = 0;   /* V3D_EZ_UNDECIDED -- depth test starts off */
    context->depth_persist_seen = 0;   /* no frame has loaded depth yet */
    context->force_new_pass = 0;
    context->doing_intermediate_pass = 0;
    context->has_scratch_color = 0;
    context->scratch_color_mem.size = 0;
    context->scratch_color_mem.handle = 0;
    context->scratch_color_mem.busaddr = 0;
    context->scratch_color_mem.hostptr = 0;
    context->scratch_stride = 0;

    v3d_frame_begin(&context->frame, tilesX, tilesY);

    context->fixed_color = 0;
    context->zmode = 0;
    context->blend_srcmode = 0;
    context->blend_dstmode = 0;
    /* Alpha-channel blend factors and blend equations, set by
     * glBlendFuncSeparate/glBlendEquation. Written explicitly rather than
     * left to AllocVec's MEMF_CLEAR even though ADD and ZERO both happen to
     * be 0 -- alpha_func just below shows what relying on zero-init here
     * costs, and blend_alpha_srcmode's correct default is ONE, which is NOT
     * zero. */
    context->blend_alpha_srcmode = V3D_BLEND_FACTOR_ONE;
    context->blend_alpha_dstmode = V3D_BLEND_FACTOR_ZERO;
    context->blend_color_equation = V3D_BLEND_MODE_ADD;
    context->blend_alpha_equation = V3D_BLEND_MODE_ADD;
    context->blend_nonadd_warned = 0;
    context->alpha_test_enable = 0;
    context->alpha_ref = 0.0f;
    /* alpha_func must be initialised here: left to AllocVec's MEMF_CLEAR it
     * would be 0, which is V3D_ALPHAFUNC_NEVER, whereas GL's documented
     * default is GL_ALWAYS. Every compare function takes effect, so an app
     * that calls glEnable(GL_ALPHA_TEST) without ever calling glAlphaFunc()
     * would discard every fragment that goes through the alpha test. Set the
     * spec default explicitly. */
    context->alpha_func = V3D_ALPHAFUNC_ALWAYS;
    context->color_mask_r = context->color_mask_g = context->color_mask_b = context->color_mask_a = 1;

    context->scissor_x = context->scissor_y = 0;
    context->scissor_w = width;
    context->scissor_h = height;
    context->scissor_enable = 0;

    context->fog_enable = 0;
    context->fog_mode = 0;
    context->fog_start = context->fog_end = context->fog_density = 0.0f;
    context->fog_r = context->fog_g = context->fog_b = context->fog_a = 0;

    context->bound_texture[0] = 0;
    context->bound_texture[1] = 0;
    context->texture_list = 0;

    D(("v3d_context_init: exit OK\n"));

    return 0;
}

/* The size-dependent half of v3d_context_init, for a resize: same sizing,
 * alignment and cache flush as there. zbuffer_bits stays latched from the
 * context's creation. */
int v3d_context_resize(V3DContext* context, V3DDevice* device, v3d_u16 width, v3d_u16 height)
{
    v3d_u32 zbuffer_size, tile_state_size, tile_alloc_size, tilesX, tilesY;

    v3d_frame_end(device, &context->frame);
    v3d_mem_free(device, &context->tile_alloc_mem);
    v3d_mem_free(device, &context->tile_state_mem);
    v3d_mem_free(device, &context->zbuffer_mem);
    /* Sized for the old screen; v3d_backend_alloc_scratch_color allocates
     * it again on first use. */
    v3d_mem_free(device, &context->scratch_color_mem);
    context->has_scratch_color = 0;
    context->scratch_stride = 0;

    context->width = width;
    context->height = height;
    g_v3d_render_timeout_iterations = ((v3d_u32)width * (v3d_u32)height) >> 2;

    v3d_frame_compute_pool_sizes(width, height, context->zbuffer_bits,
                                  &zbuffer_size, &tile_state_size,
                                  &tile_alloc_size, &tilesX, &tilesY);

    if (v3d_mem_alloc(device, &context->zbuffer_mem, zbuffer_size + 4096) < 0)
    {
        E(("v3d_context_resize: zbuffer_mem alloc FAILED (size=%lu)\n", (ULONG)(zbuffer_size + 4096)));
        return -1;
    }
    {
        ULONG len = (ULONG)context->zbuffer_mem.size;
        CachePreDMA(context->zbuffer_mem.hostptr, &len, 0);
    }
    align_mem_4096(device, &context->zbuffer_mem);

    if (v3d_mem_alloc(device, &context->tile_state_mem, tile_state_size + 4096) < 0)
    {
        E(("v3d_context_resize: tile_state_mem alloc FAILED (size=%lu)\n", (ULONG)(tile_state_size + 4096)));
        return -2;
    }
    {
        ULONG len = (ULONG)context->tile_state_mem.size;
        CachePreDMA(context->tile_state_mem.hostptr, &len, 0);
    }
    align_mem_4096(device, &context->tile_state_mem);

    if (v3d_mem_alloc(device, &context->tile_alloc_mem, tile_alloc_size + 4096) < 0)
    {
        E(("v3d_context_resize: tile_alloc_mem alloc FAILED (size=%lu)\n", (ULONG)(tile_alloc_size + 4096)));
        return -3;
    }
    {
        ULONG len = (ULONG)context->tile_alloc_mem.size;
        CachePreDMA(context->tile_alloc_mem.hostptr, &len, 0);
    }
    align_mem_4096(device, &context->tile_alloc_mem);

    /* The new Z buffer holds nothing a frame could load yet. */
    context->ez_state = 0;
    context->first_ez_state = 0;
    context->depth_persist_seen = 0;
    context->force_new_pass = 0;
    context->doing_intermediate_pass = 0;

    v3d_frame_begin(&context->frame, tilesX, tilesY);
    return 0;
}

void v3d_context_free(V3DDevice* device, V3DContext* context)
{
    v3d_frame_end(device, &context->frame);
    v3d_mem_free(device, &context->state_mem[0]);
    v3d_mem_free(device, &context->state_mem[1]);

    v3d_mem_free(device, &context->tile_list_mem[0]);
    v3d_mem_free(device, &context->tile_list_mem[1]);
    v3d_mem_free(device, &context->render_mem[0]);
    v3d_mem_free(device, &context->render_mem[1]);
    v3d_mem_free(device, &context->binning_mem[0]);
    v3d_mem_free(device, &context->binning_mem[1]);
    v3d_mem_free(device, &context->tile_alloc_mem);
    v3d_mem_free(device, &context->tile_state_mem);
    v3d_mem_free(device, &context->zbuffer_mem);

    /* shader_code_mem (draw.c's gl_EnsureShaders) isn't allocated in
     * v3d_context_init -- it's lazily allocated on the first real draw
     * call, once per context (guarded by shaders_ready) -- so it doesn't
     * belong in v3d_context_init's own free_partial unwind list above;
     * it is freed here instead. v3d_mem_free is always safe to call even
     * if shader_code_mem.handle is still 0 (context never drew) -- matches
     * every other field freed above. */
    v3d_mem_free(device, &context->shader_code_mem);

    /* scratch_color_mem (see force_new_pass's own comment in
     * v3d_context.h): also lazily allocated, by
     * v3d_backend_alloc_scratch_color below -- same "always safe to free,
     * handle may still be 0" reasoning as shader_code_mem just above. */
    v3d_mem_free(device, &context->scratch_color_mem);
}

int v3d_backend_alloc_scratch_color(V3DDevice* device, V3DContext* context)
{
    v3d_u32 stride, size;

    if (context->scratch_color_mem.hostptr)
        return 0; /* already allocated by an earlier call on this context */

    stride = (v3d_u32)context->width * 4; /* RGBA8 */
    size = stride * (v3d_u32)context->height;

    /* +4096 slack for align_mem_4096, exactly matching v3d_context_init's
     * zbuffer_mem/tile_state_mem/tile_alloc_mem allocations above. */
    if (v3d_mem_alloc(device, &context->scratch_color_mem, size + 4096) < 0)
    {
        E(("v3d_backend_alloc_scratch_color: alloc FAILED (size=%lu)\n", (ULONG)(size + 4096)));
        return -1;
    }
    /* No explicit memset -- v3d_mem_alloc's own AllocVec call already uses
     * MEMF_CLEAR. One-time flush, same reasoning as zbuffer_mem's own
     * (v3d_context_init above): the CPU never writes to this again after
     * this point, V3D owns it as scratch from here on. */
    {
        ULONG len = (ULONG)context->scratch_color_mem.size;
        CachePreDMA(context->scratch_color_mem.hostptr, &len, 0);
    }
    align_mem_4096(device, &context->scratch_color_mem);

    context->scratch_stride = stride;

    return 0;
}

void v3d_context_flush_for_dma(V3DDevice* device, V3DContext* context)
{
    struct ExecBase* const SysBase = device->sysbase;
    ULONG len;
    v3d_u8 slot = context->build_slot;

    len = (ULONG)context->binning_buf[slot].used;
    CachePreDMA(context->binning_buf[slot].start, &len, 0);

    len = (ULONG)context->state_buf[slot].used;
    CachePreDMA(context->state_buf[slot].start, &len, 0);

    len = (ULONG)context->tile_list_buf[slot].used;
    CachePreDMA(context->tile_list_buf[slot].start, &len, 0);

    len = (ULONG)context->render_buf[slot].used;
    CachePreDMA(context->render_buf[slot].start, &len, 0);
}

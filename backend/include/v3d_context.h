/*
 * (C) 2025-2026 Dennis van der Boon
 */

#ifndef V3D_CONTEXT_H
#define V3D_CONTEXT_H

#include "v3d_types.h"
#include "v3d_texture.h"
#include "v3d_frame.h"
#include "v3d_device.h"

/*
 * V3DContext -- replaces W3D_Context / RPIV3D_Context
 * (PoC/v3d_structs.h:127-162), which embedded "W3D_Context w3d" as its
 * first member; V3DContext drops that embedding. The render-state fields
 * below (zmode/blend/alpha test/fixedcolor) are carried over from
 * RPIV3D_Context, since that state is backend-owned, not Warp3D-shaped --
 * only the container changed.
 *
 * The control-list buffers are persistent fields here; the PoC allocated
 * its buffers locally, per call, inside CL_Draw_Triangle (v3d_cle.c:604-609,
 * `v3d_static_buffer abuffer[3]`).
 *
 * `frame` is embedded directly (not a pointer to a separately-allocated
 * struct): a context has one frame in flight at a time (there is no job
 * queue, and at most one render is outstanding -- see render_pending
 * below), and V3DFrame is small, so embedding avoids a separate
 * alloc/free step for it.
 */

#define V3D_MAX_TEXUNIT 2  /* must match MAX_TEXUNIT in gl/include/mgl/gl.h -- backend can't depend on gl.h, so kept in sync by convention/comment, not a shared include */

/*
 * Initial capacity, per slot, of binning_buf and state_buf. Every draw
 * call writes fresh packets into both every frame, so their usage scales
 * with the number of draw calls per frame (state_buf's also with the
 * vertices drawn). state_buf grows on demand through v3d_cl_claim_grow,
 * except for the attribute records glShaderStateAttributeRecord appends
 * after each shader state record. binning_buf, like render_buf and
 * tile_list_buf, is written through v3d_buffer_claim_memory directly
 * (V3D_BUFFER_ALLOC_OPERATION and friends, v3d_hw.h) and does not grow:
 * an overflow drops the frame (the buffer's `overflowed` flag, checked by
 * gl_FramePresent), so this size is its headroom.
 */
#define V3D_INITIAL_CL_BUFFER_SIZE 4194304

/* render_buf/tile_list_buf have their OWN, smaller sizes: they hold only
 * the RENDER and INDIRECT lists gl_FramePresent builds for each pass,
 * while binning_buf scales directly with primitive count and keeps the
 * full V3D_INITIAL_CL_BUFFER_SIZE above. These two sizes are generous
 * headroom over observed usage, not a proven upper bound; like
 * binning_buf, neither buffer grows. */
#define V3D_INITIAL_RENDER_BUFFER_SIZE 65536
#define V3D_INITIAL_TILE_LIST_BUFFER_SIZE 32768

/*
 * Backend-local fog mode IDs -- fog.c maps GL_LINEAR/GL_EXP/GL_EXP2 onto
 * these (backend/ must not depend on gl.h, same reasoning as V3DTexture's
 * filter/wrap/format enums).
 */
enum {
    V3D_FOG_LINEAR = 0,
    V3D_FOG_EXP,
    V3D_FOG_EXP2
};

/*
 * Backend-local alpha-test comparison-function IDs -- others.c's
 * GLAlphaFunc maps GL_NEVER/LESS/EQUAL/LEQUAL/GREATER/NOTEQUAL/GEQUAL/
 * ALWAYS onto these.
 *
 * All eight are functionally distinct. NEVER/LESS/EQUAL/LEQUAL/GREATER/
 * NOTEQUAL/GEQUAL each select their own alpha-test shader in draw.c;
 * ALWAYS is handled by clearing draw.c's alphatest predicates outright,
 * since it is definitionally "alpha test disabled". See
 * V3D_SHADER_VARIANT_FRAGMENT_*_ALPHATEST_* in v3d_shader_assembler.h for
 * the fcmp/setmsf derivation of each.
 */
enum {
    V3D_ALPHAFUNC_NEVER = 0,
    V3D_ALPHAFUNC_LESS,
    V3D_ALPHAFUNC_EQUAL,
    V3D_ALPHAFUNC_LEQUAL,
    V3D_ALPHAFUNC_GREATER,
    V3D_ALPHAFUNC_NOTEQUAL,
    V3D_ALPHAFUNC_GEQUAL,
    V3D_ALPHAFUNC_ALWAYS
};

/*
 * blend_srcmode/blend_dstmode (below, generic v3d_u32 fields) hold real
 * backend/hw/v3d_hw.h `v3d_blend_factor` enum values directly, assigned by
 * others.c's GLBlendFunc, and are passed to the BlendCfg() CL packet.
 */

/*
 * Backend-local depth-compare-function IDs -- context.c's GLDepthFunc maps
 * GL_NEVER/LESS/EQUAL/LEQUAL/GREATER/NOTEQUAL/GEQUAL/ALWAYS onto these for
 * the `zmode` field (a generic v3d_u32). With the depth test enabled,
 * gl_EmitCullBlendState (draw.c) passes zmode straight to CFG_BITS as the
 * depth-test function, so each value must equal its v3d_compare_function
 * counterpart (v3d_hw.h). V3D's tile renderer takes a binary early-Z
 * direction (v3d_EARLY_Z_DIRECTION_LT_LE / _GT_GE, see
 * TileRenderingModeCFGCommon in v3d_commands.h), not an 8-way comparator
 * like Warp3D's W3D_Z_*, so the early-Z direction is derived from zmode
 * separately (V3D_EZ_* below).
 */
enum {
    V3D_Z_NEVER = 0,
    V3D_Z_LESS,
    V3D_Z_EQUAL,
    V3D_Z_LEQUAL,
    V3D_Z_GREATER,
    V3D_Z_NOTEQUAL,
    V3D_Z_GEQUAL,
    V3D_Z_ALWAYS
};

/*
 * Early-Z state, MESA's enum v3d_ez_state verbatim (v3d_context.h in its
 * gallium driver) including the value order, so the two can be compared
 * directly when re-reading that source. UNDECIDED must stay 0: it is the
 * reset state, and v3d_context_init writes a literal 0 for it.
 *
 * V3D's early-Z is a single DIRECTION per frame, not a comparator -- the
 * hardware can reject early against "nearer wins" or "farther wins". That
 * is why this is accumulated per frame rather than decided per draw.
 */
enum {
    V3D_EZ_UNDECIDED = 0,
    V3D_EZ_GT_GE,
    V3D_EZ_LT_LE,
    V3D_EZ_DISABLED
};

#ifndef V3D_CONTEXT_TYPEDEF_DEFINED
#define V3D_CONTEXT_TYPEDEF_DEFINED
typedef struct V3DContext V3DContext; /* also forward-declared by v3d_texture.h, guarded to avoid a redeclaration warning when both headers are included together (v3d_texture.c does) */
#endif

struct V3DContext {
    /* draw region */
    void*   framebuffer;       /* meant as the output bitmap/buffer address -- unused: only zeroed at init; gl_FramePresent stores to GLcontext's vmembase */
    v3d_u16 width, height;
    v3d_i32 y_offset;

    /* Z-BUFFER precision in bits: 32 = D32F (default), 16 = D16. Latched from
     * g_v3d_requested_zbuffer_bits at context init, because zbuffer_mem below
     * is sized from it -- so it CANNOT change while a context lives. Read this
     * field, never the global, once a context exists.
     *
     * NOT screen depth. See g_v3d_requested_zbuffer_bits (v3d_frame.h) for why
     * that distinction is called out explicitly on this platform. */
    int     zbuffer_bits;

    /* persistent GPU memory pools -- sized once at context-create/resize, not per draw call like PoC's CL_Draw_Triangle did */
    v3d_mem zbuffer_mem;
    v3d_mem tile_state_mem;
    v3d_mem tile_alloc_mem;

    /* persistent control-list buffers. Each v3d_static_buffer is just a writable VIEW
     * (start/used/capacity) into its matching v3d_mem backing allocation below -- kept separate
     * so growing a buffer (new, bigger v3d_mem; the old one retired, see v3d_cl_claim_grow)
     * doesn't need to change what the rest of the code touches (the v3d_static_buffer).
     *
     * state_buf holds shader state and attribute records, uniforms and texture/sampler state --
     * what PoC's `scl` (state control list, the 4th of CL_Draw_Triangle's 4x4KB chunks,
     * v3d_cle.c) held -- plus each draw's vertex and index arrays (gl_EmitPrimitiveV3D). */
    /* Each of these 4 is a 2-element array, indexed by build_slot below;
     * gl_FramePresent alternates build_slot after every submitted pass.
     * They are the only 4 things the CPU directly writes bytes into during
     * CL-build, so they are the only things that need a second copy to keep
     * CL-building off the bytes of a submitted render that may still be
     * executing: nothing GPU-execution-only -- zbuffer_mem/tile_state_mem/
     * tile_alloc_mem/the framebuffer itself -- needs one, since GPU
     * execution stays strictly serial between frames. (gl_FrameBegin
     * currently waits for the outstanding render, in MGLFlushPendingRender,
     * before it resets any slot, so the two do not overlap.) */
    v3d_mem binning_mem[2], render_mem[2], tile_list_mem[2], state_mem[2];
    v3d_static_buffer binning_buf[2];
    v3d_static_buffer render_buf[2];
    v3d_static_buffer tile_list_buf[2];
    v3d_static_buffer state_buf[2];
    v3d_static_buffer* current_buf;   /* points at whichever of the above is being written -- same pattern as PoC's SetBuffer()/context->currentBuf */
    v3d_u8 build_slot;   /* which slot [0]/[1] the CPU is currently building into */

    /* A SINGLE render-completion flag, not per-CL-slot -- only one bitmap
     * lock is ever in play, regardless of which of the 2 CL-buffer slots is
     * currently being built into. A new render is never submitted while
     * an older one is still unconfirmed (gl_FrameBegin always resolves
     * render_pending before gl_FramePresent submits the next one), so
     * at most one render is ever outstanding at a time -- one flag is
     * both correct and sufficient. Tracking it per slot would be wrong:
     * the slot about to be reused is 2 frames back, while the render
     * still outstanding is 1 frame back, so the wait would be skipped and
     * a second lock stacked on the same framebuffer. */
    v3d_u8 render_lastframe;
    v3d_u8 render_pending;

    /* frame/job lifecycle state */
    V3DFrame frame;

    /* render state (mirrors what base MiniGL pushes via W3D_SetState/W3D_SetBlendMode/etc) */
    v3d_u32 fixed_color;        /* packed RGBA, current-color-when-no-per-vertex-color */
    v3d_u32 zmode;
    v3d_u32 blend_srcmode, blend_dstmode;
    /* Separate ALPHA-channel factors and per-channel blend equations, for
     * glBlendFuncSeparate()/glBlendEquation(). BlendCfg() takes independent
     * colour and alpha triples (v3d_commands.h), so these need no CL packet
     * of their own. Defaults are alpha = ONE/ZERO and both equations = ADD. */
    v3d_u32 blend_alpha_srcmode, blend_alpha_dstmode;
    v3d_u32 blend_color_equation, blend_alpha_equation;
    v3d_u8  blend_nonadd_warned;   /* nothing reads this; context init and GLBlendEquation still clear it */
    v3d_u8  alpha_test_enable;
    v3d_u8  alpha_func;   /* V3D_ALPHAFUNC_* above */
    float   alpha_ref;
    v3d_u8  color_mask_r, color_mask_g, color_mask_b, color_mask_a;

    /* scissor */
    v3d_u16 scissor_x, scissor_y, scissor_w, scissor_h;
    v3d_u8  scissor_enable;

    /* fog */
    v3d_u8  fog_enable;
    v3d_u8  fog_mode;   /* V3D_FOG_LINEAR/EXP/EXP2 above */
    float   fog_start, fog_end, fog_density;
    v3d_u8  fog_r, fog_g, fog_b, fog_a;

    /* texture bindings, one per unit */
    V3DTexture* bound_texture[V3D_MAX_TEXUNIT];

    /* meant as the allocated-texture list head (replaces AddTail(&context->restex,...)) -- nothing links or walks this list */
    V3DTexture* texture_list;

    /*
     * Frame accumulation. V3D bins across a whole frame, then renders
     * once -- there is no per-call immediate-mode rasterization the way
     * Warp3D had -- so drawing is "many draw calls, then present".
     * context.c's gl_FrameBegin/gl_FramePresent own this: frame_active
     * tracks whether the binning preamble has been emitted (GLClear and
     * every draw.c terminal function call gl_FrameBegin, which is a no-op
     * if already active unless force_new_pass is set, below);
     * pending_clear_color/pending_clear_depth record what GLClear asked
     * for, consumed by gl_FramePresent (called from MGLSwitchDisplay) when
     * it finally builds the render list and submits.
     */
    v3d_u8 frame_active;
    v3d_u8 pending_clear_color;
    v3d_u8 pending_clear_depth;

    /* Set by gl_EmitPrimitiveV3D (draw.c) when a draw call's own multi-claim
     * fragment-uniform sequence (unif_frag_address, captured once before
     * several v3d_cl_claim_grow calls for TMU config/blend/color uniforms)
     * gets straddled by a mid-sequence state_buf regrow -- the captured
     * address goes stale (points at the old, retired block) while later
     * uniforms in the same sequence land in the new one, corrupting that
     * draw's fragment_shader_uniforms_address, which feeds real TMU
     * texture-fetch addresses (a grow inside the index-buffer claim sets it
     * the same way). gl_FramePresent checks it before acquiring the bitmap
     * lock and drops the frame without submitting it. Reset every
     * gl_FrameBegin, alongside pending_clear_color/
     * pending_clear_depth. See v3d_clbuf.c's own v3d_cl_claim_grow comment
     * for the old block's retirement. */
    v3d_u8 frame_corrupted;

    /* Render-pass split. Without one, a glClear() arriving mid-pass joins
     * the pass already in progress (gl_FrameBegin is a no-op while
     * frame_active), so its depth reset never happens: geometry drawn after
     * the clear still tests against depth written before it.
     *
     * force_new_pass: set by GLClear when a full-drawable clear arrives
     * mid-pass with geometry already binned (guarded by
     * draw_state_configured, so a run of clears at the top of a frame does
     * not split; a scissored clear draws a quad instead, gl_ClearViaQuad),
     * by MGLReadbackBegin, and by tex_SyncBeforeModify (texture.c) when a
     * texture already drawn in this pass must be modified and cannot be
     * renamed. gl_FrameBegin checks this when it would otherwise no-op
     * (frame_active already TRUE): it clears the flag and finalizes the
     * in-progress pass for real (a genuine extra gl_FramePresent call) so
     * the caller gets a truly fresh binning pass.
     *
     * In single-buffer mode there is no back buffer (double buffering is
     * off unless the driver is built with MGLV3D_DOUBLE_BUFFER_ENABLED=1,
     * context.c), so an early-finalized pass can't be allowed to store
     * straight into the real, already-visible screen -- that flickers.
     * Instead it stores into scratch_color_mem, a driver-owned
     * system-memory buffer, and the next pass loads that color content back
     * first, compositing exactly as if both had shared one real pass all
     * along, just without ever exposing the intermediate result on screen.
     *
     * doing_intermediate_pass: set only around the extra gl_FramePresent
     * call above, so that call stores color into scratch_color_mem instead
     * of context->vmembase, and takes the bitmap lock only when it has to
     * load the screen's color (no color clear and no pending scratch
     * composite).
     *
     * has_scratch_color: set once an intermediate pass is submitted,
     * consumed (and cleared) by the very next gl_FramePresent's color LOAD
     * step, whichever pass that turns out to be -- a second intermediate
     * pass or the real final one (a full-drawable GLClear color clear, a
     * dropped frame, or MGLReadbackEnd ending a readback pass that
     * MGLReadbackBegin started itself discards it instead). The next pass
     * therefore draws over a real loaded copy of the earlier pass's output,
     * not a cleared buffer. Kept independent of force_new_pass and
     * doing_intermediate_pass since it needs to survive from the end of one
     * gl_FramePresent call to the start of the next one, not just within a
     * single call.
     *
     * Depth gets none of this: depth Load/Store keep targeting zbuffer_mem
     * unconditionally, intermediate or not, so a pass that clears depth
     * discards whatever the earlier pass wrote there. */
    /* Early-Z, accumulated per FRAME -- MESA's job->ez_state / first_ez_state
     * pair, with the same enum values (see V3D_EZ_* above).
     *
     * V3D's early-Z is one DIRECTION for the whole frame. ez_state is the
     * running decision: UNDECIDED until some draw picks a direction, then
     * that direction while every later draw agrees, then DISABLED for good
     * the moment one does not. first_ez_state latches the first decided
     * direction and is what drives TileRenderingModeCFGCommon's direction and
     * `ezd` -- MESA reads first_ez_state there, not ez_state, so a frame that
     * conflicts LATER still gets the packet configured for the direction its
     * early draws used while the per-draw CFG_BITS pair turns early-Z off
     * from the conflict onward.
     *
     * The direction per draw comes from the depth func: LESS/LEQUAL -> LT_LE,
     * GREATER/GEQUAL -> GT_GE, NEVER/EQUAL -> UNDECIDED (compatible with
     * anything, so they cost nothing), ALWAYS/NOTEQUAL -> DISABLED.
     *
     * A scissored depth clear (gl_ClearViaQuad) draws with dtf = ALWAYS,
     * which maps to DISABLED under the ordinary rule above, so it needs no
     * early-Z handling of its own. */
    v3d_u8 ez_state;
    v3d_u8 first_ez_state;

    /* The early-Z direction implied by the CURRENT depth state, cached.
     * Recomputed by v3d_update_depth_ez_dir (context.c) only when
     * glDepthFunc or GL_DEPTH_TEST changes -- NOT per draw, because
     * gl_EmitPrimitiveV3D runs per draw call and is the hot path on a 68k:
     * anything per-draw has to earn its place. */
    v3d_u8 depth_ez_dir;

    /* Sticky, and deliberately NOT reset by gl_FrameBegin: set the first
     * time any frame reaches gl_FramePresent without pending_clear_depth,
     * i.e. the first time an app actually LOADS the depth buffer it wrote
     * last frame (a pass with PassContinuesSplit set -- one continuing a
     * split frame, or a readback's own pass -- does not count, see
     * context.h). Once set it stays set for the life of the context.
     *
     * It gates skipping the depth STORE. That store exists only to serve a
     * future frame that loads, so for an app that clears depth every frame
     * it writes the whole depth buffer every frame for nothing, and the
     * cost scales with resolution.
     *
     * Whether the store is needed depends on a FUTURE frame, which is
     * unknowable when the CL is built, so this cannot be made perfectly
     * safe. The residual cost is exactly ONE corrupted frame, on the frame
     * where an app first stops clearing depth. From the next frame onward
     * the buffer is consistent again, because that frame's own load sets
     * this flag before its store is emitted. Accepted deliberately over
     * storing depth every frame. */
    v3d_u8 depth_persist_seen;

    v3d_u8 force_new_pass;
    v3d_u8 doing_intermediate_pass;
    v3d_u8 has_scratch_color;
    v3d_mem scratch_color_mem;   /* lazily allocated, on first use only -- see v3d_backend_alloc_scratch_color (v3d_context.c) */
    v3d_u32 scratch_stride;      /* bytes per row in scratch_color_mem, computed once at allocation (width*4, RGBA8) */

    /*
     * Snapshot of context->ClearColor/ClearDepth taken by GLClear() at the
     * moment it's called -- NOT the same as reading context->ClearColor
     * live in gl_FramePresent(). Real GL semantics require glClear to use
     * whatever clear color was active WHEN IT WAS CALLED, not whatever's
     * active later when the frame actually gets flushed to hardware (MESA's
     * v3d_tlb_clear in v3dx_draw.c likewise packs the clear color into the
     * in-progress job's own state at clear-call-time). Without this
     * snapshot, a legal "glClearColor(A); glClear(...); glClearColor(B);"
     * sequence within one frame would clear to B instead of A.
     */
    v3d_u32 clear_color_value;
    float   clear_depth_value;

    /*
     * Per-pass binning state. MESA emits draw state alongside an actual
     * draw call, not at job startup, so gl_FrameBegin's binning preamble
     * leaves it out and gl_EnsureDrawState (draw.c) emits the per-pass
     * part at a pass's first draw call. draw_state_configured gates that
     * to once per pass (reset by gl_FrameBegin) rather than once per draw
     * call.
     */
    v3d_u8 draw_state_configured;

    /*
     * Shader machine code storage. Lazily allocated by gl_EnsureShaders
     * (draw.c) on the first real draw call, NOT in v3d_context_init, so a
     * context that never draws never needs shader code at all.
     * shaders_ready guards one-time assemble+upload; shader_code_mem holds
     * vertex/coordinate/fragment machine code at fixed offsets.
     */
    v3d_mem shader_code_mem;
    v3d_u8  shaders_ready;
};

/*
 * Allocates all persistent GPU memory pools (zbuffer/tile_state/tile_alloc,
 * sized per v3d_frame_compute_pool_sizes) and both slots of the four CL
 * buffers, sized by the V3D_INITIAL_*_BUFFER_SIZE constants above. Sets
 * render/scissor/fog state to reasonable defaults. Returns 0 on success,
 * negative on any allocation failure (matching v3d_mem_alloc's convention).
 */
int v3d_context_init(V3DContext* context, V3DDevice* device, v3d_u16 width, v3d_u16 height);

/* Frees every pool/buffer allocated by v3d_context_init, the lazily allocated shader_code_mem and scratch_color_mem, and the frame's spill pool and retire list. */
void v3d_context_free(V3DDevice* device, V3DContext* context);

/* Re-sizes an initialised context for a new render-target size: frees and
 * reallocates only what depends on the size (Z buffer, tile state, tile
 * allocation, the frame's tile counts, the scratch colour buffer) and keeps
 * everything else -- control-list buffers, shader code, render state. The
 * caller must have waited for every render and must not be mid-frame.
 * Returns 0, or v3d_context_init's -1/-2/-3 if an allocation fails; the
 * size-dependent buffers are then left freed and v3d_context_free is still
 * safe. */
int v3d_context_resize(V3DContext* context, V3DDevice* device, v3d_u16 width, v3d_u16 height);

/*
 * Lazily allocates context->scratch_color_mem (RGBA8, scratch_stride =
 * width*4 bytes/row) the first time an intermediate compositing pass
 * actually needs it -- see force_new_pass's comment above for why this
 * exists. No-op (returns 0) if already allocated. Matches
 * v3d_context_init's own zbuffer_mem allocation exactly: size+4096 slack
 * for align_mem_4096, one-time CachePreDMA flush. Returns 0 on success,
 * negative on allocation failure (matching v3d_mem_alloc's convention) --
 * a failure here is non-fatal to the caller, see gl_FrameBegin's own
 * handling (context.c).
 */
int v3d_backend_alloc_scratch_color(V3DDevice* device, V3DContext* context);

/*
 * AllocVec is v3d_mem_alloc's default backend (v3d_mem_allocvec.c) --
 * ordinary Amiga memory, unlike the RPi mailbox's deliberately-uncached
 * GPU memory, needs the ARM-side CPU cache explicitly flushed before V3D
 * reads it (CachePreDMA), or V3D can hang seeing stale/garbage data.
 * Flushes the used part of the current build_slot's four CL buffers, which
 * are rebuilt every frame -- binning_buf, state_buf, tile_list_buf,
 * render_buf. Call this once per frame, right before invalidating V3D's
 * own caches and submitting.
 *
 * Does NOT cover: zbuffer/tile_state/tile_alloc (V3D's own read-write
 * scratch, flushed once by v3d_context_init right after allocation -- the
 * CPU never writes to them again), shader-code/texture data (flushed by
 * the code that writes them: gl_EnsureShaders in draw.c once, v3d_texture.c
 * after every upload), or a state_buf block retired by a grow (flushed by
 * v3d_cl_claim_grow before it is retired).
 * Harmless to call even when linked against the mailbox-backed
 * v3d_mem_alloc (v3d_device.c, opt-in via V3D_MEM_USE_MAILBOX) -- that
 * memory is already uncached, so the flush is a redundant but cheap
 * no-op rather than something that could break it.
 */
void v3d_context_flush_for_dma(V3DDevice* device, V3DContext* context);

#endif /* V3D_CONTEXT_H */

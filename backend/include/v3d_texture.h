/*
 * (C) 2025-2026 Dennis van der Boon
 */

#ifndef V3D_TEXTURE_H
#define V3D_TEXTURE_H

#include "v3d_types.h"
#include "v3d_device.h"

/* Forward-declared, not included -- v3d_context.h already includes this
 * file, so including it back here would be circular. Only a pointer is
 * needed below. Guarded so including both this file and v3d_context.h in
 * the same translation unit (v3d_texture.c does) doesn't redeclare the
 * typedef. */
#ifndef V3D_CONTEXT_TYPEDEF_DEFINED
#define V3D_CONTEXT_TYPEDEF_DEFINED
typedef struct V3DContext V3DContext;
#endif

/*
 * V3DTexture -- replaces W3D_Texture / RPIV3D_Texture. RPIV3D_Texture
 * (PoC/v3d_structs.h:164) embedded "W3D_Texture w3d" as its first member
 * plus texenv/envcolor/texture_mem; that embedding is exactly the coupling
 * this struct removes -- no W3D_* type appears here. V3DTexture is the
 * object itself, created once per GL texture name and referenced across
 * many frames, not a wrapper around a W3D_Texture.
 *
 * `resident`/`dirty` mirror bookkeeping bits Warp3D's own W3D_Texture
 * carried -- kept because they're generically useful (has this been
 * uploaded yet? does it need re-upload?), not because they're
 * Warp3D-specific.
 *
 * Filter/wrap/format fields are small backend-local enums rather than
 * GL_* constants -- backend/ must not depend on MiniGL's gl.h.
 * gl/src/texture.c maps GL_NEAREST/GL_LINEAR/GL_REPEAT/etc. onto these.
 */

enum {
    V3D_TEXFILTER_NEAREST = 0,
    V3D_TEXFILTER_LINEAR,
    V3D_TEXFILTER_NEAREST_MIPMAP,
    V3D_TEXFILTER_LINEAR_MIPMAP
};

enum {
    V3D_TEXWRAP_REPEAT = 0,
    V3D_TEXWRAP_CLAMP,
    V3D_TEXWRAP_BORDER   /* V3D_WRAP_MODE_BORDER, v3d_texture_alloc's default -- no GL wrap mode maps to it (gl/src/texture.c sets REPEAT or CLAMP) */
};

/*
 * Backend-local texture-env-mode IDs -- gl/src/texture.c maps
 * GL_MODULATE/DECAL/REPLACE onto these for the `texenv_mode` field.
 */
enum {
    V3D_TEXENV_MODULATE = 0,
    V3D_TEXENV_DECAL,
    V3D_TEXENV_REPLACE
};

/*
 * Backend-local source pixel formats for v3d_texture_convert_row --
 * mirrors PoC/v3d_texture.c's ConvertTexRow, which switched on Warp3D's
 * W3D_* format enum (backend/ must not depend on that, same reasoning as
 * not depending on GL_* -- see the V3DTexture comment above).
 * gl/src/texture.c maps GL_UNSIGNED_BYTE/GL_RGB/GL_RGBA/etc. combinations
 * onto these.
 */
enum {
    V3D_SRCFMT_RGBA8 = 0,   /* already what V3D wants -- no conversion, just tiling */
    V3D_SRCFMT_ARGB8,
    V3D_SRCFMT_RGB8,
    V3D_SRCFMT_ARGB4,
    V3D_SRCFMT_ARGB1555,
    V3D_SRCFMT_RGB565,
    V3D_SRCFMT_LA8,
    V3D_SRCFMT_L8,          /* one byte per texel: L -> (L,L,L,1), GL_LUMINANCE */
    V3D_SRCFMT_A8           /* one byte per texel: A -> (1,1,1,A), GL_ALPHA     */
};

/* Enough for a 4096x4096 base (13 levels: 4096 down to 1). V3D 4.2's own
 * maximum texture dimension is 4096, so this cannot be exceeded by a legal
 * texture; v3d_texture_alloc_mipchain clamps to it regardless rather than
 * trusting that. */
#define V3D_MAX_MIP_LEVELS 13

typedef struct V3DTexture {
    struct V3DTexture* next;    /* meant as the link for V3DContext.texture_list, replacing AddTail(&context->restex,...) (PoC/v3d_texture.c:198) -- nothing links or walks that list */

    v3d_mem texture_mem;         /* GPU memory holding the uploaded texture data */

    v3d_u16 width, height;
    v3d_u8  format;              /* backend-local format id (V3D_SRCFMT_*) */
    v3d_u8  mipmap_count;

    v3d_u8  min_filter, mag_filter;   /* V3D_TEXFILTER_* */

    /* Filtering BETWEEN mip levels, which is a separate hardware field
     * (v3d_sampler_state.mip_filter_nearest) from the within-level filters
     * above. It is the SECOND word in the GL enum names, and
     * V3D_TEXFILTER_* cannot express it: tex_GLFilter2V3D maps all four
     * mipmap modes onto just two values, so GL_LINEAR_MIPMAP_NEAREST and
     * GL_LINEAR_MIPMAP_LINEAR are indistinguishable by the time they arrive
     * in min_filter. Left at 0, every mipmapped texture would silently get
     * LINEAR filtering between levels -- including GL_LINEAR_MIPMAP_NEAREST,
     * the cheap mode, on hardware that has to pay for the second level
     * fetch.
     *
     * Kept as its own field rather than by widening V3D_TEXFILTER_*, because
     * that enum is used elsewhere.
     * 1 = nearest (pick one level), 0 = linear (blend two). */
    v3d_u8  mip_filter_nearest;
    v3d_u8  wrap_s, wrap_t;            /* V3D_TEXWRAP_* */
    v3d_u8  border_r, border_g, border_b, border_a;

    v3d_u8  texenv_mode;          /* modulate/decal/replace/etc, backend-local id */
    v3d_u8  env_r, env_g, env_b, env_a;

    v3d_u8  resident;             /* uploaded to GPU memory */
    v3d_u8  dirty;                /* needs re-upload */

    /* Which enum v3d_memory_format this texture's level 0 was ACTUALLY
     * tiled/stored as (v3d_texture_alloc decides this from width/height,
     * matching MESA's own v3d_setup_plane_slices decision tree -- full
     * UIF is WRONG for narrow images, real hardware requires UBLINEAR_1/
     * 2_COLUMN or LINEARTILE below certain width/height thresholds).
     * v3d_texture_emit_state needs to know this to set
     * level_0_is_strictly_uif/level_0_xor_enable/extended correctly -- see
     * that function's own comment. Stored as v3d_u8 rather than enum
     * v3d_memory_format -- matches this header's own existing convention
     * of not depending on backend/hw-only types. */
    v3d_u8  tiling_format;

    /* v3d_get_ub_pad's result at alloc time (v3d_texture.c), needed
     * verbatim by v3d_texture_emit_state's level_0_ubpad, which it writes
     * only in its UIF_XOR branch. MESA's own v3dvx_image.c writes
     * level_0_ub_pad inside its `if (tex.level_0_is_strictly_uif)` guard,
     * and sets level_0_is_strictly_uif for UIF_NO_XOR as well -- see
     * emit_state's own comment for why this driver narrows it. */
    v3d_u8  ub_pad;

    /* ---- MIP CHAIN -----------------------------------------------------
     *
     * V3D lays a mip chain out as one allocation with the levels packed
     * SMALLEST FIRST, so level 0 sits at the highest offset. That is not a
     * style choice: MESA's v3d_setup_slices walks `for (i = last_level; i >=
     * 0; i--)` accumulating the offset, then shifts every level so level 0's
     * base lands on a 4K boundary (the hardware wants that for UIF XOR).
     * Each level independently re-runs the same LINEARTILE / UBLINEAR_1 /
     * UBLINEAR_2 / UIF decision v3d_texture_alloc already applies to level 0,
     * on that level's own dimensions -- so a 256x256 UIF_XOR base ends with
     * LINEARTILE tail levels.
     *
     * One subtlety worth stating, because it is easy to get wrong and the
     * hardware will not complain: from level 2 down, the dimensions come from
     * the POWER-OF-TWO-PADDED base, not from plain minification. MESA's
     * v3d_get_dimension_mpad exists for exactly this.
     *
     * LAZY PROMOTION. A texture starts life with num_levels == 1 and a
     * level-0-only allocation, so an application that never mips pays no
     * memory for levels it never uploads. The chain is built only when a
     * level > 0 upload actually arrives. That is safe to do late because
     * level 0's LAYOUT is identical
     * either way -- same tiling, padded dimensions, stride and size, only its
     * offset moves -- so promotion is a plain memcpy of level 0's bytes to
     * their new offset, with no re-tiling and no re-upload. */
    v3d_u8  num_levels;           /* levels ALLOCATED; 1 = no chain (default) */

    /* Highest level actually uploaded so far. Distinct from num_levels on
     * purpose: promotion allocates the whole chain at once, so the layout is
     * decided once and never has to move again, but the levels above this one
     * still hold no texel data. max_level in the shader state is driven from
     * THIS, so the TMU is not told about levels above the highest one
     * uploaded -- otherwise a minified surface would sample garbage. */
    v3d_u8  max_level_uploaded;

    struct {
        v3d_u32 offset;           /* byte offset of this level within texture_mem */
        v3d_u32 stride;           /* padded_w * 4 */
        v3d_u16 padded_w, padded_h;
        v3d_u8  tiling_format;    /* this level's OWN v3d_memory_format */
        v3d_u8  ub_pad;           /* this level's own v3d_get_ub_pad result */
    } levels[V3D_MAX_MIP_LEVELS];

    /* 1 = the texture's internalformat has no alpha component (GL_RGB, 3,
     * GL_RGB5, GL_RGB8, 5-6-5), so every upload into it -- level 0, mip
     * levels, sub-images -- is stored with alpha 255 whatever the source
     * carried. Set by GLTexImage2DNoMIP at level 0 AFTER v3d_texture_alloc
     * (whose memset clears it); v3d_texture_alloc_mipchain leaves it alone.
     * Appended after the fields above so none of them moves. */
    v3d_u8  no_alpha;

    /* ---- MID-FRAME TEXTURE REUSE HAZARD ---------------------------------
     *
     * Generation in which this texture was last referenced by a BINNED draw,
     * or 0 for "never". This is a tile renderer: a draw call is binned with
     * the texture's ADDRESS, and the texels are not read until the pass
     * actually renders. Overwriting a texture between two draws in the same
     * pass therefore changes what the EARLIER draw samples -- upload A, draw
     * left, upload B, draw right renders B on BOTH sides.
     *
     * tex_SyncBeforeModify (gl/src/texture.c) compares this against
     * g_mglv3d_frame_number (draw.c) and, on a match, renames the texture
     * before the new texels land -- falling back to a render-pass split if
     * the rename fails -- so the already-binned draws still sample the OLD
     * contents; see v3d_texture_rename below. Stamped at gl/src/draw.c's
     * single binning commit, for both texture units.
     *
     * The counter it is compared against ticks on EVERY gl_FramePresent,
     * including an intermediate split (gl_FrameBegin calls gl_FramePresent
     * directly, and gl_FramePresent's ++ sits above that function's own
     * frame_active early-out). That makes it a BIN GENERATION rather than a
     * frame count, which is precisely what this test wants: a split drains
     * the bin, so every stamp SHOULD go stale at that moment. Do not "fix" the
     * counter to tick only on real presents without re-reading this.
     *
     * Zero means never-drawn -- tex_SyncBeforeModify returns at once on 0 --
     * which is why g_mglv3d_frame_number starts at 1: v3d_texture_alloc
     * memsets this whole struct, so a brand-new texture reads 0 here, and a
     * counter starting at 0 would stamp every draw of the first bin
     * generation with the never-drawn value, hiding any hazard in it.
     *
     * Appended last, after no_alpha, so no existing field moves. */
    v3d_u32 last_draw_frame;
} V3DTexture;

/*
 * Allocates GPU memory for a width x height RGBA8 texture, sized from the
 * padded dimensions its tiling format needs (v3d_texture_level_layout),
 * plus 256 bytes slack for over-allocate-and-align-up: the base is aligned
 * up to 256 -- v3d_texture_shader_state's texture base pointer field is
 * right-shifted by 6, needing 64-byte alignment. Does not upload any data
 * -- call v3d_texture_upload_rgba8 after. Returns 0 on success.
 */
int v3d_texture_alloc(V3DDevice* device, V3DTexture* tex, v3d_u16 width, v3d_u16 height);

/*
 * Promotes a level-0-only texture to a full mip chain of `want_levels`
 * levels, reallocating and carrying level 0's existing tiled bytes across
 * unchanged. Called lazily, when a level > 0 upload actually arrives, so a
 * texture that never gets mipped never pays for a chain.
 *
 * Returns 0 on success, -1 if the new allocation fails.
 */
int v3d_texture_alloc_mipchain(V3DDevice* device, V3DTexture* tex, v3d_u8 want_levels);

/* Frees the GPU memory allocated by v3d_texture_alloc. Only safe when no
 * command list the GPU has yet to finish can still name this texture's address
 * -- otherwise park it (below) instead. */
void v3d_texture_free(V3DDevice* device, V3DTexture* tex);

/* ---- MID-FRAME TEXTURE REUSE: renaming and deferred free -----------------
 *
 * See the long comment above these functions' definitions (v3d_texture.c) for
 * the mechanism and for why it is used instead of a pass split.
 *
 * v3d_texture_rename gives `tex` a FRESH allocation of identical layout and
 * parks the old one, so draws already binned against the old address keep
 * sampling the right bytes. copy_old != 0 carries the existing texels across;
 * pass 0 only when the caller is about to overwrite the whole allocation.
 * Returns 0 on success, -1 leaving the texture untouched -- on -1 the caller
 * MUST fall back to a synchronous pass split, not proceed.
 *
 * On success tex->last_draw_frame is cleared and tex->texture_mem now points at
 * memory no binned draw can name. THE CALLER MUST ALSO EVICT THE DRAW-SIDE
 * SHADER-STATE CACHE for this texture (gl_InvalidateTexStateCache, draw.c):
 * that cache is keyed on the V3DTexture POINTER, which a rename deliberately
 * does not change, so without the eviction the next draw reuses a state record
 * still holding the OLD base address and the whole exercise is silently undone.
 */
int  v3d_texture_rename(V3DDevice* device, V3DTexture* tex, int copy_old);

/* Park a block instead of freeing it. Returns 0 on success (the caller's
 * v3d_mem is zeroed -- it no longer owns the block), -1 if the park list is
 * full, in which case the caller must fall back. */
int  v3d_texture_park_mem(V3DDevice* device, v3d_mem* mem);

/* Everything parked so far may be named by the command list this present
 * submits, so it becomes releasable at the next successful render wait.
 * Call once per present, before any path that can drop the frame
 * (gl_FramePresent does it first): a dropped frame submits no command
 * list, so its parked blocks are named by nothing and equally safe to
 * release. */
void v3d_texture_parked_mark_submitted(void);

/* Release parked blocks whose command list has retired. Call ONLY after a
 * render wait has actually succeeded. */
void v3d_texture_drain_parked(V3DDevice* device);

/* Teardown: release everything, submitted or not, plus the recycling pool. */
void v3d_texture_drain_parked_all(V3DDevice* device);

/*
 * Tiles `src` (srcStride bytes/row, already RGBA8-ordered -- use
 * v3d_texture_convert_row first for other source formats) into
 * tex->texture_mem via v3d_store_tiled_image. Flushes the whole of
 * tex->texture_mem via CachePreDMA once -- AllocVec is v3d_mem_alloc's default
 * backend (v3d_device.c's V3D_MEM_USE_MAILBOX comment); harmless no-op
 * if linked against the mailbox path instead. Sets tex->resident,
 * clears tex->dirty.
 */
void v3d_texture_upload_rgba8(V3DDevice* device, V3DTexture* tex, const void* src, v3d_u32 srcStride);

/*
 * Uploads one mip level. `src` holds that level's data at its REAL size
 * (width >> level), which is what the application supplies -- not the padded
 * size recorded in tex->levels[], which is only how much memory the level
 * occupies. v3d_texture_upload_rgba8 is this with level 0.
 */
void v3d_texture_upload_rgba8_level(V3DDevice* device, V3DTexture* tex, v3d_u8 level,
                                     const void* src, v3d_u32 srcStride);

/*
 * For glTexSubImage2D: same as v3d_texture_upload_rgba8, but writes
 * only a sub-rectangle (xoffset,yoffset,width,height) of tex's EXISTING
 * tiled GPU memory, leaving the rest of the texture untouched. `src`
 * must already be RGBA8-ordered, srcStride bytes/row, exactly like
 * v3d_texture_upload_rgba8 -- caller converts other GL formats first.
 *
 * No separate tiling math is needed: v3d_store_tiled_image's box parameter
 * is already a general (x,y,width,height) sub-rectangle within the full
 * image (v3d_hw.c's v3d_move_pixels_general_percomponentsPerPixel walks
 * whole aligned utiles in the interior, falling back to a per-pixel path
 * at the box's edges when x/y/width/height aren't utile-aligned --
 * multiples of 4 for RGBA8, whose utile is 4x4 pixels) -- the
 * full-image upload above just always happens to pass box={0,0,w,h}.
 *
 * `level` selects which mip level the box lands in, and is bounds-checked
 * against num_levels.
 */
void v3d_texture_upload_rgba8_subimage(V3DDevice* device, V3DTexture* tex, v3d_u8 level,
                                        const void* src, v3d_u32 srcStride,
                                        int xoffset, int yoffset, int width, int height);

/*
 * Ported from PoC/v3d_texture.c's ConvertTexRow -- ARGB8/RGB8/ARGB4/
 * ARGB1555/RGB565/LA8 (V3D_SRCFMT_*) to RGBA8, `count` pixels, into `dst`.
 * It also handles RGBA8 (a byte copy) and the one-byte L8 and A8 formats.
 * A pure data transform: no CL packet or hardware struct is involved.
 */
void v3d_texture_convert_row(v3d_u32* dst, const v3d_u8* src, v3d_u32 count, v3d_u8 srcFormat);

/*
 * Builds the texture_shader_state + sampler_state records for `tex`
 * in context->state_buf. Must be called after v3d_texture_upload_rgba8
 * (it reads tex->texture_mem.hostptr, set by v3d_texture_alloc, and
 * max_level_uploaded, set by the uploads) and while
 * context->current_buf/context->state_buf is the active claim target.
 * Returns the two addresses the TMU-config fragment uniforms need to
 * reference, via *outTextureShaderStateAddress/
 * *outTextureSamplerStateAddress -- caller still owns building the TMU
 * config uniform words and the shader state record itself.
 */
void v3d_texture_emit_state(V3DDevice* device, V3DContext* context, V3DTexture* tex,
                             ULONG* outTextureShaderStateAddress, ULONG* outTextureSamplerStateAddress);

#endif /* V3D_TEXTURE_H */

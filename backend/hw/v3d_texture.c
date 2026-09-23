/*
 * (C) 2025-2026 Dennis van der Boon
 */

/*
 * V3DTexture's alloc/upload/emit-state implementation: tiling layout, the
 * mip chain, the texture shader/sampler state records, and the renaming and
 * parking of texture memory that a binned draw may still name.
 * v3d_texture_alloc/v3d_texture_upload_rgba8 follow the PoC's texture setup
 * (PoC/v3d_cle.c: v3d_mem_alloc with +256 slack, V3D_ALIGN_UP to 256,
 * v3d_store_tiled_image); the tiling-format choice is MESA's, below, not
 * the PoC's "width > 128 -> XOR" rule.
 *
 * v3d_texture_convert_row is ported from PoC/v3d_texture.c's
 * ConvertTexRow (W3D_* format constants swapped for this file's
 * backend-local V3D_SRCFMT_* -- backend/ must not depend on Warp3D
 * types, same reasoning v3d_texture.h's own comments give for not
 * depending on GL_* types). Pure data transform, no CL/hardware-struct
 * risk.
 */

#include <exec/execbase.h>
#include <proto/exec.h>

#include "../include/v3d_texture.h"
#include "../include/v3d_context.h"
#include "../include/v3d_clbuf.h"
#include "../include/v3d_commands.h"
#include "v3d_hw.h"
#include "v3d_debug.h"

/*
 * TILING FORMAT. V3D does NOT always use full UIF tiling. MESA's
 * slice-tiling decision (v3dv_image.c's v3d_setup_plane_slices,
 * broadcom/vulkan) gives a non-MSAA image a DIFFERENT addressing scheme
 * entirely below certain width thresholds:
 *   - width <= utile_w OR height <= utile_h  -> LINEARTILE
 *   - width <= uif_block_w (1 UIF block)     -> UBLINEAR, 1 column
 *   - width <= 2*uif_block_w (2 UIF blocks)  -> UBLINEAR, 2 columns
 *   - otherwise                              -> full UIF (XOR/NO_XOR)
 * For RGBA8, uif_block_w = utile_w*2 = 8. v3d_get_uif_pixel_offset's
 * macroblock math is the wrong address function for an image below these
 * width thresholds, scattering texel bytes to the wrong offsets regardless
 * of how much buffer padding surrounds them. v3d_move_tiled_image (v3d_hw.c)
 * has dispatch cases and address functions for LINEARTILE/UBLINEAR_1/
 * UBLINEAR_2 (v3d_get_lt_pixel_offset/
 * v3d_get_ublinear_1_column_pixel_offset/
 * v3d_get_ublinear_2_column_pixel_offset).
 * v3d_texture_level_layout picks the format (stored on the V3DTexture
 * object so v3d_texture_upload_rgba8/_subimage and v3d_texture_emit_state
 * -- called separately, later -- all agree on what was actually used) and
 * pads according to THAT format's own alignment rule, matching MESA's own
 * `align(level_width, X)` calls (each case aligns to a different value,
 * not always 4*mb_width).
 */
/* V3D_UIFCFG_PAGE_SIZE=4096, V3D_UIFCFG_BANKS=8 -- "tunable parameters in
 * the HW design, but all V3D implementations agree" (MESA's own comment,
 * v3dv_limits.h) -- safe to hardcode, not device-specific.
 * V3D_UIFBLOCK_ROW_SIZE = 4 * (4 * 64) = 1024.
 * PAGE_CACHE_UB_ROWS = (4096*8)/1024 = 32.
 * PAGE_UB_ROWS = 4096/1024 = 4; PAGE_UB_ROWS_TIMES_1_5 = (4*3)>>1 = 6.
 * PAGE_CACHE_MINUS_1_5_UB_ROWS = 32-6 = 26. */
#define V3D_PAGE_CACHE_UB_ROWS 32
#define V3D_PAGE_UB_ROWS_TIMES_1_5 6
#define V3D_PAGE_CACHE_MINUS_1_5_UB_ROWS 26

/* The page size the three constants above are DERIVED from, named rather than
 * left as a bare 4096 at its use sites (MESA's V3D_UIFCFG_PAGE_SIZE). The
 * derived values stay written out by hand above; PAGE_CACHE_UB_ROWS must keep
 * the *8 banks factor -- 4096/1024 = 4 instead of 32 changes the XOR decision
 * for 32x32, 64x64 and 128x128 textures. This constant exists so the
 * mip-chain code below does not repeat the same bare literal. */
#define V3D_UIFCFG_PAGE_SIZE 4096

/* Ported byte-for-byte from MESA's v3d_get_ub_pad (broadcom/vulkan/
 * v3dv_image.c). XOR is a rare special case for TALL textures whose padded
 * height lands exactly on a page-cache-block boundary (the rule in
 * v3d_texture_level_layout), not a width threshold at all. Picking XOR for
 * e.g. a 320x24 texture (only 3 UIF-block rows tall) makes
 * v3d_get_uif_pixel_offset's `mb_y ^= 0x10` (v3d_hw.c) jump mb_id far
 * outside the allocated buffer (mb_base_addr=46848 for a 30976-byte
 * allocation) and corrupts the heap. */
static v3d_u32 v3d_get_ub_pad(v3d_u32 uif_block_h, v3d_u32 height)
{
    v3d_u32 height_ub = height / uif_block_h;
    v3d_u32 height_offset_in_pc = height_ub % V3D_PAGE_CACHE_UB_ROWS;

    /* Perfectly aligned for UIF-XOR already -- no pad needed. */
    if (height_offset_in_pc == 0)
        return 0;

    if (height_offset_in_pc < V3D_PAGE_UB_ROWS_TIMES_1_5)
    {
        /* Fits entirely in the page cache -- don't pad. */
        if (height_ub < V3D_PAGE_CACHE_UB_ROWS)
            return 0;

        return V3D_PAGE_UB_ROWS_TIMES_1_5 - height_offset_in_pc;
    }

    /* Close to page-cache-size aligned -- round up and rely on XOR. */
    if (height_offset_in_pc > V3D_PAGE_CACHE_MINUS_1_5_UB_ROWS)
        return V3D_PAGE_CACHE_UB_ROWS - height_offset_in_pc;

    /* Far enough away (top and bottom) to not need any padding. */
    return 0;
}

/* MESA's u_minify: a mip level's dimension never drops below 1. */
static v3d_u32 v3d_minify(v3d_u32 value, v3d_u32 level)
{
    v3d_u32 v = value >> level;
    return v ? v : 1;
}

/* util_next_power_of_two. Loop rather than a bit trick: this runs a handful of
 * times per texture upload, never per pixel, and the loop is obviously correct
 * on a 32-bit target without needing a clz. */
static v3d_u32 v3d_next_pot(v3d_u32 v)
{
    v3d_u32 p = 1;
    while (p < v) p <<= 1;
    return p;
}

/*
 * Decides one mip level's tiling format and padded dimensions.
 *
 * v3d_texture_alloc uses it for level 0 and v3d_texture_alloc_mipchain for
 * every level, so the mip chain cannot drift from level 0's decision. MESA
 * applies exactly this chain per level in v3d_setup_slices (v3d_resource.c)
 * -- a 256x256 UIF_XOR base legitimately ends with LINEARTILE tail levels,
 * because each level re-decides on its own dimensions.
 */
static void v3d_texture_level_layout(v3d_u32 level_w, v3d_u32 level_h,
                                     v3d_u32 utile_w, v3d_u32 utile_h,
                                     enum v3d_memory_format* out_format,
                                     v3d_u32* out_padded_w, v3d_u32* out_padded_h,
                                     v3d_u32* out_ub_pad)
{
    v3d_u32 mb_width = utile_w * 2;
    v3d_u32 mb_height = utile_h * 2;

    *out_ub_pad = 0;

    if (level_w <= utile_w || level_h <= utile_h)
    {
        *out_format = V3D_MEMORY_FORMAT_LINEARTILE;
        *out_padded_w = V3D_ALIGN(level_w, utile_w);
        *out_padded_h = V3D_ALIGN(level_h, utile_h);
    }
    else if (level_w <= mb_width)
    {
        *out_format = V3D_MEMORY_FORMAT_UB_LINEAR_1_UIF_BLOCK_WIDE;
        *out_padded_w = V3D_ALIGN(level_w, mb_width);
        *out_padded_h = V3D_ALIGN(level_h, mb_height);
    }
    else if (level_w <= 2 * mb_width)
    {
        *out_format = V3D_MEMORY_FORMAT_UB_LINEAR_2_UIF_BLOCKS_WIDE;
        *out_padded_w = V3D_ALIGN(level_w, 2 * mb_width);
        *out_padded_h = V3D_ALIGN(level_h, mb_height);
    }
    else
    {
        v3d_u32 ub_pad;

        *out_padded_w = V3D_ALIGN(level_w, 4 * mb_width);
        *out_padded_h = V3D_ALIGN(level_h, mb_height);

        ub_pad = v3d_get_ub_pad(mb_height, *out_padded_h);
        *out_padded_h += ub_pad * mb_height;
        *out_ub_pad = ub_pad;

        /* MESA's XOR-selection rule (v3dv_image.c) -- only true when the
         * padded height lands exactly on a page-cache-block boundary.
         * NOTHING to do with width. */
        if ((*out_padded_h / mb_height) % V3D_PAGE_CACHE_UB_ROWS == 0)
            *out_format = V3D_MEMORY_FORMAT_UIF_XOR;
        else
            *out_format = V3D_MEMORY_FORMAT_UIF_NO_XOR;
    }
}

int v3d_texture_alloc(V3DDevice* device, V3DTexture* tex, v3d_u16 width, v3d_u16 height)
{
    v3d_u32 numComponents = 4;
    v3d_u32 utile_w, utile_h, mb_width, mb_height;
    v3d_u32 padded_width, padded_height;
    enum v3d_memory_format format;

    memset(tex, 0, sizeof(*tex));

    /* UIF allocation size: v3d_get_uif_pixel_offset (v3d_hw.c)'s macroblock
     * addressing -- identical to MESA's own v3d_get_uif_pixel_offset
     * (broadcom/common/v3d_tiling.c) -- computes tile addresses assuming
     * the image is at least 4 macroblock-columns wide. For narrower images
     * the computed mb_id legitimately exceeds what a naive width*height*4
     * allocation provides room for: laid out as UIF, a 16x16 RGBA8 texture
     * (only 2 macroblock-columns wide) reaches mb_id 5 for its last
     * macroblock, but only 4 macroblocks (mb_id 0-3) fit in 16*16*4=1024
     * bytes. The addressing math itself is correct (V3D requires it); the
     * allocation must be sized for it. As in MESA's texture-sizing code
     * (v3dv_image.c), UIF-tiled allocations pad WIDTH up to a multiple of
     * 4 macroblock-columns (not just the raw pixel width), and height up
     * to one macroblock. An allocation that is too small is written past
     * its end, corrupting whatever heap memory sits right after it; the
     * damage can surface much later as a crash in unrelated code, such as
     * the context teardown.
     *
     * This applies to images that use full UIF (>2 UIF blocks wide and taller
     * than one utile); the others use LINEARTILE or UBLINEAR with their own
     * padding (see the tiling-format comment above). */
    utile_w = v3d_utile_width((int)numComponents);
    utile_h = v3d_utile_height((int)numComponents);
    mb_width = utile_w * 2;
    mb_height = utile_h * 2;

    {
        /* The decision lives in v3d_texture_level_layout so the mip chain
         * runs the identical code rather than a second copy of it. */
        v3d_u32 ub_pad = 0;
        v3d_texture_level_layout((v3d_u32)width, (v3d_u32)height, utile_w, utile_h,
                                 &format, &padded_width, &padded_height, &ub_pad);
        tex->ub_pad = (v3d_u8)ub_pad;
    }

    tex->tiling_format = (v3d_u8)format;

    if (v3d_mem_alloc(device, &tex->texture_mem, padded_width * padded_height * numComponents + 256) < 0)
        return -1;

    tex->width = width;
    tex->height = height;
    tex->format = V3D_SRCFMT_RGBA8;
    tex->min_filter = V3D_TEXFILTER_NEAREST;
    tex->mag_filter = V3D_TEXFILTER_NEAREST;
    tex->mip_filter_nearest = 1;
    tex->wrap_s = V3D_TEXWRAP_BORDER;
    tex->wrap_t = V3D_TEXWRAP_BORDER;
    tex->resident = 0;
    tex->dirty = 1;

    /* Level 0 recorded as slice 0 so the chain and non-chain paths read the
     * same way downstream. num_levels 1 means "no chain" -- the state this
     * texture keeps unless a level > 0 upload promotes it. */
    tex->num_levels = 1;
    tex->levels[0].offset = 0;
    tex->levels[0].stride = padded_width * numComponents;
    tex->levels[0].padded_w = (v3d_u16)padded_width;
    tex->levels[0].padded_h = (v3d_u16)padded_height;
    tex->levels[0].tiling_format = (v3d_u8)format;
    tex->levels[0].ub_pad = tex->ub_pad;

    return 0;
}

/*
 * Builds the full mip-chain layout for a texture that already holds level 0,
 * reallocating and carrying level 0's existing bytes across.
 *
 * Layout is MESA's v3d_setup_slices, which is not the obvious one and is worth
 * stating plainly because both surprises are silent if you get them wrong:
 *
 *   1. Levels are packed SMALLEST FIRST. The loop runs downward from the last
 *      level accumulating an offset, so level 0 lands at the HIGHEST offset,
 *      and afterwards every level shifts up so level 0's OFFSET is 4K-aligned
 *      (relative to the 256-aligned base; the hardware wants that for UIF XOR).
 *   2. From level 2 down, dimensions come from the POWER-OF-TWO-PADDED base,
 *      not from minifying the real one. MESA's v3d_get_dimension_mpad exists
 *      solely for this. The two can disagree for a non-power-of-two size: at
 *      width 12, level 1 is 6, so the padded base is 16 and level 2 is 4 --
 *      not 12 >> 2 = 3.
 *
 * Promotion is a memcpy because level 0's layout does not depend on whether a
 * chain follows it -- same tiling, padded dimensions, stride and size, only
 * the offset moves.
 *
 * Returns 0 on success, -1 if the new allocation fails; the texture is then
 * left exactly as it was (tex->texture_mem and tex->levels[] restored), so the
 * caller can carry on unmipped.
 */
int v3d_texture_alloc_mipchain(V3DDevice* device, V3DTexture* tex, v3d_u8 want_levels)
{
    v3d_u32 numComponents = 4;
    v3d_u32 utile_w = v3d_utile_width((int)numComponents);
    v3d_u32 utile_h = v3d_utile_height((int)numComponents);
    v3d_u32 mb_height = utile_h * 2;
    v3d_u32 pot_w, pot_h, offset = 0, total, pad;
    v3d_mem old_mem;
    v3d_u32 level0_size;
    int i;
    /* The layout loop below rewrites levels[] before the allocation, so a
     * failed allocation must put the old table back. */
    v3d_u8 saved_levels[sizeof(tex->levels)];

    if (want_levels <= 1) return 0;
    if (want_levels > V3D_MAX_MIP_LEVELS) want_levels = V3D_MAX_MIP_LEVELS;
    if (tex->num_levels >= want_levels) return 0;  /* already big enough */

    /* pot_* per MESA's v3d_get_dimension_mpad(dim, 1): next power of two of
     * the level-1 dimension, doubled back to level-0 scale. */
    pot_w = v3d_next_pot(v3d_minify((v3d_u32)tex->width, 1)) * 2;
    pot_h = v3d_next_pot(v3d_minify((v3d_u32)tex->height, 1)) * 2;

    memcpy(saved_levels, tex->levels, sizeof(tex->levels));
    for (i = (int)want_levels - 1; i >= 0; i--)
    {
        v3d_u32 lw = (i < 2) ? v3d_minify((v3d_u32)tex->width, (v3d_u32)i)
                             : v3d_minify(pot_w, (v3d_u32)i);
        v3d_u32 lh = (i < 2) ? v3d_minify((v3d_u32)tex->height, (v3d_u32)i)
                             : v3d_minify(pot_h, (v3d_u32)i);
        enum v3d_memory_format fmt;
        v3d_u32 pw, ph, ubp, sz;

        v3d_texture_level_layout(lw, lh, utile_w, utile_h, &fmt, &pw, &ph, &ubp);

        sz = ph * pw * numComponents;

        tex->levels[i].offset = offset;
        tex->levels[i].stride = pw * numComponents;
        tex->levels[i].padded_w = (v3d_u16)pw;
        tex->levels[i].padded_h = (v3d_u16)ph;
        tex->levels[i].tiling_format = (v3d_u8)fmt;
        tex->levels[i].ub_pad = (v3d_u8)ubp;

        /* The hardware aligns level 1's base to a page when level 1 or below
         * could be UIF XOR; lower levels inherit it by being power-of-two
         * sized. MESA does the same, at the same place. */
        if (i == 1 && pw > 4 * (utile_w * 2) &&
            ph > V3D_PAGE_CACHE_MINUS_1_5_UB_ROWS * mb_height)
            sz = V3D_ALIGN(sz, V3D_UIFCFG_PAGE_SIZE);

        offset += sz;
    }

    total = offset;
    pad = V3D_ALIGN(tex->levels[0].offset, V3D_UIFCFG_PAGE_SIZE) - tex->levels[0].offset;
    if (pad)
    {
        total += pad;
        for (i = 0; i < (int)want_levels; i++)
            tex->levels[i].offset += pad;
    }

    level0_size = (v3d_u32)tex->levels[0].padded_h * tex->levels[0].stride;

    /* Allocate the new home BEFORE touching the old one, so a failure leaves
     * the old allocation in place. */
    old_mem = tex->texture_mem;
    if (v3d_mem_alloc(device, &tex->texture_mem, total + 256) < 0)
    {
        tex->texture_mem = old_mem;
        memcpy(tex->levels, saved_levels, sizeof(tex->levels));
        E(("v3d_texture_alloc_mipchain: alloc FAILED (size=%lu) -- staying unmipped\n",
           (ULONG)(total + 256)));
        return -1;
    }

    /* Carry level 0 across. Its tiled bytes are valid as-is: only the offset
     * changed, not the layout. */
    memcpy((char*)V3D_ALIGN_UP((v3d_uintptr)tex->texture_mem.hostptr, 256) + tex->levels[0].offset,
           (const char*)V3D_ALIGN_UP((v3d_uintptr)old_mem.hostptr, 256),
           level0_size);

    /* PARKED, not freed: level 0's base address moves here, and a draw binned
     * earlier this frame may still name the old one. Park failure is not fatal
     * on this path -- the promotion has already succeeded and the texture is
     * correct -- but the old block is then freed immediately, which keeps the
     * hazard parking exists to avoid: such a draw may still name it. */
    if (v3d_texture_park_mem(device, &old_mem) < 0)
        v3d_mem_free(device, &old_mem);

    tex->num_levels = want_levels;
    tex->mipmap_count = (v3d_u8)(want_levels - 1);
    tex->dirty = 1;

    /* Keep the scalar layout fields describing level 0. The loop above
     * recomputed levels[0] from (tex->width, tex->height) -- the same inputs
     * v3d_texture_alloc used -- so these already agree, but the shader state's
     * level_0_is_strictly_uif/level_0_xor_enable/level_0_ubpad bits read the
     * scalars while the uploads read levels[]. Writing them back makes that
     * agreement enforced rather than merely true. */
    tex->tiling_format = (enum v3d_memory_format)tex->levels[0].tiling_format;
    tex->ub_pad        = tex->levels[0].ub_pad;

    return 0;
}

void v3d_texture_free(V3DDevice* device, V3DTexture* tex)
{
    v3d_mem_free(device, &tex->texture_mem);
    tex->resident = 0;
    tex->dirty = 0;
}

/* ====================================================================
 * TEXTURE RENAMING -- the mid-frame reuse hazard.
 *
 * This is a tile renderer: a draw is BINNED carrying the texture's ADDRESS and
 * no texel is read until the pass renders. v3d_texture_emit_state bakes that
 * address into a shader-state record (the `texture_base_pointer_rshift_6` field
 * below), and the command list carries only that snapshot -- never a
 * V3DTexture*. So modifying a texture's bytes after a draw has been binned
 * against it retroactively changes what that earlier draw samples: upload A,
 * draw left, upload B, draw right renders B on both sides.
 *
 * Giving the new contents a FRESH allocation, and letting the already-binned
 * draws keep the old one, fixes that without the GPU ever stalling. It is the
 * standard buffer-renaming answer and it exploits the same property: because
 * the address is a snapshot, the old block stays correct for as long as we
 * decline to free it.
 *
 * WHY NOT SPLIT THE PASS: splitting is also correct, and ruinous for an
 * application that uploads into ONE shared scratch texture and draws
 * immediately, once per surface -- a frame with N such surfaces pays N-1
 * full-screen store/reload cycles with two blocking GPU waits each, against
 * zero stalls for a rename.
 *
 * THE OLD BLOCK CANNOT BE FREED IMMEDIATELY. The command list naming it is
 * submitted at present time and the GPU finishes with it asynchronously, so a
 * FreeVec here would hand the system pool memory the hardware is still reading.
 * Parked blocks are therefore released only once a render wait has actually
 * succeeded -- see v3d_texture_drain_parked and its caller in gl/src/context.c.
 * ==================================================================== */

/* Capped deliberately, and OVERFLOW IS NOT SILENT: v3d_texture_park_mem returns
 * failure, and v3d_texture_rename then fails so its caller falls back to the
 * synchronous pass split, which is slow but correct. */
/* SIZED FOR SPEED, NOT FOR MEMORY. The target has RAM to spare and does not
 * have bandwidth to spare, so both caps are set well past any realistic
 * per-frame rename count; they exist to never be reached.
 *
 * Peak memory is roughly (renames per frame) x 2 x texture size, not
 * V3D_TEX_PARK_MAX x 2: blocks MOVE from the park list into the pool at the
 * drain, they do not accumulate in both. For a 128x128 texture (65,856 bytes)
 * even 200 renames a frame is about 26MB.
 *
 * THE POOL MUST NOT BE SMALLER THAN THE PARK LIST. A smaller pool throws every
 * drained block past its capacity at FreeVec and makes the next frame
 * re-AllocVec it -- with MEMF_REVERSE (a last-fit scan) and MEMF_CLEAR zeroing
 * a block about to be overwritten. Keep them equal. */
#define V3D_TEX_PARK_MAX 512
#define V3D_TEX_POOL_MAX V3D_TEX_PARK_MAX

typedef struct {
    v3d_mem mem;
    v3d_u8  submitted;   /* 1 once a CL that may name this block has been submitted */
} v3d_parked_mem;

static v3d_parked_mem s_park[V3D_TEX_PARK_MAX];
static int s_park_count = 0;

/* Recycling pool, fed ONLY from the drain (never from the park -- a block still
 * in flight must not be handed back out). Keyed on exact requested size, which
 * is sufficient because tiling is a pure address permutation of
 * (width, height, cpp): two allocations of the same requested size and the same
 * dimensions have identical layout. It exists to dodge AllocVec's MEMF_CLEAR,
 * which would otherwise zero the whole block on every rename for nothing --
 * every caller either copies the old texels over the whole block or is about
 * to overwrite it. */
static v3d_mem s_pool[V3D_TEX_POOL_MAX];
static int s_pool_count = 0;

/* Renames performed, monotonic. This is the direct measurement of how often the
 * hazard actually fires. Costs one increment on a path that is already
 * allocating. */
int g_mglv3d_texture_renames = 0;

/* Times a rename could NOT be done -- out of memory, or the park list full --
 * and the caller had to fall back to a synchronous pass split. Should be 0 on
 * a healthy machine; a non-zero value is the signal to raise V3D_TEX_PARK_MAX
 * rather than to go looking for a rendering bug. */
int g_mglv3d_texture_rename_fails = 0;

static int tex_pool_alloc(V3DDevice* device, v3d_mem* out, v3d_u32 size)
{
    int i;

    for (i = 0; i < s_pool_count; i++)
    {
        if (s_pool[i].size == size)
        {
            *out = s_pool[i];
            s_pool[i] = s_pool[--s_pool_count];
            return 0;
        }
    }
    return v3d_mem_alloc(device, out, size);
}

int v3d_texture_park_mem(V3DDevice* device, v3d_mem* mem)
{
    (void)device;

    if (!mem->hostptr)
        return 0;                    /* nothing to park */
    if (s_park_count >= V3D_TEX_PARK_MAX)
        return -1;                   /* caller must fall back -- never drop silently */

    s_park[s_park_count].mem = *mem;
    s_park[s_park_count].submitted = 0;
    s_park_count++;

    /* The caller no longer owns the block; zeroing here makes a stray
     * v3d_mem_free on it a no-op rather than a double free. */
    mem->size = 0;
    mem->handle = 0;
    mem->busaddr = 0;
    mem->hostptr = 0;
    return 0;
}

void v3d_texture_parked_mark_submitted(void)
{
    int i;

    for (i = 0; i < s_park_count; i++)
        s_park[i].submitted = 1;
}

void v3d_texture_drain_parked(V3DDevice* device)
{
    int i = 0;

    while (i < s_park_count)
    {
        /* Parked but NOT yet submitted: the CL naming it has not gone to the
         * hardware, so this block is still live for a draw in the frame being
         * built right now. MGLFlushPendingRender can run in that state. */
        if (!s_park[i].submitted)
        {
            i++;
            continue;
        }

        if (s_pool_count < V3D_TEX_POOL_MAX)
            s_pool[s_pool_count++] = s_park[i].mem;
        else
            v3d_mem_free(device, &s_park[i].mem);

        /* Swap-remove; do NOT advance i -- the element just moved into this
         * slot has not been tested yet. */
        s_park[i] = s_park[--s_park_count];
    }
}

void v3d_texture_drain_parked_all(V3DDevice* device)
{
    int i;

    for (i = 0; i < s_park_count; i++)
        v3d_mem_free(device, &s_park[i].mem);
    s_park_count = 0;

    for (i = 0; i < s_pool_count; i++)
        v3d_mem_free(device, &s_pool[i]);
    s_pool_count = 0;
}

int v3d_texture_rename(V3DDevice* device, V3DTexture* tex, int copy_old)
{
    v3d_mem old_mem;
    v3d_u32 size;

    if (!tex || !tex->texture_mem.hostptr)
        return -1;

    size    = tex->texture_mem.size;
    old_mem = tex->texture_mem;

    /* Allocate BEFORE parking, so a failure leaves the texture exactly as it
     * was and the caller can fall back to the pass split. Same ordering
     * discipline v3d_texture_alloc_mipchain already uses. */
    if (tex_pool_alloc(device, &tex->texture_mem, size) < 0)
    {
        tex->texture_mem = old_mem;
        g_mglv3d_texture_rename_fails++;
        return -1;
    }

    if (copy_old)
    {
        /* Between the 256-ALIGNED BASES, never the raw hostptrs. Each AllocVec
         * block is only 64-aligned and carries its own slack up to the 256
         * boundary, so a hostptr-to-hostptr copy would shear the whole image by
         * the difference between the two slacks -- which is zero often enough
         * to look like it works. This is the same arithmetic
         * v3d_texture_alloc_mipchain uses to carry level 0 across, and it is
         * exact for the same reason: tiling is a pure address permutation fixed
         * by (width, height, cpp), so two allocations of the same dimensions
         * are byte-identical in layout.
         *
         * size - 256 is the guaranteed-usable span from the aligned base: the
         * raw block is size + V3D_ALLOCVEC_ALIGN and the base sits at most 192
         * bytes past hostptr. */
        memcpy((char*)V3D_ALIGN_UP((v3d_uintptr)tex->texture_mem.hostptr, 256),
               (const char*)V3D_ALIGN_UP((v3d_uintptr)old_mem.hostptr, 256),
               (size_t)(size - 256));
    }

    if (v3d_texture_park_mem(device, &old_mem) < 0)
    {
        /* Park list full. Undo cleanly rather than free a block the GPU may
         * still be reading -- the caller falls back to a pass split. */
        v3d_mem_free(device, &tex->texture_mem);
        tex->texture_mem = old_mem;
        g_mglv3d_texture_rename_fails++;
        return -1;
    }

    g_mglv3d_texture_renames++;

    /* The new block cannot be named by any already-binned draw, so a SECOND
     * modification before the next draw is free -- it takes the in-place path.
     * This is what keeps repeated uploads to one texture from renaming more
     * than once per intervening draw. */
    tex->last_draw_frame = 0;
    tex->dirty = 1;
    return 0;
}

/*
 * Uploads one mip level. Higher levels differ from level 0 only in where they
 * land and which tiling they use, both of which v3d_texture_alloc_mipchain
 * already decided.
 *
 * Note the dimensions here are the level's REAL ones (width >> level), not
 * the padded ones in tex->levels[]: the padding is how much memory the level
 * occupies, the real size is how much data the application actually hands
 * over, and mixing them up would tile the wrong number of rows.
 */
void v3d_texture_upload_rgba8_level(V3DDevice* device, V3DTexture* tex, v3d_u8 level,
                                     const void* src, v3d_u32 srcStride)
{
    struct ExecBase* const SysBase = device->sysbase;
    char* tiledData;
    v3d_u32 level_w, level_h, dstStride;
    v3d_texture_box box;
    enum v3d_memory_format format;
    ULONG len;

    if (level >= tex->num_levels) return;

    level_w = v3d_minify((v3d_u32)tex->width, (v3d_u32)level);
    level_h = v3d_minify((v3d_u32)tex->height, (v3d_u32)level);
    dstStride = level_w * 4;

    tiledData = (char*)V3D_ALIGN_UP((v3d_uintptr)tex->texture_mem.hostptr, 256)
                + tex->levels[level].offset;

    /* Use whatever format the allocation actually decided (LINEARTILE/
     * UBLINEAR_1/UBLINEAR_2/full UIF), never an assumed full UIF -- must
     * match exactly, since the padding was computed for THIS format
     * specifically. Per level: a 256x256 UIF_XOR base legitimately has
     * LINEARTILE tail levels. */
    format = (enum v3d_memory_format)tex->levels[level].tiling_format;

    box.x = 0; box.y = 0; box.z = 0;
    box.width = (int)level_w;
    box.height = (int)level_h;
    box.depth = 1;

    v3d_store_tiled_image(tiledData, dstStride, (void*)src, srcStride, format, 4, (int)level_h, &box);

    /* One flush per upload -- AllocVec is v3d_mem_alloc's default backend
     * (v3d_device.c's V3D_MEM_USE_MAILBOX comment); harmless no-op on
     * the mailbox path. */
    len = (ULONG)tex->texture_mem.size;
    CachePreDMA(tex->texture_mem.hostptr, &len, 0);

    if (level > tex->max_level_uploaded)
        tex->max_level_uploaded = level;

    tex->resident = 1;
    tex->dirty = 0;
}

/* A level-0 call, so there is one upload implementation rather than two. */
void v3d_texture_upload_rgba8(V3DDevice* device, V3DTexture* tex, const void* src, v3d_u32 srcStride)
{
    v3d_texture_upload_rgba8_level(device, tex, 0, src, srcStride);
}

void v3d_texture_upload_rgba8_subimage(V3DDevice* device, V3DTexture* tex, v3d_u8 level,
                                        const void* src, v3d_u32 srcStride,
                                        int xoffset, int yoffset, int width, int height)
{
    struct ExecBase* const SysBase = device->sysbase;
    char* tiledData;
    v3d_u32 level_w, level_h, dstStride;
    v3d_texture_box box;
    enum v3d_memory_format format;
    ULONG len;

    if (level >= tex->num_levels) return;

    level_w = v3d_minify((v3d_u32)tex->width,  (v3d_u32)level);
    level_h = v3d_minify((v3d_u32)tex->height, (v3d_u32)level);
    dstStride = level_w * 4;

    tiledData = (char*)V3D_ALIGN_UP((v3d_uintptr)tex->texture_mem.hostptr, 256)
                + tex->levels[level].offset;

    /* Same rule as v3d_texture_upload_rgba8_level -- a sub-image update
     * targets the SAME already-resident texture, so it must use the same
     * tiling format that level was actually allocated with. Per level: a
     * 256x256 UIF_XOR base legitimately has LINEARTILE tail levels, and
     * the padding was computed for THAT format. */
    format = (enum v3d_memory_format)tex->levels[level].tiling_format;

    box.x = xoffset;
    box.y = yoffset;
    box.z = 0;
    box.width = width;
    box.height = height;
    box.depth = 1;

    v3d_store_tiled_image(tiledData, dstStride, (void*)src, srcStride, format, 4, (int)level_h, &box);

    /* Flush the whole texture_mem region, not just the sub-box -- matches
     * v3d_texture_upload_rgba8's own one-shot flush; the sub-box write
     * doesn't touch contiguous memory (tiled, not linear), so a precise
     * partial flush isn't straightforward and the whole-buffer flush is
     * cheap enough not to bother. */
    len = (ULONG)tex->texture_mem.size;
    CachePreDMA(tex->texture_mem.hostptr, &len, 0);
}

/*
 * BYTE ORDER. LE32 is a REAL 4-byte reversal, correct for actual 32-bit
 * integer fields (addresses, counts) that the 68k CPU and little-endian
 * V3D hardware must agree on as ONE number. Texel data is 4 INDEPENDENT
 * per-channel bytes, not one integer: the conventional word
 * (A<<24)|(R<<16)|(G<<8)|B would come out of LE32 as B,G,R,A in memory
 * (byte0=B), swapping R and B on screen. So every branch below builds its
 * word as (A<<24)|(B<<16)|(G<<8)|R -- R and B (or r8/b8) swapped relative
 * to the usual ARGB packing -- so that AFTER LE32's reversal, memory holds
 * genuine R,G,B,A (byte0=R). MESA's `v3dx_format_table.c` has hardware format
 * RGBA8 + identity swizzle expect byte0=R, matching
 * PIPE_FORMAT_R8G8B8A8_UNORM's own convention; BGRA-ordered memory needs
 * a non-identity SWIZ_ZYXW swizzle instead, which this driver's
 * `v3d_texture_emit_state` does NOT set -- it uses plain identity, so the
 * DATA must be genuine R,G,B,A in memory. This matches the direct-GL_RGBA
 * fast path (`GLTexImage2DNoMIP`'s srcfmt==V3D_SRCFMT_RGBA8 branch, which
 * skips this function entirely and copies the app's own R,G,B,A bytes
 * verbatim).
 */
void v3d_texture_convert_row(v3d_u32* dst, const v3d_u8* src, v3d_u32 count, v3d_u8 srcFormat)
{
    if (srcFormat == V3D_SRCFMT_RGBA8)
    {
        while (count--)
        {
            *dst++ = LE32(((v3d_u32)src[2] << 16) | ((v3d_u32)src[1] << 8) | ((v3d_u32)src[0] << 0) | ((v3d_u32)src[3] << 24));
            src += 4;
        }
    }
    else if (srcFormat == V3D_SRCFMT_ARGB8)
    {
        while (count--)
        {
            *dst++ = LE32(((v3d_u32)src[0] << 24) | ((v3d_u32)src[3] << 16) | ((v3d_u32)src[2] << 8) | ((v3d_u32)src[1] << 0));
            src += 4;
        }
    }
    else if (srcFormat == V3D_SRCFMT_RGB8)
    {
        while (count--)
        {
            *dst++ = LE32((0xffUL << 24) | ((v3d_u32)src[2] << 16) | ((v3d_u32)src[1] << 8) | ((v3d_u32)src[0] << 0));
            src += 3;
        }
    }
    else if (srcFormat == V3D_SRCFMT_ARGB4)
    {
        while (count--)
        {
            v3d_u16 val = *(const v3d_u16*)src;
            v3d_u32 a4 = (val >> 12) & 0xf, r4 = (val >> 8) & 0xf, g4 = (val >> 4) & 0xf, b4 = val & 0xf;
            v3d_u32 a8 = (a4 << 4) | a4, r8 = (r4 << 4) | r4, g8 = (g4 << 4) | g4, b8 = (b4 << 4) | b4;
            *dst++ = LE32((a8 << 24) | (b8 << 16) | (g8 << 8) | r8);
            src += 2;
        }
    }
    else if (srcFormat == V3D_SRCFMT_ARGB1555)
    {
        while (count--)
        {
            v3d_u16 val = *(const v3d_u16*)src;
            v3d_u32 a8 = (val & 0x8000) ? 255 : 0;
            v3d_u32 r5 = (val >> 10) & 0x1f, g5 = (val >> 5) & 0x1f, b5 = val & 0x1f;
            v3d_u32 r8 = (r5 << 3) | (r5 >> 2), g8 = (g5 << 3) | (g5 >> 2), b8 = (b5 << 3) | (b5 >> 2);
            *dst++ = LE32((a8 << 24) | (b8 << 16) | (g8 << 8) | r8);
            src += 2;
        }
    }
    else if (srcFormat == V3D_SRCFMT_RGB565)
    {
        while (count--)
        {
            v3d_u16 val = *(const v3d_u16*)src;
            v3d_u32 r5 = (val >> 11) & 0x1f, g6 = (val >> 5) & 0x3f, b5 = val & 0x1f;
            v3d_u32 r8 = (r5 << 3) | (r5 >> 2), g8 = (g6 << 2) | (g6 >> 4), b8 = (b5 << 3) | (b5 >> 2);
            *dst++ = LE32((0xffUL << 24) | (b8 << 16) | (g8 << 8) | r8);
            src += 2;
        }
    }
    else if (srcFormat == V3D_SRCFMT_LA8)
    {
        while (count--)
        {
            v3d_u8 l = src[0];
            v3d_u8 a = src[1];
            *dst++ = LE32(((v3d_u32)a << 24) | ((v3d_u32)l << 16) | ((v3d_u32)l << 8) | (v3d_u32)l);
            src += 2;
        }
    }
    /* Single-byte formats. GL_LUMINANCE reads as (L,L,L,1).
     * GL_ALPHA reads as (1,1,1,A): the fixed-function texture functions for
     * an ALPHA-format texture leave RGB untouched (MODULATE gives Cf, not
     * Cf*0), and the original MiniGL's own A8_ARGB conversion wrote RGB=0xF
     * for exactly that reason. (0,0,0,A) would multiply every modulated
     * colour to black. Same word layout as the LA8 branch above. */
    else if (srcFormat == V3D_SRCFMT_L8)
    {
        while (count--)
        {
            v3d_u8 l = *src++;
            *dst++ = LE32((0xffUL << 24) | ((v3d_u32)l << 16) | ((v3d_u32)l << 8) | (v3d_u32)l);
        }
    }
    else if (srcFormat == V3D_SRCFMT_A8)
    {
        while (count--)
        {
            v3d_u8 a = *src++;
            *dst++ = LE32(((v3d_u32)a << 24) | 0x00ffffffUL);
        }
    }
    else
    {
        D(("v3d_texture_convert_row: unsupported source format %d\n", (int)srcFormat));
    }
}

void v3d_texture_emit_state(V3DDevice* device, V3DContext* context, V3DTexture* tex,
                             ULONG* outTextureShaderStateAddress, ULONG* outTextureSamplerStateAddress)
{
    char* tiledData = (char*)V3D_ALIGN_UP((v3d_uintptr)tex->texture_mem.hostptr, 256);
    v3d_texture_shader_state* ts;
    v3d_sampler_state* ss;
    ULONG* swivel;
    UBYTE* swivel24;
    UWORD* swivel16;
    UBYTE temp;
    UBYTE minFilterNearest, magFilterNearest;

    /* Texture shader state (PoC v3d_cle.c:836-873). */
    /* Address is captured from the pointer the claim actually returns,
     * AFTER the claim -- not via CurrentBufferAddress() before it. If
     * THIS claim is what triggers state_buf to grow, the real data lands
     * in the NEW (post-growth) buffer; capturing the address beforehand
     * would bake in the OLD buffer's now-stale offset instead, feeding the
     * GPU an address that was never populated with this struct's real
     * values. */
    AlignBuffer(context, 16);
    ts = (v3d_texture_shader_state*)v3d_cl_claim_fast(device, &context->state_mem[context->build_slot], &context->state_buf[context->build_slot], sizeof(v3d_texture_shader_state), &context->frame);
    *outTextureShaderStateAddress = (ULONG)ts;

    /* Several of this struct's declared bitfields (reverse_standard_
     * border_color/ahdr/srgb/flip_s_and_t_on_incoming_request/
     * flip_texture_x_axis, array_stride_64_byte_aligned, pad2 and
     * friends, the trailing pad:32 word) are never explicitly assigned
     * below. Without this memset they would hold whatever was already
     * sitting in the destination memory, which is undefined on every
     * reuse of the state buffer, since v3d_cl_reset only rewinds the
     * write cursor and never re-zeros the buffer -- and whatever garbage
     * lands in texture_type/swizzle-adjacent/XOR-tiling bits can make the
     * TMU misinterpret the texture entirely. Zeroing the whole struct up
     * front makes every field deterministic regardless of address
     * reuse. */
    memset(ts, 0, sizeof(*ts));

    /* The base pointer addresses LEVEL 0, not the start of the allocation.
     * Those differ once a mip chain exists, because levels are packed
     * smallest-first and level 0 therefore sits at the HIGHEST offset -- the
     * hardware walks DOWNWARD from here to reach the smaller levels, deriving
     * their addresses from the layout rules rather than from a table. MESA
     * does exactly this: texture_base_pointer = bo->offset +
     * v3d_layer_offset(prsc, 0, layer), and v3d_layer_offset returns
     * slices[LEVEL 0].offset (v3dx_state.c). */
    ts->texture_base_pointer_rshift_6 = ((ULONG)tiledData + tex->levels[0].offset) >> 6;
    ts->flip_texture_y_axis = FALSE;

    /* Tell the TMU how many levels it may select. Both stay 0 for an unmipped
     * texture (num_levels 1), which is what the memset above already gave us. */
    ts->base_level = 0;
    ts->max_level = (ULONG)tex->max_level_uploaded;
    ts->image_width_lo = tex->width & 0x3f;
    ts->image_width_hi = tex->width >> 6;
    ts->image_height = tex->height;
    ts->image_depth_lo = 1 & 0x3FF;
    ts->image_depth_hi = 1 >> 10;
    ts->texture_type = V3D_TEXTURE_DATA_FORMAT_RGBA8;
    ts->swizzle_r = v3d_SWIZZLE_RED;
    ts->swizzle_g = v3d_SWIZZLE_GREEN;
    ts->swizzle_b = v3d_SWIZZLE_BLUE;
    ts->swizzle_a = v3d_SWIZZLE_ALPHA;

    /* As in MESA's v3dvx_image.c (v3dvx_pack TEXTURE_SHADER_STATE),
     * level_0_is_strictly_uif/level_0_xor_enable/level_0_ubpad/extended
     * must reflect the real tiling format, not stay zeroed -- without them
     * a UIF_XOR texture renders corrupted. MESA sets
     * level_0_is_strictly_uif for BOTH UIF_XOR and UIF_NO_XOR; this driver
     * deliberately narrows it to the UIF_XOR branch, so UIF_NO_XOR textures
     * are emitted with these bits at 0, level_0_ubpad included even when
     * their ub_pad is non-zero; setting them there as MESA does has not been
     * tried. Every other tiling_format's emitted state also keeps these bits
     * at 0. */
    if (tex->tiling_format == V3D_MEMORY_FORMAT_UIF_XOR)
    {
        ts->level_0_is_strictly_uif = TRUE;
        ts->level_0_xor_enable = TRUE;
        ts->level_0_ubpad = tex->ub_pad;
        ts->extended = TRUE;
    }

    swivel = (ULONG*)ts;
    swivel[0] = LE32(swivel[0]);
    swivel[1] = LE32(swivel[1]);
    swivel[2] = LE32(swivel[2]);
    swivel[3] = LE32(swivel[3]);
    swivel[4] = LE32(swivel[4]);

    /* Sampler state (PoC v3d_cle.c:874-890). filter/wrap taken from the
     * V3DTexture object instead of hardcoded.
     *
     * A *_MIPMAP min filter keeps its within-level filter: the test must
     * accept V3D_TEXFILTER_LINEAR_MIPMAP as well as V3D_TEXFILTER_LINEAR,
     * or GL_LINEAR_MIPMAP_NEAREST collapses to point sampling.
     *
     * The min and mag fields are driven independently, which is what GL
     * requires: the mag filter applies when magnifying, the min filter when
     * minifying. */
    minFilterNearest = (tex->min_filter == V3D_TEXFILTER_LINEAR ||
                        tex->min_filter == V3D_TEXFILTER_LINEAR_MIPMAP) ? 0 : 1;
    /* GL only permits GL_NEAREST/GL_LINEAR for the mag filter, so the
     * _MIPMAP arm here is defensive rather than reachable from a conformant
     * app -- but if one does pass it, linear is the honest reading. */
    magFilterNearest = (tex->mag_filter == V3D_TEXFILTER_LINEAR ||
                        tex->mag_filter == V3D_TEXFILTER_LINEAR_MIPMAP) ? 0 : 1;

    /* Same rule as v3d_texture_shader_state above -- capture the address
     * from the returned pointer, after the claim/possible growth. */
    AlignBuffer(context, 16);
    ss = (v3d_sampler_state*)v3d_cl_claim_fast(device, &context->state_mem[context->build_slot], &context->state_buf[context->build_slot], sizeof(v3d_sampler_state), &context->frame);
    *outTextureSamplerStateAddress = (ULONG)ss;

    /* Same as v3d_texture_shader_state above -- this struct has its own
     * set of never-explicitly-assigned bitfields (srgb_disable,
     * anisotropy_enable, fixed_bias, maximum_anisotropy,
     * border_color_mode, wrap_i_border, wrap_r, and more further down).
     * Zero it up front for the same reason. */
    memset(ss, 0, sizeof(*ss));

    ss->mag_filter_nearest = magFilterNearest;
    ss->min_filter_nearest = minFilterNearest;
    /* Filtering BETWEEN levels. Left at the memset's 0 = linear, every
     * mipmapped texture would be silently promoted to trilinear regardless
     * of what the app asked for -- including GL_LINEAR_MIPMAP_NEAREST, which
     * only reads one level. This is the axis V3D_TEXFILTER_* cannot express;
     * see tex_GLMipFilterNearest. */
    ss->mip_filter_nearest = tex->mip_filter_nearest ? 1 : 0;
    ss->depth_compare_function = V3D_COMPARE_FUNC_NEVER;
    ss->wrap_s = (tex->wrap_s == V3D_TEXWRAP_REPEAT) ? V3D_WRAP_MODE_REPEAT :
                 (tex->wrap_s == V3D_TEXWRAP_CLAMP)  ? V3D_WRAP_MODE_CLAMP  : V3D_WRAP_MODE_BORDER;
    ss->wrap_t = (tex->wrap_t == V3D_TEXWRAP_REPEAT) ? V3D_WRAP_MODE_REPEAT :
                 (tex->wrap_t == V3D_TEXWRAP_CLAMP)  ? V3D_WRAP_MODE_CLAMP  : V3D_WRAP_MODE_BORDER;
    /* LOD clamp. With a real chain, open the range up to the levels that
     * actually exist -- anything narrower would lock sampling to one level and
     * defeat the point of having uploaded a chain.
     *
     * The no-chain arm must NOT pin both ends to 1.0. The hardware decides
     * between the min and the mag filter by whether the computed LOD is above
     * the base level, so a LOD pinned to exactly 1.0 reads as minification
     * ALWAYS -- and GL_TEXTURE_MAG_FILTER would do nothing for any texture
     * without a mip chain.
     *
     * MESA clamps the same case with MIN2(existing, 1.0/256.0) and says why:
     * the LOD must be held to the base level, but must still be allowed
     * "fractionally over the baselevel, so that the HW can decide between the
     * min and mag filters" (v3dx_state.c).
     *
     * Note MIN2, not assignment. A default GL sampler has min_lod 0, so MESA
     * ends up with min = 0 and max = 1/256 -- an ASYMMETRIC range. That
     * matters: only a range whose bottom reaches 0 permits the LOD to fall to
     * or below the base level, which is what magnification means and what
     * makes the hardware pick the mag filter. Setting BOTH ends to 1/256 does
     * not work: [1/256, 1/256] is just as unable to reach 0 as [1.0, 1.0].
     * The top of the range is what pins sampling to the base level; the
     * bottom must stay 0. */
    if (tex->max_level_uploaded > 0)
    {
        ss->min_level_of_detail = V3D_FLOAT_TO_U4_8(0.f);
        ss->max_level_of_detail = V3D_FLOAT_TO_U4_8((float)tex->max_level_uploaded);
    }
    else
    {
        ss->min_level_of_detail = V3D_FLOAT_TO_U4_8(0.f);
        ss->max_level_of_detail = V3D_FLOAT_TO_U4_8(1.f / 256.f);
    }

    swivel24 = (UBYTE*)ss;
    temp = swivel24[3];
    swivel24[3] = swivel24[1];
    swivel24[1] = temp;

    swivel16 = (UWORD*)ss;
    swivel16[3] = LE16(swivel16[3]);
}

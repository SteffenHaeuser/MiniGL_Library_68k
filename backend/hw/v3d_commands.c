/*
 * (C) 2025-2026 Dennis van der Boon
 */

/*
 * Migrated from PoC/v3d_commands.c. The PoC functions include the CLE
 * packet writers for the ~37 struct types PoC/v3d_v3d.h's author manually
 * verified for the big-endian 68k/little-endian V3D mismatch. DepthOffset,
 * BlendEnables, BlendCfg, NumberOfLayers, IndexedPrimList, IndexBufferSetup
 * and LoadTileBufferGeneral have no PoC counterpart. Every
 * byte-manipulation "swivel"/reorder hack in the PoC functions (the
 * `swivelNN[...]` blocks, the TileCoordinates/MulticoreRenderingSupertileCFG
 * byte swaps) is kept EXACTLY as in PoC -- these are the hand-tuning this
 * file is built on, not incidental code to "clean up".
 */

#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#include <exec/execbase.h>
#include <exec/resident.h>
#include <exec/initializers.h>
#include <exec/alerts.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>

#include "v3d_debug.h"
#include "devicetree_protos.h"
#include "v3d_regs.h"
#include "../include/v3d_context.h"
#include "../include/v3d_commands.h"
#include "v3d_hw.h"

//***************************************************************************

/*
 * DEFECT: swap_float32 returns the byte-swapped bits AS A FLOAT, so they can
 * travel through the FPU on the way back into the caller's buffer. When the
 * swapped pattern reads as a signaling NaN -- low 7 bits of the real value all
 * ones, bit 15 set, bit 14 clear, about one value in 512 -- the FPU makes it a
 * quiet NaN by setting a fraction bit, and once the V3D swaps back, bit 14 of
 * the real value is set: 1068.08 becomes 1070.08. Use swap_float32_into below,
 * which never loads the bits into the FPU; nothing in this driver calls this
 * one.
 */
float swap_float32(float val)
{
    ULONG u;
    float res;

    memcpy(&u, &val, sizeof(u));

    u = LE32(u);

    memcpy(&res, &u, sizeof(res));

    return res;
}

/* The float the V3D reads: val's IEEE bits byte-swapped and copied into the
 * four bytes at dst as an integer, never loaded into the FPU. dst is void * so
 * the packed CLE packet fields can be passed directly. */
void swap_float32_into(void *dst, float val)
{
    ULONG u;

    memcpy(&u, &val, sizeof(u));

    u = LE32(u);

    memcpy(dst, &u, sizeof(u));
}

//***************************************************************************

ULONG float_to_ulong(float f)
{
    return *(ULONG*)&f;
}

//***************************************************************************

/*
 * Like v3d_buffer_align: advance `used`, leave `capacity` alone. `capacity`
 * is the buffer's TOTAL size (v3d_buffer_claim_memory and v3d_buffer_write
 * compute the free space as `capacity - used`), so also lowering it here
 * would count the alignment padding as consumed twice and shrink the
 * buffer's apparent size on every call (see v3d_clbuf.h).
 */
void AlignBuffer(V3DContext* context, ULONG alignment)  //there is no overflow checking whatsoever (TODO)
{
     v3d_static_buffer* buffer = context->current_buf;
     buffer->used = (buffer->used + (alignment - 1)) & ~(alignment - 1);
}

//***************************************************************************

void SetBuffer(V3DContext* context, v3d_static_buffer* buffer)
{
    context->current_buf = buffer;
}

//***************************************************************************

ULONG CurrentBufferAddress(V3DContext* context)
{
    v3d_static_buffer* buffer = context->current_buf;

    return ((ULONG)buffer->start + buffer->used);
}

//***************************************************************************

void TileBinningModeCfg(V3DContext* context, UBYTE taibz, UBYTE tabz, UBYTE targets, UBYTE bpp, BOOL msaa, BOOL db, UWORD width, UWORD height)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_TILE_BINNING_MODE_CFG,
                               v3d_tile_binning_mode_cfg, binningModeConfig);

    binningModeConfig->tile_allocation_initial_block_size = taibz;
    binningModeConfig->tile_allocation_block_size         = tabz;
    binningModeConfig->number_of_render_targets_minus_one = targets - 1;
    binningModeConfig->maximum_bpp_of_all_render_targets  = bpp;
    binningModeConfig->multisample_mode_4x                = msaa;
    binningModeConfig->double_buffer_in_non_ms_mode       = db;
    binningModeConfig->width_in_pixels_minus_one          = LE16(width  - 1);
    binningModeConfig->height_in_pixels_minus_one         = LE16(height - 1);
}

//***************************************************************************

void OcclusionQueryCounter(V3DContext* context, ULONG address)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_OCCLUSION_QUERY_COUNTER,
                               v3d_occlusion_query_counter, occlusionQueryCounter);

    occlusionQueryCounter->address = LE32(address);
}

//***************************************************************************

void DoCommand(V3DContext* context, UBYTE command)
{
     v3d_static_buffer* buffer = context->current_buf;
     /* v3d_flush_vcd_cache is a placeholder (size = 1); claimed directly, with a NULL check, because V3D_BUFFER_QUEUE_OPERATION_NO_ARGUMENTS doesn't NULL-check. */
     v3d_flush_vcd_cache* quickQueue = (v3d_flush_vcd_cache*)v3d_buffer_claim_memory(buffer, sizeof(v3d_flush_vcd_cache));
     if (quickQueue)
     {
         quickQueue->operation = command;
     }
}

//***************************************************************************

void ClipWindow(V3DContext* context, UWORD lpc, UWORD bpc, UWORD width, UWORD height)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_CLIPWINDOW,
                               v3d_clipwindow, clipWindow);

    clipWindow->clip_window_left_pixel_coordinate   = LE16(lpc);
    clipWindow->clip_window_bottom_pixel_coordinate = LE16(bpc);
    clipWindow->clip_window_width_in_pixels         = LE16(width);
    clipWindow->clip_window_height_in_pixels        = LE16(height);
}

//***************************************************************************

void CfgBits(V3DContext* context, UBYTE effp, UBYTE erfp, UBYTE cp, UBYTE edo, UBYTE lr, UBYTE rom, UBYTE d3dwftm,
             UBYTE dtf, UBYTE zue, UBYTE eze, UBYTE ezue, UBYTE se, UBYTE be, UBYTE d3dpfm, UBYTE d3dpv)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_CFG_BITS,
                               v3d_cfg_bits, cfgBits);

    cfgBits->enable_forward_facing_primitive    = effp;
    cfgBits->enable_reverse_facing_primitive    = erfp;
    cfgBits->clockwise_primitives               = cp;
    cfgBits->enable_depth_offset                = edo;
    cfgBits->line_rasterization                 = lr;
    cfgBits->rasterizer_oversample_mode         = rom;
    cfgBits->direct3d_wireframe_triangles_mode  = d3dwftm;
    cfgBits->depth_test_function                = dtf;
    cfgBits->z_updates_enable                   = zue;
    cfgBits->early_z_enable                     = eze;
    cfgBits->early_z_updates_enable             = ezue;
    cfgBits->stencil_enable                     = se;
    cfgBits->blend_enable                       = be;
    cfgBits->direct3d_point_fill_mode           = d3dpfm;
    cfgBits->direct3d_provoking_vertex          = d3dpv;
}

//***************************************************************************

void PointSize(V3DContext* context, float size)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_POINT_SIZE,
                               v3d_point_size, pointSize);
    swap_float32_into(&pointSize->point_size, size);
}

//***************************************************************************

void LineWidth(V3DContext* context, float width)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_LINE_WIDTH,
                               v3d_line_width, lineWidth);
    swap_float32_into(&lineWidth->line_width, width);
}

//***************************************************************************

/* DepthOffset: the hardware polygon-offset packet (op 106), MESA's
 * v3dx_emit.c/v3dx_state.c encoding: depth_offset_factor is the app's real
 * factor unchanged; depth_offset_units is the app's real units, scaled by
 * 256.0 when the depth buffer is Z16 -- V3D 4.2 counts offset units against
 * a Z24 buffer, so one Z16 unit is 256 of them. The depth format is chosen
 * per context (zbuffer_bits; see TileRenderingModeCFGCommon's call site in
 * gl/src/context.c): D32F, which takes the units unscaled, unless the
 * context asked for a 16-bit Z buffer with mglChooseZBufferDepth(16).
 * `limit` (real GL_EXT_polygon_offset_clamp) has no caller-facing API in
 * this driver -- 0.0 disables the clamp, matching desktop GL's default
 * un-clamped behavior when the extension isn't used. */
void DepthOffset(V3DContext* context, float factor, float units)
{
    v3d_static_buffer* buffer = context->current_buf;
    /* x256 is exact (a power of two), so no FPCR rounding is involved. */
    float units_scaled = (context->zbuffer_bits == 16) ? units * 256.0f : units;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_DEPTH_OFFSET,
                               v3d_depth_offset, depthOffset);
    depthOffset->depth_offset_factor = LE16(v3d_float_to_f187(factor));
    depthOffset->depth_offset_units  = LE16(v3d_float_to_f187(units_scaled));
    swap_float32_into(&depthOffset->limit, 0.0f);
}

//***************************************************************************

void ClipperXYScaling(V3DContext* context, UWORD width, UWORD height)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_CLIPPER_XY_SCALING,
                               v3d_clipper_xy_scaling, clipperXYScaling);

    swap_float32_into(&clipperXYScaling->viewport_half_width_in_1_256th_of_pixel, (width / 2) * 256.f);
    swap_float32_into(&clipperXYScaling->viewport_half_height_in_1_256th_of_pixel, (height / 2) * -256.f);
}

//***************************************************************************

void ClipperZScaleAndOffset(V3DContext* context, float vpzs, float vpzo)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_CLIPPER_Z_SCALE_AND_OFFSET,
                               v3d_clipper_z_scale_and_offset, clipperZScaleOffset);

    swap_float32_into(&clipperZScaleOffset->viewport_z_scale_zc_to_zs, vpzs);
    swap_float32_into(&clipperZScaleOffset->viewport_z_offset_zc_to_zs, vpzo);
}

//***************************************************************************

void ClipperZMinMaxClippingPlanes(V3DContext* context, float minzw, float maxzw)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_CLIPPER_Z_MIN_MAX_CLIPPING_PLANES,
                               v3d_clipper_z_min_max_clipping_planes, clipperZminmaxclpplns);

    swap_float32_into(&clipperZminmaxclpplns->minimum_zw, minzw);
    swap_float32_into(&clipperZminmaxclpplns->maximum_zw, maxzw);
}

//***************************************************************************

void ViewportOffset(V3DContext* context, UWORD width, UWORD coarse_x, UWORD height, UWORD coarse_y)
{
    ULONG* swivel;

    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_VIEWPORT_OFFSET,
                               v3d_viewport_offset, viewportOffset);

    viewportOffset->fine_x   = V3D_FLOAT_TO_U14_8((float)width / 2.f);
    viewportOffset->coarse_x = ((UWORD)(coarse_x));
    viewportOffset->fine_y   = V3D_FLOAT_TO_U14_8((float)height / 2.f);
    viewportOffset->coarse_y = ((UWORD)(coarse_y));

    swivel       = (ULONG*)((UBYTE*)viewportOffset + 1);
    swivel[0]    = (ULONG)LE32(swivel[0]);                   //LE packing is hell
    swivel[1]    = (ULONG)LE32(swivel[1]);
}

//***************************************************************************

void ColorWriteMasks(V3DContext* context, ULONG mask)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_COLOR_WRITE_MASKS,
                               v3d_color_write_masks, colorwriteMasks);

    colorwriteMasks->mask = LE32(mask);
}

//***************************************************************************

void BlendConstantColor(V3DContext* context, UWORD alpha, UWORD blue, UWORD green, UWORD red)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_BLEND_CONSTANT_COLOR,
                               v3d_blend_constant_color, blendconstantColor);

    blendconstantColor->alpha_f16 = LE16(alpha);
    blendconstantColor->blue_f16  = LE16(blue);
    blendconstantColor->green_f16 = LE16(green);
    blendconstantColor->red_f16   = LE16(red);
}

//***************************************************************************

/*
 * v3d_blend_enables/v3d_blend_cfg (opcodes 83/84): no LE32/LE16 swap
 * needed here, same as CfgBits() above: every payload field is a
 * byte-sized-or-smaller bitfield, so the PACKED struct's natural
 * (big-endian) byte order already matches what the CLE expects, matching
 * v3d_cfg_bits's own established convention.
 */
void BlendEnables(V3DContext* context, UBYTE mask)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_BLEND_ENABLES,
                               v3d_blend_enables, blendEnables);

    if (blendEnables)
    {
        blendEnables->mask = mask;
    }
}

//***************************************************************************

void BlendCfg(V3DContext* context, UBYTE renderTargetMask, UBYTE colorDstFactor, UBYTE colorSrcFactor,
              UBYTE colorMode, UBYTE alphaDstFactor, UBYTE alphaSrcFactor, UBYTE alphaMode)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_BLEND_CFG,
                               v3d_blend_cfg, blendCfg);

    if (blendCfg)
    {
        blendCfg->_unused28              = 0;
        blendCfg->render_target_mask     = renderTargetMask;
        blendCfg->color_blend_dst_factor = colorDstFactor;
        blendCfg->color_blend_src_factor = colorSrcFactor;
        blendCfg->color_blend_mode       = colorMode;
        blendCfg->alpha_blend_dst_factor = alphaDstFactor;
        blendCfg->alpha_blend_src_factor = alphaSrcFactor;
        blendCfg->alpha_blend_mode       = alphaMode;

    }
    else
    {
        D(("BlendCfg: v3d_buffer_claim_memory returned NULL -- buffer full!\n"));
    }
}

//***************************************************************************

void TransformFeedbackSpecs(V3DContext* context, UBYTE enable, UBYTE no16b)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_TRANSFORM_FEEDBACK_SPECS,
                               v3d_transform_feedback_specs, transformfeedbackSpecs);

    transformfeedbackSpecs->enable = enable;
    transformfeedbackSpecs->number_of_16_bit_output_data_specs_following = no16b;
}

//***************************************************************************

void SampleState(V3DContext* context, UWORD coverage, UBYTE mask)
{
    ULONG* swivel;

    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_SAMPLE_STATE,
                               v3d_sample_state, sampleState);

    sampleState->coverage = (float_to_ulong(coverage)) >> 16;
    sampleState->mask     = mask;

    swivel    = (ULONG*)((UBYTE*)sampleState + 1);
    swivel[0] = (ULONG)LE32(swivel[0]);                            //LE packing is hell
}

//***************************************************************************

/* NUMBER_OF_LAYERS (opcode 119). MESA emits it in the binning preamble of
 * every job with num_layers > 0, before TILE_BINNING_MODE_CFG ("This must go before the binning
 * mode configuration. It is required for layered framebuffers to work",
 * v3dx_draw.c:58-65) with num_layers = 1 for an ordinary framebuffer. The
 * field is minus-one encoded (v3d_packet.xml:940). */
void NumberOfLayers(V3DContext* context, UBYTE layers)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_NUMBER_OF_LAYERS,
                               v3d_number_of_layers, numberofLayers);

    numberofLayers->number_of_layers_minus_one = (v3d_u8)(layers - 1);
}

//***************************************************************************

void VCMCacheSize(V3DContext* context, UBYTE cbinning, UBYTE crendering)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_VCM_CACHE_SIZE,
                               v3d_vcm_cache_size, vcmcacheSize);

    vcmcacheSize->number_of_16_vertex_batches_for_rendering = crendering;
    vcmcacheSize->number_of_16_vertex_batches_for_binning   = cbinning;
}

//***************************************************************************

void glShaderState(V3DContext* context, ULONG staterecordAddress, ULONG attr_count)
{
    ULONG* swivel;

    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_GL_SHADER_STATE,
                               v3d_gl_shader_state, glshaderState);

    glshaderState->address_rshift_5           = staterecordAddress >> 5;
    glshaderState->number_of_attribute_arrays = attr_count;

    swivel    = (ULONG*)((UBYTE*)glshaderState + 1);
    swivel[0] = (ULONG)LE32(swivel[0]);                                   //LE packing is hell
}

//***************************************************************************

void VertexArrayPrims(V3DContext* context, UBYTE mode, ULONG length, ULONG index)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_VERTEX_ARRAY_PRIMS,
                               v3d_vertex_array_prims, vertexarrayPrims);

    vertexarrayPrims->mode                  = mode;
    vertexarrayPrims->length                = LE32(length);
    vertexarrayPrims->index_of_first_vertex = LE32(index);
}

//***************************************************************************

/*
 * IndexedPrimList has no PoC precedent -- PoC never drew indexed
 * geometry. See v3d_hw.h's comment on the struct itself for the MESA
 * cross-check and why length+enable_primitive_restarts is a hand-packed
 * plain field rather than a compiler bitfield.
 */
void IndexedPrimList(V3DContext* context, UBYTE mode, UBYTE indexType, ULONG length, BOOL enablePrimitiveRestarts, ULONG indexOffset)
{
    v3d_static_buffer* buffer = context->current_buf;
    ULONG temp;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_INDEXED_PRIM_LIST,
                               v3d_indexed_prim_list, indexedPrimList);

    indexedPrimList->mode       = mode;
    indexedPrimList->index_type = indexType;

    temp = (length & 0x7FFFFFFF) | (enablePrimitiveRestarts ? 0x80000000 : 0);
    indexedPrimList->length_and_restart = LE32(temp);
    indexedPrimList->index_offset       = LE32(indexOffset);
}

//***************************************************************************

/*
 * No PoC precedent. Must be emitted before IndexedPrimList
 * -- MESA (v3dx_draw.c) always emits INDEX_BUFFER_SETUP first, and
 * IndexedPrimList's index_offset is a byte offset INTO this bound buffer,
 * not an absolute address.
 */
void IndexBufferSetup(V3DContext* context, ULONG address, ULONG size)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_INDEX_BUFFER_SETUP,
                               v3d_index_buffer_setup, indexBufferSetup);

    indexBufferSetup->address = LE32(address);
    indexBufferSetup->size    = LE32(size);
}

//***************************************************************************

void VertexArrayInstancedPrims(V3DContext* context, UBYTE mode, ULONG length,
                                 ULONG instances, ULONG index)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_VERTEX_ARRAY_INSTANCED_PRIMS,
                               v3d_vertex_array_instanced_prims, vertexarrayinstancedPrims);

    vertexarrayinstancedPrims->mode                  = mode;
    vertexarrayinstancedPrims->instance_length       = LE32(length);
    vertexarrayinstancedPrims->number_of_instances   = LE32(instances);
    vertexarrayinstancedPrims->index_of_first_vertex = LE32(index);
}

//***************************************************************************

void PrimListFormat(V3DContext* context, UBYTE type, UBYTE tsof)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_PRIM_LIST_FORMAT,
                               v3d_prim_list_format, primlistFormat);

    primlistFormat->primitive_type   = type;
    primlistFormat->tri_strip_or_fan = tsof;
}

//***************************************************************************

void BranchToImplicitTileList(V3DContext* context, UBYTE tlsn)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_BRANCH_TO_IMPLICIT_TILE_LIST,
                               v3d_branch_to_implicit_tile_list, branchtoimplicittileList);

    branchtoimplicittileList->tile_list_set_number = tlsn;
}

//***************************************************************************

void ClearTileBuffers(V3DContext* context, UBYTE clearzstencil, UBYTE clearallrender)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_CLEAR_TILE_BUFFERS,
                               v3d_clear_tile_buffers, cleartileBuffers);

    cleartileBuffers->clear_z_stencil_buffer   = clearzstencil;
    cleartileBuffers->clear_all_render_targets = clearallrender;
}

//***************************************************************************

void StoreTileBufferGeneral(V3DContext* context, UBYTE bts, UBYTE mf, UBYTE flipy, ULONG ditm,
                            ULONG decm, ULONG oif, ULONG cbbs, ULONG cr, ULONG rbswap, ULONG strideorub,
                            ULONG height, ULONG address)
{
    UWORD *swivel16;

    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_STORE_TILE_BUFFER_GENERAL,
                               v3d_store_tile_buffer_general, storetilebufferGeneral);

    storetilebufferGeneral->buffer_to_store           = bts;
    storetilebufferGeneral->memory_format             = mf;
    storetilebufferGeneral->flip_y                    = flipy;
    storetilebufferGeneral->dither_mode               = ditm;
    storetilebufferGeneral->decimate_mode             = decm;
    storetilebufferGeneral->output_image_format       = oif;
    storetilebufferGeneral->clear_buffer_being_stored = cbbs;
    storetilebufferGeneral->channel_reverse           = cr;
    storetilebufferGeneral->r_b_swap                  = rbswap;
    storetilebufferGeneral->height_in_ub_or_stride_lo = (strideorub << 4) & 0xF0;
    storetilebufferGeneral->height_in_ub_or_stride_hi = LE16((strideorub) >> 4);
    storetilebufferGeneral->height                    = LE16(height);
    storetilebufferGeneral->address                   = LE32((ULONG)address);

    swivel16    = (UWORD*)((UBYTE*)storetilebufferGeneral + 2);
    swivel16[0] = (UWORD)LE16(swivel16[0]);                   //LE packing is hell
}

//***************************************************************************

/* Loads a tile buffer from existing memory instead of clearing it. A buffer
 * the app did not glClear() must be loaded: a tile whose buffer is neither
 * cleared nor loaded starts with whatever the on-chip tile buffer held from
 * the tile rendered immediately before it, not this tile's real prior
 * framebuffer content. Mirrors StoreTileBufferGeneral's own
 * field-set + swivel16 pattern exactly, field-for-field against
 * v3d_load_tile_buffer_general's own layout (see that struct's comment). */
void LoadTileBufferGeneral(V3DContext* context, UBYTE btl, UBYTE mf, UBYTE flipy,
                            ULONG decm, ULONG iif, ULONG fa1, ULONG cr, ULONG rbswap,
                            ULONG strideorub, ULONG height, ULONG address)
{
    UWORD *swivel16;

    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_LOAD_TILE_BUFFER_GENERAL,
                               v3d_load_tile_buffer_general, loadtilebufferGeneral);

    loadtilebufferGeneral->buffer_to_load          = btl;
    loadtilebufferGeneral->memory_format           = mf;
    loadtilebufferGeneral->flip_y                  = flipy;
    loadtilebufferGeneral->decimate_mode           = decm;
    loadtilebufferGeneral->input_image_format      = iif;
    loadtilebufferGeneral->force_alpha_1           = fa1;
    loadtilebufferGeneral->channel_reverse         = cr;
    loadtilebufferGeneral->r_b_swap                = rbswap;
    loadtilebufferGeneral->_unused_no_dither       = 0;
    loadtilebufferGeneral->height_in_ub_or_stride_lo = (strideorub << 4) & 0xF0;
    loadtilebufferGeneral->height_in_ub_or_stride_hi = LE16((strideorub) >> 4);
    loadtilebufferGeneral->height                    = LE16(height);
    loadtilebufferGeneral->address                   = LE32((ULONG)address);

    swivel16    = (UWORD*)((UBYTE*)loadtilebufferGeneral + 2);
    swivel16[0] = (UWORD)LE16(swivel16[0]);                   //LE packing is hell
}

//***************************************************************************

void TileRenderingModeCFGCommon(V3DContext* context, UBYTE targets, UWORD width, UWORD height,
                                UWORD maxbpp, UWORD msm, UWORD dbinmsm, UWORD eztaud, UWORD ezd,
                                UWORD idt, UWORD edsc)
{
    UWORD *swivel16;

    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_TILE_RENDERING_MODE_CFG_COMMON,
                               v3d_tile_rendering_mode_cfg_common, tilerenderingmodecfgCommon);

    tilerenderingmodecfgCommon->sub_id                             = 0; //COMMON
    tilerenderingmodecfgCommon->number_of_render_targets_minus_one = targets - 1;
    tilerenderingmodecfgCommon->image_width_pixels                 = LE16(width);
    tilerenderingmodecfgCommon->image_height_pixels                = LE16(height);
    tilerenderingmodecfgCommon->maximum_bpp_of_all_render_targets  = maxbpp;
    tilerenderingmodecfgCommon->multisample_mode_4x                = msm;
    tilerenderingmodecfgCommon->double_buffer_in_non_ms_mode       = dbinmsm;
    tilerenderingmodecfgCommon->early_z_test_and_update_direction  = eztaud;
    tilerenderingmodecfgCommon->early_z_disable                    = ezd;
    tilerenderingmodecfgCommon->internal_depth_type                = idt;
    tilerenderingmodecfgCommon->early_depth_stencil_clear          = edsc;

    swivel16    = (UWORD*)((UBYTE*)tilerenderingmodecfgCommon+ 6);
    swivel16[0] = (UWORD)LE16(swivel16[0]);                         //LE packing is hell
}

//***************************************************************************

void TileCoordinates(V3DContext* context, ULONG column, ULONG row)
{
    UBYTE *swivel24;
    UBYTE temp;

    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_TILE_COORDINATES,
                               v3d_tile_coordinates, tileCoordinates);

    tileCoordinates->tile_column_number = column;
    tileCoordinates->tile_row_number    = row;

    swivel24 = (UBYTE*)tileCoordinates + 1;
    temp = swivel24[2];
    swivel24[2] = swivel24[0];
    swivel24[0] = temp;
}

//***************************************************************************

void StartAddressOfGenericTileList(V3DContext* context, ULONG start, ULONG end)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_START_ADDRESS_OF_GENERIC_TILE_LIST,
                               v3d_start_address_of_generic_tile_list, startaddressofgenerictileList);

    startaddressofgenerictileList->start = LE32(start);
    startaddressofgenerictileList->end   = LE32(end);
}

//***************************************************************************

void Supertile_Coordinates(V3DContext* context, ULONG colnum, ULONG rownum)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_SUPERTILE_COORDINATES,
                               v3d_supertile_coordinates, supertileCoordinates);

    supertileCoordinates->column_number_in_supertiles = colnum;
    supertileCoordinates->row_number_in_supertiles    = rownum;
}

//***************************************************************************

void Multicore_Rendering_Tile_List_Set_Base(V3DContext* context, ULONG address, ULONG tlsn)
{
    ULONG* swivel;

    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_MULTICORE_RENDERING_TILE_LIST_SET_BASE,
                               v3d_multicore_rendering_tile_list_set_base, multicorerenderingtilelistsetBase);

    multicorerenderingtilelistsetBase->address_rshift_6     = address >> 6;
    multicorerenderingtilelistsetBase->tile_list_set_number = tlsn;

    swivel    = (ULONG*)((UBYTE*)multicorerenderingtilelistsetBase + 1);
    swivel[0] = (ULONG)LE32(swivel[0]);
}                                               //LE packing is hell

//***************************************************************************

void TileListInitialBlockSize(V3DContext* context, UBYTE size, UBYTE useauto)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_TILE_LIST_INITIAL_BLOCK_SIZE,
                               v3d_tile_list_initial_block_size, tilelistinitialblockSize);

    tilelistinitialblockSize->size_of_first_block_in_chained_tile_lists = size;
    tilelistinitialblockSize->use_auto_chained_tile_lists               = useauto;
}

//***************************************************************************

void TileRenderingModeCFGZSClearValues(V3DContext* context, UBYTE scv, float zcv)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_TILE_RENDERING_MODE_CFG_ZS_CLEAR_VALUES,
                               v3d_tile_rendering_mode_cfg_zs_clear_values, tilerenderingmodecfgzsclearValues);

    tilerenderingmodecfgzsclearValues->sub_id              = 2; //CLEAR
    tilerenderingmodecfgzsclearValues->stencil_clear_value = scv;
    swap_float32_into(&tilerenderingmodecfgzsclearValues->z_clear_value, zcv);
}

//***************************************************************************

void MulticoreRenderingSupertileCFG(V3DContext* context, UBYTE supertile_w, UBYTE supertile_h,
                                    UBYTE frame_w_supertile, UBYTE frame_h_supertile, UBYTE nobtl,
                                    UBYTE sro, UBYTE me, ULONG tilesY, ULONG tilesX)
{
    UBYTE *swivel24;
    UBYTE temp;

    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_MULTICORE_RENDERING_SUPERTILE_CFG,
                               v3d_multicore_rendering_supertile_cfg, multicorerenderingsupertileCfg);

    multicorerenderingsupertileCfg->supertile_width_in_tiles_minus_one  = supertile_w - 1;
    multicorerenderingsupertileCfg->supertile_height_in_tiles_minus_one = supertile_h - 1;
    multicorerenderingsupertileCfg->total_frame_width_in_supertiles     = frame_w_supertile;
    multicorerenderingsupertileCfg->total_frame_height_in_supertiles    = frame_h_supertile;
    multicorerenderingsupertileCfg->number_of_bin_tile_lists_minus_one  = nobtl - 1;
    multicorerenderingsupertileCfg->supertile_raster_order              = sro;
    multicorerenderingsupertileCfg->multicore_enable                    = me;
    multicorerenderingsupertileCfg->total_frame_height_in_tiles         = tilesY;
    multicorerenderingsupertileCfg->total_frame_width_in_tiles          = tilesX;

    swivel24    = (UBYTE*)multicorerenderingsupertileCfg + 1;
    temp        = swivel24[6];
    swivel24[6] = swivel24[4];
    swivel24[4] = temp;
}

//***************************************************************************

void TileRenderingModeCFGClearColorsPart1(V3DContext* context, UBYTE target, ULONG color)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_TILE_RENDERING_MODE_CFG_CLEAR_COLORS_PART1,
                               v3d_tile_rendering_mode_cfg_clear_colors_part1, tilerenderingmodecfgclearcolorsPart1);

    tilerenderingmodecfgclearcolorsPart1->sub_id                   = 3; //Part1
    tilerenderingmodecfgclearcolorsPart1->render_target_number     = target;
    tilerenderingmodecfgclearcolorsPart1->clear_color_low_32_bits  = LE32(color); //abgr
    /*
     * clear_color_next_16_bits and clear_color_last_8_bits carry the part of
     * V3D's wider-than-32-bit internal clear color above the low 32 bits; the
     * struct has them as two separate PLAIN (non-bitfield) fields because one
     * combined 24-bit bitfield did not work. Both are zeroed explicitly (LE16
     * on the 16-bit field per this file's convention for multi-byte fields;
     * the 8-bit field needs no byte-order swap).
     */
    tilerenderingmodecfgclearcolorsPart1->clear_color_next_16_bits = LE16(0);
    tilerenderingmodecfgclearcolorsPart1->clear_color_last_8_bits  = 0;
}

//***************************************************************************

void TileRenderingModeCFGColor(V3DContext* context, UBYTE bpp, UBYTE type, UBYTE clamp) //fixed to target0 atm
{
    ULONG temp;

    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_TILE_RENDERING_MODE_CFG_COLOR,
                               v3d_tile_rendering_mode_cfg_color, tilerenderingmodecfgColor);

    temp = 1 | bpp << 4 | type << 6 | clamp << 10;

    tilerenderingmodecfgColor->fake1 = LE32(temp); //subid 1, bpp 0 (32), type 2 (8), clamp 0 (none) gives 0x81000000
    tilerenderingmodecfgColor->fake2 = 0; //target 1, 2 and 3 not used
}

//***************************************************************************

void glShaderStateAttributeRecord(V3DContext* context, APTR address, UBYTE raiu, UBYTE nit,
                                  UBYTE sit, UBYTE vsize, UBYTE vtype, UBYTE novrbvs, UBYTE novrbcs,
                                  UWORD divisor, ULONG stride, ULONG maxindex)
{
    v3d_static_buffer* buffer = context->current_buf;
    /* Claimed directly, with a NULL check, because V3D_BUFFER_ALLOC_STRUCTNAME doesn't NULL-check. */
    v3d_gl_shader_state_attribute_record* attr =
        (v3d_gl_shader_state_attribute_record*)v3d_buffer_claim_memory(buffer, sizeof(v3d_gl_shader_state_attribute_record));

    if (attr)
    {
        attr->address                                      = LE32((ULONG)address);
        attr->read_as_int_uint                             = raiu;
        attr->normalized_int_type                          = nit;
        attr->signed_int_type                              = sit;
        attr->vec_size                                     = vsize;
        attr->type                                         = vtype;
        attr->number_of_values_read_by_vertex_shader       = novrbvs;
        attr->number_of_values_read_by_coordinate_shader   = novrbcs;
        attr->instance_divisor                             = LE16(divisor);
        attr->stride                                       = LE32(stride);
        attr->maximum_index                                = LE32(maxindex);
    }
}

//***************************************************************************

void SetInstanceid(V3DContext* context, ULONG instanceid)
{
    v3d_static_buffer* buffer = context->current_buf;

    V3D_BUFFER_ALLOC_OPERATION(buffer, v3d_OP_SET_INSTANCEID, v3d_set_instanceid, setInstanceid);

    setInstanceid->instance_id = LE32(instanceid);
}

//***************************************************************************

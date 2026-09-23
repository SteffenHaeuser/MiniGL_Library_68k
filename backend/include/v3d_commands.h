/*
 * (C) 2025-2026 Dennis van der Boon
 */

#ifndef V3D_COMMANDS_H
#define V3D_COMMANDS_H

#include "v3d_context.h"

/*
 * Declarations for backend/hw/v3d_commands.c.
 */

float swap_float32(float val);			/* DEFECT: returns through the FPU, see v3d_commands.c -- use swap_float32_into */
void swap_float32_into(void *dst, float val);	/* val byte-swapped for the V3D, written to dst as an integer */
ULONG float_to_ulong(float f);
void AlignBuffer(V3DContext* context, ULONG alignment);
void SetBuffer(V3DContext* context, v3d_static_buffer* buffer);
ULONG CurrentBufferAddress(V3DContext* context);
void DoCommand(V3DContext* context, UBYTE command);

void TileBinningModeCfg(V3DContext* context, UBYTE taibz, UBYTE tabz, UBYTE targets, UBYTE bpp, BOOL msaa, BOOL db, UWORD width, UWORD height);
void OcclusionQueryCounter(V3DContext* context, ULONG address);
void ClipWindow(V3DContext* context, UWORD lpc, UWORD bpc, UWORD width, UWORD height);
void CfgBits(V3DContext* context, UBYTE effp, UBYTE erfp, UBYTE cp, UBYTE edo, UBYTE lr, UBYTE rom, UBYTE d3dwftm,
             UBYTE dtf, UBYTE zue, UBYTE eze, UBYTE ezue, UBYTE se, UBYTE be, UBYTE d3dpfm, UBYTE d3dpv);
void PointSize(V3DContext* context, float size);
void LineWidth(V3DContext* context, float width);
void DepthOffset(V3DContext* context, float factor, float units);
void ClipperXYScaling(V3DContext* context, UWORD width, UWORD height);
void ClipperZScaleAndOffset(V3DContext* context, float vpzs, float vpzo);
void ClipperZMinMaxClippingPlanes(V3DContext* context, float minzw, float maxzw);
void ViewportOffset(V3DContext* context, UWORD width, UWORD coarse_x, UWORD height, UWORD coarse_y);
void ColorWriteMasks(V3DContext* context, ULONG mask);
void BlendConstantColor(V3DContext* context, UWORD alpha, UWORD blue, UWORD green, UWORD red);
void BlendEnables(V3DContext* context, UBYTE mask);
void BlendCfg(V3DContext* context, UBYTE renderTargetMask, UBYTE colorDstFactor, UBYTE colorSrcFactor,
              UBYTE colorMode, UBYTE alphaDstFactor, UBYTE alphaSrcFactor, UBYTE alphaMode);
void TransformFeedbackSpecs(V3DContext* context, UBYTE enable, UBYTE no16b);
void SampleState(V3DContext* context, UWORD coverage, UBYTE mask);
void VCMCacheSize(V3DContext* context, UBYTE cbinning, UBYTE crendering);
void NumberOfLayers(V3DContext* context, UBYTE layers);
void glShaderState(V3DContext* context, ULONG staterecordAddress, ULONG attr_count);
void VertexArrayPrims(V3DContext* context, UBYTE mode, ULONG length, ULONG index);
void VertexArrayInstancedPrims(V3DContext* context, UBYTE mode, ULONG length, ULONG instances, ULONG index);
void IndexedPrimList(V3DContext* context, UBYTE mode, UBYTE indexType, ULONG length, BOOL enablePrimitiveRestarts, ULONG indexOffset);
void IndexBufferSetup(V3DContext* context, ULONG address, ULONG size);
void PrimListFormat(V3DContext* context, UBYTE type, UBYTE tsof);
void BranchToImplicitTileList(V3DContext* context, UBYTE tlsn);
void ClearTileBuffers(V3DContext* context, UBYTE clearzstencil, UBYTE clearallrender);
void StoreTileBufferGeneral(V3DContext* context, UBYTE bts, UBYTE mf, UBYTE flipy, ULONG ditm,
                            ULONG decm, ULONG oif, ULONG cbbs, ULONG cr, ULONG rbswap, ULONG strideorub,
                            ULONG height, ULONG address);
void LoadTileBufferGeneral(V3DContext* context, UBYTE btl, UBYTE mf, UBYTE flipy,
                            ULONG decm, ULONG iif, ULONG fa1, ULONG cr, ULONG rbswap,
                            ULONG strideorub, ULONG height, ULONG address);
void TileRenderingModeCFGCommon(V3DContext* context, UBYTE targets, UWORD width, UWORD height,
                                UWORD maxbpp, UWORD msm, UWORD dbinmsm, UWORD eztaud, UWORD ezd,
                                UWORD idt, UWORD edsc);
void TileCoordinates(V3DContext* context, ULONG column, ULONG row);
void StartAddressOfGenericTileList(V3DContext* context, ULONG start, ULONG end);
void Supertile_Coordinates(V3DContext* context, ULONG colnum, ULONG rownum);
void Multicore_Rendering_Tile_List_Set_Base(V3DContext* context, ULONG address, ULONG tlsn);
void TileListInitialBlockSize(V3DContext* context, UBYTE size, UBYTE useauto);
void TileRenderingModeCFGZSClearValues(V3DContext* context, UBYTE scv, float zcv);
void MulticoreRenderingSupertileCFG(V3DContext* context, UBYTE supertile_w, UBYTE supertile_h,
                                    UBYTE frame_w_supertile, UBYTE frame_h_supertile, UBYTE nobtl,
                                    UBYTE sro, UBYTE me, ULONG tilesY, ULONG tilesX);
void TileRenderingModeCFGClearColorsPart1(V3DContext* context, UBYTE target, ULONG color);
void TileRenderingModeCFGColor(V3DContext* context, UBYTE bpp, UBYTE type, UBYTE clamp);
void glShaderStateAttributeRecord(V3DContext* context, APTR address, UBYTE raiu, UBYTE nit,
                                  UBYTE sit, UBYTE vsize, UBYTE vtype, UBYTE novrbvs, UBYTE novrbcs,
                                  UWORD divisor, ULONG stride, ULONG maxindex);
void SetInstanceid(V3DContext* context, ULONG instanceid);

#endif /* V3D_COMMANDS_H */

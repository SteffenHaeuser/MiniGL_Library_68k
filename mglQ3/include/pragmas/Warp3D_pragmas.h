/**/
/***/
/** Warp3D fd file for library generation*/
/** $VER: Warp3D.fd 1.0 (27.07.98)*/
/***/
/***/
/** This file is part of the Warp3D Project.*/
/** Warp3D is copyrighted*/
/** (C) 1998 Sam Jordan, Hans-Jörg Frieden, Thomas Frieden*/
/** All rights reserved*/
/***/
/** See the documentation for conditions.*/
/***/
/**/
/**/
/*   Context functions (Context.c)*/
/**/
#pragma libcall Warp3DBase W3D_CreateContext 1e 9802
#pragma tagcall Warp3DBase W3D_CreateContextTags 1e 9802
#pragma libcall Warp3DBase W3D_DestroyContext 24 801
#pragma libcall Warp3DBase W3D_GetState 2a 1802
#pragma libcall Warp3DBase W3D_SetState 30 10803
/**/
/*   Driver functions (Driver.c)*/
/**/
#pragma libcall Warp3DBase W3D_CheckDriver 36 0
#pragma libcall Warp3DBase W3D_LockHardware 3c 801
#pragma libcall Warp3DBase W3D_UnLockHardware 42 801
#pragma libcall Warp3DBase W3D_WaitIdle 48 801
#pragma libcall Warp3DBase W3D_CheckIdle 4e 801
#pragma libcall Warp3DBase W3D_Query 54 10803
#pragma libcall Warp3DBase W3D_GetTexFmtInfo 5a 10803
/**/
/*   Texture functions (Texture.c)*/
/**/
#pragma libcall Warp3DBase W3D_AllocTexObj 60 A9803
#pragma libcall Warp3DBase W3D_FreeTexObj 66 9802
#pragma libcall Warp3DBase W3D_ReleaseTexture 6c 9802
#pragma libcall Warp3DBase W3D_FlushTextures 72 801
#pragma libcall Warp3DBase W3D_SetFilter 78 109804
#pragma libcall Warp3DBase W3D_SetTexEnv 7e A19804
#pragma libcall Warp3DBase W3D_SetWrapMode 84 A109805
#pragma libcall Warp3DBase W3D_UpdateTexImage 8a B1A9805
#pragma libcall Warp3DBase W3D_UploadTexture 90 9802
/**/
/*   Drawing functions*/
/**/
#pragma libcall Warp3DBase W3D_DrawLine 96 9802
#pragma libcall Warp3DBase W3D_DrawPoint 9c 9802
#pragma libcall Warp3DBase W3D_DrawTriangle a2 9802
#pragma libcall Warp3DBase W3D_DrawTriFan a8 9802
#pragma libcall Warp3DBase W3D_DrawTriStrip ae 9802
/**/
/*   Effect functions*/
/**/
#pragma libcall Warp3DBase W3D_SetAlphaMode b4 91803
#pragma libcall Warp3DBase W3D_SetBlendMode ba 10803
#pragma libcall Warp3DBase W3D_SetDrawRegion c0 A19804
#pragma libcall Warp3DBase W3D_SetFogParams c6 19803
#pragma libcall Warp3DBase W3D_SetColorMask cc 3210805
#pragma libcall Warp3DBase W3D_SetStencilFunc d2 210804
/**/
/*   ZBuffer functions*/
/**/
#pragma libcall Warp3DBase W3D_AllocZBuffer d8 801
#pragma libcall Warp3DBase W3D_FreeZBuffer de 801
#pragma libcall Warp3DBase W3D_ClearZBuffer e4 9802
#pragma libcall Warp3DBase W3D_ReadZPixel ea 910804
#pragma libcall Warp3DBase W3D_ReadZSpan f0 9210805
#pragma libcall Warp3DBase W3D_SetZCompareMode f6 1802
/**/
/*   Stencil buffer functions*/
/**/
#pragma libcall Warp3DBase W3D_AllocStencilBuffer fc 801
#pragma libcall Warp3DBase W3D_ClearStencilBuffer 102 9802
#pragma libcall Warp3DBase W3D_FillStencilBuffer 108 943210807
#pragma libcall Warp3DBase W3D_FreeStencilBuffer 10e 801
#pragma libcall Warp3DBase W3D_ReadStencilPixel 114 910804
#pragma libcall Warp3DBase W3D_ReadStencilSpan 11a 9210805
/**/
/*   New functions*/
/**/
#pragma libcall Warp3DBase W3D_SetLogicOp 120 1802
#pragma libcall Warp3DBase W3D_Hint 126 10803
#pragma libcall Warp3DBase W3D_SetDrawRegionWBM 12c A9803
#pragma libcall Warp3DBase W3D_GetDriverState 132 801
#pragma libcall Warp3DBase W3D_Flush 138 801
#pragma libcall Warp3DBase W3D_SetPenMask 13e 1802
#pragma libcall Warp3DBase W3D_SetStencilOp 144 210804
#pragma libcall Warp3DBase W3D_SetWriteMask 14a 1802
#pragma libcall Warp3DBase W3D_WriteStencilPixel 150 210804
#pragma libcall Warp3DBase W3D_WriteStencilSpan 156 A9210806
#pragma libcall Warp3DBase W3D_WriteZPixel 15c 910804
#pragma libcall Warp3DBase W3D_WriteZSpan 162 A9210806
#pragma libcall Warp3DBase W3D_SetCurrentColor 168 9802
#pragma libcall Warp3DBase W3D_SetCurrentPen 16e 1802
#pragma libcall Warp3DBase W3D_UpdateTexSubImage 174 0CB1A9807
#pragma libcall Warp3DBase W3D_FreeAllTexObj 17a 801
#pragma libcall Warp3DBase W3D_GetDestFmt 180 0
/**/
/* V2*/
/**/
#pragma libcall Warp3DBase W3D_DrawLineStrip 186 9802
#pragma libcall Warp3DBase W3D_DrawLineLoop 18c 9802
#pragma libcall Warp3DBase W3D_GetDrivers 192 0
#pragma libcall Warp3DBase W3D_QueryDriver 198 10803
#pragma libcall Warp3DBase W3D_GetDriverTexFmtInfo 19e 10803
#pragma libcall Warp3DBase W3D_RequestMode 1a4 801
#pragma libcall Warp3DBase W3D_SetScissor 1aa 9802
#pragma libcall Warp3DBase W3D_FlushFrame 1b0 801
#pragma libcall Warp3DBase W3D_TestMode 1b6 001
#pragma libcall Warp3DBase W3D_SetChromaTestBounds 1bc 2109805
#pragma libcall Warp3DBase W3D_ClearDrawRegion 1c2 0802
/**/
/* V3*/
/**/
#pragma libcall Warp3DBase W3D_DrawTriangleV 1c8 9802
#pragma libcall Warp3DBase W3D_DrawTriFanV 1ce 9802
#pragma libcall Warp3DBase W3D_DrawTriStripV 1d4 9802
#pragma libcall Warp3DBase W3D_GetScreenmodeList 1da 0
#pragma libcall Warp3DBase W3D_FreeScreenmodeList 1e0 801
#pragma libcall Warp3DBase W3D_BestModeID 1e6 801
/**/
/* V4*/
/**/
#pragma libcall Warp3DBase W3D_VertexPointer 1ec 2109805
#pragma libcall Warp3DBase W3D_TexCoordPointer 1f2 432109807
#pragma libcall Warp3DBase W3D_ColorPointer 1f8 32109806
#pragma libcall Warp3DBase W3D_BindTexture 1fe 90803
#pragma libcall Warp3DBase W3D_DrawArray 204 210804
#pragma libcall Warp3DBase W3D_DrawElements 20a 9210805
#pragma libcall Warp3DBase W3D_SetFrontFace 210 0802

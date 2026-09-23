/*
 * $Id: others.c,v 1.1.1.1 2000/04/07 19:44:51 hfrieden Exp $
 *
 * $Date: 2000/04/07 19:44:51 $
 * $Revision: 1.1.1.1 $
 *
 * (C) 1999 by Hyperion
 * All rights reserved
 *
 * This file is part of the MiniGL library project
 * See the file Licence.txt for more details
 *
 */

/*
 * Modified by Dennis van der Boon for use on the PiStorm with Pi4.
 * Copyright 2025-2026.
 */

/*
 * MiniGLV3D fork of MiniGL/src/others.c: alpha test, colour mask, draw and
 * read buffer, polygon and shade model, blend function and equation, hints,
 * the state queries (glGet*, glIsEnabled, glGetString, glGetError), polygon
 * offset, MGLWriteShotPPM and glReadPixels, and the MGLMainLoop input loop
 * with its handler setters.
 */

#include "sysinc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../backend/hw/v3d_hw.h"

static char rcsid[] = "$Id: others.c,v 1.1.1.1 2000/04/07 19:44:51 hfrieden Exp $";


extern struct IntuitionBase *IntuitionBase;
extern struct ExecBase *SysBase;

void GLAlphaFunc(GLcontext context, GLenum func, GLclampf ref)
{
	v3d_u8 v3dfunc;

	/* Returns explicitly: GLFlagError only records the error (gl.h), so
	 * without the return a glAlphaFunc between glBegin and glEnd would take
	 * effect. */
	if (context->CurrentPrimitive != GL_BASE) { GLFlagError(context, 1, GL_INVALID_OPERATION); return; }

	switch(func)
	{
		case GL_NEVER:
			v3dfunc = V3D_ALPHAFUNC_NEVER;
			break;
		case GL_LESS:
			v3dfunc = V3D_ALPHAFUNC_LESS;
			break;
		case GL_EQUAL:
			v3dfunc = V3D_ALPHAFUNC_EQUAL;
			break;
		case GL_LEQUAL:
			v3dfunc = V3D_ALPHAFUNC_LEQUAL;
			break;
		case GL_GREATER:
			v3dfunc = V3D_ALPHAFUNC_GREATER;
			break;
		case GL_NOTEQUAL:
			v3dfunc = V3D_ALPHAFUNC_NOTEQUAL;
			break;
		case GL_GEQUAL:
			v3dfunc = V3D_ALPHAFUNC_GEQUAL;
			break;
		case GL_ALWAYS:
			v3dfunc = V3D_ALPHAFUNC_ALWAYS;
			break;
		default:
			GLFlagError(context, 1, GL_INVALID_ENUM);
			return;
	}

	context->backend.alpha_func = v3dfunc;
	context->backend.alpha_ref = (float)ref;
}

/*
 * Per-channel color write mask -- pure state storage, same "store now, real
 * consumer reads it fresh every draw call" pattern as Blend_State/
 * CullFace_State. The actual v3d_OP_COLOR_WRITE_MASKS CL emission lives in
 * gl_EmitCullBlendState (draw.c), not here, so the mask can change between
 * draws within one frame; see that function for the hardware bit layout.
 */
void GLColorMask(GLcontext context, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
	context->ColorMaskR = red;
	context->ColorMaskG = green;
	context->ColorMaskB = blue;
	context->ColorMaskA = alpha;
}


void GLDrawBuffer(GLcontext context, GLenum mode)
{
}

/*
 * glReadBuffer. This context has exactly one colour buffer a program can
 * read, and it is the FRONT: single-buffered, so everything is drawn into
 * it, and GLReadPixels below returns everything drawn so far, the
 * unfinished frame included. GL_DOUBLEBUFFER answers FALSE. So the
 * accepted names are the three GL defines as front-left for reading:
 * GL_FRONT, GL_LEFT and GL_FRONT_LEFT.
 *
 * GL_BACK is REFUSED: a single-buffered context has no back buffer, and GL
 * makes naming a buffer the context does not have an error. GLReadPixels
 * reads the one buffer either way -- only GL_READ_BUFFER tells the two
 * apart, and it tells the truth.
 *
 * GL's two error classes: a buffer this context does not have (back, right)
 * is GL_INVALID_OPERATION; a value that names no buffer at all, GL_NONE and
 * GL_FRONT_AND_BACK included, is GL_INVALID_ENUM. Neither changes the state.
 */
void GLReadBuffer(GLcontext context, GLenum mode)
{
	if (context->CurrentPrimitive != GL_BASE)
	{
		GLFlagError(context, 1, GL_INVALID_OPERATION);
		return;
	}

	switch (mode)
	{
		case GL_FRONT:
		case GL_LEFT:
		case GL_FRONT_LEFT:
			context->ReadBufferMode = mode;
			return;

		case GL_BACK:
		case GL_BACK_LEFT:
		case GL_BACK_RIGHT:
		case GL_RIGHT:
		case GL_FRONT_RIGHT:
			GLFlagError(context, 1, GL_INVALID_OPERATION);
			return;

		default:
			GLFlagError(context, 1, GL_INVALID_ENUM);
			return;
	}
}

void GLPolygonMode(GLcontext context, GLenum face, GLenum mode)
{
   context->CurPolygonMode = mode ;
   return ;
}

void GLShadeModel(GLcontext context, GLenum mode)
{
      context->CurShadeModel = mode ;

//avoid excessive state changes:

   if(context->CurShadeModel != context->ShadeModel)
   {
	context->ShadeModel = mode;
   }
}



/*
 * Maps GL_* blend factor enums onto the v3d_blend_factor hardware
 * enum (backend/hw/v3d_hw.h) directly -- names don't token-paste cleanly
 * (e.g. GL_ONE_MINUS_DST_COLOR -> V3D_BLEND_FACTOR_INVDSTCOLOR, GL_DST_COLOR
 * -> V3D_BLEND_FACTOR_DSTCOLOR, no shared suffix), so each case names both.
 */
#define BLS(X, Y) case GL_##X: *out = V3D_BLEND_FACTOR_##Y; return GL_TRUE
#define BLD(X, Y) case GL_##X: *out = V3D_BLEND_FACTOR_##Y; return GL_TRUE

/* GLBlendFuncSeparate uses the same mapping for the colour and the alpha
 * channel. The two accept-lists are deliberately DIFFERENT and both are
 * exactly GL 1.1's legal sets: SRC_COLOR/ONE_MINUS_SRC_COLOR are not legal
 * SOURCE factors and DST_COLOR/ONE_MINUS_DST_COLOR are not legal DESTINATION
 * factors until GL 1.4, and SRC_ALPHA_SATURATE is source-only. Returning
 * GL_FALSE means "not a legal factor", which the caller turns into
 * GL_INVALID_ENUM. */
static GLboolean gl_MapBlendSrcFactor(GLenum f, v3d_u32 *out)
{
	switch(f)
	{
		BLS(ZERO, ZERO);
		BLS(ONE, ONE);
		BLS(DST_COLOR, DSTCOLOR);
		BLS(ONE_MINUS_DST_COLOR, INVDSTCOLOR);
		BLS(SRC_ALPHA, SRCALPHA);
		BLS(ONE_MINUS_SRC_ALPHA, INVSRCALPHA);
		BLS(DST_ALPHA, DSTALPHA);
		BLS(ONE_MINUS_DST_ALPHA, INVDSTALPHA);
		BLS(SRC_ALPHA_SATURATE, SRCALPHASATURATE);
	default:
		return GL_FALSE;
	}
}

static GLboolean gl_MapBlendDstFactor(GLenum f, v3d_u32 *out)
{
	switch(f)
	{
		BLD(ZERO, ZERO);
		BLD(ONE, ONE);
		BLD(SRC_COLOR, SRCCOLOR);
		BLD(ONE_MINUS_SRC_COLOR, INVSRCCOLOR);
		BLD(SRC_ALPHA, SRCALPHA);
		BLD(ONE_MINUS_SRC_ALPHA, INVSRCALPHA);
		BLD(DST_ALPHA, DSTALPHA);
		BLD(ONE_MINUS_DST_ALPHA, INVDSTALPHA);
	default:
		return GL_FALSE;
	}
}

/*
 * GL_EXT_blend_func_separate / GL 1.4's glBlendFuncSeparate. It needs NO
 * hardware path of its own: BlendCfg() (v3d_commands.h) takes independent
 * colour and alpha triples.
 *
 * Two deliberate rules:
 *
 *  - ALL FOUR factors are validated BEFORE any state is written. Assigning
 *    CurBlendSrc/CurBlendDst first would let a bad enum leave the redundancy
 *    cache claiming a state the backend had never been given; the next call
 *    with those same (valid) enums would then early-out and silently skip
 *    programming the hardware. GL also requires that a call raising
 *    GL_INVALID_ENUM change no state.
 *
 *  - The early-out compares the MAPPED hardware factors rather than the GL
 *    enums, so it stays correct across a glBlendFunc/glBlendFuncSeparate mix.
 *    Comparing only CurBlendSrc/CurBlendDst would let
 *    glBlendFuncSeparate(A,B,C,D) followed by glBlendFunc(A,B) early-out and
 *    strand the alpha channel at C/D instead of resetting it to A/B.
 */
void GLBlendFuncSeparate(GLcontext context, GLenum srcRGB, GLenum dstRGB,
                         GLenum srcAlpha, GLenum dstAlpha)
{
	v3d_u32 sc, dc, sa, da;

	if (!gl_MapBlendSrcFactor(srcRGB,   &sc) ||
	    !gl_MapBlendDstFactor(dstRGB,   &dc) ||
	    !gl_MapBlendSrcFactor(srcAlpha, &sa) ||
	    !gl_MapBlendDstFactor(dstAlpha, &da))
	{
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	if (context->backend.blend_srcmode       == sc &&
	    context->backend.blend_dstmode       == dc &&
	    context->backend.blend_alpha_srcmode == sa &&
	    context->backend.blend_alpha_dstmode == da)
		return;

	context->backend.blend_srcmode       = sc;
	context->backend.blend_dstmode       = dc;
	context->backend.blend_alpha_srcmode = sa;
	context->backend.blend_alpha_dstmode = da;

	context->CurBlendSrc = srcRGB;
	context->CurBlendDst = dstRGB;
	context->SrcAlpha    = srcRGB;
	context->DstAlpha    = dstRGB;

	/* No hardware call here can refuse a blend mode, so there is never a
	 * fallback to SRC_ALPHA/ONE_MINUS_SRC_ALPHA for AlphaFellBack to
	 * report. */
	context->AlphaFellBack = GL_FALSE;
}

void GLBlendFunc(GLcontext context, GLenum sfactor, GLenum dfactor)
{
	/* FAST PATH. An unchanged state is the overwhelmingly common case: games
	 * re-assert the same blend func constantly, and MGLDrawMultitexBuffer
	 * (texture.c) calls this every time it runs. Delegating straight to
	 * GLBlendFuncSeparate would put the early-out BEHIND four factor-mapping
	 * switches.
	 *
	 * So compare the GL enums directly, before any mapping. The extra two
	 * tests confirm the alpha channel still mirrors the colour channel: after
	 * a glBlendFuncSeparate they differ, and this call must then really reset
	 * them, so it has to fall through to the slow path. CurBlendSrc/CurBlendDst
	 * only ever hold already-validated enums, so a bad enum can never match
	 * here and skip its GL_INVALID_ENUM. */
	if (context->CurBlendSrc == sfactor && context->CurBlendDst == dfactor &&
	    context->backend.blend_alpha_srcmode == context->backend.blend_srcmode &&
	    context->backend.blend_alpha_dstmode == context->backend.blend_dstmode)
		return;

	GLBlendFuncSeparate(context, sfactor, dfactor, sfactor, dfactor);
}

/*
 * glBlendEquation: the equation for BlendCfg()'s colorMode/alphaMode
 * arguments. All five core GL equations map straight onto real hardware
 * modes. (The hardware additionally has MUL, SCREEN, DARKEN and LIGHTEN --
 * reachable only via the advanced-blend extensions, which this driver does
 * not expose.)
 */
void GLBlendEquation(GLcontext context, GLenum mode)
{
	v3d_u32 m;

	switch(mode)
	{
	case GL_FUNC_ADD:              m = V3D_BLEND_MODE_ADD;  break;
	case GL_FUNC_SUBTRACT:         m = V3D_BLEND_MODE_SUB;  break;
	case GL_FUNC_REVERSE_SUBTRACT: m = V3D_BLEND_MODE_RSUB; break;
	case GL_MIN:                   m = V3D_BLEND_MODE_MIN;  break;
	case GL_MAX:                   m = V3D_BLEND_MODE_MAX;  break;
	default:
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	if (context->backend.blend_color_equation == m &&
	    context->backend.blend_alpha_equation == m)
		return;

	context->backend.blend_color_equation = m;
	context->backend.blend_alpha_equation = m;
	context->backend.blend_nonadd_warned  = 0;
}


void GLHint(GLcontext context, GLenum target, GLenum mode)
{
	switch(target)
	{
		case GL_FOG_HINT:
		case GL_PERSPECTIVE_CORRECTION_HINT:
			/* No V3D-backend equivalent exposed -- purely advisory
			 * hardware quality hints, safe to accept and drop. */
			break;
		case MGL_W_ONE_HINT:
			if (mode == GL_FASTEST) context->WOne_Hint = GL_TRUE;
			else            context->WOne_Hint = GL_FALSE;
			break;
		case MGL_FIXPOINTTRANS_HINT:
			if (mode == GL_FASTEST) context->FixpointTrans_Hint = GL_TRUE;
			else            context->FixpointTrans_Hint = GL_FALSE;
			break;
		default:
			GLFlagError(context, 1, GL_INVALID_ENUM);
	}
}


/*
 * STATE QUERIES.
 *
 * glGetBooleanv, glGetIntegerv, glGetFloatv, glGetDoublev and glIsEnabled
 * share ONE table. mgl_QueryState answers in floats and returns how many it
 * wrote; each getter converts. Every name reads the same through all the
 * getters, and a multi-element name fills every element.
 *
 * Two deliberate deviations from GL:
 *  - glGetIntegerv of a colour rounds the [0,1] float instead of scaling it
 *    into the integer range GL defines; glGetFloatv returns the colour
 *    unrounded.
 *  - GL_POLYGON_MODE answers one value where GL defines two, front and back.
 *    This driver keeps one mode, and writing a second element into a
 *    caller's one-element buffer would be worse than the missing value.
 */

#define MGL_QUERY_MAX 16

static GLenum mgl_ZModeToGL(v3d_u32 zmode)
{
	switch (zmode)
	{
		case V3D_Z_NEVER:    return GL_NEVER;
		case V3D_Z_EQUAL:    return GL_EQUAL;
		case V3D_Z_LEQUAL:   return GL_LEQUAL;
		case V3D_Z_GREATER:  return GL_GREATER;
		case V3D_Z_NOTEQUAL: return GL_NOTEQUAL;
		case V3D_Z_GEQUAL:   return GL_GEQUAL;
		case V3D_Z_ALWAYS:   return GL_ALWAYS;
		default:             return GL_LESS;
	}
}

static GLenum mgl_AlphaFuncToGL(v3d_u8 func)
{
	switch (func)
	{
		case V3D_ALPHAFUNC_NEVER:    return GL_NEVER;
		case V3D_ALPHAFUNC_LESS:     return GL_LESS;
		case V3D_ALPHAFUNC_EQUAL:    return GL_EQUAL;
		case V3D_ALPHAFUNC_LEQUAL:   return GL_LEQUAL;
		case V3D_ALPHAFUNC_GREATER:  return GL_GREATER;
		case V3D_ALPHAFUNC_NOTEQUAL: return GL_NOTEQUAL;
		case V3D_ALPHAFUNC_GEQUAL:   return GL_GEQUAL;
		default:                     return GL_ALWAYS;
	}
}

/* Returns how many values were written into v, 0 if this driver cannot
 * answer the name. v must hold MGL_QUERY_MAX floats. */
static int mgl_QueryState(GLcontext context, GLenum pname, GLfloat *v)
{
	int i;

	switch (pname)
	{
		/* ---- capabilities, from the state MGLSetState really tracks ---- */
		case GL_BLEND:               v[0] = (GLfloat)context->Blend_State;              return 1;
		case GL_ALPHA_TEST:          v[0] = (GLfloat)context->AlphaTest_State;           return 1;
		case GL_DEPTH_TEST:          v[0] = (GLfloat)context->DepthTest_State;           return 1;
		case GL_CULL_FACE:           v[0] = (GLfloat)context->CullFace_State;            return 1;
		case GL_FOG:                 v[0] = (GLfloat)context->Fog_State;                 return 1;
		case GL_SCISSOR_TEST:        v[0] = (GLfloat)context->Scissor_State;             return 1;
		case GL_DITHER:              v[0] = (GLfloat)context->Dither_State;              return 1;
		case GL_POINT_SMOOTH:        v[0] = (GLfloat)context->PointSmooth_State;         return 1;
		case GL_POLYGON_OFFSET_FILL: v[0] = (GLfloat)context->PolygonOffsetFill_State;   return 1;
		/* The three state-only capabilities (see context.h). */
		case GL_POLYGON_OFFSET_LINE:  v[0] = (GLfloat)context->PolygonOffsetLine_State;  return 1;
		case GL_POLYGON_OFFSET_POINT: v[0] = (GLfloat)context->PolygonOffsetPoint_State; return 1;
		case GL_SHARED_TEXTURE_PALETTE_EXT:
			v[0] = (GLfloat)context->SharedTexturePalette_State;
			return 1;
		case GL_TEXTURE_GEN_S:       v[0] = (GLfloat)context->TextureGenS_State;         return 1;
		case GL_TEXTURE_GEN_T:       v[0] = (GLfloat)context->TextureGenT_State;         return 1;
		case GL_TEXTURE_2D:
			v[0] = (GLfloat)context->Texture2D_State[context->ActiveTexture];
			return 1;

		/* The REAL depth write mask. glDepthMask writes context->DepthMask,
		 * the field draw.c reads, while MGLSetState(GL_DEPTH_WRITEMASK)
		 * writes CurWriteMask. */
		case GL_DEPTH_WRITEMASK:     v[0] = (GLfloat)context->DepthMask;                 return 1;

		/* ---- rectangles and ranges: every element, not just the first ---- */
		case GL_VIEWPORT:
			for (i = 0; i < 4; i++) v[i] = (GLfloat)context->ViewportBox[i];
			return 4;
		case GL_SCISSOR_BOX:
			for (i = 0; i < 4; i++) v[i] = (GLfloat)context->ScissorBox[i];
			return 4;
		case GL_DEPTH_RANGE:
			v[0] = (GLfloat)context->near;
			v[1] = (GLfloat)context->far;
			return 2;
		case GL_MAX_VIEWPORT_DIMS:
			/* V3D 4.2's own dimension limit, the same 4096 the texture code
			 * documents (backend/include/v3d_texture.h). */
			v[0] = 4096.0f; v[1] = 4096.0f;
			return 2;

		/* ---- colours ---- */
		case GL_CURRENT_COLOR:
			v[0] = context->CurrentColor.r; v[1] = context->CurrentColor.g;
			v[2] = context->CurrentColor.b; v[3] = context->CurrentColor.a;
			return 4;
		case GL_COLOR_CLEAR_VALUE:
			/* ClearColor is packed 0xAARRGGBB (context.c). */
			v[0] = (GLfloat)((context->ClearColor >> 16) & 0xFF) / 255.0f;
			v[1] = (GLfloat)((context->ClearColor >>  8) & 0xFF) / 255.0f;
			v[2] = (GLfloat)( context->ClearColor        & 0xFF) / 255.0f;
			v[3] = (GLfloat)((context->ClearColor >> 24) & 0xFF) / 255.0f;
			return 4;
		case GL_COLOR_WRITEMASK:
			v[0] = (GLfloat)context->ColorMaskR; v[1] = (GLfloat)context->ColorMaskG;
			v[2] = (GLfloat)context->ColorMaskB; v[3] = (GLfloat)context->ColorMaskA;
			return 4;

		/* ---- matrices and the stacks ---- */
		case GL_MODELVIEW_MATRIX:
			for (i = 0; i < 16; i++) v[i] = context->ModelView[context->ModelViewNr].v[i];
			return 16;
		case GL_PROJECTION_MATRIX:
			for (i = 0; i < 16; i++) v[i] = context->Projection[context->ProjectionNr].v[i];
			return 16;
		/* GL_TEXTURE, the third matrix mode, has real storage (context.h),
		 * so it answers the same two queries as the other two. */
		case GL_TEXTURE_MATRIX:
			for (i = 0; i < 16; i++) v[i] = context->Texture[context->TextureNr].v[i];
			return 16;
		case GL_MATRIX_MODE:            v[0] = (GLfloat)context->CurrentMatrixMode;            return 1;
		case GL_MODELVIEW_STACK_DEPTH:  v[0] = (GLfloat)(context->ModelViewStackPointer + 1);  return 1;
		case GL_PROJECTION_STACK_DEPTH: v[0] = (GLfloat)(context->ProjectionStackPointer + 1); return 1;
		case GL_TEXTURE_STACK_DEPTH:    v[0] = (GLfloat)(context->TextureStackPointer + 1);    return 1;

		/* ---- comparison and face state, mapped back from the backend ---- */
		case GL_DEPTH_FUNC:      v[0] = (GLfloat)mgl_ZModeToGL(context->backend.zmode);            return 1;
		case GL_ALPHA_TEST_FUNC: v[0] = (GLfloat)mgl_AlphaFuncToGL(context->backend.alpha_func);   return 1;
		case GL_ALPHA_TEST_REF:  v[0] = context->backend.alpha_ref;                                return 1;
		case GL_CULL_FACE_MODE:  v[0] = (GLfloat)context->CurrentCullFace;                         return 1;
		case GL_FRONT_FACE:      v[0] = (GLfloat)context->CurrentFrontFace;                        return 1;
		case GL_SHADE_MODEL:     v[0] = (GLfloat)context->CurShadeModel;                           return 1;
		case GL_LINE_WIDTH:      v[0] = context->CurrentLineWidth;                                 return 1;
		case GL_POLYGON_MODE:    v[0] = (GLfloat)context->CurPolygonMode;                          return 1;
		case GL_BLEND_SRC:       v[0] = (GLfloat)context->CurBlendSrc;                             return 1;
		case GL_BLEND_DST:       v[0] = (GLfloat)context->CurBlendDst;                             return 1;

		/* ---- pixel store ---- */
		case GL_UNPACK_ROW_LENGTH:  v[0] = (GLfloat)context->CurUnpackRowLength;  return 1;
		case GL_UNPACK_SKIP_PIXELS: v[0] = (GLfloat)context->CurUnpackSkipPixels; return 1;
		case GL_UNPACK_SKIP_ROWS:   v[0] = (GLfloat)context->CurUnpackSkipRows;   return 1;
		case GL_UNPACK_ALIGNMENT:   v[0] = (GLfloat)context->UnpackAlign;         return 1;
		case GL_PACK_ALIGNMENT:     v[0] = (GLfloat)context->PackAlign;           return 1;
		case GL_PACK_ROW_LENGTH:    v[0] = (GLfloat)context->PackRowLength;       return 1;
		case GL_PACK_SKIP_PIXELS:   v[0] = (GLfloat)context->PackSkipPixels;      return 1;
		case GL_PACK_SKIP_ROWS:     v[0] = (GLfloat)context->PackSkipRows;        return 1;
		case GL_PACK_SWAP_BYTES:    v[0] = (GLfloat)context->PackSwapBytes;       return 1;
		case GL_PACK_LSB_FIRST:     v[0] = (GLfloat)context->PackLsbFirst;        return 1;

		/* ---- limits and how the framebuffer is actually built ---- */
		case GL_MAX_TEXTURE_SIZE:
			/* Base MiniGL answers 256, which is not a V3D 4.2 limit; an
			 * application that sizes its textures from this answer
			 * downscales anything larger to fit it. 1024 is deliberately
			 * below V3D 4.2's 4096 (v3d_texture.h): it is the highest value
			 * proven on this hardware, not a guess. */
			v[0] = 1024.0f;
			return 1;
		case GL_MAX_TEXTURE_UNITS_ARB: v[0] = (GLfloat)MAX_TEXUNIT; return 1;
		case GL_RED_BITS:
		case GL_GREEN_BITS:
		case GL_BLUE_BITS:
		case GL_ALPHA_BITS:  v[0] = 8.0f; return 1;   /* the framebuffer is RGBA8, always */
		case GL_DEPTH_BITS:  v[0] = (GLfloat)context->backend.zbuffer_bits; return 1;
		case GL_RGBA_MODE:   v[0] = 1.0f; return 1;
		case GL_INDEX_MODE:  v[0] = 0.0f; return 1;   /* and it never was */
		case GL_DOUBLEBUFFER: v[0] = 0.0f; return 1;  /* disabled project-wide */

		/* ---- the index, edge-flag and read-buffer state. None of it
		 * affects rendering -- context.h says why for each -- so answering
		 * it is the part an application can see. ---- */
		case GL_CURRENT_INDEX:          v[0] = context->CurrentIndex;                 return 1;
		case GL_EDGE_FLAG:              v[0] = (GLfloat)context->CurrentEdgeFlag;     return 1;
		case GL_READ_BUFFER:            v[0] = (GLfloat)context->ReadBufferMode;      return 1;
		case GL_INDEX_ARRAY_TYPE:       v[0] = (GLfloat)context->IndexArrayType;      return 1;
		case GL_INDEX_ARRAY_STRIDE:     v[0] = (GLfloat)context->IndexArrayStride;    return 1;
		case GL_EDGE_FLAG_ARRAY_STRIDE: v[0] = (GLfloat)context->EdgeFlagArrayStride; return 1;
	}

	return 0;
}

void  GLGetBooleanv(GLcontext context, GLenum pname, GLboolean *params)
{
	GLfloat v[MGL_QUERY_MAX];
	int n = mgl_QueryState(context, pname, v);
	int i;

	if (n == 0)
	{
		/* Deterministic rather than GL's "leave it untouched": with an
		 * untouched buffer, a caller that does not check glGetError reads
		 * its own stack. Same choice in every getter. */
		*params = GL_FALSE;
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	for (i = 0; i < n; i++)
		params[i] = (GLboolean)(v[i] != 0.0f);
}


/*
 * The float -> int conversion for glGetIntegerv, and NOT `(GLint)(f + 0.5f)`.
 *
 * Nearly every value the table above answers with is ALREADY an exact integer
 * (an enum, a count, a pixel rectangle), so adding 0.5 lands exactly on a tie
 * -- and this driver's CPU code runs under the CALLER's FPCR. A tie resolved
 * round-half-to-EVEN goes to the nearest even number, which returns every ODD
 * value one too high while leaving even ones correct.
 *
 * Truncating first and then adjusting by whatever fraction is left never
 * forms a tie, so an exact integer survives under ANY rounding mode and a
 * genuine fraction still rounds the way GL asks.
 */
static GLint mgl_FloatToInt(GLfloat f)
{
	GLint   i   = (GLint)f;
	GLfloat rem = f - (GLfloat)i;

	if (rem >= 0.5f)       i += 1;
	else if (rem <= -0.5f) i -= 1;

	return i;
}

void  GLGetIntegerv(GLcontext context, GLenum pname, GLint *params)
{
	GLfloat v[MGL_QUERY_MAX];
	int n = mgl_QueryState(context, pname, v);
	int i;

	if (n == 0)
	{
		*params = 0;
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	for (i = 0; i < n; i++)
		params[i] = mgl_FloatToInt(v[i]);
}


const GLubyte * GLGetString(GLcontext context, GLenum name)
{
	switch(name)
	{
		case GL_RENDERER:
			return "MiniGLV3D/Broadcom V3D 4.2";

		case GL_VENDOR:     return "Hyperion / MiniGLV3D port";
		case GL_VERSION:    return "1.1";
		case GL_EXTENSIONS:
			/* GL_EXT_paletted_texture + GL_EXT_shared_texture_palette
			 * advertise GL_COLOR_INDEX texture upload (TEX_SRCFMT_COLOR_INDEX8
			 * / tex_BuildPaletteLUT in texture.c).
			 *
			 * The shared-palette semantics are the honest ones here: the
			 * palette lives in the GL context (GLColorTable writes
			 * context->PaletteData), i.e. ONE palette shared by all textures.
			 * EXT_paletted_texture nominally implies PER-TEXTURE palettes,
			 * which this does not do -- it is listed because Quake2 requires
			 * both names before it will use the shared path, and the shared
			 * path is the only one it actually exercises. Anything that
			 * genuinely relies on per-texture palettes would be misled.
			 *
			 * NOTE the MGL_ prefixes on two entries are deliberate and must
			 * stay: "GL_MGL_ARB_multitexture" CONTAINS "GL_ARB_multitexture"
			 * as a substring, so strstr-based detection still finds it. */
			return "GL_MGL_ARB_multitexture GL_EXT_compiled_vertex_array GL_MGL_packed_pixels GL_EXT_color_table GL_EXT_paletted_texture GL_EXT_shared_texture_palette";

		default:            return "Huh?";
	}
}

void GLGetFloatv(GLcontext context, GLenum pname, GLfloat *params)
{
	GLfloat v[MGL_QUERY_MAX];
	int n = mgl_QueryState(context, pname, v);
	int i;

	if (n == 0)
	{
		/* Deterministic, as in GLGetBooleanv: an untouched buffer leaves a
		 * caller that does not check glGetError reading uninitialised
		 * memory. */
		*params = 0.0f;
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	for (i = 0; i < n; i++)
		params[i] = v[i];
}

/*
 * glGetDoublev: the fourth getter over the same table, converting the way
 * GLGetFloatv does. Widening the table's floats loses nothing, because the
 * state behind them -- the matrices, the colours -- is itself kept in single
 * precision.
 *
 * The one exception is the depth range, which glDepthRange stores as the
 * GLdouble it was given. Narrowing it through the float table and widening it
 * again would hand back 0.100000001490116 for 0.1, so it is answered from the
 * GLdouble fields directly.
 */
void GLGetDoublev(GLcontext context, GLenum pname, GLdouble *params)
{
	GLfloat v[MGL_QUERY_MAX];
	int n;
	int i;

	if (pname == GL_DEPTH_RANGE)
	{
		params[0] = context->near;
		params[1] = context->far;
		return;
	}

	n = mgl_QueryState(context, pname, v);
	if (n == 0)
	{
		*params = 0.0;   /* deterministic, as in the other three getters */
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	for (i = 0; i < n; i++)
		params[i] = (GLdouble)v[i];
}

/*
 * glGetPointerv. Answers the five client arrays gl.h can name. The texcoord
 * pointer is the CLIENT active unit's, as GL specifies -- the same routing
 * glTexCoordPointer uses to store it. Until an application sets them, the
 * vertex, colour and unit-0 texcoord arrays report the placeholder addresses
 * MGLInitContext gives them rather than GL's NULL; nothing is ever read
 * through those unless the array is also enabled. There is no feedback or
 * selection buffer to report.
 */
void GLGetPointerv(GLcontext context, GLenum pname, GLvoid **params)
{
	switch (pname)
	{
		case GL_VERTEX_ARRAY_POINTER:
			*params = (GLvoid *)context->ArrayPointer.verts;
			return;
		case GL_COLOR_ARRAY_POINTER:
			*params = (GLvoid *)context->ArrayPointer.colors;
			return;
		case GL_TEXTURE_COORD_ARRAY_POINTER:
			*params = (GLvoid *)((context->ClientActiveTexture == 1)
			                     ? context->ArrayPointer.texcoords1
			                     : context->ArrayPointer.texcoords);
			return;
		case GL_INDEX_ARRAY_POINTER:
			*params = (GLvoid *)context->IndexArrayPointer;
			return;
		case GL_EDGE_FLAG_ARRAY_POINTER:
			*params = (GLvoid *)context->EdgeFlagArrayPointer;
			return;
		default:
			*params = NULL;   /* deterministic, as in the getters above */
			GLFlagError(context, 1, GL_INVALID_ENUM);
			return;
	}
}

GLenum GLGetError(GLcontext context)
{
	GLenum ret = context->CurrentError;
	context->CurrentError = GL_NO_ERROR;
	return ret;
}

/*
 * Answers every capability MGLSetState actually tracks: the GL ones through
 * the same table as the getters, MiniGL's own two directly.
 */
GLboolean GLIsEnabled(GLcontext context, GLenum cap)
{
	GLfloat v[MGL_QUERY_MAX];

	switch (cap)
	{
		case GL_BLEND:
		case GL_ALPHA_TEST:
		case GL_DEPTH_TEST:
		case GL_CULL_FACE:
		case GL_FOG:
		case GL_SCISSOR_TEST:
		case GL_DITHER:
		case GL_POINT_SMOOTH:
		case GL_POLYGON_OFFSET_FILL:
		case GL_POLYGON_OFFSET_LINE:
		case GL_POLYGON_OFFSET_POINT:
		case GL_SHARED_TEXTURE_PALETTE_EXT:
		case GL_TEXTURE_GEN_S:
		case GL_TEXTURE_GEN_T:
		case GL_TEXTURE_2D:
		case GL_DEPTH_WRITEMASK:
			if (mgl_QueryState(context, cap, v) == 1)
				return (GLboolean)(v[0] != 0.0f);
			break;

		/* MiniGL's own capabilities, tracked by the same switch. */
		case MGL_Z_OFFSET:              return (GLboolean)context->ZOffset_State;
		case MGL_ARRAY_TRANSFORMATIONS: return (GLboolean)context->VertexArrayPipeline;

		default:
			break;
	}

	/* Anything else is a capability this driver does not have. Lighting,
	 * stencil and clip planes are not even enum values here, so they cannot
	 * be named, and false is the honest answer for whatever remains. */
	return GL_FALSE;
}


void MGLSetZOffset(GLcontext context, GLfloat offset)
{
	context->ZOffset = offset;
}

/* glPolygonOffset. It and MGLSetZOffset feed the same hardware mechanism
 * (CfgBits' enable_depth_offset + DepthOffset(), v3d_commands.c), but they do
 * NOT carry the same quantity, so they do not share a field: `units` here is
 * in multiples of the minimum resolvable depth difference, whereas
 * MGLSetZOffset's value is a direct [0,1] depth delta -- see mgl/context.h. */
void GLPolygonOffset(GLcontext context, GLfloat factor, GLfloat units)
{
	context->PolygonOffsetFactor = factor;
	context->PolygonOffsetUnits  = units;
}

/* The fullscreen RastPort to read the presented frame from. Single buffer:
 * the screen's own. Double buffering: a private RastPort on the buffer on
 * display (the one that isn't BufNr, the back buffer) -- this doesn't rely on
 * ChangeScreenBuffer retargeting the screen's RastPort bitmap, which is not
 * documented behaviour. Nothing
 * renders into either buffer after a swap: MGLSwitchDisplay waits for the
 * render in this mode. */
static struct RastPort *mgl_FrontRastPort(GLcontext context, struct RastPort *frontrp)
{
	if (context->NumBuffers < 2)
		return &context->v3dScreen->RastPort;

	InitRastPort(frontrp);
	frontrp->BitMap = context->Buffers[1 - context->BufNr]->sb_BitMap;
	return frontrp;
}

void MGLWriteShotPPM(GLcontext context, char *filename)
{
	GLubyte *pixelline;
	FILE *f;
	int i;
	size_t bytes;
	int width, height;
	struct RastPort *rport;
	struct RastPort frontrp;

	/* context->v3dWindow is always NULL on the fullscreen path
	 * (vid_OpenDisplay opens a Screen and never sets v3dWindow -- see
	 * viewport.c's header comment), so fall back to context->v3dScreen's
	 * Width/Height and its front RastPort. */
	if (context->v3dWindow)
	{
		width = context->v3dWindow->Width;
		height = context->v3dWindow->Height;
		rport = context->v3dWindow->RPort;
	}
	else if (context->v3dScreen)
	{
		width = context->v3dScreen->Width;
		height = context->v3dScreen->Height;
		rport = mgl_FrontRastPort(context, &frontrp);
	}
	else
	{
		/* A MGLCreateContextFromBitMap context: no window and no screen, so
		 * mgl_FrontRastPort cannot be used either -- it returns
		 * &v3dScreen->RastPort for NumBuffers < 2, which a bitmap context
		 * always is.
		 *
		 * Here the render target IS the thing worth capturing, and we already
		 * hold a rastport for it. */
		width = (int)context->backend.width;
		height = (int)context->backend.height;
		rport = context->v3dRastPort;
	}

	/* A screenshot of what is on screen: wait for a render still running
	 * (frame pipelining submits without waiting) and release the bitmap lock
	 * it holds -- ReadPixelArray against a locked bitmap is a library call
	 * the cybergraphics autodoc forbids, and reading during the render can
	 * catch half-stored tiles. Unlike glReadPixels this does not render the
	 * unfinished frame: it captures the screen, not the GL front buffer. */
	{
		extern void MGLFlushPendingRender(GLcontext context);
		MGLFlushPendingRender(context);
	}

	pixelline = (GLubyte *)malloc(width*3);
	if (!pixelline) return;

	f = fopen(filename, "wb");
	if (!f)
	{
		free(pixelline);
		return;
	}

	// Write PPM header
	fprintf(f, "P6\n%d %d\n255\n", width, height);

	for (i=0; i<height; i++)
	{
		(void)ReadPixelArray(pixelline, 0, 0, width, rport,
			0, (UWORD)i, width, 1, RECTFMT_RGB);
		bytes = fwrite(pixelline, width*3, 1, f);
	}

	fclose(f);
	free(pixelline);
}

/*
 * glReadPixels to the OpenGL 1.1 standard.
 *
 * WHICH PIXELS: every pixel drawn before the call, including the frame
 * still being built -- GL's commands take effect in the order issued, and
 * a single-buffered context draws straight into the front buffer it reads.
 * MGLReadbackBegin (context.c) renders a pass of its own into scratch memory
 * -- the unfinished frame, or with no frame in progress a copy of the render
 * target -- wrapped in CachePreDMA/CachePostDMA, and the read comes from
 * there (no flicker: the screen is not written). mglSwitchDisplay + glFinish
 * before a read is not needed, and harmless.
 *
 * WHERE: (x, y) is the lower-left corner in window coordinates; output row
 * 0 is the bottom row of the rectangle (window row y). Pixels outside the
 * window are clipped away and their destination bytes are left untouched --
 * GL leaves them undefined. The scissor does not apply.
 *
 * WHAT: formats GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA, GL_RGB, GL_RGBA,
 * GL_LUMINANCE (R+G+B, clamped) and GL_LUMINANCE_ALPHA; types
 * GL_UNSIGNED_BYTE, GL_BYTE, GL_UNSIGNED_SHORT, GL_SHORT, GL_UNSIGNED_INT,
 * GL_INT and GL_FLOAT, converted from the 8-bit framebuffer components with
 * GL 1.1's table (UNSIGNED: (2^n-1)c; signed: ((2^n-1)c-1)/2; FLOAT: c).
 * Alpha is the framebuffer's real destination alpha (GL_ALPHA_BITS is 8).
 * Pixel transfer (scale/bias/maps) is the identity: glPixelTransfer and
 * glPixelMap do not exist in this driver.
 *
 * HOW PACKED: GL_PACK_ALIGNMENT (default 4 -- rows are padded to it),
 * GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS and
 * GL_PACK_SWAP_BYTES, placed exactly as GL 1.1 section 3.6.4 places unpacked
 * data. Multi-byte values are written in the client's (big-endian) byte
 * order unless SWAP_BYTES is set.
 *
 * ERRORS (pixels untouched): GL_INVALID_OPERATION between glBegin and glEnd;
 * GL_INVALID_VALUE for a negative width or height; GL_INVALID_ENUM for an
 * unknown type or format; GL_INVALID_OPERATION for GL_COLOR_INDEX (this is
 * an RGBA context) and, for now, GL_DEPTH_COMPONENT (the depth buffer is
 * stored in the tile layout and its CPU decoding is not verified on this
 * hardware). GL_STENCIL_INDEX and GL_BITMAP are not declared in gl.h, so
 * they arrive as unknown values.
 *
 * The bytes the render store writes are B,G,R,A per pixel, rows top-down
 * (V3D_OUTPUT_IMAGE_FORMAT_RGBA8 with r_b_swap into the PIXFMT_BGRA32
 * screen, and the same layout into scratch_color_mem).
 */
extern GLboolean MGLReadbackBegin(GLcontext context, UBYTE **base, ULONG *stride, APTR *lock);
extern void MGLReadbackEnd(GLcontext context, APTR lock);

typedef struct
{
	GLint     align;
	GLint     rowlength;
	GLint     skippixels;
	GLint     skiprows;
	GLboolean swapbytes;
} mgl_PackState;

/* Components per pixel for a colour format this driver reads, or 0. */
static int mgl_ReadFormatComponents(GLenum format)
{
	switch (format)
	{
		case GL_RED:
		case GL_GREEN:
		case GL_BLUE:
		case GL_ALPHA:
		case GL_LUMINANCE:       return 1;
		case GL_LUMINANCE_ALPHA: return 2;
		case GL_RGB:             return 3;
		case GL_RGBA:            return 4;
	}
	return 0;
}

/* Bytes per component for a GL 1.1 non-bitmap type, or 0. */
static int mgl_ReadTypeSize(GLenum type)
{
	switch (type)
	{
		case GL_UNSIGNED_BYTE:
		case GL_BYTE:           return 1;
		case GL_UNSIGNED_SHORT:
		case GL_SHORT:          return 2;
		case GL_UNSIGNED_INT:
		case GL_INT:
		case GL_FLOAT:          return 4;
	}
	return 0;
}

/* One framebuffer component c = v/255 written as `type` at dst (which need
 * not be aligned), GL 1.1's conversion table. The signed forms use C's
 * truncation toward zero, so 0 stays 0 and 255 becomes the type's maximum. */
static void mgl_PutComponent(GLubyte *dst, ULONG v, GLenum type, GLboolean swapbytes)
{
	GLubyte tmp[4];
	int size = 1;

	switch (type)
	{
		case GL_UNSIGNED_BYTE:
			dst[0] = (GLubyte)v;
			return;
		case GL_BYTE:
			dst[0] = (GLubyte)(GLbyte)(((LONG)v - 1) / 2);
			return;
		case GL_UNSIGNED_SHORT:
		{
			GLushort s = (GLushort)(v * 257UL);
			memcpy(tmp, &s, 2);
			size = 2;
			break;
		}
		case GL_SHORT:
		{
			GLshort s = (GLshort)(((LONG)(v * 257UL) - 1) / 2);
			memcpy(tmp, &s, 2);
			size = 2;
			break;
		}
		case GL_UNSIGNED_INT:
		{
			GLuint u = (GLuint)(v * 0x01010101UL);
			memcpy(tmp, &u, 4);
			size = 4;
			break;
		}
		case GL_INT:
		{
			GLint i = v ? (GLint)((v * 0x01010101UL - 1UL) / 2UL) : 0;
			memcpy(tmp, &i, 4);
			size = 4;
			break;
		}
		case GL_FLOAT:
		{
			GLfloat f = (GLfloat)v / 255.0f;
			memcpy(tmp, &f, 4);
			size = 4;
			break;
		}
		default:
			return;
	}

	if (swapbytes)
	{
		int k;
		for (k = 0; k < size; k++)
			dst[k] = tmp[size - 1 - k];
	}
	else
		memcpy(dst, tmp, (size_t)size);
}

/* The validated read: clip, fetch the source, pack. n = components per
 * pixel, s = bytes per component. */
static void mgl_ReadColor(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height,
                          GLenum format, GLenum type, GLubyte *pixels,
                          const mgl_PackState *pack, int n, int s)
{
	GLint fbw = (GLint)context->backend.width;
	GLint fbh = (GLint)context->backend.height;
	GLint x0 = x < 0 ? 0 : x;
	GLint y0 = y < 0 ? 0 : y;
	GLint x1 = (x + width  > fbw) ? fbw : x + width;
	GLint y1 = (y + height > fbh) ? fbh : y + height;
	ULONG l, a, rowbytes;
	UBYTE *base;
	ULONG stride;
	APTR lock;
	GLint wx, wy;

	if (width == 0 || height == 0 || x0 >= x1 || y0 >= y1)
		return;

	/* GL 1.1 section 3.6.4: l groups per row (ROW_LENGTH, else width), n
	 * elements of s bytes each; a row takes n*l*s bytes when s >= alignment,
	 * else that rounded up to a multiple of the alignment. */
	l = (ULONG)(pack->rowlength > 0 ? pack->rowlength : width);
	a = (ULONG)pack->align;
	if ((ULONG)s >= a)
		rowbytes = (ULONG)(n * s) * l;
	else
		rowbytes = a * (((ULONG)(n * s) * l + a - 1) / a);

	if (!MGLReadbackBegin(context, &base, &stride, &lock))
	{
		MGLReadbackEnd(context, lock);   /* always paired: it may discard the readback's own pass */
		return;
	}

	for (wy = y0; wy < y1; wy++)
	{
		const UBYTE *srcrow = base + (ULONG)(fbh - 1 - wy) * stride;
		GLubyte *dstrow = pixels + (ULONG)(pack->skiprows + (wy - y)) * rowbytes;

		for (wx = x0; wx < x1; wx++)
		{
			const UBYTE *p = srcrow + (ULONG)wx * 4;
			GLubyte *dst = dstrow + (ULONG)(pack->skippixels + (wx - x)) * (ULONG)(n * s);
			ULONG b = p[0], g = p[1], r = p[2], al = p[3];
			ULONG c[4];
			int k;

			switch (format)
			{
				case GL_RED:   c[0] = r;  break;
				case GL_GREEN: c[0] = g;  break;
				case GL_BLUE:  c[0] = b;  break;
				case GL_ALPHA: c[0] = al; break;
				case GL_LUMINANCE:
					c[0] = r + g + b;
					if (c[0] > 255) c[0] = 255;
					break;
				case GL_LUMINANCE_ALPHA:
					c[0] = r + g + b;
					if (c[0] > 255) c[0] = 255;
					c[1] = al;
					break;
				case GL_RGB:
					c[0] = r; c[1] = g; c[2] = b;
					break;
				default: /* GL_RGBA */
					c[0] = r; c[1] = g; c[2] = b; c[3] = al;
					break;
			}

			for (k = 0; k < n; k++)
				mgl_PutComponent(dst + (ULONG)(k * s), c[k], type, pack->swapbytes);
		}
	}

	MGLReadbackEnd(context, lock);
}

void GLReadPixels(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
{
	mgl_PackState pack;
	int n, s;

	if (context->CurrentPrimitive != GL_BASE)
	{
		GLFlagError(context, 1, GL_INVALID_OPERATION);
		return;
	}
	if (width < 0 || height < 0)
	{
		GLFlagError(context, 1, GL_INVALID_VALUE);
		return;
	}
	s = mgl_ReadTypeSize(type);
	if (s == 0)
	{
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}
	if (format == GL_COLOR_INDEX || format == GL_DEPTH_COMPONENT)
	{
		GLFlagError(context, 1, GL_INVALID_OPERATION);
		return;
	}
	n = mgl_ReadFormatComponents(format);
	if (n == 0)
	{
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	pack.align      = context->PackAlign;
	pack.rowlength  = context->PackRowLength;
	pack.skippixels = context->PackSkipPixels;
	pack.skiprows   = context->PackSkipRows;
	pack.swapbytes  = context->PackSwapBytes;

	mgl_ReadColor(context, x, y, width, height, format, type, (GLubyte *)pixels, &pack, n, s);
}

/* For glCopyTex(Sub)Image2D (texture.c): the rectangle at window (x, y),
 * bottom-origin, as tightly packed GL_RGB bytes, bottom row first --
 * GLReadPixels' result with the caller's pack state ignored, since the copy
 * owns this buffer, and with no error of its own (the copy validated its
 * arguments). Pixels outside the window are left as they were. */
void mgl_ReadPixelsTightRGB(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height, GLubyte *pixels)
{
	mgl_PackState tight;

	if (width <= 0 || height <= 0)
		return;

	tight.align      = 1;
	tight.rowlength  = 0;
	tight.skippixels = 0;
	tight.skiprows   = 0;
	tight.swapbytes  = GL_FALSE;

	mgl_ReadColor(context, x, y, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels, &tight, 3, 1);
}

void MGLKeyFunc(GLcontext context, KeyHandlerFn k)
{
	context->KeyHandler = k;
}

void MGLSpecialFunc(GLcontext context, SpecialHandlerFn s)
{
	context->SpecialHandler = s;
}

void MGLMouseFunc(GLcontext context, MouseHandlerFn m)
{
	context->MouseHandler = m;
}

void MGLIdleFunc(GLcontext context, IdleFn i)
{
	context->Idle = i;
}

void MGLExit(GLcontext context)
{
	context->Running = GL_FALSE;
}

void MGLMainLoop(GLcontext context)
{
	struct IntuiMessage *imsg;
	struct Window *window;
	ULONG Class;
	UWORD Code;
	WORD MouseX, MouseY;
	GLbitfield buttons = 0;

	/* context->inputWindow, NOT v3dWindow -- v3dWindow is always NULL in
	 * fullscreen mode (that field means "true windowed rendering", not
	 * "has a window at all"). inputWindow is set in both vid_OpenWindow
	 * (same real window) and vid_OpenDisplay (a dedicated backdrop window
	 * opened purely for IDCMP input) -- see context.h's own field
	 * comment. */
	window = context->inputWindow;
	if (!window)
	{
		printf("MGLMainLoop: no input window available, cannot run\n");
		return;
	}
	ModifyIDCMP(window, IDCMP_VANILLAKEY|IDCMP_RAWKEY|IDCMP_MOUSEMOVE|IDCMP_MOUSEBUTTONS);

	context->Running = GL_TRUE;

	while (context->Running == GL_TRUE)
	{
		/* GetMsg() below is a real exec.library call, and frame
		 * pipelining defers the lock release (gl_FrameBegin/
		 * MGLFlushPendingRender, context.c), so the bitmap lock from
		 * whichever frame was last submitted can still be held at this
		 * point. Calling GetMsg while that lock is held violates
		 * cybergraphics.library's own documented rule against any
		 * library call while a bitmap is locked (see vid_OpenDisplay,
		 * context.c). Flushing it here, every iteration, guarantees no
		 * lock is ever outstanding across this call. */
		extern void MGLFlushPendingRender(GLcontext context);
		MGLFlushPendingRender(context);

		while (imsg = (struct IntuiMessage *)GetMsg(window->UserPort))
		{
			Class  = imsg->Class;
			Code   = imsg->Code;
			MouseX = imsg->MouseX;
			MouseY = imsg->MouseY;
			ReplyMsg((struct Message *)imsg);
			switch(Class)
			{
			case IDCMP_VANILLAKEY:
				if (context->KeyHandler)
				{
					context->KeyHandler((char)Code);
				}
				break;
			case IDCMP_MOUSEBUTTONS:
				switch(Code)
				{
					case SELECTDOWN: buttons |= MGL_BUTTON_LEFT;    break;
					case SELECTUP:   buttons &= ~MGL_BUTTON_LEFT;   break;
					case MENUDOWN:   buttons |= MGL_BUTTON_RIGHT;   break;
					case MENUUP:     buttons &= ~MGL_BUTTON_RIGHT;  break;
					case MIDDLEDOWN: buttons |= MGL_BUTTON_MID;     break;
					case MIDDLEUP:   buttons &= MGL_BUTTON_MID;     break;
				}
			// drop through
			case IDCMP_MOUSEMOVE:
				if (context->MouseHandler)
				{
					context->MouseHandler((GLint)MouseX, (GLint)MouseY, buttons);
				}
				break;
			} /* switch Class */
		} /* While imsg */

		if (context->Idle)
		{
			context->Idle();
		}
	} /* While running */
}

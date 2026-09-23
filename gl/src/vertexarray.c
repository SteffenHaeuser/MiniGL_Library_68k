/*
 * $Id: vertexarray.c,v 1.3 2001/02/05 16:56:03 tfrieden Exp $
 *
 * $Date: 2001/02/05 16:56:03 $
 * $Revision: 1.3 $
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
 * MiniGLV3D fork of MiniGL/src/vertexarray.c.
 *
 * BIGGEST ARCHITECTURAL DEPARTURE FROM THE ORIGINAL: the original's
 * pointer setters immediately pushed the app's pointer+stride+format
 * into Warp3D's OWN array-pointer interface (`Set_W3D_VertexPointer`/
 * `Set_W3D_ColorPointer`/`Set_W3D_TexCoordPointer`, the original's
 * `vertexarray.h`) -- Warp3D had a native "scan this array directly" fast
 * path, so the original's whole design revolves around feeding that
 * interface as directly as possible, including a `w_off`/`w_buffer` trick
 * to splice a synthetic W coordinate into Warp3D's expected vertex layout
 * via raw pointer arithmetic. V3D has no such interface in this port's
 * architecture: GLDrawArrays gathers app-array data into
 * context->VertexBuffer[] itself (the same staging array GLVertex4f
 * populates) and feeds draw.c's d_Draw* functions -- there is no "direct
 * hardware scan" path to feed at pointer-set time at all. These setters
 * are therefore simpler than the original: validate, then store
 * pointer/stride/size/format into context->ArrayPointer (MGLAPointer,
 * context.h -- see the comment there for the field-level redesign). No
 * Set_W3D_* calls, no w_off/w_buffer, no cur_vertexpointer_state/
 * cur_texcoordpointer_state redundant-write caching (that existed to avoid
 * repeat writes to Warp3D's array pointers -- meaningless with nothing to
 * write to).
 *
 * context->VertexArrayPipeline (defaults GL_TRUE, set by MGLSetState's
 * MGL_ARRAY_TRANSFORMATIONS, context.c) is stored but NOT consulted here --
 * the original used it to pick between the CPU-transform pipeline and a raw
 * hardware-array bypass; this port has no bypass to fall back to (see
 * above), so every array draw takes the gather path whatever its value.
 * GLLockArrays (vertexelements.c) does read it and refuses a lock while it
 * is GL_FALSE.
 *
 * GL_INT vertex arrays: accepted, flagged AP_FIXPOINT in ArrayPointer.state,
 * and converted by the gather step. The original's "fixed-point" path
 * (MiniGL/src/vertexarray.c's A_TransformArray, and vertexelements.c's
 * TransformIndex) read the app's ints UNSCALED -- plain integer coordinates
 * -- and multiplied by a 1.15 fixed copy of the combined matrix
 * (float2fix = 32768);
 * vertexelements.c rescaled the product back with fix2float, vertexarray.c
 * let the common factor cancel in the perspective divide. Either way the
 * value semantics are exactly (float)int, which is what
 * GatherVertexFromArray/GatherVertexFromIndex do here.
 *
 * MGL_UBYTE_BGRA (a MiniGL-specific glColorPointer type in the original):
 * size 3 -> BGR, size 4 -> BGRA, byte order per the original's
 * Convert_UB_BGR/BGRA (see MGLAColorMode, context.h). The enum is public in
 * gl.h, so an app may use it. MGL_UBYTE_ARGB is supported too -- see
 * GLColorPointer/GatherVertexFromArray below for the implementation, and
 * MGLAColorMode's own comment (context.h) for the byte order.
 */

#include "sysinc.h"
#include "../../backend/hw/v3d_debug.h"

static char rcsid[] = "$Id: vertexarray.c,v 1.1.1.1 2000/04/07 19:44:51 hfrieden Exp $";

extern void d_DrawTriangles(GLcontext context);
extern void d_DrawTriangleStrip(GLcontext context);
extern void d_DrawTriangleFan(GLcontext context);
extern void d_DrawPoints(GLcontext context);
extern void d_DrawLines(GLcontext context);
extern void d_DrawLineStrip(GLcontext context);
extern void d_DrawLineLoop(GLcontext context);

/* draw.c skips its per-vertex real-w scan while this is set. Every d_Draw*
 * call below draws only vertices GatherVertexFromArray or mglv3d_gather_fast
 * has just written, and both store w = q = 1.0f for a position of fewer than
 * 4 components. Set right before the call and cleared right after it, so no
 * other draw sees it (vertexelements.c keeps the same bracket). */
extern int g_mglv3d_vb_affine;
#define DRAW_GATHERED(call) \
	do { g_mglv3d_vb_affine = (context->ArrayPointer.vertexsize < 4); call; g_mglv3d_vb_affine = 0; } while (0)

extern unsigned long g_mglv3d_lock_epoch;   /* vertexelements.c */

void GLEnableClientState(GLcontext context, GLenum state)
{
	switch (state)
	{
		case GL_TEXTURE_COORD_ARRAY:
			/* Routed by the CLIENT active texture unit, same as
			 * GLTexCoordPointer. */
			if (context->ClientActiveTexture == 1)
				context->ClientState |= GLCS_TEXTURE1;
			else
				context->ClientState |= GLCS_TEXTURE;
			break;

		case GL_COLOR_ARRAY:
			context->ClientState |= GLCS_COLOR;
			break;

		case GL_VERTEX_ARRAY:
			context->ClientState |= GLCS_VERTEX;
			break;

		/* GL_INDEX_ARRAY and GL_EDGE_FLAG_ARRAY are legal GL 1.1 arrays
		 * (glIndexPointer, glEdgeFlagPointer). Tracked, never drawn from --
		 * see GLIndexPointer below. glInterleavedArrays assigns ClientState
		 * outright and so clears both, which is what GL says it must do. */
		case GL_INDEX_ARRAY:
			context->ClientState |= GLCS_INDEX;
			break;

		case GL_EDGE_FLAG_ARRAY:
			context->ClientState |= GLCS_EDGEFLAG;
			break;

		default:
			GLFlagError(context, 1, GL_INVALID_ENUM);
			break;
	}
}

void GLDisableClientState(GLcontext context, GLenum state)
{
	switch (state)
	{
		case GL_TEXTURE_COORD_ARRAY:
			/* Routed by the CLIENT active texture unit, same as
			 * GLEnableClientState above. */
			if (context->ClientActiveTexture == 1)
				context->ClientState &= ~GLCS_TEXTURE1;
			else
				context->ClientState &= ~GLCS_TEXTURE;
			break;

		case GL_COLOR_ARRAY:
			context->ClientState &= ~GLCS_COLOR;
			break;

		case GL_VERTEX_ARRAY:
			context->ClientState &= ~GLCS_VERTEX;
			break;

		case GL_INDEX_ARRAY:
			context->ClientState &= ~GLCS_INDEX;
			break;

		case GL_EDGE_FLAG_ARRAY:
			context->ClientState &= ~GLCS_EDGEFLAG;
			break;

		default:
			GLFlagError(context, 1, GL_INVALID_ENUM);
			break;
	}
}

/*
 * REJECTION POLICY for the three pointer setters.
 *
 * Returning WITHOUT storing when the type or the size is refused would leave
 * the PREVIOUS pointer bound. Applications rarely ask glGetError, so the
 * application does not learn the call was refused: it frees its old buffer
 * and the next draw renders stale geometry or dereferences freed memory.
 * Storing the refused pointer anyway is no safer -- the gather would read an
 * unsupported layout as though it were a supported one.
 *
 * A refused call therefore FAILS CLOSED: that array's pointer becomes NULL.
 * Nothing stale can be dereferenced; the gathers fall back to their
 * non-array sources (current colour, unit 0's current texcoord, zero for
 * texture unit 1); a draw whose POSITION array is NULL is dropped; and the next valid call restores
 * everything. The client-state enable bits are deliberately NOT touched --
 * clearing GL_VERTEX_ARRAY here would keep array drawing switched off even
 * after the application supplied a good pointer.
 */
void GLVertexPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	/* A new position array retires any positions packed for a lock
	 * (vertexelements.c, DrawLockedTriangles). */
	g_mglv3d_lock_epoch++;

	if (type != GL_FLOAT && type != GL_INT)
	{
		GLFlagError(context, 1, GL_INVALID_ENUM);
		context->ArrayPointer.verts = NULL;
		return;
	}

	if (size < 2 || size > 4)
	{
		GLFlagError(context, 1, GL_INVALID_VALUE);
		context->ArrayPointer.verts = NULL;
		return;
	}

	/* GL_INT: integer coordinates, converted with (float) at gather time
	 * (see this file's header comment for why that matches the original's
	 * AP_FIXPOINT path). GLint and GLfloat are both 4 bytes, so the default
	 * stride is the same for both types. */
	if (type == GL_INT)
		context->ArrayPointer.state |= AP_FIXPOINT;
	else
		context->ArrayPointer.state &= ~(AP_FIXPOINT);

	context->ArrayPointer.verts = (UBYTE*)pointer;
	context->ArrayPointer.vertexstride = (stride != 0) ? stride
	                                   : size * ((type == GL_INT) ? sizeof(GLint) : sizeof(GLfloat));
	context->ArrayPointer.vertexsize = size;
}

void GLColorPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	ULONG mode;

	/* MGL_UBYTE_ARGB is a real, distinct type (not GL_UNSIGNED_BYTE with a
	 * different size) -- always exactly 4 bytes/vertex, same as
	 * MGLA_COLOR_UBYTE_RGBA, just a different byte order (see
	 * MGLAColorMode's own comment, context.h). */
	if (type != GL_UNSIGNED_BYTE && type != GL_FLOAT && type != MGL_UBYTE_ARGB && type != MGL_UBYTE_BGRA)
	{
		GLFlagError(context, 1, GL_INVALID_ENUM);
		context->ArrayPointer.colors = NULL;   /* fail closed, see GLVertexPointer */
		return;
	}

	if (type == MGL_UBYTE_ARGB)
	{
		if (size != 4)
		{
			GLFlagError(context, 1, GL_INVALID_VALUE);
			context->ArrayPointer.colors = NULL;
			return;
		}

		context->ArrayPointer.colors = (UBYTE*)pointer;
		context->ArrayPointer.colorstride = (stride != 0) ? stride : 4 * sizeof(GLubyte);
		context->ArrayPointer.colormode = MGLA_COLOR_UBYTE_ARGB;
		return;
	}

	if (size != 3 && size != 4)
	{
		GLFlagError(context, 1, GL_INVALID_VALUE);
		context->ArrayPointer.colors = NULL;
		return;
	}

	if (type == GL_FLOAT)
		mode = (size == 3) ? MGLA_COLOR_FLOAT_RGB : MGLA_COLOR_FLOAT_RGBA;
	else if (type == MGL_UBYTE_BGRA)
		mode = (size == 3) ? MGLA_COLOR_UBYTE_BGR : MGLA_COLOR_UBYTE_BGRA;	/* the original: size 3 = BGR, size 4 = BGRA */
	else
		mode = (size == 3) ? MGLA_COLOR_UBYTE_RGB : MGLA_COLOR_UBYTE_RGBA;

	context->ArrayPointer.colors = (UBYTE*)pointer;
	context->ArrayPointer.colorstride = (stride != 0) ? stride
	                                   : size * ((type == GL_FLOAT) ? sizeof(GLfloat) : sizeof(GLubyte));
	context->ArrayPointer.colormode = mode;
}

void GLTexCoordPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	/* Fail closed on the CLIENT active unit's own pointer, see
	 * GLVertexPointer's policy comment. */
	if (type != GL_FLOAT || size < 1 || size > 4)
	{
		GLFlagError(context, 1, (type != GL_FLOAT) ? GL_INVALID_ENUM : GL_INVALID_VALUE);

		if (context->ClientActiveTexture == 1)
			context->ArrayPointer.texcoords1 = NULL;
		else
			context->ArrayPointer.texcoords = NULL;

		return;
	}

	/* Routed by the CLIENT active texture unit (glClientActiveTextureARB),
	 * independent of ArrayPointer's original single-unit fields -- see
	 * context.h's own comment on ClientActiveTexture/texcoords1 for why
	 * these are kept separate rather than reusing ActiveTexture. */
	if (context->ClientActiveTexture == 1)
	{
		context->ArrayPointer.texcoords1 = (UBYTE*)pointer;
		context->ArrayPointer.texcoordstride1 = (stride != 0) ? stride : size * sizeof(GLfloat);
		context->ArrayPointer.texcoordsize1 = size;
		return;
	}

	context->ArrayPointer.texcoords = (UBYTE*)pointer;
	context->ArrayPointer.texcoordstride = (stride != 0) ? stride : size * sizeof(GLfloat);
	context->ArrayPointer.texcoordsize = size;
}

/*
 * glIndexPointer / glEdgeFlagPointer.
 *
 * Both arrays are STORED and never GATHERED, and that is the right amount of
 * work rather than a shortcut: GL ignores the index array in RGBA mode, the
 * only mode this context has, and the edge-flag array matters only to the
 * GL_LINE/GL_POINT polygon modes, which this driver does not implement (see
 * GLEdgeFlag, vertexbuffer_min.c). What an application can observe is the
 * state itself -- glGetPointerv and the type/stride queries -- so that is
 * kept exactly, the stride as given rather than resolved.
 *
 * A refused call leaves the previous array in place, as GL specifies. That is
 * deliberately NOT the fail-closed policy of the three setters above: those
 * NULL their pointer because the draw path dereferences it, and nothing
 * dereferences these two.
 *
 * GL 1.1's index types are GL_UNSIGNED_BYTE, GL_SHORT, GL_INT, GL_FLOAT and
 * GL_DOUBLE. An edge-flag array has no type; its elements are booleans.
 */
void GLIndexPointer(GLcontext context, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	if (type != GL_UNSIGNED_BYTE && type != GL_SHORT && type != GL_INT &&
	    type != GL_FLOAT && type != GL_DOUBLE)
	{
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	if (stride < 0)
	{
		GLFlagError(context, 1, GL_INVALID_VALUE);
		return;
	}

	context->IndexArrayPointer = pointer;
	context->IndexArrayType    = type;
	context->IndexArrayStride  = stride;
}

void GLEdgeFlagPointer(GLcontext context, GLsizei stride, const GLvoid *pointer)
{
	if (stride < 0)
	{
		GLFlagError(context, 1, GL_INVALID_VALUE);
		return;
	}

	context->EdgeFlagArrayPointer = pointer;
	context->EdgeFlagArrayStride  = stride;
}

/*
 * The CLIENT-side analog of GLActiveTextureARB (texture.c), selecting which
 * array glTexCoordPointer/glEnableClientState(GL_TEXTURE_COORD_ARRAY)
 * target. Kept as separate state (context->ClientActiveTexture, not
 * ActiveTexture) matching real GL client/server-state independence, same
 * validation shape as GLActiveTextureARB.
 */
void GLClientActiveTextureARB(GLcontext context, GLenum unit)
{
	/* `>=`, not `>`: GL_TEXTURE0_ARB + MAX_TEXUNIT is already out of range.
	 * GLFlagError does not return, so the explicit return is what refuses
	 * the call. */
	if (unit < GL_TEXTURE0_ARB || unit >= GL_TEXTURE0_ARB + MAX_TEXUNIT)
	{
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	context->ClientActiveTexture = unit - GL_TEXTURE0_ARB;
}

/*
 * GLInterleavedArrays: decodes the 6 GL_*_V3F format enums into
 * ArrayPointer settings directly (matching the original's own per-format
 * offset math), setting ClientState to exactly the bits each format
 * implies. No Set_W3D_* calls, no Convert function-pointer selection
 * (the original picked a Convfn here for its own later use during
 * transform -- the gathers branch on colormode/vertexsize/texcoordsize
 * directly instead, no separate conversion-function abstraction needed).
 */
void GLInterleavedArrays(GLcontext context, GLenum format, GLsizei stride, const GLvoid *pointer)
{
	#define PTR context->ArrayPointer

	/* Same as GLVertexPointer: the position array changes. */
	g_mglv3d_lock_epoch++;

	/* Every interleaved format carries GLfloat vertices; a GL_INT
	 * glVertexPointer made earlier must not leak its flag into them. */
	PTR.state &= ~(AP_FIXPOINT);

	switch ((int)format)
	{
		case GL_V3F:
			context->ClientState = GLCS_VERTEX;
			PTR.verts = (UBYTE*)pointer;
			PTR.vertexsize = 3;
			PTR.vertexstride = (stride != 0) ? stride : 3 * sizeof(GLfloat);
			break;

		case GL_C4UB_V3F:
			context->ClientState = GLCS_VERTEX | GLCS_COLOR;
			PTR.colors = (UBYTE*)pointer;
			PTR.verts = (UBYTE*)pointer + 4;
			PTR.colormode = MGLA_COLOR_UBYTE_RGBA;
			PTR.vertexsize = 3;
			PTR.vertexstride = PTR.colorstride = (stride != 0) ? stride : (3 * sizeof(GLfloat) + 4 * sizeof(GLubyte));
			break;

		case GL_C3F_V3F:
			context->ClientState = GLCS_VERTEX | GLCS_COLOR;
			PTR.colors = (UBYTE*)pointer;
			PTR.verts = (UBYTE*)pointer + 12;
			PTR.colormode = MGLA_COLOR_FLOAT_RGB;
			PTR.vertexsize = 3;
			PTR.vertexstride = PTR.colorstride = (stride != 0) ? stride : (6 * sizeof(GLfloat));
			break;

		case GL_T2F_V3F:
			context->ClientState = GLCS_VERTEX | GLCS_TEXTURE;
			PTR.texcoords = (UBYTE*)pointer;
			PTR.texcoordsize = 2;
			PTR.verts = (UBYTE*)pointer + 8;
			PTR.vertexsize = 3;
			PTR.vertexstride = PTR.texcoordstride = (stride != 0) ? stride : (5 * sizeof(GLfloat));
			break;

		case GL_T2F_C4UB_V3F:
			context->ClientState = GLCS_VERTEX | GLCS_TEXTURE | GLCS_COLOR;
			PTR.texcoords = (UBYTE*)pointer;
			PTR.texcoordsize = 2;
			PTR.colors = (UBYTE*)pointer + 8;
			PTR.colormode = MGLA_COLOR_UBYTE_RGBA;
			PTR.verts = (UBYTE*)pointer + 12;
			PTR.vertexsize = 3;
			PTR.vertexstride = PTR.colorstride = PTR.texcoordstride =
				(stride != 0) ? stride : (5 * sizeof(GLfloat) + 4 * sizeof(GLubyte));
			break;

		case GL_T2F_C3F_V3F:
			context->ClientState = GLCS_VERTEX | GLCS_TEXTURE | GLCS_COLOR;
			PTR.texcoords = (UBYTE*)pointer;
			PTR.texcoordsize = 2;
			PTR.colors = (UBYTE*)pointer + 8;
			PTR.colormode = MGLA_COLOR_FLOAT_RGB;
			PTR.verts = (UBYTE*)pointer + 20;
			PTR.vertexsize = 3;
			PTR.vertexstride = PTR.colorstride = PTR.texcoordstride = (stride != 0) ? stride : (8 * sizeof(GLfloat));
			break;

		default:
			/* Fails closed like the pointer setters: NULL the three arrays
			 * this call would have described. ClientState is left alone:
			 * zeroing it would switch array drawing off until the
			 * application re-enabled it by hand, and a rejected call is not
			 * a reason to forget that arrays were enabled. GL_V2F and
			 * GL_C4UB_V2F reach here: gl.h declares them, so they compile,
			 * and this switch does not implement them. */
			GLFlagError(context, 1, GL_INVALID_ENUM);
			PTR.verts = NULL;
			PTR.colors = NULL;
			PTR.texcoords = NULL;
			break;
	}

	#undef PTR
}


/*
 * Byte-to-float table for the UBYTE colour arrays, shared with
 * vertexelements.c. g_ub2f[c] holds (float)c / 255.0f, built by that same
 * expression in this file. The division rounds under the caller's FPCR, so
 * the table is keyed on the FPCR rounding precision and mode bits (0xF0) and
 * rebuilt whenever they differ: a lookup then stores exactly the bits the
 * division would store.
 */
float g_ub2f[256] = { 0.0f };
static ULONG s_ub2f_key = 0xFFFFFFFFUL;	/* matches no FPCR value: the first use builds */


static __attribute__((noinline)) void ub2f_build(ULONG key)
{
	int c;

	for (c = 0; c < 256; c++)
	{
		GLubyte b = (GLubyte)c;

		/* Hides b's value from the optimiser, so the division runs here under
		 * this FPCR and is not folded at compile time. */
		__asm__ ("" : "+d"(b));
		g_ub2f[c] = (float)b / 255.0f;
	}
	s_ub2f_key = key;
}

/* Brings g_ub2f in line with the current FPCR. GLDrawArrays and
 * GLDrawElements call it once per draw, before they gather; only a byte
 * colour array reads the table, so any other draw returns at once. */
void mglv3d_ub2f_sync(GLcontext context)
{
	ULONG mode = context->ArrayPointer.colormode;
	ULONG fpcr;

	if (!((context->ClientState & GLCS_COLOR) && context->ArrayPointer.colors != NULL) ||
	    mode == MGLA_COLOR_FLOAT_RGB || mode == MGLA_COLOR_FLOAT_RGBA)
		return;

	__asm__ volatile ("fmove.l %%fpcr,%0" : "=d"(fpcr));
	fpcr &= 0xF0;	/* rounding precision and rounding mode */
	if (fpcr != s_ub2f_key)
		ub2f_build(fpcr);
}

/*
 * GLDrawArrays (below). ARCHITECTURE (see this file's own header comment
 * for the full rationale): rather than reimplementing the original's whole
 * A_TransformArray/A_DrawTriangles batching-and-clip machinery against a
 * Warp3D-shaped terminal call, this GATHERS the app's array data (via
 * context->ArrayPointer) into context->VertexBuffer[] -- the EXACT same
 * per-context staging array GLVertex4f populates one call at a time for
 * immediate-mode drawing -- then calls d_DrawTriangles (draw.c), or the
 * d_Draw* function of the other modes, directly. d_DrawTriangles needs no
 * changes to serve array-sourced geometry; from its point of view, this
 * looks like a very large glBegin(GL_TRIANGLES) block. Chunked into batches
 * no larger than context->VertexBufferSize (the allocated capacity of
 * VertexBuffer -- 256 by default, see context.c's own newVertexBufferSize)
 * AND no larger than MGL_MAXVERTS (1024), each GL_TRIANGLES chunk rounded
 * down to a whole number of triangles so no triangle is ever split across
 * chunk boundaries.
 *
 * Colors are gathered per context->ArrayPointer.colormode (MGLAColorMode,
 * not a Warp3D bitfield) when GL_COLOR_ARRAY is enabled; otherwise every
 * gathered vertex gets the current color (context->CurrentColor), matching
 * GLVertex4f's own "stamp .color for every vertex regardless of shading
 * mode" convention (see vertexbuffer_min.c). Texcoords are gathered when
 * GL_TEXTURE_COORD_ARRAY is enabled: s and t only, with q stored as 1.0f,
 * so a 4-component array's q has no effect (glTexCoord4f's q, by contrast,
 * reaches draw.c through .q).
 */
/*
 * Shared gather step -- one app-array vertex (srcIndex, per
 * context->ArrayPointer) copied into context->VertexBuffer[dstIndex]. Used
 * by every mode GLDrawArrays supports (GL_TRIANGLES when
 * mglv3d_gather_fast has no specialised loop for the layout).
 *
 * noinline: inlined into one of GLDrawArrays' loops, it would make every
 * GLDrawArrays call save and restore the FPU registers it uses.
 */
static __attribute__((noinline)) void GatherVertexFromArray(GLcontext context, int dstIndex, int srcIndex)
{
	MGLVertex *dst = &context->VertexBuffer[dstIndex];
	float x, y, z, w;
	GLubyte *vbytes = context->ArrayPointer.verts + srcIndex * context->ArrayPointer.vertexstride;

	if (context->ArrayPointer.state & AP_FIXPOINT)
	{
		/* GL_INT array: integer coordinates, (float) conversion -- see
		 * this file's header comment for the original's semantics. */
		const GLint *vsrc = (const GLint*)vbytes;

		x = (float)vsrc[0];
		y = (float)vsrc[1];
		z = (context->ArrayPointer.vertexsize >= 3) ? (float)vsrc[2] : 0.0f;
		w = (context->ArrayPointer.vertexsize >= 4) ? (float)vsrc[3] : 1.0f;
	}
	else
	{
		const GLfloat *vsrc = (const GLfloat*)vbytes;

		x = vsrc[0];
		y = vsrc[1];
		z = (context->ArrayPointer.vertexsize >= 3) ? vsrc[2] : 0.0f;
		w = (context->ArrayPointer.vertexsize >= 4) ? vsrc[3] : 1.0f;
	}

	dst->bx = x; dst->by = y; dst->bz = z; dst->bw = w;
	dst->v.x = x; dst->v.y = y; dst->v.z = z; dst->v.w = w;
	dst->normal = 0;
	/* Real per-pixel Q perspective correction: array-mode vertices never go
	 * through GLTexCoord4f, so `.q` here would otherwise be whatever stale
	 * value was left in this VertexBuffer slot by an unrelated earlier draw
	 * (VertexBuffer is a fixed, reused array, never zeroed) -- draw.c's
	 * draw_has_real_w pre-scan checks `.q != 1.0f`, so stale garbage here
	 * would route a 4-component position array's draw through the real-w
	 * shader pair. For fewer components DRAW_GATHERED skips that scan, which
	 * is only equivalent because this store makes q 1.0f. */
	dst->q = 1.0f;

	/* The NULL test is the other half of the setters' fail-closed policy (see
	 * GLVertexPointer): a refused glColorPointer leaves no pointer here, and
	 * the current colour is the right fallback rather than a stale array. */
	if ((context->ClientState & GLCS_COLOR) && context->ArrayPointer.colors != NULL)
	{
		GLubyte *csrc = context->ArrayPointer.colors + srcIndex * context->ArrayPointer.colorstride;

		/* Byte colours read g_ub2f, which holds (float)c / 255.0f under this
		 * FPCR: GLDrawArrays syncs it before gathering. */
		switch (context->ArrayPointer.colormode)
		{
			case MGLA_COLOR_FLOAT_RGB:
			{
				GLfloat *c = (GLfloat*)csrc;
				dst->color.r = c[0]; dst->color.g = c[1]; dst->color.b = c[2]; dst->color.a = 1.0f;
				break;
			}
			case MGLA_COLOR_FLOAT_RGBA:
			{
				GLfloat *c = (GLfloat*)csrc;
				dst->color.r = c[0]; dst->color.g = c[1]; dst->color.b = c[2]; dst->color.a = c[3];
				break;
			}
			case MGLA_COLOR_UBYTE_RGB:
				dst->color.r = g_ub2f[csrc[0]];
				dst->color.g = g_ub2f[csrc[1]];
				dst->color.b = g_ub2f[csrc[2]];
				dst->color.a = 1.0f;
				break;
			case MGLA_COLOR_UBYTE_RGBA:
			default:
				dst->color.r = g_ub2f[csrc[0]];
				dst->color.g = g_ub2f[csrc[1]];
				dst->color.b = g_ub2f[csrc[2]];
				dst->color.a = g_ub2f[csrc[3]];
				break;
			/* MGL_UBYTE_ARGB: byte order per the original's Convert_UB_ARGB --
			 * byte0=A, byte1=R, byte2=G, byte3=B, a genuine 4-way
			 * permutation of RGBA, not a 2-byte swap. */
			case MGLA_COLOR_UBYTE_ARGB:
				dst->color.a = g_ub2f[csrc[0]];
				dst->color.r = g_ub2f[csrc[1]];
				dst->color.g = g_ub2f[csrc[2]];
				dst->color.b = g_ub2f[csrc[3]];
				break;
			/* MGL_UBYTE_BGRA: byte order per the original's
			 * Convert_UB_BGR/BGRA -- byte0=B, byte1=G, byte2=R, byte3=A. */
			case MGLA_COLOR_UBYTE_BGR:
				dst->color.b = g_ub2f[csrc[0]];
				dst->color.g = g_ub2f[csrc[1]];
				dst->color.r = g_ub2f[csrc[2]];
				dst->color.a = 1.0f;
				break;
			case MGLA_COLOR_UBYTE_BGRA:
				dst->color.b = g_ub2f[csrc[0]];
				dst->color.g = g_ub2f[csrc[1]];
				dst->color.r = g_ub2f[csrc[2]];
				dst->color.a = g_ub2f[csrc[3]];
				break;
		}
	}
	else
	{
		dst->color.r = context->CurrentColor.r;
		dst->color.g = context->CurrentColor.g;
		dst->color.b = context->CurrentColor.b;
		dst->color.a = context->CurrentColor.a;
	}

	if ((context->ClientState & GLCS_TEXTURE) && context->ArrayPointer.texcoords != NULL)
	{
		GLfloat *tsrc = (GLfloat*)(context->ArrayPointer.texcoords + srcIndex * context->ArrayPointer.texcoordstride);
		dst->v.u0 = tsrc[0];
		dst->v.v0 = (context->ArrayPointer.texcoordsize >= 2) ? tsrc[1] : 0.0f;
	}
	else
	{
		/* No texcoord array: whatever the caller's last glTexCoord2f()
		 * set, applied uniformly, as the colour falls back to
		 * CurrentColor -- see GLTexCoord2f's own comment
		 * (vertexbuffer_min.c). Kept duplicated from GatherVertexFromIndex
		 * (vertexelements.c) on purpose. */
		dst->v.u0 = context->CurrentTexS;
		dst->v.v0 = context->CurrentTexT;
	}

	/* Texture unit 1, mirroring the GLCS_TEXTURE block above. When the
	 * array isn't enabled, explicitly zero rather than leave stale
	 * VertexBuffer[] contents from a previous draw -- this only matters
	 * once draw.c's `multitextured` (driven by texture bind/enable state,
	 * not this ClientState bit) is true. */
	if ((context->ClientState & GLCS_TEXTURE1) && context->ArrayPointer.texcoords1 != NULL)
	{
		GLfloat *tsrc1 = (GLfloat*)(context->ArrayPointer.texcoords1 + srcIndex * context->ArrayPointer.texcoordstride1);
		dst->v.u1 = tsrc1[0];
		dst->v.v1 = (context->ArrayPointer.texcoordsize1 >= 2) ? tsrc1[1] : 0.0f;
	}
	else
	{
		dst->v.u1 = 0.0f;
		dst->v.v1 = 0.0f;
	}
}

/*
 * Specialised GL_TRIANGLES gathers for the array layouts the host
 * applications draw with: a 3-component position (float, or GL_INT through
 * the same (float) conversion), colour as float RGBA, UBYTE RGBA or UBYTE
 * ARGB, or off, and unit 0 / unit 1 texcoords of 2 or more components, or
 * off. gf_decode makes the generic gathers' ClientState, NULL, size and
 * colour-mode tests once per chunk; any other layout returns 0 and the
 * caller runs the generic gather.
 * An array that is off becomes a stride-0 source holding what the generic
 * gather stores for it, so the loops test nothing per vertex.
 *
 * Every field something reads gets the generic gather's bits: floats are
 * copied as integers, w and q are 1.0f (0x3F800000), normal is 0, and byte
 * colours come from g_ub2f. The generic gather moves a float position through
 * the FPU, which differs from the raw copy only for a signalling NaN (quieted)
 * or, on an FPU that flushes to zero, a denormal. .bx/.by/.bz/.bw are not
 * written: their readers are the CPU clipper (hclip.c/aclip.c, v_Transform,
 * d_ConservativeDecideFrontface), which has no callers; reviving it needs
 * them back. Used here by GLDrawArrays and, from vertexelements.c, by
 * DrawLockedTriangles and the GLDrawElements GL_TRIANGLES loop.
 */
enum { GF_COL_F4, GF_COL_RGBA, GF_COL_ARGB };

typedef struct
{
	const GLubyte *pos, *col, *tex0, *tex1;
	GLint pos_stride, col_stride, tex0_stride, tex1_stride;
	int pos_int, col_kind;
	ULONG col_const[4], tex0_const[2];	/* the stride-0 sources */
} gf_layout;

static const ULONG s_gf_zero2[2] = { 0, 0 };	/* unit 1 off: u1 = v1 = 0.0f */

/* A float copied through its bit pattern, never through an FPU register. */
#define GF_COPY(dstfield, srcaddr) (*(ULONG *)&(dstfield) = *(const ULONG *)(srcaddr))
/* The same for the g_ub2f entry of a colour byte. */
#define GF_UB2F(dstfield, byte) (*(ULONG *)&(dstfield) = ((const ULONG *)g_ub2f)[(byte)])

/* elem_snapshots: the caller would pass the generic gather an element
 * position (GLArrayElement's per-slot texcoords); a draw that would read them
 * is left to the generic gather. */
static int gf_decode(GLcontext context, gf_layout *L, int elem_snapshots)
{
	const MGLAPointer *ap = &context->ArrayPointer;

	if (ap->vertexsize != 3)
		return 0;
	L->pos = ap->verts;
	L->pos_stride = ap->vertexstride;
	L->pos_int = (ap->state & AP_FIXPOINT) != 0;

	if ((context->ClientState & GLCS_COLOR) && ap->colors != NULL)
	{
		switch (ap->colormode)
		{
			case MGLA_COLOR_FLOAT_RGBA:
				L->col_kind = GF_COL_F4;
				break;
			case MGLA_COLOR_UBYTE_ARGB:
				L->col_kind = GF_COL_ARGB;
				break;
			case MGLA_COLOR_UBYTE_RGB:
			case MGLA_COLOR_FLOAT_RGB:
			case MGLA_COLOR_UBYTE_BGR:
			case MGLA_COLOR_UBYTE_BGRA:
				return 0;
			case MGLA_COLOR_UBYTE_RGBA:
			default:	/* the generic switch's default arm is RGBA too */
				L->col_kind = GF_COL_RGBA;
				break;
		}
		L->col = ap->colors;
		L->col_stride = ap->colorstride;
	}
	else
	{
		GF_COPY(L->col_const[0], &context->CurrentColor.r);
		GF_COPY(L->col_const[1], &context->CurrentColor.g);
		GF_COPY(L->col_const[2], &context->CurrentColor.b);
		GF_COPY(L->col_const[3], &context->CurrentColor.a);
		L->col = (const GLubyte *)L->col_const;
		L->col_stride = 0;
		L->col_kind = GF_COL_F4;
	}

	if ((context->ClientState & GLCS_TEXTURE) && ap->texcoords != NULL)
	{
		if (ap->texcoordsize < 2)
			return 0;
		L->tex0 = ap->texcoords;
		L->tex0_stride = ap->texcoordstride;
	}
	else
	{
		if (elem_snapshots)
			return 0;
		GF_COPY(L->tex0_const[0], &context->CurrentTexS);
		GF_COPY(L->tex0_const[1], &context->CurrentTexT);
		L->tex0 = (const GLubyte *)L->tex0_const;
		L->tex0_stride = 0;
	}

	if ((context->ClientState & GLCS_TEXTURE1) && ap->texcoords1 != NULL)
	{
		if (ap->texcoordsize1 < 2)
			return 0;
		L->tex1 = ap->texcoords1;
		L->tex1_stride = ap->texcoordstride1;
	}
	else
	{
		L->tex1 = (const GLubyte *)s_gf_zero2;
		L->tex1_stride = 0;
	}
	return 1;
}

/* One vertex. pos_int and col_kind are constants at every call, so each
 * instance of the loops below is straight-line code. */
static __inline__ __attribute__((always_inline)) void gf_vertex(MGLVertex *d, const GLubyte *p, const GLubyte *c,
                                                                const GLubyte *t0, const GLubyte *t1,
                                                                const int pos_int, const int col_kind)
{
	if (pos_int)
	{
		d->v.x = (float)((const GLint *)p)[0];
		d->v.y = (float)((const GLint *)p)[1];
		d->v.z = (float)((const GLint *)p)[2];
	}
	else
	{
		GF_COPY(d->v.x, p);
		GF_COPY(d->v.y, p + 4);
		GF_COPY(d->v.z, p + 8);
	}
	*(ULONG *)&d->v.w = 0x3F800000UL;	/* 1.0f */
	*(ULONG *)&d->q = 0x3F800000UL;
	d->normal = 0;

	if (col_kind == GF_COL_F4)
	{
		GF_COPY(d->color.r, c);
		GF_COPY(d->color.g, c + 4);
		GF_COPY(d->color.b, c + 8);
		GF_COPY(d->color.a, c + 12);
	}
	else if (col_kind == GF_COL_RGBA)
	{
		GF_UB2F(d->color.r, c[0]);
		GF_UB2F(d->color.g, c[1]);
		GF_UB2F(d->color.b, c[2]);
		GF_UB2F(d->color.a, c[3]);
	}
	else	/* GF_COL_ARGB: byte 0 is alpha */
	{
		GF_UB2F(d->color.a, c[0]);
		GF_UB2F(d->color.r, c[1]);
		GF_UB2F(d->color.g, c[2]);
		GF_UB2F(d->color.b, c[3]);
	}

	GF_COPY(d->v.u0, t0);
	GF_COPY(d->v.v0, t0 + 4);
	GF_COPY(d->v.u1, t1);
	GF_COPY(d->v.v1, t1 + 4);
}

/* Elements src .. src+n-1 into d[0 .. n-1]. The sources walk by their strides,
 * which lands on the addresses the generic gather computes as
 * base + index * stride. */
static __inline__ __attribute__((always_inline)) void gf_run(const gf_layout *L, MGLVertex *d, int n, int src,
                                                             const int pos_int, const int col_kind)
{
	const GLint ps = L->pos_stride, cs = L->col_stride, t0s = L->tex0_stride, t1s = L->tex1_stride;
	const GLubyte *p  = L->pos  + src * ps;
	const GLubyte *c  = L->col  + src * cs;
	const GLubyte *t0 = L->tex0 + src * t0s;
	const GLubyte *t1 = L->tex1 + src * t1s;

	for (; n > 0; n--)
	{
		gf_vertex(d, p, c, t0, t1, pos_int, col_kind);
		d++;
		p += ps;
		c += cs;
		t0 += t0s;
		t1 += t1s;
	}
}

/* Elements idx[0 .. n-1] into d[0 .. n-1]. */
static __inline__ __attribute__((always_inline)) void gf_run_indexed(const gf_layout *L, MGLVertex *d, int n, const int *idx,
                                                                     const int pos_int, const int col_kind)
{
	const GLint ps = L->pos_stride, cs = L->col_stride, t0s = L->tex0_stride, t1s = L->tex1_stride;
	const GLubyte *p = L->pos, *c = L->col, *t0 = L->tex0, *t1 = L->tex1;

	for (; n > 0; n--)
	{
		int s = *idx++;

		gf_vertex(d, p + s * ps, c + s * cs, t0 + s * t0s, t1 + s * t1s, pos_int, col_kind);
		d++;
	}
}

/* Gathers elements src .. src+n-1 into VertexBuffer[0 .. n-1], as n calls of
 * the generic gather with element position -1 would. Returns 0, having
 * written nothing, for a layout without a specialised loop. */
__attribute__((noinline)) int mglv3d_gather_fast(GLcontext context, int src, int n)
{
	gf_layout L;
	MGLVertex *d = context->VertexBuffer;

	if (!gf_decode(context, &L, 0))
		return 0;

	switch (L.pos_int * 3 + L.col_kind)
	{
		case 0:  gf_run(&L, d, n, src, 0, GF_COL_F4);   break;
		case 1:  gf_run(&L, d, n, src, 0, GF_COL_RGBA); break;
		case 2:  gf_run(&L, d, n, src, 0, GF_COL_ARGB); break;
		case 3:  gf_run(&L, d, n, src, 1, GF_COL_F4);   break;
		case 4:  gf_run(&L, d, n, src, 1, GF_COL_RGBA); break;
		default: gf_run(&L, d, n, src, 1, GF_COL_ARGB); break;
	}
	return 1;
}

static int s_gf_idx[MGL_MAXVERTS];

/* Gathers the elements indices[base .. base+n-1] into VertexBuffer[0 .. n-1],
 * as n generic gathers of ReadIndex(type, indices, base + i) would, with
 * element positions when elem_snapshots is set. Returns 0, having written
 * nothing, for a layout without a specialised loop. */
__attribute__((noinline)) int mglv3d_gather_fast_indexed(GLcontext context, GLenum type, const GLvoid *indices,
                                                         int base, int n, int elem_snapshots)
{
	gf_layout L;
	MGLVertex *d = context->VertexBuffer;
	int i;

	if (n > MGL_MAXVERTS || !gf_decode(context, &L, elem_snapshots))
		return 0;

	/* The indices widened once, the way ReadIndex widens them. */
	switch (type)
	{
		case GL_UNSIGNED_BYTE:
		{
			const GLubyte *s = (const GLubyte *)indices + base;
			for (i = 0; i < n; i++)
				s_gf_idx[i] = (int)s[i];
			break;
		}
		case GL_UNSIGNED_SHORT:
		{
			const GLushort *s = (const GLushort *)indices + base;
			for (i = 0; i < n; i++)
				s_gf_idx[i] = (int)s[i];
			break;
		}
		case GL_UNSIGNED_INT:
		default:
		{
			const GLuint *s = (const GLuint *)indices + base;
			for (i = 0; i < n; i++)
				s_gf_idx[i] = (int)s[i];
			break;
		}
	}

	switch (L.pos_int * 3 + L.col_kind)
	{
		case 0:  gf_run_indexed(&L, d, n, s_gf_idx, 0, GF_COL_F4);   break;
		case 1:  gf_run_indexed(&L, d, n, s_gf_idx, 0, GF_COL_RGBA); break;
		case 2:  gf_run_indexed(&L, d, n, s_gf_idx, 0, GF_COL_ARGB); break;
		case 3:  gf_run_indexed(&L, d, n, s_gf_idx, 1, GF_COL_F4);   break;
		case 4:  gf_run_indexed(&L, d, n, s_gf_idx, 1, GF_COL_RGBA); break;
		default: gf_run_indexed(&L, d, n, s_gf_idx, 1, GF_COL_ARGB); break;
	}
	return 1;
}


void GLDrawArrays(GLcontext context, GLenum mode, GLint first, GLsizei count)
{
	int chunk_cap;

	if (!(context->ClientState & GLCS_VERTEX))
	{
		D(("GLDrawArrays: GL_VERTEX_ARRAY not enabled, dropping\n"));
		GLFlagError(context, 1, GL_INVALID_OPERATION);
		return;
	}

	/* No position array: a glVertexPointer was refused and failed closed (see
	 * its policy comment). Drawing nothing is the safe reading -- the
	 * alternative is gathering from whatever pointer the application has
	 * since freed. */
	if (context->ArrayPointer.verts == NULL)
	{
		D(("GLDrawArrays: no vertex array bound (a refused glVertexPointer), dropping\n"));
		return;
	}

	/* GL_POLYGON is topologically identical to GL_TRIANGLE_FAN (a single
	 * convex polygon triangulated as a fan from vertex 0) -- the final
	 * `else` branch below handles exactly this shape and is reached by
	 * GL_POLYGON too, no separate logic. */
	if (mode != GL_TRIANGLES && mode != GL_TRIANGLE_STRIP && mode != GL_TRIANGLE_FAN && mode != GL_POLYGON &&
	    mode != GL_QUADS && mode != GL_QUAD_STRIP &&
	    mode != GL_POINTS && mode != GL_LINES && mode != GL_LINE_STRIP && mode != GL_LINE_LOOP)
	{
		D(("GLDrawArrays: mode %ld not supported, dropping\n", (LONG)mode));
		GLFlagError(context, 1, GL_INVALID_OPERATION);
		return;
	}

	/* Below the mode's own minimum GL draws nothing at all. */
	{
		int min_verts = 3;

		if (mode == GL_POINTS)
			min_verts = 1;
		else if (mode == GL_LINES || mode == GL_LINE_STRIP || mode == GL_LINE_LOOP)
			min_verts = 2;

		if (count < min_verts)
			return;
	}

	mglv3d_ub2f_sync(context);

	chunk_cap = (int)context->VertexBufferSize;
	if (chunk_cap > MGL_MAXVERTS)
		chunk_cap = MGL_MAXVERTS;

	/* GL_POINTS / GL_LINES / GL_LINE_STRIP / GL_LINE_LOOP: drawn with
	 * d_DrawPoints / d_DrawLines / d_DrawLineStrip / d_DrawLineLoop
	 * (draw.c), the same hardware primitives immediate mode uses.
	 *
	 * Each mode chunks differently:
	 *   POINTS      every vertex is independent, so any chunk size works.
	 *   LINES       the chunk is forced EVEN so a pair is never split across
	 *               two draws, and a trailing unpaired vertex is dropped as
	 *               GL specifies.
	 *   LINE_STRIP  one continuous primitive: every chunk after the first
	 *               repeats the previous chunk's LAST vertex as its own
	 *               first. Unlike a triangle strip this needs no parity
	 *               rule and only one carried vertex -- a line strip has no
	 *               winding to alternate, so the chunk size stays free.
	 *   LINE_LOOP   drawn as a real loop when it fits in one chunk, since
	 *               the hardware primitive closes it itself. When it does
	 *               not fit, it becomes a chunked strip plus one explicit
	 *               closing segment from the last vertex back to the first,
	 *               which is exactly what the loop means. */
	if (mode == GL_POINTS)
	{
		int base;

		for (base = 0; base < count; base += chunk_cap)
		{
			int n = count - base;
			int i;

			if (n > chunk_cap)
				n = chunk_cap;

			for (i = 0; i < n; i++)
				GatherVertexFromArray(context, i, first + base + i);

			context->VertexBufferPointer = n;
			DRAW_GATHERED(d_DrawPoints(context));
		}
		return;
	}

	if (mode == GL_LINES)
	{
		int line_chunk_cap = chunk_cap & ~1;
		int base;

		if (line_chunk_cap < 2)
		{
			D(("GLDrawArrays: VertexBufferSize too small to hold one line, dropping\n"));
			return;
		}

		count &= ~1;

		for (base = 0; base < count; base += line_chunk_cap)
		{
			int n = count - base;
			int i;

			if (n > line_chunk_cap)
				n = line_chunk_cap;

			for (i = 0; i < n; i++)
				GatherVertexFromArray(context, i, first + base + i);

			context->VertexBufferPointer = n;
			DRAW_GATHERED(d_DrawLines(context));
		}
		return;
	}

	if (mode == GL_LINE_STRIP || mode == GL_LINE_LOOP)
	{
		int closed = (mode == GL_LINE_LOOP);
		int used = 0;
		int first_chunk = 1;

		if (chunk_cap < 2)
		{
			D(("GLDrawArrays: VertexBufferSize too small to hold one line-strip chunk, dropping\n"));
			return;
		}

		if (closed && count <= chunk_cap)
		{
			int i;

			for (i = 0; i < count; i++)
				GatherVertexFromArray(context, i, first + i);

			context->VertexBufferPointer = count;
			DRAW_GATHERED(d_DrawLineLoop(context));
			return;
		}

		while (used < count)
		{
			int slot = 0;
			int new_count, k;

			if (!first_chunk)
				GatherVertexFromArray(context, slot++, first + used - 1);

			new_count = first_chunk ? chunk_cap : (chunk_cap - 1);
			if (new_count > count - used)
				new_count = count - used;

			for (k = 0; k < new_count; k++)
				GatherVertexFromArray(context, slot++, first + used + k);

			used += new_count;

			context->VertexBufferPointer = slot;
			DRAW_GATHERED(d_DrawLineStrip(context));

			first_chunk = 0;
		}

		if (closed)
		{
			GatherVertexFromArray(context, 0, first + count - 1);
			GatherVertexFromArray(context, 1, first);
			context->VertexBufferPointer = 2;
			DRAW_GATHERED(d_DrawLines(context));
		}
		return;
	}

	/* GL_QUADS and GL_QUAD_STRIP. A quad strip is, vertex for vertex, a
	 * triangle strip with the same winding -- quad n is (2n, 2n+1, 2n+3,
	 * 2n+2), and the strip's triangles (2n, 2n+1, 2n+2) and (2n+2, 2n+1,
	 * 2n+3) cover it with the same orientation -- so it simply takes the
	 * strip branch below, with a trailing odd vertex dropped as GL
	 * specifies. Independent quads become two triangles each (0,1,2 and
	 * 0,2,3, the same split the immediate-mode fan uses); six slots per
	 * quad, so a chunk holds chunk_cap/6 quads and never splits one. An
	 * incomplete trailing quad is ignored, as GL specifies. */
	if (mode == GL_QUAD_STRIP)
	{
		if (count < 4)
			return;
		count &= ~1;
		mode = GL_TRIANGLE_STRIP;
	}
	else if (mode == GL_QUADS)
	{
		static const int corner[6] = { 0, 1, 2, 0, 2, 3 };
		int quads = count / 4;
		int quads_per_chunk = chunk_cap / 6;
		int q = 0;

		if (quads_per_chunk < 1)
		{
			D(("GLDrawArrays: VertexBufferSize too small to hold even one quad, dropping\n"));
			return;
		}

		while (q < quads)
		{
			int n = quads - q;
			int i, slot = 0;

			if (n > quads_per_chunk)
				n = quads_per_chunk;

			for (i = 0; i < n; i++)
			{
				int k;
				for (k = 0; k < 6; k++)
					GatherVertexFromArray(context, slot++, first + (q + i) * 4 + corner[k]);
			}

			context->VertexBufferPointer = slot;
			DRAW_GATHERED(d_DrawTriangles(context));
			q += n;
		}
		return;
	}

	if (mode == GL_TRIANGLES)
	{
		int base;
		int tri_chunk_cap = (chunk_cap / 3) * 3;

		if (tri_chunk_cap < 3)
		{
			D(("GLDrawArrays: VertexBufferSize too small to hold even one triangle, dropping\n"));
			return;
		}

		for (base = 0; base < count; base += tri_chunk_cap)
		{
			int n = count - base;
			int i;

			if (n > tri_chunk_cap)
				n = tri_chunk_cap;
			n = (n / 3) * 3;
			if (n == 0)
				break;

			if (mglv3d_gather_fast(context, first + base, n))
			{
			}
			else
			{
				for (i = 0; i < n; i++)
					GatherVertexFromArray(context, i, first + base + i);
			}

			context->VertexBufferPointer = n;
			DRAW_GATHERED(d_DrawTriangles(context));
		}
	}
	else if (mode == GL_TRIANGLE_STRIP)
	{
		/* GL_TRIANGLE_STRIP: ONE continuous primitive -- a chunk boundary
		 * must carry over the LAST TWO vertices of the previous chunk as
		 * the first two of the next, so the strip's connectivity isn't
		 * lost across the cut. chunk_cap_strip is forced EVEN: each
		 * native V3D_PRIM_TRIANGLESTRIP call's own hardware winding-
		 * alternation always treats ITS OWN local vertex 0 as the start
		 * of an "even" triangle -- an even chunk size means every chunk
		 * (first and subsequent) contributes an EVEN number of triangles
		 * (chunk_cap_strip-2), so every chunk boundary lands on an
		 * original triangle index that's ALSO even, keeping every chunk's
		 * local "even" naturally aligned with the unchunked strip's real
		 * parity -- no per-triangle fixup needed. Swapping the 2
		 * carried-over vertices' order instead would be WRONG: a native
		 * strip call is a sliding window over its own local vertex array,
		 * so swapping which vertex sits in local slot 0 vs 1 changes which
		 * vertex EVERY later triangle in that chunk shares, not just the
		 * first one's winding -- it corrupts geometry. */
		int chunk_cap_strip = chunk_cap;
		int used, first_chunk;

		if (chunk_cap_strip & 1)
			chunk_cap_strip--;

		if (chunk_cap_strip < 4)
		{
			D(("GLDrawArrays: VertexBufferSize too small to hold even one strip chunk, dropping\n"));
			return;
		}

		used = 0;
		first_chunk = 1;

		while (used < count)
		{
			int local_count, slot;

			if (first_chunk)
			{
				local_count = (count - used < chunk_cap_strip) ? (count - used) : chunk_cap_strip;

				for (slot = 0; slot < local_count; slot++)
					GatherVertexFromArray(context, slot, first + used + slot);

				used += local_count;
			}
			else
			{
				int new_count = (count - used < chunk_cap_strip - 2) ? (count - used) : (chunk_cap_strip - 2);

				GatherVertexFromArray(context, 0, first + used - 2);
				GatherVertexFromArray(context, 1, first + used - 1);

				for (slot = 0; slot < new_count; slot++)
					GatherVertexFromArray(context, 2 + slot, first + used + slot);

				local_count = 2 + new_count;
				used += new_count;
			}

			context->VertexBufferPointer = local_count;
			DRAW_GATHERED(d_DrawTriangleStrip(context));

			first_chunk = 0;
		}
	}
	else /* GL_TRIANGLE_FAN, GL_POLYGON */
	{
		/* GL_TRIANGLE_FAN: no winding-alternation concern at all (every
		 * triangle shares the hub, consistently wound by construction), so
		 * chunking is simpler than the strip case above: each chunk after
		 * the first carries over just the hub + the single last perimeter
		 * vertex of the previous chunk, no parity tracking needed. */
		int hub = first;
		int perim_count = count - 1;
		int perim_used = 0;
		int first_chunk = 1;

		if (chunk_cap < 3)
		{
			D(("GLDrawArrays: VertexBufferSize too small to hold even one fan chunk, dropping\n"));
			return;
		}

		while (perim_used < perim_count)
		{
			int slot = 0;
			int new_count;

			GatherVertexFromArray(context, slot++, hub);

			if (!first_chunk)
				GatherVertexFromArray(context, slot++, first + 1 + perim_used - 1);

			new_count = first_chunk ? (chunk_cap - 1) : (chunk_cap - 2);
			if (new_count > perim_count - perim_used)
				new_count = perim_count - perim_used;

			{
				int k;
				for (k = 0; k < new_count; k++)
					GatherVertexFromArray(context, slot++, first + 1 + perim_used + k);
			}

			perim_used += new_count;

			context->VertexBufferPointer = slot;
			DRAW_GATHERED(d_DrawTriangleFan(context));

			first_chunk = 0;
		}
	}
}

/*
 * glMultiDrawArrays: OpenGL 1.4 core function (also
 * GL_EXT_multi_draw_arrays). A loop calling GLDrawArrays once per
 * (first[i], count[i]) pair -- glMultiDrawArrays draws N *independent*
 * primitives, not one long one.
 */
void GLMultiDrawArrays(GLcontext context, GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount)
{
	GLsizei i;

	for (i = 0; i < primcount; i++)
		GLDrawArrays(context, mode, first[i], count[i]);
}

/*
 * $Id: vertexbuffer.c,v 1.1.1.1 2000/04/07 19:44:51 hfrieden Exp $
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
 * MiniGLV3D fork of part of MiniGL/src/vertexbuffer.c; that file's
 * GLDepthRange and GLViewport are in viewport.c. The plain (non-USE_MGLAPI)
 * glVertex3f()/glColor3f() macros in gl.h call the GLVertex* and GLColor*
 * functions here.
 *
 * GLBegin picks the d_Draw* function (draw.c) for the primitive. GL_QUADS
 * and GL_QUAD_STRIP need no decomposition here: d_DrawQuads and
 * d_DrawQuadStrip draw each quad as a V3D_PRIM_TRIANGLEFAN. GL_POLYGON
 * reuses d_DrawTriangleFan (a convex polygon triangulates the same way as a
 * fan from vertex 0; see its case label below). GL_LINE_STRIP and
 * GL_LINE_LOOP have their own implementations (d_DrawLineStrip/
 * d_DrawLineLoop), although the original maps both to an empty
 * d_DrawLineStrip. A mode GLBegin has no case for is flagged
 * GL_INVALID_OPERATION. Through CurrentDraw, GL_TRIANGLES always draws
 * with d_DrawTriangles: the original's switch in GLEnd to a vertex-array
 * path (d_DrawTrianglesVA) for large batches is not ported.
 *
 * GLVertex4f and its variants write the raw, untransformed input into
 * `.v.x/.v.y/.v.z/.v.w`. That is what draw.c feeds V3D's vertex/coordinate
 * shaders, which take raw object-space input plus a uniform matrix (see the
 * QPU assembly in backend/hw/v3d_assembler.c and draw.c's header comment).
 *
 * The glColor* family also mirrors the current colour into
 * context->backend.fixed_color (packed RGBA), which draw.c's shader-uniform
 * code reads as the current flat colour. The float variants clamp every
 * component to [0,1] under CLAMP_COLORS (defined in config.h), as the
 * original does:
 * fixed_color packs each component as (v3d_u32)(c * 255.0f) with no
 * masking, so an out-of-range component spills into the neighbouring byte
 * (blue 2.0 gives 0x01FE0000) and 1.005 leaves 0x00 in its own byte. The
 * ubyte variants skip the clamp, as the original does: a ubyte is always in
 * range. They share SetCurrentColor with GLColor4f rather than duplicating
 * its CurrentColor/fixed_color logic.
 */

#include "sysinc.h"
#include "../../backend/hw/v3d_debug.h"

extern void fog_Set(GLcontext context);
extern void GLDrawElements(GLcontext context, GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
extern void d_DrawTriangles(GLcontext context);
extern void d_DrawPoints(GLcontext context);
extern void d_DrawLines(GLcontext context);
extern void d_DrawLineStrip(GLcontext context);
extern void d_DrawLineLoop(GLcontext context);
extern void d_DrawTriangleFan(GLcontext context);
extern void d_DrawTriangleStrip(GLcontext context);
extern void d_DrawQuads(GLcontext context);
extern void d_DrawQuadStrip(GLcontext context);

void GLBegin(GLcontext context, GLenum mode)
{
	context->CurrentTexQValid = GL_FALSE;
	/* CurrentTexQValid (Surgeon's, in MiniGL) is cleared here and set only
	 * by glTexCoord4f. CurTexQ0, the q GLVertex4f latches into every vertex,
	 * is reset wherever CurrentTexQValid is, so a glTexCoord4f's q does not
	 * carry into the next block. GL would carry the current q across
	 * glBegin, but this driver treats q as per-block. The only reader of
	 * CurrentTexQValid is hclip.c, part of the CPU clipper, which has no
	 * callers (see GLVertex4f). */
	context->CurTexQ0 = 1.0f;
	context->VertexBufferPointer = 0;
	/* Reset per glBegin/glEnd sequence; see context.h's comment on
	 * UsedArrayElement. */
	context->UsedArrayElement = GL_FALSE;

	switch((int)mode)
	{
		case GL_TRIANGLES:
			context->CurrentPrimitive = mode;
			context->CurrentDraw = (DrawFn)d_DrawTriangles;
			break;

		case GL_POINTS:
			context->CurrentPrimitive = mode;
			context->CurrentDraw = (DrawFn)d_DrawPoints;
			break;

		case GL_LINES:
			context->CurrentPrimitive = mode;
			context->CurrentDraw = (DrawFn)d_DrawLines;
			break;

		case GL_LINE_STRIP:
			/* d_DrawLineStrip is empty ({}) in the original MiniGL, which
			 * maps both GL_LINE_STRIP and GL_LINE_LOOP to it. See draw.c's
			 * comment on d_DrawLineStrip/d_DrawLineLoop. */
			context->CurrentPrimitive = mode;
			context->CurrentDraw = (DrawFn)d_DrawLineStrip;
			break;

		case GL_LINE_LOOP:
			context->CurrentPrimitive = mode;
			context->CurrentDraw = (DrawFn)d_DrawLineLoop;
			break;

		case GL_TRIANGLE_FAN:
			context->CurrentPrimitive = mode;
			context->CurrentDraw = (DrawFn)d_DrawTriangleFan;
			break;

		case MGL_FLATFAN:
		case GL_POLYGON:
			/* MGL_FLATFAN is a MiniGL-specific mode, not real GL. In
			 * MiniGL it is a fan given in device coordinates, drawn
			 * without the current matrices and viewport; here it has no
			 * such bypass and is drawn exactly like GL_TRIANGLE_FAN
			 * (nothing in draw.c tests for MGL_FLATFAN). A convex
			 * GL_POLYGON triangulates identically to a fan from vertex 0.
			 * The original splits GL_POLYGON three ways (d_DrawNormalPoly/
			 * d_DrawSmoothPoly/d_DrawMtexPoly, chosen by smooth/texture
			 * state at glBegin time). d_DrawTriangleFan dispatches
			 * smooth/textured/fog/blend from GL state via the shared
			 * gl_EmitPrimitiveV3D, so it covers all three. See draw.c's
			 * comment on d_DrawMtexPoly/d_DrawSmoothPoly/d_DrawNormalPoly. */
			context->CurrentPrimitive = mode;
			context->CurrentDraw = (DrawFn)d_DrawTriangleFan;
			break;

		case MGL_FLATSTRIP:
		case GL_TRIANGLE_STRIP:
			/* MGL_FLATSTRIP (Surgeon's addition to MiniGL, similar to
			 * MGL_FLATFAN): as with MGL_FLATFAN above, there is no
			 * device-coordinate bypass; it is drawn exactly like
			 * GL_TRIANGLE_STRIP, and d_DrawTriangleStrip dispatches
			 * shading/texture/blend from GL state. */
			context->CurrentPrimitive = mode;
			context->CurrentDraw = (DrawFn)d_DrawTriangleStrip;
			break;

		case GL_QUADS:
			context->CurrentPrimitive = mode;
			context->CurrentDraw = (DrawFn)d_DrawQuads;
			break;

		case GL_QUAD_STRIP:
			context->CurrentPrimitive = mode;
			context->CurrentDraw = (DrawFn)d_DrawQuadStrip;
			break;

		default:
			GLFlagError(context, 1, GL_INVALID_OPERATION);
			break;
	}

	if(context->NormalBufferPointer)
	{
		context->NormalBuffer[0].x = context->NormalBuffer[context->NormalBufferPointer].x;
		context->NormalBuffer[0].y = context->NormalBuffer[context->NormalBufferPointer].y;
		context->NormalBuffer[0].z = context->NormalBuffer[context->NormalBufferPointer].z;

		context->NormalBufferPointer = 0;
	}
}

/*
 * glArrayElement(i) -- used inside glBegin/glEnd instead of a real
 * glVertex call, once glEnableClientState(GL_VERTEX_ARRAY) +
 * glVertexPointer(...) are set up, to record vertex INDICES to be drawn
 * from the bound arrays via glDrawElements once glEnd is reached. The
 * drawing happens in GLEnd below and in GLDrawElements (vertexelements.c).
 */
void GLArrayElement(GLcontext context, GLint i)
{
	/* Snapshot the CURRENT texcoord (as of THIS call), not just the vertex
	 * index -- see context.h's ElementTexS/ElementTexT comment for why GL's
	 * glArrayElement semantics require it and why reading it at gather time
	 * would not do. Same slot as ElementIndex, so GatherVertexFromIndex can
	 * look this exact call's value back up later. */
	context->ElementTexS[context->VertexBufferPointer] = context->CurrentTexS;
	context->ElementTexT[context->VertexBufferPointer] = context->CurrentTexT;
	context->ElementIndex[context->VertexBufferPointer++] = (UWORD)i;
	/* Mark THIS sequence as glArrayElement-based -- see context.h's
	 * comment on UsedArrayElement. */
	context->UsedArrayElement = GL_TRUE;
}

void GLEnd(GLcontext context)
{
	if(context->VertexBufferPointer == 0)
	{
		context->CurrentPrimitive = GL_BASE;
		return;
	}

	/* A series of glArrayElement calls recorded indices into ElementIndex
	 * rather than real vertex data -- dispatch through GLDrawElements
	 * instead of CurrentDraw, as the original's GLEnd does.
	 *
	 * The test is UsedArrayElement, not the original's
	 * `context->ClientState & GLCS_VERTEX` (see context.h's comment on
	 * UsedArrayElement). GL_VERTEX_ARRAY client state is GLOBAL and
	 * persistent, unrelated to any one glBegin/End sequence: an
	 * application can enable it once for its own array-based rendering
	 * while using plain glVertex-based immediate mode elsewhere, and
	 * testing it would send every one of those plain sequences through
	 * GLDrawElements/ElementIndex (which they never populate) instead of
	 * CurrentDraw. UsedArrayElement is reset per sequence by GLBegin and
	 * set only by a real GLArrayElement call, matching GL semantics. */
	if (context->UsedArrayElement)
	{
		GLDrawElements(context, context->CurrentPrimitive, context->VertexBufferPointer, GL_UNSIGNED_SHORT, context->ElementIndex);
		context->CurrentPrimitive = GL_BASE;
		return;
	}

	if (context->FogDirty && context->Fog_State)
	{
		fog_Set(context);
		context->FogDirty = GL_FALSE;
	}

	/* As in the original, the draw is wrapped in a lock/unlock when
	 * LockMode==MGL_LOCK_AUTOMATIC (AUTOMATIC_LOCKING_ENABLE is defined in
	 * config.h); the original uses W3D_LockHardware/UnLockHardware. No
	 * real hardware lock primitive exists in this backend (context.c's
	 * header comment on MGLLockDisplay/MGLLockBack -- the only
	 * hardware-touching primitive is a whole binning/render CL
	 * submit+wait, not an incremental lock), so MGLLockDisplay always
	 * succeeds: the original's "lock failed" error branch has no
	 * counterpart, and the lock only keeps v3dLocked tracked around the
	 * draw for any caller that checks it. Under MGL_LOCK_MANUAL (the
	 * context-init default -- see context.c) and MGL_LOCK_SMART (the
	 * original's timer-based lock has no counterpart) CurrentDraw is called
	 * directly, with no lock/unlock at all. */
#ifdef AUTOMATIC_LOCKING_ENABLE
	if (context->LockMode == MGL_LOCK_AUTOMATIC)
		MGLLockDisplay(context);
#endif

	if (context->CurrentDraw)
	{
		context->CurrentDraw(context);
	}
	else
	{
		D(("GLEnd: no CurrentDraw set (primitive type not yet supported), dropping %ld vertices\n",
		   (LONG)context->VertexBufferPointer));
	}

#ifdef AUTOMATIC_LOCKING_ENABLE
	if (context->LockMode == MGL_LOCK_AUTOMATIC)
		MGLUnlockDisplay(context);
#endif

	context->CurrentPrimitive = GL_BASE;
}

/* Surgeon's (marked "surgeon:" in the MiniGL source). */
void GLPointSize(GLcontext context, GLfloat size)
{
	context->CurrentPointSize = size;
}

/*
 * glLineWidth. Read exactly as CurrentPointSize is: by gl_EnsureDrawState
 * (draw.c), which is gated by draw_state_configured, and only gl_FrameBegin
 * resets that flag, when it starts a new pass. So a width changed after a
 * pass's first draw takes effect only in the next pass, and the same holds
 * for point size. Set the width before the pass's first draw.
 *
 * The zero test has to be explicit. GL says a width of zero or less is
 * GL_INVALID_VALUE and leaves the state alone, but GLFlagError only records
 * the error and does not return (gl.h) -- a bare flag here would fall
 * straight through and hand the hardware a zero-width line.
 */
void GLLineWidth(GLcontext context, GLfloat width)
{
	if (width <= 0.0f)
	{
		GLFlagError(context, 1, GL_INVALID_VALUE);
		return;
	}

	context->CurrentLineWidth = width;
}

/* GLFinish blocks until previously submitted GPU work is complete:
 * MGLFlushPendingRender (context.c) is that wait -- it is what
 * gl_FrameBegin calls before reusing a CL-buffer slot -- and it also
 * releases the bitmap lock the pipelined render holds between frames.
 *
 * INCOMPLETE: GL requires glFinish to complete all previously ISSUED
 * commands. This covers only the previous frame's still-in-flight pipelined
 * render; the CURRENT frame's accumulated binning list holds draws that have
 * been issued and not yet rendered, and this returns with them still pending.
 * Rendering that bin means arming force_new_pass and calling gl_FrameBegin
 * before this flush (the pass split MGLReadbackBegin uses), and it is not
 * free: every glFinish reached with geometry binned would force an extra
 * render pass. The MGLFlushPendingRender call must stay unconditional even
 * then -- gl_FrameBegin's scratch-allocation failure path returns BEFORE its
 * own flush -- and an application can depend on glFinish releasing the
 * between-frames bitmap lock (one that hands its fullscreen display to
 * another program hangs without it). */
void GLFinish(GLcontext context)
{
	extern void MGLFlushPendingRender(GLcontext context);
	MGLFlushPendingRender(context);
}

void GLFlush(GLcontext context)
{
}

/* Shared store for the whole glColor* family: CurrentColor plus the packed
 * backend.fixed_color mirror. Components must already be in [0,1] -- the
 * float entry points clamp (below), the ubyte ones are in range by
 * construction. */
static inline void SetCurrentColor(GLcontext context, GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
	context->CurrentColor.r = red;
	context->CurrentColor.g = green;
	context->CurrentColor.b = blue;
	context->CurrentColor.a = alpha;
	context->UpdateCurrentColor = GL_TRUE;

	context->backend.fixed_color =
		  ((v3d_u32)(alpha * 255.0f) << 24)
		| ((v3d_u32)(blue  * 255.0f) << 16)
		| ((v3d_u32)(green * 255.0f) << 8)
		|  (v3d_u32)(red   * 255.0f);
}

void GLColor4f(GLcontext context, GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
#ifdef CLAMP_COLORS
	/* Clamp to [0,1] like the original, where Surgeon moved the clamp into
	 * the glColor functions (see this file's header comment for the spill
	 * an unclamped component causes). */
	if (red   < 0.0f) red   = 0.0f; else if (red   > 1.0f) red   = 1.0f;
	if (green < 0.0f) green = 0.0f; else if (green > 1.0f) green = 1.0f;
	if (blue  < 0.0f) blue  = 0.0f; else if (blue  > 1.0f) blue  = 1.0f;
	if (alpha < 0.0f) alpha = 0.0f; else if (alpha > 1.0f) alpha = 1.0f;
#endif

	SetCurrentColor(context, red, green, blue, alpha);
}

void GLColor3f(GLcontext context, GLfloat red, GLfloat green, GLfloat blue)
{
	GLColor4f(context, red, green, blue, 1.0f);
}

void GLColor4fv(GLcontext context, GLfloat *v)
{
	GLColor4f(context, v[0], v[1], v[2], v[3]);
}

void GLColor3fv(GLcontext context, GLfloat *v)
{
	GLColor4f(context, v[0], v[1], v[2], 1.0f);
}

/*
 * GLColor4ub/3ub/4ubv/3ubv, straight ubyte->float conversion (no clamping
 * needed, as the original notes: a ubyte is always 0-255), routed through
 * SetCurrentColor above -- not through GLColor4f, whose clamp would be
 * eight wasted compares per call here -- instead of repeating the
 * CurrentColor stores in each function the way the original's separate
 * ubyte functions do.
 */
void GLColor4ub(GLcontext context, GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
	SetCurrentColor(context, (float)red / 255.0f, (float)green / 255.0f, (float)blue / 255.0f, (float)alpha / 255.0f);
}

void GLColor3ub(GLcontext context, GLubyte red, GLubyte green, GLubyte blue)
{
	SetCurrentColor(context, (float)red / 255.0f, (float)green / 255.0f, (float)blue / 255.0f, 1.0f);
}

void GLColor4ubv(GLcontext context, GLubyte *v)
{
	SetCurrentColor(context, (float)v[0] / 255.0f, (float)v[1] / 255.0f, (float)v[2] / 255.0f, (float)v[3] / 255.0f);
}

void GLColor3ubv(GLcontext context, GLubyte *v)
{
	SetCurrentColor(context, (float)v[0] / 255.0f, (float)v[1] / 255.0f, (float)v[2] / 255.0f, 1.0f);
}

/*
 * GLNormal3f pushes onto context->NormalBuffer[++NormalBufferPointer]
 * exactly like the original; the buffer is allocated by MGLInitContext.
 * The normal buffer, in place of per-vertex normals, is Surgeon's (MiniGL).
 * No lighting is implemented in this port: the normals' only reader is
 * sphere-map texgen (draw.c's v_GenTexCoords).
 * GLNormal3fv is empty in the original too, and is mirrored as-is.
 */
void GLNormal3f(GLcontext context, GLfloat x, GLfloat y, GLfloat z)
{
	GLuint nbp = ++context->NormalBufferPointer;
	context->NormalBuffer[nbp].x = x;
	context->NormalBuffer[nbp].y = y;
	context->NormalBuffer[nbp].z = z;
}

void GLNormal3fv(GLcontext context, GLfloat *n)
{
}

/*
 * glIndexi / glIndexiv. The current colour INDEX, kept as a float the way
 * GL keeps it and answered through GL_CURRENT_INDEX. This context is RGBA
 * only -- GL_RGBA_MODE answers TRUE, GL_INDEX_MODE FALSE -- and GL specifies
 * that the current index has no effect on rendering in RGBA mode, so storing
 * it is the whole implementation, not a placeholder. The integer converts
 * directly, with no scaling, as GL says.
 */
void GLIndexi(GLcontext context, GLint c)
{
	context->CurrentIndex = (GLfloat)c;
}

void GLIndexiv(GLcontext context, const GLint *c)
{
	context->CurrentIndex = (GLfloat)(*c);
}

/*
 * glEdgeFlag / glEdgeFlagv. GL uses the edge flag for one thing: marking
 * which polygon edges are boundary edges when glPolygonMode is GL_LINE or
 * GL_POINT. That mode is inert in this driver -- GLPolygonMode stores it and
 * only the query reads it -- so every polygon is filled, and a filled polygon
 * has no edges for a flag to suppress. Storing the flag and answering
 * GL_EDGE_FLAG is therefore everything GL asks for TODAY.
 *
 * CORRECT ONLY WHILE POLYGON MODE STAYS INERT. The day GL_LINE is implemented
 * this becomes load-bearing: GLVertex4f would have to latch the flag per vertex
 * the way it latches the current colour, glEdgeFlagPointer's array would have
 * to be gathered, and the outline pass would have to skip every edge whose
 * starting vertex carries GL_FALSE.
 */
void GLEdgeFlag(GLcontext context, GLboolean flag)
{
	context->CurrentEdgeFlag = flag ? GL_TRUE : GL_FALSE;
}

void GLEdgeFlagv(GLcontext context, const GLboolean *flag)
{
	context->CurrentEdgeFlag = (*flag) ? GL_TRUE : GL_FALSE;
}

/*
 * GLTexCoord2f/2fv/4f/4fv, ported from minigl.h's inline (USE_MGLAPI-only)
 * bodies (config.h leaves USE_MGLAPI undefined, so those inline
 * definitions never compile in). Same "current state, latched by the NEXT
 * glVertex call" convention as GLColor4f/GLVertex4f: these set the current
 * texture coordinate, which GLVertex4f copies into the vertex slot it
 * fills. So glTexCoord2f/4f must be called BEFORE glVertex for a given
 * vertex, matching GL's call order -- not enforced here.
 *
 * Unit 0's coordinate lands in .v.u0/.v0 (vertexbuffer.h). draw.c's
 * real_w_combined mechanism is the consumer of `.q`.
 */
void GLTexCoord2f(GLcontext context, GLfloat s, GLfloat t)
{
	/* THE current texture coordinate, not a write into the next vertex
	 * slot: GLVertex4f latches these into every vertex, exactly as it
	 * latches CurrentColor. Every vertex takes its q from CurTexQ0, which
	 * this sets to 1.0, so a glTexCoord4f's q cannot leak into a later
	 * vertex. */
	context->CurTexU0 = s;
	context->CurTexV0 = t;
	context->CurTexQ0 = 1.0f;

	/* CurrentTexS/CurrentTexT are what GatherVertexFromIndex/
	 * GatherVertexFromArray (vertexelements.c/vertexarray.c) use when no
	 * texcoord array is bound -- directly, or through GLArrayElement's
	 * per-slot snapshot of them -- as they use CurrentColor when no color
	 * array is bound. A caller that issues glTexCoord2f() per vertex
	 * followed by glArrayElement() -- never glTexCoordPointer() -- depends
	 * on this. */
	context->CurrentTexS = s;
	context->CurrentTexT = t;
}

void GLTexCoord2fv(GLcontext context, GLfloat *v)
{
	GLTexCoord2f(context, v[0], v[1]);
}

void GLTexCoord4f(GLcontext context, GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
	/* Real per-pixel Q perspective correction. A Glide-compatibility caller
	 * passes s,t in Glide's GrVertex convention, sow/tow ("S-over-W"/
	 * "T-over-W") -- i.e. s,t ARRIVE already pre-divided by w, with q (oow,
	 * "one-over-W") supplied separately for hardware to undo that division
	 * per-pixel. Storing sow/tow raw and letting draw.c's recip mechanism
	 * divide by q AGAIN would divide by w twice. Dividing here (s/q, t/q =
	 * sow/oow = s, tow/oow = t) recovers the TRUE s,t once -- then draw.c's
	 * real_w_combined mechanism (feeding q into position/recip) applies the
	 * ONE per-pixel perspective division on top, via the GPU's
	 * fixed-function interpolation. real_w_combined covers only smooth-shaded
	 * single-texture draws (draw.c's `combined`, and smooth_alphatest); every
	 * other variant uses w = 1.0, so q has no effect there. */
	/* Current state, latched by glVertex -- but the DIVIDED s/q, t/q, which
	 * is why CurTexU0/V0 are separate from the raw CurrentTexS/T below. */
	context->CurTexU0 = s / q;
	context->CurTexV0 = t / q;
	context->CurTexQ0 = q;
	context->CurrentTexQValid = GL_TRUE;
	context->CurrentTexS = s;
	context->CurrentTexT = t;
}

void GLTexCoord4fv(GLcontext context, GLfloat *v)
{
	GLTexCoord4f(context, v[0], v[1], v[2], v[3]);
}

/*
 * Ported from minigl.h's inline (USE_MGLAPI-only)
 * `glActiveTextureARB`/`glMultiTexCoord2fARB`/`glMultiTexCoord2fvARB`
 * bodies (GLActiveTextureARB is in texture.c), which never compile in
 * here, like GLTexCoord2f/4f's (see that comment above). `unit - GL_TEXTURE0_ARB` selects which of MGLVertex_t's
 * two texcoord pairs the vertex gets -- `.v.u0/.v0` for unit 0,
 * `.v.u1/.v1` for unit 1 -- matching MAX_TEXUNIT == 2 (vertexbuffer.h). An
 * out-of-range unit is refused with GL_INVALID_ENUM.
 */
void GLMultiTexCoord2fARB(GLcontext context, GLenum unit, GLfloat s, GLfloat t)
{
	int u = unit - GL_TEXTURE0_ARB;

	/* `>=`, not `>`: unit MAX_TEXUNIT is already out of range, and letting
	 * GL_TEXTURE2_ARB through here would overwrite unit 1's texcoord. */
	if (u < 0 || u >= MAX_TEXUNIT)
	{
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	/* Per-unit current state, latched by glVertex, for unit 1 as for
	 * unit 0. */
	if (u)
	{
		context->CurTexU1 = s;
		context->CurTexV1 = t;
	}
	else
	{
		context->CurTexU0 = s;
		context->CurTexV0 = t;
		context->CurTexQ0 = 1.0f;
		/* Unit 0 through this entry point is still the current texcoord, so
		 * the array gather's raw fallback has to follow it too, as it does
		 * for glTexCoord2f. */
		context->CurrentTexS = s;
		context->CurrentTexT = t;
	}
}

void GLMultiTexCoord2fvARB(GLcontext context, GLenum unit, GLfloat *v)
{
	GLMultiTexCoord2fARB(context, unit, v[0], v[1]);
}

void GLVertex4f(GLcontext context, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
	/* The slot, computed once. As a macro over the context, every store
	 * below would re-derive VertexBuffer + VertexBufferPointer * 92: under
	 * -fno-strict-aliasing GCC cannot assume a float store leaves either field
	 * alone. VertexBuffer is a separate allocation, so storing through the
	 * slot cannot change them. */
	MGLVertex *tv = &context->VertexBuffer[context->VertexBufferPointer];
	#define thisvertex (*tv)

	/* Stamp .color for EVERY vertex, not just under GL_SMOOTH as the
	 * original does -- needed so GL_FLAT primitives that change glColor3f
	 * BETWEEN primitives WITHIN one glBegin/glEnd block (e.g. alternating
	 * color per quad in one GL_QUAD_STRIP) have a real per-vertex color to
	 * read the PROVOKING vertex's flat color from at draw time -- see
	 * d_DrawQuadStrip's comment (draw.c). The single "current" colour cannot
	 * recover which color was active for an EARLIER vertex once a later
	 * glColor3f call happened. Cheap (4 float stores) and harmless to
	 * GL_SMOOTH. */
	thisvertex.color.r = context->CurrentColor.r;
	thisvertex.color.g = context->CurrentColor.g;
	thisvertex.color.b = context->CurrentColor.b;
	thisvertex.color.a = context->CurrentColor.a;

	/* The current TEXTURE COORDINATE, latched here like the colour above:
	 * GL defines the current texcoord as persistent state that every vertex
	 * captures, so `glTexCoord2f(); glVertex(); glVertex();` textures both
	 * vertices. Both units, and `.q`. */
	thisvertex.v.u0 = context->CurTexU0;
	thisvertex.v.v0 = context->CurTexV0;
	thisvertex.q    = context->CurTexQ0;
	thisvertex.v.u1 = context->CurTexU1;
	thisvertex.v.v1 = context->CurTexV1;

	/* .bx/.by/.bz/.bw are not written: nothing reachable reads them. Their
	 * readers are the CPU clipper (hclip.c/aclip.c, v_Transform,
	 * d_ConservativeDecideFrontface), which has no callers; reviving it needs
	 * these four stores back. */

	/* Raw object-space position, what draw.c feeds the vertex shaders --
	 * see this file's header comment. */
	thisvertex.v.x = x;
	thisvertex.v.y = y;
	thisvertex.v.z = z;
	thisvertex.v.w = w;

	thisvertex.normal = context->NormalBufferPointer;

	context->VertexBufferPointer ++;
	#undef thisvertex
}

void GLVertex4fv(GLcontext context, GLfloat *v)
{
	GLVertex4f(context, v[0], v[1], v[2], v[3]);
}

void GLVertex3fv(GLcontext context, GLfloat *v)
{
	GLVertex4f(context, v[0], v[1], v[2], 1.0f);
}

void GLVertex2f(GLcontext context, GLfloat x, GLfloat y)
{
	GLVertex4f(context, x, y, 0.0f, 1.0f);
}

void GLVertex2fv(GLcontext context, GLfloat *v)
{
	GLVertex4f(context, v[0], v[1], 0.0f, 1.0f);
}

/* Called unconditionally by init.c's MGLInit(). In the original the body
 * lives in vertexarray.c: a one-time fill of a file-local static lookup
 * table that nothing outside that file depends on. Nothing in this port
 * uses such a table, so an empty body is safe. */
void Init_ArrayToElements_Warpper(void)
{
}

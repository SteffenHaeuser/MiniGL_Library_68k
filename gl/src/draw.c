/*
 * $Id: draw.c,v 1.4 2001/02/01 14:36:49 tfrieden Exp $
 *
 * $Date: 2001/02/01 14:36:49 $
 * $Revision: 1.4 $
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
 * MiniGLV3D fork of MiniGL/src/draw.c.
 *
 * Every terminal draw function in the original ends in an immediate-mode
 * Warp3D call (W3D_DrawTriangle/W3D_DrawTriFanV/W3D_DrawArray/etc -- one
 * call rasterizes one primitive RIGHT NOW). V3D has no equivalent: it's
 * tile-based, geometry must be BINNED across a whole frame's worth of
 * draw calls, then RENDERED once at the end. See
 * context.c's own header comment (gl_FrameBegin/gl_FramePresent) for the
 * frame-accumulation architecture this file's terminal functions plug
 * into: gl_FrameBegin (context.c, extern here) guarantees a frame is
 * being accumulated; MGLSwitchDisplay (context.c) is what actually
 * submits, later, once the whole frame -- however many draw calls -- has
 * been appended to the binning list.
 *
 * VERTEX PIPELINE
 *
 * The original's v_ToScreen did a FULL CPU-side perspective-divide +
 * viewport-scale, producing literal screen-pixel coordinates -- because
 * Warp3D's immediate-mode rasterizer expected already-transformed,
 * already-screen-space vertices (it did no transform of its own).
 * V3D's vertex/coordinate shaders are NOT that kind of rasterizer target
 * -- their own QPU instructions (`x_p = x_s / w_s * scale_p * 0.5 *
 * 256.0`, see backend/hw/v3d_assembler.c's g_vertex_shader_assembly/
 * g_coordinate_shader_assembly) take RAW OBJECT-SPACE position, multiply
 * by a uniform 4x4 matrix to get clip-space (x_s/y_s/w_s), THEN do the
 * perspective-divide+scale themselves, on the GPU. Feeding these shaders
 * v_ToScreen's already-divided-and-scaled output would double-apply that
 * math and produce garbage.
 *
 * So: v_ToScreen is not ported at all. Instead, the emitters below feed
 * the shader RAW OBJECT-SPACE vertex data -- captured by
 * vertexbuffer_min.c's GLVertex4f into `.v.x/y/z/w` (V3DVertex's own
 * fields) at glVertex-call time -- plus the current
 * context->CombinedMatrix (MiniGL's own ModelView*Projection combined
 * matrix, matrix.c) as the shader's uniform matrix. The mapping from
 * CombinedMatrix's OF_11..OF_44 access (matrix.h) onto the shader
 * uniform's M_00..M_33 naming (v3d_my_uniforms, below) is derived in
 * gl_EmitPrimitiveV3DEx's own uniform-setup comment.
 *
 * gl_EmitPrimitiveV3D's use_clip_space parameter is the exception: with
 * it set, the emitter reads `.bx/.by/.bz` (clip-space) instead of
 * `.v.x/y/z` and feeds an IDENTITY matrix instead of
 * context->CombinedMatrix -- the position is already transformed, so
 * transforming it again would double-apply the matrix. dh_DrawPoly/
 * dh_DrawLine (hclip.c's two terminal calls) use this path for vertices
 * that only exist as clip-space interpolation results and have no
 * object-space value to hand the shader otherwise.
 *
 * v3d_my_uniforms/byteswap64/g_default_values_buff -- duplicated here
 * rather than shared; there is no backend header that declares them.
 */

#include "sysinc.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../backend/include/v3d_device.h"
#include "../../backend/include/v3d_context.h"
#include "../../backend/include/v3d_commands.h"
#include "../../backend/include/v3d_clbuf.h"
#include "../../backend/include/v3d_shader_assembler.h"
#include "../../backend/include/v3d_texture.h"
#include "../../backend/hw/v3d_hw.h"
#include "../../backend/hw/v3d_debug.h"
#include <string.h>

/* swap_float32_into (v3d_commands.c) inlined for this file. A draw writes about
 * 9 floats per vertex and 85 per draw into the CL buffers, and the out-of-line
 * call costs a jsr, link/unlk and a stack round trip for each. Same operation:
 * `val` is a float parameter, so the conversion to float happens at the call
 * site exactly as before, and only its bits -- never the swapped value -- are
 * handled afterwards, as integers. v3d_commands.c keeps the function for its
 * own callers.
 *
 * The result goes out with a typed ULONG store: a memcpy into the void*
 * destination makes GCC route every value through a stack temporary, and the
 * destinations are state_buf claims that take one long move. */
static inline void swap_float32_into_inline(void *dst, float val)
{
	ULONG u;

	memcpy(&u, &val, sizeof(u));
	*(ULONG *)dst = LE32(u);
}
#define swap_float32_into(dst, val) swap_float32_into_inline((dst), (val))

static char rcsid[] ="$Id: draw.c,v 1.4 2001/02/01 14:36:49 tfrieden Exp $";

extern void gl_FrameBegin(GLcontext context); /* context.c */
extern void hc_ClipAndDrawPoly(GLcontext context, MGLPolygon *poly, ULONG or_codes); /* hclip.c */
extern void hc_ClipAndDrawLine(GLcontext context, MGLPolygon *poly, ULONG or_codes); /* hclip.c */
extern GLboolean hc_DecideFrontface(GLcontext context, MGLVertex *v0, MGLVertex *v1, MGLVertex *v2); /* hclip.c */
extern void m_CombineMatrices(GLcontext context); /* matrix.c */
extern void m_BuildInverted(GLcontext context); /* matrix.c */

/* gl_FrameBegin returns at once while a pass is open and no split is pending;
 * that test is made here, so the common case costs no call. */
static inline void d_FrameBegin(GLcontext context)
{
	V3DContext* backend = &context->backend;

	if (!backend->frame_active || backend->force_new_pass)
		gl_FrameBegin(context);
}

#define CLIP_EPS (1e-7)

/*
 * A conservative CPU-side pre-cull. It reads .bx/.by/.bw, so it belongs
 * to the CPU clipping path, and nothing calls it.
 *
 * Unlike hc_DecideFrontface (hclip.c), whose winding test loses reliable
 * precision as a primitive's true screen-space area shrinks, this only
 * culls when CONFIDENT the result is numerically trustworthy, via two
 * checks:
 *  - Both edge vectors (a-b, c-b) must have a real, non-tiny length --
 *    catches the exact-degenerate case (two of the three test vertices
 *    coincide) where the winding cross-product's own ratio becomes
 *    ill-conditioned, not just noisy.
 *  - The angle between those edges must not be too close to parallel --
 *    a scale-invariant check (compares r^2, twice the signed area
 *    squared, against a margin scaled by the edges' own squared lengths
 *    -- equivalent to requiring |sin(angle between edges)| above a
 *    threshold, regardless of the triangle's absolute size) that catches
 *    thin-but-non-degenerate triangles the edge-length check alone
 *    wouldn't.
 *
 * Whenever EITHER check fails, this returns GL_TRUE (keep, don't cull) --
 * identical to not having culled that primitive at all, so the GPU's own
 * hardware backface-cull (configured on every draw, gl_EmitCullBlendState)
 * still makes the final, reliable decision for it. Both margins below are
 * deliberately conservative, biased toward "not confident, don't cull".
 */
#define CULL_MIN_EDGE_LENSQ (1e-4f)
#define CULL_MIN_SIN_THETA (0.1f)
static GLboolean d_ConservativeDecideFrontface(GLcontext context, MGLVertex *a, MGLVertex *b, MGLVertex *c)
{
	float a1, a2, b1, b2, r;
	float aw, bw, cw;
	float lenA_sq, lenB_sq;

	aw = 1.0f / a->bw;
	bw = 1.0f / b->bw;
	cw = 1.0f / c->bw;

	a1 = a->bx*aw - b->bx*bw;
	a2 = a->by*aw - b->by*bw;
	b1 = c->bx*cw - b->bx*bw;
	b2 = c->by*cw - b->by*bw;

	lenA_sq = a1*a1 + a2*a2;
	lenB_sq = b1*b1 + b2*b2;

	if (lenA_sq < CULL_MIN_EDGE_LENSQ || lenB_sq < CULL_MIN_EDGE_LENSQ)
	{
		return GL_TRUE; /* an edge is too short to trust -- keep, let hardware decide */
	}

	r = a1*b2 - a2*b1;

	if (r*r < CULL_MIN_SIN_THETA * CULL_MIN_SIN_THETA * lenA_sq * lenB_sq)
	{
		return GL_TRUE; /* too close to parallel/degenerate -- keep, let hardware decide */
	}

	if ((r < 0.0f && context->CurrentCullSign < 0) ||
	    (r > 0.0f && context->CurrentCullSign > 0))
	{
		return GL_FALSE; /* confidently back-facing */
	}
	return GL_TRUE;
}

/* PoC/v3d_assembler.c:41-49, duplicated per this file's own header comment */
static v3d_u64 byteswap64(v3d_u64 x)
{
	v3d_u64 hi_lo_swapped = (x << 32) | (x >> 32);
	v3d_u32 hi = (v3d_u32)(hi_lo_swapped >> 32);
	v3d_u32 lo = (v3d_u32)hi_lo_swapped;
	return ((v3d_u64)LE32(hi) << 32) | LE32(lo);
}

/* Duplicated per this file's header comment. scale_p and scale_p_y are the
 * separate X/Y screen-space scales (see v3d_assembler.c's own comment on the
 * vertex/coordinate shaders' x_p/y_p formulas). Every vertex/coordinate
 * shader variant reads TWO uniforms here (rf10, then a second register) in
 * this exact order -- scale_p_y must stay the 2nd field, right after
 * scale_p, or the M_00.. uniform stream shifts out of sync with what the
 * shaders expect. */
typedef struct v3d_my_uniforms
{
	float scale_p;
	float scale_p_y;
	float M_00, M_10, M_20, M_30;
	float M_01, M_11, M_21, M_31;
	float M_02, M_12, M_22, M_32;
	float M_03, M_13, M_23, M_33;
	/* glDepthRange. APPENDED, deliberately -- uniforms are read
	 * sequentially by the shaders, so anything inserted before the matrix
	 * would be consumed as M_00/M_10 by the COORDINATE shader, which reads
	 * the first 18 and stops. Appending means the coordinate shader (and any
	 * other that does not want them) simply never reads these two, while the
	 * six render-pass vertex shaders that DO read them find them exactly
	 * where their two trailing ldunifrf reads expect. That asymmetry is the
	 * whole reason these go last rather than next to scale_p_y, which was
	 * inserted mid-stream because every shader wanted it. */
	float z_scale, z_offset;
} v3d_my_uniforms;

static float g_default_values_buff[] = { 0.0f, 0.0f, 0.0f, 1.0f };

void d_DrawPoints        (GLcontext);
void d_DrawLines         (GLcontext);
void d_DrawLineStrip     (GLcontext);
void d_DrawLineLoop      (GLcontext);
void d_DrawTriangles     (GLcontext);
void d_DrawTriangleFan   (GLcontext);
void d_DrawTriangleStrip (GLcontext);
void d_DrawQuads         (GLcontext);
void d_DrawQuadStrip     (GLcontext);
void d_DrawTrianglesVA   (GLcontext);
void d_DrawFlat	         (GLcontext);

/* d_DrawMtexPoly/d_DrawSmoothPoly/d_DrawNormalPoly (the original's own
 * GL_POLYGON dispatch, chosen by smooth/texture state at glBegin time) are
 * not ported -- GL_POLYGON reuses d_DrawTriangleFan directly instead
 * (vertexbuffer_min.c's GLBegin), since a convex polygon triangulates
 * identically to a fan from vertex 0, and d_DrawTriangleFan already
 * dispatches smooth/textured/fog/blend purely from GL state via the shared
 * gl_EmitPrimitiveV3D -- the three-way split only existed because Warp3D
 * needed a different vertex-buffer shape per case. d_DrawTrianglesVA (a
 * Surgeon addition upstream: a vertex-array path for unclipped chains of
 * triangles) and d_DrawFlat are declared here but not ported either. */

INLINE GLvoid v_Transform(GLcontext context);

/*
 * GL_TEXTURE_GEN_S/T. Three modes are performed: GL_SPHERE_MAP (spherical
 * environment mapping via a reflection vector), GL_OBJECT_LINEAR and
 * GL_EYE_LINEAR. Any other mode leaves that coordinate exactly as the
 * application supplied it -- texture.c's GLTexGeni records the requested
 * mode without judging it, and the tests below decide what is generated.
 *
 * SIMPLER than the original: that version also computed clip-space
 * position as a side effect, because it REPLACED v_Transform's own
 * position-transform branch entirely (the original's v_Transform made
 * texgen and position-transform mutually exclusive). This fork's
 * v_Transform (below) always does the position transform first, so this
 * function only needs the eye-space intermediate the reflection and
 * eye-linear formulas use, not clip-space too. Not static -- referenced
 * from v_Transform, which vbcc treats as having external linkage (INLINE,
 * not static) and won't let reference a static callee.
 *
 * Eye-space position: ModelView (CurrentMV, context.h's macro for the
 * top of the modelview stack -- NOT context->CombinedMatrix, texgen
 * needs a real eye-space intermediate the clip-space path doesn't
 * keep) applied to `.v.x/y/z/w` -- the RAW OBJECT-SPACE copy GLVertex4f
 * preserves for the vertex shader's own matrix multiply (see
 * vertexbuffer_min.c's own header comment) -- deliberately NOT
 * `.bx/.by/.bz/.bw`, which v_Transform's position-transform loop
 * overwrites with CLIP-space.
 *
 * Eye-space normal: object-space normal (NormalBuffer[vert->normal])
 * times context->InvRot (matrix.c's m_BuildInverted -- the standard
 * inverse-transpose-of-rotation normal transform). Falls back to
 * NormalBuffer[0] if no glNormal3f was ever called
 * (NormalBufferPointer==0), matching the original's own fallback exactly.
 *
 * Reflection vector r = u - 2*(n.u)*n (u = normalized eye-space view
 * vector, pointing from the origin -- i.e. the eye, in eye space -- to
 * the vertex), then s = rx/(2m)+0.5, t = ry/(2m)+0.5, m = sqrt(rx^2+
 * ry^2+(rz+1)^2) -- the standard OpenGL GL_SPHERE_MAP formula,
 * unchanged from the original.
 */
/* GL_SPHERE_MAP keeps this header's own positional value, so a client built
 * against REAL GL headers passes the spec value instead -- both spellings
 * count. See GLTexGeni in texture.c for the whole account. */
#define MGL_SPHERE_MAP_SPEC 0x2402

static int v_IsSphereMap(GLenum mode)
{
	return (mode == GL_SPHERE_MAP || mode == MGL_SPHERE_MAP_SPEC);
}

/* The two linear modes carry their real spec values (gl.h), so one test each. */
static int v_IsObjectLinear(GLenum mode) { return (mode == GL_OBJECT_LINEAR); }
static int v_IsEyeLinear(GLenum mode)    { return (mode == GL_EYE_LINEAR); }

static int v_IsGenerated(GLenum mode)
{
	return (v_IsSphereMap(mode) || v_IsObjectLinear(mode) || v_IsEyeLinear(mode));
}

/* Enabled AND set to a mode this driver actually performs. The gates below
 * use this so a client that asked for object-linear does not pay for the
 * per-vertex reflection maths on its way to not getting one. */
int v_TexGenActive(GLcontext context)
{
	return ((context->TextureGenS_State == GL_TRUE && v_IsGenerated(context->TexGenModeS))
	     || (context->TextureGenT_State == GL_TRUE && v_IsGenerated(context->TexGenModeT)));
}

void v_GenTexCoords(GLcontext context, int vertex)
{
	MGLVertex *vert = &context->VertexBuffer[vertex];
	float ex, ey, ez, ew;
	float ux, uy, uz, ul;
	float nx, ny, nz, nu;
	float rx, ry, rz, m;
	int nbp;
	int sOn = (context->TextureGenS_State == GL_TRUE);
	int tOn = (context->TextureGenT_State == GL_TRUE);

	/* GL_OBJECT_LINEAR first: s = p . (xo,yo,zo,wo), a plain dot product
	 * against the OBJECT coordinates, which .v.x/y/z/w still hold here (the
	 * position transform writes .bx/by/bz/bw and leaves these alone). It
	 * needs none of the eye-space work below, and must not be gated behind
	 * it -- the zero-length early-out further down would otherwise skip a
	 * perfectly well-defined object-linear coordinate. */
	#define OBJDOT(p) ((p)[0]*vert->v.x + (p)[1]*vert->v.y \
	                 + (p)[2]*vert->v.z + (p)[3]*vert->v.w)

	if (sOn && v_IsObjectLinear(context->TexGenModeS))
		vert->v.u0 = OBJDOT(context->ObjectPlaneS);
	if (tOn && v_IsObjectLinear(context->TexGenModeT))
		vert->v.v0 = OBJDOT(context->ObjectPlaneT);

	#undef OBJDOT

	/* Everything past this point needs eye coordinates. */
	if (!((sOn && (v_IsEyeLinear(context->TexGenModeS) || v_IsSphereMap(context->TexGenModeS)))
	   || (tOn && (v_IsEyeLinear(context->TexGenModeT) || v_IsSphereMap(context->TexGenModeT)))))
		return;

	#define a(x) (CurrentMV->v[OF_##x])

	ex = a(11)*vert->v.x + a(12)*vert->v.y + a(13)*vert->v.z + a(14)*vert->v.w;
	ey = a(21)*vert->v.x + a(22)*vert->v.y + a(23)*vert->v.z + a(24)*vert->v.w;
	ez = a(31)*vert->v.x + a(32)*vert->v.y + a(33)*vert->v.z + a(34)*vert->v.w;
	ew = a(41)*vert->v.x + a(42)*vert->v.y + a(43)*vert->v.z + a(44)*vert->v.w;

	#undef a

	/* GL_EYE_LINEAR: s = p' . (xe,ye,ze,we), with p' already carrying the
	 * inverse-modelview transform applied by glTexGenfv at specification
	 * time. Also above the early-out, for the same reason. */
	#define EYEDOT(p) ((p)[0]*ex + (p)[1]*ey + (p)[2]*ez + (p)[3]*ew)

	if (sOn && v_IsEyeLinear(context->TexGenModeS))
		vert->v.u0 = EYEDOT(context->EyePlaneS);
	if (tOn && v_IsEyeLinear(context->TexGenModeT))
		vert->v.v0 = EYEDOT(context->EyePlaneT);

	#undef EYEDOT

	/* Only sphere map is left, and only it needs the reflection vector. */
	if (!((sOn && v_IsSphereMap(context->TexGenModeS))
	   || (tOn && v_IsSphereMap(context->TexGenModeT))))
		return;

	ul = (float)sqrt((double)(ex*ex + ey*ey + ez*ez));
	if (ul == 0.0f)
	{
		D(("v_GenTexCoords: zero-length eye-space vector for vertex %ld, skipping\n", (LONG)vertex));
		return;
	}
	ul = 1.0f / ul;
	ux = ex * ul; uy = ey * ul; uz = ez * ul;

	nbp = (context->NormalBufferPointer > 0) ? (int)vert->normal : 0;

	#define nrm(c) (context->NormalBuffer[nbp].c)
	#define b(x) (context->InvRot[x])

	nx = nrm(x)*b(0) + nrm(y)*b(3) + nrm(z)*b(6);
	ny = nrm(x)*b(1) + nrm(y)*b(4) + nrm(z)*b(7);
	nz = nrm(x)*b(2) + nrm(y)*b(5) + nrm(z)*b(8);

	#undef b
	#undef nrm

	nu = (nx*ux + ny*uy + nz*uz) * 2.0f;

	rx = ux - nx*nu;
	ry = uy - ny*nu;
	rz = uz - nz*nu + 1.0f;

	m = 0.5f / (float)sqrt((double)(rx*rx + ry*ry + rz*rz));

	/* Per coordinate, and only where the mode really is sphere map: a
	 * coordinate asking for anything else keeps the texcoord the application
	 * supplied instead of being overwritten with a reflection. */
	if (context->TextureGenS_State == GL_TRUE && v_IsSphereMap(context->TexGenModeS))
		vert->v.u0 = rx*m + 0.5f;
	if (context->TextureGenT_State == GL_TRUE && v_IsSphereMap(context->TexGenModeT))
		vert->v.v0 = ry*m + 0.5f;
}

/* Out of line on purpose: the generation maths needs FP registers, and
 * inlined into every d_Draw* it made each call save and restore fp2-fp7. */
static __attribute__((noinline)) void v_RunTexGen(GLcontext context)
{
	int i;

	if (context->InvRotValid == GL_FALSE)
	{
		m_BuildInverted(context);
	}

	for (i = 0; i < context->VertexBufferPointer; i++)
	{
		v_GenTexCoords(context, i);
	}
}

/* The transform state every d_Draw* needs before it emits. Nothing calls
 * v_Transform, so this is the only place either job happens:
 * context->CombinedMatrix, recomputed only when GL matrix state changed
 * since the last draw (gl_EmitPrimitiveV3D reads it directly to build the
 * GPU's transform uniform), and the GL_TEXTURE_GEN_S/T generation, run only
 * when texgen is enabled for a mode this driver performs -- so a draw
 * without texgen pays one test. */
INLINE void v_EnsureTransformState(GLcontext context)
{
	if (context->CombinedValid == GL_FALSE)
	{
		m_CombineMatrices(context);
	}

	if (v_TexGenActive(context))
		v_RunTexGen(context);
}

/*
 * A deliberate no-op, not a stub. The original's own PrepTexCoords
 * (MiniGL/src/draw.c) multiplies each vertex's normalized 0..1 s/t by the
 * bound texture's pixel width/height, converting to Warp3D's own
 * texel-space vertex format (`W3D_Vertex.u/v` wants texel coordinates, not
 * normalized UV). V3D's texture-sampling hardware (the TMU) wants
 * NORMALIZED 0..1 coordinates directly, so porting the texel-space
 * multiply here would be actively WRONG for this backend, not just
 * unnecessary -- GLTexCoord2f/4f (vertexbuffer_min.c) already write
 * exactly what gl_EmitPrimitiveV3D's texcoord attribute buffer needs,
 * with no further per-vertex processing required.
 */
INLINE void PrepTexCoords(GLcontext context, int start, const int numverts, GLboolean cullfan)
{
}

/*
 * THE TEXTURE MATRIX (GL_TEXTURE).
 *
 * GL transforms every texture coordinate by the texture matrix, after texgen
 * and before the texture is sampled. This driver applies it HERE -- on the way
 * out of the vertex buffer and into the attribute buffer gl_EmitPrimitiveV3DEx
 * builds -- rather than in glTexCoord2f or in the two array gatherers, for one
 * reason: the vertex buffer is NOT written once per draw. glLockArrays fills it
 * once and any number of glDrawElements calls then read it back
 * (d_DrawTrianglesLocked), so a transform applied in place at fill time would
 * be applied a second and a third time by those draws. The attribute buffer is
 * rebuilt from scratch on every draw, so transforming on the way into it runs
 * exactly once per vertex per draw -- and still picks up a texture matrix that
 * changed between two draws over the same locked range.
 *
 * Only the s,t plane is carried. r is always 0 in this driver (glTexCoord4f
 * takes an r and discards it) and the transform treats q as 1 -- glTexCoord4f's
 * q is kept separately in `.q`, for the per-pixel perspective divide -- so the
 * third column drops out and the fourth is a plain translation. A PROJECTIVE texture matrix -- one with a
 * non-trivial fourth ROW -- is therefore NOT applied: doing that correctly
 * needs a per-pixel divide, which none of the fragment shaders perform.
 *
 * Unit 1's coordinates (ARB multitexture) are left alone. GL gives each texture
 * unit its own texture matrix; GL 1.1 has exactly one, and that is what this
 * is, so it belongs to unit 0.
 */
/*
 * DECLARED volatile AT ITS USE SITE, AND THAT IS NOT DECORATION.
 * Without it GCC parks the six coefficients in fp2..fp7 for the whole of
 * gl_EmitPrimitiveV3DEx, so the function's prologue saves and restores SIX
 * floating-point registers instead of three, on EVERY draw call, whether or not
 * any texture matrix is ever set. volatile forces them to stay in memory: the
 * prologue goes back to exactly what it was before this feature existed, and the
 * only thing it costs is a reload per use on the rare path where a texture matrix
 * really is active.
 *
 * Filling the struct only on the active path, WITHOUT volatile, is the trap: a
 * conditionally defined struct wrecks GCC's register allocation and costs far
 * more than it saves. With volatile it is free.
 */
typedef struct
{
	float a, b, tx;    /* s' = a*s + b*t + tx */
	float c, d, ty;    /* t' = c*s + d*t + ty */
} v_TexMatrix;

static int v_TexMatrixActive(GLcontext context, volatile v_TexMatrix *t)
{
	/* INTEGER compares on the IEEE bit patterns, not float compares, and the
	 * coefficients are not even loaded unless the matrix turns out to be doing
	 * something. Asked once per DRAW. A float version of this test costs six
	 * FPU loads on the path every draw takes, and pulls the coefficients into
	 * registers that gl_EmitPrimitiveV3DEx then has to save and restore on
	 * every single draw call.
	 *
	 * Safe direction: 1.0f is 0x3F800000 and nothing else, +0.0f is 0 and
	 * nothing else. -0.0f (0x80000000) fails the test, so a matrix holding a
	 * negative zero is treated as ACTIVE and transformed -- which produces the
	 * same numbers, just by the slow path. The error can only ever go that way;
	 * a matrix that is doing something can never be mistaken for identity.
	 *
	 * The cast is fine under this project's -fno-strict-aliasing, and Matrix.v
	 * is float[16], so the alignment is already right. */
	const ULONG *b = (const ULONG *)context->Texture[context->TextureNr].v;

	if (b[OF_11] == 0x3F800000UL && b[OF_22] == 0x3F800000UL &&
	    b[OF_12] == 0UL && b[OF_14] == 0UL &&
	    b[OF_21] == 0UL && b[OF_24] == 0UL)
		return 0;

	{
		const float *m = (const float *)b;

		t->a = m[OF_11]; t->b = m[OF_12]; t->tx = m[OF_14];
		t->c = m[OF_21]; t->d = m[OF_22]; t->ty = m[OF_24];
	}
	return 1;
}

/* KEEP THE TERNARY. A "tidier" version that hoists both coordinates into locals
 * under ONE shared `if` costs several times as much extra code, because GCC then
 * spills the float temporaries in all three branches rather than folding each
 * ternary straight into the argument it feeds. */
#define TEXMAT_S(act, tm, v) ((act) ? ((tm).a*(v)->v.u0 + (tm).b*(v)->v.v0 + (tm).tx) : (v)->v.u0)
#define TEXMAT_T(act, tm, v) ((act) ? ((tm).c*(v)->v.u0 + (tm).d*(v)->v.v0 + (tm).ty) : (v)->v.v0)

INLINE GLvoid v_Transform(GLcontext context)
{
	int i;

	/* Position transform -- unconditional, regardless of texgen state,
	 * unlike the original, where the two were mutually exclusive (see
	 * v_GenTexCoords' own comment). */
	if (context->CombinedValid == GL_FALSE)
	{
		m_CombineMatrices(context);
	}

	if (context->WOne_Hint == GL_FALSE)
	{
		#define a(x) (context->CombinedMatrix.v[OF_##x])

		float a11 = a(11);
		float a12 = a(12);
		float a13 = a(13);
		float a14 = a(14);
		float a21 = a(21);
		float a22 = a(22);
		float a23 = a(23);
		float a24 = a(24);
		float a31 = a(31);
		float a32 = a(32);
		float a33 = a(33);
		float a34 = a(34);
		float a41 = a(41);
		float a42 = a(42);
		float a43 = a(43);
		float a44 = a(44);

		MGLVertex *v = &context->VertexBuffer[0];

		i = context->VertexBufferPointer;
		do
		{
			float x = v->bx;
			float y = v->by;
			float z = v->bz;
			float w = v->bw;

			v->bx = a11*x + a12*y + a13*z + a14*w;
			v->by = a21*x + a22*y + a23*z + a24*w;
			v->bz = a31*x + a32*y + a33*z + a34*w;
			v->bw = a41*x + a42*y + a43*z + a44*w;

			v++;
		} while (--i);

		#undef a
	}
	else
	{
		#define a(x) (context->CombinedMatrix.v[OF_##x])

		float a11 = a(11);
		float a12 = a(12);
		float a13 = a(13);
		float a14 = a(14);
		float a21 = a(21);
		float a22 = a(22);
		float a23 = a(23);
		float a24 = a(24);
		float a31 = a(31);
		float a32 = a(32);
		float a33 = a(33);
		float a34 = a(34);
		float a41 = a(41);
		float a42 = a(42);
		float a43 = a(43);
		float a44 = a(44);

		MGLVertex *v = &context->VertexBuffer[0];

		i = context->VertexBufferPointer;

		do
		{
			float x = v->bx;
			float y = v->by;
			float z = v->bz;

			v->bx = a11*x + a12*y + a13*z + a14;
			v->by = a21*x + a22*y + a23*z + a24;
			v->bz = a31*x + a32*y + a33*z + a34;
			v->bw = a41*x + a42*y + a43*z + a44;

			v++;
		} while (--i);

		#undef a
	}

	/* GL_TEXTURE_GEN_S/T -- see v_GenTexCoords' own comment for the full
	 * account. Runs AFTER the position transform above, reading the
	 * object-space `.v.x/y/z/w` copy (untouched by that transform, which
	 * only writes `.bx/by/bz/bw`). */
	if (v_TexGenActive(context))
	{
		if (context->InvRotValid == GL_FALSE)
			m_BuildInverted(context);

		for (i = 0; i < context->VertexBufferPointer; i++)
		{
			v_GenTexCoords(context, i);
		}
	}
}

/* Multitexture buffer. The original's bodies allocate a literal
 * W3D_VAVertex-typed buffer (Warp3D vertex-array-hardware-specific), which
 * this backend has no use for, so both are empty here. AllocMtex returns
 * GL_TRUE so MGLInitContext's error check (context.c) doesn't fail context
 * creation over a buffer that is never allocated. */
GLboolean AllocMtex(int size)
{
	return GL_TRUE;
}

void FreeMtex(void)
{
}

/* dh_DrawPoly/dh_DrawLine (hclip.c's two terminal calls for anything that
 * needs real clipping) are implemented further down, right after
 * gl_EmitPrimitiveV3D -- they call it directly (with use_clip_space=
 * GL_TRUE), so they need to come after its definition in this file. */

/* Lazily assembles and uploads every shader variant's machine code, once
 * per context lifetime (backend->shaders_ready) -- a context that only ever
 * calls GLClear never needs this, so it doesn't belong in v3d_context_init.
 * The code memory is one allocation of fixed 1024-byte slots, one slot per
 * variant, in the order the offsets below give. Adding a variant means
 * editing the slot count here as well: see v3d_shader_assembler.h's own
 * enum comment for the full lockstep list. */
static int gl_EnsureShaders(GLcontext context)
{
	V3DContext* backend = &context->backend;
	v3d_u64* shader_vex;
	v3d_u64* shader_coord;
	v3d_u64* shader_frag;
	v3d_u64* shader_frag_textured;
	v3d_u64* shader_vex_smooth;
	v3d_u64* shader_frag_smooth;
	v3d_u64* shader_vex_smooth_textured;
	v3d_u64* shader_frag_textured_smooth;
	v3d_u64* shader_frag_untextured_fog;
	v3d_u64* shader_frag_untextured_alphatest;
	v3d_u64* shader_frag_textured_fog;
	v3d_u64* shader_frag_textured_alphatest;
	v3d_u64* shader_vex_multitexture;
	v3d_u64* shader_frag_multitexture;
	v3d_u64* shader_frag_untextured_blend;
	v3d_u64* shader_frag_untextured_blend_add;
	v3d_u64* shader_frag_untextured_smooth_blend_add;
	v3d_u64* shader_frag_untextured_smooth_blend;
	v3d_u64* shader_frag_textured_blend;
	v3d_u64* shader_frag_textured_smooth_blend;
	v3d_u64* shader_frag_textured_padded_flat_test;
	v3d_u64* shader_coord_clipspace;
	v3d_u64* shader_vex_smooth_textured_clipspace;
	v3d_u64* shader_vex_multitexture_clipspace;
	v3d_u64* shader_frag_multitexture_decal;
	v3d_u64* shader_frag_multitexture_replace;
	v3d_u64* shader_frag_multitexture_modulate_blend;
	v3d_u64* shader_frag_multitexture_decal_blend;
	v3d_u64* shader_frag_multitexture_replace_blend;
	v3d_u64* shader_frag_untextured_alphatest_greater;
	v3d_u64* shader_frag_textured_alphatest_greater;
	v3d_u64* shader_frag_textured_smooth_alphatest_greater;
	v3d_u64* shader_frag_textured_smooth_alphatest;
	v3d_u64* shader_frag_multitexture_modulate_translucent;
	v3d_u64* shader_frag_textured_colormod;
	v3d_u64* shader_frag_textured_smooth_blend_add;
	v3d_u64* shader_frag_textured_smooth_dstcolor_zero;
	v3d_u64* shader_frag_textured_smooth_dstcolor_one;
	v3d_u64* shader_frag_textured_smooth_dstcolor_srccolor;
	v3d_u64* shader_frag_textured_smooth_dstcolor_invdstalpha;
	v3d_u64* shader_frag_textured_smooth_zero_invsrccolor;
	v3d_u64* shader_frag_textured_smooth_dstcolor_srcalpha;
	v3d_u64* shader_frag_textured_smooth_one_invsrcalpha;
	v3d_u64* shader_frag_textured_smooth_invsrcalpha_srcalpha;
	v3d_u64* shader_frag_untextured_blend_srcalpha_one;
	v3d_u64* shader_frag_untextured_smooth_blend_srcalpha_one;
	v3d_u64* shader_frag_textured_smooth_blend_srcalpha_one;
	/* The remaining alpha_func variants: 5 funcs x 3 shapes. */
	v3d_u64* shader_frag_untextured_alphatest_never;
	v3d_u64* shader_frag_textured_alphatest_never;
	v3d_u64* shader_frag_textured_smooth_alphatest_never;
	v3d_u64* shader_frag_untextured_alphatest_less;
	v3d_u64* shader_frag_textured_alphatest_less;
	v3d_u64* shader_frag_textured_smooth_alphatest_less;
	v3d_u64* shader_frag_untextured_alphatest_equal;
	v3d_u64* shader_frag_textured_alphatest_equal;
	v3d_u64* shader_frag_textured_smooth_alphatest_equal;
	v3d_u64* shader_frag_untextured_alphatest_lequal;
	v3d_u64* shader_frag_textured_alphatest_lequal;
	v3d_u64* shader_frag_textured_smooth_alphatest_lequal;
	v3d_u64* shader_frag_untextured_alphatest_notequal;
	v3d_u64* shader_frag_textured_alphatest_notequal;
	v3d_u64* shader_frag_textured_smooth_alphatest_notequal;
	v3d_u64* shader_frag_untextured_fog_alphatest;
	v3d_u64* shader_frag_textured_fog_alphatest;
	v3d_u64* shader_frag_untextured_fog_alphatest_greater;
	v3d_u64* shader_frag_textured_fog_alphatest_greater;
	v3d_u64* shader_frag_untextured_fog_alphatest_less;
	v3d_u64* shader_frag_textured_fog_alphatest_less;
	v3d_u64* shader_frag_untextured_fog_alphatest_equal;
	v3d_u64* shader_frag_textured_fog_alphatest_equal;
	v3d_u64* shader_frag_untextured_fog_alphatest_lequal;
	v3d_u64* shader_frag_textured_fog_alphatest_lequal;
	v3d_u64* shader_frag_untextured_fog_alphatest_notequal;
	v3d_u64* shader_frag_textured_fog_alphatest_notequal;
	v3d_u64* shader_frag_untextured_fog_alphatest_never;
	v3d_u64* shader_frag_textured_fog_alphatest_never;
	v3d_u64* shader_frag_untextured_smooth_fog;
	v3d_u64* shader_frag_textured_smooth_fog;
	v3d_u64* shader_frag_textured_smooth_fog_alphatest;
	v3d_u64* shader_frag_textured_smooth_fog_alphatest_greater;
	v3d_u64* shader_frag_textured_smooth_fog_alphatest_less;
	v3d_u64* shader_frag_textured_smooth_fog_alphatest_equal;
	v3d_u64* shader_frag_textured_smooth_fog_alphatest_lequal;
	v3d_u64* shader_frag_textured_smooth_fog_alphatest_notequal;
	v3d_u64* shader_frag_textured_smooth_fog_alphatest_never;
	v3d_u64* shader_frag_multitexture_fog;
	v3d_u64* shader_frag_multitexture_decal_fog;
	v3d_u64* shader_frag_multitexture_replace_fog;
	v3d_u64* shader_frag_textured_blend_fog;
	v3d_u64* shader_frag_textured_smooth_blend_fog;
	v3d_u64* shader_frag_textured_smooth_dstcolor_zero_fog;
	v3d_u64* shader_frag_textured_smooth_dstcolor_one_fog;
	v3d_u64* shader_frag_textured_smooth_dstcolor_srccolor_fog;
	v3d_u64* shader_frag_textured_smooth_dstcolor_invdstalpha_fog;
	v3d_u64* shader_frag_textured_smooth_zero_invsrccolor_fog;
	v3d_u64* shader_frag_textured_smooth_dstcolor_srcalpha_fog;
	v3d_u64* shader_frag_textured_smooth_one_invsrcalpha_fog;
	v3d_u64* shader_frag_textured_smooth_invsrcalpha_srcalpha_fog;
	v3d_u64* shader_frag_multitexture_modulate_translucent_fog;
	v3d_u64* shader_frag_multitexture_modulate_blend_fog;
	v3d_u64* shader_frag_multitexture_decal_blend_fog;
	v3d_u64* shader_frag_multitexture_replace_blend_fog;
	v3d_u64* shader_frag_untextured_smooth_blend_fog;
	v3d_u64* shader_frag_untextured_smooth_blend_add_fog;
	v3d_u64* shader_frag_untextured_smooth_blend_srcalpha_one_fog;
	v3d_u64* shader_frag_textured_smooth_blend_add_fog;
	v3d_u64* shader_frag_textured_smooth_blend_srcalpha_one_fog;
	v3d_u64* shader_frag_untextured_blend_fog;
	v3d_u64* shader_frag_untextured_blend_add_fog;
	v3d_u64* shader_frag_untextured_blend_srcalpha_one_fog;
	v3d_u64* shader_frag_untextured_point_smooth;
	v3d_u64* shader_frag_untextured_smooth_point_smooth;
	v3d_u64* shader_frag_textured_point_smooth;
	v3d_u64* shader_frag_textured_smooth_point_smooth;
	V3DAssembledShader* vex;
	V3DAssembledShader* coord;
	V3DAssembledShader* frag;
	V3DAssembledShader* frag_textured;
	V3DAssembledShader* vex_smooth;
	V3DAssembledShader* frag_smooth;
	V3DAssembledShader* vex_smooth_textured;
	V3DAssembledShader* frag_textured_smooth;
	V3DAssembledShader* frag_untextured_fog;
	V3DAssembledShader* frag_untextured_alphatest;
	V3DAssembledShader* frag_textured_fog;
	V3DAssembledShader* frag_textured_alphatest;
	V3DAssembledShader* vex_multitexture;
	V3DAssembledShader* frag_multitexture;
	V3DAssembledShader* frag_untextured_blend;
	V3DAssembledShader* frag_untextured_blend_add;
	V3DAssembledShader* frag_untextured_smooth_blend_add;
	V3DAssembledShader* frag_untextured_smooth_blend;
	V3DAssembledShader* frag_textured_blend;
	V3DAssembledShader* frag_textured_smooth_blend;
	V3DAssembledShader* frag_textured_padded_flat_test;
	V3DAssembledShader* coord_clipspace;
	V3DAssembledShader* vex_smooth_textured_clipspace;
	V3DAssembledShader* vex_multitexture_clipspace;
	V3DAssembledShader* frag_multitexture_decal;
	V3DAssembledShader* frag_multitexture_replace;
	V3DAssembledShader* frag_multitexture_modulate_blend;
	V3DAssembledShader* frag_multitexture_decal_blend;
	V3DAssembledShader* frag_multitexture_replace_blend;
	V3DAssembledShader* frag_untextured_alphatest_greater;
	V3DAssembledShader* frag_textured_alphatest_greater;
	V3DAssembledShader* frag_textured_smooth_alphatest_greater;
	V3DAssembledShader* frag_textured_smooth_alphatest;
	V3DAssembledShader* frag_multitexture_modulate_translucent;
	V3DAssembledShader* frag_textured_colormod;
	V3DAssembledShader* frag_textured_smooth_blend_add;
	V3DAssembledShader* frag_textured_smooth_dstcolor_zero;
	V3DAssembledShader* frag_textured_smooth_dstcolor_one;
	V3DAssembledShader* frag_textured_smooth_dstcolor_srccolor;
	V3DAssembledShader* frag_textured_smooth_dstcolor_invdstalpha;
	V3DAssembledShader* frag_textured_smooth_zero_invsrccolor;
	V3DAssembledShader* frag_textured_smooth_dstcolor_srcalpha;
	V3DAssembledShader* frag_textured_smooth_one_invsrcalpha;
	V3DAssembledShader* frag_textured_smooth_invsrcalpha_srcalpha;
	V3DAssembledShader* frag_untextured_blend_srcalpha_one;
	V3DAssembledShader* frag_untextured_smooth_blend_srcalpha_one;
	V3DAssembledShader* frag_textured_smooth_blend_srcalpha_one;
	/* The remaining alpha_func variants: 5 funcs x 3 shapes. */
	V3DAssembledShader* frag_untextured_alphatest_never;
	V3DAssembledShader* frag_textured_alphatest_never;
	V3DAssembledShader* frag_textured_smooth_alphatest_never;
	V3DAssembledShader* frag_untextured_alphatest_less;
	V3DAssembledShader* frag_textured_alphatest_less;
	V3DAssembledShader* frag_textured_smooth_alphatest_less;
	V3DAssembledShader* frag_untextured_alphatest_equal;
	V3DAssembledShader* frag_textured_alphatest_equal;
	V3DAssembledShader* frag_textured_smooth_alphatest_equal;
	V3DAssembledShader* frag_untextured_alphatest_lequal;
	V3DAssembledShader* frag_textured_alphatest_lequal;
	V3DAssembledShader* frag_textured_smooth_alphatest_lequal;
	V3DAssembledShader* frag_untextured_alphatest_notequal;
	V3DAssembledShader* frag_textured_alphatest_notequal;
	V3DAssembledShader* frag_textured_smooth_alphatest_notequal;
	V3DAssembledShader* frag_untextured_fog_alphatest;
	V3DAssembledShader* frag_textured_fog_alphatest;
	V3DAssembledShader* frag_untextured_fog_alphatest_greater;
	V3DAssembledShader* frag_textured_fog_alphatest_greater;
	V3DAssembledShader* frag_untextured_fog_alphatest_less;
	V3DAssembledShader* frag_textured_fog_alphatest_less;
	V3DAssembledShader* frag_untextured_fog_alphatest_equal;
	V3DAssembledShader* frag_textured_fog_alphatest_equal;
	V3DAssembledShader* frag_untextured_fog_alphatest_lequal;
	V3DAssembledShader* frag_textured_fog_alphatest_lequal;
	V3DAssembledShader* frag_untextured_fog_alphatest_notequal;
	V3DAssembledShader* frag_textured_fog_alphatest_notequal;
	V3DAssembledShader* frag_untextured_fog_alphatest_never;
	V3DAssembledShader* frag_textured_fog_alphatest_never;
	V3DAssembledShader* frag_untextured_smooth_fog;
	V3DAssembledShader* frag_textured_smooth_fog;
	V3DAssembledShader* frag_textured_smooth_fog_alphatest;
	V3DAssembledShader* frag_textured_smooth_fog_alphatest_greater;
	V3DAssembledShader* frag_textured_smooth_fog_alphatest_less;
	V3DAssembledShader* frag_textured_smooth_fog_alphatest_equal;
	V3DAssembledShader* frag_textured_smooth_fog_alphatest_lequal;
	V3DAssembledShader* frag_textured_smooth_fog_alphatest_notequal;
	V3DAssembledShader* frag_textured_smooth_fog_alphatest_never;
	V3DAssembledShader* frag_multitexture_fog;
	V3DAssembledShader* frag_multitexture_decal_fog;
	V3DAssembledShader* frag_multitexture_replace_fog;
	V3DAssembledShader* frag_textured_blend_fog;
	V3DAssembledShader* frag_textured_smooth_blend_fog;
	V3DAssembledShader* frag_textured_smooth_dstcolor_zero_fog;
	V3DAssembledShader* frag_textured_smooth_dstcolor_one_fog;
	V3DAssembledShader* frag_textured_smooth_dstcolor_srccolor_fog;
	V3DAssembledShader* frag_textured_smooth_dstcolor_invdstalpha_fog;
	V3DAssembledShader* frag_textured_smooth_zero_invsrccolor_fog;
	V3DAssembledShader* frag_textured_smooth_dstcolor_srcalpha_fog;
	V3DAssembledShader* frag_textured_smooth_one_invsrcalpha_fog;
	V3DAssembledShader* frag_textured_smooth_invsrcalpha_srcalpha_fog;
	V3DAssembledShader* frag_multitexture_modulate_translucent_fog;
	V3DAssembledShader* frag_multitexture_modulate_blend_fog;
	V3DAssembledShader* frag_multitexture_decal_blend_fog;
	V3DAssembledShader* frag_multitexture_replace_blend_fog;
	V3DAssembledShader* frag_untextured_smooth_blend_fog;
	V3DAssembledShader* frag_untextured_smooth_blend_add_fog;
	V3DAssembledShader* frag_untextured_smooth_blend_srcalpha_one_fog;
	V3DAssembledShader* frag_textured_smooth_blend_add_fog;
	V3DAssembledShader* frag_textured_smooth_blend_srcalpha_one_fog;
	V3DAssembledShader* frag_untextured_blend_fog;
	V3DAssembledShader* frag_untextured_blend_add_fog;
	V3DAssembledShader* frag_untextured_blend_srcalpha_one_fog;
	V3DAssembledShader* frag_untextured_point_smooth;
	V3DAssembledShader* frag_untextured_smooth_point_smooth;
	V3DAssembledShader* frag_textured_point_smooth;
	V3DAssembledShader* frag_textured_smooth_point_smooth;
	int i;

	if (backend->shaders_ready)
		return 0;

	D(("gl_EnsureShaders: assembling and validating shaders\n"));

	if (!v3d_assemble_builtin_shaders(&context->device))
	{
		D(("gl_EnsureShaders: v3d_assemble_builtin_shaders failed\n"));
		return -1;
	}

	/* Every variant stays resident, so gl_EmitPrimitiveV3D can pick which
	 * vertex+fragment pair to point the shader state record at per draw
	 * call. The buffer is exactly full: the last slot ends precisely at the
	 * end of the allocation, so a new variant without a bump here writes
	 * past the end of shader_code_mem, over whatever V3D memory follows. */
	if (v3d_mem_alloc(&context->device, &backend->shader_code_mem, 114 * 1024) < 0)
	{
		D(("gl_EnsureShaders: v3d_mem_alloc failed\n"));
		return -2;
	}
	/* No explicit memset here -- v3d_mem_alloc's own AllocVec call already
	 * uses MEMF_CLEAR, so this memory arrives pre-zeroed. */

	shader_vex                       = (v3d_u64*)backend->shader_code_mem.hostptr;
	shader_coord                     = (v3d_u64*)((ULONG)shader_vex + 1024);
	shader_frag                      = (v3d_u64*)((ULONG)shader_vex + 2048);
	shader_frag_textured             = (v3d_u64*)((ULONG)shader_vex + 3072);
	shader_vex_smooth                = (v3d_u64*)((ULONG)shader_vex + 4096);
	shader_frag_smooth               = (v3d_u64*)((ULONG)shader_vex + 5120);
	shader_vex_smooth_textured       = (v3d_u64*)((ULONG)shader_vex + 6144);
	shader_frag_textured_smooth      = (v3d_u64*)((ULONG)shader_vex + 7168);
	shader_frag_untextured_fog       = (v3d_u64*)((ULONG)shader_vex + 8192);
	shader_frag_untextured_alphatest = (v3d_u64*)((ULONG)shader_vex + 9216);
	shader_frag_textured_fog         = (v3d_u64*)((ULONG)shader_vex + 10240);
	shader_frag_textured_alphatest   = (v3d_u64*)((ULONG)shader_vex + 11264);
	shader_vex_multitexture          = (v3d_u64*)((ULONG)shader_vex + 12288);
	shader_frag_multitexture         = (v3d_u64*)((ULONG)shader_vex + 13312);
	shader_frag_untextured_blend     = (v3d_u64*)((ULONG)shader_vex + 14336);
	shader_coord_clipspace                = (v3d_u64*)((ULONG)shader_vex + 15360);
	shader_vex_smooth_textured_clipspace  = (v3d_u64*)((ULONG)shader_vex + 16384);
	shader_vex_multitexture_clipspace     = (v3d_u64*)((ULONG)shader_vex + 17408);
	shader_frag_multitexture_decal           = (v3d_u64*)((ULONG)shader_vex + 18432);
	shader_frag_multitexture_replace         = (v3d_u64*)((ULONG)shader_vex + 19456);
	shader_frag_multitexture_modulate_blend  = (v3d_u64*)((ULONG)shader_vex + 20480);
	shader_frag_multitexture_decal_blend     = (v3d_u64*)((ULONG)shader_vex + 21504);
	shader_frag_multitexture_replace_blend   = (v3d_u64*)((ULONG)shader_vex + 22528);
	shader_frag_untextured_alphatest_greater = (v3d_u64*)((ULONG)shader_vex + 23552);
	shader_frag_textured_alphatest_greater   = (v3d_u64*)((ULONG)shader_vex + 24576);
	shader_frag_textured_smooth_alphatest_greater = (v3d_u64*)((ULONG)shader_vex + 43008);
	shader_frag_textured_smooth_alphatest = (v3d_u64*)((ULONG)shader_vex + 44032);
	shader_frag_untextured_blend_add         = (v3d_u64*)((ULONG)shader_vex + 25600);
	shader_frag_untextured_smooth_blend_add  = (v3d_u64*)((ULONG)shader_vex + 26624);
	shader_frag_untextured_smooth_blend      = (v3d_u64*)((ULONG)shader_vex + 27648);
	shader_frag_textured_blend               = (v3d_u64*)((ULONG)shader_vex + 28672);
	shader_frag_textured_smooth_blend        = (v3d_u64*)((ULONG)shader_vex + 29696);
	shader_frag_textured_padded_flat_test    = (v3d_u64*)((ULONG)shader_vex + 30720);
	shader_frag_multitexture_modulate_translucent = (v3d_u64*)((ULONG)shader_vex + 31744);
	shader_frag_textured_colormod                 = (v3d_u64*)((ULONG)shader_vex + 32768);
	shader_frag_textured_smooth_blend_add         = (v3d_u64*)((ULONG)shader_vex + 33792);
	shader_frag_textured_smooth_dstcolor_zero         = (v3d_u64*)((ULONG)shader_vex + 34816);
	shader_frag_textured_smooth_dstcolor_one          = (v3d_u64*)((ULONG)shader_vex + 35840);
	shader_frag_textured_smooth_dstcolor_srccolor     = (v3d_u64*)((ULONG)shader_vex + 36864);
	shader_frag_textured_smooth_dstcolor_invdstalpha  = (v3d_u64*)((ULONG)shader_vex + 37888);
	shader_frag_textured_smooth_zero_invsrccolor      = (v3d_u64*)((ULONG)shader_vex + 38912);
	shader_frag_textured_smooth_dstcolor_srcalpha     = (v3d_u64*)((ULONG)shader_vex + 39936);
	shader_frag_textured_smooth_one_invsrcalpha       = (v3d_u64*)((ULONG)shader_vex + 40960);
	shader_frag_textured_smooth_invsrcalpha_srcalpha  = (v3d_u64*)((ULONG)shader_vex + 41984);
	/* The SRC_ALPHA/ONE blend variants, slots 44..46. */
	shader_frag_untextured_blend_srcalpha_one         = (v3d_u64*)((ULONG)shader_vex + 45056);
	shader_frag_untextured_smooth_blend_srcalpha_one  = (v3d_u64*)((ULONG)shader_vex + 46080);
	shader_frag_textured_smooth_blend_srcalpha_one    = (v3d_u64*)((ULONG)shader_vex + 47104);
	/* The remaining alpha_func variants: 15 slots, 47..61. */
	shader_frag_untextured_alphatest_never            = (v3d_u64*)((ULONG)shader_vex + 48128);
	shader_frag_textured_alphatest_never              = (v3d_u64*)((ULONG)shader_vex + 49152);
	shader_frag_textured_smooth_alphatest_never       = (v3d_u64*)((ULONG)shader_vex + 50176);
	shader_frag_untextured_alphatest_less             = (v3d_u64*)((ULONG)shader_vex + 51200);
	shader_frag_textured_alphatest_less               = (v3d_u64*)((ULONG)shader_vex + 52224);
	shader_frag_textured_smooth_alphatest_less        = (v3d_u64*)((ULONG)shader_vex + 53248);
	shader_frag_untextured_alphatest_equal            = (v3d_u64*)((ULONG)shader_vex + 54272);
	shader_frag_textured_alphatest_equal              = (v3d_u64*)((ULONG)shader_vex + 55296);
	shader_frag_textured_smooth_alphatest_equal       = (v3d_u64*)((ULONG)shader_vex + 56320);
	shader_frag_untextured_alphatest_lequal           = (v3d_u64*)((ULONG)shader_vex + 57344);
	shader_frag_textured_alphatest_lequal             = (v3d_u64*)((ULONG)shader_vex + 58368);
	shader_frag_textured_smooth_alphatest_lequal      = (v3d_u64*)((ULONG)shader_vex + 59392);
	shader_frag_untextured_alphatest_notequal         = (v3d_u64*)((ULONG)shader_vex + 60416);
	shader_frag_textured_alphatest_notequal           = (v3d_u64*)((ULONG)shader_vex + 61440);
	shader_frag_textured_smooth_alphatest_notequal    = (v3d_u64*)((ULONG)shader_vex + 62464);
	/* Combined fog + alpha test, slots 62..75 -- see
	 * v3d_shader_assembler.h's enum comment for the lockstep rules. */
	shader_frag_untextured_fog_alphatest = (v3d_u64*)((ULONG)shader_vex + 63488);
	shader_frag_textured_fog_alphatest = (v3d_u64*)((ULONG)shader_vex + 64512);
	shader_frag_untextured_fog_alphatest_greater = (v3d_u64*)((ULONG)shader_vex + 65536);
	shader_frag_textured_fog_alphatest_greater = (v3d_u64*)((ULONG)shader_vex + 66560);
	shader_frag_untextured_fog_alphatest_less = (v3d_u64*)((ULONG)shader_vex + 67584);
	shader_frag_textured_fog_alphatest_less = (v3d_u64*)((ULONG)shader_vex + 68608);
	shader_frag_untextured_fog_alphatest_equal = (v3d_u64*)((ULONG)shader_vex + 69632);
	shader_frag_textured_fog_alphatest_equal = (v3d_u64*)((ULONG)shader_vex + 70656);
	shader_frag_untextured_fog_alphatest_lequal = (v3d_u64*)((ULONG)shader_vex + 71680);
	shader_frag_textured_fog_alphatest_lequal = (v3d_u64*)((ULONG)shader_vex + 72704);
	shader_frag_untextured_fog_alphatest_notequal = (v3d_u64*)((ULONG)shader_vex + 73728);
	shader_frag_textured_fog_alphatest_notequal = (v3d_u64*)((ULONG)shader_vex + 74752);
	shader_frag_untextured_fog_alphatest_never = (v3d_u64*)((ULONG)shader_vex + 75776);
	shader_frag_textured_fog_alphatest_never = (v3d_u64*)((ULONG)shader_vex + 76800);
	/* Smooth fog, slots 76..84 -- see
	 * v3d_shader_assembler.h's enum comment for the lockstep rules. */
	shader_frag_untextured_smooth_fog = (v3d_u64*)((ULONG)shader_vex + 77824);
	shader_frag_textured_smooth_fog = (v3d_u64*)((ULONG)shader_vex + 78848);
	shader_frag_textured_smooth_fog_alphatest = (v3d_u64*)((ULONG)shader_vex + 79872);
	shader_frag_textured_smooth_fog_alphatest_greater = (v3d_u64*)((ULONG)shader_vex + 80896);
	shader_frag_textured_smooth_fog_alphatest_less = (v3d_u64*)((ULONG)shader_vex + 81920);
	shader_frag_textured_smooth_fog_alphatest_equal = (v3d_u64*)((ULONG)shader_vex + 82944);
	shader_frag_textured_smooth_fog_alphatest_lequal = (v3d_u64*)((ULONG)shader_vex + 83968);
	shader_frag_textured_smooth_fog_alphatest_notequal = (v3d_u64*)((ULONG)shader_vex + 84992);
	shader_frag_textured_smooth_fog_alphatest_never = (v3d_u64*)((ULONG)shader_vex + 86016);
	/* Multitexture fog, slots 85..87. */
	shader_frag_multitexture_fog = (v3d_u64*)((ULONG)shader_vex + 87040);
	shader_frag_multitexture_decal_fog = (v3d_u64*)((ULONG)shader_vex + 88064);
	shader_frag_multitexture_replace_fog = (v3d_u64*)((ULONG)shader_vex + 89088);
	/* Software-blend fog, slots 88..101. */
	shader_frag_textured_blend_fog = (v3d_u64*)((ULONG)shader_vex + 90112);
	shader_frag_textured_smooth_blend_fog = (v3d_u64*)((ULONG)shader_vex + 91136);
	shader_frag_textured_smooth_dstcolor_zero_fog = (v3d_u64*)((ULONG)shader_vex + 92160);
	shader_frag_textured_smooth_dstcolor_one_fog = (v3d_u64*)((ULONG)shader_vex + 93184);
	shader_frag_textured_smooth_dstcolor_srccolor_fog = (v3d_u64*)((ULONG)shader_vex + 94208);
	shader_frag_textured_smooth_dstcolor_invdstalpha_fog = (v3d_u64*)((ULONG)shader_vex + 95232);
	shader_frag_textured_smooth_zero_invsrccolor_fog = (v3d_u64*)((ULONG)shader_vex + 96256);
	shader_frag_textured_smooth_dstcolor_srcalpha_fog = (v3d_u64*)((ULONG)shader_vex + 97280);
	shader_frag_textured_smooth_one_invsrcalpha_fog = (v3d_u64*)((ULONG)shader_vex + 98304);
	shader_frag_textured_smooth_invsrcalpha_srcalpha_fog = (v3d_u64*)((ULONG)shader_vex + 99328);
	shader_frag_multitexture_modulate_translucent_fog = (v3d_u64*)((ULONG)shader_vex + 100352);
	shader_frag_multitexture_modulate_blend_fog = (v3d_u64*)((ULONG)shader_vex + 101376);
	shader_frag_multitexture_decal_blend_fog = (v3d_u64*)((ULONG)shader_vex + 102400);
	shader_frag_multitexture_replace_blend_fog = (v3d_u64*)((ULONG)shader_vex + 103424);
	/* Register-constrained blend fog, slots 102..106. */
	shader_frag_untextured_smooth_blend_fog = (v3d_u64*)((ULONG)shader_vex + 104448);
	shader_frag_untextured_smooth_blend_add_fog = (v3d_u64*)((ULONG)shader_vex + 105472);
	shader_frag_untextured_smooth_blend_srcalpha_one_fog = (v3d_u64*)((ULONG)shader_vex + 106496);
	shader_frag_textured_smooth_blend_add_fog = (v3d_u64*)((ULONG)shader_vex + 107520);
	shader_frag_textured_smooth_blend_srcalpha_one_fog = (v3d_u64*)((ULONG)shader_vex + 108544);
	/* Untextured flat blend fog, slots 107..109. */
	shader_frag_untextured_blend_fog = (v3d_u64*)((ULONG)shader_vex + 109568);
	shader_frag_untextured_blend_add_fog = (v3d_u64*)((ULONG)shader_vex + 110592);
	shader_frag_untextured_blend_srcalpha_one_fog = (v3d_u64*)((ULONG)shader_vex + 111616);
	/* Smooth points, slots 110..113. */
	shader_frag_untextured_point_smooth        = (v3d_u64*)((ULONG)shader_vex + 112640);
	shader_frag_untextured_smooth_point_smooth = (v3d_u64*)((ULONG)shader_vex + 113664);
	shader_frag_textured_point_smooth          = (v3d_u64*)((ULONG)shader_vex + 114688);
	shader_frag_textured_smooth_point_smooth   = (v3d_u64*)((ULONG)shader_vex + 115712);

	vex                       = &v3d_shader_variants[V3D_SHADER_VARIANT_VERTEX_TEXTURED];
	coord                     = &v3d_shader_variants[V3D_SHADER_VARIANT_COORDINATE_TEXTURED];
	frag                      = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED];
	frag_textured             = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED];
	vex_smooth                = &v3d_shader_variants[V3D_SHADER_VARIANT_VERTEX_SMOOTH];
	frag_smooth               = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH];
	vex_smooth_textured       = &v3d_shader_variants[V3D_SHADER_VARIANT_VERTEX_SMOOTH_TEXTURED];
	frag_textured_smooth      = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH];
	frag_untextured_fog       = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG];
	frag_untextured_alphatest = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST];
	frag_textured_fog         = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG];
	frag_textured_alphatest   = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST];
	vex_multitexture          = &v3d_shader_variants[V3D_SHADER_VARIANT_VERTEX_MULTITEXTURE];
	frag_multitexture         = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE];
	frag_untextured_blend     = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_BLEND];
	frag_untextured_blend_add = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_BLEND_ADD];
	frag_untextured_smooth_blend_add = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_BLEND_ADD];
	frag_untextured_smooth_blend = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_BLEND];
	frag_textured_blend = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_BLEND];
	frag_textured_smooth_blend = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_BLEND];
	frag_textured_padded_flat_test = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_PADDED_FLAT_TEST];
	coord_clipspace                = &v3d_shader_variants[V3D_SHADER_VARIANT_COORDINATE_CLIPSPACE];
	vex_smooth_textured_clipspace  = &v3d_shader_variants[V3D_SHADER_VARIANT_VERTEX_SMOOTH_TEXTURED_CLIPSPACE];
	vex_multitexture_clipspace     = &v3d_shader_variants[V3D_SHADER_VARIANT_VERTEX_MULTITEXTURE_CLIPSPACE];
	frag_multitexture_decal          = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_DECAL];
	frag_multitexture_replace        = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_REPLACE];
	frag_multitexture_modulate_blend = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_MODULATE_BLEND];
	frag_multitexture_decal_blend    = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_DECAL_BLEND];
	frag_multitexture_replace_blend  = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_REPLACE_BLEND];
	frag_untextured_alphatest_greater = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST_GREATER];
	frag_textured_alphatest_greater   = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST_GREATER];
	frag_textured_smooth_alphatest_greater = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST_GREATER];
	frag_textured_smooth_alphatest = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST];
	frag_multitexture_modulate_translucent = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_MODULATE_TRANSLUCENT];
	frag_textured_colormod = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_COLORMOD];
	frag_textured_smooth_blend_add = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_BLEND_ADD];
	frag_textured_smooth_dstcolor_zero = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_ZERO];
	frag_textured_smooth_dstcolor_one = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_ONE];
	frag_textured_smooth_dstcolor_srccolor = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_SRCCOLOR];
	frag_textured_smooth_dstcolor_invdstalpha = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_INVDSTALPHA];
	frag_textured_smooth_zero_invsrccolor = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ZERO_INVSRCCOLOR];
	frag_textured_smooth_dstcolor_srcalpha = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_SRCALPHA];
	frag_textured_smooth_one_invsrcalpha = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ONE_INVSRCALPHA];
	frag_textured_smooth_invsrcalpha_srcalpha = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_INVSRCALPHA_SRCALPHA];
	frag_untextured_blend_srcalpha_one = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_BLEND_SRCALPHA_ONE];
	frag_untextured_smooth_blend_srcalpha_one = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_BLEND_SRCALPHA_ONE];
	frag_textured_smooth_blend_srcalpha_one = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_BLEND_SRCALPHA_ONE];
	/* The remaining alpha_func variants: 5 funcs x 3 shapes. */
	frag_untextured_alphatest_never       = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST_NEVER];
	frag_textured_alphatest_never         = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST_NEVER];
	frag_textured_smooth_alphatest_never  = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST_NEVER];
	frag_untextured_alphatest_less        = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST_LESS];
	frag_textured_alphatest_less          = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST_LESS];
	frag_textured_smooth_alphatest_less   = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST_LESS];
	frag_untextured_alphatest_equal       = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST_EQUAL];
	frag_textured_alphatest_equal         = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST_EQUAL];
	frag_textured_smooth_alphatest_equal  = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST_EQUAL];
	frag_untextured_alphatest_lequal      = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST_LEQUAL];
	frag_textured_alphatest_lequal        = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST_LEQUAL];
	frag_textured_smooth_alphatest_lequal = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST_LEQUAL];
	frag_untextured_alphatest_notequal    = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST_NOTEQUAL];
	frag_textured_alphatest_notequal      = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST_NOTEQUAL];
	frag_textured_smooth_alphatest_notequal = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST_NOTEQUAL];
	/* Combined fog + alpha test, slots 62..75 -- see
	 * v3d_shader_assembler.h's enum comment for the lockstep rules. */
	frag_untextured_fog_alphatest = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST];
	frag_textured_fog_alphatest = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST];
	frag_untextured_fog_alphatest_greater = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST_GREATER];
	frag_textured_fog_alphatest_greater = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST_GREATER];
	frag_untextured_fog_alphatest_less = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST_LESS];
	frag_textured_fog_alphatest_less = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST_LESS];
	frag_untextured_fog_alphatest_equal = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST_EQUAL];
	frag_textured_fog_alphatest_equal = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST_EQUAL];
	frag_untextured_fog_alphatest_lequal = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST_LEQUAL];
	frag_textured_fog_alphatest_lequal = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST_LEQUAL];
	frag_untextured_fog_alphatest_notequal = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST_NOTEQUAL];
	frag_textured_fog_alphatest_notequal = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST_NOTEQUAL];
	frag_untextured_fog_alphatest_never = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST_NEVER];
	frag_textured_fog_alphatest_never = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST_NEVER];
	/* Smooth fog, slots 76..84 -- see
	 * v3d_shader_assembler.h's enum comment for the lockstep rules. */
	frag_untextured_smooth_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_FOG];
	frag_textured_smooth_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG];
	frag_textured_smooth_fog_alphatest = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST];
	frag_textured_smooth_fog_alphatest_greater = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST_GREATER];
	frag_textured_smooth_fog_alphatest_less = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST_LESS];
	frag_textured_smooth_fog_alphatest_equal = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST_EQUAL];
	frag_textured_smooth_fog_alphatest_lequal = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST_LEQUAL];
	frag_textured_smooth_fog_alphatest_notequal = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST_NOTEQUAL];
	frag_textured_smooth_fog_alphatest_never = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST_NEVER];
	/* Multitexture fog, slots 85..87. */
	frag_multitexture_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_FOG];
	frag_multitexture_decal_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_DECAL_FOG];
	frag_multitexture_replace_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_REPLACE_FOG];
	/* Software-blend fog, slots 88..101. */
	frag_textured_blend_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_BLEND_FOG];
	frag_textured_smooth_blend_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_BLEND_FOG];
	frag_textured_smooth_dstcolor_zero_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_ZERO_FOG];
	frag_textured_smooth_dstcolor_one_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_ONE_FOG];
	frag_textured_smooth_dstcolor_srccolor_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_SRCCOLOR_FOG];
	frag_textured_smooth_dstcolor_invdstalpha_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_INVDSTALPHA_FOG];
	frag_textured_smooth_zero_invsrccolor_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ZERO_INVSRCCOLOR_FOG];
	frag_textured_smooth_dstcolor_srcalpha_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_SRCALPHA_FOG];
	frag_textured_smooth_one_invsrcalpha_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ONE_INVSRCALPHA_FOG];
	frag_textured_smooth_invsrcalpha_srcalpha_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_INVSRCALPHA_SRCALPHA_FOG];
	frag_multitexture_modulate_translucent_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_MODULATE_TRANSLUCENT_FOG];
	frag_multitexture_modulate_blend_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_MODULATE_BLEND_FOG];
	frag_multitexture_decal_blend_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_DECAL_BLEND_FOG];
	frag_multitexture_replace_blend_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_REPLACE_BLEND_FOG];
	/* Register-constrained blend fog, slots 102..106. */
	frag_untextured_smooth_blend_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_BLEND_FOG];
	frag_untextured_smooth_blend_add_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_BLEND_ADD_FOG];
	frag_untextured_smooth_blend_srcalpha_one_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_BLEND_SRCALPHA_ONE_FOG];
	frag_textured_smooth_blend_add_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_BLEND_ADD_FOG];
	frag_textured_smooth_blend_srcalpha_one_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_BLEND_SRCALPHA_ONE_FOG];
	/* Untextured flat blend fog, slots 107..109. */
	frag_untextured_blend_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_BLEND_FOG];
	frag_untextured_blend_add_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_BLEND_ADD_FOG];
	frag_untextured_blend_srcalpha_one_fog = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_BLEND_SRCALPHA_ONE_FOG];
	/* Smooth points, slots 110..113. */
	frag_untextured_point_smooth        = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_POINT_SMOOTH];
	frag_untextured_smooth_point_smooth = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_POINT_SMOOTH];
	frag_textured_point_smooth          = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_POINT_SMOOTH];
	frag_textured_smooth_point_smooth   = &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_POINT_SMOOTH];

	for (i = 0; i < vex->numInstructions; i++)
		shader_vex[i] = byteswap64(vex->instructions[i]);
	for (i = 0; i < coord->numInstructions; i++)
		shader_coord[i] = byteswap64(coord->instructions[i]);
	for (i = 0; i < frag->numInstructions; i++)
		shader_frag[i] = byteswap64(frag->instructions[i]);
	for (i = 0; i < frag_textured->numInstructions; i++)
		shader_frag_textured[i] = byteswap64(frag_textured->instructions[i]);
	for (i = 0; i < vex_smooth->numInstructions; i++)
		shader_vex_smooth[i] = byteswap64(vex_smooth->instructions[i]);
	for (i = 0; i < frag_smooth->numInstructions; i++)
		shader_frag_smooth[i] = byteswap64(frag_smooth->instructions[i]);
	for (i = 0; i < vex_smooth_textured->numInstructions; i++)
		shader_vex_smooth_textured[i] = byteswap64(vex_smooth_textured->instructions[i]);
	for (i = 0; i < frag_textured_smooth->numInstructions; i++)
		shader_frag_textured_smooth[i] = byteswap64(frag_textured_smooth->instructions[i]);
	for (i = 0; i < frag_untextured_fog->numInstructions; i++)
		shader_frag_untextured_fog[i] = byteswap64(frag_untextured_fog->instructions[i]);
	for (i = 0; i < frag_untextured_alphatest->numInstructions; i++)
		shader_frag_untextured_alphatest[i] = byteswap64(frag_untextured_alphatest->instructions[i]);
	for (i = 0; i < frag_textured_fog->numInstructions; i++)
		shader_frag_textured_fog[i] = byteswap64(frag_textured_fog->instructions[i]);
	for (i = 0; i < frag_textured_alphatest->numInstructions; i++)
		shader_frag_textured_alphatest[i] = byteswap64(frag_textured_alphatest->instructions[i]);
	for (i = 0; i < vex_multitexture->numInstructions; i++)
		shader_vex_multitexture[i] = byteswap64(vex_multitexture->instructions[i]);
	for (i = 0; i < frag_multitexture->numInstructions; i++)
		shader_frag_multitexture[i] = byteswap64(frag_multitexture->instructions[i]);
	for (i = 0; i < frag_untextured_blend->numInstructions; i++)
		shader_frag_untextured_blend[i] = byteswap64(frag_untextured_blend->instructions[i]);
	for (i = 0; i < frag_untextured_blend_add->numInstructions; i++)
		shader_frag_untextured_blend_add[i] = byteswap64(frag_untextured_blend_add->instructions[i]);
	for (i = 0; i < frag_untextured_smooth_blend_add->numInstructions; i++)
		shader_frag_untextured_smooth_blend_add[i] = byteswap64(frag_untextured_smooth_blend_add->instructions[i]);
	for (i = 0; i < frag_untextured_smooth_blend->numInstructions; i++)
		shader_frag_untextured_smooth_blend[i] = byteswap64(frag_untextured_smooth_blend->instructions[i]);
	for (i = 0; i < frag_textured_blend->numInstructions; i++)
		shader_frag_textured_blend[i] = byteswap64(frag_textured_blend->instructions[i]);
	for (i = 0; i < frag_textured_smooth_blend->numInstructions; i++)
		shader_frag_textured_smooth_blend[i] = byteswap64(frag_textured_smooth_blend->instructions[i]);
	for (i = 0; i < frag_textured_padded_flat_test->numInstructions; i++)
		shader_frag_textured_padded_flat_test[i] = byteswap64(frag_textured_padded_flat_test->instructions[i]);
	for (i = 0; i < coord_clipspace->numInstructions; i++)
		shader_coord_clipspace[i] = byteswap64(coord_clipspace->instructions[i]);
	for (i = 0; i < vex_smooth_textured_clipspace->numInstructions; i++)
		shader_vex_smooth_textured_clipspace[i] = byteswap64(vex_smooth_textured_clipspace->instructions[i]);
	for (i = 0; i < vex_multitexture_clipspace->numInstructions; i++)
		shader_vex_multitexture_clipspace[i] = byteswap64(vex_multitexture_clipspace->instructions[i]);
	for (i = 0; i < frag_multitexture_decal->numInstructions; i++)
		shader_frag_multitexture_decal[i] = byteswap64(frag_multitexture_decal->instructions[i]);
	for (i = 0; i < frag_multitexture_replace->numInstructions; i++)
		shader_frag_multitexture_replace[i] = byteswap64(frag_multitexture_replace->instructions[i]);
	for (i = 0; i < frag_multitexture_modulate_blend->numInstructions; i++)
		shader_frag_multitexture_modulate_blend[i] = byteswap64(frag_multitexture_modulate_blend->instructions[i]);
	for (i = 0; i < frag_multitexture_decal_blend->numInstructions; i++)
		shader_frag_multitexture_decal_blend[i] = byteswap64(frag_multitexture_decal_blend->instructions[i]);
	for (i = 0; i < frag_multitexture_replace_blend->numInstructions; i++)
		shader_frag_multitexture_replace_blend[i] = byteswap64(frag_multitexture_replace_blend->instructions[i]);
	for (i = 0; i < frag_untextured_alphatest_greater->numInstructions; i++)
		shader_frag_untextured_alphatest_greater[i] = byteswap64(frag_untextured_alphatest_greater->instructions[i]);
	for (i = 0; i < frag_textured_alphatest_greater->numInstructions; i++)
		shader_frag_textured_alphatest_greater[i] = byteswap64(frag_textured_alphatest_greater->instructions[i]);
	for (i = 0; i < frag_textured_smooth_alphatest_greater->numInstructions; i++)
		shader_frag_textured_smooth_alphatest_greater[i] = byteswap64(frag_textured_smooth_alphatest_greater->instructions[i]);
	for (i = 0; i < frag_textured_smooth_alphatest->numInstructions; i++)
		shader_frag_textured_smooth_alphatest[i] = byteswap64(frag_textured_smooth_alphatest->instructions[i]);
	for (i = 0; i < frag_multitexture_modulate_translucent->numInstructions; i++)
		shader_frag_multitexture_modulate_translucent[i] = byteswap64(frag_multitexture_modulate_translucent->instructions[i]);
	for (i = 0; i < frag_textured_colormod->numInstructions; i++)
		shader_frag_textured_colormod[i] = byteswap64(frag_textured_colormod->instructions[i]);
	for (i = 0; i < frag_textured_smooth_blend_add->numInstructions; i++)
		shader_frag_textured_smooth_blend_add[i] = byteswap64(frag_textured_smooth_blend_add->instructions[i]);
	for (i = 0; i < frag_textured_smooth_dstcolor_zero->numInstructions; i++)
		shader_frag_textured_smooth_dstcolor_zero[i] = byteswap64(frag_textured_smooth_dstcolor_zero->instructions[i]);
	for (i = 0; i < frag_textured_smooth_dstcolor_one->numInstructions; i++)
		shader_frag_textured_smooth_dstcolor_one[i] = byteswap64(frag_textured_smooth_dstcolor_one->instructions[i]);
	for (i = 0; i < frag_textured_smooth_dstcolor_srccolor->numInstructions; i++)
		shader_frag_textured_smooth_dstcolor_srccolor[i] = byteswap64(frag_textured_smooth_dstcolor_srccolor->instructions[i]);
	for (i = 0; i < frag_textured_smooth_dstcolor_invdstalpha->numInstructions; i++)
		shader_frag_textured_smooth_dstcolor_invdstalpha[i] = byteswap64(frag_textured_smooth_dstcolor_invdstalpha->instructions[i]);
	for (i = 0; i < frag_textured_smooth_zero_invsrccolor->numInstructions; i++)
		shader_frag_textured_smooth_zero_invsrccolor[i] = byteswap64(frag_textured_smooth_zero_invsrccolor->instructions[i]);
	for (i = 0; i < frag_textured_smooth_dstcolor_srcalpha->numInstructions; i++)
		shader_frag_textured_smooth_dstcolor_srcalpha[i] = byteswap64(frag_textured_smooth_dstcolor_srcalpha->instructions[i]);
	for (i = 0; i < frag_textured_smooth_one_invsrcalpha->numInstructions; i++)
		shader_frag_textured_smooth_one_invsrcalpha[i] = byteswap64(frag_textured_smooth_one_invsrcalpha->instructions[i]);
	for (i = 0; i < frag_textured_smooth_invsrcalpha_srcalpha->numInstructions; i++)
		shader_frag_textured_smooth_invsrcalpha_srcalpha[i] = byteswap64(frag_textured_smooth_invsrcalpha_srcalpha->instructions[i]);
	for (i = 0; i < frag_untextured_blend_srcalpha_one->numInstructions; i++)
		shader_frag_untextured_blend_srcalpha_one[i] = byteswap64(frag_untextured_blend_srcalpha_one->instructions[i]);
	for (i = 0; i < frag_untextured_smooth_blend_srcalpha_one->numInstructions; i++)
		shader_frag_untextured_smooth_blend_srcalpha_one[i] = byteswap64(frag_untextured_smooth_blend_srcalpha_one->instructions[i]);
	for (i = 0; i < frag_textured_smooth_blend_srcalpha_one->numInstructions; i++)
		shader_frag_textured_smooth_blend_srcalpha_one[i] = byteswap64(frag_textured_smooth_blend_srcalpha_one->instructions[i]);
	/* The remaining alpha_func variants: 5 funcs x 3 shapes. */
	for (i = 0; i < frag_untextured_alphatest_never->numInstructions; i++)
		shader_frag_untextured_alphatest_never[i] = byteswap64(frag_untextured_alphatest_never->instructions[i]);
	for (i = 0; i < frag_textured_alphatest_never->numInstructions; i++)
		shader_frag_textured_alphatest_never[i] = byteswap64(frag_textured_alphatest_never->instructions[i]);
	for (i = 0; i < frag_textured_smooth_alphatest_never->numInstructions; i++)
		shader_frag_textured_smooth_alphatest_never[i] = byteswap64(frag_textured_smooth_alphatest_never->instructions[i]);
	for (i = 0; i < frag_untextured_alphatest_less->numInstructions; i++)
		shader_frag_untextured_alphatest_less[i] = byteswap64(frag_untextured_alphatest_less->instructions[i]);
	for (i = 0; i < frag_textured_alphatest_less->numInstructions; i++)
		shader_frag_textured_alphatest_less[i] = byteswap64(frag_textured_alphatest_less->instructions[i]);
	for (i = 0; i < frag_textured_smooth_alphatest_less->numInstructions; i++)
		shader_frag_textured_smooth_alphatest_less[i] = byteswap64(frag_textured_smooth_alphatest_less->instructions[i]);
	for (i = 0; i < frag_untextured_alphatest_equal->numInstructions; i++)
		shader_frag_untextured_alphatest_equal[i] = byteswap64(frag_untextured_alphatest_equal->instructions[i]);
	for (i = 0; i < frag_textured_alphatest_equal->numInstructions; i++)
		shader_frag_textured_alphatest_equal[i] = byteswap64(frag_textured_alphatest_equal->instructions[i]);
	for (i = 0; i < frag_textured_smooth_alphatest_equal->numInstructions; i++)
		shader_frag_textured_smooth_alphatest_equal[i] = byteswap64(frag_textured_smooth_alphatest_equal->instructions[i]);
	for (i = 0; i < frag_untextured_alphatest_lequal->numInstructions; i++)
		shader_frag_untextured_alphatest_lequal[i] = byteswap64(frag_untextured_alphatest_lequal->instructions[i]);
	for (i = 0; i < frag_textured_alphatest_lequal->numInstructions; i++)
		shader_frag_textured_alphatest_lequal[i] = byteswap64(frag_textured_alphatest_lequal->instructions[i]);
	for (i = 0; i < frag_textured_smooth_alphatest_lequal->numInstructions; i++)
		shader_frag_textured_smooth_alphatest_lequal[i] = byteswap64(frag_textured_smooth_alphatest_lequal->instructions[i]);
	for (i = 0; i < frag_untextured_alphatest_notequal->numInstructions; i++)
		shader_frag_untextured_alphatest_notequal[i] = byteswap64(frag_untextured_alphatest_notequal->instructions[i]);
	for (i = 0; i < frag_textured_alphatest_notequal->numInstructions; i++)
		shader_frag_textured_alphatest_notequal[i] = byteswap64(frag_textured_alphatest_notequal->instructions[i]);
	for (i = 0; i < frag_textured_smooth_alphatest_notequal->numInstructions; i++)
		shader_frag_textured_smooth_alphatest_notequal[i] = byteswap64(frag_textured_smooth_alphatest_notequal->instructions[i]);
	/* Combined fog + alpha test, slots 62..75 -- see
	 * v3d_shader_assembler.h's enum comment for the lockstep rules. */
	for (i = 0; i < frag_untextured_fog_alphatest->numInstructions; i++)
		shader_frag_untextured_fog_alphatest[i] = byteswap64(frag_untextured_fog_alphatest->instructions[i]);
	for (i = 0; i < frag_textured_fog_alphatest->numInstructions; i++)
		shader_frag_textured_fog_alphatest[i] = byteswap64(frag_textured_fog_alphatest->instructions[i]);
	for (i = 0; i < frag_untextured_fog_alphatest_greater->numInstructions; i++)
		shader_frag_untextured_fog_alphatest_greater[i] = byteswap64(frag_untextured_fog_alphatest_greater->instructions[i]);
	for (i = 0; i < frag_textured_fog_alphatest_greater->numInstructions; i++)
		shader_frag_textured_fog_alphatest_greater[i] = byteswap64(frag_textured_fog_alphatest_greater->instructions[i]);
	for (i = 0; i < frag_untextured_fog_alphatest_less->numInstructions; i++)
		shader_frag_untextured_fog_alphatest_less[i] = byteswap64(frag_untextured_fog_alphatest_less->instructions[i]);
	for (i = 0; i < frag_textured_fog_alphatest_less->numInstructions; i++)
		shader_frag_textured_fog_alphatest_less[i] = byteswap64(frag_textured_fog_alphatest_less->instructions[i]);
	for (i = 0; i < frag_untextured_fog_alphatest_equal->numInstructions; i++)
		shader_frag_untextured_fog_alphatest_equal[i] = byteswap64(frag_untextured_fog_alphatest_equal->instructions[i]);
	for (i = 0; i < frag_textured_fog_alphatest_equal->numInstructions; i++)
		shader_frag_textured_fog_alphatest_equal[i] = byteswap64(frag_textured_fog_alphatest_equal->instructions[i]);
	for (i = 0; i < frag_untextured_fog_alphatest_lequal->numInstructions; i++)
		shader_frag_untextured_fog_alphatest_lequal[i] = byteswap64(frag_untextured_fog_alphatest_lequal->instructions[i]);
	for (i = 0; i < frag_textured_fog_alphatest_lequal->numInstructions; i++)
		shader_frag_textured_fog_alphatest_lequal[i] = byteswap64(frag_textured_fog_alphatest_lequal->instructions[i]);
	for (i = 0; i < frag_untextured_fog_alphatest_notequal->numInstructions; i++)
		shader_frag_untextured_fog_alphatest_notequal[i] = byteswap64(frag_untextured_fog_alphatest_notequal->instructions[i]);
	for (i = 0; i < frag_textured_fog_alphatest_notequal->numInstructions; i++)
		shader_frag_textured_fog_alphatest_notequal[i] = byteswap64(frag_textured_fog_alphatest_notequal->instructions[i]);
	for (i = 0; i < frag_untextured_fog_alphatest_never->numInstructions; i++)
		shader_frag_untextured_fog_alphatest_never[i] = byteswap64(frag_untextured_fog_alphatest_never->instructions[i]);
	for (i = 0; i < frag_textured_fog_alphatest_never->numInstructions; i++)
		shader_frag_textured_fog_alphatest_never[i] = byteswap64(frag_textured_fog_alphatest_never->instructions[i]);
	/* Smooth fog, slots 76..84 -- see
	 * v3d_shader_assembler.h's enum comment for the lockstep rules. */
	for (i = 0; i < frag_untextured_smooth_fog->numInstructions; i++)
		shader_frag_untextured_smooth_fog[i] = byteswap64(frag_untextured_smooth_fog->instructions[i]);
	for (i = 0; i < frag_textured_smooth_fog->numInstructions; i++)
		shader_frag_textured_smooth_fog[i] = byteswap64(frag_textured_smooth_fog->instructions[i]);
	for (i = 0; i < frag_textured_smooth_fog_alphatest->numInstructions; i++)
		shader_frag_textured_smooth_fog_alphatest[i] = byteswap64(frag_textured_smooth_fog_alphatest->instructions[i]);
	for (i = 0; i < frag_textured_smooth_fog_alphatest_greater->numInstructions; i++)
		shader_frag_textured_smooth_fog_alphatest_greater[i] = byteswap64(frag_textured_smooth_fog_alphatest_greater->instructions[i]);
	for (i = 0; i < frag_textured_smooth_fog_alphatest_less->numInstructions; i++)
		shader_frag_textured_smooth_fog_alphatest_less[i] = byteswap64(frag_textured_smooth_fog_alphatest_less->instructions[i]);
	for (i = 0; i < frag_textured_smooth_fog_alphatest_equal->numInstructions; i++)
		shader_frag_textured_smooth_fog_alphatest_equal[i] = byteswap64(frag_textured_smooth_fog_alphatest_equal->instructions[i]);
	for (i = 0; i < frag_textured_smooth_fog_alphatest_lequal->numInstructions; i++)
		shader_frag_textured_smooth_fog_alphatest_lequal[i] = byteswap64(frag_textured_smooth_fog_alphatest_lequal->instructions[i]);
	for (i = 0; i < frag_textured_smooth_fog_alphatest_notequal->numInstructions; i++)
		shader_frag_textured_smooth_fog_alphatest_notequal[i] = byteswap64(frag_textured_smooth_fog_alphatest_notequal->instructions[i]);
	for (i = 0; i < frag_textured_smooth_fog_alphatest_never->numInstructions; i++)
		shader_frag_textured_smooth_fog_alphatest_never[i] = byteswap64(frag_textured_smooth_fog_alphatest_never->instructions[i]);
	/* Multitexture fog, slots 85..87. */
	for (i = 0; i < frag_multitexture_fog->numInstructions; i++)
		shader_frag_multitexture_fog[i] = byteswap64(frag_multitexture_fog->instructions[i]);
	for (i = 0; i < frag_multitexture_decal_fog->numInstructions; i++)
		shader_frag_multitexture_decal_fog[i] = byteswap64(frag_multitexture_decal_fog->instructions[i]);
	for (i = 0; i < frag_multitexture_replace_fog->numInstructions; i++)
		shader_frag_multitexture_replace_fog[i] = byteswap64(frag_multitexture_replace_fog->instructions[i]);
	/* Software-blend fog, slots 88..101. */
	for (i = 0; i < frag_textured_blend_fog->numInstructions; i++)
		shader_frag_textured_blend_fog[i] = byteswap64(frag_textured_blend_fog->instructions[i]);
	for (i = 0; i < frag_textured_smooth_blend_fog->numInstructions; i++)
		shader_frag_textured_smooth_blend_fog[i] = byteswap64(frag_textured_smooth_blend_fog->instructions[i]);
	for (i = 0; i < frag_textured_smooth_dstcolor_zero_fog->numInstructions; i++)
		shader_frag_textured_smooth_dstcolor_zero_fog[i] = byteswap64(frag_textured_smooth_dstcolor_zero_fog->instructions[i]);
	for (i = 0; i < frag_textured_smooth_dstcolor_one_fog->numInstructions; i++)
		shader_frag_textured_smooth_dstcolor_one_fog[i] = byteswap64(frag_textured_smooth_dstcolor_one_fog->instructions[i]);
	for (i = 0; i < frag_textured_smooth_dstcolor_srccolor_fog->numInstructions; i++)
		shader_frag_textured_smooth_dstcolor_srccolor_fog[i] = byteswap64(frag_textured_smooth_dstcolor_srccolor_fog->instructions[i]);
	for (i = 0; i < frag_textured_smooth_dstcolor_invdstalpha_fog->numInstructions; i++)
		shader_frag_textured_smooth_dstcolor_invdstalpha_fog[i] = byteswap64(frag_textured_smooth_dstcolor_invdstalpha_fog->instructions[i]);
	for (i = 0; i < frag_textured_smooth_zero_invsrccolor_fog->numInstructions; i++)
		shader_frag_textured_smooth_zero_invsrccolor_fog[i] = byteswap64(frag_textured_smooth_zero_invsrccolor_fog->instructions[i]);
	for (i = 0; i < frag_textured_smooth_dstcolor_srcalpha_fog->numInstructions; i++)
		shader_frag_textured_smooth_dstcolor_srcalpha_fog[i] = byteswap64(frag_textured_smooth_dstcolor_srcalpha_fog->instructions[i]);
	for (i = 0; i < frag_textured_smooth_one_invsrcalpha_fog->numInstructions; i++)
		shader_frag_textured_smooth_one_invsrcalpha_fog[i] = byteswap64(frag_textured_smooth_one_invsrcalpha_fog->instructions[i]);
	for (i = 0; i < frag_textured_smooth_invsrcalpha_srcalpha_fog->numInstructions; i++)
		shader_frag_textured_smooth_invsrcalpha_srcalpha_fog[i] = byteswap64(frag_textured_smooth_invsrcalpha_srcalpha_fog->instructions[i]);
	for (i = 0; i < frag_multitexture_modulate_translucent_fog->numInstructions; i++)
		shader_frag_multitexture_modulate_translucent_fog[i] = byteswap64(frag_multitexture_modulate_translucent_fog->instructions[i]);
	for (i = 0; i < frag_multitexture_modulate_blend_fog->numInstructions; i++)
		shader_frag_multitexture_modulate_blend_fog[i] = byteswap64(frag_multitexture_modulate_blend_fog->instructions[i]);
	for (i = 0; i < frag_multitexture_decal_blend_fog->numInstructions; i++)
		shader_frag_multitexture_decal_blend_fog[i] = byteswap64(frag_multitexture_decal_blend_fog->instructions[i]);
	for (i = 0; i < frag_multitexture_replace_blend_fog->numInstructions; i++)
		shader_frag_multitexture_replace_blend_fog[i] = byteswap64(frag_multitexture_replace_blend_fog->instructions[i]);
	/* Register-constrained blend fog, slots 102..106. */
	for (i = 0; i < frag_untextured_smooth_blend_fog->numInstructions; i++)
		shader_frag_untextured_smooth_blend_fog[i] = byteswap64(frag_untextured_smooth_blend_fog->instructions[i]);
	for (i = 0; i < frag_untextured_smooth_blend_add_fog->numInstructions; i++)
		shader_frag_untextured_smooth_blend_add_fog[i] = byteswap64(frag_untextured_smooth_blend_add_fog->instructions[i]);
	for (i = 0; i < frag_untextured_smooth_blend_srcalpha_one_fog->numInstructions; i++)
		shader_frag_untextured_smooth_blend_srcalpha_one_fog[i] = byteswap64(frag_untextured_smooth_blend_srcalpha_one_fog->instructions[i]);
	for (i = 0; i < frag_textured_smooth_blend_add_fog->numInstructions; i++)
		shader_frag_textured_smooth_blend_add_fog[i] = byteswap64(frag_textured_smooth_blend_add_fog->instructions[i]);
	for (i = 0; i < frag_textured_smooth_blend_srcalpha_one_fog->numInstructions; i++)
		shader_frag_textured_smooth_blend_srcalpha_one_fog[i] = byteswap64(frag_textured_smooth_blend_srcalpha_one_fog->instructions[i]);
	/* Untextured flat blend fog, slots 107..109. */
	for (i = 0; i < frag_untextured_blend_fog->numInstructions; i++)
		shader_frag_untextured_blend_fog[i] = byteswap64(frag_untextured_blend_fog->instructions[i]);
	for (i = 0; i < frag_untextured_blend_add_fog->numInstructions; i++)
		shader_frag_untextured_blend_add_fog[i] = byteswap64(frag_untextured_blend_add_fog->instructions[i]);
	for (i = 0; i < frag_untextured_blend_srcalpha_one_fog->numInstructions; i++)
		shader_frag_untextured_blend_srcalpha_one_fog[i] = byteswap64(frag_untextured_blend_srcalpha_one_fog->instructions[i]);
	/* Smooth points, slots 110..113. */
	for (i = 0; i < frag_untextured_point_smooth->numInstructions; i++)
		shader_frag_untextured_point_smooth[i] = byteswap64(frag_untextured_point_smooth->instructions[i]);
	for (i = 0; i < frag_untextured_smooth_point_smooth->numInstructions; i++)
		shader_frag_untextured_smooth_point_smooth[i] = byteswap64(frag_untextured_smooth_point_smooth->instructions[i]);
	for (i = 0; i < frag_textured_point_smooth->numInstructions; i++)
		shader_frag_textured_point_smooth[i] = byteswap64(frag_textured_point_smooth->instructions[i]);
	for (i = 0; i < frag_textured_smooth_point_smooth->numInstructions; i++)
		shader_frag_textured_smooth_point_smooth[i] = byteswap64(frag_textured_smooth_point_smooth->instructions[i]);

	{
		struct ExecBase* const SysBase = context->device.sysbase;
		ULONG len = (ULONG)backend->shader_code_mem.size;
		CachePreDMA(backend->shader_code_mem.hostptr, &len, 0);
	}

	backend->shaders_ready = TRUE;
	D(("gl_EnsureShaders: shaders assembled and validated OK\n"));
	D(("gl_EnsureShaders: OK, vex=%ld coord=%ld frag=%ld frag_textured=%ld vex_smooth=%ld frag_smooth=%ld vex_smooth_textured=%ld frag_textured_smooth=%ld fog=%ld alphatest=%ld tex_fog=%ld tex_alphatest=%ld vex_multitex=%ld frag_multitex=%ld untextured_blend=%ld instructions, code_mem=%08lx\n",
	   (LONG)vex->numInstructions, (LONG)coord->numInstructions, (LONG)frag->numInstructions,
	   (LONG)frag_textured->numInstructions, (LONG)vex_smooth->numInstructions, (LONG)frag_smooth->numInstructions,
	   (LONG)vex_smooth_textured->numInstructions, (LONG)frag_textured_smooth->numInstructions,
	   (LONG)frag_untextured_fog->numInstructions, (LONG)frag_untextured_alphatest->numInstructions,
	   (LONG)frag_textured_fog->numInstructions, (LONG)frag_textured_alphatest->numInstructions,
	   (LONG)vex_multitexture->numInstructions, (LONG)frag_multitexture->numInstructions,
	   (LONG)frag_untextured_blend->numInstructions,
	   (ULONG)backend->shader_code_mem.hostptr));
	return 0;
}

/*
 * The shader state record, its attribute-record descriptors and the vertex
 * data for `count` vertices -- named by `indices`, into
 * context->VertexBuffer -- are built per draw call, bound with glShaderState
 * and drawn with the primitive-list packet below.
 *
 * The state record is rebuilt fresh on every draw call rather than cached
 * across the draw calls of a frame: glShaderStateAttributeRecord's
 * own ordering constraint -- attribute records must immediately follow
 * their state record in the CL -- means a cached state record's attribute
 * data would need a FIXED vertex-buffer address, which a new glBegin/glEnd's
 * fresh vertex data cannot reuse anyway.
 */

/* The point size the current pass rasterizes with (POINT_SIZE, sent below
 * once per pass). The smooth-point shaders measure distance in pixels
 * against it, so they get this value rather than the live glPointSize. */
static GLfloat s_pass_point_size = 1.0f;

static void gl_EnsureDrawState(GLcontext context)
{
	V3DContext* backend = &context->backend;

	if (backend->draw_state_configured)
		return;

	/* MESA-exact: CLIP_WINDOW and the viewport group are per
	 * draw (gl_EmitPrimitiveV3D / gl_EmitCullBlendState, dirty-cached), and
	 * this function is called after the first draw's blend group so that the
	 * per-pass residue left here (BLEND_CONSTANT_COLOR, the ZERO_ALL flags,
	 * TF specs, OQ, SAMPLE_STATE, POINT_SIZE/LINE_WIDTH, VCM_CACHE_SIZE)
	 * lands exactly where MESA's first draw emits it (v3dx_emit.c:569-703).
	 *
	 * CfgBits and the blend-config packets are NOT here: their effp/erfp/cp
	 * (cull-face) and be (blend-enable) bits have to vary per draw call
	 * within a single frame, so they are rebuilt from live GL state in
	 * gl_EmitCullBlendState instead. This function keeps only what does not
	 * vary within a pass. */

	/* Point size and line width come from the GL app's real glPointSize/
	 * glLineWidth (context->CurrentPointSize/CurrentLineWidth,
	 * vertexbuffer_min.c).
	 *
	 * This function runs once per PASS (draw_state_configured, reset by
	 * gl_FrameBegin), so a pass that changes point size or line width
	 * between batches gets whichever value was live at its first draw. That
	 * is a deliberate deviation: MESA sends POINT_SIZE on every draw on
	 * ver==42 (v3dx_emit.c:429-434, a binner-FIFO erratum workaround that
	 * needs "any CLE command" per draw). */
	PointSize(backend, context->CurrentPointSize);
	s_pass_point_size = context->CurrentPointSize;
	LineWidth(backend, context->CurrentLineWidth);
	/* ColorWriteMasks is NOT here either, for the same reason as CfgBits:
	 * glColorMask can be toggled more than once within a single frame, so a
	 * once-per-pass value would only ever reflect whichever call happened to
	 * be active when the first draw of the pass ran. It is emitted per draw
	 * call from gl_EmitCullBlendState. */
	BlendConstantColor(backend, 0, 0, 0, 0);
	DoCommand(backend, v3d_OP_ZERO_ALL_FLAT_SHADE_FLAGS);
	DoCommand(backend, v3d_OP_ZERO_ALL_NON_PERSPECTIVE_FLAGS);
	DoCommand(backend, v3d_OP_ZERO_ALL_CENTROID_FLAGS);
	TransformFeedbackSpecs(backend, FALSE, 0);
	OcclusionQueryCounter(backend, 0);
	SampleState(backend, 1.0, 0xF);
	/* Once per pass. MESA instead sends VCM_CACHE_SIZE before every
	 * GL_SHADER_STATE, with the VS input segment folded to 1 (v3dx_draw.c:865,
	 * vir.c:837); this driver sends (4,4) once per pass. */
	VCMCacheSize(backend, 4, 4);

	backend->draw_state_configured = TRUE;
	D(("gl_EnsureDrawState: configured per-pass residue\n"));
}

/* MESA-style dirty caching of per-draw state packets. MESA emits each of
 * these only when its dirty flag is set (v3dx_emit.c). One last-emitted copy
 * per packet type, invalidated by g_v3d_cl_generation at every pass start, so
 * nothing can outlive the CL it was written into (the counter is bumped
 * wherever a CL is restarted, including the mid-frame intermediate pass).
 *
 * NOT cached, on purpose, because MESA does not: GL_SHADER_STATE (a fresh
 * record every draw under immediate-mode submission) and the primitives.
 * POINT_SIZE/LINE_WIDTH and VCM_CACHE_SIZE are per pass (gl_EnsureDrawState).
 *
 * Field order: 32-bit fields first, then the UWORDs, then the UBYTEs. v3d_hw.h's
 * #pragma pack(push,1) is still in force here, so interleaving them left ints
 * at odd offsets, and GCC read each of those with byte loads. */
typedef struct
{
	v3d_u32 gen;
	int   cfg_valid;
	int   do_valid;
	float do_f, do_u;
	int   vp_valid;
	int   cw_valid;
	int   bl_valid;
	int   cwm_valid;
	int   cwm;
	UWORD vp_sx, vp_sy, vp_ax, vp_ay;
	UWORD cw_l, cw_b, cw_w, cw_h;
	UBYTE cfg_effp, cfg_erfp, cfg_cp, cfg_edo, cfg_dtf, cfg_zue, cfg_eze, cfg_ezue, cfg_be;
	UBYTE bl_on, bl_src, bl_dst, bl_eq, bl_asrc, bl_adst, bl_aeq;
} mglv3d_dedup_cache;

static mglv3d_dedup_cache s_dd = { 0xFFFFFFFFUL };

/* Resets every entry when the CL generation moved: one compare per draw. */
static void dd_sync(void)
{
	extern v3d_u32 g_v3d_cl_generation;

	if (s_dd.gen != g_v3d_cl_generation)
	{
		s_dd.gen = g_v3d_cl_generation;
		s_dd.cfg_valid = 0;
		s_dd.do_valid  = 0;
		s_dd.vp_valid  = 0;
		s_dd.cw_valid  = 0;
		s_dd.bl_valid  = 0;
		s_dd.cwm_valid = 0;
	}
}

/*
 * GL_CULL_FACE + GL_BLEND. Called fresh from gl_EmitPrimitiveV3D every draw
 * call, unlike gl_EnsureDrawState above, which only fires once per pass --
 * an application may toggle either around individual draw calls.
 *
 * effp/erfp/cp mapping cross-checked against MESA's real, currently-
 * shipping V3D Vulkan driver (v3dvx_cmd_buffer.c, v3d_X_cmd_buffer_emit_
 * viewport-equivalent -- the cull-mode/front-face block that fills in
 * v3d_cfg_bits), not guessed at, per this project's own hard constraint
 * on CLE-adjacent hardware behavior:
 *
 *   config.enable_forward_facing_primitive = !(cull_mode & FRONT_BIT);
 *   config.enable_reverse_facing_primitive = !(cull_mode & BACK_BIT);
 *   config.clockwise_primitives = front_face == COUNTER_CLOCKWISE;
 *     -- MESA's own comment on this last line: "Seems like the hardware
 *     is backwards regarding this setting..." -- i.e. this is a real,
 *     confirmed hardware quirk (cp=TRUE when the API's front face is
 *     CCW), not an arbitrary choice; matched here exactly rather than
 *     "fixed" to look more intuitive.
 *
 * GL_FRONT culls forward-facing (front) primitives -> effp=FALSE.
 * GL_BACK culls reverse-facing (back) primitives -> erfp=FALSE.
 * GL_FRONT_AND_BACK culls both -> both FALSE (culls everything, a
 * legal/real GL_FRONT_AND_BACK result). CullFace_State GL_FALSE, the
 * default, keeps both TRUE.
 *
 * blend_enable (be) comes straight from context->Blend_State. The
 * smooth/multitextured/textured/multitex_env arguments are not read here:
 * they describe the draw shape, which nothing in this function branches on;
 * the callers pass what they already have.
 * BlendEnables/BlendCfg carry the real v3d_blend_factor values GLBlendFunc/
 * glBlendFuncSeparate store in blend_srcmode/blend_dstmode and the
 * equations glBlendEquation stores (others.c). mask=0x0F (all 4 possible
 * render target slots, per MESA's own gallium driver convention for the
 * non-independent-blend case, v3dx_state.c/v3dx_emit.c --
 * V3D_MAX_RENDER_TARGETS is a FIXED hardware maximum of 4 for V3D 4.2, not
 * "however many targets this app uses"). When blending is disabled,
 * BlendEnables(mask=0) is still emitted (matches MESA's own "always emit,
 * mask=0 means off" pattern) rather than skipped, keeping the CL packet
 * stream shape consistent. */

/* vp_sx..vp_ay: the viewport packets' UWORDs, (UWORD)(sx/sy/ax/ay * 2.0f),
 * computed by gl_EmitPrimitiveV3DEx together with the ClipWindow rectangle. */
static void gl_EmitCullBlendState(GLcontext context, GLboolean smooth, GLboolean multitextured, GLboolean textured, int multitex_env,
                                  UWORD vp_sx, UWORD vp_sy, UWORD vp_ax, UWORD vp_ay)
{
	V3DContext* backend = &context->backend;
	GLboolean effp, erfp, cp, be;
	UBYTE dtf, zue;

	if (context->CullFace_State == GL_TRUE)
	{
		effp = (context->CurrentCullFace == GL_FRONT || context->CurrentCullFace == GL_FRONT_AND_BACK) ? GL_FALSE : GL_TRUE;
		erfp = (context->CurrentCullFace == GL_BACK  || context->CurrentCullFace == GL_FRONT_AND_BACK) ? GL_FALSE : GL_TRUE;
	}
	else
	{
		effp = GL_TRUE;
		erfp = GL_TRUE;
	}
	cp = (context->CurrentFrontFace == GL_CCW) ? GL_TRUE : GL_FALSE;

	/* Every blended draw goes to the fixed-function hardware blend. The
	 * software-blend (ldtlb) shader variants are not selected anywhere; they
	 * stay in v3d_assembler.c, unreachable. */
	be = (context->Blend_State == GL_TRUE) ? GL_TRUE : GL_FALSE;

	/* Depth test function straight from GLDepthFunc (context->backend.zmode).
	 * With GL_DEPTH_TEST off the compare is ALWAYS, as GL specifies. */
	dtf = (context->DepthTest_State == GL_TRUE) ? (UBYTE)context->backend.zmode : V3D_COMPARE_FUNC_ALWAYS;

	/* MESA-exact: z_updates_enable = depth test on AND glDepthMask on
	 * (v3dx_emit.c, config.z_updates_enable = zsa->base.depth_writemask
	 * inside the depth_enabled gate). */
	zue = (context->DepthTest_State == GL_TRUE && context->DepthMask == GL_TRUE) ? TRUE : FALSE;

	{
		int colormask = (context->ColorMaskR ? 0 : 0x1) |
		                (context->ColorMaskG ? 0 : 0x2) |
		                (context->ColorMaskB ? 0 : 0x4) |
		                (context->ColorMaskA ? 0 : 0x8);
		/* MESA-style dedup (see mglv3d_dedup_cache above). */
		dd_sync();

		/* eze (early_z_enable) and ezue (early_z_updates_enable) share
		 * one condition, matching MESA's gallium driver (v3dx_emit.c),
		 * which inside its depth_enabled gate sets
		 * `config.early_z_enable = config.early_z_updates_enable;`. The
		 * one case where they differ is handled below. */
		{
			/* Accumulate this draw's early-Z direction into the frame's,
			 * MESA's v3d_update_job_ez (v3dx_draw.c) -- see ez_state's own
			 * comment (v3d_context.h) for the model. The direction itself is
			 * NOT derived here: depth_ez_dir is cached and only recomputed
			 * when glDepthFunc or GL_DEPTH_TEST changes, because deriving it
			 * per draw is real cost for a byte-identical packet. This is the
			 * hot path.
			 *
			 * The `draw_ez != ez_state` test is the fast path and carries the
			 * whole steady state: once the frame has settled on a direction
			 * and every draw agrees, this is ONE comparison and nothing else
			 * runs. The body only executes when a draw genuinely disagrees
			 * with the frame so far, which is rare. */
			UBYTE eze_ezue;
			{
				V3DContext* bk = &context->backend;
				UBYTE draw_ez = bk->depth_ez_dir;

				if (draw_ez != bk->ez_state)
				{
					if (draw_ez == V3D_EZ_DISABLED)
					{
						bk->ez_state = V3D_EZ_DISABLED;
					}
					else if (draw_ez != V3D_EZ_UNDECIDED)
					{
						if (bk->ez_state == V3D_EZ_UNDECIDED)
						{
							bk->ez_state = draw_ez;
							/* Latch the first decided direction -- that, not
							 * the running state, configures the frame's
							 * packet, so a frame conflicting later keeps the
							 * direction its early draws used. */
							if (bk->first_ez_state == V3D_EZ_UNDECIDED)
								bk->first_ez_state = draw_ez;
						}
						else
						{
							/* Two directions in one frame: V3D has only one,
							 * so the frame gives up early-Z from here on. */
							bk->ez_state = V3D_EZ_DISABLED;
						}
					}
					/* draw_ez UNDECIDED against a decided frame: compatible
					 * with either direction, so it changes nothing. */
				}

				eze_ezue = (context->DepthTest_State == GL_TRUE
				            && bk->ez_state != V3D_EZ_DISABLED) ? TRUE : FALSE;
			}

			/* Hardware depth offset, MESA-exact: for a draw with polygon
			 * offset MESA sets CFG_BITS.enable_depth_offset and emits
			 * DEPTH_OFFSET immediately after CFG_BITS (v3dx_emit.c:299 then
			 * 404-417), followed by POINT_SIZE and LINE_WIDTH (429-440) --
			 * the RASTERIZER group -- then the VIEWPORT group (442-504), then
			 * the blend packets (506-570).
			 *
			 * Plain GL state drives it: glEnable(GL_POLYGON_OFFSET_FILL) +
			 * glPolygonOffset(), or the Amiga-only glEnable(MGL_Z_OFFSET) +
			 * mglSetZOffset(). */
			GLboolean edo_want = (context->PolygonOffsetFill_State == GL_TRUE ||
			                      context->ZOffset_State == GL_TRUE) ? GL_TRUE : GL_FALSE;
			UBYTE edo = edo_want ? TRUE : FALSE;
			float edo_f = 0.0f, edo_u = 0.0f;
			UBYTE ezue = eze_ezue;

			if (edo)
			{
				/* The two entry points carry DIFFERENT quantities.
				 * glPolygonOffset already speaks the packet's language:
				 * `units` in multiples of the minimum resolvable depth
				 * difference -- pass it through. MGLSetZOffset is a DIRECT
				 * [0,1] depth delta (the Warp3D-era convention) and must be
				 * converted into MRD multiples. MGL_ZOFFSET_UNITS_SCALE is
				 * CALIBRATED, NOT DERIVED: it maps a -0.0009 depth delta, the
				 * magnitude that convention uses, onto -2.0 units, the
				 * GL-conventional magnitude. */
				#define MGL_ZOFFSET_UNITS_SCALE 2222.0f

				if (context->PolygonOffsetFill_State == GL_TRUE)
				{
					edo_f = context->PolygonOffsetFactor;
					edo_u = context->PolygonOffsetUnits;
				}
				else
				{
					edo_f = 0.0f;
					edo_u = context->ZOffset * MGL_ZOFFSET_UNITS_SCALE;
				}
			}

			/* MESA, v3dx_emit.c:345-360: early_z_updates_enable = (job ez_state
			 * != DISABLED) is set OUTSIDE the depth-test gate and early_z_enable
			 * only inside it, so a depth-off draw in a non-disabled job is
			 * (eze=0, ezue=1) with zue=0. Scoped to exactly those draws:
			 * depth test off AND no z writes. */
			if (context->DepthTest_State != GL_TRUE && !zue)
			{
				ezue = (context->backend.ez_state != V3D_EZ_DISABLED) ? TRUE : FALSE;
			}

			/* CFG_BITS: MESA re-emits on RASTERIZER|ZSA|PRIM_MODE|BLEND dirty,
			 * i.e. when any of these inputs changed; the other arguments are
			 * compile-time constants at this call site. */
			if (!s_dd.cfg_valid ||
			    s_dd.cfg_effp != (UBYTE)effp || s_dd.cfg_erfp != (UBYTE)erfp ||
			    s_dd.cfg_cp   != (UBYTE)cp   || s_dd.cfg_edo  != edo ||
			    s_dd.cfg_dtf  != dtf         || s_dd.cfg_zue  != zue ||
			    s_dd.cfg_eze  != eze_ezue    || s_dd.cfg_ezue != ezue ||
			    s_dd.cfg_be   != (UBYTE)be)
			{
				CfgBits(backend, effp, erfp, cp, edo, V3D_LINE_RASTERIZATION_DIAMOND_EXIT, 0, FALSE,
				        dtf, zue, eze_ezue, ezue, FALSE, be, FALSE, FALSE);
				s_dd.cfg_valid = 1;
				s_dd.cfg_effp = (UBYTE)effp; s_dd.cfg_erfp = (UBYTE)erfp; s_dd.cfg_cp = (UBYTE)cp;
				s_dd.cfg_edo  = edo;         s_dd.cfg_dtf  = dtf;         s_dd.cfg_zue = zue;
				s_dd.cfg_eze  = eze_ezue;    s_dd.cfg_ezue = ezue;        s_dd.cfg_be  = (UBYTE)be;
			}

			/* MESA's RASTERIZER group, in MESA's order and adjacency: CFG_BITS
			 * -> DEPTH_OFFSET (v3dx_emit.c:404-417). MESA follows with
			 * POINT_SIZE (every draw on ver==42) and LINE_WIDTH; this driver
			 * sends those once per pass from gl_EnsureDrawState, see there.
			 * The order and adjacency are kept because they are MESA's. */
			if (edo &&
			    (!s_dd.do_valid || s_dd.do_f != edo_f || s_dd.do_u != edo_u))
			{
				DepthOffset(backend, edo_f, edo_u);
				s_dd.do_valid = 1; s_dd.do_f = edo_f; s_dd.do_u = edo_u;
			}

			/* MESA's VIEWPORT group follows the rasterizer group and precedes the
			 * blend packets (v3dx_emit.c:442-504) -- always all four:
			 * CLIPPER_XY_SCALING, CLIPPER_Z_SCALE_AND_OFFSET,
			 * CLIPPER_Z_MIN_MAX_CLIPPING_PLANES, VIEWPORT_OFFSET, from the real
			 * glViewport (context->ax/ay centre, sx/sy half-extents, viewport.c).
			 * The two Z packets stay LITERAL on purpose: the render-pass vertex
			 * shaders bake the same 0.5 scale and 0.5 offset into their own zs
			 * computation (v3d_assembler.c) and the two must agree, so this is
			 * not the place a depth range can be applied.
			 *
			 * Cached as one group, like MESA's V3D_DIRTY_VIEWPORT: all four or
			 * none. */
			{
				if (!s_dd.vp_valid ||
				    s_dd.vp_sx != vp_sx || s_dd.vp_sy != vp_sy ||
				    s_dd.vp_ax != vp_ax || s_dd.vp_ay != vp_ay)
				{
					ClipperXYScaling(backend, vp_sx, vp_sy);
					ClipperZScaleAndOffset(backend, 0.5f, 0.5f);
					ClipperZMinMaxClippingPlanes(backend, 0.0f, 1.0f);
					ViewportOffset(backend, vp_ax, 0, vp_ay, 0);
					s_dd.vp_valid = 1;
					s_dd.vp_sx = vp_sx; s_dd.vp_sy = vp_sy; s_dd.vp_ax = vp_ax; s_dd.vp_ay = vp_ay;
				}
			}
		}

		/* Blend packets: MESA re-emits them on V3D_DIRTY_BLEND only. The key is
		 * the enable plus the six BLEND_CFG fields (only meaningful when on). */
		if (!s_dd.bl_valid || s_dd.bl_on != (UBYTE)be ||
		    (be && (s_dd.bl_src  != (UBYTE)backend->blend_srcmode ||
		            s_dd.bl_dst  != (UBYTE)backend->blend_dstmode ||
		            s_dd.bl_eq   != (UBYTE)backend->blend_color_equation ||
		            s_dd.bl_asrc != (UBYTE)backend->blend_alpha_srcmode ||
		            s_dd.bl_adst != (UBYTE)backend->blend_alpha_dstmode ||
		            s_dd.bl_aeq  != (UBYTE)backend->blend_alpha_equation)))
		{
		s_dd.bl_valid = 1;
		s_dd.bl_on   = (UBYTE)be;
		s_dd.bl_src  = (UBYTE)backend->blend_srcmode;
		s_dd.bl_dst  = (UBYTE)backend->blend_dstmode;
		s_dd.bl_eq   = (UBYTE)backend->blend_color_equation;
		s_dd.bl_asrc = (UBYTE)backend->blend_alpha_srcmode;
		s_dd.bl_adst = (UBYTE)backend->blend_alpha_dstmode;
		s_dd.bl_aeq  = (UBYTE)backend->blend_alpha_equation;
		if (be)
		{
			/* mask=0x0F, not 0x01 -- cross-checked against MESA's real gallium
			 * driver (v3dx_emit.c): for the common non-independent-blend case
			 * (exactly this project's situation, one framebuffer, no MRT), it
			 * sets BLEND_ENABLES.mask/BLEND_CFG.render_target_mask to
			 * (1 << V3D_MAX_RENDER_TARGETS) - 1, and V3D_MAX_RENDER_TARGETS is
			 * a FIXED HARDWARE maximum of 4 for V3D 4.2 (v3d_limits.h), not
			 * "however many render targets this app actually uses" -- i.e.
			 * 0x0F regardless of only one attachment being real. 0x01
			 * ("just RT0") does not match how the real driver programs this
			 * and produces solid black instead of a real blend. */
			BlendEnables(backend, 0x0F);
			/* Colour and alpha carry their OWN factors and equation, from
			 * glBlendFuncSeparate/glBlendEquation: the packet has six
			 * independent fields. A program that only calls glBlendFunc
			 * gets the colour pair in both halves (others.c passes it
			 * twice) and the ADD both equations start at. */
			BlendCfg(backend, 0x0F,
			         (UBYTE)backend->blend_dstmode, (UBYTE)backend->blend_srcmode,
			         (UBYTE)backend->blend_color_equation,
			         (UBYTE)backend->blend_alpha_dstmode, (UBYTE)backend->blend_alpha_srcmode,
			         (UBYTE)backend->blend_alpha_equation);
		}
		else
		{
			BlendEnables(backend, 0x00);
		}
		}

		/* Real glColorMask, per draw call rather than once per pass -- see
		 * gl_EnsureDrawState's own comment for why.
		 *
		 * Bit layout: the plain, UNSWAPPED Gallium PIPE_MASK_R/G/B/A
		 * convention (bit0=R, 1=G, 2=B, 3=A, inverted so 1=disable and
		 * 0=write-enabled). COLOR_WRITE_MASKS' bit-to-channel mapping does
		 * NOT follow this project's own shader-side R/B vfpack-slot
		 * convention; it is a separate, plain, hardware-fixed indexing. */
		if (!s_dd.cwm_valid || s_dd.cwm != colormask)
		{
			ColorWriteMasks(backend, colormask);
			s_dd.cwm_valid = 1;
			s_dd.cwm = colormask;
		}
	}
}

/*
 * `primType` (V3D_PRIM_TRIANGLES/_POINTS/_LINES/_TRIANGLEFAN/...) only
 * reaches the primitive-list packet's own mode argument -- VertexArrayPrims,
 * or IndexedPrimList on the indexed path: the attribute-record and
 * shader-binding logic below is identical whatever the primitive shape.
 * `count` is the vertex count for the whole batch.
 *
 * use_clip_space: when GL_TRUE, the emitter reads `.bx/.by/.bz` (clip-space,
 * as produced by hclip.c's clip-plane interpolation) instead of `.v.x/y/z`
 * (raw object-space), and feeds an IDENTITY matrix instead of
 * context->CombinedMatrix -- the position is already transformed, so
 * transforming it again would double-apply the matrix. Used by
 * dh_DrawPoly/dh_DrawLine (hclip.c's clipped-primitive terminal calls) for
 * vertices that only exist as clip-space interpolation results and have no
 * object-space value to hand the shader otherwise. See this file's header
 * comment for the vertex pipeline background.
 *
 * A real per-vertex w reaches the shader only where the draw is also
 * combined or multitextured: those shapes declare a 4-component position
 * record and take a clip-space coordinate shader. A flat or smooth-only
 * clip-space draw packs 3 components, so the identity matrix leaves w at 1
 * and the vertex's real .bw is not used.
 */
int g_mglv3d_frame_prim_calls = 0;
int g_mglv3d_frame_prim_verts = 0;
int g_mglv3d_frame_prim_maxcount = 0;
UBYTE g_mglv3d_frame_prim_types_seen = 0; /* bit N set = primType N used this frame */
/* Incremented once per gl_FramePresent call -- which means every real present
 * AND every intermediate pass split, since gl_FrameBegin's split branch calls
 * gl_FramePresent directly and the ++ sits above that function's own
 * frame_active early-out. So this is a BIN GENERATION, not a frame count.
 *
 * STARTS AT 1, not 0: V3DTexture.last_draw_frame uses 0 as its never-drawn
 * sentinel and is zeroed by v3d_texture_alloc's memset, so a counter starting
 * at 0 would make the first upload of every texture look like a mid-frame
 * reuse hazard. See that field's comment (v3d_texture.h). */
int g_mglv3d_frame_number = 1;

/* Raw IEEE-754 bit-pattern check (no libm isnan/isinf on this cross
 * compiler): exponent field all-1s means NaN or Inf either way -- both
 * are equally "bad" for a vertex position feeding the GPU. */
static int mglv3d_float_is_bad(float f)
{
	union { float f; LONG l; } u;
	u.f = f;
	return ((u.l >> 23) & 0xFF) == 0xFF;
}

extern v3d_u32 g_v3d_cl_generation;

/* The caller's FPCR. The driver runs under it, so every memo of an FPU result
 * carries it in its key. */
static inline ULONG d_ReadFPCR(void)
{
	ULONG v;

	__asm__ volatile ("fmove.l %%fpcr,%0" : "=d"(v));
	return v;
}

/* A float's bit pattern, read as an integer: no FPU register involved. */
static inline ULONG d_Bits(const float* f)
{
	return *(const ULONG*)f;
}

/* The caches below are driver-private: laid out normally, not under the
 * pack(1) that v3d_hw.h leaves in force. */
#pragma pack(push, 4)

/*
 * Texture state-record cache: the shader-state and sampler records
 * v3d_texture_emit_state wrote for a texture, shared by both units. Direct-
 * mapped on the V3DTexture pointer.
 *
 * The records are a pure function of the texture fields that function reads
 * (all twelve are in the entry and compared on every lookup, filter and wrap
 * included -- tex_SetFilter/tex_SetWrap change them without invalidating
 * anything) and of FPCR. The addresses stay valid for the whole control-list
 * generation: state_buf is append-only until the CL is reset, which bumps
 * g_v3d_cl_generation, and a block replaced by growth is retired only after
 * its render wait. texture_mem.hostptr in the key covers texture renaming.
 *
 * fragpair: where a [p0][p1] TMU config pair naming exactly these two records
 * was written this generation, for reuse as the whole fragment-uniform stream
 * of a combined, unfogged, non-point draw. 0 = none.
 */
#define D_TEXCACHE_SIZE 64

typedef struct
{
	V3DTexture* tex;
	v3d_u8*     sb_start;
	void*       hostptr;
	v3d_u32     gen;
	ULONG       fpcr;
	v3d_u32     level0_offset;
	ULONG       ts_addr, ss_addr;
	ULONG       fragpair;
	v3d_u16     width, height;
	v3d_u8      max_level_uploaded, tiling_format, ub_pad;
	v3d_u8      min_filter, mag_filter, mip_filter_nearest, wrap_s, wrap_t;
} d_texcache_entry;

static d_texcache_entry s_texcache[D_TEXCACHE_SIZE];

static inline d_texcache_entry* d_TexCacheSlot(const V3DTexture* tex)
{
	ULONG p = (ULONG)tex;

	return &s_texcache[((p >> 4) ^ (p >> 10)) & (D_TEXCACHE_SIZE - 1)];
}

static inline int d_TexCacheHit(const d_texcache_entry* e, const V3DTexture* tex, const v3d_u8* sb_start, ULONG fpcr)
{
	return e->tex == tex && e->gen == g_v3d_cl_generation && e->sb_start == sb_start && e->fpcr == fpcr &&
	       e->hostptr == tex->texture_mem.hostptr && e->level0_offset == tex->levels[0].offset &&
	       e->width == tex->width && e->height == tex->height &&
	       e->max_level_uploaded == tex->max_level_uploaded && e->tiling_format == tex->tiling_format &&
	       e->ub_pad == tex->ub_pad && e->min_filter == tex->min_filter && e->mag_filter == tex->mag_filter &&
	       e->mip_filter_nearest == tex->mip_filter_nearest && e->wrap_s == tex->wrap_s && e->wrap_t == tex->wrap_t;
}

/* After v3d_texture_emit_state: sb_start is read now, so a claim that grew
 * state_buf is already reflected in the key. */
static inline void d_TexCacheFill(d_texcache_entry* e, V3DTexture* tex, const v3d_static_buffer* sb, ULONG fpcr,
                                  ULONG ts_addr, ULONG ss_addr)
{
	e->tex                = tex;
	e->sb_start           = sb->start;
	e->hostptr            = tex->texture_mem.hostptr;
	e->gen                = g_v3d_cl_generation;
	e->fpcr               = fpcr;
	e->level0_offset      = tex->levels[0].offset;
	e->ts_addr            = ts_addr;
	e->ss_addr            = ss_addr;
	e->fragpair           = 0;
	e->width              = tex->width;
	e->height             = tex->height;
	e->max_level_uploaded = tex->max_level_uploaded;
	e->tiling_format      = tex->tiling_format;
	e->ub_pad             = tex->ub_pad;
	e->min_filter         = tex->min_filter;
	e->mag_filter         = tex->mag_filter;
	e->mip_filter_nearest = tex->mip_filter_nearest;
	e->wrap_s             = tex->wrap_s;
	e->wrap_t             = tex->wrap_t;
}

#pragma pack(pop)

/*
 * Evict one texture from the cache above: called when a texture is deleted
 * and when it is renamed.
 *
 * The rename (v3d_texture_rename swaps tex->texture_mem and keeps the
 * V3DTexture*) is also caught by texture_mem.hostptr in the key; the eviction
 * stays so neither mechanism depends on the other.
 *
 * DO NOT "simplify" this by bumping g_v3d_cl_generation instead. That value
 * means THE COMMAND LIST WAS RESTARTED, which a rename does not do, and
 * gl_EmitPrimitiveV3DEx's transform-feedback dedup (s_tf_gen) reads it with
 * exactly that meaning: on a generation change it resets s_tf_prim WITHOUT
 * emitting TransformFeedbackSpecs, because it assumes gl_EnsureDrawState is
 * about to. Mid-pass, gl_EnsureDrawState returns early (draw_state_configured
 * is still set), so a spurious bump would swallow a needed re-emit whenever the
 * primitive type changed across the rename.
 */
void gl_InvalidateTexStateCache(V3DTexture *tex)
{
	d_texcache_entry* e = d_TexCacheSlot(tex);

	if (e->tex == tex)
		e->tex = NULL;
}

/* Lock-aware GL_TRIANGLES (vertexelements.c, DrawLockedTriangles).
 * `indices`/`count` below then name the locked vertices one each, and this
 * carries the caller's own index list, drawn with IndexedPrimList instead of
 * VertexArrayPrims. */
typedef struct mglv3d_indexed_draw
{
	const GLvoid *indices;
	GLenum        type;
	int           nidx;
	int           first;   /* lockfirst: subtracted from every index */
} mglv3d_indexed_draw;

extern unsigned long g_mglv3d_lock_epoch;

/* Positions packed by the first indexed draw of a lock, reused by the rest.
 * Only valid for the same lock epoch, in the same control-list generation and
 * build slot, and while state_buf has neither moved nor grown: the address
 * points into that block. */
static struct
{
	FLOAT        *buf;
	unsigned long epoch;
	v3d_u32       gen;
	int           slot;
	v3d_u8       *start;
	int           capacity;
	int           nverts;
} s_lockpos = { NULL, 0, 0, 0, NULL, 0, 0 };

/* The identity index list, defined with d_AllocSeq below. */
static int *s_seq;

/* Inline copies of v3d_commands.c's CurrentBufferAddress and AlignBuffer:
 * the same arithmetic on context->current_buf, without the call. */
static inline ULONG d_CurrentBufferAddress(V3DContext* context)
{
	v3d_static_buffer* buffer = context->current_buf;

	return ((ULONG)buffer->start + buffer->used);
}

static inline void d_AlignBuffer(V3DContext* context, ULONG alignment)
{
	v3d_static_buffer* buffer = context->current_buf;

	buffer->used = (buffer->used + (alignment - 1)) & ~(alignment - 1);
}

/* Inline copies of v3d_commands.c's glShaderState, VertexArrayPrims,
 * IndexBufferSetup and IndexedPrimList. Same packet macro body (including the
 * zeroing), same field stores; only the claim's fits-case is inline, and a
 * claim that does not fit still goes through v3d_buffer_claim_memory. */
static inline void d_glShaderState(V3DContext* context, ULONG staterecordAddress, ULONG attr_count)
{
	ULONG* swivel;

	v3d_static_buffer* buffer = context->current_buf;

	V3D_BUFFER_ALLOC_OPERATION_WITH(v3d_buffer_claim_memory_fast, buffer, v3d_OP_GL_SHADER_STATE,
	                                v3d_gl_shader_state, glshaderState);

	glshaderState->address_rshift_5           = staterecordAddress >> 5;
	glshaderState->number_of_attribute_arrays = attr_count;

	swivel    = (ULONG*)((UBYTE*)glshaderState + 1);
	swivel[0] = (ULONG)LE32(swivel[0]);
}

static inline void d_VertexArrayPrims(V3DContext* context, UBYTE mode, ULONG length, ULONG index)
{
	v3d_static_buffer* buffer = context->current_buf;

	V3D_BUFFER_ALLOC_OPERATION_WITH(v3d_buffer_claim_memory_fast, buffer, v3d_OP_VERTEX_ARRAY_PRIMS,
	                                v3d_vertex_array_prims, vertexarrayPrims);

	vertexarrayPrims->mode                  = mode;
	vertexarrayPrims->length                = LE32(length);
	vertexarrayPrims->index_of_first_vertex = LE32(index);
}

static inline void d_IndexedPrimList(V3DContext* context, UBYTE mode, UBYTE indexType, ULONG length, BOOL enablePrimitiveRestarts, ULONG indexOffset)
{
	v3d_static_buffer* buffer = context->current_buf;
	ULONG temp;

	V3D_BUFFER_ALLOC_OPERATION_WITH(v3d_buffer_claim_memory_fast, buffer, v3d_OP_INDEXED_PRIM_LIST,
	                                v3d_indexed_prim_list, indexedPrimList);

	indexedPrimList->mode       = mode;
	indexedPrimList->index_type = indexType;

	temp = (length & 0x7FFFFFFF) | (enablePrimitiveRestarts ? 0x80000000 : 0);
	indexedPrimList->length_and_restart = LE32(temp);
	indexedPrimList->index_offset       = LE32(indexOffset);
}

static inline void d_IndexBufferSetup(V3DContext* context, ULONG address, ULONG size)
{
	v3d_static_buffer* buffer = context->current_buf;

	V3D_BUFFER_ALLOC_OPERATION_WITH(v3d_buffer_claim_memory_fast, buffer, v3d_OP_INDEX_BUFFER_SETUP,
	                                v3d_index_buffer_setup, indexBufferSetup);

	indexBufferSetup->address = LE32(address);
	indexBufferSetup->size    = LE32(size);
}

/* Per-vertex writers for gl_EmitPrimitiveV3DEx's unswitched pack loops. Each
 * is one arm of the per-vertex chain that function keeps for texture-matrix
 * draws: the same values into the same slots, in the same order. */
static inline void d_PackPos3(FLOAT* p, const MGLVertex* v)
{
	swap_float32_into(&p[0], v->v.x);
	swap_float32_into(&p[1], v->v.y);
	swap_float32_into(&p[2], v->v.z);
}

/* s,t,r,g then b,a -- the combined arm's order, see its R/B note. */
static inline void d_PackCombined(FLOAT* t, FLOAT* t2, const MGLVertex* v)
{
	swap_float32_into(&t[0], v->v.u0);
	swap_float32_into(&t[1], v->v.v0);
	swap_float32_into(&t[2], v->color.r);
	swap_float32_into(&t[3], v->color.g);
	swap_float32_into(&t2[0], v->color.b);
	swap_float32_into(&t2[1], v->color.a);
}

static inline void d_PackCombinedWhite(FLOAT* t, FLOAT* t2, const MGLVertex* v)
{
	swap_float32_into(&t[0], v->v.u0);
	swap_float32_into(&t[1], v->v.v0);
	swap_float32_into(&t[2], 1.0f);
	swap_float32_into(&t[3], 1.0f);
	swap_float32_into(&t2[0], 1.0f);
	swap_float32_into(&t2[1], 1.0f);
}

static inline void d_PackSmooth(FLOAT* t, const MGLVertex* v)
{
	swap_float32_into(&t[0], v->color.r);
	swap_float32_into(&t[1], v->color.g);
	swap_float32_into(&t[2], v->color.b);
	swap_float32_into(&t[3], v->color.a);
}

static inline void d_PackMulti(FLOAT* t, const MGLVertex* v)
{
	swap_float32_into(&t[0], v->v.u0);
	swap_float32_into(&t[1], v->v.v0);
	swap_float32_into(&t[2], v->v.u1);
	swap_float32_into(&t[3], v->v.v1);
}

static inline void d_PackTextured(FLOAT* t, const MGLVertex* v)
{
	swap_float32_into(&t[0], v->v.u0);
	swap_float32_into(&t[1], v->v.v0);
}

/* Set by vertexarray.c/vertexelements.c around a d_Draw* call that draws only
 * vertices their gathers have just written from a position array of fewer
 * than 4 components. Those gathers store w = q = 1.0f, so the real-w scan in
 * gl_EmitPrimitiveV3DEx would find nothing. */
int g_mglv3d_vb_affine = 0;

/* The real-w scan on bit patterns: 1.0f is 0x3F800000 and nothing else, and a
 * NaN differs from it either way, so this is `w != 1.0f || q != 1.0f`
 * without an FPU compare. */
static inline GLboolean d_HasRealW(const MGLVertex* vb, const int* indices, int count, int seq)
{
	int i;

	if (seq)
	{
		const MGLVertex* v = vb;

		for (i = 0; i < count; i++, v++)
			if ((d_Bits(&v->v.w) ^ 0x3F800000UL) | (d_Bits(&v->q) ^ 0x3F800000UL))
				return GL_TRUE;
	}
	else
	{
		for (i = 0; i < count; i++)
		{
			const MGLVertex* v = &vb[indices[i]];

			if ((d_Bits(&v->v.w) ^ 0x3F800000UL) | (d_Bits(&v->q) ^ 0x3F800000UL))
				return GL_TRUE;
		}
	}
	return GL_FALSE;
}

extern ULONG g_mglv3d_combined_serial; /* matrix.c */

/* Shape flags of a shader state record: with the two code offsets and the
 * shader code base, every input of d_BuildShaderRecord except the per-draw
 * addresses. */
#define D_SR_COMBINED         0x001UL
#define D_SR_SMOOTH_ALPHATEST 0x002UL
#define D_SR_SMOOTH           0x004UL
#define D_SR_MULTITEXTURED    0x008UL
#define D_SR_TEXTURED         0x010UL
#define D_SR_ALPHATEST        0x020UL
#define D_SR_SMOOTH_POINT     0x040UL
#define D_SR_MULTITEX_BLEND   0x080UL
#define D_SR_NEEDS_REAL_W     0x100UL

/* The template patch below relies on this layout: a 36-byte record whose
 * per-draw addresses are the whole words at 8, 16, 24 and 32, then 16-byte
 * attribute records whose address is their word 0. */
typedef char d_sr_layout_check[(sizeof(v3d_gl_shader_state_record) == 36 &&
                                sizeof(v3d_gl_shader_state_attribute_record) == 16) ? 1 : -1];

#define D_SR_MAXWORDS ((sizeof(v3d_gl_shader_state_record) + 3 * sizeof(v3d_gl_shader_state_attribute_record)) / 4)

/*
 * The shader state record and its attribute records, as gl_EmitPrimitiveV3DEx
 * has always built them, written to dst (36 + 16 * 2 or 3 bytes). The
 * attribute records go through glShaderStateAttributeRecord against a local
 * buffer over dst, which places them directly after the record exactly as
 * their claims from state_buf did.
 */
static __attribute__((noinline)) void d_BuildShaderRecord(V3DContext* backend, v3d_u8* dst, ULONG shape,
                                                          ULONG frag_code_offset, ULONG vex_code_offset,
                                                          ULONG default_attr_values_address, ULONG unif_frag_address,
                                                          ULONG unif_vex_address, ULONG unif_coord_address,
                                                          FLOAT* posbuf, FLOAT* texbuf, FLOAT* texbuf2)
{
	GLboolean combined         = (shape & D_SR_COMBINED)         ? GL_TRUE : GL_FALSE;
	GLboolean smooth_alphatest = (shape & D_SR_SMOOTH_ALPHATEST) ? GL_TRUE : GL_FALSE;
	GLboolean smooth           = (shape & D_SR_SMOOTH)           ? GL_TRUE : GL_FALSE;
	GLboolean multitextured    = (shape & D_SR_MULTITEXTURED)    ? GL_TRUE : GL_FALSE;
	GLboolean textured         = (shape & D_SR_TEXTURED)         ? GL_TRUE : GL_FALSE;
	GLboolean alphatest        = (shape & D_SR_ALPHATEST)        ? GL_TRUE : GL_FALSE;
	GLboolean smooth_point     = (shape & D_SR_SMOOTH_POINT)     ? GL_TRUE : GL_FALSE;
	GLboolean multitex_blend   = (shape & D_SR_MULTITEX_BLEND)   ? GL_TRUE : GL_FALSE;
	GLboolean needs_real_w     = (shape & D_SR_NEEDS_REAL_W)     ? GL_TRUE : GL_FALSE;
	v3d_gl_shader_state_record* shader = (v3d_gl_shader_state_record*)dst;
	v3d_static_buffer attrs;
	v3d_static_buffer* saved_buf = backend->current_buf;
	ULONG* swivel;
	int i;

	/* Only about half this struct's real fields are explicitly assigned
	 * below (min_vertex/coordinate_shader_input/output_segments_required_*,
	 * turn_off_scoreboard, the per-shader instance/vertex-id read flags and
	 * more are never touched), and state_buf's addresses are reused across
	 * frames and draw calls without ever being re-zeroed. Without this
	 * memset a record built at a given state_buf offset picks up stale
	 * VPM-scheduling bits left there by a DIFFERENT draw call -- a
	 * different shader variant, with different segment-size needs. Same
	 * class as v3d_texture_shader_state/sampler_state; see
	 * backend/hw/v3d_texture.c's own comment. */
	memset(shader, 0, sizeof(*shader));

	shader->enable_clipping = TRUE;
	shader->fragment_shader_uses_real_pixel_centre_w_in_addition_to_centroid_w2 = TRUE;
	/* Only the smooth-point shaders read the implicit point coordinate; on any
	 * other shader the two extra varyings would shift every read. */
	shader->disable_implicit_point_line_varyings = smooth_point ? FALSE : TRUE;
	shader->number_of_varyings_in_fragment_shader = (combined || smooth_alphatest) ? 6 : (smooth ? 4 : (multitextured ? 4 : (textured ? 2 : 0)));

	/* MESA sets this from prog_data.fs->lock_scoreboard_on_first_thrsw
	 * (v3dx_draw.c:490 and :583), and sets that flag true whenever a thrsw is
	 * emitted after a TLB load has already been emitted (nir_to_vir.c:167-172).
	 * Meaning: "do not wait until the LAST thread switch to take the scoreboard
	 * lock -- take it on the FIRST one", which is required when the shader
	 * reads the tile buffer, because the read must not happen before the lock.
	 * An unlocked ldtlb shows up as a tile-boundary "sprinkle" artifact.
	 *
	 * Set for exactly the shaders that read the TLB (ldtlb), as MESA does --
	 * locking the scoreboard earlier than needed costs inter-thread
	 * parallelism on every other draw. The only ldtlb consumer the dispatch
	 * still names is multitex_blend, which no draw selects, so in practice
	 * this is FALSE throughout. The ldtlb shaders in v3d_assembler.c keep
	 * their thrsw-before-ldtlb prologue; the field only chooses which thread
	 * switch takes the lock, so one half is inert without the other. */
	shader->do_scoreboard_wait_on_first_thread_switch = multitex_blend ? 1 : 0;

	/* PASSTHROUGH DEPTH WRITE. Every alphatest shader variant carries a
	 * `tlbu` Z write, and so do the four smooth-point shaders.
	 *
	 * fragment_shader_does_z_writes tells the hardware the FEP must STOP
	 * writing depth because the QPU will do it instead. Setting it on a
	 * shader that has no tlbu write would mean NO depth gets written at
	 * all -- breaking depth in the opposite direction -- so it must stay
	 * exactly in step with which shaders carry the instruction.
	 *
	 * turn_off_early_z_test is MESA's prog_data.fs->disable_ez: set for a
	 * fragment shader that writes Z or discards. This driver has no compiler
	 * to derive it, so it goes with the same predicate. On its own it fixes
	 * nothing, because it only moves WHEN the FEP writes, not who writes;
	 * the pair is what matters. The CfgBits eze/ezue pair is deliberately
	 * NOT widened to match: MESA keeps early_z_enable on for exactly this
	 * shader shape (a passthrough INVARIANT Z write is writes_z_from_fep,
	 * which v3dx_draw.c leaves ez_state enabled for), so eze=1 alongside
	 * turn_off_early_z_test=1 is the reference driver's own normal state,
	 * not a contradiction. */
	{
		/* `alphatest` and `smooth_alphatest` are exactly the two umbrella
		 * predicates that select an alphatest shader, so they map 1:1 onto
		 * the shaders that have the instruction -- which is the lockstep
		 * this flag requires. Setting it for a shader WITHOUT a tlbu write
		 * would stop depth being written at all. */
		/* `alphatest` stays true for a combined fog+alphatest draw (the fog
		 * predicate does not exclude it), and every combined fog+alphatest
		 * shader carries the same `or tlbu` write, so this predicate covers
		 * them -- no extra term needed. fog && !alphatest selects a fog-only
		 * shader, which has NO tlbu, and this stays FALSE for it. */
		/* The four smooth-point shaders carry the same tlbu write (they
		 * discard outside the disc). */
		GLboolean zwrite_shader = (alphatest || smooth_alphatest || smooth_point) ? GL_TRUE : GL_FALSE;
		shader->turn_off_early_z_test      = zwrite_shader ? TRUE : FALSE;
		shader->fragment_shader_does_z_writes = zwrite_shader ? TRUE : FALSE;
	}
	shader->coordinate_shader_output_vpm_segment_size = 1;
	shader->coordinate_shader_input_vpm_segment_size = 1;
	shader->vertex_shader_output_vpm_segment_size = 2;
	/* A deliberate deviation from MESA, which on V3D 4.2 never uses separate
	 * input/output VPM segments -- vir.c:837-848 folds the input into the
	 * output segment and v3dx_draw.c:613-618 sends a literal 1 here, paired
	 * with VCM_CACHE_SIZE before every GL_SHADER_STATE. */
	shader->vertex_shader_input_vpm_segment_size =
		(combined || smooth_alphatest) ? 2 : 1;
	shader->address_of_default_attribute_values = LE32(default_attr_values_address);

	shader->fragment_shader_code_address_rshift_3 = (ULONG)((ULONG)backend->shader_code_mem.hostptr + frag_code_offset) >> 3;
	shader->fragment_shader_uniforms_address = LE32(unif_frag_address);
	shader->fragment_shader_4_way_threadable = TRUE;
	shader->fragment_shader_start_in_final_thread_section = FALSE;
	shader->fragment_shader_propagate_nans = TRUE;

	shader->vertex_shader_code_address_rshift_3 = (ULONG)((ULONG)backend->shader_code_mem.hostptr + vex_code_offset) >> 3;
	shader->vertex_shader_uniforms_address = LE32(unif_vex_address);
	shader->vertex_shader_4_way_threadable = TRUE;
	shader->vertex_shader_start_in_final_thread_section = TRUE;
	shader->vertex_shader_propagate_nans = TRUE;

	/* A needs_real_w draw routes to the real-w coordinate shader (+15360,
	 * COORDINATE_CLIPSPACE) -- position-only, generic across every variant,
	 * unchanged by which vertex shader is paired with it -- instead of the
	 * shared COORDINATE_TEXTURED (+1024) every other variant reuses. Must
	 * agree with posbuf's own 4-component widening below, since the
	 * coordinate and vertex shaders consume the same attribute records. */
	shader->coordinate_shader_code_address_rshift_3 = (ULONG)((ULONG)backend->shader_code_mem.hostptr + (needs_real_w ? 15360 : 1024)) >> 3;
	shader->coordinate_shader_uniforms_address = LE32(unif_coord_address);
	shader->coordinate_shader_4_way_threadable = TRUE;
	shader->coordinate_shader_start_in_final_thread_section = TRUE;
	shader->coordinate_shader_propagate_nans = TRUE;

	swivel = (ULONG*)shader;
	swivel[3] = LE32(swivel[3]);
	swivel[5] = LE32(swivel[5]);
	swivel[7] = LE32(swivel[7]);

	attrs.start      = dst + sizeof(v3d_gl_shader_state_record);
	attrs.used       = 0;
	attrs.capacity   = 3 * sizeof(v3d_gl_shader_state_attribute_record);
	attrs.overflowed = 0;
	backend->current_buf = &attrs;

	/* needs_real_w declares posbuf as a real 4-component record (x,y,z,w)
	 * instead of 3 -- both novrbvs/novrbcs (values read by vertex/coordinate
	 * shader) go to 4, since BOTH real-w shaders read the real w, and this
	 * shifts every later attribute record's VPM offset by +1, matching
	 * g_coordinate_shader_clipspace_assembly/
	 * g_vertex_shader_smooth_textured_clipspace_assembly/
	 * g_vertex_shader_multitexture_clipspace_assembly's own shifted
	 * ldvpmv_in offsets (v3d_assembler.c). */
	if (needs_real_w)
	{
		glShaderStateAttributeRecord(backend, posbuf, FALSE, FALSE, FALSE, v3d_VEC_4,
		                              v3d_ATTRIBUTE_FLOAT, 4, 4, 0, (4 * sizeof(float)), 0xFFFFFF);
	}
	else
	{
		glShaderStateAttributeRecord(backend, posbuf, FALSE, FALSE, FALSE, v3d_VEC_3,
		                              v3d_ATTRIBUTE_FLOAT, 3, 3, 0, (3 * sizeof(float)), 0xFFFFFF);
	}
	/* attr1 carries 4 components (r,g,b,a) when smooth instead of 2 (s,t).
	 * A combined draw declares attr1 as a real 4-component record (s,t,r,g)
	 * plus a SEPARATE attr2 record of 2 components (b,a), never one
	 * 6-component record: the hardware's vec_size field is only 2 bits, so
	 * 4 is the genuine maximum, and asking for 6 is undefined behaviour that
	 * corrupts the 3rd real component. See texbuf's own claim-site comment
	 * for the layout this produces. */
	i = combined ? 4 : (smooth ? 4 : (multitextured ? 4 : 2));
	glShaderStateAttributeRecord(backend, texbuf, FALSE, FALSE, FALSE, v3d_VEC_4,
	                              v3d_ATTRIBUTE_FLOAT, i, 0, 0, (i * sizeof(float)), 0xFFFFFF);
	if (combined || smooth_alphatest)
	{
		glShaderStateAttributeRecord(backend, texbuf2, FALSE, FALSE, FALSE, v3d_VEC_2,
		                              v3d_ATTRIBUTE_FLOAT, 2, 0, 0, (2 * sizeof(float)), 0xFFFFFF);
	}

	backend->current_buf = saved_buf;
}

/* Driver-private memo records, laid out normally (see the texture cache). */
#pragma pack(push, 4)

/* The shader state record template: d_BuildShaderRecord's output for one
 * shape with every per-draw address 0. The per-draw words are whole LE32
 * words that no other field and no swivel touches (record words 2, 4, 6 and
 * 8, word 0 of each attribute record), so the template with those words
 * patched is the record d_BuildShaderRecord writes for the draw. Keyed on
 * the builder's inputs; the generation and state_buf block are in the key
 * too, though the template itself names no state_buf address. */
typedef struct
{
	v3d_u8* sb_start;
	void*   code_base;
	v3d_u32 gen;
	ULONG   frag_code_offset, vex_code_offset, shape;
	int     valid;
} d_srec_key;

/* The default-attribute block: its contents never change
 * (g_default_values_buff is only read), so one block serves a whole
 * control-list generation and state_buf block. */
typedef struct
{
	v3d_u8* sb_start;
	v3d_u32 gen;
	ULONG   addr;
	int     valid;
} d_defattr_memo;

/* The vertex and coordinate uniform blocks. Their 80 bytes are a function of
 * CombinedMatrix (tracked by g_mglv3d_combined_serial: m_CombineMatrices is
 * its only writer and bumps it), use_clip_space, sx, sy, sz, az and FPCR
 * (sx*2.0f rounds only on overflow, but it is an FPU result). */
typedef struct
{
	v3d_u8* sb_start;
	v3d_u32 gen;
	ULONG   serial, fpcr, sx, sy, sz, az;
	ULONG   vu, cu;
	int     clip, valid;
} d_vu_memo;

/* The ClipWindow rectangle and the viewport UWORDs: pure functions of these
 * inputs, the float-to-integer conversions rounding under FPCR. */
typedef struct
{
	v3d_u8* sb_start;
	v3d_u32 gen;
	ULONG   fpcr, sx, sy, ax, ay;
	v3d_u16 width, height, sc_x, sc_y, sc_w, sc_h;
	UWORD   cw_l, cw_b, cw_w, cw_h;
	UWORD   vp_sx, vp_sy, vp_ax, vp_ay;
	v3d_u8  sc_en, valid;
} d_rect_memo;

/* The four fixed-colour uniform words, byte-swapped: a function of
 * fixed_color and FPCR (the divides round under it). */
typedef struct
{
	v3d_u8* sb_start;
	v3d_u32 gen;
	ULONG   fpcr, fixed_color;
	ULONG   words[4];
	int     valid;
} d_color_memo;

/* The eight fog uniform words, byte-swapped: a function of the fog mode,
 * start, end, density, colour and FPCR. */
typedef struct
{
	v3d_u8* sb_start;
	v3d_u32 gen;
	ULONG   fpcr, start, end, density;
	ULONG   words[8];
	v3d_u8  mode, r, g, b;
	int     valid;
} d_fog_memo;

#pragma pack(pop)

static d_srec_key     s_srec;
static ULONG          s_srec_tpl[D_SR_MAXWORDS];
static d_defattr_memo s_defattr;
static d_vu_memo      s_vum;
static d_rect_memo    s_rectm;
static d_color_memo   s_colm;
static d_fog_memo     s_fogm;

/* Copies the memoized fixed-colour words to dst when the key matches. */
static inline int d_ColorMemoCopy(ULONG* dst, const V3DContext* backend, const v3d_u8* sb_start, ULONG fpcr)
{
	if (s_colm.valid && s_colm.gen == g_v3d_cl_generation && s_colm.sb_start == sb_start &&
	    s_colm.fpcr == fpcr && s_colm.fixed_color == backend->fixed_color)
	{
		dst[0] = s_colm.words[0];
		dst[1] = s_colm.words[1];
		dst[2] = s_colm.words[2];
		dst[3] = s_colm.words[3];
		return 1;
	}
	return 0;
}

/* Memoizes the four words just computed at src. */
static inline void d_ColorMemoStore(const ULONG* src, const V3DContext* backend, v3d_u8* sb_start, ULONG fpcr)
{
	s_colm.words[0]    = src[0];
	s_colm.words[1]    = src[1];
	s_colm.words[2]    = src[2];
	s_colm.words[3]    = src[3];
	s_colm.fixed_color = backend->fixed_color;
	s_colm.fpcr        = fpcr;
	s_colm.gen         = g_v3d_cl_generation;
	s_colm.sb_start    = sb_start;
	s_colm.valid       = 1;
}

/* Copies the memoized fog words to dst when the key matches. */
static inline int d_FogMemoCopy(ULONG* dst, const V3DContext* backend, const v3d_u8* sb_start, ULONG fpcr)
{
	int k;

	if (s_fogm.valid && s_fogm.gen == g_v3d_cl_generation && s_fogm.sb_start == sb_start && s_fogm.fpcr == fpcr &&
	    s_fogm.start == d_Bits(&backend->fog_start) && s_fogm.end == d_Bits(&backend->fog_end) &&
	    s_fogm.density == d_Bits(&backend->fog_density) && s_fogm.mode == backend->fog_mode &&
	    s_fogm.r == backend->fog_r && s_fogm.g == backend->fog_g && s_fogm.b == backend->fog_b)
	{
		for (k = 0; k < 8; k++)
			dst[k] = s_fogm.words[k];
		return 1;
	}
	return 0;
}

/* Memoizes the eight words just computed at src. */
static inline void d_FogMemoStore(const ULONG* src, const V3DContext* backend, v3d_u8* sb_start, ULONG fpcr)
{
	int k;

	for (k = 0; k < 8; k++)
		s_fogm.words[k] = src[k];
	s_fogm.start    = d_Bits(&backend->fog_start);
	s_fogm.end      = d_Bits(&backend->fog_end);
	s_fogm.density  = d_Bits(&backend->fog_density);
	s_fogm.mode     = backend->fog_mode;
	s_fogm.r        = backend->fog_r;
	s_fogm.g        = backend->fog_g;
	s_fogm.b        = backend->fog_b;
	s_fogm.fpcr     = fpcr;
	s_fogm.gen      = g_v3d_cl_generation;
	s_fogm.sb_start = sb_start;
	s_fogm.valid    = 1;
}

/* The ClipWindow rectangle (out[0..3] = left, top, width, height) and the
 * viewport packet UWORDs (out[4..7] = sx, sy, ax, ay doubled). Out of line so
 * its float temporaries do not cost gl_EmitPrimitiveV3DEx a saved FP register
 * on every draw; gl_EmitPrimitiveV3DEx memoizes the result (s_rectm). */
static __attribute__((noinline)) void d_ClipRectCompute(GLcontext context, UWORD* out)
{
	V3DContext* backend = &context->backend;
	LONG cw_l, cw_b, cw_r, cw_t;
	LONG vp_l, vp_r, vp_t, vp_b;
	float hx = context->sx < 0.0f ? -context->sx : context->sx;
	float hy = context->sy < 0.0f ? -context->sy : context->sy;

	/* NAMING TRAP, read before editing: cw_b is named for ClipWindow's
	 * "bottom pixel coordinate" field, but that field is top-origin here
	 * (see below), so cw_b holds the rectangle's TOP edge and cw_t holds
	 * its BOTTOM edge. The vp_/sc_ locals use t=top, b=bottom the normal
	 * way round. Hence the deliberate-looking cw_b = max(sc_t, vp_t)
	 * pairings: they are correct, not transposed. */

	vp_l = (LONG)(context->ax - hx);
	vp_r = (LONG)(context->ax + hx);
	vp_t = (LONG)(context->ay - hy);
	vp_b = (LONG)(context->ay + hy);

	if (backend->scissor_enable)
	{
		/* ClipWindow's y is TOP-origin on this hardware, despite the
		 * packet field being named "bottom pixel coordinate". Feeding it
		 * a bottom-origin y flips the rectangle about the drawable.
		 *
		 * GLScissor already stores the rect top-origin (it computes
		 * height - y - h from GL's bottom-origin y), so pass it
		 * straight through rather than converting back. */
		LONG sc_l = (LONG)backend->scissor_x;
		LONG sc_t = (LONG)backend->scissor_y;
		LONG sc_r = sc_l + (LONG)backend->scissor_w;
		LONG sc_b = sc_t + (LONG)backend->scissor_h;

		cw_l = (sc_l > vp_l) ? sc_l : vp_l;
		cw_b = (sc_t > vp_t) ? sc_t : vp_t;
		cw_r = (sc_r < vp_r) ? sc_r : vp_r;
		cw_t = (sc_b < vp_b) ? sc_b : vp_b;
	}
	else
	{
		cw_l = vp_l;
		cw_b = vp_t;
		cw_r = vp_r;
		cw_t = vp_b;
	}

	/* Clamp to the drawable last, so neither the viewport nor the scissor
	 * can push the binner's rectangle off the render target. */
	if (cw_l < 0) cw_l = 0;
	if (cw_b < 0) cw_b = 0;
	if (cw_r > (LONG)backend->width)  cw_r = (LONG)backend->width;
	if (cw_t > (LONG)backend->height) cw_t = (LONG)backend->height;

	out[0] = (UWORD)cw_l;
	out[1] = (UWORD)cw_b;
	out[2] = (UWORD)((cw_r > cw_l) ? (cw_r - cw_l) : 0);
	out[3] = (UWORD)((cw_t > cw_b) ? (cw_t - cw_b) : 0);

	/* gl_EmitCullBlendState's viewport packet values (viewport.c's centre and
	 * half-extents, doubled). */
	out[4] = (UWORD)(context->sx * 2.0f);
	out[5] = (UWORD)(context->sy * 2.0f);
	out[6] = (UWORD)(context->ax * 2.0f);
	out[7] = (UWORD)(context->ay * 2.0f);
}


static void gl_EmitPrimitiveV3DEx(GLcontext context, int* indices, int count, UBYTE primType, GLboolean use_clip_space, int chunk_size, const mglv3d_indexed_draw *ix)
{
	V3DContext* backend = &context->backend;
	UWORD* idxbuf = NULL;
	ULONG idxbytes = 0;
	GLboolean pos_cached = GL_FALSE;

	/* Once any earlier draw this frame has marked it corrupted (a state_buf
	 * grow mid-sequence -- see the check further down in this same
	 * function), every subsequent draw call in the frame is dropped here
	 * immediately: no buffer claims, no binning emission, nothing.
	 * gl_FramePresent refuses to submit this frame regardless, and skipping
	 * the work also keeps a frame that needs more room after the first grow
	 * from triggering more of them. Self-clears next frame --
	 * frame_corrupted only resets in gl_FrameBegin. */
	if (backend->frame_corrupted)
		return;

	g_mglv3d_frame_prim_calls++;
	g_mglv3d_frame_prim_verts += count;
	if (count > g_mglv3d_frame_prim_maxcount) g_mglv3d_frame_prim_maxcount = count;
	if (primType < 8) g_mglv3d_frame_prim_types_seen |= (1 << primType);
	ULONG default_attr_values_address;
	FLOAT* posbuf;
	FLOAT* texbuf;
	FLOAT* texbuf2; /* combined and smooth_alphatest only: b,a split out of
	                 * texbuf -- see texbuf's own claim site for why. */
	ULONG unif_vex_address, unif_coord_address, unif_frag_address;
	int unif_frag_pre_capacity;
	v3d_my_uniforms* vu;
	v3d_my_uniforms* cu;
	FLOAT* col;
	float scale_p;
	float scale_p_y;
	ULONG staterecordAddress;
	v3d_gl_shader_state_record* shader;
	ULONG* swivel;
	int i;
	GLboolean textured;
	V3DTexture* bound_tex;
	ULONG textureShaderStateAddress = 0, textureSamplerStateAddress = 0;
	GLboolean smooth;
	GLboolean combined;
	GLboolean fog;
	GLboolean alphatest;
	GLboolean smooth_alphatest;
	/* Which alphatest shader slot the current alpha_func selects, for each
	 * of the shapes that carry an alpha test. Only meaningful when the
	 * matching umbrella predicate is GL_TRUE, and only evaluated then. */
	ULONG alphatest_code_offset = 0;
	ULONG smooth_alphatest_code_offset = 0;
	ULONG fog_alphatest_code_offset = 0;
	ULONG smooth_fog_alphatest_code_offset = 0;
	GLboolean multitextured;
	V3DTexture* bound_tex2;
	ULONG textureShaderStateAddress2 = 0, textureSamplerStateAddress2 = 0;
	ULONG vex_code_offset, frag_code_offset;
	GLboolean replace_white;
	GLboolean clipspace_combined;
	GLboolean clipspace_multitextured;
	GLboolean needs_real_w;
	GLboolean draw_has_real_w;
	GLboolean real_w_combined;
	GLboolean multitex_blend;
	int multitex_env;
	GLboolean smooth_point;
	/* GL_TEXTURE -- see v_TexMatrixActive for why the texture matrix is
	 * applied on the way into the attribute buffer and nowhere earlier.
	 * `texmat_on` is resolved once, below. */
	volatile v_TexMatrix texmat;
	int texmat_on;
	/* This draw's state_buf/state_mem slot, loaded once: nothing this function
	 * calls writes build_slot, and GCC could not know that, so it recomputed
	 * both addresses for every claim. */
	v3d_static_buffer* sb;
	v3d_mem* sm;
	MGLVertex* vb;
	int seq;   /* indices is the identity list s_seq */
	ULONG fpcr;
	d_texcache_entry* tex0_entry = NULL;

	/* gl_EnsureShaders returns 0 at once when shaders_ready is set; the flag
	 * is tested here so a ready context makes no call. */
	if (!backend->shaders_ready && gl_EnsureShaders(context) != 0)
	{
		D(("gl_EmitPrimitiveV3D: gl_EnsureShaders failed, dropping\n"));
		return;
	}

	/* Every cache below keys on g_v3d_cl_generation, NOT build_slot.
	 * build_slot is a 2-slot toggle, and a cache only ever sees the slot
	 * values that a DRAW observes. An intermediate render pass (context.c's
	 * doing_intermediate_pass path) flips the slot mid-frame; if no draw
	 * follows it in that frame, the end-of-frame flip returns the slot to the
	 * value already cached, and the next frame would reuse addresses into a
	 * state_buf that has since been reset. g_v3d_cl_generation is monotonic
	 * and bumped at every CL reset and at both flip sites, so no sequence of
	 * flips can return a value a cache has already seen. It is also
	 * process-global and never reset, so it survives a context being torn
	 * down and reopened. The state_buf block's start is in every key too. */
	fpcr = d_ReadFPCR();


	sb = &backend->state_buf[backend->build_slot];
	sm = &backend->state_mem[backend->build_slot];
	backend->current_buf = sb;

	/*
	 * `textured` gates every texture-specific piece below (attr1 content,
	 * fragment uniforms, varying count, which fragment shader code address is
	 * used) -- matching PrepTexCoords' own original gating condition
	 * (Texture2D_State[0] + a non-NULL bound texture object), minus the
	 * Warp3D-specific texel-space conversion that condition also gates in the
	 * original (see PrepTexCoords' own comment on why that is not ported). The
	 * texture's shader/sampler state records come from the texture
	 * state-record cache (s_texcache) when this generation already holds
	 * them, and from v3d_texture_emit_state otherwise.
	 */
	bound_tex = (context->Texture2D_State[0] == GL_TRUE)
	            ? context->textureObjects[context->CurrentBinding] : NULL;
	textured = (bound_tex != NULL) ? GL_TRUE : GL_FALSE;

	if (textured)
	{
		d_texcache_entry* e = d_TexCacheSlot(bound_tex);

		if (d_TexCacheHit(e, bound_tex, sb->start, fpcr))
		{
			textureShaderStateAddress = e->ts_addr;
			textureSamplerStateAddress = e->ss_addr;
		}
		else
		{
			v3d_texture_emit_state(&context->device, backend, bound_tex,
			                        &textureShaderStateAddress, &textureSamplerStateAddress);
			d_TexCacheFill(e, bound_tex, sb, fpcr, textureShaderStateAddress, textureSamplerStateAddress);
		}
		tex0_entry = e;
	}

	/* Multitexture -- unit 1's own bound texture, same gating shape as unit
	 * 0's `bound_tex`/`textured` above (Texture2D_State[1] + a non-NULL bound
	 * object at VirtualBinding, texture.c's own name for unit 1's binding,
	 * matching GLBindTexture's ActiveTexture==1 branch). `multitextured`
	 * requires BOTH units bound -- one unit alone just uses the plain
	 * single-texture path. */
	bound_tex2 = (context->Texture2D_State[1] == GL_TRUE)
	             ? context->textureObjects[context->VirtualBinding] : NULL;
	multitextured = (textured && bound_tex2 != NULL) ? GL_TRUE : GL_FALSE;

	if (multitextured)
	{
		d_texcache_entry* e = d_TexCacheSlot(bound_tex2);

		if (d_TexCacheHit(e, bound_tex2, sb->start, fpcr))
		{
			textureShaderStateAddress2 = e->ts_addr;
			textureSamplerStateAddress2 = e->ss_addr;
		}
		else
		{
			v3d_texture_emit_state(&context->device, backend, bound_tex2,
			                        &textureShaderStateAddress2, &textureSamplerStateAddress2);
			d_TexCacheFill(e, bound_tex2, sb, fpcr, textureShaderStateAddress2, textureSamplerStateAddress2);
		}
	}

	/* Combine-mode dispatch for multitexture. multitex_env reads unit 1's OWN
	 * texenv_mode (bound_tex2), which MGLDrawMultitexBuffer sets from its
	 * TexEnv argument and glTexEnvi sets through tex_SetEnv when unit 1 is
	 * the active texture unit (texture.c) -- 0=MODULATE,
	 * 1=DECAL, 2=REPLACE, matching V3D_TEXENV_* ordinal values
	 * (v3d_texture.h).
	 *
	 * multitex_blend is permanently GL_FALSE. It selects the "_blend"
	 * fragment shader variants, which read the tile buffer via `ldtlb` and
	 * blend against it entirely in software; the fixed-function hardware
	 * blend (`be`, gl_EmitCullBlendState) is the single source of truth
	 * instead. MGLDrawMultitexBuffer calls GLBlendFunc internally
	 * (texture.c), setting the same blend_srcmode/blend_dstmode/Blend_State
	 * fields gl_EmitCullBlendState reads, so selecting a software-blend
	 * shader here as well would DOUBLE-blend: once in the shader via ldtlb,
	 * once again via hardware blend on top of that already-blended output.
	 * Multitextured draws therefore always take the plain (non-blend)
	 * MODULATE/DECAL/REPLACE combine shader. The `_blend` variants stay in
	 * v3d_assembler.c, unreachable from here. */
	multitex_env = 0;
	multitex_blend = GL_FALSE;
	if (multitextured)
	{
		if (bound_tex2)
		{
			if (bound_tex2->texenv_mode == V3D_TEXENV_DECAL)
				multitex_env = 1;
			else if (bound_tex2->texenv_mode == V3D_TEXENV_REPLACE)
				multitex_env = 2;
		}
	}

	/*
	 * GL_SMOOTH (per-vertex colour). `combined` (both textured AND smooth)
	 * has its own shader pair (VERTEX_SMOOTH_TEXTURED/
	 * FRAGMENT_TEXTURED_SMOOTH, GL_MODULATE -- texture sampled, then
	 * multiplied by the per-vertex colour). */
	smooth = (context->ShadeModel == GL_SMOOTH) ? GL_TRUE : GL_FALSE;
	/* `smooth_alphatest` is computed HERE, before `combined` just below,
	 * because `combined` excludes it: a smooth+textured draw with the alpha
	 * test enabled has to reach the alpha-discard fragment shader rather
	 * than the plain smooth-textured one. It mirrors `alphatest`'s own
	 * umbrella/selector relationship exactly: the umbrella predicate says
	 * whether this SHAPE carries an alpha test at all (and so drives every
	 * structural sibling -- varying count, VPM segment size, the second
	 * attribute record, glShaderState's attribute count, `combined`,
	 * real_w_combined, and the fragment uniform stream), while the companion
	 * `smooth_alphatest_code_offset` / `alphatest_code_offset` selects WHICH
	 * compare function's shader to dispatch.
	 *
	 * GL_ALWAYS is excluded here rather than being given its own shader,
	 * because it IS "alpha test disabled" -- and clearing the umbrella
	 * predicate is the only safe way to express that. Every structural
	 * sibling below reads `smooth_alphatest` (the varying count, the VPM
	 * segment size, the second attribute record, glShaderState's attribute
	 * count, `combined`, real_w_combined) and this function's fragment
	 * UNIFORM stream is likewise built from these same predicates, so
	 * clearing it keeps all of them in lockstep automatically. Selecting a
	 * different shader while leaving the predicate set would desynchronise
	 * the uniform stream from frag_code_offset. */
	smooth_alphatest = (textured && smooth && !multitextured &&
	           context->backend.alpha_func != V3D_ALPHAFUNC_ALWAYS &&
	           context->backend.alpha_test_enable == GL_TRUE) ? GL_TRUE : GL_FALSE;
	/* Every compare function has its own textured+smooth shader. GL_ALWAYS
	 * cannot reach here (excluded above); the default arm keeps GEQUAL purely
	 * as a defensive fallback for an out-of-range alpha_func, which
	 * GLAlphaFunc's own GL_INVALID_ENUM check should already have rejected.
	 * Read only when smooth_alphatest, so evaluated only then; likewise the
	 * three switches below under their own predicates. */
	if (smooth_alphatest)
	switch (context->backend.alpha_func)
	{
		case V3D_ALPHAFUNC_NEVER:    smooth_alphatest_code_offset = 50176; break;
		case V3D_ALPHAFUNC_LESS:     smooth_alphatest_code_offset = 53248; break;
		case V3D_ALPHAFUNC_EQUAL:    smooth_alphatest_code_offset = 56320; break;
		case V3D_ALPHAFUNC_LEQUAL:   smooth_alphatest_code_offset = 59392; break;
		case V3D_ALPHAFUNC_GREATER:  smooth_alphatest_code_offset = 43008; break;
		case V3D_ALPHAFUNC_NOTEQUAL: smooth_alphatest_code_offset = 62464; break;
		case V3D_ALPHAFUNC_GEQUAL:   smooth_alphatest_code_offset = 44032; break;
		default:                     smooth_alphatest_code_offset = 44032; break;
	}
	/* `combined` excludes `multitextured` and `smooth_alphatest` by
	 * construction, the same mutually-exclusive-by-construction pattern
	 * `alphatest` uses. Every ternary below checks `combined` before
	 * `multitextured`, so without the exclusion the single-texture combined
	 * shader would win every time and unit 1's texture would never be sampled
	 * by the GPU, however correctly it is bound in GL state. */
	combined = (smooth && textured && !multitextured && !smooth_alphatest) ? GL_TRUE : GL_FALSE;

	/* Clip-space vertices (dh_DrawPoly/dh_DrawLine, use_clip_space==TRUE)
	 * need a REAL per-vertex w (`.bw`) instead of every other variant's
	 * hardcoded w=1.0 -- see this function's own header comment and
	 * v3d_assembler.c's comments on g_coordinate_shader_clipspace_assembly/
	 * g_vertex_shader_smooth_textured_clipspace_assembly for the design.
	 * Only the combined (smooth+textured) shape takes that path here; the
	 * multitextured one has its own flag just below. Any other
	 * use_clip_space case -- flat, smooth-only, and smooth+textured with the
	 * alpha test on -- falls back to the
	 * w=1.0-hardcoded shaders below, which is correct as long as w really is
	 * 1 -- it is, for a purely affine ModelView with no projective
	 * transform. */
	clipspace_combined = (use_clip_space && combined) ? GL_TRUE : GL_FALSE;

	/* The same real-w path, for multitextured clip-space vertices -- see
	 * g_vertex_shader_multitexture_clipspace_assembly's own comment
	 * (v3d_assembler.c). Mutually exclusive with clipspace_combined by
	 * construction, since `combined` excludes `multitextured` above. */
	clipspace_multitextured = (use_clip_space && multitextured) ? GL_TRUE : GL_FALSE;

	/* Real w for ordinary (non-clip-space) combined draws: an app that calls
	 * glVertex4f with a genuine w != 1.0 (GLVertex2f/GLVertex3fv always route
	 * through GLVertex4f with a hardcoded 1.0f, so this is a
	 * zero-false-positive signal -- see vertexbuffer_min.c) gets the same
	 * real-w coordinate/vertex shader pair the clip-space path above uses,
	 * verbatim; it is correct for any CombinedMatrix, not only an identity or
	 * affine one. Recomputed fresh every draw call, with no cached or sticky
	 * flag -- the same "rebuild from real GL state" convention fog_Set uses.
	 * Only `combined` and its `smooth_alphatest` refinement are covered;
	 * every other shape hardcodes w=1.0.
	 *
	 * Real per-pixel q perspective correction: this also triggers on a real
	 * `.q` (glTexCoord4f; GLTexCoord2f always resets `.q` to 1.0f, the same
	 * zero-false-positive discipline as `.v.w`). An application that carries
	 * its perspective factor in q and then calls glVertex3f leaves `.v.w` at
	 * 1.0, so `.q` is that caller's only carrier for it. Both need the exact
	 * same shader and CL layout, so real_w_combined covers both rather than a
	 * second flag family. */
	/* Hoisted for the scan below and the pack loops: under
	 * -fno-strict-aliasing GCC reloaded VertexBuffer for every vertex,
	 * although no store here can change it. When indices is s_seq, s_seq[i]
	 * == i for every i below count (d_AllocSeq; callers never pass more than
	 * d_SeqCount), so the loops can walk the vertices directly. */
	vb = context->VertexBuffer;
	seq = (indices == s_seq);

	draw_has_real_w = GL_FALSE;
	if ((combined || smooth_alphatest) && !use_clip_space)
	{
		/* Vertices an array gather has just written from a position of fewer
		 * than 4 components all hold w = q = 1.0f (g_mglv3d_vb_affine). */
		if (!g_mglv3d_vb_affine)
			draw_has_real_w = d_HasRealW(vb, indices, count, seq);
	}
	/* `smooth_alphatest` belongs here alongside `combined` because it is a
	 * REFINEMENT of it -- same vertex-side shader and CL layout, different
	 * fragment shader -- not something orthogonal to it. Without
	 * `|| smooth_alphatest`, any smooth+textured draw with GL_ALPHA_TEST
	 * enabled would lose the real-w/q perspective mechanism entirely
	 * (needs_real_w collapses to false) and fall back to affine mapping. */
	real_w_combined = (!use_clip_space && (combined || smooth_alphatest) && draw_has_real_w) ? GL_TRUE : GL_FALSE;

	needs_real_w = (clipspace_combined || clipspace_multitextured || real_w_combined) ? GL_TRUE : GL_FALSE;

	/* GL_FOG and GL_ALPHA_TEST. fog_Set's sync (fog.c) is done fresh here,
	 * the same "rebuild from real GL state every draw call" pattern already
	 * used for smooth/textured above, so context->backend.fog_enable/
	 * fog_start/fog_end reflect glEnable(GL_FOG)/glFogf(GL_FOG_START/END).
	 * alpha_test_enable/alpha_ref need no such sync step -- MGLSetState and
	 * GLAlphaFunc (context.c/others.c) write directly into context->backend;
	 * no separate "commit" function exists for those the way fog_Set exists
	 * for fog. */
	/* fog_Set's three stores, inline. The two double-to-float conversions
	 * are made only for a fogged draw: fog_start/fog_end are read only
	 * under `fog` below, which is exactly fog_enable != 0. */
	backend->fog_enable = context->Fog_State;
	if (backend->fog_enable)
	{
		backend->fog_start = (float)context->FogStart;
		backend->fog_end = (float)context->FogEnd;
	}


	/* The flat shape's alpha test. `!smooth` is what separates it from
	 * `smooth_alphatest` above, which covers the smooth+textured shape.
	 * Neither predicate covers a smooth UNTEXTURED draw, and both carry
	 * `!multitextured`, so a multitextured draw gets no alpha test either:
	 * there is no shader variant for either shape, so such a draw gets no
	 * alpha-test discard whatever the app's GL_ALPHA_TEST state says. A dedicated shader variant would be the
	 * fix, not relaxing this condition.
	 *
	 * GL_ALWAYS is excluded for the same reason as in the smooth shape above:
	 * it is definitionally "alpha test disabled", and clearing this umbrella
	 * predicate is what keeps the fragment uniform stream in lockstep with
	 * frag_code_offset. */
	alphatest = (!smooth && !multitextured &&
	             context->backend.alpha_func != V3D_ALPHAFUNC_ALWAYS &&
	             context->backend.alpha_test_enable) ? GL_TRUE : GL_FALSE;
	/* Every compare function dispatches to its own shader pair, untextured
	 * and textured. GL_ALWAYS cannot reach here (excluded above); the default
	 * arm is a defensive GEQUAL fallback for an out-of-range alpha_func that
	 * GLAlphaFunc should already have rejected. */
	if (alphatest)
	switch (context->backend.alpha_func)
	{
		case V3D_ALPHAFUNC_NEVER:    alphatest_code_offset = textured ? 49152 : 48128; break;
		case V3D_ALPHAFUNC_LESS:     alphatest_code_offset = textured ? 52224 : 51200; break;
		case V3D_ALPHAFUNC_EQUAL:    alphatest_code_offset = textured ? 55296 : 54272; break;
		case V3D_ALPHAFUNC_LEQUAL:   alphatest_code_offset = textured ? 58368 : 57344; break;
		case V3D_ALPHAFUNC_GREATER:  alphatest_code_offset = textured ? 24576 : 23552; break;
		case V3D_ALPHAFUNC_NOTEQUAL: alphatest_code_offset = textured ? 61440 : 60416; break;
		case V3D_ALPHAFUNC_GEQUAL:   alphatest_code_offset = textured ? 11264 : 9216;  break;
		default:                     alphatest_code_offset = textured ? 11264 : 9216;  break;
	}
	/* No shape exclusions: every primitive shape this driver can draw has a
	 * fog shader -- flat and smooth, untextured, textured and multitextured,
	 * with and without alpha test -- so fog depends only on whether the app
	 * asked for it. */
	fog = (context->backend.fog_enable) ? GL_TRUE : GL_FALSE;

	/* Smooth fog + alpha test slot, same shape as the flat fog_alphatest
	 * switch. Textured only -- smooth_alphatest itself requires textured,
	 * so there is no untextured arm to pick. */
	if (fog && smooth_alphatest)
	switch (context->backend.alpha_func)
	{
		case V3D_ALPHAFUNC_NEVER:    smooth_fog_alphatest_code_offset = 86016; break;
		case V3D_ALPHAFUNC_LESS:     smooth_fog_alphatest_code_offset = 81920; break;
		case V3D_ALPHAFUNC_EQUAL:    smooth_fog_alphatest_code_offset = 82944; break;
		case V3D_ALPHAFUNC_LEQUAL:   smooth_fog_alphatest_code_offset = 83968; break;
		case V3D_ALPHAFUNC_GREATER:  smooth_fog_alphatest_code_offset = 80896; break;
		case V3D_ALPHAFUNC_NOTEQUAL: smooth_fog_alphatest_code_offset = 84992; break;
		case V3D_ALPHAFUNC_GEQUAL:   smooth_fog_alphatest_code_offset = 79872; break;
		default:                     smooth_fog_alphatest_code_offset = 79872; break;
	}

	/* Combined fog + alpha test slot, same shape as the alphatest switch
	 * above. Only consulted when (fog && alphatest); there is no smooth arm
	 * because `alphatest` requires !smooth -- the smooth shape's own slot is
	 * smooth_fog_alphatest_code_offset just above. */
	if (fog && alphatest)
	switch (context->backend.alpha_func)
	{
		case V3D_ALPHAFUNC_NEVER:    fog_alphatest_code_offset = textured ? 76800 : 75776; break;
		case V3D_ALPHAFUNC_LESS:     fog_alphatest_code_offset = textured ? 68608 : 67584; break;
		case V3D_ALPHAFUNC_EQUAL:    fog_alphatest_code_offset = textured ? 70656 : 69632; break;
		case V3D_ALPHAFUNC_LEQUAL:   fog_alphatest_code_offset = textured ? 72704 : 71680; break;
		case V3D_ALPHAFUNC_GREATER:  fog_alphatest_code_offset = textured ? 66560 : 65536; break;
		case V3D_ALPHAFUNC_NOTEQUAL: fog_alphatest_code_offset = textured ? 74752 : 73728; break;
		case V3D_ALPHAFUNC_GEQUAL:   fog_alphatest_code_offset = textured ? 64512 : 63488; break;
		default:                     fog_alphatest_code_offset = textured ? 64512 : 63488; break;
	}

	/* GL_POINT_SMOOTH: a points draw in one of the four base shapes takes a
	 * disc shader (slots 110..113, v3d_assembler.c). With fog, an active
	 * alpha test or multitexture it keeps the square shader -- the
	 * alpha test is read from the GL enable itself, so an untextured smooth
	 * point (which has no alpha-test shader) is left square too. Evaluated
	 * left to right: any other primitive stops at the first compare.
	 * Wherever it is TRUE, four things must follow together: the fragment
	 * offset below, the two uniform words after the alphatest block, the
	 * record's implicit point varyings and its Z-write pair. */
	smooth_point = (primType == V3D_PRIM_POINTS && context->PointSmooth_State == GL_TRUE &&
	                !fog && !multitextured &&
	                !(context->backend.alpha_test_enable && context->backend.alpha_func != V3D_ALPHAFUNC_ALWAYS))
	               ? GL_TRUE : GL_FALSE;

	/* Default attribute values, from g_default_values_buff -- unused attribute
	 * slots fall back to these. The block never changes, so one serves the
	 * whole control-list generation and state_buf block (s_defattr). */
	if (s_defattr.valid && s_defattr.gen == g_v3d_cl_generation && s_defattr.sb_start == sb->start)
	{
		default_attr_values_address = s_defattr.addr;
	}
	else
	{
		FLOAT* uscl = (FLOAT*)v3d_cl_claim_fast(&context->device, sm, sb,
		                                         (64 / 4) * V3D_ARRAY_SIZE(g_default_values_buff) * sizeof(float), &backend->frame);
		size_t ii, jj;
		FLOAT* p = uscl;
		/* Address from the CLAIMED pointer, not
		 * CurrentBufferAddress() taken before the claim: if this claim grows
		 * state_buf, the data lands at the start of the NEW block while the
		 * pre-claim address still pointed at the end of the old one, and the
		 * frame is not dropped here. Same value when nothing grows. */
		default_attr_values_address = (ULONG)uscl;
		/* The block never changes (g_default_values_buff is only read), so
		 * it is byte-swapped once into a template and copied from then on:
		 * the same 256 bytes the per-float loop below produces. */
		{
			static ULONG s_default_values_swapped[(64 / 4) * V3D_ARRAY_SIZE(g_default_values_buff)];
			static int s_default_values_ready = 0;

			if (!s_default_values_ready)
			{
				p = (FLOAT*)s_default_values_swapped;
				for (ii = 0; ii < 64 / 4; ii++)
				{
					for (jj = 0; jj < V3D_ARRAY_SIZE(g_default_values_buff); jj++)
						swap_float32_into(&p[jj], g_default_values_buff[jj]);
					p += 4;
				}
				s_default_values_ready = 1;
			}
			memcpy(uscl, s_default_values_swapped, sizeof(s_default_values_swapped));
		}
		/* Keyed on the block the claim returned into. */
		s_defattr.addr     = default_attr_values_address;
		s_defattr.gen      = g_v3d_cl_generation;
		s_defattr.sb_start = sb->start;
		s_defattr.valid    = 1;
	}

	/* Position attribute (attr0) -- raw object-space, from `.v.x/y/z`
	 * (vertexbuffer_min.c's GLVertex4f), NOT `.bx/.by/.bz`, which is
	 * clip-space. use_clip_space flips this to `.bx/.by/.bz` for
	 * dh_DrawPoly/dh_DrawLine's clip-interpolated vertices -- see this
	 * function's own header comment.
	 *
	 * needs_real_w widens posbuf to 4 real components (x,y,z,w) so the
	 * real-w coordinate/vertex shaders can read a genuine w instead of the
	 * hardcoded 1.0 every other variant's path relies on. Its three inputs
	 * are mutually exclusive by construction, so this one check covers all
	 * of them. */
	/* Lock-aware draw: a later draw of the same lock reuses the positions the
	 * first one packed (s_lockpos). */
	if (ix != NULL && !needs_real_w && !use_clip_space &&
	    s_lockpos.buf != NULL &&
	    s_lockpos.epoch == g_mglv3d_lock_epoch &&
	    s_lockpos.gen == g_v3d_cl_generation &&
	    s_lockpos.slot == backend->build_slot &&
	    s_lockpos.start == sb->start &&
	    s_lockpos.capacity == sb->capacity &&
	    s_lockpos.nverts == count)
	{
		posbuf = s_lockpos.buf;
		pos_cached = GL_TRUE;
	}
	else
	{
		posbuf = (FLOAT*)v3d_cl_claim_fast(&context->device, sm, sb, count * (needs_real_w ? 4 : 3) * sizeof(float), &backend->frame);
	}

	if (!pos_cached)
	{
	/* Homogeneous scale for the real-w path. At 1.0 the clip-space
	 * coordinates handed to the binner have exactly the magnitude an
	 * ordinary glFrustum application produces. A uniform factor k is free to
	 * choose in exact arithmetic -- the perspective divide and the hardware's
	 * perspective-correct varying interpolation both cancel it -- so this is
	 * kept as a named factor, computed once per draw, and can be re-tuned in
	 * one place. */
	float real_w_scale = 1.0f;

	if (!use_clip_space && !needs_real_w)
	{
		/* The common case in its own loop: object-space x,y,z, exactly what
		 * the loop below writes for it, without its per-vertex clip-space and
		 * real-w tests or the q compare only the real-w path uses. */
		FLOAT* p = posbuf;

		if (seq)
		{
			const MGLVertex* v = vb;

			for (i = 0; i < count; i++, v++, p += 3)
				d_PackPos3(p, v);
		}
		else
		{
			for (i = 0; i < count; i++, p += 3)
				d_PackPos3(p, &vb[indices[i]]);
		}
	}
	else
	{
	for (i = 0; i < count; i++)
	{
		MGLVertex* v = &vb[indices[i]];
		float px, py, pz, pw;
		if (use_clip_space)
		{
			px = v->bx; py = v->by; pz = v->bz; pw = v->bw;
			if (needs_real_w)
			{
				swap_float32_into(&posbuf[i*4+0], v->bx);
				swap_float32_into(&posbuf[i*4+1], v->by);
				swap_float32_into(&posbuf[i*4+2], v->bz);
				swap_float32_into(&posbuf[i*4+3], v->bw);
			}
			else
			{
				swap_float32_into(&posbuf[i*3+0], v->bx);
				swap_float32_into(&posbuf[i*3+1], v->by);
				swap_float32_into(&posbuf[i*3+2], v->bz);
			}
		}
		else
		{
			/* Prefer `.q` (glTexCoord4f) over `.v.w` (glVertex4f) when
			 * `.q` is real -- a caller that sets its perspective factor
			 * with glTexCoord4f and then calls glVertex3f leaves `.v.w`
			 * at 1.0, so `.q` is its only carrier. This is a per-VERTEX
			 * choice, not a per-draw one, so a batch mixing
			 * glVertex4f-real-w vertices with glTexCoord4f-real-q
			 * vertices is handled correctly without a second flag family.
			 *
			 * `.q` is a RECIPROCAL depth (1/w), not w itself, unlike
			 * glVertex4f's w, which IS true w and feeds this mechanism
			 * as-is. Feeding q straight through would make w_s (the
			 * shader's output w) equal q = 1/w, so `recip` = 1/w_s would
			 * come out as w instead of 1/w -- perspective correction with
			 * an inverted depth factor, which overshoots worse than no
			 * correction at all. True w = 1/q is recovered here first. */
			GLboolean w_from_q = (v->q != 1.0f) ? GL_TRUE : GL_FALSE;
			float real_w = w_from_q ? (1.0f / v->q) : v->v.w;
			px = v->v.x; py = v->v.y; pz = v->v.z; pw = real_w;
			if (needs_real_w)
			{
				/* real_w_combined: the app supplied a genuine w via
				 * glVertex4f or a genuine q via glTexCoord4f -- feed it
				 * through, same as the clip-space branch above. See this
				 * function's own comment on draw_has_real_w/
				 * real_w_combined for why this is correct for any
				 * CombinedMatrix.
				 *
				 * A glVertex4f caller has already pre-multiplied x,y,z by
				 * its own real w (that is what a homogeneous coordinate
				 * is), so the `else` arm applies only real_w_scale to
				 * them. A glTexCoord4f caller has NOT: it submits plain
				 * x,y,z via glVertex3f, unrelated to q. Feeding the
				 * recovered w through without also pre-multiplying x,y,z
				 * would leave the matrix multiply's translation term
				 * (glOrtho's non-zero M30, say) scaled by it instead of
				 * held fixed, so screen_x would come out divided by an
				 * extra factor. Multiplying x,y,z by the same w here
				 * reconstructs a genuine homogeneous coordinate: the factor
				 * cancels exactly in the position result, identical to the
				 * w=1 case, while w_s stays proportional to it so recip
				 * still drives texcoord perspective correction.
				 *
				 * real_w_scale multiplies the whole homogeneous coordinate
				 * (x*k, y*k, z*k, w*k), which represents the same point for
				 * any nonzero k -- both the perspective divide (z_s/w_s)
				 * and the hardware's perspective-correct varying
				 * interpolation, which uses the same 1/w_s factor in
				 * numerator and denominator, cancel a uniform k exactly.
				 * Shrinking k therefore shrinks every intermediate term of
				 * the QPU's matrix multiply proportionally without changing
				 * the mathematical result, which matters because float32
				 * precision is relative to magnitude and the subtractions
				 * inherent in a projection matrix's row-sum can leave a
				 * large absolute error behind after cancellation. Applied
				 * to BOTH branches uniformly: a caller's own
				 * pre-multiplication is just as valid a homogeneous
				 * coordinate to rescale further as this branch's. */
				if (w_from_q)
				{
					/* `(x * real_w_scale) / q` is mathematically
					 * `x * (1/q) * real_w_scale`, but it reaches the
					 * same result in two roundings per component instead
					 * of three, by never materialising the separately
					 * rounded reciprocal for this GPU-bound write. */
					swap_float32_into(&posbuf[i*4+0], (v->v.x * real_w_scale) / v->q);
					swap_float32_into(&posbuf[i*4+1], (v->v.y * real_w_scale) / v->q);
					swap_float32_into(&posbuf[i*4+2], (v->v.z * real_w_scale) / v->q);
					swap_float32_into(&posbuf[i*4+3], real_w_scale / v->q);
				}
				else
				{
					swap_float32_into(&posbuf[i*4+0], v->v.x * real_w_scale);
					swap_float32_into(&posbuf[i*4+1], v->v.y * real_w_scale);
					swap_float32_into(&posbuf[i*4+2], v->v.z * real_w_scale);
					swap_float32_into(&posbuf[i*4+3], real_w * real_w_scale);
				}

			}
			else
			{
				swap_float32_into(&posbuf[i*3+0], v->v.x);
				swap_float32_into(&posbuf[i*3+1], v->v.y);
				swap_float32_into(&posbuf[i*3+2], v->v.z);
			}
		}

	}
	}

	}

	/* First indexed draw of a lock: keep what was just packed. Recorded after
	 * the claim, so a grow inside it is already reflected in start/capacity. */
	if (ix != NULL && !pos_cached && !needs_real_w && !use_clip_space)
	{
		s_lockpos.buf      = posbuf;
		s_lockpos.epoch    = g_mglv3d_lock_epoch;
		s_lockpos.gen      = g_v3d_cl_generation;
		s_lockpos.slot     = backend->build_slot;
		s_lockpos.start    = sb->start;
		s_lockpos.capacity = sb->capacity;
		s_lockpos.nverts   = count;
	}

	/* Second attribute (attr1) -- the vertex/coordinate shader mnemonics are
	 * identical across every variant and structurally expect this slot bound
	 * whether or not the fragment shader consumes it (an untextured fragment
	 * shader has 0 varyings, so the values here do not affect its result).
	 * Its content: real per-vertex s/t (from `.v.u0/.v0`, GLTexCoord2f/4f,
	 * vertexbuffer_min.c) when textured; real per-vertex r/g/b/a (from
	 * `.color`, the CPU-working float struct vertexbuffer_min.c's GLVertex4f
	 * populates) when smooth, 4 floats instead of 2; s/t/r/g (4 floats) when
	 * combined, with b/a moved to a SECOND attribute (texbuf2); 4 floats
	 * (u0,v0,u1,v1, MGLVertex_t's own two independent texcoord pairs,
	 * MAX_TEXUNIT==2) when multitextured, matching VERTEX_MULTITEXTURE's own
	 * input layout; dummy zeros (2 floats, the textured/flat layout) when
	 * none of the above.
	 *
	 * A combined draw's six real floats per vertex (s,t,r,g,b,a) must NOT go
	 * into one attribute record: the hardware's attribute-record "vec_size"
	 * field is only 2 BITS wide, so 4 components is the absolute maximum one
	 * record can represent (v3d_packet.xml's "GL Shader State Attribute
	 * Record", "Vec size" field, size=2 bits), and asking for 6 is undefined
	 * behaviour that lands on the 3rd real component in practice. Hence the
	 * split into texbuf (4 real floats: s,t,r,g) and texbuf2 (2 real floats:
	 * b,a). VPM slot numbering is unaffected -- attr1's 4 components still
	 * land on slots 3-6 and attr2's 2 components on slots 7-8, exactly where
	 * the vertex shader expects them; only the attribute-record declarations
	 * differ. */
	i = combined ? 4 : (smooth ? 4 : (multitextured ? 4 : 2));
	/* `smooth_alphatest` shares `combined`'s vertex shader verbatim (both
	 * dispatch VERTEX_SMOOTH_TEXTURED, vex_code_offset 6144 -- see
	 * frag_code_offset/vex_code_offset's own ternary chains below), which
	 * unconditionally reads a 9-word input (pos + s,t + r,g,b,a), so it needs
	 * the SAME 2-attribute-record layout as `combined`, not the 1-record
	 * colour-only layout the generic `smooth && !multitextured` branch below
	 * provides. Without the second record the vertex shader reads colour
	 * where it expects s/t, and reads unallocated memory for 2 of its 4
	 * colour inputs.
	 *
	 * ONE claim covering both records, not two separate ones: v3d_cl_claim_
	 * grow can reallocate and repoint state_buf on overflow (see its own
	 * header comment, v3d_clbuf.c). Between two separate claims, the first
	 * claim's pointer (texbuf) would go stale -- still a valid, writable
	 * pointer, since the old block is not freed until frame end, but into a
	 * block state_buf no longer references, so v3d_context_flush_for_dma's
	 * CachePreDMA, which flushes the CURRENT state_buf.start/.used, would
	 * never make the CPU's writes through it visible to the GPU, while
	 * texbuf2's writes into the new block would be fine. A single claim makes
	 * growth atomic with respect to this attribute pair. */
	if (combined || smooth_alphatest)
	{
		texbuf = (FLOAT*)v3d_cl_claim_fast(&context->device, sm, sb,
		                                    count * (i + 2) * sizeof(float), &backend->frame);
		texbuf2 = texbuf + (count * i);
	}
	else
	{
		texbuf = (FLOAT*)v3d_cl_claim_fast(&context->device, sm, sb,
		                                    count * i * sizeof(float), &backend->frame);
		texbuf2 = NULL;
	}

	/* GL_TEXTURE_ENV_MODE for unit 0, set on the texture object by texture.c's
	 * tex_SetEnv. `combined` (smooth+textured, the branch below)
	 * unconditionally multiplies the sampled texel by the per-vertex colour,
	 * which is correct for GL_MODULATE but NOT for GL_REPLACE/GL_DECAL, and
	 * no separate REPLACE fragment shader exists. So white (1,1,1,1) is fed
	 * into the same modulate shader in place of the true vertex colour when
	 * REPLACE or DECAL is requested -- texture * white == texture, i.e. true
	 * replace, with no new shader variant. GL_DECAL takes the same
	 * white-multiply path; it is identical to REPLACE for a texture with no
	 * alpha channel. */
	replace_white = (bound_tex && (bound_tex->texenv_mode == V3D_TEXENV_REPLACE ||
	                                bound_tex->texenv_mode == V3D_TEXENV_DECAL)) ? GL_TRUE : GL_FALSE;

	/* One test for the whole draw. Nothing below reads texmat unless this is
	 * non-zero, so the coefficients it leaves behind for an identity matrix are
	 * never used. */
	texmat_on = v_TexMatrixActive(context, &texmat);

	/* Every predicate below is constant for the draw, so the shape is chosen
	 * once and each shape has its own loop. The per-vertex chain at the end
	 * is kept for texture-matrix draws (their volatile coefficients stay in
	 * that loop), and it is the reference for what each loop writes. */
	if (!texmat_on && (combined || smooth_alphatest))
	{
		FLOAT* t = texbuf;
		FLOAT* t2 = texbuf2;

		if (seq)
		{
			const MGLVertex* v = vb;

			if (replace_white)
				for (i = 0; i < count; i++, v++, t += 4, t2 += 2)
					d_PackCombinedWhite(t, t2, v);
			else
				for (i = 0; i < count; i++, v++, t += 4, t2 += 2)
					d_PackCombined(t, t2, v);
		}
		else if (replace_white)
		{
			for (i = 0; i < count; i++, t += 4, t2 += 2)
				d_PackCombinedWhite(t, t2, &vb[indices[i]]);
		}
		else
		{
			for (i = 0; i < count; i++, t += 4, t2 += 2)
				d_PackCombined(t, t2, &vb[indices[i]]);
		}
	}
	else if (!texmat_on && smooth && !multitextured)
	{
		FLOAT* t = texbuf;

		if (seq)
		{
			const MGLVertex* v = vb;

			for (i = 0; i < count; i++, v++, t += 4)
				d_PackSmooth(t, v);
		}
		else
		{
			for (i = 0; i < count; i++, t += 4)
				d_PackSmooth(t, &vb[indices[i]]);
		}
	}
	else if (!texmat_on && multitextured)
	{
		FLOAT* t = texbuf;

		if (seq)
		{
			const MGLVertex* v = vb;

			for (i = 0; i < count; i++, v++, t += 4)
				d_PackMulti(t, v);
		}
		else
		{
			for (i = 0; i < count; i++, t += 4)
				d_PackMulti(t, &vb[indices[i]]);
		}
	}
	else if (!texmat_on && textured)
	{
		FLOAT* t = texbuf;

		if (seq)
		{
			const MGLVertex* v = vb;

			for (i = 0; i < count; i++, v++, t += 2)
				d_PackTextured(t, v);
		}
		else
		{
			for (i = 0; i < count; i++, t += 2)
				d_PackTextured(t, &vb[indices[i]]);
		}
	}
	else if (!texmat_on)
	{
		/* Untextured flat: two zero words per vertex, as the chain's last arm. */
		FLOAT* t = texbuf;

		for (i = 0; i < count; i++, t += 2)
		{
			swap_float32_into(&t[0], 0.0f);
			swap_float32_into(&t[1], 0.0f);
		}
	}
	else
	{
	for (i = 0; i < count; i++)
	{
		MGLVertex* v = &vb[indices[i]];

		if (combined || smooth_alphatest)
		{
			/* `combined` and `smooth_alphatest` share
			 * VERTEX_SMOOTH_TEXTURED, so both need the same s,t + r,g,b,a
			 * layout here -- see this function's own texbuf2-allocation
			 * comment above. */
			swap_float32_into(&texbuf[i*4+0], TEXMAT_S(texmat_on, texmat, v));
			swap_float32_into(&texbuf[i*4+1], TEXMAT_T(texmat_on, texmat, v));

			if (replace_white)
			{
				swap_float32_into(&texbuf[i*4+2], 1.0f);
				swap_float32_into(&texbuf[i*4+3], 1.0f);
				swap_float32_into(&texbuf2[i*2+0], 1.0f);
				swap_float32_into(&texbuf2[i*2+1], 1.0f);
			}
			else
			{
				/* Natural r,g,b,a order into attribute slots 5,6,7,8 --
				 * the same order the vertex shader's own ldvpmv_in
				 * comments use, and the same order the non-combined
				 * branch below writes.
				 *
				 * Both sides of this are straight: the vertex shader is a
				 * pure passthrough (ldvpmv_in 5,6,7,8 -> stvpmv 6,7,8,9,
				 * no reordering), and the fragment shaders that consume
				 * these varyings modulate rf7 x rf20 and rf9 x rf22, so
				 * each reads the channel it names. Writing b,g,r,a here
				 * while crossing the modulate operands in the shader
				 * emits the same colour -- the two transpositions cancel
				 * -- but changing ONE side alone inverts red and blue
				 * across every textured+smooth fragment shader this
				 * branch's guard can select. */
				swap_float32_into(&texbuf[i*4+2], v->color.r);
				swap_float32_into(&texbuf[i*4+3], v->color.g);
				swap_float32_into(&texbuf2[i*2+0], v->color.b);
				swap_float32_into(&texbuf2[i*2+1], v->color.a);
			}
		}
		/* `!multitextured` is what keeps this branch from winning over the
		 * `multitextured` branch below when both are true, which would write
		 * the per-vertex COLOUR into texbuf instead of unit 1's real
		 * texcoords (u1,v1) -- the same mutually-exclusive-by-construction
		 * rule `combined` above follows. */
		else if (smooth && !multitextured)
		{
			/* This branch is exclusively the plain untextured-smooth case,
			 * which dispatches VERTEX_SMOOTH + FRAGMENT_UNTEXTURED_SMOOTH.
			 * Both read and write colour as plain sequential r,g,b,a (see
			 * v3d_assembler.c's g_vertex_shader_smooth_assembly: ldvpmv_in
			 * rf11-15 labelled r,g,b,a in order;
			 * g_fragment_shader_untextured_smooth_assembly: 4 sequential
			 * ldvary calls straight to rf7-10, no crossing), so this pairing
			 * needs no swap. */
			swap_float32_into(&texbuf[i*4+0], v->color.r);
			swap_float32_into(&texbuf[i*4+1], v->color.g);
			swap_float32_into(&texbuf[i*4+2], v->color.b);
			swap_float32_into(&texbuf[i*4+3], v->color.a);
		}
		else if (multitextured)
		{
			/* Unit 0 only -- see v_TexMatrixActive's own comment on why there
			 * is one texture matrix and it is unit 0's. */
			swap_float32_into(&texbuf[i*4+0], TEXMAT_S(texmat_on, texmat, v));
			swap_float32_into(&texbuf[i*4+1], TEXMAT_T(texmat_on, texmat, v));
			swap_float32_into(&texbuf[i*4+2], v->v.u1);
			swap_float32_into(&texbuf[i*4+3], v->v.v1);
		}
		else if (textured)
		{
			swap_float32_into(&texbuf[i*2+0], TEXMAT_S(texmat_on, texmat, v));
			swap_float32_into(&texbuf[i*2+1], TEXMAT_T(texmat_on, texmat, v));
		}
		else
		{
			swap_float32_into(&texbuf[i*2+0], 0.0f);
			swap_float32_into(&texbuf[i*2+1], 0.0f);
		}
	}
	}


	/* Lock-aware draw: the caller's indices, rebased to the locked range and
	 * byte-swapped for the GPU, padded to a 4-byte boundary. A grow inside
	 * this claim would leave posbuf/texbuf in the abandoned block, so it drops
	 * the frame the way the uniform sequence below does. */
	if (ix != NULL)
	{
		int k;
		int cap_before = sb->capacity;
		/* In locals: GCC reloaded both from *ix for every index. */
		const int nidx = ix->nidx;
		const int first = ix->first;

		idxbytes = ((ULONG)nidx * 2 + 3) & ~3UL;
		idxbuf = (UWORD*)v3d_cl_claim_fast(&context->device, sm, sb,
		                                   idxbytes, &backend->frame);
		if (sb->capacity != cap_before)
		{
			E(("gl_EmitPrimitiveV3D: state_buf grew while claiming the index buffer -- marking frame corrupted, dropping\n"));
			backend->frame_corrupted = TRUE;
			return;
		}

		switch (ix->type)
		{
			case GL_UNSIGNED_BYTE:
			{
				const GLubyte *p = (const GLubyte *)ix->indices;
				for (k = 0; k < nidx; k++)
					idxbuf[k] = LE16((UWORD)((int)p[k] - first));
				break;
			}
			case GL_UNSIGNED_SHORT:
			{
				const GLushort *p = (const GLushort *)ix->indices;
				for (k = 0; k < nidx; k++)
					idxbuf[k] = LE16((UWORD)((int)p[k] - first));
				break;
			}
			default:
			{
				const GLuint *p = (const GLuint *)ix->indices;
				for (k = 0; k < nidx; k++)
					idxbuf[k] = LE16((UWORD)(p[k] - (GLuint)first));
				break;
			}
		}

		if (idxbytes > (ULONG)nidx * 2)
			idxbuf[nidx] = 0;

	}

	/* Fragment uniforms -- a TMU config parameter pair (texture-state +
	 * sampler-state addresses, from v3d_texture_emit_state above) when
	 * textured; otherwise the flat fixed_color (context->backend.fixed_color,
	 * synced by vertexbuffer_min.c's GLColor3f/4f), unpacked to float. */
	unif_frag_address = d_CurrentBufferAddress(backend);
	/* Snapshot state_buf's capacity right here, before the multi-claim
	 * sequence below (TMU config pairs, colour, fog, alphatest, point size)
	 * that all append to the SAME uniform stream this address points at. If
	 * any of those claims triggers a grow, state_buf's buf->start moves to a
	 * new block -- this captured address still points at the old one, which
	 * the deferred-free keeps alive but which no longer receives the LATER
	 * writes in this same sequence: they land in the new block instead.
	 * Checked right before this draw's own commit point, below. */
	unif_frag_pre_capacity = sb->capacity;
	if (textured)
	{
		/* A combined, unfogged, non-point draw's whole fragment-uniform stream
		 * is this pair, and the pair is a function of the two record
		 * addresses alone. So one written for these records in this
		 * generation and state_buf block is reused (texture cache fragpair). */
		d_texcache_entry* pe = (combined && !fog && !smooth_point && tex0_entry != NULL &&
		                        tex0_entry->tex == bound_tex && tex0_entry->sb_start == sb->start &&
		                        tex0_entry->ts_addr == textureShaderStateAddress &&
		                        tex0_entry->ss_addr == textureSamplerStateAddress) ? tex0_entry : NULL;

		if (pe != NULL && pe->fragpair != 0)
		{
			unif_frag_address = pe->fragpair;
		}
		else
		{
		v3d_tmu_config_parameter_0* p0 = (v3d_tmu_config_parameter_0*)v3d_cl_claim_fast(
			&context->device, sm, sb, sizeof(v3d_tmu_config_parameter_0), &backend->frame);
		v3d_tmu_config_parameter_1* p1;

		p0->return_words_of_texture_data = 3;
		p0->texture_state_address_rshift_4 = textureShaderStateAddress >> 4;
		swivel = (ULONG*)p0;
		swivel[0] = LE32(swivel[0]);

		p1 = (v3d_tmu_config_parameter_1*)v3d_cl_claim_fast(
			&context->device, sm, sb, sizeof(v3d_tmu_config_parameter_1), &backend->frame);
		/* per_pixel_mask_enable/unnormalized_coordinates/output_type_32_bit
		 * MUST be assigned explicitly: v3d_cl_claim_grow's memory is not
		 * zeroed per allocation (only a one-time pool memset at context
		 * init), so an unassigned bit reads back whatever was previously in
		 * that CL buffer region. If leftover content set
		 * unnormalized_coordinates=1, every texture fetch would sample raw
		 * pixel addresses instead of this driver's normalized 0-1 UV
		 * convention -- black textures, or outright crashes from wildly
		 * out-of-range TMU fetch addresses. Same uninitialized
		 * packed-struct-bitfield class as the texture-state and
		 * shader-state records, in a struct populated on EVERY
		 * textured/multitextured draw call. */
		p1->per_pixel_mask_enable = FALSE;
		p1->unnormalized_coordinates = FALSE;
		p1->output_type_32_bit = FALSE;
		p1->sampler_state_address_rshift_3 = textureSamplerStateAddress >> 3;
		swivel = (ULONG*)p1;
		swivel[0] = LE32(swivel[0]);

		/* Without growth the pair is the 8 bytes at unif_frag_address. */
		if (pe != NULL && sb->capacity == unif_frag_pre_capacity)
		{
			pe->fragpair = unif_frag_address;
		}
		}

		D(("gl_EmitPrimitiveV3D: textured, texShaderState=%08lx texSamplerState=%08lx\n",
		   (ULONG)textureShaderStateAddress, (ULONG)textureSamplerStateAddress));

		/* Unit 1's TMU config pair, appended immediately after unit 0's --
		 * matching g_fragment_shader_multitexture_assembly's own read order
		 * (unit 0's 2 wrtmuc calls consume the first 2 uniform words, unit
		 * 1's 2 wrtmuc calls consume these next 2). */
		if (multitextured)
		{
			v3d_tmu_config_parameter_0* p0b = (v3d_tmu_config_parameter_0*)v3d_cl_claim_fast(
				&context->device, sm, sb, sizeof(v3d_tmu_config_parameter_0), &backend->frame);
			v3d_tmu_config_parameter_1* p1b;

			p0b->return_words_of_texture_data = 3;
			p0b->texture_state_address_rshift_4 = textureShaderStateAddress2 >> 4;
			swivel = (ULONG*)p0b;
			swivel[0] = LE32(swivel[0]);

			p1b = (v3d_tmu_config_parameter_1*)v3d_cl_claim_fast(
				&context->device, sm, sb, sizeof(v3d_tmu_config_parameter_1), &backend->frame);
			/* Same explicit-assignment rule as unit 0's p1 above -- see that
			 * comment. */
			p1b->per_pixel_mask_enable = FALSE;
			p1b->unnormalized_coordinates = FALSE;
			p1b->output_type_32_bit = FALSE;
			p1b->sampler_state_address_rshift_3 = textureSamplerStateAddress2 >> 3;
			swivel = (ULONG*)p1b;
			swivel[0] = LE32(swivel[0]);

			D(("gl_EmitPrimitiveV3D: multitextured, texShaderState2=%08lx texSamplerState2=%08lx\n",
			   (ULONG)textureShaderStateAddress2, (ULONG)textureSamplerStateAddress2));

			/* Blend dst-factor flag, appended right after unit 1's TMU
			 * config pair -- matching g_fragment_shader_multitexture_
			 * {modulate,decal,replace}_blend_assembly's own read order
			 * (ldunifrf.rf24 comes right after the two wrtmuc-consumed TMU
			 * config pairs). Only written when multitex_blend is selected:
			 * the non-blend variants never read a 5th uniform, so skipping
			 * it for them is correct, not an oversight. */
			if (multitex_blend)
			{
				FLOAT* bf = (FLOAT*)v3d_cl_claim_fast(&context->device, sm, sb, sizeof(float), &backend->frame);
				swap_float32_into(&bf[0], (context->backend.blend_dstmode == V3D_BLEND_FACTOR_SRCALPHA) ? 1.0f : 0.0f);
			}
		}
		/* The plain textured+flat case dispatches to
		 * FRAGMENT_TEXTURED_COLORMOD (see frag_code_offset's terminal
		 * fallback), which reads 4 uniforms in the order blue/green/red/alpha
		 * (rf7/rf8/rf9/rf10, see v3d_assembler.c). fixed_color's RED bits
		 * multiply rf7 and its BLUE bits rf9, matching the texture sample's
		 * own hardware channel layout rather than fixed_color's logical
		 * order.
		 *
		 * This condition must match EXACTLY the cases that reach the shaders
		 * which read these words: a uniform written for a shader that does
		 * not read it desynchronises the CL stream for the draw. The flat
		 * textured alpha-test family and the flat textured fog family both
		 * modulate by these same four words (v3d_assembler.c), ahead of their
		 * own, so neither `alphatest` nor `fog` is excluded here. The stream
		 * for a flat textured fogged draw is
		 *   [TMU][TMU][r][g][b][a][fog x5][fog colour x3](+[alpha_ref][TLB cfg])
		 * and the shaders read it in exactly that order.
		 *
		 * What is excluded, and must stay excluded: `multitextured` feeds its
		 * own colour, and `combined` and every smooth shape read colour from
		 * a per-vertex varying. The untextured shapes take the else branch
		 * below, which always writes these four words. */
		if (!multitextured && !combined && !(smooth && !multitextured))
		{
			FLOAT* ca = (FLOAT*)v3d_cl_claim_fast(&context->device, sm, sb, 4 * sizeof(float), &backend->frame);

			/* The words are memoized on fixed_color and FPCR (s_colm). */
			if (d_ColorMemoCopy((ULONG*)ca, backend, sb->start, fpcr))
			{
			}
			else
			{
			swap_float32_into(&ca[0], (float)( backend->fixed_color        & 0xFF) / 255.0f); /* red -> multiplies rf7 */
			swap_float32_into(&ca[1], (float)((backend->fixed_color >>  8) & 0xFF) / 255.0f); /* green -> multiplies rf8 */
			swap_float32_into(&ca[2], (float)((backend->fixed_color >> 16) & 0xFF) / 255.0f); /* blue -> multiplies rf9 */
			swap_float32_into(&ca[3], (float)((backend->fixed_color >> 24) & 0xFF) / 255.0f); /* alpha */
			d_ColorMemoStore((const ULONG*)ca, backend, sb->start, fpcr);
			}
		}
	}
	else
	{
		col = (FLOAT*)v3d_cl_claim_fast(&context->device, sm, sb, 4 * sizeof(float), &backend->frame);

		/* The words are memoized on fixed_color and FPCR (s_colm). */
		if (d_ColorMemoCopy((ULONG*)col, backend, sb->start, fpcr))
		{
		}
		else
		{
		swap_float32_into(&col[0], (float)( backend->fixed_color        & 0xFF) / 255.0f);
		swap_float32_into(&col[1], (float)((backend->fixed_color >>  8) & 0xFF) / 255.0f);
		swap_float32_into(&col[2], (float)((backend->fixed_color >> 16) & 0xFF) / 255.0f);
		swap_float32_into(&col[3], (float)((backend->fixed_color >> 24) & 0xFF) / 255.0f);
		d_ColorMemoStore((const ULONG*)col, backend, sb->start, fpcr);
		}
	}

	/* Fog/alphatest uniforms, appended AFTER whatever base uniforms the
	 * textured/untextured branch above already wrote (the 2 TMU-config
	 * structs, or the 4 flat-colour floats) -- the fog/alphatest fragment
	 * shaders read exactly these via ldunifrf right after their own base
	 * sequence, see v3d_assembler.c's own comments on
	 * g_fragment_shader_textured_fog_assembly/textured_alphatest_assembly. */
	if (fog)
	{
		/* Eight words. The shaders compute
		 *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c)
		 * with c = eye distance, which covers all three GL fog modes from
		 * one instruction sequence -- so a mode change costs no new shader
		 * variant. Everything mode-specific is folded into these
		 * coefficients here, exactly as MESA's st_nir_lower_fog.c folds its
		 * own into gl_MesaFogParamsOptimized.
		 *
		 * LINEAR: f = (end - c)/(end - start), so A = end/(end-start) and
		 *         B = -1/(end-start), with M = 1 selecting this branch. The
		 *         exponential term still evaluates (to 2^0 = 1) and is
		 *         multiplied away by (1-M).
		 * EXP:    f = e^(-d*c) = 2^(-d*log2(e) * c), so C = -d*log2(e).
		 * EXP2:   f = e^(-(d*c)^2) = 2^(-d*d*log2(e) * c*c), so
		 *         D = -d*d*log2(e).
		 * M = 1 for LINEAR, 0 otherwise; the unused coefficients are zeroed
		 * so the other branch contributes nothing rather than garbage. */
		FLOAT* fu = (FLOAT*)v3d_cl_claim_fast(&context->device, sm, sb, 8 * sizeof(float), &backend->frame);

		/* The words are memoized on the fog parameters and FPCR (s_fogm). */
		if (d_FogMemoCopy((ULONG*)fu, backend, sb->start, fpcr))
		{
		}
		else
		{
		float range = backend->fog_end - backend->fog_start;
		float invRange = (range > -0.0001f && range < 0.0001f) ? 0.0f : (1.0f / range);
		float density = backend->fog_density;
		const float LOG2E = 1.442695041f;
		float fA = 0.0f, fB = 0.0f, fC = 0.0f, fD = 0.0f, fM = 0.0f;

		if (backend->fog_mode == V3D_FOG_EXP)
		{
			fC = -density * LOG2E;
		}
		else if (backend->fog_mode == V3D_FOG_EXP2)
		{
			fD = -density * density * LOG2E;
		}
		else /* V3D_FOG_LINEAR, and the defensive default */
		{
			fA = backend->fog_end * invRange;
			fB = -invRange;
			fM = 1.0f;
		}

		swap_float32_into(&fu[0], fA);
		swap_float32_into(&fu[1], fB);
		swap_float32_into(&fu[2], fC);
		swap_float32_into(&fu[3], fD);
		swap_float32_into(&fu[4], fM);
		swap_float32_into(&fu[5], (float)backend->fog_r / 255.0f);
		swap_float32_into(&fu[6], (float)backend->fog_g / 255.0f);
		swap_float32_into(&fu[7], (float)backend->fog_b / 255.0f);
		D(("gl_EmitPrimitiveV3D: fog uniforms fogEnd=%ld invRange=%ld (millis)\n",
		   (LONG)(backend->fog_end * 1000.0f), (LONG)(invRange * 1000.0f)));
		d_FogMemoStore((const ULONG*)fu, backend, sb->start, fpcr);
		}
	}
	/* NOT `else if`: a combined fog+alphatest draw needs BOTH blocks, and in
	 * exactly this order -- the fog words first, then alpha_ref, then the TLB
	 * config word, which is what the combined shaders read. The fog-only and
	 * alphatest-only paths write only their own block, because the other
	 * predicate is false. */
	if (alphatest || smooth_alphatest)
	{
		/* Two words: alpha_ref, plus a TLB config word for the passthrough
		 * depth write.
		 *
		 * The `or tlbu, ...` instruction added to the alphatest shaders
		 * consumes one word from THIS stream implicitly -- unlike the
		 * explicit ldunifrf reads, it takes whatever the next sequential
		 * uniform is. So the config word has to sit at exactly the index
		 * the shader's tlbu write lands on. For the untextured, unfogged
		 * shape that is index 5: the untextured branch writes 4 colour
		 * words (col[], consumed by ldunifrf rf7/rf8/rf9/rf10) and then
		 * this alpha_ref word (rf15), = 5 words read before the tlbu
		 * executes. Appending here therefore lands it correctly.
		 *
		 * 0xffffff84 = TLB_TYPE_DEPTH (2<<6) | TLB_V42_DEPTH_TYPE_INVARIANT
		 * (0<<3) | TLB_SAMPLE_MODE_PER_PIXEL (1<<2), or'd with 0xffffff00
		 * exactly as MESA does (nir_to_vir.c emit_frag_end). INVARIANT =
		 * take Z from the FEP, i.e. a passthrough. LE32 for the same
		 * reason swap_float32 exists -- V3D reads little-endian, m68k is
		 * big-endian.
		 *
		 * Written for EVERY alphatest draw, not only the variants that
		 * carry the tlbu instruction. A shader that does not consume it
		 * simply leaves it unread, which is harmless -- writing more
		 * uniforms than are read is fine, reading more than written is the
		 * bug class this file's other comments warn about -- and it keeps
		 * the stream layout identical across all alphatest variants. */
		ULONG* au = (ULONG*)v3d_cl_claim_fast(&context->device, sm, sb, 2 * sizeof(ULONG), &backend->frame);
		swap_float32_into(&au[0], backend->alpha_ref);
		au[1] = LE32(0xffffff84u);
		D(("gl_EmitPrimitiveV3D: alphatest uniform ref=%ld (millis) + TLB depth cfg\n", (LONG)(backend->alpha_ref * 1000.0f)));
	}

	/* Smooth points: the point size, then the TLB config word for the disc
	 * shaders' passthrough Z write -- the same word, for the same reason, as
	 * the alphatest block above (which never runs together with this one).
	 * The size is the one this pass rasterizes with: gl_EnsureDrawState sends
	 * POINT_SIZE once per pass, later in this very function for a pass's
	 * first draw, so before that it is still the live value. */
	if (smooth_point)
	{
		ULONG* pu = (ULONG*)v3d_cl_claim_fast(&context->device, sm, sb, 2 * sizeof(ULONG), &backend->frame);
		swap_float32_into(&pu[0], backend->draw_state_configured ? s_pass_point_size : context->CurrentPointSize);
		pu[1] = LE32(0xffffff84u);
	}

	/* Vertex/coordinate uniforms -- scale_p/scale_p_y are derived from the GL
	 * app's real viewport (context->sx/sy, viewport half-extents in pixels,
	 * GLViewport/viewport.c), not the full backend width/height, the same
	 * quantities the ClipWindow/viewport packets use.
	 *
	 * The two scales are genuinely separate: scale_p = sx*2 (X pixel extent)
	 * and scale_p_y = sy*2 (Y pixel extent), so each axis gets its own true
	 * screen-space scale. One shared scale would map only one axis 1:1 to the
	 * screen on a non-square viewport, and rotating geometry under that
	 * mismatch SHEARS it rather than merely stretching it. Every
	 * vertex/coordinate shader variant reads two scale uniforms (rf10 =
	 * scale_p, a second register for scale_p_y) and uses each in its own
	 * x_p/y_p formula -- see v3d_assembler.c's own comments.
	 *
	 * M_00..M_33 = the GL app's real CombinedMatrix (ModelView * Projection,
	 * matrix.c).
	 *
	 * The row/column mapping onto M_XY is derived from two independent
	 * pieces of code. matrix.c's GLTranslatef (`v(14)=x; v(24)=y; v(34)=z;`,
	 * matching its own "1 0 0 x / 0 1 0 y / 0 0 1 z / 0 0 0 1" comment)
	 * proves context->CombinedMatrix.v[OF_rc] is standard row-r/col-c
	 * indexing with translation in COLUMN 4. v3d_assembler.c's QPU assembly
	 * comments (`x_out = x_in M_00 + y_in M_10 + z_in M_20 + w_in M_30`,
	 * `y_out = x_in M_01 + y_in M_11 + z_in M_21 + w_in M_31`) show the
	 * shader dots the INPUT vector against a fixed OUTPUT-component's uniform
	 * group -- i.e. M_0Y..M_3Y must be ROW Y+1 of the matrix, so that M_30
	 * lands on translation-x, M_31 on translation-y and so on. Hence
	 * M_XY = a(Y+1, X+1): output row Y+1, contributing input column X+1.
	 * Note that an identity CombinedMatrix cannot distinguish this from its
	 * transpose, so it has to be read off the code rather than tested. */
	/* Both blocks are reused while every input is unchanged (s_vum). */
	if (s_vum.valid && s_vum.gen == g_v3d_cl_generation && s_vum.sb_start == sb->start &&
	    s_vum.serial == g_mglv3d_combined_serial && s_vum.fpcr == fpcr &&
	    s_vum.clip == (int)use_clip_space &&
	    s_vum.sx == d_Bits(&context->sx) && s_vum.sy == d_Bits(&context->sy) &&
	    s_vum.sz == d_Bits(&context->sz) && s_vum.az == d_Bits(&context->az))
	{
		unif_vex_address = s_vum.vu;
		unif_coord_address = s_vum.cu;
	}
	else
	{
	scale_p = context->sx * 2.0f;
	scale_p_y = context->sy * 2.0f;

	vu = (v3d_my_uniforms*)v3d_cl_claim_fast(&context->device, sm, sb, sizeof(v3d_my_uniforms), &backend->frame);
	unif_vex_address = (ULONG)vu;   /* from the claimed pointer -- see default_attr_values_address */
	swap_float32_into(&vu->scale_p, scale_p);
	swap_float32_into(&vu->scale_p_y, scale_p_y);
	if (use_clip_space)
	{
		/* Identity -- position is already clip-space (post-transform),
		 * see this function's header comment. */
		swap_float32_into(&vu->M_00, 1.0f); swap_float32_into(&vu->M_10, 0.0f);
		swap_float32_into(&vu->M_20, 0.0f); swap_float32_into(&vu->M_30, 0.0f);
		swap_float32_into(&vu->M_01, 0.0f); swap_float32_into(&vu->M_11, 1.0f);
		swap_float32_into(&vu->M_21, 0.0f); swap_float32_into(&vu->M_31, 0.0f);
		swap_float32_into(&vu->M_02, 0.0f); swap_float32_into(&vu->M_12, 0.0f);
		swap_float32_into(&vu->M_22, 1.0f); swap_float32_into(&vu->M_32, 0.0f);
		swap_float32_into(&vu->M_03, 0.0f); swap_float32_into(&vu->M_13, 0.0f);
		swap_float32_into(&vu->M_23, 0.0f); swap_float32_into(&vu->M_33, 1.0f);
	}
	else
	{
		swap_float32_into(&vu->M_00, context->CombinedMatrix.v[OF_11]); swap_float32_into(&vu->M_10, context->CombinedMatrix.v[OF_12]);
		swap_float32_into(&vu->M_20, context->CombinedMatrix.v[OF_13]); swap_float32_into(&vu->M_30, context->CombinedMatrix.v[OF_14]);
		swap_float32_into(&vu->M_01, context->CombinedMatrix.v[OF_21]); swap_float32_into(&vu->M_11, context->CombinedMatrix.v[OF_22]);
		swap_float32_into(&vu->M_21, context->CombinedMatrix.v[OF_23]); swap_float32_into(&vu->M_31, context->CombinedMatrix.v[OF_24]);
		swap_float32_into(&vu->M_02, context->CombinedMatrix.v[OF_31]); swap_float32_into(&vu->M_12, context->CombinedMatrix.v[OF_32]);
		swap_float32_into(&vu->M_22, context->CombinedMatrix.v[OF_33]); swap_float32_into(&vu->M_32, context->CombinedMatrix.v[OF_34]);
		swap_float32_into(&vu->M_03, context->CombinedMatrix.v[OF_41]); swap_float32_into(&vu->M_13, context->CombinedMatrix.v[OF_42]);
		swap_float32_into(&vu->M_23, context->CombinedMatrix.v[OF_43]); swap_float32_into(&vu->M_33, context->CombinedMatrix.v[OF_44]);
	}

	/* glDepthRange. GLDepthRange (viewport.c) computes sz = (f-n)/2 and
	 * az = (n+f)/2; the six render-pass vertex shaders read the pair from
	 * here. The default range (0,1) gives exactly 0.5/0.5, and
	 * GLDepthRange(0.0, 1.0) runs at context init (context.c), so these are
	 * never the MEMF_CLEAR zeroes.
	 *
	 * The COORDINATE shader deliberately does not read these: per MESA's own
	 * convention its `is_coord` outputs are the RAW clip position for the
	 * binning pass, never a viewport-scaled zs (see v3d_assembler.c). *cu =
	 * *vu below copies them anyway, harmlessly -- the coordinate shader stops
	 * reading after the 16th matrix value. */
	swap_float32_into(&vu->z_scale, context->sz);
	swap_float32_into(&vu->z_offset, context->az);

	cu = (v3d_my_uniforms*)v3d_cl_claim_fast(&context->device, sm, sb, sizeof(v3d_my_uniforms), &backend->frame);
	unif_coord_address = (ULONG)cu;   /* from the claimed pointer -- see default_attr_values_address */
	*cu = *vu;

	/* Kept only when neither claim grew state_buf: a grown sequence drops
	 * the frame below. */
	if (sb->capacity == unif_frag_pre_capacity)
	{
		s_vum.vu       = unif_vex_address;
		s_vum.cu       = unif_coord_address;
		s_vum.serial   = g_mglv3d_combined_serial;
		s_vum.fpcr     = fpcr;
		s_vum.clip     = (int)use_clip_space;
		s_vum.sx       = d_Bits(&context->sx);
		s_vum.sy       = d_Bits(&context->sy);
		s_vum.sz       = d_Bits(&context->sz);
		s_vum.az       = d_Bits(&context->az);
		s_vum.gen      = g_v3d_cl_generation;
		s_vum.sb_start = sb->start;
		s_vum.valid    = 1;
	}
	}

	/* Which vertex and fragment shader this draw binds. The shapes are
	 * checked combined-first, since `combined` implies both `smooth` and
	 * `textured`; the varying count that goes with each shape is in
	 * d_BuildShaderRecord. The COORDINATE shader code address is untouched by
	 * any of this except needs_real_w -- the coordinate stage is
	 * position-only and generic across every variant, so no smooth/textured/
	 * multitexture variant needs one of its own (see d_BuildShaderRecord's
	 * own comment at coordinate_shader_code_address).
	 *
	 * vertex_shader_input_vpm_segment_size is 2 for `combined` and its
	 * `smooth_alphatest` refinement, 1 for everything else -- see
	 * v3d_assembler.c's own comment on g_vertex_shader_smooth_textured_
	 * assembly: that shader's 9-word input exceeds one 8-word VPM sector, so
	 * the "1" that fits every input of 8 words or fewer is not enough for
	 * it. It is the one field in the whole shader-variant
	 * matrix sized to exactly what the formula requires rather than
	 * comfortably above it.
	 *
	 * alphatest and fog reuse VERTEX_TEXTURED (offset 0) unchanged, the same
	 * as textured/flat -- neither needs a colour varying -- and take their
	 * own fragment slots (8192/9216 = untextured fog/alphatest, 10240/11264 =
	 * textured fog/alphatest). multitextured DOES need its own vertex shader
	 * (4 texcoord varyings, VERTEX_MULTITEXTURE at 12288) and is checked
	 * ahead of alphatest/fog in frag_code_offset (FRAGMENT_MULTITEXTURE at
	 * 13312).
	 *
	 * clipspace_combined/clipspace_multitextured are checked AHEAD of
	 * combined/multitextured (each implies its own non-clipspace counterpart,
	 * the same way combined implies smooth+textured): they route to the
	 * real-w vertex shader, +16384 or +17408, instead of the normal combined
	 * (+6144) or multitexture (+12288) one. frag_code_offset is UNCHANGED in
	 * both cases -- a fragment shader only consumes already-interpolated
	 * varyings and is agnostic to how position was computed upstream.
	 *
	 * Both ternaries below must check `!multitextured` alongside raw
	 * `smooth`, for the same reason `combined` excludes it by construction:
	 * without that term a smooth multitextured draw selects the smooth-only
	 * vertex/fragment pair (4096/5120) instead of the multitexture pair
	 * (12288/13312), feeding multitextured attribute data to a shader
	 * compiled for smooth's completely different varying semantics. */
	vex_code_offset = (clipspace_combined || real_w_combined) ? 16384 :
	                   (clipspace_multitextured ? 17408 :
	                   ((combined || smooth_alphatest) ? 6144 : ((smooth && !multitextured) ? 4096 : (multitextured ? 12288 : 0))));
	/* multitextured's own frag_code_offset is a 6-way dispatch: multitex_env
	 * selects the combine mode, multitex_blend selects between the plain and
	 * blend-aware variant of whichever combine mode. See
	 * v3d_shader_assembler.h's own comment on those enum entries. */
	/* SMOOTH FOG ARMS FIRST. smooth_alphatest and combined are the first two
	 * shape arms of the chain below, so a fogged smooth draw would be
	 * swallowed by them and lose its fog. Both of those require textured, so
	 * the third arm safely catches the untextured smooth remainder. */
	/* Smooth points first: their predicate already excludes fog, alpha test
	 * and multitexture, so each maps onto exactly one base shape below --
	 * combined (7168), the smooth untextured arm (5120), the textured
	 * terminal arm (32768) or the untextured one (2048) -- and takes that
	 * shape's disc shader instead. */
	frag_code_offset = smooth_point ? (textured ? (smooth ? 115712 : 114688) : (smooth ? 113664 : 112640)) :
	                    (fog && smooth_alphatest) ? smooth_fog_alphatest_code_offset :
	                    ((fog && combined) ? 78848 :
	                    ((fog && multitex_blend) ?
	                        (multitex_env == 1 ? 102400 : (multitex_env == 2 ? 103424 : 101376)) :
	                    /* LAST of the fog arms, deliberately. This is the catch-all
	                     * for fogged smooth draws, and it shadows anything placed
	                     * below it, so keeping it last is structural rather than
	                     * another exclusion list to forget. */
	                    ((fog && smooth && !multitextured && !textured) ? 77824 :
	                    (smooth_alphatest ? smooth_alphatest_code_offset :
	                    (combined ? 7168 :
	                    ((smooth && !multitextured) ? 5120 :
	                    /* Multitexture fog arm ahead of the plain multitexture
	                     * arm, for the same reason the smooth fog arms lead the
	                     * chain: otherwise the arm below swallows the draw and
	                     * the fog is silently dropped. Only the non-blending env
	                     * modes reach it -- the (fog && multitex_blend) arm
	                     * higher up has already taken the blending ones. */
	                    ((fog && multitextured) ?
	                        (multitex_env == 1 ? 88064 : (multitex_env == 2 ? 89088 : 87040)) :
	                    (multitextured ? (multitex_blend ?
	                        (multitex_env == 1 ? 21504 : (multitex_env == 2 ? 22528 : 20480)) :
	                        (multitex_env == 1 ? 18432 : (multitex_env == 2 ? 19456 : 13312))) :
	                    /* Combined arm FIRST: it must be tested ahead of both
	                     * single-feature arms, or the `alphatest` arm below would
	                     * swallow the case and drop the fog. fog_alphatest_code_offset
	                     * is only meaningful when both are set, which is the guard
	                     * here. */
	                    ((fog && alphatest) ? fog_alphatest_code_offset :
	                    (alphatest ? alphatest_code_offset :
	                    (fog ? (textured ? 10240 : 8192) :
	                    /* The terminal case is "textured, flat-shaded,
	                     * nothing else active". It dispatches to
	                     * FRAGMENT_TEXTURED_COLORMOD (32768), which
	                     * multiplies the texture sample by a per-draw
	                     * uniform colour rather than emitting the texel
	                     * unmodulated -- see the four-word colour feed
	                     * written for it in the textured uniform block
	                     * above. */
	                    (textured ? 32768 : 2048))))))))))));


	/* The record and its attribute records in ONE claim: a copy of the
	 * shape's template (s_srec_tpl, rebuilt by d_BuildShaderRecord when the
	 * shape changes) with this draw's addresses patched in. The attribute
	 * records land directly after the record, where their own claims put
	 * them. */
	{
		ULONG shape = (combined         ? D_SR_COMBINED         : 0) |
		              (smooth_alphatest ? D_SR_SMOOTH_ALPHATEST : 0) |
		              (smooth           ? D_SR_SMOOTH           : 0) |
		              (multitextured    ? D_SR_MULTITEXTURED    : 0) |
		              (textured         ? D_SR_TEXTURED         : 0) |
		              (alphatest        ? D_SR_ALPHATEST        : 0) |
		              (smooth_point     ? D_SR_SMOOTH_POINT     : 0) |
		              (multitex_blend   ? D_SR_MULTITEX_BLEND   : 0) |
		              (needs_real_w     ? D_SR_NEEDS_REAL_W     : 0);
		ULONG nwords = (sizeof(v3d_gl_shader_state_record) +
		                ((combined || smooth_alphatest) ? 3 : 2) * sizeof(v3d_gl_shader_state_attribute_record)) / 4;
		ULONG* rec;
		ULONG w;

		if (!(s_srec.valid && s_srec.gen == g_v3d_cl_generation && s_srec.sb_start == sb->start &&
		      s_srec.code_base == backend->shader_code_mem.hostptr &&
		      s_srec.frag_code_offset == frag_code_offset && s_srec.vex_code_offset == vex_code_offset &&
		      s_srec.shape == shape))
		{
			for (w = 0; w < D_SR_MAXWORDS; w++)
				s_srec_tpl[w] = 0;
			d_BuildShaderRecord(backend, (v3d_u8*)s_srec_tpl, shape, frag_code_offset, vex_code_offset,
			                    0, 0, 0, 0, NULL, NULL, NULL);
			s_srec.gen              = g_v3d_cl_generation;
			s_srec.sb_start         = sb->start;
			s_srec.code_base        = backend->shader_code_mem.hostptr;
			s_srec.frag_code_offset = frag_code_offset;
			s_srec.vex_code_offset  = vex_code_offset;
			s_srec.shape            = shape;
			s_srec.valid            = 1;
		}

		d_AlignBuffer(backend, 32);
		shader = (v3d_gl_shader_state_record*)v3d_cl_claim_fast(&context->device, sm, sb, nwords * 4, &backend->frame);
		staterecordAddress = (ULONG)shader;   /* from the claimed pointer; a grown block starts 64-aligned, so >>5 still holds */

		rec = (ULONG*)shader;
		for (w = 0; w < nwords; w++)
			rec[w] = s_srec_tpl[w];
		rec[2]  = LE32(default_attr_values_address);
		rec[4]  = LE32(unif_frag_address);
		rec[6]  = LE32(unif_vex_address);
		rec[8]  = LE32(unif_coord_address);
		rec[9]  = LE32((ULONG)posbuf);
		rec[13] = LE32((ULONG)texbuf);
		if (nwords > 17)
			rec[17] = LE32((ULONG)texbuf2);

	}

	/* SetBuffer, inline: it is this same store. */
	backend->current_buf = &backend->binning_buf[backend->build_slot];
	/* The per-pass residue (gl_EnsureDrawState) goes out after the first
	 * draw's blend group, below. The viewport group -- the real per-draw
	 * glViewport -- is emitted by gl_EmitCullBlendState. */

	/* glScissor, modelled on MESA's v3dx_emit.c:228-291. The rectangle is
	 * computed for every draw, not only a scissored one, and the ClipWindow
	 * packet is re-emitted whenever it changes.
	 *
	 * MESA intersects viewport with scissor and clamps to the drawable,
	 * because the binner uses this rectangle to decide where to put things --
	 * an out-of-range rect is not merely a wrong picture. It also leaves the
	 * extent at zero for an empty rect rather than letting it go negative.
	 * All three are reproduced here.
	 *
	 * The viewport half is always applied, scissor or no scissor, which is
	 * MESA's own reasoning: "always clip the rendering to the viewport, since
	 * the hardware does guardband clipping, meaning primitives would rasterize
	 * outside of the view volume".
	 *
	 * GLViewport's ax/ay are CENTRES and sx/sy are HALF-extents, and ay is
	 * already top-origin (it computes screenHeight - y - h/2 from GL's
	 * bottom-origin y), so the viewport rect needs no conversion to sit in the
	 * same space as the scissor and as ClipWindow. fabs on the half-extents
	 * mirrors MESA's fabsf(vpscale), which guards a negative/flipped scale. */
	{
		UWORD dl, db, dw, dh;
		UWORD vp_sx, vp_sy, vp_ax, vp_ay;

		/* The rectangle and gl_EmitCullBlendState's viewport UWORDs are
		 * memoized on their raw inputs and FPCR (s_rectm). */
		if (s_rectm.valid && s_rectm.gen == g_v3d_cl_generation && s_rectm.sb_start == sb->start &&
		    s_rectm.fpcr == fpcr &&
		    s_rectm.sx == d_Bits(&context->sx) && s_rectm.sy == d_Bits(&context->sy) &&
		    s_rectm.ax == d_Bits(&context->ax) && s_rectm.ay == d_Bits(&context->ay) &&
		    s_rectm.sc_en == backend->scissor_enable &&
		    s_rectm.sc_x == backend->scissor_x && s_rectm.sc_y == backend->scissor_y &&
		    s_rectm.sc_w == backend->scissor_w && s_rectm.sc_h == backend->scissor_h &&
		    s_rectm.width == backend->width && s_rectm.height == backend->height)
		{
			dl = s_rectm.cw_l;
			db = s_rectm.cw_b;
			dw = s_rectm.cw_w;
			dh = s_rectm.cw_h;
			vp_sx = s_rectm.vp_sx;
			vp_sy = s_rectm.vp_sy;
			vp_ax = s_rectm.vp_ax;
			vp_ay = s_rectm.vp_ay;
		}
		else
		{
		UWORD r[8];

		d_ClipRectCompute(context, r);
		dl    = r[0];
		db    = r[1];
		dw    = r[2];
		dh    = r[3];
		vp_sx = r[4];
		vp_sy = r[5];
		vp_ax = r[6];
		vp_ay = r[7];

		s_rectm.cw_l     = dl;
		s_rectm.cw_b     = db;
		s_rectm.cw_w     = dw;
		s_rectm.cw_h     = dh;
		s_rectm.vp_sx    = vp_sx;
		s_rectm.vp_sy    = vp_sy;
		s_rectm.vp_ax    = vp_ax;
		s_rectm.vp_ay    = vp_ay;
		s_rectm.sx       = d_Bits(&context->sx);
		s_rectm.sy       = d_Bits(&context->sy);
		s_rectm.ax       = d_Bits(&context->ax);
		s_rectm.ay       = d_Bits(&context->ay);
		s_rectm.sc_en    = backend->scissor_enable;
		s_rectm.sc_x     = backend->scissor_x;
		s_rectm.sc_y     = backend->scissor_y;
		s_rectm.sc_w     = backend->scissor_w;
		s_rectm.sc_h     = backend->scissor_h;
		s_rectm.width    = backend->width;
		s_rectm.height   = backend->height;
		s_rectm.fpcr     = fpcr;
		s_rectm.gen      = g_v3d_cl_generation;
		s_rectm.sb_start = sb->start;
		s_rectm.valid    = 1;
		}

		/* MESA re-emits CLIP_WINDOW on SCISSOR|VIEWPORT|RASTERIZER_SCISSOR
		 * dirty, i.e. when the rectangle changed (mglv3d_dedup_cache). */
		dd_sync();
		if (!s_dd.cw_valid ||
		    s_dd.cw_l != dl || s_dd.cw_b != db || s_dd.cw_w != dw || s_dd.cw_h != dh)
		{
			ClipWindow(backend, dl, db, dw, dh);
			s_dd.cw_valid = 1;
			s_dd.cw_l = dl; s_dd.cw_b = db; s_dd.cw_w = dw; s_dd.cw_h = dh;
		}

		gl_EmitCullBlendState(context, smooth, multitextured, textured, multitex_env, vp_sx, vp_sy, vp_ax, vp_ay);
	}
	/* `combined` and `smooth_alphatest` write THREE attribute records
	 * (posbuf, texbuf, texbuf2); every other shape writes exactly two
	 * (posbuf + texbuf). d_glShaderState below must be told the right
	 * number, or the hardware never consumes texbuf2 as part of this draw
	 * call's attribute list and the alignment of whatever follows is
	 * corrupted. See texbuf2's own allocation comment for the layout. */

	/* If state_buf grew anywhere between capturing unif_frag_address and
	 * here, that address is stale -- this draw's
	 * fragment_shader_uniforms_address, just embedded into `shader` above,
	 * points at the old, now-incomplete block. It feeds actual TMU
	 * texture-fetch addresses, so a garbage value there can wedge the binner
	 * rather than merely render wrong colours. Same risk class as this
	 * function's own texbuf/texbuf2 pair, which is fixed by a single atomic
	 * claim; a general fix here would need reserving this whole
	 * variable-length sequence's worst-case size upfront across every
	 * conditional branch, which is fiddly and easy to get subtly wrong.
	 * Detecting the stale condition and dropping the whole frame instead
	 * matches gl_FramePresent's own CL-buffer-overflow guard, and a dropped
	 * frame reads as a hitch rather than as broken geometry. This draw's own
	 * commit (glShaderState/VertexArrayPrims below) is skipped too: there is
	 * no reason to reference a state record built with a known-stale uniform
	 * address, even though the frame will not be submitted either way. */
	if (sb->capacity != unif_frag_pre_capacity)
	{
		E(("gl_EmitPrimitiveV3D: state_buf grew mid-sequence building this draw's fragment uniforms (unif_frag_address now stale) -- marking frame corrupted, dropping\n"));
		backend->frame_corrupted = TRUE;
		return;
	}

	/* The per-pass residue -- BLEND_CONSTANT_COLOR, ZERO_ALL_* flags, TF
	 * specs, OQ, SAMPLE_STATE, POINT_SIZE/LINE_WIDTH, VCM_CACHE_SIZE -- goes
	 * out here on the first draw of the pass, after the blend group and
	 * before GL_SHADER_STATE, which is where MESA's first draw emits it
	 * (v3dx_emit.c:569-703 then v3dx_draw.c:865-882). */
	gl_EnsureDrawState(context);

	/* MESA re-emits TRANSFORM_FEEDBACK_SPECS (enable=false, no specs)
	 * whenever the primitive type changes (V3D_DIRTY_PRIM_MODE,
	 * v3dx_emit.c:629-658). The first draw of a pass gets it from
	 * gl_EnsureDrawState. */
	{
		extern v3d_u32 g_v3d_cl_generation;
		static v3d_u32 s_tf_gen  = 0xFFFFFFFFUL;
		static int     s_tf_prim = -1;

		if (s_tf_gen != g_v3d_cl_generation)
		{
			s_tf_gen  = g_v3d_cl_generation;
			s_tf_prim = (int)primType;
		}
		else if ((int)primType != s_tf_prim)
		{
			TransformFeedbackSpecs(backend, FALSE, 0);
			s_tf_prim = (int)primType;
		}
	}

	/* MID-FRAME TEXTURE REUSE HAZARD -- stamp the bound textures with the
	 * current bin generation, so a later upload that would overwrite texels
	 * this draw is about to sample can split the pass first. See
	 * V3DTexture.last_draw_frame (v3d_texture.h) for the mechanism and
	 * tex_SyncBeforeModify (texture.c) for the consumer.
	 *
	 * HERE, and deliberately not at the bound_tex/bound_tex2 resolve points
	 * further up or inside v3d_texture_emit_state: this is the single binning
	 * commit -- every draw path reaches it, both units are still in scope and
	 * unmodified, and both of this function's mid-way abort paths are already
	 * behind it, so a draw that bails out never leaves a stamp claiming it
	 * binned something. The emit-state route would miss most draws outright,
	 * because the texture state-record cache (s_texcache) skips it whenever
	 * the records already exist in this generation, which is the common case.
	 *
	 * `multitextured` implies `textured` by construction, so unit 1's pointer
	 * is non-NULL whenever that flag is set. */
	if (textured)
		bound_tex->last_draw_frame = (v3d_u32)g_mglv3d_frame_number;
	if (multitextured)
		bound_tex2->last_draw_frame = (v3d_u32)g_mglv3d_frame_number;

	d_glShaderState(backend, staterecordAddress, (combined || smooth_alphatest) ? 3 : 2);

	/*
	 * chunk_size lets one shared attribute buffer and one shader state
	 * record -- both built once above, for the WHOLE batch -- still emit
	 * several independent VertexArrayPrims packets against it. That is what
	 * GL_QUADS/GL_QUAD_STRIP need, where each 4-vertex TRIANGLEFAN is its own
	 * independent quad rather than one bigger connected fan (see d_DrawQuads'
	 * own header comment for why concatenating quads into one
	 * VertexArrayPrims(TRIANGLEFAN, N, 0) call would be wrong, not merely
	 * unbatched). Every other call site passes chunk_size == count, which is
	 * a single call.
	 */
	if (ix != NULL)
	{
		/* Lock-aware draw, in MESA's order (v3dx_draw.c): INDEX_BUFFER_SETUP,
		 * then INDEXED_PRIM_LIST with a byte offset into that buffer. */
		d_IndexBufferSetup(backend, (ULONG)idxbuf, idxbytes);
		d_IndexedPrimList(backend, primType, v3d_INDEX_TYPE_16_BIT, (ULONG)ix->nidx, FALSE, 0);
	}
	else
	{
		for (i = 0; i < count; i += chunk_size)
		{
			d_VertexArrayPrims(backend, primType, (ULONG)chunk_size, (ULONG)i);
		}
	}
}

static void gl_EmitPrimitiveV3D(GLcontext context, int* indices, int count, UBYTE primType, GLboolean use_clip_space, int chunk_size)
{
	gl_EmitPrimitiveV3DEx(context, indices, count, primType, use_clip_space, chunk_size, NULL);
}

/*
 * Test-only entry point into gl_EmitPrimitiveV3D's use_clip_space=TRUE path
 * (real w preserved, see needs_real_w above). That path is only ever reached
 * internally, via dh_DrawPoly/dh_DrawLine -- there is no public GL call that
 * can request it, since it only happens as a side effect of real near-plane
 * clipping, so a caller driving the public API alone cannot exercise it.
 * Here the caller fills MGLVertex structs directly (bx/by/bz/bw +
 * v.u0/v0/u1/v1 + color.r/g/b/a); this writes them into
 * context->VertexBuffer and calls gl_EmitPrimitiveV3D with
 * use_clip_space=TRUE, including a real, non-1.0 w. Not part of the public
 * GL API; diagnostic use only, matching this project's existing
 * "test-only" precedent (see v3d_debug.h's E()/D() macros' own scope).
 */
void mglv3d_test_emit_clipspace(GLcontext context, MGLVertex* verts, int count, UBYTE primType)
{
	int indices[16];
	int i;

	if (count > 16) count = 16; /* indices[] holds 16 */

	for (i = 0; i < count; i++)
	{
		context->VertexBuffer[i] = verts[i];
		indices[i] = i;
	}

	gl_EmitPrimitiveV3D(context, indices, count, primType, GL_TRUE, count);
}

/*
 * hclip.c's two terminal calls for clipped geometry. Both use
 * gl_EmitPrimitiveV3D's use_clip_space=GL_TRUE path, which reads the
 * clip-space fields: hc_ClipAndDrawPoly/Line's Sutherland-Hodgeman-ish
 * clipping reads and writes .bx/.by/.bz/.bw throughout, both for the
 * vertices it interpolates at a crossing and for the ones it carries
 * through unchanged. poly->verts[] already indexes into
 * context->VertexBuffer exactly like gl_EmitPrimitiveV3D's own `indices`
 * parameter expects -- no copy needed.
 */
void dh_DrawPoly(GLcontext context, MGLPolygon *poly)
{
	D(("dh_DrawPoly: entry, numverts=%ld\n", (LONG)poly->numverts));

	if (poly->numverts < 3)
	{
		D(("dh_DrawPoly: fewer than 3 vertices after clipping, dropping\n"));
		return;
	}

	/* Clipped polygon IS a triangle fan once its vertices are in
	 * perimeter order (Sutherland-Hodgeman clipping preserves perimeter
	 * order) -- same reasoning as the GL_QUADS port, matching the
	 * original's own W3D_DrawTriFanV(vertexcount=poly->numverts) call. */
	gl_FrameBegin(context);
	gl_EmitPrimitiveV3D(context, poly->verts, poly->numverts, V3D_PRIM_TRIANGLEFAN, GL_TRUE, poly->numverts);
}

/* Only poly->verts[0] and poly->verts[1] are read, whatever poly->numverts
 * says -- matching the original's own fixed-arity W3D_DrawLine call. */
void dh_DrawLine(GLcontext context, MGLPolygon *poly)
{
	D(("dh_DrawLine: entry\n"));

	gl_FrameBegin(context);
	gl_EmitPrimitiveV3D(context, poly->verts, 2, V3D_PRIM_LINES, GL_TRUE, 2);
}

/*
 * The draws below batch rather than emitting one gl_EmitPrimitiveV3D call
 * per primitive: VertexArrayPrims' own `length` parameter means "this many
 * vertices, grouped per primType" (v3d_commands.c), so a single
 * V3D_PRIM_TRIANGLES call with length=3*N already renders N independent
 * triangles correctly -- one shader state record and one attribute buffer
 * for the batch. A whole glBegin/glEnd block goes out in one call where its
 * vertices share everything the state record holds; the quad paths below
 * split theirs per flat colour instead, one call per batch.
 */
/*
 * The vertex numbers 0..VertexBufferSize-1, for every draw below whose index
 * list is just the vertices in order. One glBegin/glEnd may carry up to
 * VertexBufferSize vertices, which the application chooses, so this table is
 * allocated with the vertex buffer, at its size (MGLInitContext), and freed
 * with it (MGLDeleteContext).
 */
static int *s_seq = NULL;
static int  s_seq_size = 0;

void d_FreeSeq(void)
{
	if (s_seq)
		free(s_seq);
	s_seq = NULL;
	s_seq_size = 0;
}

GLboolean d_AllocSeq(int size)
{
	int i;

	d_FreeSeq();
	s_seq = malloc(sizeof(int) * size);
	if (!s_seq)
		return GL_FALSE;
	for (i = 0; i < size; i++)
		s_seq[i] = i;
	s_seq_size = size;
	return GL_TRUE;
}

/* Vertices in the current block that s_seq can number -- all of them, unless
 * glVertex went past VertexBufferSize (which it does not check). */
static int d_SeqCount(GLcontext context)
{
	return ((int)context->VertexBufferPointer > s_seq_size) ? s_seq_size : (int)context->VertexBufferPointer;
}

void d_DrawTriangles(GLcontext context)
{
	int count;

	D(("d_DrawTriangles: entry, VertexBufferPointer=%ld CullFace_State=%ld\n",
	   (LONG)context->VertexBufferPointer, (LONG)context->CullFace_State));

	if(context->VertexBufferPointer < 3)
	{
		D(("d_DrawTriangles: fewer than 3 vertices, dropping\n"));
		return;
	}

	if(context->CullFace_State == GL_TRUE && context->CurrentCullFace == GL_FRONT_AND_BACK)
	{
		D(("d_DrawTriangles: GL_FRONT_AND_BACK cull, dropping everything\n"));
		return;
	}

	/* There is no CPU-side pre-filter: no per-vertex matrix multiply, no
	 * outcode trivial-reject, no front-face pre-cull. Nothing in this
	 * function reads bx/by/bz/bw at all; V3D does its own visibility
	 * culling as part of tile binning, and CfgBits' effp/erfp/cp fields
	 * configure real hardware face culling from this same GL cull state --
	 * matching MESA's v3d gallium driver, whose draw path carries no
	 * CPU-side frustum/bbox/face-cull pre-filter either. Every triangle
	 * goes straight to the batch; hardware decides visibility and facing.
	 *
	 * v_EnsureTransformState is still required, and does two jobs: it
	 * recomputes context->CombinedMatrix (the ModelView*Projection matrix)
	 * whenever GL matrix state changed since the last draw, and it runs the
	 * GL_TEXTURE_GEN_S/T (GL_SPHERE_MAP) generation when texgen is active
	 * -- see its own comment. gl_EmitPrimitiveV3D reads CombinedMatrix
	 * directly to build the GPU's transform uniform for every
	 * use_clip_space=GL_FALSE draw, which is all of them here, so a scene
	 * whose matrix changed since the last draw would otherwise be
	 * transformed with a stale matrix. The texgen step costs nothing when
	 * texgen is not active, and the matrix refresh runs only when GL matrix
	 * state has changed. */
	v_EnsureTransformState(context);

	d_FrameBegin(context);

	/* Whole triangles only: a trailing one or two vertices draw nothing, as GL
	 * says. */
	count = (d_SeqCount(context) / 3) * 3;
	D(("d_DrawTriangles: emitting batch of %ld triangles (%ld vertices), hardware clip only\n",
	   (LONG)(count/3), (LONG)count));
	gl_EmitPrimitiveV3D(context, s_seq, count, V3D_PRIM_TRIANGLES, GL_FALSE, count);
}

/*
 * Lock-aware GL_TRIANGLES (vertexelements.c, DrawLockedTriangles):
 * VertexBuffer[0..VertexBufferPointer) holds the locked
 * vertices, one each, and the caller's `nidx` indices (already checked to lie
 * in the locked range) say how to connect them. Same entry work as
 * d_DrawTriangles: GL_FRONT_AND_BACK culling draws nothing, the combined
 * matrix and any texgen are brought up to date, and the frame is opened.
 */
void d_DrawTrianglesLocked(GLcontext context, int nidx, GLenum type, const GLvoid *indices, int first)
{
	int nverts = d_SeqCount(context);
	mglv3d_indexed_draw ix;

	if (context->CullFace_State == GL_TRUE && context->CurrentCullFace == GL_FRONT_AND_BACK)
		return;

	v_EnsureTransformState(context);
	d_FrameBegin(context);

	ix.indices = indices;
	ix.type    = type;
	ix.nidx    = nidx;
	ix.first   = first;

	gl_EmitPrimitiveV3DEx(context, s_seq, nverts, V3D_PRIM_TRIANGLES, GL_FALSE, nverts, &ix);
}

/*
 * Points use the real V3D primitive (V3D_PRIM_POINTS) -- unlike the
 * original, which drew a degenerate right-triangle to simulate a point (a
 * Warp3D-era technique, not a hardware necessity here).
 */
void d_DrawPoints(GLcontext context)
{
	int count;

	D(("d_DrawPoints: entry, VertexBufferPointer=%ld\n", (LONG)context->VertexBufferPointer));

	if(context->VertexBufferPointer == 0)
		return;

	if(context->CullFace_State == GL_TRUE && context->CurrentCullFace == GL_FRONT_AND_BACK)
		return;

	/* Every point goes straight to hardware; nothing in this function reads
	 * bx/by/bz/bw. See d_DrawTriangles' own comment for what
	 * v_EnsureTransformState covers. */
	v_EnsureTransformState(context);

	d_FrameBegin(context);

	count = d_SeqCount(context);
	D(("d_DrawPoints: emitting batch of %ld points\n", (LONG)count));
	gl_EmitPrimitiveV3D(context, s_seq, count, V3D_PRIM_POINTS, GL_FALSE, count);
}

/*
 * GL_LINES -- the same shape as d_DrawTriangles, per-pair instead of
 * per-triple: each pair is a V3D_PRIM_LINES primitive, and the whole
 * glBegin/glEnd block goes out in one call (see the batching comment above
 * s_seq).
 */
void d_DrawLines(GLcontext context)
{
	int count;

	D(("d_DrawLines: entry, VertexBufferPointer=%ld\n", (LONG)context->VertexBufferPointer));

	if(context->VertexBufferPointer < 2)
		return;

	if(context->CullFace_State == GL_TRUE && context->CurrentCullFace == GL_FRONT_AND_BACK)
		return;

	/* Every line goes straight to hardware; nothing in this function reads
	 * bx/by/bz/bw. See d_DrawTriangles' own comment for what
	 * v_EnsureTransformState covers. */
	v_EnsureTransformState(context);

	d_FrameBegin(context);

	/* Whole pairs only: a trailing single vertex draws nothing, as GL says. */
	count = d_SeqCount(context) & ~1;
	D(("d_DrawLines: emitting batch of %ld unclipped lines (%ld vertices)\n",
	   (LONG)(count/2), (LONG)count));
	gl_EmitPrimitiveV3D(context, s_seq, count, V3D_PRIM_LINES, GL_FALSE, count);
}

/*
 * GL_LINE_STRIP / GL_LINE_LOOP -- not a port. d_DrawLineStrip is a true
 * no-op ({} with no body at all) in the ORIGINAL MiniGL (MiniGL/src/draw.c),
 * mapped to BOTH
 * GL_LINE_STRIP and GL_LINE_LOOP's glBegin dispatch and never implemented
 * for either, so there is no reference behaviour to port.
 *
 * V3D_PRIM_LINELOOP/V3D_PRIM_LINESTRIP are both native V3D primitives
 * (v3d_hw.h's own V3D_PRIM_* enum, same file as V3D_PRIM_TRIANGLEFAN/STRIP)
 * -- the same "no CPU-side decomposition needed" situation as triangle
 * fans/strips, and unlike GL_QUADS/GL_QUAD_STRIP, which have no direct V3D
 * equivalent at all. The whole strip or loop is one single native draw
 * call.
 */
void d_DrawLineStrip(GLcontext context)
{
	int count;

	D(("d_DrawLineStrip: entry, VertexBufferPointer=%ld\n", (LONG)context->VertexBufferPointer));

	if (context->VertexBufferPointer < 2)
		return;

	if (context->CullFace_State == GL_TRUE && context->CurrentCullFace == GL_FRONT_AND_BACK)
		return;

	/* There is no per-segment fallback: hardware clips a whole LINESTRIP
	 * primitive correctly, the same native-primitive hardware clipping
	 * TRIANGLEFAN/TRIANGLESTRIP rely on, so the single native call is
	 * unconditional. See d_DrawTriangles' own comment for what
	 * v_EnsureTransformState covers. */
	v_EnsureTransformState(context);

	d_FrameBegin(context);

	count = d_SeqCount(context);
	gl_EmitPrimitiveV3D(context, s_seq, count, V3D_PRIM_LINESTRIP, GL_FALSE, count);
}

/*
 * GL_LINE_LOOP -- same as d_DrawLineStrip above (see its own comment), with
 * one extra closing segment (last vertex back to vertex 0). The hardware
 * primitive itself draws that extra segment: that is the whole difference
 * between V3D_PRIM_LINESTRIP and V3D_PRIM_LINELOOP.
 */
void d_DrawLineLoop(GLcontext context)
{
	int count;

	D(("d_DrawLineLoop: entry, VertexBufferPointer=%ld\n", (LONG)context->VertexBufferPointer));

	if (context->VertexBufferPointer < 2)
		return;

	if (context->CullFace_State == GL_TRUE && context->CurrentCullFace == GL_FRONT_AND_BACK)
		return;

	/* Same as d_DrawLineStrip just above -- hardware clips a whole LINELOOP
	 * primitive correctly, so the single native call is unconditional. */
	v_EnsureTransformState(context);

	d_FrameBegin(context);

	count = d_SeqCount(context);
	gl_EmitPrimitiveV3D(context, s_seq, count, V3D_PRIM_LINELOOP, GL_FALSE, count);
}

/*
 * Fans and strips. V3D_PRIM_TRIANGLEFAN/V3D_PRIM_TRIANGLESTRIP are both
 * native V3D primitives (v3d_hw.h's own V3D_PRIM_* enum) -- unlike
 * GL_QUADS/GL_QUAD_STRIP, which have no direct V3D equivalent and need real
 * triangle decomposition. gl_EmitPrimitiveV3D takes a primType, so a fan or
 * strip needs nothing beyond a sequential index array and the right mode:
 * the whole buffer is one draw call, with no CPU-side outcode analysis.
 * Hardware clips and culls a whole TRIANGLEFAN primitive correctly,
 * per-triangle.
 */
void d_DrawTriangleFan(GLcontext context)
{
	static int s_trace_calls = 0;
	int trace = (s_trace_calls < 5);
	int i;
	int count;

	if (trace) s_trace_calls++;

	D(("d_DrawTriangleFan: entry, VertexBufferPointer=%ld\n", (LONG)context->VertexBufferPointer));
	if (trace) D(("d_DrawTriangleFan: entry, VertexBufferPointer=%ld\n", (LONG)context->VertexBufferPointer));

	if(context->VertexBufferPointer < 3)
	{
		if (trace) D(("d_DrawTriangleFan: too few vertices (%ld < 3) -- discarding\n", (LONG)context->VertexBufferPointer));
		return;
	}

	if(context->CullFace_State == GL_TRUE && context->CurrentCullFace == GL_FRONT_AND_BACK)
	{
		if (trace) D(("d_DrawTriangleFan: CullFace_State GL_FRONT_AND_BACK -- discarding\n"));
		return;
	}

	/* MGL_FLATFAN gets identical treatment to GL_TRIANGLE_FAN/GL_POLYGON
	 * here -- no special-casing, no window-space-pixel bypass, the same
	 * use_clip_space=GL_FALSE emit below. See d_DrawTriangles' own comment
	 * for what v_EnsureTransformState covers. */
	v_EnsureTransformState(context);

	/* GL_CULL_FACE: a whole fan submitted as ONE V3D_PRIM_TRIANGLEFAN call
	 * still gets per-triangle winding tested by the GPU's own primitive
	 * setup engine, using the already-wired CfgBits effp/erfp/cp cull
	 * config -- same real hardware cull this project already relies on
	 * for GL_TRIANGLES/GL_QUADS. A fan has no winding-alternation
	 * convention to worry about (every fan triangle shares the hub and
	 * is wound consistently by construction), so nothing extra is
	 * needed here. */

	d_FrameBegin(context);

	count = d_SeqCount(context);

	if (trace) D(("d_DrawTriangleFan: emitting, count=%ld Texture2D[0]=%ld Texture2D[1]=%ld ActiveTexture=%ld CurrentBinding=%lu\n",
	   (LONG)context->VertexBufferPointer, (LONG)context->Texture2D_State[0], (LONG)context->Texture2D_State[1],
	   (LONG)context->ActiveTexture, (ULONG)context->CurrentBinding));

	/* Per-vertex .bx/.by/.bz/.bw and unit-0 texcoords, bounded by the same
	 * `trace` (first 5 calls) guard already used above, not a per-frame
	 * cost. */
	if (trace)
	{
		for (i = 0; i < context->VertexBufferPointer; i++)
		{
			MGLVertex *vv = &context->VertexBuffer[i];
			D(("d_DrawTriangleFan: vert %ld bx=%ld by=%ld bz=%ld bw=%ld u=%ld v=%ld (x1000)\n",
			   (LONG)i, (LONG)(vv->bx*1000.0f), (LONG)(vv->by*1000.0f),
			   (LONG)(vv->bz*1000.0f), (LONG)(vv->bw*1000.0f),
			   (LONG)(vv->v.u0*1000.0f), (LONG)(vv->v.v0*1000.0f)));
		}
	}

	gl_EmitPrimitiveV3D(context, s_seq, count, V3D_PRIM_TRIANGLEFAN, GL_FALSE, count);
}

/*
 * The whole strip is one V3D_PRIM_TRIANGLESTRIP draw call, with no CPU-side
 * outcode analysis: hardware clips and culls it correctly per-triangle,
 * including the alternating-winding correction -- see the GL_CULL_FACE
 * comment below.
 */
void d_DrawTriangleStrip(GLcontext context)
{
	int count;

	D(("d_DrawTriangleStrip: entry, VertexBufferPointer=%ld\n", (LONG)context->VertexBufferPointer));

	if(context->VertexBufferPointer < 3)
		return;

	if(context->CullFace_State == GL_TRUE && context->CurrentCullFace == GL_FRONT_AND_BACK)
		return;

	/* MGL_FLATSTRIP gets identical treatment to GL_TRIANGLE_STRIP here --
	 * no window-space-bypass branch, same as MGL_FLATFAN in
	 * d_DrawTriangleFan. Nothing in this function reads bx/by/bz/bw. See
	 * d_DrawTriangles' own comment for what v_EnsureTransformState covers. */
	v_EnsureTransformState(context);

	/* GL_CULL_FACE: relies on V3D_PRIM_TRIANGLESTRIP's own hardware
	 * primitive-assembly decomposing the strip into triangles with the
	 * standard GL alternating-winding correction already applied (even i:
	 * (i,i+1,i+2); odd i: (i+1,i,i+2)) before the per-triangle cull test
	 * (CfgBits effp/erfp/cp) runs -- i.e. that a native strip primitive
	 * culls as one CONSISTENTLY-wound ribbon, not as independent triangles
	 * whose raw index order alternates in apparent winding every other
	 * triangle. */

	d_FrameBegin(context);

	count = d_SeqCount(context);

	gl_EmitPrimitiveV3D(context, s_seq, count, V3D_PRIM_TRIANGLESTRIP, GL_FALSE, count);
}

/*
 * GL_QUADS/GL_QUAD_STRIP have no direct V3D primitive -- but the original
 * never decomposed a quad into two independent triangles either: it called
 * W3D_DrawTriFanV with vertexcount=4 (a quad IS a 2-triangle fan once its
 * 4 vertices are in perimeter order). V3D_PRIM_TRIANGLEFAN is a native V3D
 * primitive, so each quad is one 4-vertex TRIANGLEFAN. Unlike a fan or
 * strip, each quad in a GL_QUADS/GL_QUAD_STRIP call is an independent
 * primitive, so the loop below walks the vertex buffer a quad at a time
 * rather than handing the whole buffer over as d_DrawTriangleFan/Strip do.
 *
 * Backface culling here relies entirely on the GPU's own hardware cull
 * (CfgBits, gl_EmitCullBlendState). There is no CPU-side front-face test:
 * a winding test loses reliable precision as a quad's true screen-space
 * area shrinks.
 *
 * Concatenating several quads' indices into ONE VertexArrayPrims(
 * TRIANGLEFAN, N, 0) call would be WRONG, not just unbatched: a fan's
 * vertex count means "how big is this ONE connected shape", not "how many
 * independent shapes", unlike TRIANGLES/LINES/POINTS where more vertices of
 * the same primType naturally means more independent primitives. They are
 * batched anyway, following MESA's own v3d gallium driver (v3dx_draw.c),
 * which binds shader+attribute state once per GL draw call and then emits
 * VertexArrayPrims/INDEXED_PRIM_LIST as its own separate CL packet: state
 * binding and primitive emission are decoupled. gl_EmitPrimitiveV3D takes a
 * `chunk_size` parameter, so every quad's 4 indices accumulate into one
 * shared batch array and ONE gl_EmitPrimitiveV3D call builds ONE shared
 * attribute buffer + shader state record for the whole batch, then
 * internally issues N separate VertexArrayPrims(TRIANGLEFAN, 4, offset)
 * calls against it (one per quad, `offset` stepping by chunk_size=4 each
 * time) -- not one call with a bigger length, which would form one giant
 * fan.
 */
/* d_DrawQuads'/d_DrawQuadStrip's shared multi-color batching -- see
 * d_DrawQuadStrip's own comment for the rationale (GL_FLAT strips/quads that
 * alternate glColor3f between quads need more than one flat-color batch slot
 * or every color change forces its own draw call). File-scope since it
 * sizes static arrays shared by both functions. */
#define MAX_COLOR_BATCHES 4

void d_DrawQuads(GLcontext context)
{
	int i, k;
	/* Same multi-color batching as d_DrawQuadStrip: one shared batch has
	 * room for only ONE flat color, so GL_QUADS blocks that change glColor
	 * between quads need several slots. See d_DrawQuadStrip's own comment. */
	static int color_batch[MAX_COLOR_BATCHES][MGL_MAXVERTS];
	int color_batch_count[MAX_COLOR_BATCHES];
	v3d_u32 color_batch_color[MAX_COLOR_BATCHES];
	int num_active_batches = 0;

	for (k = 0; k < MAX_COLOR_BATCHES; k++)
		color_batch_count[k] = 0;

	D(("d_DrawQuads: entry, VertexBufferPointer=%ld\n", (LONG)context->VertexBufferPointer));

	if(context->VertexBufferPointer < 4)
	{
		D(("d_DrawQuads: fewer than 4 vertices, dropping\n"));
		return;
	}

	if(context->CullFace_State == GL_TRUE && context->CurrentCullFace == GL_FRONT_AND_BACK)
	{
		D(("d_DrawQuads: GL_FRONT_AND_BACK cull, dropping everything\n"));
		return;
	}

	/* No MGL_FLATFAN/MGL_FLATSTRIP equivalent reaches this path -- glBegin
	 * maps those to the fan and strip paths only. See d_DrawTriangles' own
	 * comment for what v_EnsureTransformState covers. */
	v_EnsureTransformState(context);

	d_FrameBegin(context);

	/* Whole quads only: a trailing one, two or three vertices draw
	 * nothing. */
	for (i=0; i+3<context->VertexBufferPointer; i+=4)
	{
		PrepTexCoords(context, i, 4, GL_FALSE);

		{
			/* Every quad goes through this color-batch path
			 * unconditionally; hardware decides visibility and facing.
			 * Each quad stays its own independent 4-vertex window (the
			 * perimeter order every W3D_DrawTriFanV call used), while the
			 * quads sharing one flat color share an attribute buffer and
			 * state record: every gl_EmitPrimitiveV3D call below passes
			 * chunk_size 4, so it emits one
			 * VertexArrayPrims(TRIANGLEFAN, 4, ...) per quad in that batch.
			 *
			 * Flat color read from the PROVOKING vertex (i+3, the last of
			 * this quad's 4 vertices in submission order -- GL_QUADS is
			 * already in perimeter order, no index swap needed here unlike
			 * d_DrawQuadStrip). Same "existing slot or new slot, flush
			 * oldest if full" multi-batch logic as that function -- see
			 * its own comment. */
			MGLVertex* pv = &context->VertexBuffer[i+3];
			v3d_u32 quadColor =
				  ((v3d_u32)(pv->color.a * 255.0f) << 24)
				| ((v3d_u32)(pv->color.b * 255.0f) << 16)
				| ((v3d_u32)(pv->color.g * 255.0f) << 8)
				|  (v3d_u32)(pv->color.r * 255.0f);
			int slot = -1;

			for (k = 0; k < num_active_batches; k++)
			{
				if (color_batch_color[k] == quadColor)
				{
					slot = k;
					break;
				}
			}

			if (slot == -1)
			{
				if (num_active_batches < MAX_COLOR_BATCHES)
				{
					slot = num_active_batches++;
				}
				else
				{
					D(("d_DrawQuads: color batch table full, flushing slot 0 (color %08lx, %ld quads) to make room\n",
					   (ULONG)color_batch_color[0], (LONG)(color_batch_count[0]/4)));
					context->backend.fixed_color = color_batch_color[0];
					gl_EmitPrimitiveV3D(context, color_batch[0], color_batch_count[0], V3D_PRIM_TRIANGLEFAN, GL_FALSE, 4);
					slot = 0;
				}
				color_batch_color[slot] = quadColor;
				color_batch_count[slot] = 0;
			}

			/* A slot holds MGL_MAXVERTS indices; send it before the quad
			 * that would overrun it. */
			if (color_batch_count[slot] + 4 > MGL_MAXVERTS)
			{
				context->backend.fixed_color = color_batch_color[slot];
				gl_EmitPrimitiveV3D(context, color_batch[slot], color_batch_count[slot], V3D_PRIM_TRIANGLEFAN, GL_FALSE, 4);
				color_batch_count[slot] = 0;
			}

			color_batch[slot][color_batch_count[slot]+0] = i+0;
			color_batch[slot][color_batch_count[slot]+1] = i+1;
			color_batch[slot][color_batch_count[slot]+2] = i+2;
			color_batch[slot][color_batch_count[slot]+3] = i+3;
			color_batch_count[slot] += 4;
		}
	}

	for (k = 0; k < num_active_batches; k++)
	{
		if (color_batch_count[k] > 0)
		{
			D(("d_DrawQuads: emitting batch of %ld quads (color %08lx)\n",
			   (LONG)(color_batch_count[k]/4), (ULONG)color_batch_color[k]));
			context->backend.fixed_color = color_batch_color[k];
			gl_EmitPrimitiveV3D(context, color_batch[k], color_batch_count[k], V3D_PRIM_TRIANGLEFAN, GL_FALSE, 4);
		}
	}
}

void d_DrawQuadStrip(GLcontext context)
{
	int i, k;
	/* Up to MAX_COLOR_BATCHES independently-tracked flat-color batches, not
	 * one: a single batch would have to be flushed on every color change,
	 * turning a strip that alternates color per quad into one draw call per
	 * quad. A strip only ever cycles through a small number of distinct
	 * flat colors in practice, so tracking a handful at once and flushing
	 * only when a genuinely new color shows up keeps the batching. */
	static int color_batch[MAX_COLOR_BATCHES][MGL_MAXVERTS];
	int color_batch_count[MAX_COLOR_BATCHES];
	v3d_u32 color_batch_color[MAX_COLOR_BATCHES];
	int num_active_batches = 0;

	for (k = 0; k < MAX_COLOR_BATCHES; k++)
		color_batch_count[k] = 0;

	D(("d_DrawQuadStrip: entry, VertexBufferPointer=%ld\n", (LONG)context->VertexBufferPointer));

	if(context->VertexBufferPointer < 4)
	{
		D(("d_DrawQuadStrip: fewer than 4 vertices, dropping\n"));
		return;
	}

	if(context->CullFace_State == GL_TRUE && context->CurrentCullFace == GL_FRONT_AND_BACK)
	{
		D(("d_DrawQuadStrip: GL_FRONT_AND_BACK cull, dropping everything\n"));
		return;
	}

	/* No MGL_FLATFAN/MGL_FLATSTRIP equivalent reaches this path -- glBegin
	 * maps those to the fan and strip paths only. See d_DrawTriangles' own
	 * comment for what v_EnsureTransformState covers. */
	v_EnsureTransformState(context);

	d_FrameBegin(context);

	PrepTexCoords(context, 0, context->VertexBufferPointer, GL_FALSE);

	/* Whole quads only: an odd trailing vertex draws nothing. */
	for (i=0; i+3<context->VertexBufferPointer; i+=2)
	{
		{
			/* Every quad goes through this color-batch path
			 * unconditionally; hardware decides visibility and facing.
			 *
			 * Per-quad flat color, read from the PROVOKING vertex (i+3,
			 * the last of this quad's 4 vertices in original submission
			 * order -- standard GL "last vertex" flat-shading convention)
			 * via the .color field GLVertex4f stamps for every vertex
			 * regardless of shading mode (see that function's own
			 * comment). Packed the same way the glColor* family packs
			 * context->backend.fixed_color, so the two are directly
			 * comparable.
			 *
			 * Looks for an EXISTING batch slot already tracking this
			 * exact color and appends to it; only starts a NEW slot (or,
			 * once all MAX_COLOR_BATCHES slots are in use, flushes the
			 * oldest one to make room) when a genuinely new color shows
			 * up. gl_EmitPrimitiveV3D reads backend->fixed_color ONCE per
			 * call as a single uniform for the whole batch, so quads with
			 * different flat colors can never share one call -- but quads
			 * that RECUR the same color can and should share one, rather
			 * than flushing on every single alternation. */
			MGLVertex* pv = &context->VertexBuffer[i+3];
			v3d_u32 quadColor =
				  ((v3d_u32)(pv->color.a * 255.0f) << 24)
				| ((v3d_u32)(pv->color.b * 255.0f) << 16)
				| ((v3d_u32)(pv->color.g * 255.0f) << 8)
				|  (v3d_u32)(pv->color.r * 255.0f);
			int slot = -1;

			for (k = 0; k < num_active_batches; k++)
			{
				if (color_batch_color[k] == quadColor)
				{
					slot = k;
					break;
				}
			}

			if (slot == -1)
			{
				if (num_active_batches < MAX_COLOR_BATCHES)
				{
					slot = num_active_batches++;
				}
				else
				{
					D(("d_DrawQuadStrip: color batch table full, flushing slot 0 (color %08lx, %ld quads) to make room\n",
					   (ULONG)color_batch_color[0], (LONG)(color_batch_count[0]/4)));
					context->backend.fixed_color = color_batch_color[0];
					gl_EmitPrimitiveV3D(context, color_batch[0], color_batch_count[0], V3D_PRIM_TRIANGLEFAN, GL_FALSE, 4);
					slot = 0;
				}
				color_batch_color[slot] = quadColor;
				color_batch_count[slot] = 0;
			}

			/* Quad-strip vertex order is (v0,v1,v2,v3,...) alternating
			 * bottom/top rows, NOT already perimeter order like GL_QUADS
			 * -- swap the last two indices to turn each 4-vertex window
			 * into a valid perimeter-order fan, exactly matching the
			 * original's own verts[2]=i+3/verts[3]=i+2 swap before its
			 * W3D_DrawTriFanV call. */
			/* A slot holds MGL_MAXVERTS indices; send it before the quad
			 * that would overrun it. */
			if (color_batch_count[slot] + 4 > MGL_MAXVERTS)
			{
				context->backend.fixed_color = color_batch_color[slot];
				gl_EmitPrimitiveV3D(context, color_batch[slot], color_batch_count[slot], V3D_PRIM_TRIANGLEFAN, GL_FALSE, 4);
				color_batch_count[slot] = 0;
			}

			color_batch[slot][color_batch_count[slot]+0] = i+0;
			color_batch[slot][color_batch_count[slot]+1] = i+1;
			color_batch[slot][color_batch_count[slot]+2] = i+3;
			color_batch[slot][color_batch_count[slot]+3] = i+2;
			color_batch_count[slot] += 4;
		}
	}

	for (k = 0; k < num_active_batches; k++)
	{
		if (color_batch_count[k] > 0)
		{
			D(("d_DrawQuadStrip: emitting batch of %ld quads (color %08lx)\n",
			   (LONG)(color_batch_count[k]/4), (ULONG)color_batch_color[k]));
			context->backend.fixed_color = color_batch_color[k];
			gl_EmitPrimitiveV3D(context, color_batch[k], color_batch_count[k], V3D_PRIM_TRIANGLEFAN, GL_FALSE, 4);
		}
	}
}

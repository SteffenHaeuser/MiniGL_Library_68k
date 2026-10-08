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


extern void gl_FrameBegin(GLcontext context); /* context.c */
extern void hc_ClipAndDrawPoly(GLcontext context, MGLPolygon *poly, ULONG or_codes); /* hclip.c */
extern void hc_ClipAndDrawLine(GLcontext context, MGLPolygon *poly, ULONG or_codes); /* hclip.c */
extern GLboolean hc_DecideFrontface(GLcontext context, MGLVertex *v0, MGLVertex *v1, MGLVertex *v2); /* hclip.c */
extern void m_CombineMatrices(GLcontext context); /* matrix.c */
extern void m_BuildInverted(GLcontext context); /* matrix.c */
extern int g_mgl_mtex_flush_pending; /* texture.c */

/* gl_FrameBegin returns at once while a pass is open and no split is pending;
 * that test is made here, so the common case costs no call. */
static inline void d_FrameBegin(GLcontext context)
{
	V3DContext* backend = &context->backend;

	if (!backend->frame_active || backend->force_new_pass)
		gl_FrameBegin(context);
}

/* From an earlier library by the same author, duplicated per this file's own
 * header comment */
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
	 * whole reason these go last rather than next to scale_p_y, which sits
	 * mid-stream because every shader reads it. */
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
void d_DrawWireframe     (GLcontext, int);   /* glPolygonMode GL_LINE/GL_POINT */
static void d_DrawWireframeLocked(GLcontext, int, GLenum, const GLvoid *, int);

/*
 * glPolygonMode, tested at the top of each polygon d_Draw*. Both faces must
 * agree: there is one decomposition, and a front mode differing from the back
 * would need the geometry emitted twice with complementary cull state. MESA
 * does not support that either on this hardware, and warns; here a mix simply
 * stays filled rather than being half-applied.
 */
#define WIREFRAME_WANTED(c) \
	((c)->CurPolygonMode != GL_FILL && (c)->CurPolygonMode == (c)->CurPolygonModeBack)
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
 * prologue is exactly what it would be without this feature, and the only thing
 * it costs is a reload per use on the rare path where a texture matrix really is
 * active.
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
 * The code memory is ONE PACKED allocation, sized from the variants' real
 * sizes, in the order the offsets below give -- no fixed slot stride. Adding a
 * variant needs no edit here; see v3d_shader_assembler.h's enum comment for
 * what it does need. */
/*
 * Where each shader variant's code sits, as a byte offset into
 * shader_code_mem. Filled by gl_EnsureShaders and read by every draw-time
 * selector, so the upload and the selection cannot disagree about an address.
 * The layout is identical for every context -- same shaders, same sizes -- so
 * one table serves them all.
 */
/*
 * SHADER ORDER. The shaders are laid end to end in this order and
 * g_shader_offset[n] is the running sum of the sizes before shader n, so
 * g_shader_offset[0] = 0, [1] = size of shader 0, [2] = [1] + size of shader 1.
 * The selectors below pick a shader by this index, so the address is one
 * table read and no shift.
 */
static const UBYTE g_shader_variant[V3D_MAX_SHADER_VARIANTS] =
{
	V3D_SHADER_VARIANT_VERTEX_TEXTURED,                        /*   0 */
	V3D_SHADER_VARIANT_COORDINATE_TEXTURED,                    /*   1 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED,                    /*   2 */
	V3D_SHADER_VARIANT_VERTEX_SMOOTH,                          /*   3 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH,             /*   4 */
	V3D_SHADER_VARIANT_VERTEX_SMOOTH_TEXTURED,                 /*   5 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH,               /*   6 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG,                /*   7 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST,          /*   8 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG,                  /*   9 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST,            /*  10 */
	V3D_SHADER_VARIANT_VERTEX_MULTITEXTURE,                    /*  11 */
	V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE,                  /*  12 */
	V3D_SHADER_VARIANT_COORDINATE_CLIPSPACE,                   /*  13 */
	V3D_SHADER_VARIANT_VERTEX_SMOOTH_TEXTURED_CLIPSPACE,       /*  14 */
	V3D_SHADER_VARIANT_VERTEX_MULTITEXTURE_CLIPSPACE,          /*  15 */
	V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_DECAL,            /*  16 */
	V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_REPLACE,          /*  17 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST_GREATER,  /*  18 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST_GREATER,    /*  19 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_COLORMOD,             /*  20 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST_GREATER, /*  21 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST,     /*  22 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST_NEVER,    /*  23 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST_NEVER,      /*  24 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST_NEVER, /*  25 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST_LESS,     /*  26 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST_LESS,       /*  27 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST_LESS, /*  28 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST_EQUAL,    /*  29 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST_EQUAL,      /*  30 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST_EQUAL, /*  31 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST_LEQUAL,   /*  32 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST_LEQUAL,     /*  33 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST_LEQUAL, /*  34 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST_NOTEQUAL, /*  35 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST_NOTEQUAL,   /*  36 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST_NOTEQUAL, /*  37 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST,      /*  38 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST,        /*  39 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST_GREATER, /*  40 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST_GREATER, /*  41 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST_LESS, /*  42 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST_LESS,   /*  43 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST_EQUAL, /*  44 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST_EQUAL,  /*  45 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST_LEQUAL, /*  46 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST_LEQUAL, /*  47 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST_NOTEQUAL, /*  48 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST_NOTEQUAL, /*  49 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST_NEVER, /*  50 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST_NEVER,  /*  51 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_FOG,         /*  52 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG,           /*  53 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST, /*  54 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST_GREATER, /*  55 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST_LESS, /*  56 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST_EQUAL, /*  57 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST_LEQUAL, /*  58 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST_NOTEQUAL, /*  59 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST_NEVER, /*  60 */
	V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_FOG,              /*  61 */
	V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_DECAL_FOG,        /*  62 */
	V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_REPLACE_FOG,      /*  63 */
	V3D_SHADER_VARIANT_VERTEX_LIT,                             /*  64 */
	V3D_SHADER_VARIANT_VERTEX_LIT_TEXTURED,                    /*  65 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_POINT_SMOOTH,       /*  66 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_POINT_SMOOTH, /*  67 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_POINT_SMOOTH,         /*  68 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_POINT_SMOOTH,  /*  69 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_ALPHATEST,  /*  70 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_ALPHATEST_GREATER,  /*  71 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_ALPHATEST_LESS,  /*  72 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_ALPHATEST_EQUAL,  /*  73 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_ALPHATEST_LEQUAL,  /*  74 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_ALPHATEST_NOTEQUAL,  /*  75 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_ALPHATEST_NEVER,  /*  76 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_FOG_ALPHATEST,  /*  77 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_FOG_ALPHATEST_GREATER,  /*  78 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_FOG_ALPHATEST_LESS,  /*  79 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_FOG_ALPHATEST_EQUAL,  /*  80 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_FOG_ALPHATEST_LEQUAL,  /*  81 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_FOG_ALPHATEST_NOTEQUAL,  /*  82 */
	V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_FOG_ALPHATEST_NEVER,  /*  83 */
	V3D_SHADER_VARIANT_VERTEX_LIT_REALW,                        /*  84 */
	V3D_SHADER_VARIANT_VERTEX_LIT_MULTITEXTURE,                 /*  85 */
	V3D_SHADER_VARIANT_FRAGMENT_LIT_MULTITEXTURE,               /*  86 */
	V3D_SHADER_VARIANT_VERTEX_LIT_COLORMATERIAL,                  /*  87 */
	V3D_SHADER_VARIANT_VERTEX_LIT_TEXTURED_COLORMATERIAL,         /*  88 */
	V3D_SHADER_VARIANT_VERTEX_LIT_REALW_COLORMATERIAL,            /*  89 */
	V3D_SHADER_VARIANT_VERTEX_LIT_MULTITEXTURE_COLORMATERIAL,     /*  90 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_COLORMOD_ENVADD,         /*  91 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_COLORMOD_ENVBLEND,       /*  92 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ENVADD,           /*  93 */
	V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ENVBLEND,         /*  94 */
};

static ULONG g_shader_offset[V3D_MAX_SHADER_VARIANTS];   /* byte offset of each shader */

static int gl_EnsureShaders(GLcontext context)
{
	V3DContext* backend = &context->backend;
	ULONG total = 0;
	int i;

	if (backend->shaders_ready)
		return 0;

	D(("gl_EnsureShaders: assembling and validating shaders\n"));

	if (!v3d_assemble_builtin_shaders(&context->device))
	{
		D(("gl_EnsureShaders: v3d_assemble_builtin_shaders failed\n"));
		return -1;
	}

	/*
	 * THE LAYOUT IS COMPUTED, NOT LITERAL. The shaders are laid end to end and
	 * each one's offset is recorded in g_shader_offset, which BOTH this upload
	 * and every selector read. They cannot disagree: there is one number per
	 * shader, not two literals that have to be kept equal by hand. Nothing
	 * imposes a per-shader stride, so nothing caps a shader's length below the
	 * host capacity, and the allocation is exactly what the code needs.
	 *
	 * Alignment comes free: the shader record holds the address >> 3, and every
	 * offset here is a sum of whole 8-byte instructions.
	 */
	for (i = 0; i < V3D_MAX_SHADER_VARIANTS; i++)
	{
		g_shader_offset[i] = total;
		total += (ULONG)v3d_shader_variants[g_shader_variant[i]].numInstructions * 8;
	}

	if (v3d_mem_alloc(&context->device, &backend->shader_code_mem, total) < 0)
	{
		D(("gl_EnsureShaders: v3d_mem_alloc failed for %ld bytes\n", (LONG)total));
		return -2;
	}
	/* No explicit memset here -- v3d_mem_alloc's own AllocVec call already
	 * uses MEMF_CLEAR, so this memory arrives pre-zeroed. */

	for (i = 0; i < V3D_MAX_SHADER_VARIANTS; i++)
	{
		const V3DAssembledShader* s = &v3d_shader_variants[g_shader_variant[i]];
		v3d_u64* dst = (v3d_u64*)((ULONG)backend->shader_code_mem.hostptr + g_shader_offset[i]);
		int k;

		for (k = 0; k < s->numInstructions; k++)
			dst[k] = byteswap64(s->instructions[k]);
	}

	{
		struct ExecBase* const SysBase = context->device.sysbase;
		ULONG len = (ULONG)backend->shader_code_mem.size;
		CachePreDMA(backend->shader_code_mem.hostptr, &len, 0);
	}

	backend->shaders_ready = TRUE;
	D(("gl_EnsureShaders: OK, %ld variants in %ld bytes (was 114 x 1024 = 116736), code_mem=%08lx\n",
	   (LONG)V3D_MAX_SHADER_VARIANTS, (LONG)total, (ULONG)backend->shader_code_mem.hostptr));
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

	/* POINT_SIZE and LINE_WIDTH are NOT here: glPointSize and glLineWidth are
	 * ordinary GL state that applies to every primitive issued after them, so
	 * a once-per-pass value would give a pass whichever size happened to be
	 * live at its first draw. They are emitted per draw from
	 * gl_EmitCullBlendState, dirty-cached so an unchanged value costs a float
	 * compare and no CL bytes. MESA sends POINT_SIZE on every draw on ver==42
	 * anyway (v3dx_emit.c:429-434, a binner-FIFO erratum workaround that needs
	 * "any CLE command" per draw).
	 *
	 * ColorWriteMasks is NOT here either, for the same reason as CfgBits:
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
	int   ps_valid;
	float ps;
	int   lw_valid;
	float lw;
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
		s_dd.ps_valid  = 0;
		s_dd.lw_valid  = 0;
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

		/* POINT_SIZE and LINE_WIDTH, per draw. glPointSize and glLineWidth
		 * apply to every primitive issued after them, so a value set between
		 * two draws of one pass has to reach the second. Dirty-cached: a
		 * program that sets the size once pays one float compare per draw and
		 * emits nothing. */
		if (!s_dd.ps_valid || s_dd.ps != context->CurrentPointSize)
		{
			PointSize(backend, context->CurrentPointSize);
			s_dd.ps = context->CurrentPointSize;
			s_dd.ps_valid = 1;
		}
		if (!s_dd.lw_valid || s_dd.lw != context->CurrentLineWidth)
		{
			LineWidth(backend, context->CurrentLineWidth);
			s_dd.lw = context->CurrentLineWidth;
			s_dd.lw_valid = 1;
		}

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
				 * GL-conventional magnitude.
				 *
				 * CALIBRATED AGAINST the depth-offset verification demo, which
				 * is a magnitude SWEEP, not a pass/fail test: it draws
				 * glPolygonOffset at -1000, -10 and -2 beside
				 * mglSetZOffset(-0.0009) on a real Z tie, and -2.0 is the
				 * smallest GL magnitude that reliably wins it on this
				 * hardware. 2222 is what puts the MGL convention's one
				 * canonical value there.
				 *
				 * DO NOT "FIX" THIS INTO THE DIMENSIONAL CONVERSION. A [0,1]
				 * delta against the hardware's 1/2^24 offset step would scale
				 * by 16777216, turning -0.0009 into -15099 units -- some 7500x
				 * the magnitude the sweep showed is needed, which peels
				 * surfaces apart instead of breaking a tie. The dimensional
				 * answer is right about units and wrong about this API: what
				 * callers pass is the Warp3D knob where -0.0009 means "nudge
				 * it just enough", not a depth delta they computed. */
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

/*
 * The object-space NORMAL, byte-swapped into the GPU's scratch, for a lit draw.
 *
 * Why a gather at all, rather than pointing an attribute record straight at
 * NormalBuffer -- there are two independent blockers and both are fatal:
 *
 *   - The attribute record has NO byte-order field. Every other attribute in
 *     this driver reaches the GPU big-endian-swapped through a packed scratch
 *     buffer for exactly this reason.
 *
 *   - MGLVertex.normal is a GLuint INDEX into NormalBuffer, not a normal. It
 *     advances once per glNormal3f CALL, so N consecutive vertices sharing one
 *     normal all carry the same index, and a vertex emitted before any
 *     glNormal3f carries 0. No base + index * stride record can follow that.
 *
 * WHICH SLOT. Exactly draw.c's texgen rule, and getting this wrong throws away
 * the most common way an application sets a normal:
 *
 *     nbp = (NormalBufferPointer > 0) ? v->normal : 0
 *
 * SLOT 0 IS NOT A FALLBACK, IT IS THE CURRENT NORMAL. GLBegin copies the last
 * pushed normal down into slot 0 and then rewinds the pointer to 0
 * (vertexbuffer_min.c), so a glNormal3f issued BEFORE glBegin -- which is how
 * nearly every application and every scene in a client application does it -- lives in
 * slot 0 with the pointer at 0. Substituting a constant there instead of
 * reading it discards that normal and shades the whole object with (0,0,1).
 * It is the same rule GL_CURRENT_NORMAL answers with (others.c).
 *
 * THE (0,0,1) SUBSTITUTION then applies only where it should: no glNormal3f has
 * ever happened, so slot 0 still holds the (0,0,0) context.c initialises it to,
 * while GL's default current normal is (0,0,1).
 *
 * It is detected by VALUE rather than by a "was a normal ever set" flag: the
 * value test needs no extra state and reads the same whichever path wrote the
 * normal.
 *
 * The cost of the value test, stated rather than hidden: an application that
 * really does set glNormal3f(0,0,0) gets (0,0,1) instead. GL would give it a zero
 * dot product and therefore ambient only. A zero normal is degenerate input --
 * texgen already skips it (see this file's v_GenTexCoords) -- and no real
 * application sets one, but it is a divergence and not a rounding difference.
 *
 * Doing the substitution here rather than changing NormalBuffer[0] at context
 * creation is what keeps it local: that global default feeds GL_SPHERE_MAP texgen
 * in every shipping game that never calls glNormal3f, and changing it would alter
 * their output.
 */
static inline void d_PackNormal3(FLOAT* p, const MGLVertex* v, const GLcontext context)
{
	int              nbp = (context->NormalBufferPointer > 0) ? (int)v->normal : 0;
	const MGLNormal* n   = &context->NormalBuffer[nbp];

	if (nbp == 0 && n->x == 0.0f && n->y == 0.0f && n->z == 0.0f)
	{
		swap_float32_into(&p[0], 0.0f);
		swap_float32_into(&p[1], 0.0f);
		swap_float32_into(&p[2], 1.0f);
		return;
	}

	swap_float32_into(&p[0], n->x);
	swap_float32_into(&p[1], n->y);
	swap_float32_into(&p[2], n->z);
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

/* One-shot latch for the "this texture environment has no variant" report,
 * a file static for the same reason the lit one below is. */
static int s_texenv_excl_said = 0;

/* One-shot latch for the "lighting is on but this draw cannot be lit" report.
 * A file static and not a context field: it is a diagnostic about the build's
 * capabilities, the same for every context, and a game could hit the condition
 * on every draw of every frame. */
static int s_lit_excl_said;

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
/* Set when the draw's fragment shader is a SMOOTH-shape alpha
 * test variant that neither D_SR_ALPHATEST nor D_SR_SMOOTH_ALPHATEST already
 * covers: the untextured smooth family, and a lit draw's alpha test under
 * GL_FLAT. It exists so the passthrough-Z flag stays 1:1 with the shaders that
 * carry a `tlbu` write. */
#define D_SR_SMOOTH_SHAPE_ALPHATEST 0x080UL
#define D_SR_NEEDS_REAL_W     0x100UL
/* GL_COLOR_MATERIAL on a lit draw: the colour attribute record comes BACK,
 * as a fourth record when textured and in texbuf2's place when not. */
#define D_SR_COLORMATERIAL    0x400UL
/* Lit: the draw takes a lit vertex shader, carries a NORMAL attribute record and
 * carries NO colour one. It is the tenth bit and it is not a refinement of
 * `smooth` -- see the varying count below for why it must not be keyed on it. */
#define D_SR_LIT              0x200UL

/* The lit uniform tail, read in this order by the lit vertex shaders; the
 * stream is positional so the order is the contract, not a convention:
 *   0..11   modelview rows 1..3, four words each, for P_eye = MV . (p,1)
 *   12..20  the 3x3 inverse-transpose by rows, for N_eye = InvT . n
 *   21..23  the light position, in EYE space
 *   24      shininess
 *   25..27  diffuse product
 *   28..30  specular product
 *   31..34  base colour rgb + alpha
 * There is no view direction: eye-space V is (0,0,1) for a non-local viewer,
 * so the shaders use a constant rather than three words. */
#define D_LIT_TAIL_FLOATS 35
#define D_LIT_TAIL_BYTES  (D_LIT_TAIL_FLOATS * sizeof(float))

/* GL_COLOR_MATERIAL EXTENDS that tail rather than rearranging it. Every term
 * the vertex colour replaces stays linear in it, so light.c folds each one to
 * K0 + K1 * C and these ten words are the K1 half:
 *   35..43  three words PER CHANNEL, in r,g,b order: the base scale
 *           (emission and ambient together), the diffuse scale, then the
 *           specular scale. Grouped by channel and not by component because
 *           that is how the shaders read it -- one scratch register carries
 *           k = base + diffuse*N.L + specular*spec for one channel at a time.
 *   44      alpha scale
 * Words 0..34 keep their meaning, which is why the five shaders that read the
 * plain tail are untouched. */
#define D_LIT_CM_TAIL_FLOATS 45
#define D_LIT_CM_TAIL_BYTES  (D_LIT_CM_TAIL_FLOATS * sizeof(float))

/* The template patch below relies on this layout: a 36-byte record whose
 * per-draw addresses are the whole words at 8, 16, 24 and 32, then 16-byte
 * attribute records whose address is their word 0. */
typedef char d_sr_layout_check[(sizeof(v3d_gl_shader_state_record) == 36 &&
                                sizeof(v3d_gl_shader_state_attribute_record) == 16) ? 1 : -1];

#define D_SR_MAXWORDS ((sizeof(v3d_gl_shader_state_record) + 4 * sizeof(v3d_gl_shader_state_attribute_record)) / 4)

/*
 * The shader state record and its attribute records, as gl_EmitPrimitiveV3DEx
 * builds them, written to dst (36 + 16 * 2 or 3 bytes). The attribute records
 * go through glShaderStateAttributeRecord against a local buffer over dst,
 * which places them directly after the record.
 */
static __attribute__((noinline)) void d_BuildShaderRecord(V3DContext* backend, v3d_u8* dst, ULONG shape,
                                                          ULONG frag_code_offset, ULONG vex_code_offset,
                                                          ULONG default_attr_values_address, ULONG unif_frag_address,
                                                          ULONG unif_vex_address, ULONG unif_coord_address,
                                                          FLOAT* posbuf, FLOAT* texbuf, FLOAT* texbuf2,
                                                          FLOAT* colbuf)
{
	GLboolean combined         = (shape & D_SR_COMBINED)         ? GL_TRUE : GL_FALSE;
	GLboolean smooth_alphatest = (shape & D_SR_SMOOTH_ALPHATEST) ? GL_TRUE : GL_FALSE;
	GLboolean smooth           = (shape & D_SR_SMOOTH)           ? GL_TRUE : GL_FALSE;
	GLboolean multitextured    = (shape & D_SR_MULTITEXTURED)    ? GL_TRUE : GL_FALSE;
	GLboolean textured         = (shape & D_SR_TEXTURED)         ? GL_TRUE : GL_FALSE;
	GLboolean alphatest        = (shape & D_SR_ALPHATEST)        ? GL_TRUE : GL_FALSE;
	GLboolean smooth_point     = (shape & D_SR_SMOOTH_POINT)     ? GL_TRUE : GL_FALSE;
	GLboolean needs_real_w     = (shape & D_SR_NEEDS_REAL_W)     ? GL_TRUE : GL_FALSE;
	GLboolean lit              = (shape & D_SR_LIT)              ? GL_TRUE : GL_FALSE;
	GLboolean smooth_shape_alphatest = (shape & D_SR_SMOOTH_SHAPE_ALPHATEST) ? GL_TRUE : GL_FALSE;
	GLboolean colormaterial    = (shape & D_SR_COLORMATERIAL)    ? GL_TRUE : GL_FALSE;
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
	/* LIT FIRST, and NOT keyed on `smooth`. A lit draw pairs with the smooth
	 * fragment shaders whatever the GL shade model is, because its whole output
	 * is a per-vertex colour that has to be interpolated -- so it needs 6
	 * varyings textured (s,t,r,g,b,a) and 4 untextured (r,g,b,a) regardless.
	 *
	 * Keying this on `smooth` would give a GL_FLAT lit textured draw 2 varyings
	 * and shift every varying read in the fragment shader. GL_FLAT plus lighting
	 * is wrong by construction anyway -- a per-vertex lit colour exists to be
	 * interpolated -- so the lit arm deliberately overrides the shade model here
	 * rather than honouring it. */
	shader->number_of_varyings_in_fragment_shader =
		lit ? (multitextured ? 8 : (textured ? 6 : 4))
		    : ((combined || smooth_alphatest) ? 6 : (smooth ? 4 : (multitextured ? 4 : (textured ? 2 : 0))));

	/* MESA sets this from prog_data.fs->lock_scoreboard_on_first_thrsw
	 * (v3dx_draw.c:490 and :583), and sets that flag true whenever a thrsw is
	 * emitted after a TLB load has already been emitted (nir_to_vir.c:167-172).
	 * Meaning: "do not wait until the LAST thread switch to take the scoreboard
	 * lock -- take it on the FIRST one", which is required when the shader
	 * reads the tile buffer, because the read must not happen before the lock.
	 * An unlocked ldtlb shows up as a tile-boundary "sprinkle" artifact.
	 *
	 * Set for exactly the shaders that read the TLB (ldtlb), as MESA does --
	 * locking the scoreboard earlier than needed costs inter-thread parallelism
	 * on every other draw. No shader reads the TLB, so this is 0. */
	shader->do_scoreboard_wait_on_first_thread_switch = 0;

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
		/* D_SR_SMOOTH_SHAPE_ALPHATEST covers the two shapes the first two
		 * predicates miss: the untextured smooth family, and a lit draw's
		 * alpha test under GL_FLAT, where `alphatest` is set but the shader
		 * taken is a smooth one. Both carry the same tlbu write. */
		GLboolean zwrite_shader = (alphatest || smooth_alphatest || smooth_point ||
		                           smooth_shape_alphatest) ? GL_TRUE : GL_FALSE;
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
	/* TWO SECTORS WHENEVER THE INPUT EXCEEDS 8 WORDS, which is the formula and
	 * not a shape list -- align(words, 8) / 8:
	 *   lit textured   3 position + 2 texcoord + 3 normal  = 8  -> 1
	 *   lit real-w     4 position + 2 texcoord + 3 normal  = 9  -> 2
	 *   combined       3 position + 2 texcoord + 4 colour   = 9  -> 2
	 *   smooth tex cs  4 position + 2 texcoord + 4 colour   = 10 -> 2 */
	shader->vertex_shader_input_vpm_segment_size =
		(((combined || smooth_alphatest) && !lit) || (lit && needs_real_w)
		  || (lit && multitextured) || (lit && colormaterial))
			? 2 : 1;
	shader->address_of_default_attribute_values = LE32(default_attr_values_address);

	shader->fragment_shader_code_address_rshift_3 = (ULONG)((ULONG)backend->shader_code_mem.hostptr + g_shader_offset[frag_code_offset]) >> 3;
	shader->fragment_shader_uniforms_address = LE32(unif_frag_address);
	shader->fragment_shader_4_way_threadable = TRUE;
	shader->fragment_shader_start_in_final_thread_section = FALSE;
	shader->fragment_shader_propagate_nans = TRUE;

	shader->vertex_shader_code_address_rshift_3 = (ULONG)((ULONG)backend->shader_code_mem.hostptr + g_shader_offset[vex_code_offset]) >> 3;
	shader->vertex_shader_uniforms_address = LE32(unif_vex_address);
	shader->vertex_shader_4_way_threadable = TRUE;
	shader->vertex_shader_start_in_final_thread_section = TRUE;
	shader->vertex_shader_propagate_nans = TRUE;

	/* A needs_real_w draw routes to the real-w coordinate shader (slot 13,
	 * COORDINATE_CLIPSPACE) -- position-only, generic across every variant,
	 * unchanged by which vertex shader is paired with it -- instead of the
	 * shared COORDINATE_TEXTURED (slot 1) every other variant reuses. Slot
	 * numbers, not byte offsets: the shaders are packed, so the address comes
	 * from g_shader_offset below rather than slot * 1024. Must
	 * agree with posbuf's own 4-component widening below, since the
	 * coordinate and vertex shaders consume the same attribute records. */
	shader->coordinate_shader_code_address_rshift_3 = (ULONG)((ULONG)backend->shader_code_mem.hostptr + g_shader_offset[needs_real_w ? 13 : 1]) >> 3;
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
	attrs.capacity   = 4 * sizeof(v3d_gl_shader_state_attribute_record);
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
	/*
	 * THE LIT RECORD SHAPES. A lit draw carries NO colour record at all -- GL
	 * fixed-function lighting ignores the per-vertex colour -- and that is what
	 * pays for the normal without a fourth record:
	 *
	 *   lit untextured : posbuf(3) + normbuf(3)              = 2 records
	 *   lit textured   : posbuf(3) + texbuf(2 s,t) + normbuf(3) = 3 records
	 *
	 * So attrs.capacity stays 3, D_SR_MAXWORDS stays 21, s_srec_tpl stays 21
	 * ULONGs, and the whole fourth-record chain never arises. Per-vertex
	 * attribute bytes actually go DOWN by 4 against the unlit equivalents.
	 *
	 * The normal rides the EXISTING texbuf/texbuf2 parameters rather than a new
	 * one: untextured it is texbuf, textured it is texbuf2. That keeps the
	 * `nwords > 17` test further down correct exactly as written.
	 *
	 * novrbcs is 0 for the normal, as it is for every non-position record: the
	 * binning pass runs the coordinate shader, which reads position only.
	 */
	if (lit && !textured)
	{
		/* texbuf carries the NORMAL here, 3 components, not a texcoord. */
		glShaderStateAttributeRecord(backend, texbuf, FALSE, FALSE, FALSE, v3d_VEC_3,
		                              v3d_ATTRIBUTE_FLOAT, 3, 0, 0, (3 * sizeof(float)), 0xFFFFFF);
	}
	else if (lit && multitextured)
	{
		/* FOUR components, both texcoord pairs, and the normal follows in
		 * texbuf2 exactly as it does for the lit textured arm below. */
		glShaderStateAttributeRecord(backend, texbuf, FALSE, FALSE, FALSE, v3d_VEC_4,
		                              v3d_ATTRIBUTE_FLOAT, 4, 0, 0, (4 * sizeof(float)), 0xFFFFFF);
	}
	else if (lit && textured)
	{
		/* TWO components, s and t, and NOT the `i` below -- a lit textured draw
		 * satisfies `combined`, so `i` would be 4 and declare a 4-component
		 * record over a buffer holding 2 floats per vertex. That does not merely
		 * read two words of rubbish: every attribute record's VPM offset follows
		 * the one before it, so a 4-word record here pushes the normal from words
		 * 5-7 to words 7-9 and the shader reads its normal from whatever sits at
		 * 5-7. The lit colour then tracks nothing recognisable. */
		glShaderStateAttributeRecord(backend, texbuf, FALSE, FALSE, FALSE, v3d_VEC_2,
		                              v3d_ATTRIBUTE_FLOAT, 2, 0, 0, (2 * sizeof(float)), 0xFFFFFF);
	}
	else
	{
		i = combined ? 4 : (smooth ? 4 : (multitextured ? 4 : 2));
		glShaderStateAttributeRecord(backend, texbuf, FALSE, FALSE, FALSE, v3d_VEC_4,
		                              v3d_ATTRIBUTE_FLOAT, i, 0, 0, (i * sizeof(float)), 0xFFFFFF);
	}

	if (lit && textured)
	{
		/* texbuf2 carries the normal, 3 components rather than the 2 a combined
		 * draw's (b,a) tail uses. */
		glShaderStateAttributeRecord(backend, texbuf2, FALSE, FALSE, FALSE, v3d_VEC_3,
		                              v3d_ATTRIBUTE_FLOAT, 3, 0, 0, (3 * sizeof(float)), 0xFFFFFF);
	}
	else if (lit && colormaterial)
	{
		/* Lit UNTEXTURED with colour material: texbuf is the normal and texbuf2
		 * is the COLOUR, four components. This shape leaves texbuf2 free, so it
		 * still has three records and needs no fourth. */
		glShaderStateAttributeRecord(backend, texbuf2, FALSE, FALSE, FALSE, v3d_VEC_4,
		                              v3d_ATTRIBUTE_FLOAT, 4, 0, 0, (4 * sizeof(float)), 0xFFFFFF);
	}
	else if (combined || smooth_alphatest)
	{
		glShaderStateAttributeRecord(backend, texbuf2, FALSE, FALSE, FALSE, v3d_VEC_2,
		                              v3d_ATTRIBUTE_FLOAT, 2, 0, 0, (2 * sizeof(float)), 0xFFFFFF);
	}

	/* THE FOURTH RECORD, and the only shape that has one: lit, textured and
	 * tracking the vertex colour. It goes LAST so the position, texcoord and
	 * normal inputs keep the indices they already had. */
	if (lit && textured && colormaterial)
	{
		glShaderStateAttributeRecord(backend, colbuf, FALSE, FALSE, FALSE, v3d_VEC_4,
		                              v3d_ATTRIBUTE_FLOAT, 4, 0, 0, (4 * sizeof(float)), 0xFFFFFF);
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
 * (sx*2.0f rounds only on overflow, but it is an FPU result).
 *
 * A LIT draw appends a 16-float tail, so the key carries three more terms: `lit`
 * (the block sizes differ), `light_serial` (bumped by every lighting call --
 * without it a second lit draw in one frame reuses the first's block, every other
 * term still matching) and `light_mask` SEPARATELY, because the tail is compacted
 * so which light lands in it depends on the mask. No new matrix term:
 * g_mglv3d_combined_serial already covers the modelview and its inverse.
 *
 * ONE slot, not two. A scene that alternates lit and unlit draws therefore
 * rewrites the block every draw -- 20 to 36 swap_float32_into calls -- which is
 * a throughput question, never a correctness one. Two slots indexed by `lit` would
 * fix it if it ever measures. */
typedef struct
{
	v3d_u8* sb_start;
	v3d_u32 gen;
	ULONG   serial, fpcr, sx, sy, sz, az;
	ULONG   vu, cu;
	ULONG   light_serial, light_mask;
	int     clip, lit, valid;
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
	/* Lit draws only: the object-space normal, fused onto the end of the posbuf
	 * claim. It is then handed to d_BuildShaderRecord through the texbuf or
	 * texbuf2 parameter depending on whether the draw is textured -- see the
	 * claim site and d_BuildShaderRecord's own lit record comment. */
	FLOAT* normbuf = NULL;
	FLOAT* colbuf  = NULL;   /* GL_COLOR_MATERIAL only; fused behind normbuf */
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
	GLboolean smooth_untex_alphatest;
	GLboolean lit_alphatest;
	/* Which alphatest shader slot the current alpha_func selects, for each
	 * of the shapes that carry an alpha test. Only meaningful when the
	 * matching umbrella predicate is GL_TRUE, and only evaluated then. */
	ULONG alphatest_code_offset = 0;
	ULONG smooth_alphatest_code_offset = 0;
	ULONG fog_alphatest_code_offset = 0;
	ULONG smooth_fog_alphatest_code_offset = 0;
	/* These two fold in the fog arm, because their families are contiguous and
	 * laid out plain-then-fogged, so one switch covers both. */
	ULONG smooth_untex_alphatest_code_offset = 0;
	ULONG lit_alphatest_code_offset = 0;
	GLboolean multitextured;
	V3DTexture* bound_tex2;
	ULONG textureShaderStateAddress2 = 0, textureSamplerStateAddress2 = 0;
	ULONG vex_code_offset, frag_code_offset;
	GLboolean replace_white;
	GLboolean env_add;
	GLboolean env_blend;
	GLboolean clipspace_combined;
	GLboolean clipspace_multitextured;
	GLboolean lit;
	GLboolean colormaterial;
	GLboolean needs_real_w;
	GLboolean draw_has_real_w;
	GLboolean real_w_combined;
	int multitex_env;
	GLboolean smooth_point;
	/* GL_TEXTURE -- see v_TexMatrixActive for why the texture matrix is
	 * applied on the way into the attribute buffer and nowhere earlier.
	 * `texmat_on` is resolved once, below. */
	volatile v_TexMatrix texmat;
	int texmat_on;
	/* This draw's state_buf/state_mem slot, loaded once: nothing this function
	 * calls writes build_slot, and GCC cannot know that, so it would recompute
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
	/* INCOMPLETE TEXTURES DO NOT TEXTURE. glBindTexture creates the object on
	 * first bind (GL 1.1 3.8.8), so an object can exist with no level 0 yet --
	 * width 0, no GPU allocation. GL disables texturing for such a unit, and
	 * sampling it here would emit state for memory that was never allocated. */
	if (bound_tex != NULL && bound_tex->width == 0)
		bound_tex = NULL;
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
	/* Incomplete on unit 1 too -- see the bound_tex test above. */
	if (bound_tex2 != NULL && bound_tex2->width == 0)
		bound_tex2 = NULL;
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

	/* Combine-mode dispatch for multitexture. multitex_env reads UNIT 1's
	 * environment -- context->TexEnv[1], which MGLDrawMultitexBuffer sets from
	 * its TexEnv argument and glTexEnvi sets through tex_SetEnv when unit 1 is
	 * the active texture unit (texture.c). Reading the bound texture's own
	 * texenv_mode instead would make the unit's combine change with every
	 * bind, which GL 1.1 3.8.9 forbids.
	 *
	 * A multitextured draw always takes the plain MODULATE/DECAL/REPLACE
	 * combine shader. There is no software-blend counterpart reading the tile
	 * buffer via `ldtlb` and blending in the shader: the fixed-function
	 * hardware blend (`be`, gl_EmitCullBlendState) is the single source of
	 * truth, and MGLDrawMultitexBuffer already calls GLBlendFunc internally
	 * (texture.c), so blending in the shader as well would blend twice over
	 * the same output. */
	multitex_env = 0;
	if (multitextured)
	{
		/* This draw is the multitextured one MGLDrawMultitexBuffer's state was
		 * meant for, so its pending flag is answered. Inside the existing
		 * multitextured branch, so an ordinary draw adds no term. */
		g_mgl_mtex_flush_pending = 0;

		if (bound_tex2)
		{
			if (context->TexEnv[1] == GL_DECAL)
				multitex_env = 1;
			else if (context->TexEnv[1] == GL_REPLACE)
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
		case V3D_ALPHAFUNC_NEVER:    smooth_alphatest_code_offset = 25; break;
		case V3D_ALPHAFUNC_LESS:     smooth_alphatest_code_offset = 28; break;
		case V3D_ALPHAFUNC_EQUAL:    smooth_alphatest_code_offset = 31; break;
		case V3D_ALPHAFUNC_LEQUAL:   smooth_alphatest_code_offset = 34; break;
		case V3D_ALPHAFUNC_GREATER:  smooth_alphatest_code_offset = 21; break;
		case V3D_ALPHAFUNC_NOTEQUAL: smooth_alphatest_code_offset = 37; break;
		case V3D_ALPHAFUNC_GEQUAL:   smooth_alphatest_code_offset = 22; break;
		default:                     smooth_alphatest_code_offset = 22; break;
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
	 * `smooth_alphatest` above, which covers the smooth+textured shape, and
	 * from `smooth_untex_alphatest` below, which covers the smooth untextured
	 * one. All three carry `!multitextured`, so a MULTITEXTURED draw still
	 * gets no alpha-test discard whatever the app's GL_ALPHA_TEST state says:
	 * no variant exists for that shape. A dedicated family is the fix there
	 * too, not relaxing this condition.
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
		case V3D_ALPHAFUNC_NEVER:    alphatest_code_offset = textured ? 24 : 23; break;
		case V3D_ALPHAFUNC_LESS:     alphatest_code_offset = textured ? 27 : 26; break;
		case V3D_ALPHAFUNC_EQUAL:    alphatest_code_offset = textured ? 30 : 29; break;
		case V3D_ALPHAFUNC_LEQUAL:   alphatest_code_offset = textured ? 33 : 32; break;
		case V3D_ALPHAFUNC_GREATER:  alphatest_code_offset = textured ? 19 : 18; break;
		case V3D_ALPHAFUNC_NOTEQUAL: alphatest_code_offset = textured ? 36 : 35; break;
		case V3D_ALPHAFUNC_GEQUAL:   alphatest_code_offset = textured ? 10 : 8;  break;
		default:                     alphatest_code_offset = textured ? 10 : 8;  break;
	}
	/* No shape exclusions: every primitive shape this driver can draw has a
	 * fog shader -- flat and smooth, untextured, textured and multitextured,
	 * with and without alpha test -- so fog depends only on whether the app
	 * asked for it. */
	fog = (context->backend.fog_enable) ? GL_TRUE : GL_FALSE;

	/* The smooth UNTEXTURED shape's alpha test -- the shape neither predicate
	 * above reaches, `alphatest` needing !smooth and `smooth_alphatest`
	 * needing textured. It is mutually exclusive with both by construction,
	 * so neither of them changes. Its family is contiguous and laid out plain
	 * (70..76) then fogged (77..83), so one switch covers fog as well. */
	smooth_untex_alphatest = (!textured && smooth && !multitextured &&
	           context->backend.alpha_func != V3D_ALPHAFUNC_ALWAYS &&
	           context->backend.alpha_test_enable == GL_TRUE) ? GL_TRUE : GL_FALSE;
	if (smooth_untex_alphatest)
	switch (context->backend.alpha_func)
	{
		case V3D_ALPHAFUNC_NEVER:    smooth_untex_alphatest_code_offset = fog ? 83 : 76; break;
		case V3D_ALPHAFUNC_LESS:     smooth_untex_alphatest_code_offset = fog ? 79 : 72; break;
		case V3D_ALPHAFUNC_EQUAL:    smooth_untex_alphatest_code_offset = fog ? 80 : 73; break;
		case V3D_ALPHAFUNC_LEQUAL:   smooth_untex_alphatest_code_offset = fog ? 81 : 74; break;
		case V3D_ALPHAFUNC_GREATER:  smooth_untex_alphatest_code_offset = fog ? 78 : 71; break;
		case V3D_ALPHAFUNC_NOTEQUAL: smooth_untex_alphatest_code_offset = fog ? 82 : 75; break;
		case V3D_ALPHAFUNC_GEQUAL:   smooth_untex_alphatest_code_offset = fog ? 77 : 70; break;
		default:                     smooth_untex_alphatest_code_offset = fog ? 77 : 70; break;
	}

	/* Smooth fog + alpha test slot, same shape as the flat fog_alphatest
	 * switch. Textured only -- smooth_alphatest itself requires textured,
	 * so there is no untextured arm to pick. */
	if (fog && smooth_alphatest)
	switch (context->backend.alpha_func)
	{
		case V3D_ALPHAFUNC_NEVER:    smooth_fog_alphatest_code_offset = 60; break;
		case V3D_ALPHAFUNC_LESS:     smooth_fog_alphatest_code_offset = 56; break;
		case V3D_ALPHAFUNC_EQUAL:    smooth_fog_alphatest_code_offset = 57; break;
		case V3D_ALPHAFUNC_LEQUAL:   smooth_fog_alphatest_code_offset = 58; break;
		case V3D_ALPHAFUNC_GREATER:  smooth_fog_alphatest_code_offset = 55; break;
		case V3D_ALPHAFUNC_NOTEQUAL: smooth_fog_alphatest_code_offset = 59; break;
		case V3D_ALPHAFUNC_GEQUAL:   smooth_fog_alphatest_code_offset = 54; break;
		default:                     smooth_fog_alphatest_code_offset = 54; break;
	}

	/* Combined fog + alpha test slot, same shape as the alphatest switch
	 * above. Only consulted when (fog && alphatest); there is no smooth arm
	 * because `alphatest` requires !smooth -- the smooth shape's own slot is
	 * smooth_fog_alphatest_code_offset just above. */
	if (fog && alphatest)
	switch (context->backend.alpha_func)
	{
		case V3D_ALPHAFUNC_NEVER:    fog_alphatest_code_offset = textured ? 51 : 50; break;
		case V3D_ALPHAFUNC_LESS:     fog_alphatest_code_offset = textured ? 43 : 42; break;
		case V3D_ALPHAFUNC_EQUAL:    fog_alphatest_code_offset = textured ? 45 : 44; break;
		case V3D_ALPHAFUNC_LEQUAL:   fog_alphatest_code_offset = textured ? 47 : 46; break;
		case V3D_ALPHAFUNC_GREATER:  fog_alphatest_code_offset = textured ? 41 : 40; break;
		case V3D_ALPHAFUNC_NOTEQUAL: fog_alphatest_code_offset = textured ? 49 : 48; break;
		case V3D_ALPHAFUNC_GEQUAL:   fog_alphatest_code_offset = textured ? 39 : 38; break;
		default:                     fog_alphatest_code_offset = textured ? 39 : 38; break;
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

	/*
	 * LIT. Two GL conditions and a list of exclusions.
	 *
	 * It has to sit HERE, after fog, alphatest and smooth_point are all settled,
	 * and not up beside needs_real_w where the rest of the shape predicates are
	 * -- those three are assigned further down than they look.
	 *
	 * ONE GL CONDITION, NOT TWO. GL lights nothing with GL_LIGHTING off however
	 * many lights are on, so Lighting_State is the test. A NON-ZERO LightMask is
	 * NOT also required: with GL_LIGHTING on and every light off GL still
	 * applies the material emission and the light-model ambient, so such a draw
	 * is lit and must not be sent down the unlit path.
	 *
	 * The reason given for excluding it -- no light to unroll, so the lit shader
	 * would get an empty uniform tail -- does not hold, and the tail is why.
	 * light_Fold ZEROES a disabled light's folded diffuse and specular, and folds
	 * emission plus LightModelAmbient * Material.Ambient into LitBase whatever
	 * the mask. With no light enabled the first-enabled-light scan below leaves
	 * li = 0, so the tail carries LitDiffuse[0] and LitSpecular[0] as zeros and
	 * the shader computes LitBase + N.L*0 + spec*0 == LitBase -- exactly GL's
	 * answer here. The empty tail is the right tail.
	 *
	 * It costs those draws the lit shader where the unlit one would do. That is
	 * the correct trade and it is narrow: no game here enables GL_LIGHTING.
	 *
	 * The remaining exclusions still apply, so a lighting-on-no-lights draw that
	 * also needs multitexture is still drawn unlit and still loses its emission.
	 *
	 * THE EXCLUSIONS EXIST BECAUSE THERE IS ONE LIT VARIANT PAIR, NOT A MATRIX --
	 * for the VERTEX-STAGE ones. A feature that lives in the vertex shader needs
	 * a lit twin of it, and none exists. FOG IS NOT ONE OF THOSE: it is wholly
	 * fragment-side, so the lit vertex shader pairs with the existing smooth-fog
	 * fragment shader and needs no twin. ALPHA TEST IS THE SAME: it is a fragment
	 * discard, so what it needs is the smooth-shape shader family, not a lit
	 * twin -- see lit_alphatest below.
	 *
	 * The exclusion that actually bites is needs_real_w: the vex_code_offset chain
	 * below tests (clipspace_combined || real_w_combined) FIRST, so without this
	 * term a lit real-w draw would be handed the unlit +16384 shader and its
	 * lighting would vanish silently, with no error raised anywhere.
	 *
	 * The one-shot E() is the point of deciding it here rather than letting arm
	 * order decide it: the first run then says WHICH exclusion fired, instead of
	 * it becoming a mystery about why one object in a lit scene came out flat.
	 */
	/* TWO EXCLUSIONS, both VERTEX-stage. Multitexture has its own vertex
	 * shader AND its fragment shaders take a flat colour from uniforms with no
	 * varying to put a lit one in, so that shape needs work on both stages.
	 * Clip-space positions route to their own vertex shaders too.
	 *
	 * A REAL W IS NOT ONE: real_w_combined requires `combined` or
	 * `smooth_alphatest`, both textured, and this predicate still excludes
	 * multitextured and clip space -- so for a lit draw needs_real_w can only
	 * mean that one shape, and VERTEX_LIT_REALW covers it. Its fragment side
	 * needs nothing, a fragment shader seeing only interpolated varyings.
	 *
	 * FOG, ALPHA TEST and SMOOTH POINTS are not exclusions either, for the
	 * same reason each time: a FRAGMENT-stage feature needs no lit vertex
	 * shader, only the right fragment partner, because the lit tail extends
	 * the VERTEX block alone. Fog's factor is the fragment's own interpolated
	 * w; alpha test is a fragment discard; a smooth point's coordinate is an
	 * IMPLICIT varying the hardware supplies and its size rides the fragment
	 * stream. Each pairs with an existing shader and costs arms in
	 * frag_code_offset, not a new shader. TEXTURED fog is included with the
	 * rest: FRAGMENT_TEXTURED_SMOOTH_FOG reads the two TMU configs then eight
	 * fog words and no colour, which is exactly what a lit textured fogged
	 * draw writes. */
	/* MULTITEXTURE IS ALLOWED FOR GL_MODULATE, UNFOGGED. That shape needs work
	 * on both stages, the multitexture fragment shaders having no colour input
	 * at all -- they compute texel0 x texel1 and read no uniform. GL_DECAL and
	 * the fogged combines would each need their own lit fragment shader and
	 * have none, so those still render unlit and still say so once per run.
	 * GL_REPLACE would need no new shader, texel1 correctly replacing the
	 * primary colour, but it is left out of the gate until decal exists rather
	 * than special-cased. */
	lit = (context->Lighting_State
	       && (!multitextured || (!fog && multitex_env == 0))
	       && !use_clip_space) ? GL_TRUE : GL_FALSE;

	/* GL_COLOR_MATERIAL only means anything on a draw that is actually lit, and
	 * every lit shape has a colour-material twin, so there is no exclusion here
	 * beyond `lit` itself. It costs the colour attribute record and ten more
	 * uniform words, which is why it is a separate shape rather than folded
	 * into the plain lit shaders. */
	colormaterial = (lit && context->ColorMaterial_State) ? GL_TRUE : GL_FALSE;

	if (context->Lighting_State && !lit && !s_lit_excl_said)
	{
		s_lit_excl_said = 1;
		E(("gl_EmitPrimitiveV3DEx: lighting is on but this draw renders UNLIT -- no "
		   "lit variant exists for its shape. multitextured=%ld env=%ld fog=%ld "
		   "clip_space=%ld. Said once per run.\n",
		   (LONG)multitextured, (LONG)multitex_env, (LONG)fog,
		   (LONG)use_clip_space));
	}

	/* THE LIT SHAPE'S ALPHA TEST. A lit draw's fragment stage is smooth
	 * whatever the shade model -- frag_code_offset's lit arm is deliberately
	 * not keyed on `smooth` -- so its alpha test must take a SMOOTH-shape
	 * family, never the flat one `alphatest` selects. Textured takes the
	 * existing textured smooth family; untextured takes the untextured family,
	 * whose plain and fogged halves are contiguous. Decided here because `lit`
	 * is only settled now, and read only by that arm.
	 *
	 * ALL FOUR COMBINATIONS ARE REACHABLE, textured or not and fogged or not,
	 * because lit allows textured fog -- so every arm is a 4-way. The textured
	 * fogged slots are the FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST family, which
	 * reads eight fog words then the threshold and consumes the TLB config with
	 * its tlbu write: the stream a lit textured fogged alpha-test draw writes,
	 * word for word. */
	lit_alphatest = (lit && context->backend.alpha_test_enable == GL_TRUE &&
	                 context->backend.alpha_func != V3D_ALPHAFUNC_ALWAYS) ? GL_TRUE : GL_FALSE;
	if (lit_alphatest)
	switch (context->backend.alpha_func)
	{
		case V3D_ALPHAFUNC_NEVER:    lit_alphatest_code_offset = textured ? (fog ? 60 : 25) : (fog ? 83 : 76); break;
		case V3D_ALPHAFUNC_LESS:     lit_alphatest_code_offset = textured ? (fog ? 56 : 28) : (fog ? 79 : 72); break;
		case V3D_ALPHAFUNC_EQUAL:    lit_alphatest_code_offset = textured ? (fog ? 57 : 31) : (fog ? 80 : 73); break;
		case V3D_ALPHAFUNC_LEQUAL:   lit_alphatest_code_offset = textured ? (fog ? 58 : 34) : (fog ? 81 : 74); break;
		case V3D_ALPHAFUNC_GREATER:  lit_alphatest_code_offset = textured ? (fog ? 55 : 21) : (fog ? 78 : 71); break;
		case V3D_ALPHAFUNC_NOTEQUAL: lit_alphatest_code_offset = textured ? (fog ? 59 : 37) : (fog ? 82 : 75); break;
		case V3D_ALPHAFUNC_GEQUAL:   lit_alphatest_code_offset = textured ? (fog ? 54 : 22) : (fog ? 77 : 70); break;
		default:                     lit_alphatest_code_offset = textured ? (fog ? 54 : 22) : (fog ? 77 : 70); break;
	}

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
	 * first one packed (s_lockpos).
	 *
	 * A LIT DRAW IS EXCLUDED, and not for tidiness. normbuf is fused into the
	 * posbuf claim below, so reusing a cached posbuf would also reuse whatever
	 * normals sat behind it -- and this cache's key has no normal term at all,
	 * while g_mglv3d_lock_epoch is bumped only by lock/unlock and position-array
	 * changes, never by glNormal3f. A lit object would then be shaded by the
	 * previous draw's normals, which reads as lighting that does not follow the
	 * geometry. */
	if (ix != NULL && !needs_real_w && !use_clip_space && !lit &&
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
		/*
		 * ONE claim, with normbuf FUSED onto the end of posbuf for a lit draw --
		 * never a second claim.
		 *
		 * A second claim would add another v3d_cl_claim_fast point, and the grow
		 * path "allocates a bigger block and claims from the start of it. Nothing
		 * already claimed is copied" (v3d_clbuf.h). So if the normal's claim were
		 * the one that grew the buffer, posbuf would still point into the
		 * abandoned block while normbuf pointed into the new one, and the draw
		 * would read positions from freed memory. Fusing makes that impossible by
		 * construction rather than by ordering luck. It is the same hazard the
		 * texbuf/texbuf2 pair already documents further down.
		 *
		 * THE NORMAL'S OFFSET FOLLOWS THE POSITION STRIDE, which is 4 components
		 * for a real w and 3 otherwise -- the same expression the claim above
		 * uses. A fixed `count * 3` would land the normals on the last quarter
		 * of the positions for a real-w draw, and nothing would rasterise.
		 */
		posbuf = (FLOAT*)v3d_cl_claim_fast(&context->device, sm, sb,
		                                    count * ((needs_real_w ? 4 : 3) + (lit ? 3 : 0)
		                                             + (colormaterial ? 4 : 0)) * sizeof(float),
		                                    &backend->frame);
		/* The NULL test is not a claim-failure guard -- nothing in this function
		 * guards posbuf or texbuf, because the claim grows rather than fails, and
		 * adding one here alone would be inconsistent. It is there because
		 * NULL + count * 3 is a small NON-null wild pointer, which would write
		 * over low memory instead of trapping. This keeps a failed claim behaving
		 * exactly the way it already does for posbuf. */
		if (lit && posbuf != NULL)
			normbuf = posbuf + (count * (needs_real_w ? 4 : 3));
		/* FUSED behind the normal, for the same reason the normal is fused
		 * behind the position: a separate claim that grew state_buf would leave
		 * both earlier pointers in the abandoned block. A lit draw is already
		 * excluded from the position cache, so no cached block is ever read
		 * with this layout assumed of it. */
		if (colormaterial && posbuf != NULL)
			colbuf = normbuf + (count * 3);
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
	/*
	 * THE LIT ARMS FIRST. A lit draw carries no colour, so what the records hold
	 * differs from every unlit shape:
	 *
	 *   lit untextured : texbuf IS normbuf. No claim at all is made here -- the
	 *                    normal was already claimed, fused onto posbuf above.
	 *   lit textured   : texbuf is s,t (2 floats) and texbuf2 IS normbuf, again
	 *                    already claimed.
	 *
	 * So the lit path claims strictly less than the unlit one, and the
	 * single-claim rule the comment above insists on is satisfied trivially:
	 * there is only ever one claim in flight for the normal, the fused one.
	 */
	if (lit && !textured)
	{
		texbuf  = normbuf;
		/* The colour rides texbuf2 here, which this shape otherwise leaves
		 * free -- the same trick the normal already plays with texbuf. */
		texbuf2 = colormaterial ? colbuf : NULL;
	}
	else if (lit && multitextured)
	{
		/* Both texcoord pairs; the normal is the fused one, as for lit textured. */
		texbuf  = (FLOAT*)v3d_cl_claim_fast(&context->device, sm, sb,
		                                     count * 4 * sizeof(float), &backend->frame);
		texbuf2 = normbuf;
	}
	else if (lit && textured)
	{
		texbuf  = (FLOAT*)v3d_cl_claim_fast(&context->device, sm, sb,
		                                     count * 2 * sizeof(float), &backend->frame);
		texbuf2 = normbuf;
	}
	else if (combined || smooth_alphatest)
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
	/* Unit 0's environment, read from the UNIT and not from the bound texture:
	 * GL 1.1 3.8.9 makes it unit state, so binding a different texture must not
	 * change it. bound_tex is still tested because the environment only applies
	 * when there is a texture to apply it to.
	 *
	 * ONE SWITCH, ONE READ, AND NOTHING ON THE COMMON PATH. GL_MODULATE lands in
	 * `default` and costs a single jump; the shape test that GL_ADD and GL_BLEND
	 * need is computed inside their own arm, where it is the only place it is
	 * read. Per-draw dispatch terms are measurable fps on this renderer, and
	 * every draw in every game arrives here with GL_MODULATE.
	 *
	 * GL_REPLACE and GL_DECAL need no shader: white as the primary colour makes
	 * the modulate the textured shaders already compute into the identity.
	 * GL_ADD and GL_BLEND are different arithmetic and have their own, for the
	 * two base shapes only -- `combined` and the terminal flat-textured case.
	 * Fog, alpha test, lighting, smooth points and multitexture each dispatch
	 * into a family with no GL_ADD or GL_BLEND member, so such a draw renders
	 * GL_MODULATE and says so once per run. */
	replace_white = GL_FALSE;
	env_add       = GL_FALSE;
	env_blend     = GL_FALSE;
	if (bound_tex)
	{
		GLenum e0 = context->TexEnv[0];

		switch (e0)
		{
			case GL_REPLACE:
			case GL_DECAL:
				replace_white = GL_TRUE;
				break;

			case GL_ADD:
			case GL_BLEND:
				if (!fog && !alphatest && !smooth_alphatest && !lit
				    && !smooth_point && !multitextured)
				{
					if (e0 == GL_ADD) env_add   = GL_TRUE;
					else              env_blend = GL_TRUE;
				}
				else if (!s_texenv_excl_said)
				{
					s_texenv_excl_said = 1;
					E(("gl_EmitPrimitiveV3DEx: texture environment %s renders as GL_MODULATE -- "
					   "no variant exists for this shape. fog=%ld alphatest=%ld lit=%ld "
					   "point=%ld multitextured=%ld. Said once per run.\n",
					   (e0 == GL_ADD) ? "GL_ADD" : "GL_BLEND",
					   (LONG)fog, (LONG)(alphatest || smooth_alphatest), (LONG)lit,
					   (LONG)smooth_point, (LONG)multitextured));
				}
				break;

			default:
				break;
		}
	}

	/* One test for the whole draw. Nothing below reads texmat unless this is
	 * non-zero, so the coefficients it leaves behind for an identity matrix are
	 * never used. */
	texmat_on = v_TexMatrixActive(context, &texmat);

	/* Every predicate below is constant for the draw, so the shape is chosen
	 * once and each shape has its own loop. The per-vertex chain at the end
	 * is kept for texture-matrix draws (their volatile coefficients stay in
	 * that loop), and it is the reference for what each loop writes. */
	/*
	 * THE LIT ARM MUST COME FIRST, and this is the one place in the whole lit
	 * path where getting the order wrong is silently destructive rather than
	 * merely wrong: for a lit untextured draw texbuf IS normbuf, so any arm below
	 * that packs a colour into texbuf would overwrite the normals with colours.
	 * The geometry would still draw, lit by whatever the colour bytes happen to
	 * mean as a direction.
	 *
	 * texmat_on is honoured here too: a lit draw with a texture matrix active
	 * still needs its s,t transformed, so it falls through to the generic
	 * per-vertex chain at the end -- which is why the lit predicate does NOT
	 * exclude texmat_on, but this fast arm does.
	 */
	if (!texmat_on && lit)
	{
		FLOAT* n = normbuf;
		FLOAT* t = texbuf;
		FLOAT* c = colbuf;

		if (seq)
		{
			const MGLVertex* v = vb;

			for (i = 0; i < count; i++, v++, n += 3)
			{
				d_PackNormal3(n, v, context);
				if (textured)
				{
					swap_float32_into(&t[0], v->v.u0);
					swap_float32_into(&t[1], v->v.v0);
					/* Unit 1's pair for a lit multitextured draw. The record
					 * declares four components and the vertex shader reads this
					 * pair at inputs 5 and 6. */
					if (multitextured)
					{
						swap_float32_into(&t[2], v->v.u1);
						swap_float32_into(&t[3], v->v.v1);
						t += 4;
					}
					else
						t += 2;
				}
				/* GL_COLOR_MATERIAL's own record, written last because it is
				 * declared last. */
				if (colormaterial)
				{
					swap_float32_into(&c[0], v->color.r);
					swap_float32_into(&c[1], v->color.g);
					swap_float32_into(&c[2], v->color.b);
					swap_float32_into(&c[3], v->color.a);
					c += 4;
				}
			}
		}
		else
		{
			for (i = 0; i < count; i++, n += 3)
			{
				const MGLVertex* v = &vb[indices[i]];

				d_PackNormal3(n, v, context);
				if (textured)
				{
					swap_float32_into(&t[0], v->v.u0);
					swap_float32_into(&t[1], v->v.v0);
					/* Unit 1's pair for a lit multitextured draw. The record
					 * declares four components and the vertex shader reads this
					 * pair at inputs 5 and 6. */
					if (multitextured)
					{
						swap_float32_into(&t[2], v->v.u1);
						swap_float32_into(&t[3], v->v.v1);
						t += 4;
					}
					else
						t += 2;
				}
				/* GL_COLOR_MATERIAL's own record, written last because it is
				 * declared last. */
				if (colormaterial)
				{
					swap_float32_into(&c[0], v->color.r);
					swap_float32_into(&c[1], v->color.g);
					swap_float32_into(&c[2], v->color.b);
					swap_float32_into(&c[3], v->color.a);
					c += 4;
				}
			}
		}
	}
	else if (!texmat_on && (combined || smooth_alphatest))
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
		/* NOT for envblend: that shape's stream is this pair PLUS three
		 * environment-colour words, so a cached address describing the pair
		 * alone does not describe it. envadd reads no uniform of its own and
		 * stays cacheable. */
		d_texcache_entry* pe = (combined && !fog && !smooth_point && !env_blend &&
		                        tex0_entry != NULL &&
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

			/* No 5th uniform here: only the software-blend variants read one
			 * (ldunifrf.rf24, right after the two wrtmuc-consumed TMU config
			 * pairs), and those are gone. The combine shaders stop at four. */
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
		 * below, which always writes these four words.
		 *
		 * `lit` is excluded for the same reason as the smooth shapes -- a lit
		 * draw's colour arrives as a varying -- and it has to be, because a
		 * lit textured draw can carry an alpha test: under GL_FLAT neither
		 * `combined` nor `smooth` is set, so without this term four unread
		 * colour words would sit between the TMU configs and the threshold
		 * and the shader would read red as its alpha reference. For a lit
		 * textured draw WITHOUT alpha test the shader reads no uniforms past
		 * the TMU pair, so dropping them changes nothing but the stream size. */
		if (!multitextured && !combined && !(smooth && !multitextured) && !lit)
		{
			FLOAT* ca = (FLOAT*)v3d_cl_claim_fast(&context->device, sm, sb, 4 * sizeof(float), &backend->frame);

			/* GL_REPLACE and GL_DECAL: white, so the shader's multiply by this
			 * colour is the identity and the texel passes through. These are
			 * the words the COLORMOD shader and the flat fog and alpha-test
			 * families all modulate by, which is why one substitution covers
			 * every flat shape -- the same trick d_PackCombinedWhite plays per
			 * vertex for a smooth draw.
			 *
			 * NOT memoized: s_colm's key is fixed_color, so white stored under
			 * it would be served to a GL_MODULATE draw of the same colour.
			 * Four constant words need no memo anyway. */
			if (replace_white)
			{
				swap_float32_into(&ca[0], 1.0f);
				swap_float32_into(&ca[1], 1.0f);
				swap_float32_into(&ca[2], 1.0f);
				swap_float32_into(&ca[3], 1.0f);
			}
			/* The words are memoized on fixed_color and FPCR (s_colm). */
			else if (d_ColorMemoCopy((ULONG*)ca, backend, sb->start, fpcr))
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

		/* GL_BLEND's environment colour, three words, red/green/blue in the same
		 * order the four colour words above use -- so Cc's red pairs with the
		 * texel half Cp's red does. Written for envblend ONLY: a word written
		 * for a shader that does not read it desynchronises the stream, and
		 * envadd reads none of its own.
		 *
		 * GL_BLEND's alpha is Ap * As, with no environment term, so the fourth
		 * component is not written. */
		if (env_blend)
		{
			FLOAT* ec = (FLOAT*)v3d_cl_claim_fast(&context->device, sm, sb,
			                                       3 * sizeof(float), &backend->frame);

			swap_float32_into(&ec[0], context->TexEnvColor[0][0]);
			swap_float32_into(&ec[1], context->TexEnvColor[0][1]);
			swap_float32_into(&ec[2], context->TexEnvColor[0][2]);
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
	/* The two smooth-shape sources are here for the same reason: their shaders
	 * read the threshold with ldunifrf and consume the config word with the
	 * same tlbu write. For the untextured smooth shapes the index works out
	 * identically to the flat untextured one -- the smooth family's four
	 * ldunifrf.rf24 reads step over the same four colour words the flat family
	 * reads for real. */
	if (alphatest || smooth_alphatest || smooth_untex_alphatest || lit_alphatest)
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
	 * The live glPointSize is the right value because POINT_SIZE is emitted
	 * per draw: the shader and the rasterizer therefore measure against the
	 * same size, which a per-pass latch could not guarantee. */
	if (smooth_point)
	{
		ULONG* pu = (ULONG*)v3d_cl_claim_fast(&context->device, sm, sb, 2 * sizeof(ULONG), &backend->frame);
		swap_float32_into(&pu[0], context->CurrentPointSize);
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
	    s_vum.sz == d_Bits(&context->sz) && s_vum.az == d_Bits(&context->az) &&
	    s_vum.lit == (int)lit && s_vum.light_serial == context->LightSerial &&
	    s_vum.light_mask == context->LightMask)
	{
		unif_vex_address = s_vum.vu;
		unif_coord_address = s_vum.cu;
	}
	else
	{
	scale_p = context->sx * 2.0f;
	scale_p_y = context->sy * 2.0f;

	/* ONE claim for the block AND the tail: the stream is positional, so they must
	 * be contiguous, and a separate claim that grew state_buf would land in a new
	 * block while unif_vex_address still pointed at the old. Unlit still claims 80. */
	vu = (v3d_my_uniforms*)v3d_cl_claim_fast(&context->device, sm, sb,
	                                          sizeof(v3d_my_uniforms)
	                                            + (lit ? (colormaterial ? D_LIT_CM_TAIL_BYTES
	                                                                    : D_LIT_TAIL_BYTES) : 0),
	                                          &backend->frame);
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

	/* The lit tail, 35 floats in the order the shader reads them. The two z reads
	 * above must stay the LAST before it or the tail's first word arrives as
	 * z_scale.
	 *
	 * LIGHTING IS IN EYE SPACE. Object space is exact only where the modelview's
	 * 3x3 is orthogonal times a uniform scale: the unnormalised dot is fine
	 * either way, dot((M^-1)^T n, d) being dot(n, M^-1 d), but the divisor is
	 * |n| where GL wants |(M^-1)^T n|, and the half-vector comes out as
	 * normalize(L_obj + V_obj), which is not normalize(L + V) carried backwards.
	 * The divergence runs to 62 grey levels of 255.
	 *
	 * So the shader gets the modelview and the normal matrix and does the work
	 * per vertex. Two 4x4 matrices in the tail fit: the plain lit shaders are
	 * 170 and 175 instructions of 256. */
	if (lit)
	{
		FLOAT*  tail = (FLOAT*)(vu + 1);
		GLfloat inv[16];
		GLfloat it[9];
		GLfloat lx, ly, lz, lw;
		int     li = 0;
		int     k;

		/* First enabled light. The tail is compacted, so whichever light is on
		 * lands in slot 0 -- the demo uses GL_LIGHT1 and never GL_LIGHT0. */
		for (k = 0; k < MGL_MAX_LIGHTS; k++)
		{
			if (context->LightMask & (1U << k)) { li = k; break; }
		}

		/* The normal matrix: the 3x3 INVERSE-TRANSPOSE, so inv's upper 3x3 read
		 * by COLUMNS. m_Invert4 is needed for this. */
		if (m_Invert4(CurrentMV->v, inv))
		{
			const MGLLight* L = &context->Light[li];

			/* The light is ALREADY in eye space: light.c transforms GL_POSITION by
			 * the modelview in force when it is specified, which is GL's rule, so
			 * it is taken as given. */
			lx = L->Position[0];
			ly = L->Position[1];
			lz = L->Position[2];
			lw = L->Position[3];

			#define b(x) (inv[OF_##x])
			it[0] = b(11); it[1] = b(21); it[2] = b(31);
			it[3] = b(12); it[4] = b(22); it[5] = b(32);
			it[6] = b(13); it[7] = b(23); it[8] = b(33);
			#undef b

			if (lw == 0.0f)
			{
				/* DIRECTIONAL. The shader has no branch -- the text assembler
				 * rejects them -- and it always computes L = L_obj - P_obj, which
				 * is only right for a point light. So a direction is passed as a
				 * point very far along it: L = dir*K - P normalises back to dir,
				 * and the shader needs no extra instruction on a variant that is
				 * already exactly full.
				 *
				 * The error is bounded and below the output precision: the angle
				 * error is |P|/K, so at K = 1e8 a vertex a thousand units from the
				 * origin is off by 1e-5, against the 1/255 that eight bits can
				 * show. |L|^2 reaches 1e16, far inside float range. */
				const GLfloat K = 100000000.0f;
				lx *= K; ly *= K; lz *= K;
			}
			else if (lw != 1.0f)
			{
				GLfloat r = 1.0f / lw;
				lx *= r; ly *= r; lz *= r;
			}
		}
		else
		{
			/* Singular modelview: -z and an identity normal matrix rather than
			 * infinities in the tail. */
			int q;
			lx = 0.0f; ly = 0.0f; lz = 1.0f; lw = 0.0f;
			for (q = 0; q < 9; q++)
				it[q] = (q % 4 == 0) ? 1.0f : 0.0f;
		}

		/* Modelview rows 1..3, for P_eye. The fourth row is not read: the
		 * shader needs only xyz of the eye position. */
		#define a(x) (CurrentMV->v[OF_##x])
		swap_float32_into(&tail[0],  a(11)); swap_float32_into(&tail[1],  a(12));
		swap_float32_into(&tail[2],  a(13)); swap_float32_into(&tail[3],  a(14));
		swap_float32_into(&tail[4],  a(21)); swap_float32_into(&tail[5],  a(22));
		swap_float32_into(&tail[6],  a(23)); swap_float32_into(&tail[7],  a(24));
		swap_float32_into(&tail[8],  a(31)); swap_float32_into(&tail[9],  a(32));
		swap_float32_into(&tail[10], a(33)); swap_float32_into(&tail[11], a(34));
		#undef a
		for (k = 0; k < 9; k++)
			swap_float32_into(&tail[12 + k], it[k]);
		swap_float32_into(&tail[21], lx);
		swap_float32_into(&tail[22], ly);
		swap_float32_into(&tail[23], lz);
		/* Shininess raw, not folded: the shader raises (N.H) to it via
		 * exp2(s * log2(N.H)), so any value in GL's [0,128] works. */
		swap_float32_into(&tail[24], context->Material.Shininess);
		swap_float32_into(&tail[25], context->LitDiffuse[li][0]);
		swap_float32_into(&tail[26], context->LitDiffuse[li][1]);
		swap_float32_into(&tail[27], context->LitDiffuse[li][2]);
		swap_float32_into(&tail[28], context->LitSpecular[li][0]);
		swap_float32_into(&tail[29], context->LitSpecular[li][1]);
		swap_float32_into(&tail[30], context->LitSpecular[li][2]);
		swap_float32_into(&tail[31], context->LitBase[0]);
		swap_float32_into(&tail[32], context->LitBase[1]);
		swap_float32_into(&tail[33], context->LitBase[2]);
		swap_float32_into(&tail[34], context->LitBase[3]);

		/* GL_COLOR_MATERIAL's K1 half. Zero for every term the vertex colour
		 * does not replace, so a shader reading these on a draw whose mode
		 * tracks nothing computes exactly what the plain tail gives. */
		if (colormaterial)
		{
			/* GROUPED BY CHANNEL, three words each -- base, diffuse,
			 * specular -- which is what the shaders read: one scratch
			 * register carries k = base + diffuse*N.L + specular*spec per
			 * channel. Writing them grouped by COMPONENT instead put
			 * BaseC_g where the shader wanted DiffC_r. */
			swap_float32_into(&tail[35], context->LitBaseC[0]);
			swap_float32_into(&tail[36], context->LitDiffuseC[li][0]);
			swap_float32_into(&tail[37], context->LitSpecularC[li][0]);
			swap_float32_into(&tail[38], context->LitBaseC[1]);
			swap_float32_into(&tail[39], context->LitDiffuseC[li][1]);
			swap_float32_into(&tail[40], context->LitSpecularC[li][1]);
			swap_float32_into(&tail[41], context->LitBaseC[2]);
			swap_float32_into(&tail[42], context->LitDiffuseC[li][2]);
			swap_float32_into(&tail[43], context->LitSpecularC[li][2]);
			swap_float32_into(&tail[44], context->LitBaseC[3]);
		}
	}

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
		s_vum.lit          = (int)lit;
		s_vum.light_serial = context->LightSerial;
		s_vum.light_mask   = context->LightMask;
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
	/* LIT ARM FIRST. It is safe there precisely because the lit predicate above
	 * excludes every shape the arms below claim -- clip space, real w and
	 * multitexture among them -- so putting it first perturbs no existing arm
	 * while guaranteeing a lit draw cannot be swallowed by one. 90112 and 91136
	 * are the reclaimed slots 88 and 89 (v3d_assembler.c). */
	/* A lit real-w draw is necessarily textured, so this arm needs no
	 * textured test -- see the lit predicate above for why. */
	/* COLOUR MATERIAL FIRST INSIDE THE LIT ARM: it is a twin of each shape
	 * below, not a shape of its own, so it has to be tested before the shape
	 * it twins. 87..90 are the colour-material versions of 64, 65, 84 and 85,
	 * in that order. */
	vex_code_offset = lit ? (colormaterial
	                           ? (multitextured ? 90 :
	                              (needs_real_w ? 89 : (textured ? 88 : 87)))
	                           : (multitextured ? 85 :
	                              (needs_real_w ? 84 : (textured ? 65 : 64)))) :
	                  ((clipspace_combined || real_w_combined) ? 14 :
	                   (clipspace_multitextured ? 15 :
	                   ((combined || smooth_alphatest) ? 5 : ((smooth && !multitextured) ? 3 : (multitextured ? 11 : 0)))));
	/* multitextured's own frag_code_offset is a 3-way dispatch on multitex_env,
	 * which selects the combine mode. */
	/* SMOOTH FOG ARMS FIRST. smooth_alphatest and combined are the first two
	 * shape arms of the chain below, so a fogged smooth draw would be
	 * swallowed by them and lose its fog. Both of those require textured, so
	 * the third arm safely catches the untextured smooth remainder. */
	/* Smooth points first: their predicate already excludes fog, alpha test
	 * and multitexture, so each maps onto exactly one base shape below --
	 * combined (7168), the smooth untextured arm (5120), the textured
	 * terminal arm (shader 32) or the untextured one (shader 2) -- and takes that
	 * shape's disc shader instead. */
	/* LIT ARM FIRST here too, and it maps onto the EXISTING smooth fragment
	 * shaders rather than needing lit ones: a fragment shader only consumes
	 * already-interpolated varyings and cannot tell how the colour on them was
	 * produced. Textured takes the combined pair's fragment half (shader 6,
	 * GL_MODULATE = texel x lit colour, which is exactly GL's lit-textured
	 * semantics); untextured takes the smooth one (shader 4).
	 *
	 * AND THE SMOOTH-FOG ONE (shader 52) WHEN UNTEXTURED AND FOGGED, which is
	 * the whole of lit+fog: fog is entirely fragment-side -- its factor comes
	 * from rf0, the fragment's own interpolated w, and its uniforms ride the
	 * FRAGMENT stream while the lit tail extends only the vertex block. So the
	 * two compose with no new shader, exactly as the paragraph above says. The
	 * untextured branch writes its four fixed-colour words whatever `lit` is,
	 * which matters because that shader's ldunifrf sequence consumes them
	 * before reaching the fog values.
	 *
	 * TEXTURED lit+fog is still refused by `lit` itself: its partner would be
	 * the combined pair's fragment half, whose uniform stream is laid out
	 * differently, and that has not been verified.
	 *
	 * NOT keyed on `smooth`, for the same reason the varying count above is not
	 * -- see d_BuildShaderRecord.
	 *
	 * AND ITS ALPHA TEST COMES FIRST WITHIN THE LIT ARM, because the shape
	 * arms below pick flat families off `smooth`, which a lit draw must never
	 * take. lit_alphatest_code_offset has already made the textured/untextured
	 * and fog choices, so there is nothing left to decide here.
	 *
	 * The untextured smooth alphatest arm sits ahead of every fog arm for the
	 * usual reason: its own offset already encodes fog, and the catch-all
	 * fogged-smooth-untextured arm further down would otherwise swallow it and
	 * drop the alpha test. The plain smooth arm below would swallow the
	 * unfogged one the same way. */
	/* SMOOTH POINTS FIRST INSIDE THE LIT ARM, and on the SMOOTH disc shaders
	 * (69/67) whatever the shade model, for the same reason this arm ignores
	 * `smooth` throughout: a lit draw's colour arrives as a varying. Those two
	 * read one uniform, the point size, and consume the TLB config -- the whole
	 * of what the smooth_point block writes. The flat discs 68/66 would read
	 * four colour words that a lit draw does not write. smooth_point's own
	 * predicate excludes fog and alpha test, so this arm competes with
	 * nothing below it. */
	/* MULTITEXTURE FIRST INSIDE THE LIT ARM. A multitextured draw is also
	 * `textured`, and lit_alphatest does not exclude it, so without this the
	 * arms below would hand it a single-texture shader. Its alpha test is
	 * dropped, as it already is for an unlit multitextured draw -- no
	 * multitexture alphatest variant exists for either. */
	/* THE ENVIRONMENT ARM, ahead of everything: 93/94 for a combined draw and
	 * 91/92 for the flat one. False for every draw in every game, so the chain
	 * below is reached through a single comparison. */
	frag_code_offset = (env_add || env_blend)
	                   ? (combined ? (env_add ? 93 : 94) : (env_add ? 91 : 92)) :
	                   lit ? (multitextured ? 86 :
	                          smooth_point ? (textured ? 69 : 67) :
	                          lit_alphatest ? lit_alphatest_code_offset :
	                          (textured ? (fog ? 53 : 6) : (fog ? 52 : 4))) :
	                   smooth_point ? (textured ? (smooth ? 69 : 68) : (smooth ? 67 : 66)) :
	                    smooth_untex_alphatest ? smooth_untex_alphatest_code_offset :
	                    (fog && smooth_alphatest) ? smooth_fog_alphatest_code_offset :
	                    ((fog && combined) ? 53 :
	                    /* LAST of the fog arms, deliberately. This is the catch-all
	                     * for fogged smooth draws, and it shadows anything placed
	                     * below it, so keeping it last is structural rather than
	                     * another exclusion list to forget. */
	                    ((fog && smooth && !multitextured && !textured) ? 52 :
	                    (smooth_alphatest ? smooth_alphatest_code_offset :
	                    (combined ? 6 :
	                    ((smooth && !multitextured) ? 4 :
	                    /* Multitexture fog arm ahead of the plain multitexture
	                     * arm, for the same reason the smooth fog arms lead the
	                     * chain: otherwise the arm below swallows the draw and
	                     * the fog is silently dropped. Only the non-blending env
	                     * modes reach it. */
	                    ((fog && multitextured) ?
	                        (multitex_env == 1 ? 62 : (multitex_env == 2 ? 63 : 61)) :
	                    (multitextured ?
	                        (multitex_env == 1 ? 16 : (multitex_env == 2 ? 17 : 12)) :
	                    /* Combined arm FIRST: it must be tested ahead of both
	                     * single-feature arms, or the `alphatest` arm below would
	                     * swallow the case and drop the fog. fog_alphatest_code_offset
	                     * is only meaningful when both are set, which is the guard
	                     * here. */
	                    ((fog && alphatest) ? fog_alphatest_code_offset :
	                    (alphatest ? alphatest_code_offset :
	                    (fog ? (textured ? 9 : 7) :
	                    /* The terminal case is "textured, flat-shaded,
	                     * nothing else active". It dispatches to
	                     * FRAGMENT_TEXTURED_COLORMOD, which multiplies the
	                     * texture sample by a per-draw uniform colour rather
	                     * than emitting the texel unmodulated -- see the
	                     * four-word colour feed written for it in the
	                     * textured uniform block above. */
	                    (textured ? 20 : 2)))))))))));


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
		              (needs_real_w     ? D_SR_NEEDS_REAL_W     : 0) |
		              (lit              ? D_SR_LIT              : 0) |
		              (colormaterial    ? D_SR_COLORMATERIAL    : 0) |
		              ((smooth_untex_alphatest || lit_alphatest) ?
		                                  D_SR_SMOOTH_SHAPE_ALPHATEST : 0);
		/* A lit TEXTURED draw is the third three-record shape, alongside combined
		 * and smooth_alphatest: position, s/t, normal. Lit untextured has two,
		 * position and normal, which is the same count as the default arm.
		 *
		 * GL_COLOR_MATERIAL adds one to whichever it is -- a third record for a
		 * lit untextured draw, a FOURTH for a lit textured one, which is the only
		 * shape in the driver that has four. This expression is repeated by the
		 * rec[] patch below and by d_glShaderState's argument, and all three must
		 * agree. */
		ULONG nrec   = (ULONG)(2
		                + ((combined || smooth_alphatest || (lit && textured)
		                    || (lit && colormaterial)) ? 1 : 0)
		                + ((lit && textured && colormaterial) ? 1 : 0));
		ULONG nwords = (sizeof(v3d_gl_shader_state_record) +
		                nrec * sizeof(v3d_gl_shader_state_attribute_record)) / 4;
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
			                    0, 0, 0, 0, NULL, NULL, NULL, NULL);
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
		/* The fourth record's address. Only lit + textured + colour material
		 * reaches four records; a lit UNTEXTURED colour-material draw carries
		 * the colour in texbuf2 and is patched by the line above. */
		if (nwords > 21)
			rec[21] = LE32((ULONG)colbuf);

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
	/* `combined`, `smooth_alphatest` and a lit TEXTURED draw write THREE
	 * attribute records (posbuf, texbuf, texbuf2 -- texbuf2 being the normal in
	 * the lit case); every other shape writes exactly two (posbuf + texbuf, the
	 * latter being the normal for a lit untextured draw). d_glShaderState below
	 * must be told the right number, or the hardware never consumes the third
	 * record as part of this draw call's attribute list and the alignment of
	 * whatever follows is corrupted. See texbuf2's own allocation comment for the
	 * layout. This count and `nwords` above must always agree. */

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

	d_glShaderState(backend, staterecordAddress,
	                (ULONG)(2
	                 + ((combined || smooth_alphatest || (lit && textured)
	                     || (lit && colormaterial)) ? 1 : 0)
	                 + ((lit && textured && colormaterial) ? 1 : 0)));

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

	if (WIREFRAME_WANTED(context)) { d_DrawWireframe(context, GL_TRIANGLES); return; }


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

	if (WIREFRAME_WANTED(context))
	{
		d_DrawWireframeLocked(context, nidx, type, indices, first);
		return;
	}

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
 * glPolygonMode GL_LINE / GL_POINT, decomposed on the CPU.
 *
 * WHY NOT THE HARDWARE. V3D's CFG_BITS carry
 * direct3d_wireframe_triangles_mode, and MESA drives it straight from
 * glPolygonMode -- but V3D has NO quad or polygon primitive, so a quad or a
 * GL_POLYGON is triangulated before it is rasterized and that bit outlines
 * every TRIANGLE edge. GL draws a polygon's BOUNDARY edges and says the ones a
 * triangulation invented must not appear. With the bit set, a quad, a strip and
 * a fan each show their diagonal. Only GL_TRIANGLES is correct that way. MESA
 * reaches conformance the same way this does, in draw_pipe_unfilled.c:
 * decompose, and emit an edge only where the triangulation marked a boundary.
 *
 * STRUCTURE. Each primitive is walked a TRIANGLE at a time, and each triangle
 * says which of its three edges are real boundary edges -- the equivalent of
 * MESA's DRAW_PIPE_EDGE_FLAG_n, and it belongs to the decomposition, not to the
 * vertex, so it costs no MGLVertex field and breaks no client ABI. Facing is
 * decided per triangle at the same point, because the edges are emitted as
 * V3D_PRIM_LINES and a line carries no winding for the hardware to cull by.
 *
 * glEdgeFlag is still NOT honoured: that one IS per vertex and would need the
 * MGLVertex field. GL's default is GL_TRUE, every edge a boundary, so a program
 * that never sets edge flags sees exactly what GL specifies.
 */
static int *s_wire = NULL;
static int  s_wire_pairs = 0;

void d_FreeWire(void)
{
	if (s_wire)
		free(s_wire);
	s_wire = NULL;
	s_wire_pairs = 0;
}

/* Room for `pairs` edges of two indices each; grown and kept, like s_seq. */
static int *d_WireRoom(int pairs)
{
	if (pairs > s_wire_pairs)
	{
		int *p = (int *)realloc(s_wire, sizeof(int) * 2 * pairs);
		if (!p)
			return NULL;
		s_wire = p;
		s_wire_pairs = pairs;
	}
	return s_wire;
}

/*
 * GL_POINT mode, from the very same decomposition GL_LINE uses. GL 1.1 3.5.4
 * draws a point at each vertex that BEGINS a boundary edge, so the edge list
 * already is the answer -- take the first index of every pair, in place. A
 * closed polygon begins exactly one boundary edge per boundary vertex, so no
 * deduplication is needed, and the facing decision and the suppressed
 * triangulation diagonals both come along from d_BuildWireIndices.
 */
static int d_WireEdgesToPoints(int count)
{
	int i, n = 0;

	for (i = 0; i < count; i += 2)
		s_wire[n++] = s_wire[i];

	return n;
}

/*
 * Is this triangle culled? The hardware normally answers this from the winding
 * of a TRIANGLE primitive, which the line batch below is not, so it is
 * answered here instead -- three vertices through CombinedMatrix (the same
 * ModelView*Projection the vertex shader is handed) and the sign of the
 * projected area. Three transforms per triangle, on wireframe draws only.
 *
 * Conservative: a vertex at or behind the eye plane makes the projected winding
 * meaningless, so the triangle is KEPT rather than culled on a bad sign.
 */
static GLboolean d_WireCulled(GLcontext context, int ia, int ib, int ic)
{
	const GLfloat *m = context->CombinedMatrix.v;
	const MGLVertex *vb = context->VertexBuffer;
	float px[3], py[3], pw[3];
	float area;
	int k, idx[3];

	if (context->CullFace_State != GL_TRUE)
		return GL_FALSE;
	if (context->CurrentCullFace == GL_FRONT_AND_BACK)
		return GL_TRUE;

	idx[0] = ia; idx[1] = ib; idx[2] = ic;
	for (k = 0; k < 3; k++)
	{
		const MGLVertex *v = &vb[idx[k]];

		px[k] = v->v.x*m[OF_11] + v->v.y*m[OF_12] + v->v.z*m[OF_13] + v->v.w*m[OF_14];
		py[k] = v->v.x*m[OF_21] + v->v.y*m[OF_22] + v->v.z*m[OF_23] + v->v.w*m[OF_24];
		pw[k] = v->v.x*m[OF_41] + v->v.y*m[OF_42] + v->v.z*m[OF_43] + v->v.w*m[OF_44];

		if (pw[k] <= 0.0f)
			return GL_FALSE;
	}

	for (k = 0; k < 3; k++)
	{
		px[k] /= pw[k];
		py[k] /= pw[k];
	}

	area = (px[1] - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (py[1] - py[0]);
	if (area == 0.0f)
		return GL_FALSE;

	if ((context->CurrentFrontFace == GL_CCW) ? (area > 0.0f) : (area < 0.0f))
		return (context->CurrentCullFace == GL_FRONT) ? GL_TRUE : GL_FALSE;

	return (context->CurrentCullFace == GL_BACK) ? GL_TRUE : GL_FALSE;
}

#define WIRE_EDGE(a,b) do { e[w*2] = (a); e[w*2+1] = (b); w++; } while (0)

/*
 * One triangle's contribution: its three edges, each emitted only if the
 * decomposition marked it a boundary AND the triangle survives culling.
 */
#define WIRE_TRI(a,b,c,e0,e1,e2)                                   \
	do {                                                           \
		if (!d_WireCulled(context, (a), (b), (c)))                 \
		{                                                          \
			if (e0) WIRE_EDGE((a), (b));                           \
			if (e1) WIRE_EDGE((b), (c));                           \
			if (e2) WIRE_EDGE((c), (a));                           \
		}                                                          \
	} while (0)

/* Returns the index COUNT (pairs * 2). No primitive yields more than one edge
 * per vertex once interior edges are dropped, plus a few for the closing ones.
 * `prim` is passed rather than read from CurrentPrimitive, which
 * GLDrawArrays/GLDrawElements never set. */
static int d_BuildWireIndices(GLcontext context, int prim, int n)
{
	int *e;
	int  i, w = 0;

	e = d_WireRoom(n + 8);
	if (!e)
		return 0;

	switch (prim)
	{
		case GL_TRIANGLES:
			/* Independent: every edge is a boundary edge. */
			for (i = 0; i + 2 < n; i += 3)
				WIRE_TRI(i, i+1, i+2, 1, 1, 1);
			break;

		case GL_QUADS:
			/* Each quad is its own 2-triangle fan; the spoke (i, i+2) is
			 * interior and belongs to neither triangle's boundary. */
			for (i = 0; i + 3 < n; i += 4)
			{
				WIRE_TRI(i,   i+1, i+2, 1, 1, 0);
				WIRE_TRI(i,   i+2, i+3, 0, 1, 1);
			}
			break;

		case GL_TRIANGLE_STRIP:
			/* Triangle k is (k, k+1, k+2). It shares (k+1, k+2) with the
			 * next, so that edge is boundary only on the LAST triangle, and
			 * (k, k+1) only on the first. The rung (k+2, k) always is. */
			for (i = 0; i + 2 < n; i++)
				WIRE_TRI(i, i+1, i+2, (i == 0), (i + 3 >= n), 1);
			break;

		case GL_TRIANGLE_FAN:
		case GL_POLYGON:
			/* Triangle k is (0, k+1, k+2). The spokes (0, k+1) are interior
			 * except the first, and (k+2, 0) except on the last triangle;
			 * the rim (k+1, k+2) always is. For GL_POLYGON that leaves
			 * exactly the polygon's own perimeter. */
			for (i = 0; i + 2 < n; i++)
				WIRE_TRI(0, i+1, i+2, (i == 0), 1, (i + 3 >= n));
			break;

		case GL_QUAD_STRIP:
		{
			/* Quad k is (2k, 2k+1, 2k+3, 2k+2) in perimeter order and shares
			 * the rung (2k+2, 2k+3) with the next, so only the two end rungs
			 * and the sides survive. */
			int m = n & ~1;                 /* whole vertex pairs only */

			for (i = 0; i + 3 < m; i += 2)
			{
				WIRE_TRI(i,   i+1, i+3, (i == 0), 1, 0);
				WIRE_TRI(i,   i+3, i+2, 0, (i + 4 >= m), 1);
			}
			break;
		}

		default:
			return 0;
	}

	return w * 2;
}

/*
 * The LOCKED/indexed path's wireframe. A client application draws its world surfaces this
 * way -- glLockArrays plus an index list per texture and lightmap -- so without
 * this, gl_wireframe changes nothing on screen except the few surfaces that
 * take another route. Found exactly that way: the world stayed solid and only
 * the water looked different.
 *
 * Always GL_TRIANGLES here, so every edge is a boundary edge and there is no
 * triangulation to see through; the work is reading the caller's index array
 * rather than assuming 0..n-1. Facing is still decided per triangle, since
 * these go out as lines too.
 */
static int d_WireIndexAt(GLenum type, const GLvoid *indices, int i)
{
	switch (type)
	{
		case GL_UNSIGNED_BYTE:  return (int)((const GLubyte  *)indices)[i];
		case GL_UNSIGNED_SHORT: return (int)((const GLushort *)indices)[i];
		case GL_UNSIGNED_INT:
		default:                return (int)((const GLuint   *)indices)[i];
	}
}

static void d_DrawWireframeLocked(GLcontext context, int nidx, GLenum type,
                                  const GLvoid *indices, int first)
{
	int *e;
	int  i, w = 0;

	if (nidx < 3)
		return;

	e = d_WireRoom(nidx + 8);
	if (!e)
		return;

	v_EnsureTransformState(context);
	d_FrameBegin(context);

	for (i = 0; i + 2 < nidx; i += 3)
	{
		int a = d_WireIndexAt(type, indices, first + i);
		int b = d_WireIndexAt(type, indices, first + i + 1);
		int c = d_WireIndexAt(type, indices, first + i + 2);

		if (d_WireCulled(context, a, b, c))
			continue;

		WIRE_EDGE(a, b);
		WIRE_EDGE(b, c);
		WIRE_EDGE(c, a);
	}

	if (w == 0)
		return;

	if (context->CurPolygonMode == GL_POINT)
	{
		int np = d_WireEdgesToPoints(w * 2);
		D(("d_DrawWireframeLocked: emitting %ld boundary points\n", (LONG)np));
		gl_EmitPrimitiveV3D(context, s_wire, np, V3D_PRIM_POINTS, GL_FALSE, np);
		return;
	}

	D(("d_DrawWireframeLocked: emitting %ld boundary edges\n", (LONG)w));
	gl_EmitPrimitiveV3D(context, s_wire, w * 2, V3D_PRIM_LINES, GL_FALSE, w * 2);
}

/*
 * Entered from the top of each polygon d_Draw*, which passes its own primitive
 * -- so immediate mode, the array paths and a display-list replay all reach it,
 * and a replayed list picks up the polygon mode in force at REPLAY time, as GL
 * requires, rather than the one captured with it.
 */
void d_DrawWireframe(GLcontext context, int prim)
{
	int count;

	D(("d_DrawWireframe: entry, prim=%ld VertexBufferPointer=%ld\n",
	   (LONG)prim, (LONG)context->VertexBufferPointer));

	if (context->VertexBufferPointer < 2)
		return;

	if (context->CullFace_State == GL_TRUE && context->CurrentCullFace == GL_FRONT_AND_BACK)
		return;

	/* Same preamble as d_DrawLines -- see d_DrawTriangles' comment for what
	 * v_EnsureTransformState covers. */
	v_EnsureTransformState(context);

	d_FrameBegin(context);

	count = d_BuildWireIndices(context, prim, d_SeqCount(context));
	if (count < 2)
		return;

	if (context->CurPolygonMode == GL_POINT)
	{
		count = d_WireEdgesToPoints(count);
		D(("d_DrawWireframe: emitting %ld boundary points\n", (LONG)count));
		gl_EmitPrimitiveV3D(context, s_wire, count, V3D_PRIM_POINTS, GL_FALSE, count);
		return;
	}

	D(("d_DrawWireframe: emitting %ld boundary edges\n", (LONG)(count/2)));
	gl_EmitPrimitiveV3D(context, s_wire, count, V3D_PRIM_LINES, GL_FALSE, count);
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

	/* GL_POLYGON and MGL_FLATFAN route here too and share the fan case. */
	if (WIREFRAME_WANTED(context)) { d_DrawWireframe(context, GL_TRIANGLE_FAN); return; }


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

	if (WIREFRAME_WANTED(context)) { d_DrawWireframe(context, GL_TRIANGLE_STRIP); return; }


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
	/* Vertex count, clamped through d_SeqCount like the other eight d_Draw*
	 * functions, which gives GL_QUADS its clamp against s_seq_size. GLVertex4f
	 * bound-checks too, so this is the second line of defence rather than the
	 * only one, and the count is read once here rather than per iteration. */
	const int d_vcount = d_SeqCount(context);
	/* Same multi-color batching as d_DrawQuadStrip: one shared batch has
	 * room for only ONE flat color, so GL_QUADS blocks that change glColor
	 * between quads need several slots. See d_DrawQuadStrip's own comment. */
	static int color_batch[MAX_COLOR_BATCHES][MGL_MAXVERTS];
	int color_batch_count[MAX_COLOR_BATCHES];
	v3d_u32 color_batch_color[MAX_COLOR_BATCHES];
	int num_active_batches = 0;

	if (WIREFRAME_WANTED(context)) { d_DrawWireframe(context, GL_QUADS); return; }


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
	for (i=0; i+3<d_vcount; i+=4)
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

			/* SMOOTH: the colour travels per VERTEX (d_PackSmooth /
			 * d_PackCombined) and fixed_color is never read, so splitting on
			 * the provoking vertex costs an attribute buffer and a shader
			 * state record per distinct colour and buys nothing. One batch
			 * takes them all. Quads stay independent primitives either way --
			 * chunk_size is 4 below. */
			if (context->ShadeModel == GL_SMOOTH)
			{
				slot = (num_active_batches > 0) ? 0 : -1;
			}
			else
			{
				for (k = 0; k < num_active_batches; k++)
				{
					if (color_batch_color[k] == quadColor)
					{
						slot = k;
						break;
					}
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
	/* Clamped vertex count, same reasoning as d_DrawQuads: the count goes
	 * through d_SeqCount rather than looping on the raw VertexBufferPointer. */
	const int d_vcount = d_SeqCount(context);
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

	if (WIREFRAME_WANTED(context)) { d_DrawWireframe(context, GL_QUAD_STRIP); return; }


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

	PrepTexCoords(context, 0, d_vcount, GL_FALSE);

	/*
	 * SMOOTH: the whole strip is ONE native primitive.
	 *
	 * A GL_QUAD_STRIP already arrives in triangle-strip order -- quad k is
	 * (2k, 2k+1, 2k+3, 2k+2), and submitting 0..n-1 straight through gives
	 * triangles (0,1,2), (1,2,3), (2,3,4)... which tile exactly the same
	 * surface. The per-quad decomposition below exists only because a flat
	 * colour is a per-CALL uniform, so quads of different colours cannot
	 * share one. Under GL_SMOOTH the colour travels per VERTEX instead
	 * (d_PackSmooth/d_PackCombined) and fixed_color is never read, so there
	 * is nothing to split on: one state record and one VertexArrayPrims for
	 * the entire strip, instead of one of each per quad.
	 *
	 * glPolygonMode GL_LINE is unaffected. The hardware draws every TRIANGLE
	 * edge, strip and fan alike, so the triangulation shows either way.
	 */
	if (context->ShadeModel == GL_SMOOTH)
	{
		int count = d_vcount & ~1;          /* whole vertex pairs only */

		if (count >= 4)
		{
			D(("d_DrawQuadStrip: smooth, one TRIANGLESTRIP of %ld vertices\n", (LONG)count));
			gl_EmitPrimitiveV3D(context, s_seq, count, V3D_PRIM_TRIANGLESTRIP, GL_FALSE, count);
		}
		return;
	}

	/* Whole quads only: an odd trailing vertex draws nothing. */
	for (i=0; i+3<d_vcount; i+=2)
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

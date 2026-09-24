/*
 * MiniGLV3D fork of MiniGL/include/mgl/vertexbuffer.h -- Phase G.
 *
 * MGLVertex_t redefined around V3DVertex (backend/include/v3d_vertex.h,
 * already designed in Phase A) instead of W3D_Vertex. Per v3d_vertex.h's
 * own header comment, V3DVertex deliberately holds ONLY the data an
 * attribute record will point at -- CPU-only working state (clip
 * outcode, integer raster coords, pre-clip backup coords, texture Q,
 * normal-buffer index) stays out of it and becomes MGLVertex_t's own
 * fields again here, same role they had around the old W3D_Vertex.
 *
 * Field mapping from the original:
 *  - W3D_Vertex v (x,y,z,w position; u,v,tex3d texcoord; W3D_Color color;
 *    W3D_ColorRGB spec; l lambda) -> V3DVertex v (x,y,z,w position;
 *    packed UBYTE r,g,b,a color; u0,v0/u1,v1 for BOTH texture units --
 *    see below). tex3d (3D texture coordinate, Warp3D-private/rarely
 *    used per v3d_vertex.h's own comment) and spec/lambda (specular
 *    color, mipmap LOD bias -- no consumer anywhere in this codebase,
 *    no shader variant built so far reads either) are dropped; nothing
 *    referenced them outside W3D_Vertex's own definition.
 *  - struct TCoord_t tcoord (texture unit 1's s/t, bolted on separately
 *    because W3D_Vertex only had ONE built-in texcoord pair for unit 0)
 *    -> DROPPED. V3DVertex already has two clean, symmetric texcoord
 *    pairs (u0/v0, u1/v1) matching V3D_MAX_TEXUNIT==MAX_TEXUNIT==2 --
 *    unit 0 was v.u/v.v, unit 1 was tcoord.s/tcoord.t in the original;
 *    both are now v.u0/v.v0 and v.u1/v.v1. Confirmed real usage of both
 *    (not vestigial) by reading minigl.h's glMultiTexCoord2fARB/
 *    glMultiTexCoord2fvARB, which write unit 0 to v.u/v.v and unit>0 to
 *    tcoord.s/tcoord.t -- those call sites are ported to the new
 *    v.u0/v0/u1/v1 fields as part of this same phase.
 *  - GLubyte color[4] "for vertexarrays" -> DROPPED. V3DVertex.r/g/b/a
 *    is already exactly this (packed UBYTE, normalized) -- having both
 *    the old float-based color (via W3D_Color) AND a separate packed
 *    UBYTE copy was the original's own accommodation for two different
 *    Warp3D vertex-array paths; V3DVertex picks the one canonical
 *    hardware-facing representation instead (v3d_vertex.h's own
 *    reasoning).
 *  - GLuint cbuf_pos -- the original's own comment already says
 *    "unused (16 byte align)" -- dropped outright, not carried forward
 *    as dead padding.
 *  - outcode, xi/yi/zi, bx/by/bz/bw, q, normal -- all genuinely CPU-only
 *    working state with no hardware-attribute role, kept unchanged.
 *  - color, ADDED (not in the original's field list at all): hclip.c's
 *    clip routines LERP-interpolate the old W3D_Color's r/g/b/a as
 *    floats at every clip plane (found by reading hclip.c directly, not
 *    assumed) -- V3DVertex.r/g/b/a is deliberately packed UBYTE per its
 *    own header comment ("CPU-side lighting/blending still computes
 *    color in float internally... packs to UBYTE when writing into a
 *    V3DVertex, not a separate secondary-buffer conversion step"), so
 *    that interpolation math needs a float CPU-working copy same as
 *    bx/by/bz/bw is a CPU-working copy of clip-space position. Packing
 *    this into v.r/g/b/a is a later phase's job (draw.c finalizing a
 *    vertex for the hardware attribute buffer), not done here.
 */

#ifndef __VERTEXBUFFER_H
#define __VERTEXBUFFER_H

#include "../../../backend/include/v3d_vertex.h"

struct MGLVertex_t {

	V3DVertex	v; /* hardware-facing: x,y,z,w position; r,g,b,a packed color; u0,v0/u1,v1 texcoords */

	MGLColor	color; /* CPU-working float color -- see header comment on why this exists alongside v.r/g/b/a */

	ULONG		outcode;

	int		xi,yi,zi;

	float		bx,by,bz,bw;
	GLfloat		q;

	GLuint		normal;   //NormalBuffer-index
};

typedef struct MGLVertex_t MGLVertex;

enum {
	MGL_CLIP_NEGW   =   1<<0,
	MGL_CLIP_TOP    =   1<<1,
	MGL_CLIP_BOTTOM =   1<<2,
	MGL_CLIP_LEFT   =   1<<3,
	MGL_CLIP_RIGHT  =   1<<4,
	MGL_CLIP_FRONT  =   1<<5,
	MGL_CLIP_BACK   =   1<<6
};


/* used buffering store non-clipped primitives */
/* used for glDrawArrays/glDrawElements */

typedef struct PolyIndex_s
{
	ULONG numverts;
	ULONG first;
} PolyIndex;

#endif

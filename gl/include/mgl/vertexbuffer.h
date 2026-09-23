/*
 * $Id: vertexbuffer.h,v 1.1.1.1 2000/04/07 19:44:51 tfrieden Exp $
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
 * MiniGLV3D fork of MiniGL/include/mgl/vertexbuffer.h.
 *
 * MGLVertex_t is built around V3DVertex (backend/include/v3d_vertex.h).
 * The clip outcode, bx/by/bz/bw, the texture Q and the normal-buffer
 * index are MGLVertex_t's own fields, outside V3DVertex. GLVertex4f and
 * the vertex-array gathers store the same position in bx/by/bz/bw as in
 * v.x/y/z/w; aclip.c and hclip.c clip bx/by/bz/bw as clip-space
 * coordinates.
 *
 * Texture unit 0's coordinates are v.u0/v.v0 and unit 1's are v.u1/v.v1,
 * matching V3D_MAX_TEXUNIT == MAX_TEXUNIT == 2.
 *
 * color is the vertex color as floats; draw.c builds the color
 * attributes from it, and V3DVertex's own packed-UBYTE r/g/b/a are not
 * used. hclip.c's clip routines interpolate color as floats at every
 * clip plane (GL_SMOOTH).
 */

#ifndef __VERTEXBUFFER_H
#define __VERTEXBUFFER_H

#include "../../../backend/include/v3d_vertex.h"

struct MGLVertex_t {

	V3DVertex	v; /* hardware-facing: x,y,z,w position; u0,v0/u1,v1 texcoords */

	MGLColor	color; /* float vertex color -- see header comment */

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

typedef struct PolyIndex_s
{
	ULONG numverts;
	ULONG first;
} PolyIndex;

#endif

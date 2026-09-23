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
 * MiniGLV3D fork of two functions from MiniGL/src/vertexbuffer.c --
 * GLDepthRange and GLViewport. GLViewport's guardband clip-flag block is
 * Surgeon's (marked "surgeon begin/end" in the MiniGL source).
 *
 * Every context constructor (MGLCreateContext, MGLCreateContextFromWindow,
 * MGLCreateContextFromBitMap) calls both: every context, not just one that
 * draws triangles, needs a valid viewport/depth-range transform. Both are
 * pure GL-state bookkeeping.
 *
 * Upstream MiniGL's GLViewport always takes `ay` from the window
 * (`w3dWindow`); this fork must not assume there is one. The fullscreen
 * path (`vid_OpenDisplay`, context.c) opens a custom `Screen`
 * (`context->v3dScreen`) and never sets `v3dWindow`, which stays NULL
 * because the context is allocated with MEMF_CLEAR; a context from
 * MGLCreateContextFromBitMap has neither. `ay` is therefore taken from the
 * window, else the screen (borderless, so no Border{Top,Bottom} terms),
 * else the render target.
 *
 * `ax`/`ay` (centre) and `sx`/`sy` (half-extents) feed ClipWindow,
 * ClipperXYScaling, ViewportOffset and the vertex shaders' scale_p/scale_p_y
 * (draw.c). A garbage `ay` that puts the viewport wholly above or below the
 * render target collapses the ClipWindow rectangle to zero height and
 * silently discards every primitive.
 */

#include "sysinc.h"

void GLDepthRange(GLcontext context, GLclampd n, GLclampd f)
{
	context->near = n;
	context->far  = f;
	context->sz = (GLfloat)((f-n)*0.5);
	context->az = (GLfloat)((n+f)*0.5);
}

void GLViewport(GLcontext context, GLint x, GLint y, GLsizei w, GLsizei h)
{
	GLuint clipflags;

	/* Keep the arguments as GL gave them, for glGetIntegerv(GL_VIEWPORT) --
	 * everything computed below is a centre and a half-extent in a
	 * top-origin frame, which cannot be handed back to an application. */
	context->ViewportBox[0] = x;
	context->ViewportBox[1] = y;
	context->ViewportBox[2] = (GLint)w;
	context->ViewportBox[3] = (GLint)h;

	if(context->GuardBand == GL_TRUE)
	{
		clipflags = (MGL_CLIP_NEGW|MGL_CLIP_BACK|MGL_CLIP_FRONT);

		if(x > 0)
			clipflags |= MGL_CLIP_LEFT;

		/* Guardband clipping is off by default; mglChooseGuardBand(GL_TRUE)
		 * turns it on for contexts created afterwards. v3dScreen is NULL for
		 * an adopted-bitmap context, the same case as ay below: fall back to
		 * the render target's backend.width/height. Where a screen exists,
		 * its size is used. */
		if((x+w) < (GLint)(context->v3dScreen ? context->v3dScreen->Width : context->backend.width))
			clipflags |= MGL_CLIP_RIGHT;

		if(y > 0)
			clipflags |= MGL_CLIP_BOTTOM;

		if((y+h) < (GLint)(context->v3dScreen ? context->v3dScreen->Height : context->backend.height))
			clipflags |= MGL_CLIP_TOP;

		context->ClipFlags = clipflags;
	}
	else /* guardband clipping disabled */
	{
		context->ClipFlags = (MGL_CLIP_NEGW | MGL_CLIP_BACK | MGL_CLIP_FRONT | MGL_CLIP_LEFT | MGL_CLIP_RIGHT | MGL_CLIP_TOP | MGL_CLIP_BOTTOM);
	}

	context->ax = (float)x + (float)w*0.5f;
	if (context->v3dWindow)
	{
		context->ay =  (float)(context->v3dWindow->Height-context->v3dWindow->BorderTop-context->v3dWindow->BorderBottom)
		              -(float)y - (float)h*0.5f;
	}
	else if (context->v3dScreen)
	{
		/* Fullscreen path (vid_OpenDisplay) -- no window, no borders. */
		context->ay = (float)context->v3dScreen->Height - (float)y - (float)h*0.5f;
	}
	else
	{
		/* Neither: a context from MGLCreateContextFromBitMap renders to a
		 * bitmap with no window and no screen. backend.height is the
		 * render-target height v3d_context_init(w,h) latches when the
		 * context is opened, and GLScissor uses exactly it for the same
		 * top-origin flip. Keying off the render target rather than a
		 * display object keeps ay valid for a context that has none. */
		context->ay = (float)context->backend.height - (float)y - (float)h*0.5f;
	}
	context->sx = (float)w * 0.5f;
	context->sy = (float)h * 0.5f;
}

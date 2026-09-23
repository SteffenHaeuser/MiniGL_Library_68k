/*
 * $Id: fog.c,v 1.1.1.1 2000/04/07 19:44:51 hfrieden Exp $
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
 * MiniGLV3D fork of MiniGL/src/fog.c.
 *
 * GLFogf/GLFogfv keep the original's shape, writing into
 * context->backend.fog_mode/fog_density/fog_r/g/b (V3DContext) instead
 * of context->w3dFogMode/w3dFog.*. FogStart/FogEnd stay where the
 * original has them, as GLdouble fields on GLcontext_t itself (they were
 * never Warp3D-typed). GL_LINEAR/GL_EXP/GL_EXP2 map onto the
 * backend-local V3D_FOG_LINEAR/EXP/EXP2 IDs (backend/include/v3d_context.h)
 * instead of W3D_FOG_*. Color is stored as UBYTE
 * (context->backend.fog_r/g/b) instead of float (W3D_ColorRGB), matching
 * V3DTexture's border/env color fields' own convention (v3d_u8, not
 * float); fog_ClampToByte clamps each component to [0,1] and converts it
 * to a byte in one step.
 *
 * The original committed fog state to Warp3D's hardware fog unit via
 * W3D_SetFogParams, using a formula (-1.0 / (FogStart * P[OF_43] +
 * P[OF_44])) that remaps eye-space fog distance into whatever domain
 * Warp3D's OWN per-pixel Z interpolant used. V3D has no such hardware
 * register, so that formula does not apply here -- fog_Set is a plain
 * state copy (GL-level FogStart/FogEnd/Fog_State into context->backend's
 * fields), no eye-space remapping.
 */

#include "sysinc.h"

static char rcsid[] = "$Id: fog.c,v 1.1.1.1 2000/04/07 19:44:51 hfrieden Exp $";

void GLFogf(GLcontext context, GLenum pname, GLfloat param)
{
	context->FogDirty = GL_TRUE;

	switch(pname)
	{
		case GL_FOG_MODE:
			switch((GLint)param)
			{
				case GL_LINEAR:
					context->backend.fog_mode = V3D_FOG_LINEAR;
					break;
				case GL_EXP:
					context->backend.fog_mode = V3D_FOG_EXP;
					break;
				case GL_EXP2:
					context->backend.fog_mode = V3D_FOG_EXP2;
					break;
				default:
					GLFlagError(context, 1, GL_INVALID_ENUM);
			}
			break;

		case GL_FOG_DENSITY:
			/* A negative density is refused, not stored. GLFlagError only
			 * records the error and never returns, so the store is skipped
			 * explicitly. */
			if (param < 0) { GLFlagError(context, 1, GL_INVALID_VALUE); break; }
			context->backend.fog_density = (float)param;
			break;

		case GL_FOG_START:
			context->FogStart = param;
			break;

		case GL_FOG_END:
			context->FogEnd   = param;
			break;

		case GL_FOG_INDEX:
			GLFlagError(context, 1, GL_INVALID_ENUM);
			break;

		default:
			GLFlagError(context, 1, GL_INVALID_ENUM);
			break;
	}
}


static UBYTE fog_ClampToByte(float f)
{
	if (f > 1.0f)
		f = 1.0f;

	if (f < 0.0f)
		f = 0.0f;

	return (UBYTE)(f * 255.0f);
}

void GLFogfv(GLcontext context, GLenum pname, GLfloat *param)
{
	context->FogDirty = GL_TRUE;

	switch(pname)
	{
		case GL_FOG_MODE:
			switch((GLint)*param)
			{
				case GL_LINEAR:
					context->backend.fog_mode = V3D_FOG_LINEAR;
					break;
				case GL_EXP:
					context->backend.fog_mode = V3D_FOG_EXP;
					break;
				case GL_EXP2:
					context->backend.fog_mode = V3D_FOG_EXP2;
					break;
				default:
					GLFlagError(context, 1, GL_INVALID_ENUM);
			}
			break;

		case GL_FOG_DENSITY:
			/* Refused, not stored -- see GLFogf. */
			if (*param < 0) { GLFlagError(context, 1, GL_INVALID_VALUE); break; }
			context->backend.fog_density = (float)(*param);
			break;
		case GL_FOG_START:
			context->FogStart = *param;
			break;
		case GL_FOG_END:
			context->FogEnd   = *param;
			break;
		case GL_FOG_INDEX:
			GLFlagError(context, 1, GL_INVALID_ENUM);
			break;
		case GL_FOG_COLOR:
			context->backend.fog_r = fog_ClampToByte(*param);
			context->backend.fog_g = fog_ClampToByte(*(param+1));
			context->backend.fog_b = fog_ClampToByte(*(param+2));
			break;
		default:
			GLFlagError(context, 1, GL_INVALID_ENUM);
			break;
	}
}

/*
 * See this file's header comment -- deliberately just a state copy, no
 * eye-space remapping. draw.c's gl_EmitPrimitiveV3DEx calls this every
 * draw call and reads these fields into the fog fragment uniforms --
 * this function's own job stays simple (sync GL fog state into
 * context->backend), the uniform-building and shader-side consumption
 * both live in draw.c/v3d_assembler.c.
 */
void fog_Set(GLcontext context)
{
	context->backend.fog_enable = context->Fog_State;
	context->backend.fog_start = (float)context->FogStart;
	context->backend.fog_end = (float)context->FogEnd;
}

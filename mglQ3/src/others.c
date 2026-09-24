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

#include "sysinc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GetVar searches the calling process's local variables, then ENV:.
 * Do not cache this globally: minigl.library is shared by applications. */
static GLint ConfiguredMaxTextureSize(void)
{
	char value[32];
	LONG length = GetVar("MINIGL_MAX_TEXTURE_SIZE", value, sizeof(value), 0);
	LONG i = 0;
	GLint limit = 0;
	if (length <= 0 || length >= (LONG)sizeof(value) - 1)
		return 1024;
	while (i < length && (value[i] == ' ' || value[i] == '\t'))
		i++;
	while (i < length && value[i] >= '0' && value[i] <= '9')
	{
		limit = limit * 10 + value[i++] - '0';
		if (limit > 4096)
			return 1024;
	}
	while (i < length && (value[i] == ' ' || value[i] == '\t' ||
		value[i] == '\r' || value[i] == '\n'))
		i++;
	if (i != length || limit < 64 || (limit & (limit - 1)) != 0)
		return 1024;
	return limit;
}

//static char rcsid[] = "$Id: others.c,v 1.1.1.1 2000/04/07 19:44:51 hfrieden Exp $";

#ifndef __PPC__
extern struct IntuitionBase *IntuitionBase;
extern struct ExecBase *SysBase;
#endif

void GLPolygonOffset(GLcontext context, GLfloat factor, GLfloat units)
{
	context->PolygonOffsetFactor = factor;
	context->PolygonOffsetUnits = units;
}


void GLAlphaFunc(GLcontext context, GLenum func, GLclampf ref)
{
	ULONG		w3dmode;
	W3D_Float	refvalue = (W3D_Float)ref;

	GLFlagError(context, context->CurrentPrimitive != GL_BASE, GL_INVALID_OPERATION);

	switch(func)
	{
		case GL_NEVER:
			w3dmode = W3D_A_NEVER;
			break;

		case GL_LESS:
			w3dmode = W3D_A_LESS;
			break;

		case GL_EQUAL:
			w3dmode = W3D_A_EQUAL;
			break;

		case GL_LEQUAL:
			w3dmode = W3D_A_LEQUAL;
			break;

		case GL_GREATER:
			w3dmode = W3D_A_GREATER;
			break;

		case GL_NOTEQUAL:
			w3dmode = W3D_A_NOTEQUAL;
			break;

		case GL_GEQUAL:
			w3dmode = W3D_A_GEQUAL;
			break;

		case GL_ALWAYS:
			w3dmode = W3D_A_ALWAYS;
			break;

		default:
			GLFlagError(context, 1, GL_INVALID_ENUM);
			break;
	}

	W3D_SetAlphaMode(context->w3dContext, w3dmode, &refvalue);
}

void GLDrawBuffer(GLcontext context, GLenum mode)
{
}

void GLPolygonMode(GLcontext context, GLenum face, GLenum mode)
{
	context->CurPolygonMode = mode ;
	return ;
}

void GLShadeModel(GLcontext context, GLenum mode)
{
	//LOG(2, glShadeModel, "%d", mode);

	context->CurShadeModel = mode ;

	//avoid excessive state changes:
	if(context->CurShadeModel != context->ShadeModel)
	{
		context->ShadeModel = mode;

		if (mode == GL_FLAT)
		{
			W3D_SetState(context->w3dContext, W3D_GOURAUD, W3D_DISABLE);
		}

		else if (mode == GL_SMOOTH)
		{
			W3D_SetState(context->w3dContext, W3D_GOURAUD, W3D_ENABLE);
		}
	}
}

#define BLS(X) case GL_##X: src=W3D_##X; break
#define BLD(X) case GL_##X: dest=W3D_##X; break

void GLBlendFunc(GLcontext context, GLenum sfactor, GLenum dfactor)
{
	ULONG src = 0, dest = 0;
	GLenum w3d_dfactor = dfactor;

	/* Always submit both factors. Warp3D state can change independently of
	 * MiniGL's cached query values (for example between Carmageddon passes). */
	context->CurBlendSrc = sfactor;
	context->CurBlendDst = dfactor;

	switch(sfactor)
	{
		BLS(ZERO);
		BLS(ONE);
		/* (DST_COLOR, ZERO) is algebraically equivalent to
		 * (ZERO, SRC_COLOR), but avoids the broken source-factor path on
		 * affected classic Warp3D drivers. Do not rewrite other pairs. */
		case GL_DST_COLOR:
			if (dfactor == GL_ZERO)
			{
				src = W3D_ZERO;
				w3d_dfactor = GL_SRC_COLOR;
			}
			else
			{
				src = W3D_DST_COLOR;
			}
			break;
		BLS(ONE_MINUS_DST_COLOR);
		BLS(SRC_ALPHA);
		BLS(ONE_MINUS_SRC_ALPHA);
		BLS(DST_ALPHA);
		BLS(ONE_MINUS_DST_ALPHA);
		BLS(SRC_ALPHA_SATURATE);

		default:
			GLFlagError(context, 1, GL_INVALID_ENUM);
	}

	switch(w3d_dfactor)
	{
		BLD(ZERO);
		BLD(ONE);
		BLD(SRC_COLOR);
		BLD(ONE_MINUS_SRC_COLOR);
		BLD(SRC_ALPHA);
		BLD(ONE_MINUS_SRC_ALPHA);
		BLD(DST_ALPHA);
		BLD(ONE_MINUS_DST_ALPHA);

		//default: // glhexen2 fix - needed ?? - Cowcat
			//return;
	}

	// Try to set the mode, if unavailable, switch to
	// (SRC_ALPHA, ONE_MINUS_SRC_ALPHA) which is supported
	// by almost all Amiga supported graphics cards

	if ((ULONG)W3D_SetBlendMode(context->w3dContext, src, dest) == (ULONG)W3D_UNSUPPORTEDBLEND)
	{
		if (context->NoFallbackAlpha == GL_FALSE)
		{
			W3D_SetBlendMode(context->w3dContext, W3D_SRC_ALPHA, W3D_ONE_MINUS_SRC_ALPHA);
			context->AlphaFellBack = GL_TRUE;
		}

		else
		{
			context->AlphaFellBack = GL_FALSE;
		}
	}

	else
	{
		context->AlphaFellBack = GL_FALSE;
	}

	context->SrcAlpha = sfactor;
	context->DstAlpha = dfactor;
}

void GLColorMask(GLcontext context, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) // Cowcat
{
	W3D_SetColorMask(context->w3dContext, red, green, blue, alpha);
}

void GLHint(GLcontext context, GLenum target, GLenum mode)
{
	ULONG hint;

	switch(mode)
	{
		case GL_FASTEST:    hint = W3D_H_FAST; break;
		case GL_NICEST:	    hint = W3D_H_NICE; break;
		case GL_DONT_CARE:  hint = W3D_H_AVERAGE; break;

		default:
			GLFlagError(context, 1, GL_INVALID_ENUM);
			break;
	}

	switch(target)
	{
		case GL_FOG_HINT:
			W3D_Hint(context->w3dContext, W3D_H_FOGGING, hint);
			break;

		case GL_PERSPECTIVE_CORRECTION_HINT:
			W3D_Hint(context->w3dContext, W3D_H_PERSPECTIVE, hint);
			break;

		case MGL_W_ONE_HINT:

			if (mode == GL_FASTEST)
				context->WOne_Hint = GL_TRUE;

			else
				context->WOne_Hint = GL_FALSE;

			break;

		#if 0  // not used - Cowcat
		case MGL_FIXPOINTTRANS_HINT:

			if (mode == GL_FASTEST)
				context->FixpointTrans_Hint = GL_TRUE;

			else
				context->FixpointTrans_Hint = GL_FALSE;

			break;
		#endif

		default:
			GLFlagError(context, 1, GL_INVALID_ENUM);
	}
}

void GLGetBooleanv(GLcontext context, GLenum pname, GLboolean *params)
{
	switch( pname )
	{
		case GL_DEPTH_TEST:
			*params = context->CurDepthTest;
			return ;

		case GL_DEPTH_WRITEMASK:
			*params = context->CurWriteMask;
			return ;

		default:
			*params = GL_FALSE ;
			GLFlagError(context, 1, GL_INVALID_ENUM);
			return ;
	}
}

void GLGetIntegerv(GLcontext context, GLenum pname, GLint *params)
{
	switch( pname )
	{
		case GL_POLYGON_MODE:
			*params = context->CurPolygonMode;
			return;

		case GL_SHADE_MODEL:
			*params = context->CurShadeModel;
			return;

		case GL_BLEND_SRC:
			*params = context->CurBlendSrc;
			return;

		case GL_BLEND_DST:
			*params = context->CurBlendDst;
			return;

		case GL_MAX_TEXTURE_SIZE:
			*params = ConfiguredMaxTextureSize();
			//*params = W3D_Query(context->w3dContext, W3D_Q_MAXTEXWIDTH, 0); // future - Cowcat
			return;

		case GL_UNPACK_ROW_LENGTH:
			*params = context->CurUnpackRowLength;
			return;

		case GL_UNPACK_SKIP_PIXELS:
			*params = context->CurUnpackSkipPixels;
			return;

		case GL_UNPACK_SKIP_ROWS:
			*params = context->CurUnpackSkipRows;
			return;

		case GL_MAX_TEXTURE_UNITS_ARB:
			*params = MAX_TEXUNIT;
			return;

		default:
			*params = 0 ;
			GLFlagError(context, 1, GL_INVALID_ENUM);
			return;
	}
}

const GLubyte *GLGetString(GLcontext context, GLenum name)
{
	char	*namedriver;
	W3D_Driver **driver;

	switch(name)
	{
		case GL_RENDERER:

			if (context->w3dContext)
			{
				switch(context->w3dContext->CurrentChip)
				{
					case W3D_CHIP_VIRGE:
						return (GLubyte *)"MiniGL/Warp3D S3 ViRGE (virge)";

					case W3D_CHIP_PERMEDIA2:
						return (GLubyte *)"MiniGL/Warp3D 3DLabs Permedia 2 (permedia)";

					case W3D_CHIP_VOODOO1:
						return (GLubyte *)"MiniGL/Warp3D 3DFX Voodoo 1 (voodoo)";

					case W3D_CHIP_AVENGER_BE:
					case W3D_CHIP_AVENGER_LE:
						return (GLubyte *)"MiniGL/Warp3D 3DFX Voodoo 3 (avenger)";

					case W3D_CHIP_UNKNOWN: // Update - Cowcat
						//return (GLubyte *)"MiniGL/Warp3D Unknown graphics chip";
						driver = W3D_GetDrivers();
						namedriver = driver[0]->name;
						return (GLubyte *)namedriver;

					default:
						return (GLubyte *)"MiniGL/Warp3D";
				}
			}

			else
			{
				return (GLubyte *)"MiniGL/Warp3D";
			}

		case GL_VENDOR:
			return (GLubyte *)"Hyperion";

		case GL_VERSION:
			return (GLubyte *)"1.2"; // Cowcat

		case GL_EXTENSIONS:

			switch(context->w3dContext->CurrentChip)
			{
				case W3D_CHIP_AVENGER_BE:
				case W3D_CHIP_AVENGER_LE:
				case W3D_CHIP_PERMEDIA2:
				case W3D_CHIP_UNKNOWN:
					return (GLubyte *)"GL_MGL_ARB_multitexture GL_EXT_compiled_vertex_array GL_MGL_packed_pixels GL_EXT_color_table";
			}

			return (GLubyte *)"GL_MGL_packed_pixels GL_EXT_color_table";

		default:
			return (GLubyte *)"Huh?";
	}
}

void GLGetFloatv(GLcontext context, GLenum pname, GLfloat *params)
{
	int i;

	switch(pname)
	{
		case GL_MODELVIEW_MATRIX:

			for (i=0; i<16; i++)
			{
				*params = context->ModelView[context->ModelViewNr].v[i];
				params++;
			}

			return;

		case GL_PROJECTION_MATRIX:

			for (i=0; i<16; i++)
			{
				*params = context->Projection[context->ProjectionNr].v[i];
				params++;
			}

			return;

		default:
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

GLboolean GLIsEnabled(GLcontext context, GLenum cap)
{
	switch(cap)
	{
		case GL_BLEND:
			return( context->Blend_State );

		case GL_DEPTH_TEST:
			return( context->DepthTest_State );

		case GL_TEXTURE_2D:
			return ( context->Texture2D_State[context->ActiveTexture] );

		default:
		#ifndef __VBCC__
			GLFlagError(context, 1, GL_INVALID_ENUM);
		#endif
			return GL_FALSE; // non void function fix - Cowcat
	}
}

void MGLSetZOffset(GLcontext context, GLfloat offset)
{
	context->ZOffset = offset;
}

// NYI:
void GLReadPixels(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
{
	W3D_Context *w = context->w3dContext;
	struct RastPort rp;
	GLubyte *row;
	GLint i, j, align = context->PackAlign;
	ULONG channels, stride;
	GLboolean locked;
	if (width < 0 || height < 0 || width > 8191) { context->CurrentError = GL_INVALID_VALUE; return; }
	if (type != GL_UNSIGNED_BYTE || (format != GL_RGB && format != GL_RGBA)) {
		context->CurrentError = GL_INVALID_ENUM; return;
	}
	if (!pixels || !width || !height) return;
	if (!w || !w->drawregion || context->CurrentPrimitive != GL_BASE) {
		context->CurrentError = GL_INVALID_OPERATION; return;
	}
	if (align != 1 && align != 2 && align != 4 && align != 8) align = 4;
	channels = format == GL_RGBA ? 4 : 3;
	stride = (width * channels + align - 1) & ~(align - 1);
	if ((ULONG)height > 0x7fffffffUL / stride) { context->CurrentError = GL_INVALID_VALUE; return; }
	row = malloc(width * 4);
	if (!row) { context->CurrentError = GL_OUT_OF_MEMORY; return; }
	InitRastPort(&rp);
	rp.BitMap = w->drawregion;
	locked = context->w3dLocked;
	/* Complete rendering before reading the actual back bitmap, not the
	 * visible window. The driver owns any necessary host-to-bitmap transfer. */
	W3D_WaitIdle(w);
	if (locked) W3D_UnLockHardware(w);
	for (i = 0; i < height; ++i) {
		GLubyte *dest = (GLubyte *)pixels + i * stride;
		GLint sy = w->height - 1 - (y + i);
		memset(dest, 0, width * channels);
		if (x < 0 || x > w->width || width > w->width - x || sy < 0 || sy >= w->height) continue;
		ReadPixelArray(row, 0, 0, width * 4, &rp, x, sy + w->yoffset, width, 1, RECTFMT_ARGB);
		for (j = 0; j < width; ++j) {
			dest[j * channels] = row[j * 4 + 1];
			dest[j * channels + 1] = row[j * 4 + 2];
			dest[j * channels + 2] = row[j * 4 + 3];
			if (channels == 4) dest[j * 4 + 3] = w->destalpha ? row[j * 4] : 255;
		}
	}
	if (locked) context->w3dLocked = W3D_LockHardware(w) == W3D_SUCCESS;
	free(row);
}

#if 1 // Cowcat

void MGLWriteShotPPM(GLcontext context, char *filename)
{
	GLubyte *pixelline;
	FILE	*f;
	int	i;

	pixelline = (GLubyte *)malloc(context->w3dWindow->Width*3);

	if (!pixelline)
		return;

	f = fopen(filename, "wb");

	if (!f)
	{
		free(pixelline);
		return;
	}

	// Write PPM header
	fprintf(f, "P6\n%d %d\n255\n", context->w3dWindow->Width,
	context->w3dWindow->Height);

	for (i=0; i<context->w3dWindow->Height; i++)
	{
		(void)ReadPixelArray(pixelline, 0, 0, context->w3dWindow->Width, context->w3dWindow->RPort,
			0, (UWORD)i, context->w3dWindow->Width, 1, RECTFMT_RGB);

		(void)fwrite(pixelline, context->w3dWindow->Width*3, 1, f);
	}

	fclose(f);
	free(pixelline);
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
	// FIXME: This should become 68k
	struct IntuiMessage *imsg;
	struct Window *window;
	ULONG Class;
	UWORD Code;
	WORD MouseX, MouseY;
	GLbitfield buttons = 0;

	window = (struct Window *)mglGetWindowHandle();
	ModifyIDCMP(window, IDCMP_VANILLAKEY|IDCMP_RAWKEY|IDCMP_MOUSEMOVE|IDCMP_MOUSEBUTTONS);

	context->Running = GL_TRUE;

	while (context->Running == GL_TRUE)
	{
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
					case SELECTDOWN: buttons |= MGL_BUTTON_LEFT;	break;
					case SELECTUP:	 buttons &= ~MGL_BUTTON_LEFT;	break;
					case MENUDOWN:	 buttons |= MGL_BUTTON_RIGHT;	break;
					case MENUUP:	 buttons &= ~MGL_BUTTON_RIGHT;	break;
					case MIDDLEDOWN: buttons |= MGL_BUTTON_MID;	break;
					case MIDDLEUP:	 buttons &= MGL_BUTTON_MID;	break;
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

#endif // Cowcat

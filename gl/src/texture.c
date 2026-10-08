/*
 * $Id: texture.c,v 1.1.1.1 2000/04/07 19:44:51 tfrieden Exp $
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
 * MiniGLV3D fork of MiniGL/src/texture.c.
 *
 * FORMAT CONVERSION. The original converted GL pixel data into 16-bit
 * Warp3D texture formats (`context->w3dFormat`/`w3dAlphaFormat`, R5G6B5/
 * A4R4G4B4); that is what `MGLConvert` and its conversion routines
 * (`SHORT565_565`, `RGBA_ARGB`, `INDEX_RGB`, ...) and the `ARGBFORM`/
 * `RGBFORM` packing macros were for. This driver stores every texture as
 * RGBA8 (4 bytes/pixel), so none of that is ported. Instead
 * `tex_GLFormatToSrcFmt` below maps a GL (format, type) pair onto a
 * backend-local `V3D_SRCFMT_*` ID, and a row-stride loop calls
 * `v3d_texture_convert_row` (`backend/hw/v3d_texture.c`), which converts
 * that source format into RGBA8:
 *   - `GL_RGBA`+`GL_UNSIGNED_BYTE` -> `V3D_SRCFMT_RGBA8` (R,G,B,A byte
 *     order, the order the texture stores).
 *   - `GL_RGB`+`GL_UNSIGNED_BYTE` -> `V3D_SRCFMT_RGB8` (R,G,B, alpha forced
 *     0xff by the backend function).
 *   - `GL_LUMINANCE_ALPHA` -> `V3D_SRCFMT_LA8` (L,A byte order).
 *   - `MGL_UNSIGNED_SHORT_5_6_5` -> `V3D_SRCFMT_RGB565` (R5G6B5 from MSB,
 *     the same bit layout as the original's `RGBFORM` macro).
 *   - `MGL_UNSIGNED_SHORT_4_4_4_4` -> `V3D_SRCFMT_ARGB4` (A4R4G4B4 from
 *     MSB, the same layout as the original's `ARGBFORM` macro).
 *   - `GL_COLOR_INDEX` -- not a `V3D_SRCFMT_*`: the V3D TMU has no palette
 *     hardware, so an indexed image is expanded to RGBA8 on the CPU during
 *     upload, through the palette `GLColorTable` keeps in the GL context.
 *     See `TEX_SRCFMT_COLOR_INDEX8` and `tex_BuildPaletteLUT` below.
 *   - `GL_LUMINANCE` -> `V3D_SRCFMT_L8`, `GL_ALPHA` -> `V3D_SRCFMT_A8`
 *     (one byte per texel, expanded by the backend to (L,L,L,1) and
 *     (1,1,1,A) -- RGB passes through an ALPHA texture's MODULATE, as in
 *     GL's base-format table and the original MiniGL's own A8_ARGB
 *     conversion). The original's support for these was indirect, through
 *     A4R4G4B4, as its "Surgeon" comment says.
 * `V3D_SRCFMT_ARGB8`/`_ARGB1555` are not reachable from any GL entry point
 * here, like `V3D_TEXWRAP_BORDER` (the backend value exists, no GL
 * combination reaches it).
 *
 * OBJECT MODEL. The original's `w3dTexBuffer[]`/`w3dTexMemory[]` (parallel
 * GPU-handle and CPU-mirror arrays) become `context->textureObjects[]`, a
 * `V3DTexture**` with one pointer per GL texture name, `malloc`'d on the
 * first `glTexImage2D` for that name (the original's own "create on first
 * use" pattern). No CPU-side mirror is kept the way `w3dTexMemory` was:
 * `V3DTexture.texture_mem` holds the uploaded data in GPU memory, and every
 * upload works from the caller's own `pixels` argument, so a mirror would
 * add nothing. The original's sub-image update rewrote a sub-rectangle of
 * that mirror in place; GLTexSubImage2DNoMIP instead writes the box
 * straight into the texture's tiled GPU memory (`v3d_store_tiled_image`'s
 * tiling, not a flat buffer). `tex_FreeTextures` (called from `context.c`'s
 * `vid_CloseDisplay`/`vid_CloseWindow`) frees each `V3DTexture` with
 * `v3d_texture_free` + `free()`, in place of
 * `W3D_FreeTexObj`+`W3D_FreeAllTexObj`.
 *
 * `tex_SetEnv`/`tex_SetFilter`/`tex_SetWrap` -- the original made immediate
 * `W3D_Set*` hardware calls. Here they write the bound `V3DTexture`'s
 * `texenv_mode` (`V3D_TEXENV_*`, `v3d_texture.h`), `min_filter`/
 * `mag_filter`/`mip_filter_nearest` and `wrap_s`/`wrap_t` fields;
 * `v3d_texture_emit_state` reads filter and wrap off the object when it
 * builds the texture's state records.
 *
 * `tex_ConvertTexture` (the original's "reformat this texture to gain an
 * alpha channel for additive-blend emulation" helper) is not ported: RGBA8
 * always has an alpha channel, so there is nothing to convert to.
 *
 * `tex_Alloc`/`tex_Free`/`tex_Statistic`/`MGLTexMemStat` are the original's
 * small CPU allocator with byte accounting, unchanged; this file uses it for
 * the RGBA8 conversion scratch buffer.
 */

#include "sysinc.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "../../backend/include/v3d_device.h"
#include "../../backend/include/v3d_context.h"
#include "../../backend/include/v3d_texture.h"
#include "../../backend/hw/v3d_debug.h"


extern struct ExecBase *SysBase;

void tex_FreeTextures(GLcontext context);
void tex_SetEnv(GLcontext context, GLenum env);
void tex_SetMinFilter(GLcontext context, GLenum min);
void tex_SetMagFilter(GLcontext context, GLenum mag);
void tex_SetWrapS(GLcontext context, GLenum wrap_s);
void tex_SetWrapT(GLcontext context, GLenum wrap_t);
extern void GLBlendFunc(GLcontext context, GLenum sfactor, GLenum dfactor);
extern void MGLFlushPendingRender(GLcontext context);     /* context.c */
extern int  g_mglv3d_texture_park_overflows;              /* backend/hw/v3d_texture.c */

static ULONG Allocated_Size = 0;
static ULONG Peak_Size      = 0;

/*
 * The NULL check is load-bearing, not defensive tidiness. Without it this
 * function dereferences malloc's result immediately (`*x = size`) and then
 * returns `x+1`, so on a failed allocation it writes the size through a NULL
 * pointer and hands back 0x00000004 -- a non-NULL value, which makes every
 * caller's `if (!p)` test dead code. The caller then writes the converted texture
 * over low memory starting at address 4, which is where SysBase lives.
 *
 * Allocated_Size is also only charged on success: incrementing it before the
 * allocation is attempted permanently inflates the running total that tex_Free
 * later decrements.
 */
void *tex_Alloc(ULONG size)
{
	ULONG *x;

	x=(ULONG *)malloc(size+4);
	if (!x)
		return NULL;

	*x = size;
	Allocated_Size += size+4;

	if (Allocated_Size > Peak_Size) Peak_Size = Allocated_Size;

	return x+1;
}

void tex_Free(void *chunk)
{
	ULONG *mem = (ULONG *)chunk;
	mem--;
	Allocated_Size -= *mem;
	Allocated_Size -= 4;
	free(mem);
}

void tex_Statistic(void)
{
	D(("Peak Allocation Size: %lu\n", Peak_Size));
}

/* Context teardown. Both counters are file statics, so in the resident library
 * they outlive the context that charged them and the next program would start
 * with this one's peak. */
void tex_ResetStats(void)
{
	Allocated_Size = 0;
	Peak_Size      = 0;
}

void MGLTexMemStat(GLcontext context, GLint *Current, GLint *Peak)
{
	if (Current) *Current = (GLint)Allocated_Size;
	if (Peak)    *Peak    = (GLint)Peak_Size;
}

/* glIsTexture (GLIsTexture, after the range test below).
 *
 * BOUNDS CHECKING IS NOT OPTIONAL HERE. The tables hold textureObjectCount
 * entries and the name comes from the application. Returning GL_FALSE for an
 * out-of-range name is both the spec answer -- it is not the name of a
 * texture object -- and the safe one. Name 0 is the default texture and is
 * never a texture object.
 *
 * Keyed on GeneratedTextures[], not textureObjects[]: the latter stays NULL
 * until the first GLTexImage2D, so it would wrongly answer GL_FALSE for a
 * generated-and-bound-but-not-yet-uploaded name. Strict GL says a name is not
 * a texture object until first BOUND, and this driver keeps no per-name bound
 * flag, so it answers "generated and not deleted" instead. */
/*
 * The one range test for an application-supplied texture name, shared by
 * GLIsTexture, GLDeleteTextures and GLBindTexture. Both arrays hold exactly
 * textureObjectCount entries and are allocated together (context.c), so one
 * test covers both.
 *
 * Note the UNSIGNED comparison. A signed test,
 * `(int)texture >= context->textureObjectCount`, is false for any name at or
 * above 2^31: those cast to a negative int, pass the guard and then index the
 * array with a huge unsigned subscript. Comparing as GLuint has no such hole.
 *
 * Name 0 is the default texture, never a texture object, so it is refused.
 */
static int tex_NameInRange(GLcontext context, GLuint texture)
{
	if (texture == 0)
		return 0;
	if (context->textureObjects == NULL || context->GeneratedTextures == NULL)
		return 0;

	return (texture < (GLuint)context->textureObjectCount);
}

GLboolean GLIsTexture(GLcontext context, GLuint texture)
{
	if (!tex_NameInRange(context, texture))
		return GL_FALSE;

	return context->GeneratedTextures[texture] ? GL_TRUE : GL_FALSE;
}

/*
 * glAreTexturesResident. "Resident" means held where the GPU samples it
 * without first being paged back in. In this driver that is every texture
 * there is: an upload goes into the texture's own allocation, and nothing
 * ever moves one out to make room for another. So every valid name is resident -- and GL's rule for
 * that case is to return GL_TRUE and leave `residences` untouched.
 *
 * "Valid" is exactly glIsTexture's test, generated and not deleted, so the two
 * cannot disagree. GL's errors are followed exactly: a negative count, or any
 * name that is zero or not a texture, is GL_INVALID_VALUE and returns GL_FALSE.
 */
GLboolean GLAreTexturesResident(GLcontext context, GLsizei n, const GLuint *textures, GLboolean *residences)
{
	GLsizei i;

	if (context->CurrentPrimitive != GL_BASE)
	{
		GLFlagError(context, 1, GL_INVALID_OPERATION);
		return GL_FALSE;
	}

	if (n < 0)
	{
		GLFlagError(context, 1, GL_INVALID_VALUE);
		return GL_FALSE;
	}

	if (n > 0 && textures == NULL)
		return GL_FALSE;

	for (i = 0; i < n; i++)
	{
		if (!GLIsTexture(context, textures[i]))
		{
			GLFlagError(context, 1, GL_INVALID_VALUE);
			return GL_FALSE;
		}
	}

	return GL_TRUE;
}

/*
 * glPrioritizeTextures. A priority ranks textures for eviction when texture
 * memory runs short, and this driver never evicts (see GLAreTexturesResident
 * above) -- there is no decision for a priority to influence. GL also makes
 * the priority per-texture state readable through glGetTexParameter, which
 * this driver does not have, so no stored value could ever be asked for
 * either. Validating the call is therefore the complete implementation. GL
 * silently ignores names that are zero or not textures, so they need no
 * check.
 */
void GLPrioritizeTextures(GLcontext context, GLsizei n, const GLuint *textures, const GLclampf *priorities)
{
	if (context->CurrentPrimitive != GL_BASE)
	{
		GLFlagError(context, 1, GL_INVALID_OPERATION);
		return;
	}

	if (n < 0)
	{
		GLFlagError(context, 1, GL_INVALID_VALUE);
		return;
	}
}

/* Defined further down, next to the upload paths it serves. See its own
 * comment for the whole mechanism. */
static void tex_SyncBeforeModify(GLcontext context, V3DTexture *tex);
extern void gl_InvalidateTexStateCache(V3DTexture *tex);   /* draw.c */

/*
 * Every name is range-checked (tex_NameInRange) before either table is
 * indexed. The table is 4096 entries by default, which is headroom rather
 * than safety: the name comes straight from the application, so nothing stops
 * it exceeding any size chosen here.
 *
 * Deleting a texture that is still bound reverts that unit's binding to the
 * default texture, as GL requires, and clears the unit's cached V3DTexture*
 * in context->backend.bound_texture[unit] along with it.
 */
void GLDeleteTextures(GLcontext context, GLsizei n, const GLuint *textures)
{
	int i;

	GLFlagError(context, n < 0, GL_INVALID_VALUE);

	if (n < 0 || textures == NULL)
		return;

	for (i = 0; i < n; i++)
	{
		GLuint name = *textures++;

		/* GL says names that are 0 or not textures are silently ignored.
		 * Out-of-range is this driver's own case and is ignored the same way. */
		if (!tex_NameInRange(context, name))
			continue;

		/* Unbind before freeing, so no cached pointer outlives the object. */
		if (context->CurrentBinding == (GLint)name)
		{
			context->CurrentBinding = 0;
			context->backend.bound_texture[0] = NULL;
		}
		if (context->VirtualBinding == (GLint)name)
		{
			context->VirtualBinding = 0;
			context->backend.bound_texture[1] = NULL;
		}

		if (context->textureObjects[name])
		{
			V3DTexture *doomed = context->textureObjects[name];

			/* Evict this texture from draw.c's state-record cache, which is
			 * keyed on the V3DTexture pointer: the struct is about to be
			 * free()d, and malloc can hand the same address to the next
			 * texture created. */
			gl_InvalidateTexStateCache(doomed);

			/* Park, don't free: a draw binned earlier this frame can still
			 * name this block's address, and v3d_mem_free -> FreeVec returns
			 * it to the system pool at once. No rename -- the texture is going
			 * away, so there is nothing to carry across and parking alone is
			 * sufficient.
			 *
			 * ON A FULL LIST, FLUSH FIRST. MGLFlushPendingRender drains the
			 * park list itself, and only after a wait that actually succeeded,
			 * so this recovers every block whose command list has retired --
			 * the reachable case, a delete storm during a level load while the
			 * last frame's render is still in flight. It stalls, but only on a
			 * path that has already run out of room.
			 *
			 * WAITING CANNOT RECOVER THE REST, which is why the list is sized
			 * to a stated bound (backend/hw/v3d_texture.c): blocks
			 * parked since the last present are not marked submitted, so no
			 * wait retires them -- the danger they carry is in the future, when
			 * the CL being built now is submitted, not in a render running now.
			 *
			 * LAST RESORT IS A LEAK, NOT A FREE. Freeing hands the system pool
			 * memory a binned draw will name, so the GPU then reads or writes
			 * whatever is allocated there next -- corruption outside the
			 * driver. Leaking costs one texture's block, permanently on
			 * AmigaOS since this is AllocVec memory, and it is bounded by the
			 * deletes in that one frame. The counter is the signal to raise
			 * V3D_TEX_PARK_MAX rather than to go looking for a leak. */
			if (v3d_texture_park_mem(&context->device, &doomed->texture_mem) < 0)
			{
				MGLFlushPendingRender(context);

				if (v3d_texture_park_mem(&context->device, &doomed->texture_mem) < 0)
				{
					g_mglv3d_texture_park_overflows++;
					E(("GLDeleteTextures: park list full after a flush -- "
					   "texture %ld's GPU memory is LEAKED rather than freed "
					   "under a draw that will name it. %ld so far.\n",
					   (LONG)name, (LONG)g_mglv3d_texture_park_overflows));
				}
			}

			free(doomed);
			context->textureObjects[name] = NULL;
		}

		context->GeneratedTextures[name] = 0;
	}
}

/*
 * Exhaustion reports GL_OUT_OF_MEMORY, not GL_INVALID_OPERATION: GL 1.1 lists
 * no error for glGenTextures, and 2.5 makes OUT_OF_MEMORY the code for a
 * resource limit. The names it could not produce are set to 0 rather than left
 * as the caller had them, so ignoring the error binds the default texture
 * instead of whatever was on the stack.
 */
void GLGenTextures(GLcontext context, GLsizei n, GLuint *textures)
{
	int i,j;

	GLFlagError(context, n < 0, GL_INVALID_VALUE);

	if (n < 0 || textures == NULL)
		return;

	j = 1;
	for (i=0; i<n; i++)
	{
		/* Find a free texture */
		while (j < context->textureObjectCount)
		{
			if (context->GeneratedTextures[j] == 0) break;
			j++;
		}

		/* Table full. The test is >=, not ==: a zero-length table leaves j at
		 * 1, which compares unequal, so == would hand out name 1 and then
		 * write GeneratedTextures[1] past the allocation. */
		if (j >= context->textureObjectCount)
		{
		    GLFlagError(context, 1, GL_OUT_OF_MEMORY);
		    while (i < n) { *textures++ = 0; i++; }
		    return;
		}

		/* Insert the texture in the output array, and flag it as used internally */
		*textures = j;
		context->GeneratedTextures[j] = 1;
		textures++;
		j++;
	}
}


void tex_FreeTextures(GLcontext context)
{
	int i;

	/* Teardown never passes through the normal drain, and vid_CloseWindow frees
	 * every texture with a render possibly still in flight. Wait once here, so
	 * the frees below and the park drain that follows are both safe. */
	MGLFlushPendingRender(context);

	for (i=0; i<context->textureObjectCount; i++)
	{
		if (context->textureObjects[i])
		{
			gl_InvalidateTexStateCache(context->textureObjects[i]);
			v3d_texture_free(&context->device, context->textureObjects[i]);
			free(context->textureObjects[i]);
			context->textureObjects[i] = NULL;
		}
	}

	/* Release every parked block and the recycling pool, or a session's worth
	 * of renamed allocations leaks. Unconditional: after the flush above there
	 * is nothing left running that could name them. */
	v3d_texture_drain_parked_all(&context->device);
}


void tex_SetEnv(GLcontext context, GLenum env)
{
	V3DTexture *tex;

	/* Resolves the texture bound to the ACTIVE unit (VirtualBinding for
	 * unit 1, CurrentBinding for unit 0), the same selection tex_SetFilter/
	 * tex_SetWrap below, GLBindTexture and GLTexImage2DNoMIP make. Unit 1
	 * is not skipped: an application configures it (a multitexture
	 * lightmap's GL_MODULATE, for example) with
	 * glActiveTextureARB(GL_TEXTURE1_ARB) followed by glTexEnvi, and
	 * dropping that call would leave the texture's `texenv_mode` at
	 * whatever it already held. */
	if (context->ActiveTexture)
		tex = context->textureObjects[context->VirtualBinding];
	else
		tex = context->textureObjects[context->CurrentBinding];

	/*
	 * THE ENVIRONMENT IS PER TEXTURE UNIT, not per texture object -- GL 1.1
	 * 3.8.9. Binding a different texture must therefore leave it alone.
	 *
	 * context->TexEnv[unit] is the state, and draw.c reads it from there. The
	 * texture object's texenv_mode field stays at the MODULATE it is created
	 * with and is not read for a single-texture draw.
	 *
	 * No texture needs to be bound: GL lets the environment be set for a unit
	 * whose texture is not bound yet, and the value must survive until it is.
	 * That is why `tex` is not tested here -- it is unused.
	 */
	(void)tex;

	context->CurTexEnv = env;

	switch(env)
	{
		case GL_MODULATE:
		case GL_DECAL:
		case GL_REPLACE:
		case GL_BLEND:
		case GL_ADD:
			context->TexEnv[context->ActiveTexture ? 1 : 0] = env;
			break;
		default:
			break;
	}
}

/*
 * GL_TEXTURE_ENV_COLOR, which GL_BLEND's equation needs and no other mode
 * reads. It takes four floats, which the integer entry point cannot carry, so
 * it needs a function of its own.
 *
 * GL_TEXTURE_ENV_MODE is accepted here too, GL allowing either entry point for
 * either pname, and forwarded so there is one implementation of the mode.
 *
 * The colour is NOT clamped. GL clamps it to [0,1] on the way in; the shader
 * multiplies it by a texel in [0,1] and the result is clamped at the vfpack,
 * so an out-of-range value cannot reach the framebuffer un-clamped -- but a
 * negative one would darken rather than being treated as zero. Clamped here
 * for that reason.
 */
void GLTexEnvfv(GLcontext context, GLenum target, GLenum pname, const GLfloat *params)
{
	int unit = context->ActiveTexture ? 1 : 0;
	int i;

	if (target != GL_TEXTURE_ENV || params == NULL)
	{
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	if (pname == GL_TEXTURE_ENV_MODE)
	{
		GLTexEnvi(context, target, pname, (GLint)params[0]);
		return;
	}

	if (pname != GL_TEXTURE_ENV_COLOR)
	{
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	for (i = 0; i < 4; i++)
	{
		GLfloat v = params[i];

		if (v < 0.0f) v = 0.0f;
		if (v > 1.0f) v = 1.0f;
		context->TexEnvColor[unit][i] = v;
	}
}

static UBYTE tex_GLFilter2V3D(GLenum filter)
{
	switch(filter)
	{
		case GL_NEAREST:                return V3D_TEXFILTER_NEAREST;
		case GL_LINEAR:                 return V3D_TEXFILTER_LINEAR;
		case GL_NEAREST_MIPMAP_NEAREST: return V3D_TEXFILTER_NEAREST_MIPMAP;
		case GL_LINEAR_MIPMAP_NEAREST:  return V3D_TEXFILTER_LINEAR_MIPMAP;
		case GL_NEAREST_MIPMAP_LINEAR:  return V3D_TEXFILTER_NEAREST_MIPMAP;
		case GL_LINEAR_MIPMAP_LINEAR:   return V3D_TEXFILTER_LINEAR_MIPMAP;
	}
	return V3D_TEXFILTER_NEAREST;
}

/* The GL minification enums encode TWO independent filters: the second word
 * is the filter applied BETWEEN mip levels, which V3D_TEXFILTER_* above
 * cannot represent -- it folds all four mipmap modes onto two values. That
 * lost axis is a separate hardware field (mip_filter_nearest), so map it
 * separately here rather than widening that enum.
 *
 * Only the MIN filter can select mipmapping; GL restricts MAG to
 * GL_NEAREST/GL_LINEAR, so this is never asked about the mag filter. */
static UBYTE tex_GLMipFilterNearest(GLenum min_filter)
{
	switch(min_filter)
	{
		case GL_NEAREST_MIPMAP_NEAREST: return 1;   /* pick one level  */
		case GL_LINEAR_MIPMAP_NEAREST:  return 1;
		case GL_NEAREST_MIPMAP_LINEAR:  return 0;   /* blend two levels */
		case GL_LINEAR_MIPMAP_LINEAR:   return 0;
	}
	/* Not a mipmapping mode. The value is unused once max_level is 0, but
	 * default to nearest so an unmipmapped texture can never be charged for
	 * a second level fetch. */
	return 1;
}

/* The texture bound to the ACTIVE unit -- VirtualBinding for unit 1,
 * CurrentBinding for unit 0. The same selection tex_SetEnv, GLBindTexture and
 * GLTexImage2DNoMIP make. */
static V3DTexture *tex_Bound(GLcontext context)
{
	if (context->ActiveTexture)
		return context->textureObjects[context->VirtualBinding];

	return context->textureObjects[context->CurrentBinding];
}

/*
 * ONE AXIS PER CALL, and that is the whole point of these four.
 *
 * glTexParameteri names exactly one half of a filter or wrap pair, and GL 1.1
 * 3.8.3 makes both purely per-object state. So each of these writes ONLY the
 * field it names and leaves the other exactly as this texture had it. A
 * function that wrote both halves would have to source the unnamed one from
 * somewhere outside the texture, which is a leak between texture objects. No invalidation is needed: the texture
 * state-record cache in draw.c compares all twelve fields on every lookup,
 * filter and wrap among them.
 */
void tex_SetMinFilter(GLcontext context, GLenum min)
{
	V3DTexture *tex = tex_Bound(context);

	if (!tex) return;

	/* The GL min enum carries the mip filter too, so both fields move. */
	tex->min_filter = tex_GLFilter2V3D(min);
	tex->mip_filter_nearest = tex_GLMipFilterNearest(min);
}

void tex_SetMagFilter(GLcontext context, GLenum mag)
{
	V3DTexture *tex = tex_Bound(context);

	if (!tex) return;

	tex->mag_filter = tex_GLFilter2V3D(mag);
}

void tex_SetWrapS(GLcontext context, GLenum wrap_s)
{
	V3DTexture *tex = tex_Bound(context);

	if (!tex) return;

	tex->wrap_s = (wrap_s == GL_REPEAT) ? V3D_TEXWRAP_REPEAT : V3D_TEXWRAP_CLAMP;
}

void tex_SetWrapT(GLcontext context, GLenum wrap_t)
{
	V3DTexture *tex = tex_Bound(context);

	if (!tex) return;

	tex->wrap_t = (wrap_t == GL_REPEAT) ? V3D_TEXWRAP_REPEAT : V3D_TEXWRAP_CLAMP;
}

void GLTexEnvi(GLcontext context, GLenum target, GLenum pname, GLint param)
{
	/*
	 * target, pname AND param ARE ALL VALIDATED. gl.h maps glTexEnvf and
	 * glTexEnviv onto this function, so an unchecked `param` would be installed
	 * as the environment mode from any of those spellings.
	 *
	 * GL_TEXTURE_ENV_COLOR is refused here: it needs four floats, which this
	 * signature cannot carry, and GLTexEnvfv takes it instead. Every glTexEnv*
	 * call across the client applications passes GL_TEXTURE_ENV_MODE, so
	 * no game reaches that refusal.
	 */
	if (target != GL_TEXTURE_ENV)
	{
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	if (pname == GL_TEXTURE_ENV_COLOR)
	{
		/* Four floats through a one-int entry point cannot be meant. GL has no
		 * integer spelling for this pname either, so this is the right refusal
		 * rather than a dropped call -- see GLTexEnvfv, which takes it. */
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	if (pname != GL_TEXTURE_ENV_MODE)
	{
		/* Anything else names nothing: GL 1.1 defines these two pnames only,
		 * and GL_COMBINE's family is 1.3. */
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	switch (param)
	{
		case GL_MODULATE:
		case GL_DECAL:
		case GL_REPLACE:
		case GL_BLEND:
		case GL_ADD:
			break;

		default:
			/* GL_COMBINE and the rest of the 1.3 family land here. Refused
			 * rather than stored: a mode kept as state with no shader behind it
			 * renders as GL_MODULATE, which is a silent wrong answer. */
			GLFlagError(context, 1, GL_INVALID_ENUM);
			return;
	}

	/* Records the mode in the ACTIVE unit's TexEnv entry and always passes
	 * it to tex_SetEnv, which resolves CurrentBinding vs VirtualBinding and
	 * applies it to the texture bound on that unit. The original MiniGL
	 * ignored unit 1 here until mglDrawMultitexBuffer; this driver does not
	 * (see tex_SetEnv). */
	context->TexEnv[context->ActiveTexture] = (GLenum)param;

	tex_SetEnv(context, param);
}

/*
 * MGLDrawMultitexBuffer (gl.h's mglDrawMultitexBuffer macro).
 *
 * The original (MiniGL/src/draw.c) is a deferred two-pass-blend flush: app
 * code accumulates polygons into a global mtex_pbuffer[] via glBegin/glEnd,
 * then this function draws the WHOLE accumulated buffer in two Warp3D
 * vertex-array-pointer passes (TMU0 unblended, TMU1 blended), using the
 * BSrc/BDst/TexEnv parameters passed to THIS call, then resets the buffer
 * and restores the prior blend state -- the temporary-override-then-restore
 * shape only makes sense because nothing has actually been drawn yet at the
 * point this is called.
 *
 * None of that buffer/two-pass machinery exists in this port, and none is
 * needed: multitexturing here is a single shader that samples both units'
 * textures and combines them in ONE pass, dispatched at glEnd() time via
 * gl_EmitPrimitiveV3D -- there is no deferred buffer to flush.
 *
 * What this function does: applies TexEnv to the V3DTexture bound to
 * GL_TEXTURE1_ARB (context->VirtualBinding) and records it as unit 1's
 * TexEnv, and applies BSrc/BDst through GLBlendFunc.
 *
 * DIFFERENCE FROM THE ORIGINAL: since there is no deferred buffer, this call
 * cannot retroactively affect geometry already submitted by an earlier
 * glEnd() -- unlike the original, where BSrc/BDst/TexEnv only ever applied to
 * the buffer this SAME call flushes. The blend func and unit-1 texenv mode
 * set here persist forward (ordinary GL state semantics) rather than being
 * scoped to-and-reverted-after one buffered draw. A caller must therefore
 * make this call BEFORE the glBegin/glEnd pair it is meant to affect, not
 * after it as the original's buffered model allowed.
 *
 * That order requirement is DETECTED rather than only documented. The
 * original's buffered model is not restored and must not be: multitexturing
 * here is one shader pass that samples both units, so there is no second pass
 * for BSrc/BDst to drive, and the original's own early-out is `polypointer ==
 * 0` -- there is no polypointer. Restoring it would mean adding a deferral
 * buffer purely to honour a call-order convention.
 */

/* Set here, cleared by draw.c when a multitextured draw is actually emitted.
 * Still set at the NEXT call means nothing multitextured was drawn in between,
 * which is base MiniGL's buffered order -- where this call flushed the geometry
 * itself. A program that calls this exactly once is therefore not caught; the
 * pattern this finds is the per-frame one both the multitexture demo and
 * a client application use. */
int g_mgl_mtex_flush_pending = 0;

void MGLDrawMultitexBuffer(GLcontext context, GLenum BSrc, GLenum BDst, GLenum TexEnv)
{
	static int said = 0;

	/* Validated before anything changes, as GLTexEnvi validates its target and
	 * pname. MODULATE, DECAL and REPLACE are the three draw.c can dispatch, and
	 * it maps everything else to MODULATE -- so recording an unsupported mode
	 * leaves context->TexEnv[1] disagreeing with the combine the draw takes. The
	 * blend factors need no check here: GLBlendFunc already raises
	 * GL_INVALID_ENUM for both. */
	if (TexEnv != GL_MODULATE && TexEnv != GL_DECAL && TexEnv != GL_REPLACE)
	{
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	if (g_mgl_mtex_flush_pending && !said)
	{
		said = 1;
		E(("MGLDrawMultitexBuffer: called again with no multitextured draw in "
		   "between. Base MiniGL flushed a buffered draw HERE; this driver "
		   "cannot, because the geometry already went out at glEnd. Call it "
		   "BEFORE the glBegin/glEnd pair it should affect. Said once per run.\n"));
	}
	g_mgl_mtex_flush_pending = 1;

	/* The texture object's texenv_mode is deliberately NOT written. draw.c reads
	 * context->TexEnv[1] for the unit's combine mode and does not read the
	 * object's field, so the write would be dead -- and that field's per-object
	 * permanence is what produces a client application's white-texture failure (its surface
	 * path's own comment), so writing it keeps that failure one `if` away. */
	context->TexEnv[1] = TexEnv;

	GLBlendFunc(context, BSrc, BDst);
}

void GLTexParameteri(GLcontext context, GLenum target, GLenum pname, GLint param)
{
	switch(pname)
	{
		/*
		 * Each case writes ONLY the field it names, onto the texture bound at
		 * the active unit. Passing the unnamed half from anywhere outside the
		 * texture would stamp it with another texture's value.
		 *
		 * No context-wide copy exists: the four GLcontext fields that could
		 * hold one are reserved (context.h), this driver having no
		 * glGetTexParameter and nothing else to read them. All four carry
		 * something else: RequestedBuffers, CurPolygonModeBack, CloseWorkbench
		 * and PerspectiveMapping_State.
		 */
		case GL_TEXTURE_MIN_FILTER:
			tex_SetMinFilter(context, (GLenum)param);
			break;

		case GL_TEXTURE_MAG_FILTER:
			tex_SetMagFilter(context, (GLenum)param);
			break;

		case GL_TEXTURE_WRAP_S:
			tex_SetWrapS(context, (GLenum)param);
			break;

		case GL_TEXTURE_WRAP_T:
			tex_SetWrapT(context, (GLenum)param);
			break;
		default:
			GLFlagError(context, 1, GL_INVALID_ENUM);
	}
}

/*
 * The alignments, row lengths and skips are validated before they are stored.
 * The skips MOVE THE READ POINTER in tex_ConvertToRGBA8, so a negative one
 * would read in front of the caller's buffer; an alignment of 0 or a negative
 * would be worse still, since it feeds the row-padding arithmetic.
 *
 * GL's own rules, so this rejects nothing a conforming program does: the two
 * alignments accept 1, 2, 4 or 8, and the row length and both skips must not
 * be negative. A rejected value leaves the old one in place.
 *
 * The default arm stays silent on purpose: the pnames this driver does not
 * implement (GL_UNPACK_SWAP_BYTES, GL_UNPACK_LSB_FIRST) are LEGAL GL names,
 * so answering GL_INVALID_ENUM would be wrong.
 *
 * The pack row length, skips and byte order are stored because GLReadPixels
 * (others.c) packs its result with them, with the same validation as the
 * unpack side. GL_PACK_LSB_FIRST is stored and answered only -- it applies to
 * GL_BITMAP, which this driver never reads.
 */
void GLPixelStorei(GLcontext context, GLenum pname, GLint param)
{
	switch(pname)
	{
		case GL_PACK_ALIGNMENT:
		case GL_UNPACK_ALIGNMENT:
			if (param != 1 && param != 2 && param != 4 && param != 8)
			{
				GLFlagError(context, 1, GL_INVALID_VALUE);
				return;
			}
			if (pname == GL_PACK_ALIGNMENT) context->PackAlign   = param;
			else                            context->UnpackAlign = param;
			break;

		case GL_UNPACK_ROW_LENGTH:
		case GL_UNPACK_SKIP_PIXELS:
		case GL_UNPACK_SKIP_ROWS:
			if (param < 0)
			{
				GLFlagError(context, 1, GL_INVALID_VALUE);
				return;
			}
			if      (pname == GL_UNPACK_ROW_LENGTH)  context->CurUnpackRowLength  = param;
			else if (pname == GL_UNPACK_SKIP_PIXELS) context->CurUnpackSkipPixels = param;
			else                                     context->CurUnpackSkipRows   = param;
			break;

		case GL_PACK_ROW_LENGTH:
		case GL_PACK_SKIP_PIXELS:
		case GL_PACK_SKIP_ROWS:
			if (param < 0)
			{
				GLFlagError(context, 1, GL_INVALID_VALUE);
				return;
			}
			if      (pname == GL_PACK_ROW_LENGTH)  context->PackRowLength  = param;
			else if (pname == GL_PACK_SKIP_PIXELS) context->PackSkipPixels = param;
			else                                   context->PackSkipRows   = param;
			break;

		case GL_PACK_SWAP_BYTES:
			context->PackSwapBytes = (param != 0) ? GL_TRUE : GL_FALSE;
			break;
		case GL_PACK_LSB_FIRST:
			context->PackLsbFirst  = (param != 0) ? GL_TRUE : GL_FALSE;
			break;

		default:
			break;
	}
}

/*
 * A texture object with no image yet. GL 1.1 3.8.8 creates one the first time a
 * name is bound, with the default state below, and it stays INCOMPLETE until
 * glTexImage2D gives it a level 0 -- which is what width == 0 records here.
 * draw.c treats an incomplete texture as no texture at all, so binding a fresh
 * name disables texturing rather than sampling memory that was never allocated.
 *
 * These defaults live here, at creation, and nowhere else. glTexImage2D must not
 * reapply them when it (re)allocates: that silently discards any
 * glTexParameteri the application made between the bind and the upload --
 * the ordering every Quake engine's lightmap setup uses.
 */
static V3DTexture *tex_NewObject(GLcontext context, GLuint name)
{
	V3DTexture *tex = (V3DTexture *)malloc(sizeof(V3DTexture));

	if (!tex)
		return NULL;

	memset(tex, 0, sizeof(V3DTexture));
	tex->min_filter         = tex_GLFilter2V3D(GL_NEAREST);
	tex->mag_filter         = tex_GLFilter2V3D(GL_LINEAR);
	tex->mip_filter_nearest = tex_GLMipFilterNearest(GL_NEAREST);
	tex->wrap_s             = V3D_TEXWRAP_REPEAT;
	tex->wrap_t             = V3D_TEXWRAP_REPEAT;
	tex->texenv_mode        = V3D_TEXENV_MODULATE;

	context->textureObjects[name] = tex;
	return tex;
}

void GLBindTexture(GLcontext context, GLenum target, GLuint texture)
{
   int active;

	/* GL_TEXTURE_2D is the only target this driver has; any other one is
	 * refused, and the binding is left alone. */
	if (target != GL_TEXTURE_2D) { GLFlagError(context, 1, GL_INVALID_ENUM); return; }

	/* A name past the end of the tables is refused, not indexed.
	 *
	 * Strict GL 1.1 has no error here: any unsigned integer is a legal
	 * texture name, and binding an unused one creates the object. This driver
	 * cannot do that -- the tables are a fixed textureObjectCount, sized at
	 * context creation -- so the honest failure is GL_INVALID_VALUE and no
	 * state change. A client that genuinely needs higher names needs a bigger
	 * table (mglChooseTextureBufferSize), not a silent wrong binding.
	 *
	 * Name 0 is the default texture and stays legal; the NULL-table case is
	 * covered too, since tex_NameInRange tests both arrays. */
	if (texture != 0 && !tex_NameInRange(context, texture))
	{
		D(("[texture] GLBindTexture OUT OF BOUNDS: texture=%ld textureObjectCount=%ld\n",
			(LONG)texture, (LONG)context->textureObjectCount));
		GLFlagError(context, 1, GL_INVALID_VALUE);
		return;
	}
	if (context->textureObjects == NULL)
		return;

	/* GL 1.1 3.8.8: the first bind of an unused name CREATES the object, so a
	 * glTexParameteri before any glTexImage2D has an object to reach. Name 0 is
	 * the default texture and is never created here. A failed allocation leaves
	 * the slot NULL, which the draw path treats as no texture. */
	if (texture != 0 && context->textureObjects[texture] == NULL)
		(void)tex_NewObject(context, texture);

   active = context->ActiveTexture;


   if(active)
   {
	context->VirtualBinding = texture;
	/* The backend's own per-unit binding (context.h), resolved through
	 * VirtualBinding exactly as the GL-side state around it is. */
	context->backend.bound_texture[1] = context->textureObjects[context->VirtualBinding];

	/* A BIND CHANGES NO FILTER OR WRAP. Both are per-object state living on
	 * the texture object, so there is nothing context-wide for a bind to
	 * reset -- and resetting one would change the filtering a later
	 * glTexParameteri hands the NEXT texture. */
   }
   else
   {
	context->CurrentBinding = texture;
	/* See the bound_texture[1] comment in the active-unit branch above. */
	context->backend.bound_texture[0] = context->textureObjects[context->CurrentBinding];

	/* BINDING CHANGES NO ENVIRONMENT. GL 1.1 3.8.9 makes the environment
	 * per UNIT, so a unit set to GL_REPLACE stays GL_REPLACE whatever texture
	 * arrives on it. Filter and wrap are untouched for the matching reason --
	 * see the unit-1 branch above. */
   }
}

/* GL (format, type) -> V3D_SRCFMT_* -- internalformat is applied separately,
 * see tex_InternalFormatNoAlpha. See this file's header comment for the
 * mappings.
 *
 * The packed 16-bit formats are accepted in `format` as well as in `type`,
 * because an application may name them either way -- a lightmap uploaded with
 * `type` GL_UNSIGNED_BYTE and the packed format in `format` reaches here.
 *
 * Returns -1 for a combination with no source format here; the callers must
 * bail out on that rather than upload. */
/* GL_COLOR_INDEX.
 *
 * Paletted textures are not a V3D source format and never can be: the V3D
 * TMU has no palette hardware, so an indexed image has to be expanded to
 * RGBA8 on the CPU during upload. That makes this a GL-side concern, not a
 * backend one -- the palette lives in the GL context (GLColorTable fills
 * context->PaletteData) and no V3D_SRCFMT_* is appropriate. Rather than
 * invent a backend enum value the hardware cannot mean, GL_COLOR_INDEX gets
 * a pseudo source format understood only inside this file, deliberately far
 * outside the V3D_SRCFMT_* range so it can never be mistaken for one if it
 * escapes into a backend call.
 */
#define TEX_SRCFMT_COLOR_INDEX8 0x100

/* The expanded palette, in final destination form -- see tex_BuildPaletteLUT.
 * File-static rather than a local: 1KB is more stack than an AmigaOS program
 * should take for granted, and this driver is single-context by construction,
 * so there is no second consumer to race with. Rebuilt per upload, since the
 * application may call glColorTable between uploads. */
static v3d_u32 g_tex_palette_lut[256];

/* Expands context->PaletteData into g_tex_palette_lut. Returns 0 on success,
 * -1 if no palette has been supplied.
 *
 * DESTINATION BYTE ORDER, derived from v3d_texture_convert_row's own RGBA8
 * case rather than guessed. That case reads source bytes R,G,B,A and stores
 *
 *     LE32((a << 24) | (b << 16) | (g << 8) | r)
 *
 * i.e. it builds the value as the GPU sees it (little-endian ABGR8888) and
 * then byteswaps host->GPU. LE32 is a real swap on this target -- three m68k
 * instructions, rol.w/swap/rol.w (v3d_types.h) -- so on this big-endian host
 * the two steps cancel and the bytes that actually land in memory are simply
 * R,G,B,A. Writing that directly, as below, is the same result with no swap.
 *
 * Doing it here, once per palette entry, is the point of the LUT: the colour
 * maths runs at most 256 times per upload instead of once per texel, and the
 * per-pixel loop reduces to one indexed load and one store. For a 256x256
 * paletted texture that is 256 conversions rather than 65,536.
 *
 * Entries beyond PaletteSize are filled with opaque black. GL leaves an
 * out-of-range index undefined; opaque black is the least surprising choice
 * and, unlike transparent, cannot interact oddly with alpha test or blending.
 */
static int tex_BuildPaletteLUT(GLcontext context, int opaque)
{
	const GLubyte *pal = (const GLubyte *)context->PaletteData;
	int entries = (int)context->PaletteSize;
	int i;

	if (pal == NULL || entries <= 0)
		return -1;

	if (entries > 256)
		entries = 256;

	/* GLColorTable stores every entry as four bytes R,G,B,A, already
	 * converted from the caller's format, type and internalformat (see its
	 * header comment), so there is one layout to read here. */
	for (i = 0; i < entries; i++)
	{
		const GLubyte *e = pal + (i * 4);
		v3d_u32 r = e[0], g = e[1], b = e[2], a = e[3];

		/* An RGB internalformat stores no alpha: forced here, once per
		 * entry, so the per-texel loop stays one load and one store. */
		if (opaque) a = 0xff;

		g_tex_palette_lut[i] = (r << 24) | (g << 16) | (b << 8) | a;
	}

	for (; i < 256; i++)
		g_tex_palette_lut[i] = 0x000000ffUL; /* opaque black */

	return 0;
}

static int tex_GLFormatToSrcFmt(GLenum format, GLenum type)
{
	if (type == MGL_UNSIGNED_SHORT_5_6_5 || format == MGL_UNSIGNED_SHORT_5_6_5)
		return V3D_SRCFMT_RGB565;
	if (type == MGL_UNSIGNED_SHORT_4_4_4_4 || format == MGL_UNSIGNED_SHORT_4_4_4_4)
		return V3D_SRCFMT_ARGB4;

	switch (format)
	{
		case GL_RGBA: return V3D_SRCFMT_RGBA8;
		case GL_RGB:  return V3D_SRCFMT_RGB8;
		case 4:       return V3D_SRCFMT_RGBA8;
		case 3:       return V3D_SRCFMT_RGB8;
		case GL_LUMINANCE_ALPHA: return V3D_SRCFMT_LA8;
		case 2:       return V3D_SRCFMT_LA8;
		/* One byte per texel, expanded by the backend to (L,L,L,1) and
		 * (1,1,1,A).
		 *
		 * 1 AND 2 ARE GL'S COMPONENT COUNTS, meaning luminance and
		 * luminance-alpha. They are accepted for the same reason 3 and 4 above
		 * are, and with the same looseness: GL 1.1 allows the counts only as an
		 * INTERNALFORMAT, and this switch sees the pixel-data `format`, so all
		 * four numeric cases are an extension rather than conformance. The
		 * tokens carry their real GL values (GL_ALPHA is 0x1906), and nothing
		 * gl.h defines with the value 1 or 2 is a pixel format, so the two
		 * cannot be confused.
		 *
		 * ONE DIVERGENCE FROM BASE MiniGL, deliberately: there literal 1 WAS
		 * GL_ALPHA under that positional enum, so a 1 meant A8. GL's meaning is
		 * luminance, and that is what it maps to here. */
		case GL_LUMINANCE: return V3D_SRCFMT_L8;
		case 1:            return V3D_SRCFMT_L8;
		case GL_ALPHA:     return V3D_SRCFMT_A8;

		/* Expanded through the palette to RGBA8 on the CPU -- see
		 * TEX_SRCFMT_COLOR_INDEX8 above for why this cannot be a backend
		 * format. Only an 8-bit index is accepted: GL also allows GL_BITMAP
		 * and the wider index types, but the callers here already reject
		 * every `type` except GL_UNSIGNED_BYTE and the two packed 16-bit
		 * ones. */
		case GL_COLOR_INDEX:
			return (type == GL_UNSIGNED_BYTE) ? TEX_SRCFMT_COLOR_INDEX8 : -1;
	}
	/* Anything else (GL_INTENSITY, GL_BITMAP types, ...) has no source format
	 * here. Returns -1; both callers bail out on a negative result instead of
	 * silently uploading uninitialized memory -- see GLTexImage2DNoMIP/
	 * GLTexSubImage2DNoMIP's own srcfmt<0 checks. */
	return -1;
}

static int tex_SrcBytesPerPixel(int srcfmt)
{
	switch (srcfmt)
	{
		case V3D_SRCFMT_RGBA8: case V3D_SRCFMT_ARGB8: return 4;
		case V3D_SRCFMT_RGB8: return 3;
		case TEX_SRCFMT_COLOR_INDEX8: return 1; /* one index byte per texel */
		case V3D_SRCFMT_L8: case V3D_SRCFMT_A8: return 1;
		default: return 2; /* ARGB4/ARGB1555/RGB565/LA8 */
	}
}

/* INTERNALFORMAT WITHOUT ALPHA.
 *
 * Base MiniGL chose the STORED format from internalformat (MGLConvert,
 * MiniGL/src/texture.c:926): internalformat 3 or GL_RGB stored an RGBA,
 * 4-4-4-4 or paletted source through RGBA_RGB / SHORT4444_565 / INDEX_RGB,
 * i.e. without its alpha, while 4 or GL_RGBA kept it. That is also the GL
 * rule -- an RGB base internal format has no alpha, so the texture reads as
 * alpha 1 whatever the pixel data held.
 *
 * Done on the DATA, at upload, so drawing costs nothing: no shader variant,
 * no per-draw term. Beyond base's own list this also covers GL_RGB5 and
 * GL_RGB8 (base fell off its switch for both) and the 5-6-5 internal format
 * base accepted. The literal 3 is safe to test: in this header's positional
 * enum 3 is GL_ALPHA_BITS, not a texture format, and the compiler rejects a
 * duplicate case label should that ever change.
 *
 * One deliberate simplification: GL would turn a GL_ALPHA source stored as
 * RGB into opaque BLACK (an ALPHA pixel group is (0,0,0,A)); this gives
 * opaque white, because the A8 expansion is (1,1,1,A). */
static int tex_InternalFormatNoAlpha(GLint internalformat)
{
	switch (internalformat)
	{
		case 3:
		case GL_RGB:
		case GL_RGB5:
		case GL_RGB8:
		case MGL_UNSIGNED_SHORT_5_6_5:
			return 1;
	}
	return 0;
}

/* Source formats whose conversion can produce alpha below 255 -- the only
 * ones an RGB internalformat has anything to strip from. */
static int tex_SrcCarriesAlpha(int srcfmt)
{
	switch (srcfmt)
	{
		case V3D_SRCFMT_RGB8:
		case V3D_SRCFMT_RGB565:
		case V3D_SRCFMT_L8:
			return 0;
	}
	return 1;
}

/* Converts `width`x`height` pixels of `srcfmt` data into a freshly
 * tex_Alloc'd RGBA8 scratch buffer (caller must tex_Free it), honoring
 * GL_UNPACK_ROW_LENGTH and GL_UNPACK_ALIGNMENT for the SOURCE row stride.
 * Row padding is computed as a straightforward "row bytes rounded up to
 * UnpackAlign" -- NOT the original's `CORRECT_ALIGN` macro, which tested
 * the ACCUMULATED POINTER's absolute address modulo `PackAlign` (the
 * wrong field for an unpack operation, and only "worked" because it
 * implicitly assumed the source buffer's base address was itself aligned
 * to the same boundary). */
/*
 * Is the unpack state asking for exactly "the caller's buffer, tightly packed,
 * starting at pixel zero"?
 *
 * Both RGBA8 fast paths below hand `pixels` straight to the backend with a
 * hardcoded width*4 stride, skipping tex_ConvertToRGBA8 entirely -- and with
 * it every unpack parameter, so they are only correct for such a buffer. The
 * fast path therefore stays for the common case of a tightly packed upload;
 * anything else goes the long way round.
 */
static int tex_UnpackIsTrivial(GLcontext context, int width)
{
	int rowLen = (context->CurUnpackRowLength > 0) ? context->CurUnpackRowLength : width;
	int rowBytes = rowLen * 4;                       /* the fast path is RGBA8 only */
	int align = context->UnpackAlign;

	if (context->CurUnpackSkipPixels != 0 || context->CurUnpackSkipRows != 0)
		return 0;

	if (align > 1 && (rowBytes % align))
		rowBytes += align - (rowBytes % align);

	return (rowBytes == width * 4);
}

static void *tex_ConvertToRGBA8(GLcontext context, const GLubyte *pixels, int width, int height, int srcfmt, int no_alpha)
{
	int bpp = tex_SrcBytesPerPixel(srcfmt);
	int rowLen = (context->CurUnpackRowLength > 0) ? context->CurUnpackRowLength : width;
	int srcRowBytes = rowLen * bpp;
	int align = context->UnpackAlign;
	int skipPixels = context->CurUnpackSkipPixels;
	int skipRows   = context->CurUnpackSkipRows;
	/* Strip alpha only where there is any to strip -- see
	 * tex_InternalFormatNoAlpha. */
	int opaque = (no_alpha && tex_SrcCarriesAlpha(srcfmt)) ? 1 : 0;
	v3d_u32 *scratch;
	const GLubyte *src;
	int y;

	if (align > 1 && (srcRowBytes % align))
		srcRowBytes += align - (srcRowBytes % align);

	/* Paletted sources are expanded here rather than in the backend row
	 * converter, which has no access to the palette and no business knowing
	 * about one. Build the lookup first so a missing palette fails before
	 * any allocation. */
	if (srcfmt == TEX_SRCFMT_COLOR_INDEX8 && tex_BuildPaletteLUT(context, opaque) < 0)
	{
		/* GL_COLOR_INDEX upload with no glColorTable ever supplied. GL
		 * leaves this undefined; refusing is better than expanding through
		 * a malloc'd-but-never-written palette buffer, which would upload
		 * heap garbage as texture content -- the exact failure mode the
		 * srcfmt<0 bail-outs exist to stop. */
		GLFlagError(context, 1, GL_INVALID_OPERATION);
		return NULL;
	}

	scratch = (v3d_u32 *)tex_Alloc((ULONG)(width * height * 4));
	if (!scratch) return NULL;

	/* GL_UNPACK_SKIP_ROWS / GL_UNPACK_SKIP_PIXELS select the window of the
	 * caller's buffer to upload.
	 *
	 * GL addresses the first element as
	 *     p + rowBytes * SKIP_ROWS + bpp * SKIP_PIXELS
	 * with rowBytes the padded stride already computed above, which is why
	 * this offset is applied after the alignment padding rather than before.
	 * Negative values cannot arrive: glPixelStorei rejects them, so this can
	 * only ever move the pointer forward. */
	src = pixels + (skipRows * srcRowBytes) + (skipPixels * bpp);
	for (y = 0; y < height; y++)
	{
		if (srcfmt == TEX_SRCFMT_COLOR_INDEX8)
		{
			v3d_u32 *d = scratch + (y * width);
			const GLubyte *s = src;
			int n = width;

			/* One indexed load and one store per texel: every colour
			 * conversion already happened, once per palette entry, in
			 * tex_BuildPaletteLUT. */
			while (n--)
				*d++ = g_tex_palette_lut[*s++];
		}
		else
		{
			v3d_texture_convert_row(scratch + (y * width), src, (v3d_u32)width, (v3d_u8)srcfmt);

			/* RGB internalformat: set alpha to 255 while the row is still
			 * hot. The scratch words' bytes in memory are R,G,B,A (see
			 * tex_BuildPaletteLUT's derivation from convert_row's RGBA8
			 * case), so on this big-endian host A is the low byte of each
			 * word and OR-ing 0xFF sets it without touching R, G or B. The
			 * paletted branch above already got its alpha from the LUT. */
			if (opaque)
			{
				v3d_u32 *d = scratch + (y * width);
				int n = width;

				while (n--)
					*d++ |= 0x000000ffUL;
			}
		}
		src += srcRowBytes;
	}

	return scratch;
}

/*
 * MID-FRAME TEXTURE REUSE HAZARD.
 *
 * THE BUG. This is a tile renderer. A draw call is BINNED carrying the
 * texture's address, and the texels are not read until the pass actually
 * renders, at present time. So overwriting a texture between two draws of the
 * same pass retroactively changes what the EARLIER draw samples:
 *
 *     bind T; TexSubImage(A); draw left;  TexSubImage(B); draw right; present
 *
 * renders B on BOTH sides.
 *
 * A glFinish issued by the application (GLFinish, vertexbuffer_min.c) is no
 * substitute for this check: an application may use no barrier at all and must
 * still come out right.
 *
 * THE FIX. Give the texture a fresh allocation and park the old one before the
 * new texels land, so the already-binned draws still sample the OLD contents
 * (see the rename below). If that fails, split the render pass instead, which
 * renders those draws before the overwrite. The split machinery is
 * load-bearing for two other features -- a mid-frame glClear and glReadPixels
 * (MGLReadbackBegin, context.c) both do exactly this. It finalizes the
 * in-progress pass into scratch_color_mem and composites it back on the next
 * pass's tile load, so nothing is lost and the screen never flickers.
 *
 * THE REGRESSION TRAP, and why the stamp exists at all. Acting on EVERY
 * mid-frame upload would punish an application that uploads lightmaps
 * mid-frame, every frame. This therefore fires only when the texture about to
 * be modified was ALREADY DRAWN in the current bin generation: an upload that
 * precedes every draw of that texture in the frame never trips it, while
 * overwriting one already drawn does.
 * A texture uploaded with unit 1 active resolves through VirtualBinding
 * -- the callers already do that, and the stamp in draw.c covers both units,
 * so the two halves agree.
 *
 * CALLED BEFORE ANY ALLOCATION OR UPLOAD WORK, which matters for more than
 * tidiness: glTexImage2D at a NEW SIZE frees the old block outright
 * (`v3d_texture_free` before `v3d_texture_alloc`), and the mip-chain promotion
 * moves level 0 into a fresh allocation and parks the old block -- freeing it
 * outright if the park list is full. Either way a binned draw can be left
 * pointing at memory already returned to the system pool -- the freed-memory
 * variant of the same bug, and worse than reading stale texels. The new-size
 * path also runs v3d_texture_alloc's memset, which would WIPE the stamp before
 * it could be read. Checking first covers all three.
 *
 * The copy entry points need no call of their own: tex_UploadCopiedRect
 * deliberately re-enters the public GLTexImage2D/GLTexSubImage2D, so they
 * inherit this.
 */
static void tex_SyncBeforeModify(GLcontext context, V3DTexture *tex)
{
	V3DContext *backend = &context->backend;
	extern void gl_FrameBegin(GLcontext context);              /* context.c */
	extern void gl_InvalidateTexStateCache(V3DTexture *tex);   /* draw.c */
	extern int g_mglv3d_frame_number;                          /* draw.c */

	if (tex == NULL || tex->last_draw_frame == 0)
		return;

	/* draw_state_configured is this codebase's only "the current pass already
	 * holds geometry" proxy (set by gl_EnsureDrawState on a pass's first draw,
	 * reset by gl_FrameBegin) -- the same guard GLClear uses, and for the same
	 * reason: gl_FramePresent has no empty-bin test of its own, so a split with
	 * nothing binned still runs a full-screen store and reload through the GPU
	 * for no benefit. frame_active is checked because force_new_pass is cleared
	 * ONLY inside gl_FrameBegin's frame_active branch: setting it with no frame
	 * open would not split, and would leave the flag armed to fire a spurious
	 * split at the next draw. */
	if (!backend->frame_active || !backend->draw_state_configured)
		return;

	if (tex->last_draw_frame != (v3d_u32)g_mglv3d_frame_number)
		return;

	/* RENAME, not a pass split. Give the texture a fresh allocation and park
	 * the old one: the draws already binned keep naming the old address, which
	 * stays valid because nothing frees it until the GPU has finished with
	 * that command list.
	 *
	 * copy_old is UNCONDITIONALLY 1 here, although a write that covers the
	 * whole allocation could skip it. The skip is only sound once the call is
	 * past every remaining argument check -- and this helper deliberately runs
	 * BEFORE them, so that a call which goes on to fail validation cannot
	 * leave a texture pointing at fresh, unwritten memory. Copying is always
	 * correct and merely wastes a memcpy on paths that were about to overwrite
	 * it. */
	if (v3d_texture_rename(&context->device, tex, 1) == 0)
	{
		/* MANDATORY, and the rename is silently undone without it -- draw.c
		 * caches the emitted shader-state record on the V3DTexture POINTER,
		 * which the rename keeps. See gl_InvalidateTexStateCache's own
		 * comment. */
		gl_InvalidateTexStateCache(tex);
		return;
	}

	/* FALLBACK: out of memory, or the park list is full. Split the pass --
	 * slow (a full-screen store and reload plus two blocking GPU waits) but
	 * correct, and it renders the binned draws so the texels can then be
	 * overwritten in place. Reaching here at all means the machine is under
	 * real memory pressure, where being slow beats being wrong. */
	backend->force_new_pass = TRUE;
	gl_FrameBegin(context);
	tex->last_draw_frame = 0;
}

void GLTexImage2DNoMIP(GLcontext context, GLenum gltarget, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
	int current;
	int srcfmt;
	void *rgba8;
	V3DTexture *tex;
	GLboolean was_new;
	/* GL-level sampler state, carried across v3d_texture_alloc (see below). */
	v3d_u8 save_min, save_mag, save_mipnear, save_wraps, save_wrapt, save_env;

	if(context->ActiveTexture)
		current = context->VirtualBinding;
	else
		current = context->CurrentBinding;

	/* Both refused before anything is touched. The type check matters most:
	 * tex_GLFormatToSrcFmt maps GL_RGBA and GL_RGB without looking at `type`,
	 * so float or short pixels would be uploaded as though they were bytes.
	 * The packed 16-bit formats may arrive in `format` instead, which
	 * tex_GLFormatToSrcFmt accepts either way. */
	if (type != GL_UNSIGNED_BYTE && type != MGL_UNSIGNED_SHORT_5_6_5 && type != MGL_UNSIGNED_SHORT_4_4_4_4)
	{
		GLFlagError(context, 1, GL_INVALID_OPERATION);
		return;
	}
	if (gltarget != GL_TEXTURE_2D) { GLFlagError(context, 1, GL_INVALID_ENUM); return; }

	/* GL 1.1 3.8.1 errors: a negative level, a size outside
	 * 0..MGL_MAX_TEXTURE_SIZE, or a border this driver cannot honour. Size
	 * matters most -- width and height are cast to v3d_u16 below, so an
	 * oversized or negative one wraps and the upload goes ahead against the
	 * wrong dimensions, or leaves the name with no object and no error. */
	if (level < 0 || border != 0 ||
	    width  < 0 || width  > MGL_MAX_TEXTURE_SIZE ||
	    height < 0 || height > MGL_MAX_TEXTURE_SIZE)
	{
		GLFlagError(context, 1, GL_INVALID_VALUE);
		return;
	}

	/* Before anything allocates, frees or writes texels -- see
	 * tex_SyncBeforeModify. Covers all three of this function's destructive
	 * paths: the same-size level-0 re-upload (in place), the different-size
	 * branch (frees the old block), and the mip-chain promotion (reallocates).
	 * Both branches below re-resolve the same object from `current`. */
	tex_SyncBeforeModify(context, context->textureObjects[current]);

	if (type == MGL_UNSIGNED_SHORT_5_6_5 || type == MGL_UNSIGNED_SHORT_4_4_4_4)
	    format = type;

	/* MIP LEVELS.
	 *
	 * A level > 0 upload requires the texture to exist already. GL requires
	 * level 0 to define the texture, so a conforming chain defines it first;
	 * rejecting the out-of-order case is better than guessing a base size
	 * from level 3.
	 *
	 * The dimensions are checked rather than trusted. GL fixes level N's size
	 * at width>>N by height>>N, and an application that disagrees would
	 * otherwise have its data tiled against the wrong padding -- silent
	 * corruption rather than an error. */
	if (level != 0)
	{
		V3DTexture* mip = context->textureObjects[current];
		v3d_u32 want_w, want_h;
		v3d_u8 chain_levels;

		GLFlagError(context, mip == NULL, GL_INVALID_OPERATION);
		if (mip == NULL) return;

		want_w = (v3d_u32)mip->width  >> level; if (!want_w) want_w = 1;
		want_h = (v3d_u32)mip->height >> level; if (!want_h) want_h = 1;

		GLFlagError(context, (v3d_u32)width != want_w || (v3d_u32)height != want_h,
		            GL_INVALID_VALUE);
		if ((v3d_u32)width != want_w || (v3d_u32)height != want_h) return;

		/* Promote on first use. The whole chain is allocated at once so the
		 * layout is decided a single time and never has to move again --
		 * levels above what has been uploaded stay unreadable because
		 * max_level tracks uploads, not allocation. */
		chain_levels = 1;
		{
			v3d_u32 w = mip->width, h = mip->height;
			while (w > 1 || h > 1) { w >>= 1; if (!w) w = 1; h >>= 1; if (!h) h = 1; chain_levels++; }
		}
		if (mip->num_levels < chain_levels)
		{
			if (v3d_texture_alloc_mipchain(&context->device, mip, chain_levels) < 0)
				return;   /* out of memory -- stays unmipped and usable */
		}

		srcfmt = tex_GLFormatToSrcFmt(format, type);
		GLFlagError(context, srcfmt < 0, GL_INVALID_ENUM);
		if (srcfmt < 0) return;

		/* The level inherits the texture's no_alpha, set from level 0's
		 * internalformat: GL calls a chain with mismatched formats
		 * incomplete anyway. */
		if (srcfmt == V3D_SRCFMT_RGBA8 && !mip->no_alpha
		    && tex_UnpackIsTrivial(context, width))
		{
			v3d_texture_upload_rgba8_level(&context->device, mip, (v3d_u8)level,
			                               pixels, (v3d_u32)(width * 4));
		}
		else
		{
			void *lvl = tex_ConvertToRGBA8(context, (const GLubyte *)pixels, width, height, srcfmt, mip->no_alpha);
			if (!lvl) return;
			v3d_texture_upload_rgba8_level(&context->device, mip, (v3d_u8)level,
			                               lvl, (v3d_u32)(width * 4));
			tex_Free(lvl);
		}
		return;
	}

	srcfmt = tex_GLFormatToSrcFmt(format, type);
	GLFlagError(context, srcfmt < 0, GL_INVALID_ENUM);
	/* GLFlagError records the error but does not return (gl.h) -- explicit
	 * bail-out needed here so a genuinely unsupported format can't fall through
	 * into uploading uninitialized scratch-buffer memory as texture content. */
	if (srcfmt < 0) return;

	tex = context->textureObjects[current];
	was_new = (tex == NULL) ? GL_TRUE : GL_FALSE;

	/* GL semantics require glTexImage2D to reallocate the backing store at
	 * the new size, so an existing texture name re-uploaded at a DIFFERENT
	 * size is treated the same as a brand-new name: free the old GPU
	 * allocation first (v3d_texture_alloc's own memset would otherwise leak
	 * it), then reallocate fresh at the new size. */
	if (tex == NULL || (v3d_u16)width != tex->width || (v3d_u16)height != tex->height)
	{
		if (tex == NULL)
		{
			/* Reached only when the name was never bound -- glBindTexture
			 * creates the object (GL 1.1 3.8.8), defaults and all. */
			tex = tex_NewObject(context, current);
			if (!tex) return;
			was_new = GL_TRUE;
		}
		else
		{
			v3d_texture_free(&context->device, tex);
		}

		/*
		 * v3d_texture_alloc OVERWRITES THE SAMPLER FIELDS -- of its own accord
		 * it sets min/mag to NEAREST and both wraps to V3D_TEXWRAP_BORDER
		 * (v3d_texture.c). Those are GL-level state here, so they are carried
		 * across the call and put back below. Losing them leaves every texture
		 * wrapping to a transparent-black border, and world geometry whose
		 * texcoords run far outside [0,1] then renders black.
		 */
		save_min     = tex->min_filter;
		save_mag     = tex->mag_filter;
		save_mipnear = tex->mip_filter_nearest;
		save_wraps   = tex->wrap_s;
		save_wrapt   = tex->wrap_t;
		save_env     = tex->texenv_mode;

		if (v3d_texture_alloc(&context->device, tex, (v3d_u16)width, (v3d_u16)height) < 0)
		{
			/* GL 1.1 2.5: an allocation that cannot be satisfied is
			 * GL_OUT_OF_MEMORY. There is no fallback (see the slot
			 * clearing below); the D() is silent unless this file is
			 * built with -DDEBUG. */
			GLFlagError(context, 1, GL_OUT_OF_MEMORY);
			D(("[texture] v3d_texture_alloc FAILED: texnum=%ld %ldx%ld\n",
				(LONG)current, (LONG)width, (LONG)height));

			/* The same unbinding GLDeleteTextures does, and for the same
			 * reason: draw.c's state-record cache is keyed on the V3DTexture
			 * pointer and malloc can hand this address to the next texture,
			 * and bound_texture[] caches that pointer per unit. The BINDING is
			 * left alone -- the name is still bound, it just has no object. */
			gl_InvalidateTexStateCache(tex);
			if (context->backend.bound_texture[0] == tex)
				context->backend.bound_texture[0] = NULL;
			if (context->backend.bound_texture[1] == tex)
				context->backend.bound_texture[1] = NULL;

			free(tex);
			/* The slot points at the struct just freed, whichever way we got
			 * here: tex_NewObject fills it in the was_new case too, so an
			 * `if (!was_new)` guard would leave a dangling pointer. */
			context->textureObjects[current] = NULL;
			return;
		}

		/* Back to the application's state. It is set once by tex_NewObject and
		 * is the application's to change after that: a glTexParameteri between
		 * the bind and this upload must survive it, and a re-upload at a new
		 * size must not quietly reset a texture to defaults. */
		tex->min_filter         = save_min;
		tex->mag_filter         = save_mag;
		tex->mip_filter_nearest = save_mipnear;
		tex->wrap_s             = save_wraps;
		tex->wrap_t             = save_wrapt;
		tex->texenv_mode        = save_env;

		context->textureObjects[current] = tex;
	}
	/* Else: re-uploading into an existing texture name at the SAME size
	 * -- no reallocation needed, tex is already correctly sized. */

	/* Level 0 defines the texture, so its internalformat decides no_alpha --
	 * on a brand-new texture, a reallocated one, and a same-size re-upload
	 * alike (the last may change internalformat without changing size).
	 * Must follow v3d_texture_alloc, whose memset clears the field. */
	tex->no_alpha = (v3d_u8)tex_InternalFormatNoAlpha(internalformat);

	if (srcfmt == V3D_SRCFMT_RGBA8 && !tex->no_alpha
	    && tex_UnpackIsTrivial(context, width))
	{
		/* Already exactly what the backend wants -- no scratch buffer. */
		v3d_texture_upload_rgba8(&context->device, tex, pixels, (v3d_u32)(width * 4));
	}
	else
	{
		/* Includes an RGBA8 source into an RGB internalformat: it needs its
		 * alpha set, which cannot be done in the caller's own buffer. */
		rgba8 = tex_ConvertToRGBA8(context, (const GLubyte *)pixels, width, height, srcfmt, tex->no_alpha);
		if (!rgba8) return;
		v3d_texture_upload_rgba8(&context->device, tex, rgba8, (v3d_u32)(width * 4));
		tex_Free(rgba8);
	}

	tex->format = V3D_SRCFMT_RGBA8; /* always RGBA8 once resident, regardless of source format */

	/* The ACTIVE unit's own cached env value, not unit 0's: tex_SetEnv writes
	 * whichever object is bound at the active unit (CurrentBinding/
	 * VirtualBinding), so feeding it unit 0's value while uploading to unit 1
	 * would stamp this texture's combine mode from the wrong unit. */
	tex_SetEnv(context, context->TexEnv[context->ActiveTexture]);
}

void GLTexImage2D(GLcontext context, GLenum gltarget, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
	/* A bare forward, with no mipmapping-allowed branch of its own:
	 * mglProhibitMipMapping(GL_FALSE) does NOT ask the driver to GENERATE a
	 * pyramid. Base MiniGL's own branch here generated nothing either: it
	 * sized one buffer for the whole chain, wrote each APP-SUPPLIED level
	 * into it at the right offset, and handed W3D_AllocTexObj a miparray[] of
	 * pointers. "Mipmapping allowed" means "assemble my levels into a real
	 * mipmapped texture object" -- which is what GLTexImage2DNoMIP does.
	 *
	 * ONE capability of base MiniGL's branch is deliberately not ported: it
	 * inferred the base size by up-scaling 2^level, so a first call at level 2
	 * could allocate the whole chain. That is not portable to the tiled layout
	 * here (the level offsets depend on the base dimensions, which a lone
	 * upper level pins only ambiguously for non-square textures -- base MiniGL's
	 * own comment admits the 8x4/4x2/2x1/1x1 case is unresolvable), and GL
	 * requires level 0 to define the texture anyway. */
	GLTexImage2DNoMIP(context, gltarget, level, internalformat, width, height, border, format, type, pixels);
}

/*
 * No CPU-side mirror is needed (see this file's header comment): a sub-image
 * update doesn't need to READ the texture's existing data, only WRITE a box
 * into it, and v3d_store_tiled_image (backend, through the
 * v3d_texture_upload_rgba8_subimage wrapper) supports an arbitrary
 * (x,y,width,height) sub-rectangle within a larger tiled image, not just the
 * (0,0)-origin full-image case GLTexImage2DNoMIP uses.
 * Same GL-format -> RGBA8 conversion path as GLTexImage2DNoMIP
 * (tex_GLFormatToSrcFmt/tex_ConvertToRGBA8), and the same RGBA8 fast path,
 * which hands the caller's own buffer to the backend and copies nothing.
 */
void GLTexSubImage2DNoMIP(GLcontext context, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
{
	int current;
	int srcfmt;
	void *rgba8;
	V3DTexture *tex;

	if(context->ActiveTexture)
		current = context->VirtualBinding;
	else
		current = context->CurrentBinding;

	/* Refused, not uploaded -- see GLTexImage2DNoMIP. */
	if (target != GL_TEXTURE_2D) { GLFlagError(context, 1, GL_INVALID_ENUM); return; }

	/* The type refusal GLTexImage2DNoMIP has: tex_GLFormatToSrcFmt maps
	 * GL_RGBA and GL_RGB without looking at `type`, so float or short pixels
	 * would be written into the texture as though they were bytes. */
	if (type != GL_UNSIGNED_BYTE && type != MGL_UNSIGNED_SHORT_5_6_5 && type != MGL_UNSIGNED_SHORT_4_4_4_4)
	{
		GLFlagError(context, 1, GL_INVALID_OPERATION);
		return;
	}

	tex = context->textureObjects[current];
	GLFlagError(context, tex == NULL, GL_INVALID_OPERATION);
	/* GLFlagError records the error but does not return (gl.h) -- without
	 * this explicit bail-out, `tex` being NULL here would fall straight
	 * through to `tex->width` below, a real null-pointer dereference. */
	if (tex == NULL) return;

	/* This path always writes straight into the live allocation -- it never
	 * allocates and never frees -- so without this call every draw already
	 * binned against this texture this pass would silently switch to the new
	 * texels. See tex_SyncBeforeModify. */
	tex_SyncBeforeModify(context, tex);

	/* A sub-rectangle update to a mip level is legal GL and is addressed here,
	 * as GLTexImage2DNoMIP addresses levels.
	 *
	 * Rejecting a level the texture does not have is deliberate: without a
	 * chain, num_levels is 1, so anything above level 0 is refused. It cannot
	 * promote the texture the way GLTexImage2DNoMIP does, because a
	 * sub-rectangle carries no information about the level's full size --
	 * there would be nothing to allocate the chain FROM. */
	GLFlagError(context, level < 0 || (v3d_u8)level >= tex->num_levels, GL_INVALID_VALUE);
	if (level < 0 || (v3d_u8)level >= tex->num_levels) return;

	/* A NEGATIVE SIZE IS REJECTED HERE, as GL 1.1 requires. It matters more
	 * than it looks: the offset test below compares xoffset + width, which a
	 * negative width passes, and the fast path further down performs no
	 * allocation, so nothing downstream would catch a mixed-sign box. */
	GLFlagError(context, (width < 0) || (height < 0), GL_INVALID_VALUE);
	if ((width < 0) || (height < 0)) return;

	if (type == MGL_UNSIGNED_SHORT_5_6_5 || type == MGL_UNSIGNED_SHORT_4_4_4_4)
	    format = type;

	srcfmt = tex_GLFormatToSrcFmt(format, type);
	GLFlagError(context, srcfmt < 0, GL_INVALID_ENUM);
	if (srcfmt < 0) return;

	/* Bounds are the LEVEL's dimensions, not the base texture's. GL fixes
	 * level N at width>>N by height>>N, so a box legal at level 0 is usually
	 * out of range further down the chain -- checking against tex->width here
	 * would let a level-2 update write past the end of level 2. */
	{
		GLint lvl_w = (GLint)(tex->width  >> level); if (lvl_w < 1) lvl_w = 1;
		GLint lvl_h = (GLint)(tex->height >> level); if (lvl_h < 1) lvl_h = 1;

		GLFlagError(context, (xoffset < 0) || (yoffset < 0) ||
		                     ((xoffset + width) > lvl_w) ||
		                     ((yoffset + height) > lvl_h), GL_INVALID_VALUE);
		if ((xoffset < 0) || (yoffset < 0) ||
		    ((xoffset + width) > lvl_w) || ((yoffset + height) > lvl_h)) return;
	}

	/* THE SAME THREE-TERM FAST PATH GLTexImage2DNoMIP HAS, and it needs all
	 * three terms. The uploader takes a const source with its own stride and
	 * addresses it from the box origin, so a tightly packed RGBA8 buffer is
	 * byte-for-byte what the scratch would have held; tex_UnpackIsTrivial is
	 * what rules out skip pixels, a row length and alignment padding, and
	 * !no_alpha is what keeps an RGBA8 source bound for an RGB internalformat
	 * on the converting path, since its alpha has to be forced and that cannot
	 * be done in the caller's own buffer. */
	if (srcfmt == V3D_SRCFMT_RGBA8 && !tex->no_alpha
	    && tex_UnpackIsTrivial(context, width))
	{
		v3d_texture_upload_rgba8_subimage(&context->device, tex, (v3d_u8)level,
		                                  pixels, (v3d_u32)(width * 4),
		                                  xoffset, yoffset, width, height);
		return;
	}

	/* A sub-image is stored in the texture's own internalformat, so an RGB
	 * texture keeps alpha 255 here too -- see tex_InternalFormatNoAlpha. */
	rgba8 = tex_ConvertToRGBA8(context, (const GLubyte *)pixels, width, height, srcfmt, tex->no_alpha);
	GLFlagError(context, rgba8 == NULL, GL_OUT_OF_MEMORY);
	/* A real bail-out is needed here too: GLFlagError does not return, so
	 * without it a conversion failure would upload from a NULL source
	 * pointer. */
	if (rgba8 == NULL) return;

	v3d_texture_upload_rgba8_subimage(&context->device, tex, (v3d_u8)level, rgba8, (v3d_u32)(width * 4),
	                                   xoffset, yoffset, width, height);

	tex_Free(rgba8);
}

void GLTexSubImage2D(GLcontext context, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
	/* Same reasoning as GLTexImage2D just above -- see there for why
	 * "mipmapping allowed" never meant driver-side generation.
	 * GLTexSubImage2DNoMIP addresses mip levels, so it serves the
	 * mipmapping-allowed case exactly as well. */
	GLTexSubImage2DNoMIP(context, target, level, xoffset, yoffset, width, height, format, type, (GLvoid *)pixels);
}

/* GL_TEXTURE_GEN_MODE's real spec value. This header's own GL_TEXTURE_GEN_MODE
 * is a historical positional number, so a client built against REAL GL headers
 * passes 0x2500 instead and would otherwise have its call rejected. The two
 * mode VALUES this driver added (GL_OBJECT_LINEAR, GL_EYE_LINEAR) carry their
 * real spec values already, so they need no such pairing -- see gl.h. */
#define MGL_TEXTURE_GEN_MODE_SPEC 0x2500

/*
 * glCopyTexSubImage2D. Without a path from the framebuffer into a texture,
 * reflections, refraction, portal and mirror views, motion-blur feedback and
 * render-to-texture UI are all unavailable -- there is no sequence of other
 * GL 1.1 calls that substitutes.
 *
 * Implemented as a CPU round trip: read the framebuffer rectangle back, then
 * hand it to the ordinary sub-image upload. Both halves already exist, and
 * neither the backend nor the CL has a framebuffer-to-texture copy to drive
 * instead. It is not fast -- a readback plus an upload per call -- but it is
 * correct.
 *
 * THREE THINGS THIS HAS TO GET RIGHT, none of them obvious:
 *
 * 1. The unpack state is LIVE. GL_UNPACK_ROW_LENGTH and the two skips really
 *    move the read pointer, so an application that left them set for its own
 *    atlas uploads would have this scratch buffer -- which is always tightly
 *    packed -- read at the wrong offset and stride. They are saved, defaulted
 *    and restored around the upload.
 *
 * 2. GL's x,y are BOTTOM-origin window coordinates. The readback
 *    (mgl_ReadPixelsTightRGBA, others.c -- GLReadPixels' own conversion with
 *    the caller's PACK state ignored, because this scratch is always tightly
 *    packed) takes them as GL does, so they pass straight through. The read
 *    also includes everything drawn so far this frame, like glReadPixels
 *    itself -- GL copies from the framebuffer as it stands.
 *
 * 3. A rectangle reaching outside the framebuffer leaves the readback's
 *    output untouched, so the scratch is cleared first: GL leaves those
 *    texels undefined, and undefined heap is worse than black.
 *
 * THE COPY CARRIES THE FRAMEBUFFER'S ALPHA. This framebuffer HAS an alpha
 * channel -- mgl_ReadColor takes it from the BGRA32 target's fourth byte -- so
 * GL requires the copy to use it. Reading GL_RGB instead fills alpha with 255,
 * which makes every render-to-texture copy opaque and puts the alpha beyond
 * reach of any internalformat that asks for it. An RGB
 * destination still comes out opaque, but that is decided by the
 * DESTINATION: glCopyTexImage2D's internalformat through
 * tex_InternalFormatNoAlpha, glCopyTexSubImage2D's through the texture's own
 * recorded tex->no_alpha, which GLTexSubImage2DNoMIP already honours.
 */
/* Shared by both copy entry points: the framebuffer rectangle as tightly
 * packed RGBA, or NULL. Caller frees. */
static GLubyte *tex_ReadFramebufferRGBA(GLcontext context, GLint x, GLint y,
                                        GLsizei width, GLsizei height)
{
	GLubyte *scratch;
	size_t n = (size_t)(width * height);
	size_t i;
	extern void mgl_ReadPixelsTightRGBA(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height, GLubyte *pixels);

	scratch = (GLubyte *)malloc(n * 4);
	if (!scratch)
	{
		GLFlagError(context, 1, GL_OUT_OF_MEMORY);
		return NULL;
	}

	/* A rectangle reaching outside the framebuffer leaves the readback's
	 * pixels untouched. GL leaves those texels undefined; undefined heap is
	 * worse than black. OPAQUE black, not transparent: a zero alpha would
	 * interact with alpha test and blending, which is the same reasoning
	 * tex_BuildPaletteLUT gives for its own out-of-range entries. */
	for (i = 0; i < n; i++)
	{
		scratch[i * 4 + 0] = 0;
		scratch[i * 4 + 1] = 0;
		scratch[i * 4 + 2] = 0;
		scratch[i * 4 + 3] = 255;
	}

	mgl_ReadPixelsTightRGBA(context, x, y, width, height, scratch);
	return scratch;
}

/* The scratch above is always tightly packed, so the caller's unpack state
 * must not be applied to it -- see the header comment. */
static void tex_UploadCopiedRect(GLcontext context, GLenum target, GLint level,
                                 GLint xoffset, GLint yoffset, GLint internalformat,
                                 GLsizei width, GLsizei height,
                                 const GLubyte *scratch, int defining)
{
	GLint save_rowlen = context->CurUnpackRowLength;
	GLint save_skipx  = context->CurUnpackSkipPixels;
	GLint save_skipy  = context->CurUnpackSkipRows;
	GLint save_align  = context->UnpackAlign;

	context->CurUnpackRowLength  = 0;
	context->CurUnpackSkipPixels = 0;
	context->CurUnpackSkipRows   = 0;
	context->UnpackAlign         = 1;

	/* Through the PUBLIC entry points, not the ...NoMIP ones they forward to.
	 * Both forwards are unconditional, so this is identical work -- but
	 * a copy is an upload, and anything ever added to glTexImage2D or
	 * glTexSubImage2D should apply to it rather than be skipped because the
	 * copy path reached past them. (The "NoMIP" names are historical: those
	 * two do store mip chains.) */
	if (defining)
		GLTexImage2D(context, target, level, internalformat,
		             width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scratch);
	else
		GLTexSubImage2D(context, target, level, xoffset, yoffset,
		                width, height, GL_RGBA, GL_UNSIGNED_BYTE, scratch);

	context->CurUnpackRowLength  = save_rowlen;
	context->CurUnpackSkipPixels = save_skipx;
	context->CurUnpackSkipRows   = save_skipy;
	context->UnpackAlign         = save_align;
}

static int tex_CopyArgsBad(GLcontext context, GLenum target,
                           GLsizei width, GLsizei height)
{
	if (target != GL_TEXTURE_2D)
	{
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return 1;
	}
	if (width < 0 || height < 0)
	{
		GLFlagError(context, 1, GL_INVALID_VALUE);
		return 1;
	}
	return (width == 0 || height == 0);       /* legal, nothing to do */
}

void GLCopyTexSubImage2D(GLcontext context, GLenum target, GLint level,
                         GLint xoffset, GLint yoffset,
                         GLint x, GLint y, GLsizei width, GLsizei height)
{
	GLubyte *scratch;

	if (tex_CopyArgsBad(context, target, width, height))
		return;

	scratch = tex_ReadFramebufferRGBA(context, x, y, width, height);
	if (!scratch) return;

	tex_UploadCopiedRect(context, target, level, xoffset, yoffset, 0,
	                     width, height, scratch, 0);
	free(scratch);
}

/*
 * glCopyTexImage2D -- the DEFINING form, alongside the sub-image one.
 *
 * It needs nothing the sub-image form does not: the same readback, the same
 * unpack-state guard, and the defining upload instead of the updating one. An
 * application that wants a reflection texture sized from the viewport calls
 * THIS, and without it would have to do a dummy glTexImage2D first to conjure
 * the storage.
 *
 * `border` must be 0, which this driver could not honour anyway.
 */
void GLCopyTexImage2D(GLcontext context, GLenum target, GLint level,
                      GLenum internalformat, GLint x, GLint y,
                      GLsizei width, GLsizei height, GLint border)
{
	GLubyte *scratch;

	if (tex_CopyArgsBad(context, target, width, height))
		return;

	if (border != 0)
	{
		GLFlagError(context, 1, GL_INVALID_VALUE);
		return;
	}

	scratch = tex_ReadFramebufferRGBA(context, x, y, width, height);
	if (!scratch) return;

	tex_UploadCopiedRect(context, target, level, 0, 0, (GLint)internalformat,
	                     width, height, scratch, 1);
	free(scratch);
}

/*
 * glTexGeni. The mode is recorded per coordinate (TexGenModeS/TexGenModeT),
 * and v_GenTexCoords (draw.c) generates a coordinate only where the mode is
 * one this driver performs -- sphere map, object-linear or eye-linear. Any
 * other mode leaves that coordinate's texcoord exactly as the application
 * supplied it. glTexGeniv does not exist here; the two linear modes take their plane
 * equations through GLTexGenfv below.
 *
 * The mode VALUE is not checked here. draw.c accepts the spec value 0x2402
 * alongside this header's own GL_SPHERE_MAP, because a client using REAL GL
 * headers passes 0x2402 -- same distinction as GL_TEXTURE0_ARB in gl.h.
 *
 * THE DEFAULT IS GL_EYE_LINEAR, GL's own (context.c). Every texgen caller in the
 * six trees sets its mode in the same breath as the enable: a client application does it
 * at its model-draw sites, all six of a client application's scenes that use texgen call
 * glTexGeni (29 calls), and so does every gears demo. The client applications use no texgen at all. The bare glEnable sites in glxsglut scene05
 * inherit that scene's OWN earlier GL_SPHERE_MAP, which is sticky state, not
 * this default. GL_EYE_LINEAR is itself a generated mode, so
 * the default does not take draw.c's CPU texgen path out of play.
 *
 * The parameter names are the original's: `mode` is GL's pname and `map` is
 * its value. They stay as they are because the prototype is published.
 */
void GLTexGeni(GLcontext context, GLenum coord, GLenum mode, GLenum map)
{
	if (mode != GL_TEXTURE_GEN_MODE && mode != MGL_TEXTURE_GEN_MODE_SPEC)
	{
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	if (coord == GL_S)
		context->TexGenModeS = map;
	else if (coord == GL_T)
		context->TexGenModeT = map;
	else
	{
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}
}

/*
 * glTexGenfv. This is what makes the two linear modes usable: they are
 * DEFINED by a plane equation, and without a way to supply one only the GL
 * default planes -- S = (1,0,0,0), T = (0,1,0,0), set at context creation --
 * would ever be used. GL_R and GL_Q are deliberately out of scope: the
 * hardware-facing vertex carries u0/v0/u1/v1 and no r or q, so generating
 * them would mean changing the vertex format, the attribute record and the
 * shaders.
 *
 * GL_EYE_PLANE is transformed HERE, once, by the inverse of the modelview
 * that is current at this call -- p' = p * inverse(M) -- exactly as GL
 * specifies. Storing the raw plane and transforming per vertex would be both
 * slower and wrong: a later glRotate/glTranslate would drag the plane along
 * with the object instead of leaving it fixed in eye space. That equation is
 * also why eye-linear and object-linear agree while the modelview does not
 * change, since p' . (M . v) == p . v.
 */
void GLTexGenfv(GLcontext context, GLenum coord, GLenum pname, const GLfloat *params)
{
	GLfloat *dst;
	GLfloat inv[16];
	int i;

	if (params == NULL)
	{
		GLFlagError(context, 1, GL_INVALID_VALUE);
		return;
	}

	/* Both spellings of the mode pname, for the same reason GLTexGeni takes
	 * both: a client with real GL headers passes 0x2500. */
	if (pname == GL_TEXTURE_GEN_MODE || pname == MGL_TEXTURE_GEN_MODE_SPEC)
	{
		GLTexGeni(context, coord, GL_TEXTURE_GEN_MODE, (GLenum)params[0]);
		return;
	}

	if (coord != GL_S && coord != GL_T)
	{
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	if (pname == GL_OBJECT_PLANE)
	{
		dst = (coord == GL_S) ? context->ObjectPlaneS : context->ObjectPlaneT;
		for (i = 0; i < 4; i++)
			dst[i] = params[i];
		return;
	}

	if (pname == GL_EYE_PLANE)
	{
		dst = (coord == GL_S) ? context->EyePlaneS : context->EyePlaneT;

		if (!m_Invert4(CurrentMV->v, inv))
		{
			/* Singular modelview: the plane is undefined rather than
			 * infinite. Keep whatever was there instead of storing NaNs. */
			GLFlagError(context, 1, GL_INVALID_OPERATION);
			return;
		}

		/* p' = p * inv, row vector times a column-major matrix: element
		 * (row r, col c) of inv lives at inv[c*4 + r] (matrix.h). */
		for (i = 0; i < 4; i++)
			dst[i] = params[0]*inv[i*4 + 0] + params[1]*inv[i*4 + 1]
			       + params[2]*inv[i*4 + 2] + params[3]*inv[i*4 + 3];
		return;
	}

	GLFlagError(context, 1, GL_INVALID_ENUM);
}

/* Colour-table component types: GL 1.1's integer and float spellings, each
 * converted the way GL converts colour components. GL_DOUBLE is not a
 * glColorTable type. */
static int tex_PaletteTypeAccepted(GLenum type)
{
	switch (type)
	{
		case GL_UNSIGNED_BYTE:  case GL_BYTE:
		case GL_UNSIGNED_SHORT: case GL_SHORT:
		case GL_UNSIGNED_INT:   case GL_INT:
		case GL_FLOAT:
			return 1;
	}
	return 0;
}

/* Source components per entry for a glColorTable `format`, 0 if refused. */
static int tex_PaletteSourceComponents(GLenum format)
{
	switch (format)
	{
		case GL_RED: case GL_GREEN: case GL_BLUE:
		case GL_ALPHA: case GL_LUMINANCE:
			return 1;
		case GL_LUMINANCE_ALPHA: return 2;
		case GL_RGB:             return 3;
		case GL_RGBA:            return 4;
	}
	return 0;
}

/* The base internal format of a colour table, returned as that GL name, or 0
 * if the internalformat is refused. The GL 1.x component counts 3 and 4 are
 * accepted; 1 and 2 cannot be, because this header numbers GL_ALPHA as 1 and
 * GL_ALPHA8 as 2 -- the collision tex_GLFormatToSrcFmt describes. */
static GLenum tex_PaletteBaseFormat(GLint internalformat)
{
	switch (internalformat)
	{
		case GL_ALPHA: case GL_ALPHA8:                        return GL_ALPHA;
		/* 1 and 2 belong here for the same reason 3 and 4 already do, and here
		 * they are plain GL 1.1 rather than an extension: this argument really
		 * is an internalformat, which is where GL allows the component counts. */
		case 1: case GL_LUMINANCE: case GL_LUMINANCE8:        return GL_LUMINANCE;
		case 2: case GL_LUMINANCE_ALPHA:
		case GL_LUMINANCE8_ALPHA8:                            return GL_LUMINANCE_ALPHA;
		case GL_INTENSITY: case GL_INTENSITY8:                return GL_INTENSITY;
		case 3: case GL_RGB: case GL_RGB5: case GL_RGB8:      return GL_RGB;
		case 4: case GL_RGBA: case GL_RGB5_A1: case GL_RGBA8: return GL_RGBA;
	}
	return 0;
}

/* Component `n` of a colour-table source array as an 8-bit value. GL scales
 * an unsigned component c / (2^b - 1) and a signed one (2c + 1) / (2^b - 1),
 * clamped to [0,1]. Integer arithmetic, exact for the 8- and 16-bit types;
 * the 32-bit types round from their top 16 bits, which cannot move a result
 * by more than 0.004 of a step. A float truncates first and then adds what
 * is left, so no rounding tie is ever formed under the caller's FPCR. */
static GLubyte tex_PaletteComponent(const GLvoid *data, GLenum type, int n)
{
	switch (type)
	{
		case GL_UNSIGNED_BYTE:
			return ((const GLubyte *)data)[n];
		case GL_BYTE:
		{
			/* GLbyte is plain char here, so the sign is made explicit. */
			GLint c = 2 * (GLint)(signed char)((const GLubyte *)data)[n] + 1;
			return (GLubyte)(c <= 0 ? 0 : c);
		}
		case GL_UNSIGNED_SHORT:
			return (GLubyte)(((GLuint)((const GLushort *)data)[n] * 255u + 32767u) / 65535u);
		case GL_SHORT:
		{
			GLint c = 2 * (GLint)((const GLshort *)data)[n] + 1;
			return (GLubyte)(c <= 0 ? 0 : ((GLuint)c * 255u + 32767u) / 65535u);
		}
		case GL_UNSIGNED_INT:
			return (GLubyte)(((((const GLuint *)data)[n] >> 16) * 255u + 32767u) / 65535u);
		case GL_INT:
		{
			GLint c = ((const GLint *)data)[n];
			return (GLubyte)(c <= 0 ? 0 : ((((GLuint)c) >> 15) * 255u + 32767u) / 65535u);
		}
		case GL_FLOAT:
		{
			GLfloat f = ((const GLfloat *)data)[n];
			GLfloat v;
			GLint i;
			if (!(f > 0.0f)) return 0;      /* negative, zero and NaN */
			if (f >= 1.0f)   return 255;
			v = f * 255.0f;
			i = (GLint)v;                   /* fintrz: truncates under any FPCR */
			if (v - (GLfloat)i >= 0.5f) i++;
			return (GLubyte)i;
		}
	}
	return 0;
}

/* Builds context->PaletteData, the palette GL_COLOR_INDEX uploads expand
 * through (see tex_BuildPaletteLUT).
 *
 * WHAT IS STORED: four bytes per entry, R,G,B,A, already in the form the
 * texel takes, so tex_BuildPaletteLUT has one layout to read. GL's two
 * conversions are applied here, once per entry:
 *  - the source group to RGBA, a missing component taking GL's default (R, G
 *    and B 0, A 1): GL_LUMINANCE is (L,L,L,1), GL_ALPHA (0,0,0,A), GL_RED
 *    (R,0,0,1), and so on;
 *  - RGBA to the base internal format: luminance and intensity keep R,
 *    alpha keeps A, RGB drops A.
 * The texel is then the form this driver's direct uploads give that base
 * format: luminance (L,L,L,1), luminance-alpha (L,L,L,A), intensity
 * (I,I,I,I), and alpha (1,1,1,A), as a GL_ALPHA upload expands.
 *
 * Every argument that selects a layout -- target, internalformat, format,
 * type and width -- is checked before PaletteData, PaletteFormat or
 * PaletteSize changes, so a refused call cannot leave the recorded format
 * and size describing bytes that were never written. PaletteFormat records the base
 * internal format.
 *
 * KNOWN DEVIATION FROM THE GL SPEC, deliberately left alone: for
 * `format` == GL_RGBA this reads the source as A,R,G,B, whereas GL defines
 * GL_RGBA as R,G,B,A. It is MiniGL's own convention -- the original reads a
 * GL_RGBA source as A,R,G,B too -- and changing it would silently alter the
 * meaning of existing callers' palettes rather than fix anything they
 * currently rely on. Worth revisiting only alongside a consumer that actually
 * needs spec behaviour. */
void GLColorTable(GLcontext context, GLenum target, GLenum internalformat, GLint width, GLenum format, GLenum type, GLvoid *data)
{
	GLubyte *where;
	GLenum base;
	int ncomp, i;

	/* The width, target and storage checks return explicitly, because a bare
	 * GLFlagError compiles to nothing under GL_NOERRORCHECK and would let a
	 * palette wider than the 256 entries PaletteData holds, a target that is
	 * not a palette, or a context with no palette storage reach the copy
	 * loops. A negative width is refused the same way. */
	if (width < 0 || width > 256) { GLFlagError(context, 1, GL_INVALID_VALUE); return; }
	/* Accept all THREE targets, each writing the one context palette.
	 * GL_COLOR_TABLE is this header's own positional enum;
	 * GL_SHARED_TEXTURE_PALETTE_EXT the real 0x81FB value a client built
	 * against its own GL headers passes; GL_TEXTURE_2D the entry point of
	 * EXT_paletted_texture, which glGetString advertises -- refusing it sets a
	 * STICKY GL_INVALID_OPERATION and then fails that client's upload too,
	 * nothing having been stored.
	 *
	 * Still ONE palette per context, not per texture: correct for
	 * set-palette-then-upload, because the indices are expanded through the
	 * current palette at upload time, and unable either way to honour a palette
	 * changed after a texture is resident -- the index image is not retained. */
	if (target != GL_COLOR_TABLE && target != GL_SHARED_TEXTURE_PALETTE_EXT
	    && target != GL_TEXTURE_2D)
	{
		GLFlagError(context, 1, GL_INVALID_OPERATION);
		return;
	}

	base  = tex_PaletteBaseFormat((GLint)internalformat);
	ncomp = tex_PaletteSourceComponents(format);
	if (base == 0 || ncomp == 0 || !tex_PaletteTypeAccepted(type))
	{
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	where = (GLubyte *)context->PaletteData;
	if (where == NULL) { GLFlagError(context, 1, GL_INVALID_OPERATION); return; }

	for (i = 0; i < width; i++)
	{
		int k = i * ncomp;
		GLubyte r = 0, g = 0, b = 0, a = 255;

		switch (format)
		{
			case GL_RED:   r = tex_PaletteComponent(data, type, k); break;
			case GL_GREEN: g = tex_PaletteComponent(data, type, k); break;
			case GL_BLUE:  b = tex_PaletteComponent(data, type, k); break;
			case GL_ALPHA: a = tex_PaletteComponent(data, type, k); break;
			case GL_LUMINANCE:
				r = g = b = tex_PaletteComponent(data, type, k);
				break;
			case GL_LUMINANCE_ALPHA:
				r = g = b = tex_PaletteComponent(data, type, k);
				a = tex_PaletteComponent(data, type, k + 1);
				break;
			case GL_RGB:
				r = tex_PaletteComponent(data, type, k);
				g = tex_PaletteComponent(data, type, k + 1);
				b = tex_PaletteComponent(data, type, k + 2);
				break;
			case GL_RGBA:   /* A,R,G,B -- see the deviation note above */
				a = tex_PaletteComponent(data, type, k);
				r = tex_PaletteComponent(data, type, k + 1);
				g = tex_PaletteComponent(data, type, k + 2);
				b = tex_PaletteComponent(data, type, k + 3);
				break;
		}

		switch (base)
		{
			case GL_ALPHA:           r = g = b = 255;    break;
			case GL_LUMINANCE:       g = b = r; a = 255; break;
			case GL_LUMINANCE_ALPHA: g = b = r;          break;
			case GL_INTENSITY:       g = b = a = r;      break;
			case GL_RGB:             a = 255;            break;
		}

		*where++ = r;
		*where++ = g;
		*where++ = b;
		*where++ = a;
	}
	context->PaletteFormat = base;
	context->PaletteSize   = width;
}

/*
 * Out-of-range units are refused, explicitly rather than through a bare
 * GLFlagError, which compiles to nothing under GL_NOERRORCHECK and would
 * store ANY value. The bound is EXCLUSIVE: `unit > GL_TEXTURE0_ARB +
 * MAX_TEXUNIT` would let GL_TEXTURE2_ARB through even with checks on.
 * ActiveTexture indexes the MAX_TEXUNIT-entry arrays Texture2D_State and
 * TexEnv, so either way a following glEnable(GL_TEXTURE_2D) would write past
 * them. MAX_TEXUNIT is 2 (gl.h), which is what GL_MAX_TEXTURE_UNITS_ARB
 * answers.
 */
void GLActiveTextureARB(GLcontext context, GLenum unit)
{
    if (unit < GL_TEXTURE0_ARB || unit >= GL_TEXTURE0_ARB + MAX_TEXUNIT)
    {
        GLFlagError(context, 1, GL_INVALID_ENUM);
        return;
    }

    context->ActiveTexture = unit - GL_TEXTURE0_ARB;
}

/*
 * $Id: glu.c,v 1.1.1.1 2000/04/07 19:44:51 tfrieden Exp $
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

#include <mgl/gl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* V3D_MAX_MIP_LEVELS. The mip generator below must not offer a level the
 * backend has no slot for: V3DTexture carries levels[V3D_MAX_MIP_LEVELS], and
 * v3d_texture_alloc_mipchain clamps its chain length to the same cap, so a
 * deeper chain would be accepted by the dimension check and then written past
 * the end of that array. Taking the constant from the backend rather than
 * repeating it keeps the two from drifting apart. */
#include "../../backend/include/v3d_texture.h"


#include <stdio.h>

/* E() and D(): the driver's diagnostic channel, kprintf-backed. */
#include "../../backend/hw/v3d_debug.h"


/* ZERO LENGTH LEAVES THE VECTOR ALONE, as the reference's own normalize()
 * does (glu-master/src/libutil/project.c). GLULookAt normalises three vectors
 * and multiplies the matrix in either way, so dividing unguarded puts
 * infinities and NaNs into the modelview for a degenerate eye/centre/up. */
#define VEC_NORM(v)                                      \
{                                                        \
    GLfloat m = sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]); \
    if (m != 0.0f)                                       \
    {                                                    \
        v[0] /= m;                                       \
        v[1] /= m;                                       \
        v[2] /= m;                                       \
    }                                                    \
}

#define VEC_CROSS(v, a, b)                               \
    v[0] = a[1] * b[2] - a[2] * b[1];                    \
    v[1] = a[2] * b[0] - a[0] * b[2];                    \
    v[2] = a[0] * b[1] - a[1] * b[0];

#define VEC_SUB(v, a, b)                                 \
    v[0] = a[0] - b[0];                                  \
    v[1] = a[1] - b[1];                                  \
    v[2] = a[2] - b[2];

#define VEC_ADD(v, a, b)                                 \
    v[0] = a[0] + b[0];                                  \
    v[1] = a[1] + b[1];                                  \
    v[2] = a[2] + b[2];

#define VEC_PRINT(v)                                     \
    D(("<%f, %f, %f>\n", v[0], v[1], v[2]));



void GLULookAt(GLfloat ex, GLfloat ey, GLfloat ez, GLfloat cx, GLfloat cy, GLfloat cz, GLfloat ux, GLfloat uy, GLfloat uz)
{
    GLfloat u[3], v[3], w[3];
    GLfloat m[16];

    w[0] = ex - cx;     w[1] = ey - cy;     w[2] = ez - cz;
    v[0] = ux;          v[1] = uy;          v[2] = uz;

    VEC_NORM(w);
    VEC_CROSS(u, v, w);
    VEC_CROSS(v, w, u);
    VEC_NORM(u);
    VEC_NORM(v);

    m[ 0] = u[0];   m[ 1] = v[0];   m[ 2] = w[0];   m[ 3] = 0.0;
    m[ 4] = u[1];   m[ 5] = v[1];   m[ 6] = w[1];   m[ 7] = 0.0;
    m[ 8] = u[2];   m[ 9] = v[2];   m[10] = w[2];   m[11] = 0.0;
    m[12] = 0.0;    m[13] = 0.0;    m[14] = 0.0;    m[15] = 1.0;

    glMultMatrixf(m);
    glTranslatef(-ex, -ey, -ez);
}

void GLUPerspective(GLfloat fovy, GLfloat aspect, GLfloat znear, GLfloat zfar)
{
   GLfloat xmin, xmax, ymin, ymax;
   /* 0.008726646 is pi/360, so this is half the field of view in radians --
    * the same quantity the reference takes its sine of. */
   GLfloat sine = (GLfloat)sin(fovy * 0.008726646);

   /* THE REFERENCE'S THREE EARLY RETURNS, silent and flagging no GL error
    * (glu-master/src/libutil/project.c): a zero aspect makes xmin == xmax and
    * an equal near and far makes a zero depth range, and glFrustum divides by
    * both. A zero sine means the field of view has no extent. */
   if (aspect == 0.0f || zfar == znear || sine == 0.0f)
      return;

   ymax = znear * tan(fovy * 0.008726646);
   ymin = -ymax;
   xmin = ymin * aspect;
   xmax = ymax * aspect;

   glFrustum(xmin, xmax, ymin, ymax, znear, zfar);
}


/* ==========================================================================
 * Mipmap pyramid generation
 *
 * The driver's mip UPLOAD half is complete: GLTexImage2D stores any level, and
 * the sampler's LOD range opens from [0, 1/256] to [0, max_level_uploaded] as
 * soon as a level above zero arrives. This is the driver's only GENERATOR --
 * the only producer of the levels that path consumes.
 *
 * The filter is GLU's 2x2 box filter, and GLU's ROUNDING behaviour with it, which
 * is not uniform across the taps. halveImage_ubyte adds a half before dividing --
 * `(a + b + c + d + 2) / 4` at mipmap.c:396-399 -- while both single-axis taps in
 * halve1Dimage_ubyte are plain truncating averages, `(a + b) / 2` at mipmap.c:432
 * and :454. So the bias belongs to the genuine four-texel tap only; applying it to
 * the whole horizontal branch would introduce rounding into the 1-D row case GLU
 * truncates. (Truncating everywhere instead is a half-LSB darker than GLU on
 * every level of every image.)
 * ========================================================================== */

/*
 * Bytes per texel of a source format, or 0 for one this generator cannot
 * filter. The pyramid is built in the CALLER's format rather than converted to
 * RGBA8 first, so the filter needs the stride; each level is then converted on
 * upload by the same path a lone glTexImage2D has always used.
 *
 * The set is NARROWER than both GLU's and this driver's own uploader, which is a
 * deliberate limit of the filter rather than of the upload path.
 * tex_GLFormatToSrcFmt (texture.c) additionally accepts the GL 1.0 component
 * counts 3 and 4, GL_COLOR_INDEX, and the packed 16-bit types -- and GLTexImage2D
 * will store any of them at any level, so the refusal here is this generator's
 * alone. They are excluded because the box filter works on independent bytes:
 * palette indices cannot meaningfully be averaged at all, and a packed 16-bit
 * texel would have to be unpacked and repacked per tap, which is what GLU's
 * separate halveImagePackedPixel exists to do.
 */
static int glu_FormatComponents(GLenum format)
{
    switch (format)
    {
        case GL_RGBA:            return 4;
        case GL_RGB:             return 3;
        case GL_LUMINANCE_ALPHA: return 2;
        case GL_LUMINANCE:       return 1;
        case GL_ALPHA:           return 1;
    }
    return 0;
}

/*
 * One box-filter step: src (sw x sh) reduced into dst (dw x dh). Both are
 * tightly packed at bpp bytes per texel.
 *
 * The two degenerate shapes are where a naive halve walks off the image, and
 * both are handled by aliasing rather than by special-casing:
 *
 *  - A height that is NOT halving (sh == 1, so dh == 1) aliases the second
 *    source row onto the first. The /4 then computes (a+b+a+b)/4, which is
 *    exactly the (a+b)/2 that a horizontal-only average wants. The test is
 *    `sh > 1 && sh > dh` rather than just `sh > 1`: the second clause is what
 *    stops a non-halving level from advancing two rows and reading a row that
 *    is not there.
 *  - A width that is not halving (sw == 1, so dw == 1) takes the /2 branch and
 *    reads column zero only.
 *
 * THAT ALIASING IS WHY THE ROUNDING BIAS IS GATED. Because the row-aliased case
 * runs through the same /4 as a real 2x2 tap, `halvingX` alone does not
 * distinguish them -- it is true whenever sw > 1, while srcRowStep == 2 is true
 * only when sh > 1. GLU rounds the four-texel tap and truncates the two-texel
 * ones, so the bias needs both conditions. Adding it to the whole branch, which
 * is the obvious one-line change, would round the 1-D row case that GLU does not.
 *
 * An odd dimension loses its last row or column outright, because dw = sw/2
 * truncates and index 2*dw-1 never reaches sw-1. GLU never meets that case at
 * all, since it rescales to powers of two first; this generator does not, so the
 * loss is real here and shifts coarse levels by up to half a texel per level.
 *
 * The accumulator is unsigned int and not a byte on purpose: four texels plus the
 * bias sum to as much as 1022, which needs ten bits.
 */
static void glu_HalveImage(const GLubyte *src, int sw, int sh,
                           GLubyte *dst, int dw, int dh, int bpp)
{
    const int srcRowBytes = sw * bpp;
    const int dstRowBytes = dw * bpp;
    const int srcRowStep  = (sh > 1 && sh > dh) ? 2 : 1;
    const int halvingX    = (dw < sw);
    /* GLU rounds the four-texel tap (mipmap.c:399, `+ 2) / 4`) and truncates both
     * single-axis taps (mipmap.c:432, :454). srcRowStep == 2 AND halvingX is
     * exactly "this is a genuine 2x2 tap". */
    const int bias        = (srcRowStep == 2 && halvingX) ? 2 : 0;
    int y, x, c;

    for (y = 0; y < dh; y++)
    {
        const GLubyte *rowA = src + (srcRowStep * y) * srcRowBytes;
        const GLubyte *rowB = (srcRowStep == 2) ? (rowA + srcRowBytes) : rowA;
        GLubyte       *out  = dst + y * dstRowBytes;

        if (halvingX)
        {
            for (x = 0; x < dw; x++)
            {
                const int a = (2 * x) * bpp;
                const int b = (2 * x + 1) * bpp;

                for (c = 0; c < bpp; c++)
                {
                    unsigned int s = (unsigned int)rowA[a + c] + (unsigned int)rowA[b + c]
                                   + (unsigned int)rowB[a + c] + (unsigned int)rowB[b + c];
                    out[x * bpp + c] = (GLubyte)((s + (unsigned int)bias) / 4);
                }
            }
        }
        else
        {
            for (x = 0; x < dw; x++)
                for (c = 0; c < bpp; c++)
                {
                    unsigned int s = (unsigned int)rowA[x * bpp + c]
                                   + (unsigned int)rowB[x * bpp + c];
                    out[x * bpp + c] = (GLubyte)(s / 2);
                }
        }
    }
}

/*
 * gluBuild2DMipmaps. Uploads the image as level 0 and every box-filtered level
 * below it, down to 1x1.
 *
 * Level 0 goes up with the caller's unpack state UNTOUCHED, so that upload is
 * byte-for-byte the one a plain glTexImage2D would have performed. The
 * generated levels are tightly packed, which that same state would misread:
 * tex_ConvertToRGBA8 rounds every source row up to GL_UNPACK_ALIGNMENT, so at
 * the default alignment of 4 a three-byte GL_RGB row at width 1 would be read
 * as four. All four unpack fields are therefore saved, forced to "tight, from
 * pixel zero", and restored -- the same thing tex_UploadCopiedRect does, for
 * the same reason.
 *
 * The level dimensions are not merely halved but must AGREE with the driver,
 * which checks each level against mip->width >> level floored at 1 and refuses
 * a mismatch. Repeated integer halving is identical to that shift, since
 * floor(floor(n/2)/2) == floor(n/4), so the two never disagree.
 *
 * internalFormat is passed through exactly as given, including the GL 1.0
 * component-count spelling -- callers do pass 3 for RGB, and
 * tex_InternalFormatNoAlpha handles the literal 3. Note that counts 1 and 2
 * cannot be supported by anything here: this header numbers GL_ALPHA as 1 and
 * GL_ALPHA8 as 2, so those spellings are indistinguishable from real enums.
 *
 * A NON-POWER-OF-TWO IMAGE IS RESCALED ONTO POWER-OF-TWO DIMENSIONS FIRST, as GLU
 * does (closestFit before anything else), using glu_NearestPower and an
 * area-weighted resample. Without it, each odd dimension drops its last row or
 * column on every halving step and shifts the coarse levels by up to half a texel
 * per level. A power-of-two image never enters that path and its level 0 is
 * uploaded straight from the caller's pointer with the caller's unpack state.
 *
 * Returns 0 on success or a GLU error code. It deliberately does not set the GL
 * error state: GLU reports through its return value, and glGetError belongs to
 * the uploads themselves.
 */
/*
 * GLU's nearestPower (mipmap.c:311-338). NOT a round-up: 3->4, 5->4, 6->8, 7->8,
 * so it cannot be replaced with 1 << ceil(log2 n).
 */
static GLint glu_NearestPower(GLuint v)
{
    GLint i = 1;
    for (;;)
    {
        if (v == 1) return i;
        if (v == 3) return i * 4;
        v >>= 1;
        i *= 2;
    }
}

/*
 * Area-weighted box resample, src (sw x sh) into dst (dw x dh), both tightly
 * packed at bpp bytes per texel. This is what GLU's scale_internal_ubyte does
 * (mipmap.c:1385-1600): each destination texel averages the source rectangle it
 * covers, with partial edge coverage handled by accumulating over whole source
 * texels and dividing by the count. Only used to force a non-power-of-two image
 * onto power-of-two dimensions before the halving chain starts.
 */
static void glu_ScaleImage(const GLubyte *src, int sw, int sh,
                           GLubyte *dst, int dw, int dh, int bpp)
{
    int dx, dy, c;

    for (dy = 0; dy < dh; dy++)
    {
        int y0 = (dy     * sh) / dh;
        int y1 = ((dy + 1) * sh) / dh;
        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > sh)  y1 = sh;

        for (dx = 0; dx < dw; dx++)
        {
            int x0 = (dx     * sw) / dw;
            int x1 = ((dx + 1) * sw) / dw;
            int n, x, y;
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > sw)  x1 = sw;
            n = (x1 - x0) * (y1 - y0);

            for (c = 0; c < bpp; c++)
            {
                unsigned long s = 0;
                for (y = y0; y < y1; y++)
                    for (x = x0; x < x1; x++)
                        s += src[((size_t)y * (size_t)sw + (size_t)x) * (size_t)bpp + c];
                dst[((size_t)dy * (size_t)dw + (size_t)dx) * (size_t)bpp + c] =
                    (GLubyte)(s / (unsigned long)n);
            }
        }
    }
}

GLint GLUBuild2DMipmaps(GLcontext context, GLenum target, GLint internalFormat,
                        GLsizei width, GLsizei height,
                        GLenum format, GLenum type, const GLvoid *data)
{
    const int bpp = glu_FormatComponents(format);
    GLint  save_rowlen, save_skipx, save_skipy, save_align;
    GLubyte *cur, *nxt;
    const GLubyte *base;
    int srcRowBytes, rowLen, align;
    int w, h, level, y, pw, ph, npot;

    if (target != GL_TEXTURE_2D)                  return GLU_INVALID_ENUM;
    if (type != GL_UNSIGNED_BYTE)                 return GLU_INVALID_ENUM;
    if (bpp == 0)                                 return GLU_INVALID_ENUM;
    if (width <= 0 || height <= 0 || data == NULL) return GLU_INVALID_VALUE;

    /* GLU forces the image onto power-of-two dimensions before building anything
     * (closestFit at mipmap.c:4601), so the whole chain is power-of-two. */
    pw   = glu_NearestPower((GLuint)width);
    ph   = glu_NearestPower((GLuint)height);
    npot = (pw != (int)width || ph != (int)height);

    /* Level 0 first, so the texture exists: the driver refuses a level > 0
     * upload against a texture that has not been defined yet, which is also
     * what GL requires. If everything below fails the result is still a
     * complete level-0 texture.
     *
     * Skipped for a non-power-of-two image, whose level 0 has to be the RESCALED
     * one -- uploaded further down once the resample has run. A power-of-two
     * image still takes this path with the caller's unpack state untouched, so
     * that upload stays byte-for-byte the one a plain glTexImage2D would do. */
    if (!npot)
        GLTexImage2D(context, target, 0, internalFormat,
                     width, height, 0, format, type, data);

    if (pw == 1 && ph == 1 && !npot)
        return 0;                                 /* nothing left to halve */

    /* The caller's own row stride, by the same arithmetic tex_ConvertToRGBA8
     * uses, so the tight copy below reads the image the driver just read. */
    rowLen      = (context->CurUnpackRowLength > 0) ? context->CurUnpackRowLength : (int)width;
    srcRowBytes = rowLen * bpp;
    align       = context->UnpackAlign;
    if (align > 1 && (srcRowBytes % align))
        srcRowBytes += align - (srcRowBytes % align);
    base = (const GLubyte *)data
         + (size_t)context->CurUnpackSkipRows   * (size_t)srcRowBytes
         + (size_t)context->CurUnpackSkipPixels * (size_t)bpp;

    /* Two scratch buffers, each large enough for level 0 so they can simply be
     * swapped as the chain shrinks. Level 1 is a quarter of level 0, so sizing
     * both at the full level-0 footprint is generous by design rather than
     * tight-fitted per level: it costs one allocation pair per call and removes
     * any question of a later level overrunning. The largest image this is
     * asked for in practice is 512x2048 RGB, so the pair peaks at about 6 MB
     * and is freed before returning. */
    {
        size_t need = (size_t)width * (size_t)height;
        if ((size_t)pw * (size_t)ph > need) need = (size_t)pw * (size_t)ph;
        need *= (size_t)bpp;
        cur = (GLubyte *)malloc(need);
        nxt = (GLubyte *)malloc(need);
    }
    if (cur == NULL || nxt == NULL)
    {
        free(cur);
        free(nxt);
        /* For a power-of-two image level 0 is already up, so the caller keeps a
         * complete single-level texture. For a rescaled one nothing was
         * uploaded, so put the unscaled image up rather than leaving the
         * texture undefined. */
        if (npot)
            GLTexImage2D(context, target, 0, internalFormat,
                         width, height, 0, format, type, data);
        return GLU_OUT_OF_MEMORY;
    }

    for (y = 0; y < (int)height; y++)
        memcpy(cur + (size_t)y * (size_t)width * (size_t)bpp,
               base + (size_t)y * (size_t)srcRowBytes,
               (size_t)width * (size_t)bpp);

    w = (int)width;
    h = (int)height;

    if (npot)
    {
        GLubyte *tmp;
        glu_ScaleImage(cur, w, h, nxt, pw, ph, bpp);
        tmp = cur; cur = nxt; nxt = tmp;
        w = pw;
        h = ph;
    }

    save_rowlen = context->CurUnpackRowLength;
    save_skipx  = context->CurUnpackSkipPixels;
    save_skipy  = context->CurUnpackSkipRows;
    save_align  = context->UnpackAlign;

    context->CurUnpackRowLength  = 0;
    context->CurUnpackSkipPixels = 0;
    context->CurUnpackSkipRows   = 0;
    context->UnpackAlign         = 1;

    /* The rescaled level 0, now that the unpack state reads a tight buffer. */
    if (npot)
        GLTexImage2D(context, target, 0, internalFormat,
                     w, h, 0, format, type, cur);

    level = 0;

    /* OR, not AND: the chain continues while EITHER dimension can still shrink,
     * which is what carries a 1024x32 image down to 1x1 instead of stopping at
     * 32x1. This is the same condition the driver uses to size the chain. */
    while (w > 1 || h > 1)
    {
        const int nw = (w > 1) ? (w / 2) : 1;
        const int nh = (h > 1) ? (h / 2) : 1;
        GLubyte *swap;

        if (level + 1 >= V3D_MAX_MIP_LEVELS)
            break;                                /* no slot for a deeper level */

        glu_HalveImage(cur, w, h, nxt, nw, nh, bpp);
        level++;

        GLTexImage2D(context, target, level, internalFormat,
                     nw, nh, 0, format, type, nxt);

        swap = cur; cur = nxt; nxt = swap;
        w = nw;
        h = nh;
    }

    context->CurUnpackRowLength  = save_rowlen;
    context->CurUnpackSkipPixels = save_skipx;
    context->CurUnpackSkipRows   = save_skipy;
    context->UnpackAlign         = save_align;

    free(cur);
    free(nxt);
    return 0;
}

/*
 * gluErrorString. This must switch on the driver's own TOKENS and never on the
 * numeric values GL assigns, because this header's error constants are
 * positional members of one big enum rather than 0x0500 upwards -- a literal
 * comparison here would name the wrong error.
 */
const GLubyte *GLUErrorString(GLenum errCode)
{
    switch (errCode)
    {
        case GL_NO_ERROR:          return (const GLubyte *)"no error";
        case GL_INVALID_ENUM:      return (const GLubyte *)"invalid enumerant";
        case GL_INVALID_VALUE:     return (const GLubyte *)"invalid value";
        case GL_INVALID_OPERATION: return (const GLubyte *)"invalid operation";
        case GL_STACK_OVERFLOW:    return (const GLubyte *)"stack overflow";
        case GL_STACK_UNDERFLOW:   return (const GLubyte *)"stack underflow";
        case GL_OUT_OF_MEMORY:     return (const GLubyte *)"out of memory";
        /* String verbatim from GLU's own table (error.c:52). Dead today -- nothing
         * in this driver raises GL_TABLE_TOO_LARGE, and gl.h defines the token only
         * for completeness -- but it closes the set for a future raiser, and an
         * unmapped code otherwise prints "unknown error". */
        case GL_TABLE_TOO_LARGE:   return (const GLubyte *)"table too large";
        case GLU_INVALID_ENUM:     return (const GLubyte *)"invalid enumerant";
        case GLU_INVALID_VALUE:    return (const GLubyte *)"invalid value";
        case GLU_OUT_OF_MEMORY:    return (const GLubyte *)"out of memory";
    }
    return (const GLubyte *)"unknown error";
}


/* ==========================================================================
 * GLU quadrics
 *
 * Three surfaces built from one facet emitter. Every surface emits GL_QUADS
 * rather than GL_QUAD_STRIP, which is what lets a single code path serve all
 * three normal modes: independent facets can carry either a per-vertex normal
 * (GLU_SMOOTH) or one shared normal (GLU_FLAT), where a strip would have to
 * share vertices between neighbouring facets and could only ever do the former.
 * The cost is four vertices per facet instead of two, which for the slice counts
 * quadrics are called with is immaterial, and GL_QUADS is the driver's
 * best-batched primitive in any case.
 *
 * These make only public gl calls, so they carry no dependency on driver
 * internals -- but they belong here rather than beside a GLUT shim, because GLU
 * is part of what an application linking this driver is entitled to expect.
 * ========================================================================== */

#define GLU_TWOPI 6.2831853071795864769f
#define GLU_PI    3.14159265358979323846f

struct GLUquadricObj_t
{
    GLenum    normals;
    GLboolean texture;
    GLenum    drawStyle;
    GLenum    orientation;
    /* The GLU_ERROR callback. GLU stores it as void (*)(GLint) and casts the
     * unprototyped pointer the caller hands over (quad.c:90); same here. */
    void      (*errorCallback)(GLint);
};

/*
 * The largest `slices` a GL_QUAD_STRIP ring can carry without losing vertices.
 *
 * A ring is 2*(slices+1) vertices, and everything past the buffer is dropped with
 * GL_OUT_OF_MEMORY flagged (vertexbuffer_min.c). Read from the live context rather
 * than hardcoded, because mglChooseVertexBufferSize can enlarge it -- glutInit asks
 * for 65536, which lifts the bound enormously and makes the clamp effectively inert
 * for any GLUT application.
 *
 * THE NORMAL BUFFER IS THE BINDING CONSTRAINT, NOT THE VERTEX BUFFER, which is
 * why this is VertexBufferSize-1 and not VertexBufferSize. The two checks differ:
 *
 *   GLVertex4f refuses at  VertexBufferPointer     >= VertexBufferSize
 *   GLNormal3f refuses at  NormalBufferPointer + 1 >= VertexBufferSize
 *
 * GLNormal3f PRE-increments, so index 0 is never used and only size-1 normals fit
 * where size vertices do. A cone or cylinder wall emits ONE normal per azimuth --
 * the strip's bottom and top vertex share it -- so the vertex bound binds there.
 * A TORUS band's two vertices lie on different rings with different normals, so it
 * emits TWO per step and the normal bound binds first: at slices = size/2 - 1 that
 * is exactly one normal too many.
 *
 * One bound for every shape, deliberately: it is the minimum across them, it costs
 * one slice of resolution nothing can see, and one helper that is right beats two
 * that can drift.
 *
 * GLU has a clamp of its own, to CACHE_SIZE-1 = 239 (quad.c:170, 447, 716-717),
 * and it CANNOT simply be copied: 239 is sized for GLU's own sin/cos caches and
 * 2*(239+1) = 480 does not fit a 256-entry buffer. Clamping rather than failing is
 * GLU's behaviour though -- it accepts an over-large request in silence and draws
 * a coarser figure, which is better than the silent geometry loss plus a
 * GL_OUT_OF_MEMORY that no caller reads.
 */
/* Not static: glutshapes.c needs the same bound, and one implementation of it
 * cannot drift from a second. */
GLint glu_MaxSlices(void)
{
    GLuint n = (mini_CurrentContext != NULL)
                 ? mini_CurrentContext->VertexBufferSize : 256;
    GLint  m = (GLint)((n - 1u) / 2u) - 1;   /* -1: the normal buffer, see above */
    return (m < 3) ? 3 : m;
}

GLUquadricObj *GLUNewQuadric(void)
{
    GLUquadricObj *q = (GLUquadricObj *)malloc(sizeof(GLUquadricObj));
    if (q == NULL)
        return NULL;

    /* GLU's own documented defaults. */
    q->normals       = GLU_SMOOTH;
    q->texture       = GL_FALSE;
    q->drawStyle     = GLU_FILL;
    q->orientation   = GLU_OUTSIDE;
    q->errorCallback = NULL;
    return q;
}

/*
 * The quadric's error channel. This is GLU's gluQuadricError (quad.c:78-83)
 * exactly -- including the silence when no callback is registered, which is
 * GLU's behaviour and not a shortcut. GLU raises nothing on the GL context
 * either; a quadric's errors go to its own callback or nowhere.
 *
 * Takes a const pointer because reporting does not modify the quadric, which
 * lets the shape entry points call it on their own argument without a cast.
 */
static void glu_QuadricError(const GLUquadricObj *q, GLenum which)
{
    if (q != NULL && q->errorCallback != NULL)
        q->errorCallback((GLint)which);
}


/*
 * A NULL quadric means the caller ignored a failed gluNewQuadric. Without this
 * check every entry point below accepts it and does nothing, so the allocation
 * failure turns into a run of calls that look like they work and draw nothing.
 *
 * It is reported on the GL context, which needs saying because it is the one
 * channel that exists. GLU itself defines nothing for a NULL quadric (SGI's
 * implementation dereferences it), and the quadric's OWN error callback cannot
 * be the channel here, because the object that would carry it is precisely what
 * is missing. The argument rejections further down are a different case and keep
 * their silence deliberately: those have a quadric, so the callback is the right
 * channel for them.
 */
static int glu_NoQuadric(const GLUquadricObj *q)
{
    if (q != NULL)
        return 0;
    if (mini_CurrentContext != NULL)
        GLFlagError(mini_CurrentContext, 1, GL_INVALID_VALUE);
    return 1;
}

void GLUDeleteQuadric(GLUquadricObj *q)
{
    if (glu_NoQuadric(q)) return;
    free(q);
}

/* GLU validates this one (quad.c:99-109) and raises GLU_INVALID_ENUM on
 * anything else, without storing it. GLU_FLAT is accepted and then rendered as
 * GLU_SMOOTH, as everywhere else in this file. */
void GLUQuadricNormals(GLUquadricObj *q, GLenum normals)
{
    if (glu_NoQuadric(q)) return;

    if (normals != GLU_SMOOTH && normals != GLU_FLAT && normals != GLU_NONE)
    {
        glu_QuadricError(q, GLU_INVALID_ENUM);
        return;
    }
    q->normals = normals;
}

void GLUQuadricTexture(GLUquadricObj *q, GLboolean textureCoords)
{
    if (glu_NoQuadric(q)) return;
    q->texture = textureCoords;
}

/*
 * ALL FOUR STYLES ON ALL THREE SURFACES: glu_ConeWire, glu_SphereWire and
 * glu_DiskWire carry GLU_POINT, GLU_LINE and GLU_SILHOUETTE, and each public
 * entry point dispatches on q->drawStyle with GLU_FILL on its own path.
 *
 * An unrecognised style is REFUSED AND NOT STORED, which is GLU's own behaviour
 * (quad.c:136-146 returns before the assignment), and it REPORTS -- through the
 * quadric's own GLU_ERROR callback, which gluQuadricCallback provides. A NULL
 * quadric is different and reports on the GL context through glu_NoQuadric,
 * because no quadric-borne channel could ever serve it.
 *
 * GLU_FLAT is rendered as GLU_SMOOTH, as everywhere else in this file.
 * For GLU_POINT and the vertical strips that is not even a divergence: GLU uses
 * its smooth per-vertex azimuth there too.
 */
void GLUQuadricDrawStyle(GLUquadricObj *q, GLenum drawStyle)
{
    if (glu_NoQuadric(q)) return;

    if (drawStyle != GLU_FILL && drawStyle != GLU_POINT &&
        drawStyle != GLU_LINE && drawStyle != GLU_SILHOUETTE)
    {
        glu_QuadricError(q, GLU_INVALID_ENUM);
        return;
    }

    q->drawStyle = drawStyle;
}

/* GLU validates this one (quad.c:120-130) and raises GLU_INVALID_ENUM on
 * anything else, without storing it. */
void GLUQuadricOrientation(GLUquadricObj *q, GLenum orientation)
{
    if (glu_NoQuadric(q)) return;

    if (orientation != GLU_OUTSIDE && orientation != GLU_INSIDE)
    {
        glu_QuadricError(q, GLU_INVALID_ENUM);
        return;
    }
    q->orientation = orientation;
}

/*
 * GLU defines exactly one callback for a quadric, GLU_ERROR, and this is it.
 *
 * An unrecognised `which` is reported THROUGH THE CALLBACK ALREADY REGISTERED,
 * which is GLU's own arrangement (quad.c:93 calls gluQuadricError from the
 * default arm). It reads oddly until you see why: the mechanism being
 * misconfigured is still the right place to say so, and a caller that has
 * registered nothing yet cannot be told by any means at all.
 *
 * fn is unprototyped, as GLU's _GLUfuncptr is, so the caller's own
 * void f(GLenum) needs no cast; the cast to the concrete signature happens
 * here, once, exactly as GLU does it.
 */
void GLUQuadricCallback(GLUquadricObj *q, GLenum which, MGLUfuncptr fn)
{
    if (glu_NoQuadric(q)) return;

    switch (which)
    {
        case GLU_ERROR:
            q->errorCallback = (void (*)(GLint))fn;
            break;
        default:
            glu_QuadricError(q, GLU_INVALID_ENUM);
            break;
    }
}

/*
 * PRIMITIVES AND NORMAL MODES, common to all three surfaces.
 *
 * Every surface follows GLU's own primitive for that surface -- a GL_QUAD_STRIP
 * per stack or ring, plus GL_TRIANGLE_FANs for the sphere's poles and the disc's
 * centre -- rather than one shared four-vertex facet emitter, because the
 * primitive choice is not cosmetic: a four-vertex facet costs 4*slices vertices
 * per block where a strip costs 2*(slices+1), and the default vertex buffer is
 * 256 entries.
 *
 * Each surface does the normal work inline: GLU_NONE guards on every glNormal3f,
 * GLU_INSIDE negating the normals and reversing the traversal. GLU_FLAT is not
 * implemented for any surface at present and renders as GLU_SMOOTH, where GLU
 * attaches one flat normal to the provoking vertex. For GLU_INSIDE a QUAD_STRIP
 * swaps the two vertices within each column rather than reversing a whole
 * four-vertex order.
 *
 * One divergence from GLU survives on purpose, in glu_TessCone rather than here:
 * for GLU_INSIDE on a TAPERED cone it negates z along with xy, where GLU negates
 * only the xy part and leaves zNormal alone (quad.c:213-214). GLU's own sphere and
 * disc negate fully, so that reads as an inconsistency in GLU; matching it is a
 * judgement call rather than an obligation, and no caller in either tree sets
 * GLU_INSIDE at all. For a straight cylinder deltaRadius is 0, so there is no
 * difference either way.
 */

/*
 * A truncated cone along +z, from radius r0 at z=0 to radius r1 at z=h. A
 * cylinder is the r0 == r1 case and a cone the r1 == 0 case, which is why
 * gluCylinder alone covers both and takes separate radii.
 *
 * The side normal is not (ct, st, 0): a tapered wall leans, and its normal
 * leans with it by (r0 - r1) in z. The L > 0 guard keeps a fully degenerate
 * request (zero radii and zero height) from dividing by zero; GLU does not
 * define that case, and answering with a radial normal is better than a NaN.
 *
 * GLUT 3.7's glutSolidCone is nothing but a gluCylinder(base, 0, height) call
 * (glut_shapes.c), so everything this function leaves behind is also GLUT's
 * solid-cone behaviour, current normal included.
 */
static void glu_TessCone(const GLUquadricObj *q,
                         GLfloat r0, GLfloat r1, GLfloat h,
                         GLint slices, GLint stacks)
{
    const GLfloat nz   = r0 - r1;
    const GLfloat flip = (h < 0.0f) ? -1.0f : 1.0f;
    const GLfloat L    = (GLfloat)sqrt(nz * nz + h * h);
    const GLfloat xy   = (L > 0.0f) ? (flip * h  / L) : 1.0f;
    const GLfloat zn   = (L > 0.0f) ? (flip * nz / L) : 0.0f;
    const int inside   = (q->orientation == GLU_INSIDE);
    /*
     * Which end of a column goes first. GLU has only the GLU_INSIDE reason to
     * swap; this has two, because h < 0 is an extension GLU does not have (it
     * rejects h < 0 outright at quad.c:172-176, drawing nothing). A negative
     * height puts the far ring BELOW the near one, which reverses every quad's
     * traversal on its own, so the two reasons XOR: flipping for h < 0 alone
     * restores outward winding, and flipping for both cancels back to GLU's
     * order. Without this the normals point out while the faces face in, and an
     * application with GL_CULL_FACE loses the whole wall -- which is live, since
     * glxsglut scene06 drives glutSolidCone's height from a timer that starts
     * below zero.
     */
    const int rev      = (inside != (h < 0.0f));
    GLint i, j;

    /* Defensive only: GLUCylinder rejects slices < 2 and stacks < 1 before
     * reaching here, per GLU. The floor is 2 rather than 3 because GLU accepts
     * slices == 2 and draws the degenerate figure instead of promoting it. */
    if (slices < 2) slices = 2;
    if (stacks < 1) stacks = 1;

    /*
     * GLU's own parametrization, not an invented one. Three things follow from
     * matching it, and only the first is about the picture.
     *
     * BLOCK LENGTH. GLU opens a GL_QUAD_STRIP inside its stack loop
     * (quad.c:264), never one block for a whole surface. The vertex buffer is
     * 256 entries by default and GLVertex4f drops everything past it, so one
     * block per surface silently loses most of a quadric.
     *
     * AXES. GLU places a point at x = radius*sin(theta), y = radius*cos(theta)
     * (quad.c:282-289). Using cos for x and sin for y is a MIRROR of that, not a
     * rotation, and it shows the moment anything generates texture coordinates.
     *
     * ONE NORMAL PER COLUMN. A cone's side normal does not vary along z, so GLU
     * emits it once before the two vertices (quad.c:271):
     * (xyNormalRatio*sin, xyNormalRatio*cos, zNormal), with
     * xyNormalRatio = height/L, zNormal = deltaRadius/L, deltaRadius = r0 - r1
     * and L = hypot(deltaRadius, height).
     *
     * Columns run i = 0..slices INCLUSIVE, so the closing column repeats
     * theta = 0 and leaves (0, height/L, deltaRadius/L) as the CURRENT NORMAL on
     * return. An application that draws afterwards without setting its own
     * normal inherits exactly that, and sphere-map texgen reads it.
     */
    for (j = 0; j < stacks; j++)
    {
        const GLfloat f0  = (GLfloat)j       / (GLfloat)stacks;
        const GLfloat f1  = (GLfloat)(j + 1) / (GLfloat)stacks;
        const GLfloat z0  = h * f0;
        const GLfloat z1  = h * f1;
        const GLfloat rr0 = r0 + (r1 - r0) * f0;
        const GLfloat rr1 = r0 + (r1 - r0) * f1;

        glBegin(GL_QUAD_STRIP);
        for (i = 0; i <= slices; i++)
        {
            const GLfloat s  = (GLfloat)i / (GLfloat)slices;
            const GLfloat t  = GLU_TWOPI * s;
            const GLfloat st = (GLfloat)sin(t), ct = (GLfloat)cos(t);

            if (q->normals != GLU_NONE)
            {
                GLfloat sa = st, ca = ct;

                /* GLU_FLAT: the facet's midpoint azimuth, not the vertex's own
                 * (quad.c:223), with index slices wrapped to 0 (quad.c:241). */
                if (q->normals == GLU_FLAT)
                {
                    const GLint   im = (i == slices) ? 0 : i;
                    const GLfloat ta = GLU_TWOPI * ((GLfloat)im - 0.5f)
                                                 / (GLfloat)slices;
                    sa = (GLfloat)sin(ta);
                    ca = (GLfloat)cos(ta);
                }

                if (inside) glNormal3f(-xy * sa, -xy * ca, -zn);
                else        glNormal3f( xy * sa,  xy * ca,  zn);
            }

            /* GLU's s runs 1 - i/slices (quad.c:279). */
            if (rev)
            {
                if (q->texture) glTexCoord2f(1.0f - s, f1);
                glVertex3f(rr1 * st, rr1 * ct, z1);
                if (q->texture) glTexCoord2f(1.0f - s, f0);
                glVertex3f(rr0 * st, rr0 * ct, z0);
            }
            else
            {
                if (q->texture) glTexCoord2f(1.0f - s, f0);
                glVertex3f(rr0 * st, rr0 * ct, z0);
                if (q->texture) glTexCoord2f(1.0f - s, f1);
                glVertex3f(rr1 * st, rr1 * ct, z1);
            }
        }
        glEnd();
    }
}

/*
 * GLU_POINT, GLU_LINE and GLU_SILHOUETTE for the cone and cylinder, from
 * quad.c:308-417. GLU_FILL stays in glu_TessCone above, untouched.
 *
 * THE WIRE STYLES MAKE `stacks` A BLOCK LENGTH, which GLU_FILL does not: FILL
 * opens one strip per stack, so more stacks means more blocks, while a point
 * column and a vertical line strip are stacks+1 vertices in ONE block. That is
 * why glu_MaxSlices() is applied to stacks here and only here. It is the
 * conservative bound rather than the exact one -- the exact figure differs per
 * block shape, since a ring emits a normal per vertex while a column emits one
 * for the whole block -- and one bound that is always safe beats three that can
 * drift. It costs resolution only in requests nothing makes.
 *
 * GLU wraps every GLU_POINT grid in a SINGLE glBegin of slices*(stacks+1)
 * vertices (quad.c:309). That is split per slice here, because the vertex
 * buffer is 256 entries and GLVertex4f drops the excess with only a
 * GL_OUT_OF_MEMORY to show for it. Splitting points is free: a point carries no
 * connectivity. THE LINE STRIPS MUST NOT BE SPLIT the same way -- a strip's
 * segments join its vertices, so cutting one drops the joining segment.
 *
 * GLU_FLAT uses the SMOOTH per-vertex azimuth for the points and the vertical
 * strips, which is GLU's own choice (quad.c:311-315, :392-395) and not a
 * simplification; for the rings GLU reads its flat cache, where this file's
 * GLU_FLAT already renders as GLU_SMOOTH throughout (see the normal-mode note
 * above).
 */
static void glu_ConeWire(const GLUquadricObj *q, GLfloat r0, GLfloat r1,
                         GLfloat h, GLint slices, GLint stacks, GLenum style)
{
    const GLfloat nz   = r0 - r1;
    const GLfloat flip = (h < 0.0f) ? -1.0f : 1.0f;
    const GLfloat L    = (GLfloat)sqrt(nz * nz + h * h);
    const GLfloat xy   = (L > 0.0f) ? (flip * h  / L) : 1.0f;
    const GLfloat zn   = (L > 0.0f) ? (flip * nz / L) : 0.0f;
    const int inside   = (q->orientation == GLU_INSIDE);
    const int none     = (q->normals == GLU_NONE);
    const GLfloat sgn  = inside ? -1.0f : 1.0f;
    GLint i, j;

    if (stacks > glu_MaxSlices()) stacks = glu_MaxSlices();

    if (style == GLU_POINT)
    {
        /* i < slices, not <=: the closing column would repeat theta = 0 and
         * draw every point of it twice (quad.c:310). One normal per column,
         * outside the j loop, as GLU has it (quad.c:311-319). */
        for (i = 0; i < slices; i++)
        {
            const GLfloat s  = (GLfloat)i / (GLfloat)slices;
            const GLfloat t  = GLU_TWOPI * s;
            const GLfloat st = (GLfloat)sin(t), ct = (GLfloat)cos(t);

            if (!none) glNormal3f(sgn * xy * st, sgn * xy * ct, sgn * zn);

            glBegin(GL_POINTS);
            for (j = 0; j <= stacks; j++)
            {
                const GLfloat f  = (GLfloat)j / (GLfloat)stacks;
                const GLfloat rr = r0 + (r1 - r0) * f;

                if (q->texture) glTexCoord2f(1.0f - s, f);
                glVertex3f(rr * st, rr * ct, h * f);
            }
            glEnd();
        }
        return;
    }

    /* GLU_LINE draws the interior rings and then FALLS THROUGH into the
     * silhouette (quad.c:363, its comment marking the fall-through as
     * deliberate), so a wireframe is the rings plus the outline. */
    if (style == GLU_LINE)
    {
        for (j = 1; j < stacks; j++)
        {
            const GLfloat f  = (GLfloat)j / (GLfloat)stacks;
            const GLfloat rr = r0 + (r1 - r0) * f;

            glBegin(GL_LINE_STRIP);
            for (i = 0; i <= slices; i++)
            {
                const GLfloat s  = (GLfloat)i / (GLfloat)slices;
                const GLfloat t  = GLU_TWOPI * s;
                const GLfloat st = (GLfloat)sin(t), ct = (GLfloat)cos(t);

                if (!none) glNormal3f(sgn * xy * st, sgn * xy * ct, sgn * zn);
                if (q->texture) glTexCoord2f(1.0f - s, f);
                glVertex3f(rr * st, rr * ct, h * f);
            }
            glEnd();
        }
    }

    /* The two end rings. GLU's `j += stacks` visits exactly 0 and stacks. */
    for (j = 0; j <= stacks; j += stacks)
    {
        const GLfloat f  = (GLfloat)j / (GLfloat)stacks;
        const GLfloat rr = r0 + (r1 - r0) * f;

        glBegin(GL_LINE_STRIP);
        for (i = 0; i <= slices; i++)
        {
            const GLfloat s  = (GLfloat)i / (GLfloat)slices;
            const GLfloat t  = GLU_TWOPI * s;
            const GLfloat st = (GLfloat)sin(t), ct = (GLfloat)cos(t);

            if (!none) glNormal3f(sgn * xy * st, sgn * xy * ct, sgn * zn);
            if (q->texture) glTexCoord2f(1.0f - s, f);
            glVertex3f(rr * st, rr * ct, h * f);
        }
        glEnd();
    }

    /* And one strip up each azimuth. GLU emits its normal with zNormal replaced
     * by 0 here (quad.c:395), a radial normal rather than the wall's -- its own
     * inconsistency, and followed rather than corrected, because a silhouette's
     * leftover normal is observable through sphere-map texgen. */
    for (i = 0; i < slices; i++)
    {
        const GLfloat s  = (GLfloat)i / (GLfloat)slices;
        const GLfloat t  = GLU_TWOPI * s;
        const GLfloat st = (GLfloat)sin(t), ct = (GLfloat)cos(t);

        if (!none) glNormal3f(sgn * st, sgn * ct, 0.0f);
        glBegin(GL_LINE_STRIP);
        for (j = 0; j <= stacks; j++)
        {
            const GLfloat f  = (GLfloat)j / (GLfloat)stacks;
            const GLfloat rr = r0 + (r1 - r0) * f;

            if (q->texture) glTexCoord2f(1.0f - s, f);
            glVertex3f(rr * st, rr * ct, h * f);
        }
        glEnd();
    }
}

void GLUCylinder(GLUquadricObj *q, GLdouble base, GLdouble top,
                 GLdouble height, GLint slices, GLint stacks)
{
    if (glu_NoQuadric(q)) return;

    /*
     * GLU's reject list (quad.c:172-176), MINUS its `height < 0.0` clause. GLU
     * draws NOTHING and reports GLU_INVALID_VALUE through the quadric's error
     * callback on any of these; there is no callback channel here, so the call
     * simply returns, which is the same visible behaviour minus the report.
     *
     * The dropped height clause is deliberate and load-bearing: a negative height
     * is an extension past GLU because a client application scene06 drives glutSolidCone's
     * height from a timer that starts below zero. glu_TessCone handles it.
     */
    if (slices < 2 || stacks < 1 || base < 0.0 || top < 0.0)
    {
        glu_QuadricError(q, GLU_INVALID_VALUE);
        return;
    }
    if (slices > glu_MaxSlices())
        slices = glu_MaxSlices();

    if (q->drawStyle == GLU_FILL)
        glu_TessCone(q, (GLfloat)base, (GLfloat)top, (GLfloat)height, slices, stacks);
    else
        glu_ConeWire(q, (GLfloat)base, (GLfloat)top, (GLfloat)height,
                     slices, stacks, q->drawStyle);
}

/*
 * A sphere about the origin. Stacks run pole to pole, slices around z. The
 * normal is the unit position, which is what makes a sphere the one quadric
 * whose smooth normals need no derivation.
 *
 * GLU's own texture convention is followed: s = 1 - i/slices around, and
 * t = 1 - j/stacks, which puts t = 1 at the +z pole and t = 0 at -z
 * (quad.c:984-985, 991-992).
 */
/*
 * GLU_POINT, GLU_LINE and GLU_SILHOUETTE for the sphere, from quad.c:1031-1151.
 * GLU_FILL stays inline in GLUSphere below.
 *
 * GLU_LINE AND GLU_SILHOUETTE ARE THE SAME FIGURE HERE. quad.c:1070-1071 puts
 * both labels on one block, unlike the cone where GLU_LINE adds interior rings
 * and falls through. A sphere has no boundary to outline -- every ring is
 * interior -- so there is nothing for a silhouette to reduce to.
 *
 * `stacks` is clamped for the same reason as in glu_ConeWire: a meridian strip
 * is stacks+1 vertices in ONE block, where GLU_FILL spends stacks on the number
 * of blocks instead. The pole fans do not appear in any wire style, so the
 * texture-dependent start/finish split that GLU_FILL needs has no counterpart.
 */
static void glu_SphereWire(const GLUquadricObj *q, GLfloat r,
                           GLint slices, GLint stacks, GLenum style)
{
    const int inside  = (q->orientation == GLU_INSIDE);
    const int none    = (q->normals == GLU_NONE);
    const GLfloat ns  = inside ? -1.0f : 1.0f;
    GLint i, j;

    (void)style;   /* LINE and SILHOUETTE are one figure; POINT returns early */

    if (stacks > glu_MaxSlices()) stacks = glu_MaxSlices();

    if (q->drawStyle == GLU_POINT)
    {
        /* Split per LATITUDE row, not per column: GLU's outer loop is j
         * (quad.c:1033) and keeping that order keeps the leftover normal GLU's.
         * i < slices, so the seam column is not drawn twice. */
        for (j = 0; j <= stacks; j++)
        {
            const GLfloat p  = GLU_PI * (GLfloat)j / (GLfloat)stacks;
            const GLfloat sp = (GLfloat)sin(p), cp = (GLfloat)cos(p);

            glBegin(GL_POINTS);
            for (i = 0; i < slices; i++)
            {
                const GLfloat s  = (GLfloat)i / (GLfloat)slices;
                const GLfloat t  = GLU_TWOPI * s;
                const GLfloat st = (GLfloat)sin(t), ct = (GLfloat)cos(t);

                if (!none) glNormal3f(ns * st * sp, ns * ct * sp, ns * cp);
                if (q->texture)
                    glTexCoord2f(1.0f - s, 1.0f - (GLfloat)j / (GLfloat)stacks);
                glVertex3f(r * st * sp, r * ct * sp, r * cp);
            }
            glEnd();
        }
        return;
    }

    /* The rings, j = 1..stacks-1: the poles are single points and GLU draws no
     * ring for them (quad.c:1072). */
    for (j = 1; j < stacks; j++)
    {
        const GLfloat p  = GLU_PI * (GLfloat)j / (GLfloat)stacks;
        const GLfloat sp = (GLfloat)sin(p), cp = (GLfloat)cos(p);
        const GLfloat tt = 1.0f - (GLfloat)j / (GLfloat)stacks;

        glBegin(GL_LINE_STRIP);
        for (i = 0; i <= slices; i++)
        {
            const GLfloat s  = (GLfloat)i / (GLfloat)slices;
            const GLfloat t  = GLU_TWOPI * s;
            const GLfloat st = (GLfloat)sin(t), ct = (GLfloat)cos(t);

            if (!none) glNormal3f(ns * st * sp, ns * ct * sp, ns * cp);
            if (q->texture) glTexCoord2f(1.0f - s, tt);
            glVertex3f(r * st * sp, r * ct * sp, r * cp);
        }
        glEnd();
    }

    /* And a meridian per azimuth, pole to pole. */
    for (i = 0; i < slices; i++)
    {
        const GLfloat s  = (GLfloat)i / (GLfloat)slices;
        const GLfloat t  = GLU_TWOPI * s;
        const GLfloat st = (GLfloat)sin(t), ct = (GLfloat)cos(t);

        glBegin(GL_LINE_STRIP);
        for (j = 0; j <= stacks; j++)
        {
            const GLfloat p  = GLU_PI * (GLfloat)j / (GLfloat)stacks;
            const GLfloat sp = (GLfloat)sin(p), cp = (GLfloat)cos(p);

            if (!none) glNormal3f(ns * st * sp, ns * ct * sp, ns * cp);
            if (q->texture)
                glTexCoord2f(1.0f - s, 1.0f - (GLfloat)j / (GLfloat)stacks);
            glVertex3f(r * st * sp, r * ct * sp, r * cp);
        }
        glEnd();
    }
}

void GLUSphere(GLUquadricObj *q, GLdouble radius, GLint slices, GLint stacks)
{
    const GLfloat r = (GLfloat)radius;
    int inside, none, flat;
    GLfloat ns;
    GLint i, k, j, start, finish;

    if (glu_NoQuadric(q)) return;

    /* GLU's reject list, quad.c:718-721. Note stacks == 1 is LEGAL to GLU and
     * draws its degenerate figure (start=1, finish=0, so no bands and two
     * collapsed fans); clamping it to 2 would silently promote it to a real
     * sphere, which GLU never does. */
    if (slices < 2 || stacks < 1 || radius < 0.0)
    {
        glu_QuadricError(q, GLU_INVALID_VALUE);
        return;
    }
    if (slices > glu_MaxSlices())
        slices = glu_MaxSlices();

    if (q->drawStyle != GLU_FILL)
    {
        glu_SphereWire(q, r, slices, stacks, q->drawStyle);
        return;
    }

    inside = (q->orientation == GLU_INSIDE);
    none   = (q->normals == GLU_NONE);
    flat   = (q->normals == GLU_FLAT);
    ns     = inside ? -1.0f : 1.0f;

    /*
     * THE POLE FANS ARE SKIPPED WHEN TEXTURING, which is GLU's structure and not
     * a shortcut. A GL_TRIANGLE_FAN has one apex vertex and therefore one apex
     * texcoord, but a sphere's pole needs a different s at every surrounding
     * column, so the apex cannot be respecified -- GLU says exactly this at
     * quad.c:746-749 and answers it by setting start = 0, finish = stacks
     * (quad.c:939-940) and running every stack as a strip, the polar ones
     * degenerating to a ring of coincident points.
     *
     * One consequence is deliberate and worth stating, because it looks like an
     * inconsistency: the leftover CURRENT NORMAL is texture-dependent. Untextured,
     * the last strip is j = stacks-2 and the normal is
     * (0, sin(pi(stacks-2)/stacks), cos(...)) -- (0, 0.588, -0.809) at stacks=10.
     * Textured, the last strip is j = stacks-1, giving
     * (0, sin(pi(stacks-1)/stacks), cos(...)) = (0, 0.309, -0.951). GLU has the
     * same split for the same reason, so this matches the reference rather than
     * diverging from it.
     */
    start  = q->texture ? 0 : 1;
    finish = q->texture ? stacks : stacks - 1;

    /*
     * GL_QUAD_STRIP per stack, and the stack range and emission order taken from
     * GLU rather than invented. Three things depend on getting this exactly
     * right, and only the first is about the picture:
     *
     *  - BLOCK LENGTH. 2*(slices+1) vertices per strip, where one block for the
     *    whole sphere is 4*slices*stacks and overflows the default 256-entry
     *    vertex buffer: 10x10 alone loses a third of the surface with nothing but
     *    a GL_OUT_OF_MEMORY to show for it.
     *
     *  - AXES. GLU builds a point as x = sin(lon)sin(lat), y = cos(lon)sin(lat),
     *    z = cos(lat) (quad.c, the sinCache2a/cosCache2a terms). Using cos for x
     *    and sin for y is a MIRROR of that, not a rotation, and shows up the
     *    moment anything textures the sphere.
     *
     *  - THE NORMAL LEFT CURRENT ON RETURN. GLU runs its strips over
     *    j = 1 .. stacks-2 (quad.c:806, start=1 finish=stacks-1) with the poles
     *    drawn separately, and within each column emits the j+1 ring then the j
     *    ring. Its columns run i = 0..slices inclusive, so the final emission is
     *    the j ring at longitude 2pi: (0, sin(pi(stacks-2)/stacks),
     *    cos(pi(stacks-2)/stacks)). An application that draws afterwards without
     *    setting its own normal inherits that, and sphere-map texgen reads it.
     */
    if (!q->texture)
    {
        /*
         * The two pole fans. Each traverses its ring in the direction that keeps
         * the winding counter-clockwise seen from OUTSIDE the solid, and GLU_INSIDE
         * reverses both -- the +z fan descends i for GLU_OUTSIDE and ascends for
         * GLU_INSIDE (quad.c:830 vs 852), the -z fan the other way round
         * (quad.c:894-913 vs 914-936). Every glNormal3f here is guarded, because
         * GLU_NONE must leave the caller's current normal completely untouched:
         * GLU reaches that through `default: break` in each of its switches, and
         * unguarded, these four calls would make a GLU_NONE sphere perturb
         * exactly the state this file exists to get right.
         */
        if (!none) glNormal3f(0.0f, 0.0f, ns);
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(0.0f, 0.0f, r);
        {
            const GLfloat p  = GLU_PI / (GLfloat)stacks;
            const GLfloat sp = (GLfloat)sin(p), cp = (GLfloat)cos(p);
            for (k = 0; k <= slices; k++)
            {
                const GLfloat t  = GLU_TWOPI * (GLfloat)(inside ? k : slices - k)
                                             / (GLfloat)slices;
                const GLfloat st = (GLfloat)sin(t), ct = (GLfloat)cos(t);
                if (!none) glNormal3f(ns * st * sp, ns * ct * sp, ns * cp);
                glVertex3f(r * st * sp, r * ct * sp, r * cp);
            }
        }
        glEnd();

        if (!none) glNormal3f(0.0f, 0.0f, -ns);
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(0.0f, 0.0f, -r);
        {
            const GLfloat p  = GLU_PI - GLU_PI / (GLfloat)stacks;
            const GLfloat sp = (GLfloat)sin(p), cp = (GLfloat)cos(p);
            for (k = 0; k <= slices; k++)
            {
                const GLfloat t  = GLU_TWOPI * (GLfloat)(inside ? slices - k : k)
                                             / (GLfloat)slices;
                const GLfloat st = (GLfloat)sin(t), ct = (GLfloat)cos(t);
                if (!none) glNormal3f(ns * st * sp, ns * ct * sp, ns * cp);
                glVertex3f(r * st * sp, r * ct * sp, r * cp);
            }
        }
        glEnd();
    }

    for (j = start; j < finish; j++)
    {
        const GLfloat p0 = GLU_PI * (GLfloat)j       / (GLfloat)stacks;
        const GLfloat p1 = GLU_PI * (GLfloat)(j + 1) / (GLfloat)stacks;
        const GLfloat sp0 = (GLfloat)sin(p0), cp0 = (GLfloat)cos(p0);
        const GLfloat sp1 = (GLfloat)sin(p1), cp1 = (GLfloat)cos(p1);
        /*
         * t RUNS 1 - j/stacks, NOT j/stacks. GLU puts t = 1 at the +z pole and
         * t = 0 at -z (quad.c:984-985, 991-992), and its j == 0 is the +z pole
         * (quad.c:752, `angle = PI * j / stacks`) exactly as this loop's p0 is.
         * The plain j/stacks form is equivalent only where j == 0 means the -z
         * pole, so with GLU's axes it inverts t. s is reversed for the same
         * reason, and the two MUST stay reversed together -- both reversed is a
         * 180-degree rotation of the image (determinant +1), while either one
         * alone is a MIRROR.
         */
        const GLfloat tt0 = 1.0f - (GLfloat)j       / (GLfloat)stacks;
        const GLfloat tt1 = 1.0f - (GLfloat)(j + 1) / (GLfloat)stacks;

        glBegin(GL_QUAD_STRIP);
        for (i = 0; i <= slices; i++)
        {
            const GLfloat s  = (GLfloat)i / (GLfloat)slices;
            const GLfloat t  = GLU_TWOPI * s;
            const GLfloat st = (GLfloat)sin(t), ct = (GLfloat)cos(t);

            /* For GLU_OUTSIDE the j+1 ring goes first and the j ring second, which
             * is GLU's order (quad.c:953-957 selects the j ring for the SECOND
             * glNormal3f of each column) and what leaves the j ring current when
             * the last column closes. GLU_INSIDE swaps the pair, reversing the
             * winding along with the negated normals. */
            /* GLU_FLAT: ONE normal per column, at the facet midpoint in both
             * angles (quad.c:771, :776), emitted as the second of the pair
             * (quad.c:1003-1007). i == slices wraps to 0; GLU reads uninitialised
             * cache there, so its own last column is undefined and this is not. */
            if (flat)
            {
                const GLint   im = (i == slices) ? 0 : i;
                const GLfloat ta = GLU_TWOPI * ((GLfloat)im - 0.5f) / (GLfloat)slices;
                const GLfloat pm = GLU_PI * ((GLfloat)j + 0.5f) / (GLfloat)stacks;
                const GLfloat sm = (GLfloat)sin(pm), cm = (GLfloat)cos(pm);
                const GLfloat sa = (GLfloat)sin(ta), cb = (GLfloat)cos(ta);
                const GLfloat fn = inside ? -1.0f : 1.0f;

                if (inside)
                {
                    if (q->texture) glTexCoord2f(1.0f - s, tt0);
                    glVertex3f(r * st * sp0, r * ct * sp0, r * cp0);
                    glNormal3f(fn * sa * sm, fn * cb * sm, fn * cm);
                    if (q->texture) glTexCoord2f(1.0f - s, tt1);
                    glVertex3f(r * st * sp1, r * ct * sp1, r * cp1);
                }
                else
                {
                    if (q->texture) glTexCoord2f(1.0f - s, tt1);
                    glVertex3f(r * st * sp1, r * ct * sp1, r * cp1);
                    glNormal3f(fn * sa * sm, fn * cb * sm, fn * cm);
                    if (q->texture) glTexCoord2f(1.0f - s, tt0);
                    glVertex3f(r * st * sp0, r * ct * sp0, r * cp0);
                }
            }
            else if (inside)
            {
                if (!none)      glNormal3f(-st * sp0, -ct * sp0, -cp0);
                if (q->texture) glTexCoord2f(1.0f - s, tt0);
                glVertex3f(r * st * sp0, r * ct * sp0, r * cp0);

                if (!none)      glNormal3f(-st * sp1, -ct * sp1, -cp1);
                if (q->texture) glTexCoord2f(1.0f - s, tt1);
                glVertex3f(r * st * sp1, r * ct * sp1, r * cp1);
            }
            else
            {
                if (!none)      glNormal3f(st * sp1, ct * sp1, cp1);
                if (q->texture) glTexCoord2f(1.0f - s, tt1);
                glVertex3f(r * st * sp1, r * ct * sp1, r * cp1);

                if (!none)      glNormal3f(st * sp0, ct * sp0, cp0);
                if (q->texture) glTexCoord2f(1.0f - s, tt0);
                glVertex3f(r * st * sp0, r * ct * sp0, r * cp0);
            }
        }
        glEnd();
    }
}

/*
 * A flat annulus in z = 0, from `inner` to `outer`, `loops` rings deep. The
 * normal is +z everywhere (-z for GLU_INSIDE), so GLU_SMOOTH and GLU_FLAT agree
 * by construction. GLU maps the texture so the disc's bounding square covers
 * [0,1]x[0,1].
 *
 * gluDisk is a one-line forward to gluPartialDisk(..., 0.0, 360.0) in GLU
 * (quad.c:427), and this follows that function's FILL path. Four properties of
 * it are load-bearing:
 *
 *  - ONE NORMAL FOR THE WHOLE SURFACE, emitted before anything is drawn
 *    (quad.c:484-496) and never respecified. So it is also what stays current on
 *    return, which is the one piece of state a caller can inherit.
 *
 *  - THE RING WALK RUNS OUTER TO INNER: radiusLow = outer - delta*(j/loops)
 *    (quad.c:538-539). Combined with GLU's x = r*sin / y = r*cos axes this winds
 *    counter-clockwise seen from +z. Reversing the walk WITHOUT swapping the axes
 *    reverses every facet instead: at inner=.5 outer=1 slices=4 all 16 facets
 *    come out inward, so a GL_CULL_FACE application loses the entire disc. The
 *    walk and the axes are one choice, not two.
 *
 *  - GL_QUAD_STRIP, NOT GL_QUADS. A strip ring is 2*(slices+1) vertices where
 *    four-vertex facets are 4*slices, and the default vertex buffer is 256
 *    entries with GLVertex4f dropping the rest, so the silent-loss threshold is
 *    slices = 127 rather than 65.
 *
 *  - innerRadius == 0 IS A SEPARATE CASE (quad.c:500-532): the centre is a
 *    GL_TRIANGLE_FAN, the ring loop stops one short at loops-1, and the fan's rim
 *    sits at outer - delta*((loops-1)/loops). Without it the innermost ring
 *    degenerates to zero width and the centre is simply missing.
 */
/*
 * GLU_POINT, GLU_LINE and GLU_SILHOUETTE for the disc, from quad.c:580-687.
 * GLU_FILL stays inline in GLUDisk below, and the single (0,0,+-1) normal is
 * already emitted by the caller before this runs -- the disc is the one quadric
 * whose normal does not vary, which is why GLU hoists it above its own style
 * switch (quad.c:484-496) rather than repeating it per vertex.
 *
 * TWO OF GLU'S BRANCHES CANNOT FIRE HERE, and that is worth stating rather than
 * leaving as dead-looking code. Both test gluPartialDisk's state, which this
 * driver does not implement: `sweepAngle < 360` guards the silhouette's two
 * radial edges (quad.c:651), and `slices2` is the partial sweep's own slice
 * count. A full disc has no radial boundary, so its silhouette is the two
 * circles and nothing else.
 *
 * `loops` is clamped for the same reason `stacks` is in the other two: a radial
 * strip is loops+1 vertices in ONE block.
 */
static void glu_DiskWire(const GLUquadricObj *q, GLfloat ri, GLfloat ro,
                         GLint slices, GLint loops, GLenum style)
{
    const GLfloat dr = ro - ri;
    GLint i, j;

    if (loops > glu_MaxSlices()) loops = glu_MaxSlices();

    if (style == GLU_POINT)
    {
        for (i = 0; i < slices; i++)
        {
            const GLfloat t  = GLU_TWOPI * (GLfloat)i / (GLfloat)slices;
            const GLfloat st = (GLfloat)sin(t), ct = (GLfloat)cos(t);

            glBegin(GL_POINTS);
            for (j = 0; j <= loops; j++)
            {
                const GLfloat rl = ro - dr * ((GLfloat)j / (GLfloat)loops);
                const GLfloat tl = (ro > 0.0f) ? (rl / ro * 0.5f) : 0.0f;

                if (q->texture) glTexCoord2f(tl * st + 0.5f, tl * ct + 0.5f);
                glVertex3f(rl * st, rl * ct, 0.0f);
            }
            glEnd();
        }
        return;
    }

    /* A ZERO-WIDTH ANNULUS IS ONE CIRCLE, in both wire styles. GLU special-cases
     * it twice over -- an early return for GLU_LINE (quad.c:600-613) and a break
     * after the first ring for GLU_SILHOUETTE (quad.c:685) -- because the two
     * boundary circles coincide and drawing both would double every segment. */
    if (style == GLU_LINE && ri != ro)
    {
        for (j = 0; j <= loops; j++)
        {
            const GLfloat rl = ro - dr * ((GLfloat)j / (GLfloat)loops);
            const GLfloat tl = (ro > 0.0f) ? (rl / ro * 0.5f) : 0.0f;

            glBegin(GL_LINE_STRIP);
            for (i = 0; i <= slices; i++)
            {
                const GLfloat t  = GLU_TWOPI * (GLfloat)i / (GLfloat)slices;
                const GLfloat st = (GLfloat)sin(t), ct = (GLfloat)cos(t);

                if (q->texture) glTexCoord2f(tl * st + 0.5f, tl * ct + 0.5f);
                glVertex3f(rl * st, rl * ct, 0.0f);
            }
            glEnd();
        }

        for (i = 0; i < slices; i++)
        {
            const GLfloat t  = GLU_TWOPI * (GLfloat)i / (GLfloat)slices;
            const GLfloat st = (GLfloat)sin(t), ct = (GLfloat)cos(t);

            glBegin(GL_LINE_STRIP);
            for (j = 0; j <= loops; j++)
            {
                const GLfloat rl = ro - dr * ((GLfloat)j / (GLfloat)loops);
                const GLfloat tl = (ro > 0.0f) ? (rl / ro * 0.5f) : 0.0f;

                if (q->texture) glTexCoord2f(tl * st + 0.5f, tl * ct + 0.5f);
                glVertex3f(rl * st, rl * ct, 0.0f);
            }
            glEnd();
        }
        return;
    }

    /* GLU_SILHOUETTE, and GLU_LINE's zero-width case: the boundary circles. */
    for (j = 0; j <= loops; j += loops)
    {
        const GLfloat rl = ro - dr * ((GLfloat)j / (GLfloat)loops);
        const GLfloat tl = (ro > 0.0f) ? (rl / ro * 0.5f) : 0.0f;

        glBegin(GL_LINE_STRIP);
        for (i = 0; i <= slices; i++)
        {
            const GLfloat t  = GLU_TWOPI * (GLfloat)i / (GLfloat)slices;
            const GLfloat st = (GLfloat)sin(t), ct = (GLfloat)cos(t);

            if (q->texture) glTexCoord2f(tl * st + 0.5f, tl * ct + 0.5f);
            glVertex3f(rl * st, rl * ct, 0.0f);
        }
        glEnd();

        if (ri == ro) break;
    }
}

void GLUDisk(GLUquadricObj *q, GLdouble inner, GLdouble outer,
             GLint slices, GLint loops)
{
    const GLfloat ri = (GLfloat)inner;
    const GLfloat ro = (GLfloat)outer;
    const GLfloat dr = ro - ri;
    int inside, none;
    GLint i, k, j, finish;

    if (glu_NoQuadric(q)) return;

    /*
     * GLU's reject list, quad.c:448-452 (gluDisk forwards to gluPartialDisk at
     * quad.c:427). The `inner > outer` clause is the one that matters visibly:
     * with the radii swapped, every facet's winding reverses while the declared
     * normal stays +z, so a GL_CULL_FACE application sees a disc that is entirely
     * back-facing. `outer <= 0` also removes the division guarded below.
     */
    if (slices < 2 || loops < 1 || outer <= 0.0 || inner < 0.0 || inner > outer)
    {
        glu_QuadricError(q, GLU_INVALID_VALUE);
        return;
    }
    if (slices > glu_MaxSlices())
        slices = glu_MaxSlices();

    inside = (q->orientation == GLU_INSIDE);
    none   = (q->normals == GLU_NONE);

    if (!none)
        glNormal3f(0.0f, 0.0f, inside ? -1.0f : 1.0f);

    if (q->drawStyle != GLU_FILL)
    {
        glu_DiskWire(q, ri, ro, slices, loops, q->drawStyle);
        return;
    }

    finish = (ri == 0.0f) ? (loops - 1) : loops;

    if (ri == 0.0f)
    {
        const GLfloat rl = ro - dr * ((GLfloat)(loops - 1) / (GLfloat)loops);
        const GLfloat tl = (ro > 0.0f) ? (rl / ro * 0.5f) : 0.0f;

        glBegin(GL_TRIANGLE_FAN);
        if (q->texture) glTexCoord2f(0.5f, 0.5f);
        glVertex3f(0.0f, 0.0f, 0.0f);
        /* i descends for GLU_OUTSIDE (quad.c:516) and ascends for GLU_INSIDE
         * (quad.c:525), which is what keeps the fan facing the way the single
         * normal above claims. */
        for (k = 0; k <= slices; k++)
        {
            const GLfloat t  = GLU_TWOPI * (GLfloat)(inside ? k : slices - k)
                                         / (GLfloat)slices;
            const GLfloat st = (GLfloat)sin(t), ct = (GLfloat)cos(t);
            if (q->texture) glTexCoord2f(tl * st + 0.5f, tl * ct + 0.5f);
            glVertex3f(rl * st, rl * ct, 0.0f);
        }
        glEnd();
    }

    for (j = 0; j < finish; j++)
    {
        const GLfloat rl = ro - dr * ((GLfloat)j       / (GLfloat)loops);
        const GLfloat rh = ro - dr * ((GLfloat)(j + 1) / (GLfloat)loops);
        const GLfloat tl = (ro > 0.0f) ? (rl / ro * 0.5f) : 0.0f;
        const GLfloat th = (ro > 0.0f) ? (rh / ro * 0.5f) : 0.0f;

        glBegin(GL_QUAD_STRIP);
        for (i = 0; i <= slices; i++)
        {
            const GLfloat t  = GLU_TWOPI * (GLfloat)i / (GLfloat)slices;
            const GLfloat st = (GLfloat)sin(t), ct = (GLfloat)cos(t);

            /* GLU_OUTSIDE emits the LOW (outer) radius then the HIGH (inner) one;
             * GLU_INSIDE reverses the pair (quad.c:547-575). The texcoord is
             * GLU's own 0.5 + r*unit/(2*outer), which reduces to
             * s = 0.5 + x/(2*outer), t = 0.5 + y/(2*outer) as a function of
             * position. */
            if (inside)
            {
                if (q->texture) glTexCoord2f(th * st + 0.5f, th * ct + 0.5f);
                glVertex3f(rh * st, rh * ct, 0.0f);
                if (q->texture) glTexCoord2f(tl * st + 0.5f, tl * ct + 0.5f);
                glVertex3f(rl * st, rl * ct, 0.0f);
            }
            else
            {
                if (q->texture) glTexCoord2f(tl * st + 0.5f, tl * ct + 0.5f);
                glVertex3f(rl * st, rl * ct, 0.0f);
                if (q->texture) glTexCoord2f(th * st + 0.5f, th * ct + 0.5f);
                glVertex3f(rh * st, rh * ct, 0.0f);
            }
        }
        glEnd();
    }
}



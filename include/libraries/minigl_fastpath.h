#ifndef LIBRARIES_MINIGL_FASTPATH_H
#define LIBRARIES_MINIGL_FASTPATH_H

#include <exec/types.h>
#include <mgl/gl.h>

/*
 * Backend-neutral direct vertex-buffer interface for minigl.library V3.
 *
 * mglBeginFastPath() replaces glBegin() for one primitive batch and
 * mglEndFastPath() replaces glEnd().  The helpers below are SDK inlines;
 * therefore the inner vertex loop performs no library calls.
 */

#define MGL_FASTPATH_VERSION 2UL
#define MGL_FASTPATH_OFFSET_NONE 0xffffffffUL

/* Attribute/capability bits. */
#define MGL_FASTPATH_POSITION      (1UL << 0)
#define MGL_FASTPATH_COLOR         (1UL << 1)
#define MGL_FASTPATH_TEXCOORD0     (1UL << 2)
#define MGL_FASTPATH_TEXCOORD1     (1UL << 3)
#define MGL_FASTPATH_TEXCOORD_Q    (1UL << 4)
#define MGL_FASTPATH_NORMAL_INDEX  (1UL << 5)
#define MGL_FASTPATH_NORMAL        (1UL << 6)

/* Reserved capability bits for future per-vertex extensions. */
#define MGL_FASTPATH_FOG           (1UL << 7)
#define MGL_FASTPATH_POINT_SIZE    (1UL << 8)

#define MGL_FASTPATH_CAPABILITIES_V2 \
    (MGL_FASTPATH_POSITION | MGL_FASTPATH_COLOR | \
     MGL_FASTPATH_TEXCOORD0 | MGL_FASTPATH_TEXCOORD1 | \
     MGL_FASTPATH_TEXCOORD_Q | MGL_FASTPATH_NORMAL_INDEX | \
     MGL_FASTPATH_NORMAL)

/* Implementation/detail flags returned by the library. */
#define MGL_FASTPATH_FLAG_DIRECT_BUFFER (1UL << 0)
#define MGL_FASTPATH_FLAG_CLASSIC       (1UL << 1)
#define MGL_FASTPATH_FLAG_PISTORM3D     (1UL << 2)

typedef struct MGLFastPath
{
    ULONG size;
    ULONG version;

    ULONG requestedAttributes;
    ULONG supportedAttributes;
    ULONG flags;
    GLenum primitive;

    UBYTE *vertexBuffer;
    UBYTE *currentVertex;
    ULONG vertexStride;
    ULONG vertexCount;
    ULONG vertexCapacity;
    ULONG firstVertex;

    ULONG offsetX, offsetY, offsetZ, offsetW;
    ULONG offsetR, offsetG, offsetB, offsetA;
    ULONG offsetU0, offsetV0;
    ULONG offsetU1, offsetV1;
    ULONG offsetQ;
    ULONG offsetNormalIndex;

    GLfloat *x, *y, *z, *w;
    GLfloat *r, *g, *b, *a;
    GLfloat *u0, *v0;
    GLfloat *u1, *v1;
    GLfloat *q;
    GLuint  *normalIndex;

    /* Direct normal-buffer extension. */
    UBYTE *normalBuffer;
    ULONG normalStride;
    ULONG normalCount;
    ULONG normalCapacity;
    ULONG offsetNormalX, offsetNormalY, offsetNormalZ;
    GLuint currentNormalIndex;

    /* Current attributes copied into each emitted vertex. */
    GLfloat currentColor[4];
    GLfloat currentTex0[2];
    GLfloat currentTex1[2];
    GLfloat currentQ;

    APTR privateData;
    ULONG reserved[8];
} MGLFastPath;

#define MGL_FASTPATH_INITIALIZER { sizeof(MGLFastPath), MGL_FASTPATH_VERSION }

/* Exported capability query; safe to call before creating a context. */
ULONG mglGetFastPathCapabilities(void);

#define __inline__ inline

static __inline__ GLboolean mglFastPathHasRoom(const MGLFastPath *fp)
{
    return (fp && fp->vertexCount < fp->vertexCapacity) ? GL_TRUE : GL_FALSE;
}

static __inline__ void mglFastPathRefreshPointers(MGLFastPath *fp)
{
    UBYTE *v;

    if (!fp || !fp->vertexBuffer || fp->vertexCount >= fp->vertexCapacity)
        return;

    v = fp->vertexBuffer + fp->vertexCount * fp->vertexStride;
    fp->currentVertex = v;

#define MGL_FP_PTR(field, offset, type) \
    fp->field = ((offset) == MGL_FASTPATH_OFFSET_NONE) ? (type *)0 : (type *)(v + (offset))

    MGL_FP_PTR(x, fp->offsetX, GLfloat);
    MGL_FP_PTR(y, fp->offsetY, GLfloat);
    MGL_FP_PTR(z, fp->offsetZ, GLfloat);
    MGL_FP_PTR(w, fp->offsetW, GLfloat);
    MGL_FP_PTR(r, fp->offsetR, GLfloat);
    MGL_FP_PTR(g, fp->offsetG, GLfloat);
    MGL_FP_PTR(b, fp->offsetB, GLfloat);
    MGL_FP_PTR(a, fp->offsetA, GLfloat);
    MGL_FP_PTR(u0, fp->offsetU0, GLfloat);
    MGL_FP_PTR(v0, fp->offsetV0, GLfloat);
    MGL_FP_PTR(u1, fp->offsetU1, GLfloat);
    MGL_FP_PTR(v1, fp->offsetV1, GLfloat);
    MGL_FP_PTR(q, fp->offsetQ, GLfloat);
    MGL_FP_PTR(normalIndex, fp->offsetNormalIndex, GLuint);
#undef MGL_FP_PTR
}

static __inline__ void mglFastPathColor4f(MGLFastPath *fp,
                                           GLfloat r, GLfloat g,
                                           GLfloat b, GLfloat a)
{
    fp->currentColor[0] = r; fp->currentColor[1] = g;
    fp->currentColor[2] = b; fp->currentColor[3] = a;
}

static __inline__ void mglFastPathColor3f(MGLFastPath *fp,
                                           GLfloat r, GLfloat g, GLfloat b)
{
    mglFastPathColor4f(fp, r, g, b, 1.0f);
}

static __inline__ void mglFastPathTexCoord2f(MGLFastPath *fp,
                                              GLfloat s, GLfloat t)
{
    fp->currentTex0[0] = s; fp->currentTex0[1] = t;
}

static __inline__ void mglFastPathMultiTexCoord2f(MGLFastPath *fp,
                                                   ULONG unit,
                                                   GLfloat s, GLfloat t)
{
    if (unit == 0) {
        fp->currentTex0[0] = s; fp->currentTex0[1] = t;
    } else if (unit == 1) {
        fp->currentTex1[0] = s; fp->currentTex1[1] = t;
    }
}

static __inline__ GLboolean mglFastPathNormal3f(MGLFastPath *fp,
                                                 GLfloat x, GLfloat y, GLfloat z)
{
    UBYTE *n;
    if (!fp || !(fp->supportedAttributes & MGL_FASTPATH_NORMAL) ||
        !fp->normalBuffer || fp->normalCount + 1 >= fp->normalCapacity)
        return GL_FALSE;

    ++fp->normalCount;
    n = fp->normalBuffer + fp->normalCount * fp->normalStride;
    *(GLfloat *)(n + fp->offsetNormalX) = x;
    *(GLfloat *)(n + fp->offsetNormalY) = y;
    *(GLfloat *)(n + fp->offsetNormalZ) = z;
    fp->currentNormalIndex = fp->normalCount;
    return GL_TRUE;
}

static __inline__ GLboolean mglAdvanceFastVertex(MGLFastPath *fp)
{
    if (!fp || fp->vertexCount >= fp->vertexCapacity)
        return GL_FALSE;

    if ((fp->requestedAttributes & MGL_FASTPATH_COLOR) && fp->r) {
        *fp->r = fp->currentColor[0]; *fp->g = fp->currentColor[1];
        *fp->b = fp->currentColor[2]; *fp->a = fp->currentColor[3];
    }
    if ((fp->requestedAttributes & MGL_FASTPATH_TEXCOORD0) && fp->u0) {
        *fp->u0 = fp->currentTex0[0]; *fp->v0 = fp->currentTex0[1];
    }
    if ((fp->requestedAttributes & MGL_FASTPATH_TEXCOORD1) && fp->u1) {
        *fp->u1 = fp->currentTex1[0]; *fp->v1 = fp->currentTex1[1];
    }
    if ((fp->requestedAttributes & MGL_FASTPATH_TEXCOORD_Q) && fp->q)
        *fp->q = fp->currentQ;
    if ((fp->requestedAttributes & MGL_FASTPATH_NORMAL_INDEX) && fp->normalIndex)
        *fp->normalIndex = fp->currentNormalIndex;

    ++fp->vertexCount;
    if (fp->vertexCount < fp->vertexCapacity)
        mglFastPathRefreshPointers(fp);
    else
        fp->currentVertex = (UBYTE *)0;
    return GL_TRUE;
}

static __inline__ GLboolean mglFastPathVertex3f(MGLFastPath *fp,
                                                 GLfloat x, GLfloat y, GLfloat z)
{
    if (!mglFastPathHasRoom(fp) || !fp->x || !fp->y || !fp->z)
        return GL_FALSE;
    *fp->x = x; *fp->y = y; *fp->z = z;
    if (fp->w) *fp->w = 1.0f;
    return mglAdvanceFastVertex(fp);
}

#endif /* LIBRARIES_MINIGL_FASTPATH_H */

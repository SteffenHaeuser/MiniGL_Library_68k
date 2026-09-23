#include <mgl/gl.h>
#include <mgl/context.h>

#include "fastpath_api.h"

/*
 * The backend's own current-context symbol is the single source of truth.
 * Both the PiStorm3D backend and the shared library are built with vbcc.
 */
extern GLcontext mini_CurrentContext;

static void fastpath_clear(MGLFastPath *fp)
{
    ULONG callerSize = fp->size;
    UBYTE *p = (UBYTE *)fp;
    ULONG i;
    ULONG clearSize = callerSize < sizeof(MGLFastPath)
                    ? callerSize
                    : sizeof(MGLFastPath);

    for (i = 0; i < clearSize; ++i)
        p[i] = 0;

    fp->size = callerSize;
    fp->version = MGL_FASTPATH_VERSION;
}

static void fastpath_set_vertex_offsets(MGLFastPath *fp)
{
    MGLVertex v;
    UBYTE *base = (UBYTE *)&v;

    /*
     * Do not use offsetof() here.  The vbcc version used for the AmigaOS
     * build does not accept the nested-member offsetof expressions used by
     * the GCC build.  Computing the byte offsets from a real local object is
     * equally valid for the ABI information exported through MGLFastPath.
     */
    fp->offsetX = (ULONG)((UBYTE *)&v.bx - base);
    fp->offsetY = (ULONG)((UBYTE *)&v.by - base);
    fp->offsetZ = (ULONG)((UBYTE *)&v.bz - base);
    fp->offsetW = (ULONG)((UBYTE *)&v.bw - base);

    fp->offsetR = (ULONG)((UBYTE *)&v.color.r - base);
    fp->offsetG = (ULONG)((UBYTE *)&v.color.g - base);
    fp->offsetB = (ULONG)((UBYTE *)&v.color.b - base);
    fp->offsetA = (ULONG)((UBYTE *)&v.color.a - base);

    fp->offsetU0 = (ULONG)((UBYTE *)&v.v.u0 - base);
    fp->offsetV0 = (ULONG)((UBYTE *)&v.v.v0 - base);
    fp->offsetU1 = (ULONG)((UBYTE *)&v.v.u1 - base);
    fp->offsetV1 = (ULONG)((UBYTE *)&v.v.v1 - base);

    fp->offsetQ = (ULONG)((UBYTE *)&v.q - base);
    fp->offsetNormalIndex = (ULONG)((UBYTE *)&v.normal - base);
}

static void fastpath_set_normal_offsets(MGLFastPath *fp)
{
    MGLNormal n;
    UBYTE *base = (UBYTE *)&n;

    fp->offsetNormalX = (ULONG)((UBYTE *)&n.x - base);
    fp->offsetNormalY = (ULONG)((UBYTE *)&n.y - base);
    fp->offsetNormalZ = (ULONG)((UBYTE *)&n.z - base);
}

ULONG Lib_mglGetFastPathCapabilities(void)
{
    return MGL_FASTPATH_CAPABILITIES_V2;
}

GLboolean Lib_mglBeginFastPath(GLenum primitive, ULONG attributes, MGLFastPath *fp)
{
    GLcontext context;
    ULONG supported = Lib_mglGetFastPathCapabilities();

    if (!fp || fp->size < sizeof(MGLFastPath))
        return GL_FALSE;

    context = mini_CurrentContext;
    if (!context)
        return GL_FALSE;

    if (!(attributes & MGL_FASTPATH_POSITION) || (attributes & ~supported))
        return GL_FALSE;

    fastpath_clear(fp);
    GLBegin(context, primitive);

    fp->requestedAttributes = attributes;
    fp->supportedAttributes = supported;
    fp->flags = MGL_FASTPATH_FLAG_DIRECT_BUFFER | MGL_FASTPATH_FLAG_PISTORM3D;
    fp->primitive = primitive;

    fp->vertexBuffer = (UBYTE *)context->VertexBuffer;
    fp->vertexStride = sizeof(MGLVertex);
    fp->firstVertex = context->VertexBufferPointer;
    fp->vertexCount = context->VertexBufferPointer;
    fp->vertexCapacity = context->VertexBufferSize;

    fastpath_set_vertex_offsets(fp);

    fp->normalBuffer = (UBYTE *)context->NormalBuffer;
    fp->normalStride = sizeof(MGLNormal);
    fp->normalCount = context->NormalBufferPointer;
    fp->normalCapacity = context->VertexBufferSize;

    fastpath_set_normal_offsets(fp);

    fp->currentNormalIndex = context->NormalBufferPointer;

    fp->currentColor[0] = context->CurrentColor.r;
    fp->currentColor[1] = context->CurrentColor.g;
    fp->currentColor[2] = context->CurrentColor.b;
    fp->currentColor[3] = context->CurrentColor.a;

    fp->currentTex0[0] = context->CurrentTexS;
    fp->currentTex0[1] = context->CurrentTexT;
    fp->currentTex1[0] = context->CurrentTexS;
    fp->currentTex1[1] = context->CurrentTexT;
    fp->currentQ = context->CurrentTexQ;

    fp->privateData = (APTR)context;
    mglFastPathRefreshPointers(fp);

    return GL_TRUE;
}

void Lib_mglEndFastPath(MGLFastPath *fp)
{
    GLcontext context;

    if (!fp ||
        fp->version != MGL_FASTPATH_VERSION ||
        !fp->privateData)
        return;

    context = (GLcontext)fp->privateData;

    if (fp->vertexCount > fp->vertexCapacity)
        fp->vertexCount = fp->vertexCapacity;

    if (fp->normalCount >= fp->normalCapacity) {
        if (fp->normalCapacity)
            fp->normalCount = fp->normalCapacity - 1;
        else
            fp->normalCount = 0;
    }

    context->VertexBufferPointer = fp->vertexCount;
    context->NormalBufferPointer = fp->normalCount;

    GLEnd(context);

    fp->privateData = (APTR)0;
    fp->currentVertex = (UBYTE *)0;
}

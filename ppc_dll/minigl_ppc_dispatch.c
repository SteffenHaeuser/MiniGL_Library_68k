#define MINIGL_LIBRARY_BUILD 1
#include <exec/types.h>
#include <libraries/minigl_dispatch.h>
#include "dll.h"

/* The bundled mglQ3 core owns mini_CurrentContext (context.c). */
extern GLcontext mini_CurrentContext;

/* Present in compatible MiniGL backends but not declared by every old header. */
extern void MGLLockMode(GLcontext context, GLenum lockMode);
extern void mglChooseNumberOfBuffers(int number);
extern void mglChooseVertexBufferSize(int size);
extern void mglChooseWindowMode(GLboolean flag);
extern void mglProposeCloseDesktop(GLboolean closeme);

/* API additions not implemented independently by the classic mglQ3 core. */
static void PPCStub_GLClientActiveTextureARB(GLcontext context, GLenum unit)
{
    (void)context;
    (void)unit;
}

/* MGLResizeContext is disabled in mglQ3 until fullscreen resource handling
 * is safe again. Keep the public slot but make it a no-op. */
static GLboolean PPCStub_MGLResizeContext(GLcontext context, GLsizei width, GLsizei height)
{
    (void)context;
    (void)width;
    (void)height;
    return GL_FALSE;
}

static void *PPC_MGLGetInputWindowHandle(GLcontext context)
{
    return MGLGetWindowHandle(context);
}

static void PPC_GLMultiDrawArrays(GLcontext context, GLenum mode,
    const GLint *first, const GLsizei *count, GLsizei primcount)
{
    GLsizei i;

    if (!first || !count || primcount <= 0)
        return;

    for (i = 0; i < primcount; ++i)
        if (count[i] > 0)
            GLDrawArrays(context, mode, first[i], count[i]);
}

/* V22 calls not implemented independently by the classic mglQ3 core. */
static void PPCStub_GLBlendEquation(GLcontext context, GLenum mode)
{
    (void)context;
    (void)mode;
}
static void PPC_GLBlendFuncSeparate(GLcontext context, GLenum srcRGB,
    GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha)
{
    (void)srcAlpha;
    (void)dstAlpha;
    GLBlendFunc(context, srcRGB, dstRGB);
}
static GLboolean PPCStub_GLIsTexture(GLcontext context, GLuint texture)
{
    (void)context;
    (void)texture;
    return GL_FALSE;
}
static void PPCStub_mglChooseZBufferDepth(int bits) { (void)bits; }

/* V27 Classic-compatible entry points; see CHANGES_V27_WOS.md. */
static void PPCStub_GLLineWidth(GLcontext context, GLfloat width)
{
    (void)context; (void)width;
}

/* Nor any glTexGenfv (2026-09-12, audit fix 7): Classic's texgen is sphere map
 * only and has no plane equations to set, so there is nothing to forward to.
 * Stubbed to keep the tables the same shape. */
static void PPCStub_GLTexGenfv(GLcontext context, GLenum coord, GLenum pname, const GLfloat *params)
{
    (void)context; (void)coord; (void)pname; (void)params;
}

/* Classic MiniGL has neither framebuffer-to-texture copy (2026-09-12,
 * audit fix 9), so both stub out and its textures stay upload-only. */
static void PPCStub_GLCopyTexImage2D(GLcontext context, GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border)
{
    (void)context; (void)target; (void)level; (void)internalformat;
    (void)x; (void)y; (void)width; (void)height; (void)border;
}

static void PPCStub_GLCopyTexSubImage2D(GLcontext context, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{
    (void)context; (void)target; (void)level; (void)xoffset; (void)yoffset;
    (void)x; (void)y; (void)width; (void)height;
}

/* These GL 1.1 slots use the supplied Classic V27 fallback semantics. */
static GLboolean PPCStub_GLAreTexturesResident(GLcontext context, GLsizei n, const GLuint *textures, GLboolean *residences)
{
    (void)context; (void)n; (void)textures; (void)residences;
    return GL_TRUE;
}

static void PPCStub_GLEdgeFlag(GLcontext context, GLboolean flag) { (void)context; (void)flag; }
static void PPCStub_GLEdgeFlagPointer(GLcontext context, GLsizei stride, const GLvoid *pointer) { (void)context; (void)stride; (void)pointer; }
static void PPCStub_GLEdgeFlagv(GLcontext context, const GLboolean *flag) { (void)context; (void)flag; }

static void PPCStub_GLGetDoublev(GLcontext context, GLenum pname, GLdouble *params)
{
    (void)context; (void)pname;
    if (params) params[0] = 0.0;
}

static void PPCStub_GLGetPointerv(GLcontext context, GLenum pname, GLvoid **params)
{
    (void)context; (void)pname;
    if (params) *params = (GLvoid *)0;
}

static void PPCStub_GLIndexi(GLcontext context, GLint c) { (void)context; (void)c; }
static void PPCStub_GLIndexiv(GLcontext context, const GLint *c) { (void)context; (void)c; }
static void PPCStub_GLIndexPointer(GLcontext context, GLenum type, GLsizei stride, const GLvoid *pointer) { (void)context; (void)type; (void)stride; (void)pointer; }
static void PPCStub_GLPrioritizeTextures(GLcontext context, GLsizei n, const GLuint *textures, const GLclampf *priorities) { (void)context; (void)n; (void)textures; (void)priorities; }
static void PPCStub_GLReadBuffer(GLcontext context, GLenum mode) { (void)context; (void)mode; }


#ifndef MINIGL_PPC_BACKEND_FLAGS
#define MINIGL_PPC_BACKEND_FLAGS MINIGL_BACKEND_FLAG_PPC
#endif

#include "minigl_v29_fallbacks.h"

static const MGLDispatchTable PPCDispatchTable = {
    MINIGL_DISPATCH_ABI_VERSION,
    (ULONG)sizeof(MGLDispatchTable),
    MINIGL_PPC_BACKEND_FLAGS,
    0,
    &mini_CurrentContext,
    GLActiveTextureARB,
    GLAlphaFunc,
    GLArrayElement,
    GLBegin,
    GLBindTexture,
    GLBlendFunc,
    GLClear,
    GLClearColor,
    GLClearDepth,
    GLColor3fv,
    GLColor3ubv,
    GLColor4f,
    GLColor4fv,
    GLColor4ub,
    GLColor4ubv,
    GLColorMask,
    GLColorPointer,
    GLColorTable,
    GLCullFace,
    GLDeleteTextures,
    GLDepthFunc,
    GLDepthMask,
    GLDepthRange,
    GLDisableClientState,
    GLDrawArrays,
    GLDrawBuffer,
    GLDrawElements,
    GLEnableClientState,
    GLEnd,
    GLFinish,
    GLFlush,
    GLFogf,
    GLFogfv,
    GLFrontFace,
    GLFrustum,
    GLGenTextures,
    GLGetBooleanv,
    GLGetError,
    GLGetFloatv,
    GLGetIntegerv,
    GLGetString,
    GLHint,
    GLIsEnabled,
    GLLoadIdentity,
    GLLoadMatrixd,
    GLLoadMatrixf,
    GLLockArrays,
    GLMatrixMode,
    GLMultMatrixd,
    GLMultMatrixf,
    GLMultiTexCoord2fARB,
    GLMultiTexCoord2fvARB,
    GLNormal3f,
    GLOrtho,
    GLPixelStorei,
    GLPointSize,
    GLPolygonMode,
    GLPopMatrix,
    GLPushMatrix,
    GLReadPixels,
    GLRotated,
    GLRotatef,
    GLRotatefEXT,
    GLRotatefEXTs,
    GLScaled,
    GLScalef,
    GLScissor,
    GLShadeModel,
    GLTexCoord2f,
    GLTexCoord2fv,
    GLTexCoord4f,
    GLTexCoord4fv,
    GLTexCoordPointer,
    GLTexEnvi,
    GLTexGeni,
    GLTexImage2D,
    GLTexParameteri,
    GLTexSubImage2D,
    GLTranslated,
    GLTranslatef,
    GLULookAt,
    GLUPerspective,
    GLUnlockArrays,
    GLVertex2fv,
    GLVertex3fv,
    GLVertex4f,
    GLVertex4fv,
    GLVertexPointer,
    GLViewport,
    MGLClearPointer,
    MGLCreateContext,
    MGLCreateContextFromID,
    MGLDeleteContext,
    MGLDrawMultitexBuffer,
    MGLEnableSync,
    MGLExit,
    PPC_MGLGetInputWindowHandle,
    MGLGetWindowHandle,
    MGLIdleFunc,
    MGLKeyFunc,
    MGLLockBack,
    MGLLockDisplay,
    MGLLockMode,
    MGLMainLoop,
    MGLMinTriArea,
    MGLMouseFunc,
    MGLPrintMatrix,
    MGLPrintMatrixStack,
    PPCStub_MGLResizeContext,
    MGLSetPointer,
    MGLSetState,
    MGLSetZOffset,
    MGLSpecialFunc,
    MGLSwitchDisplay,
    MGLTexMemStat,
    MGLUnlockDisplay,
    MGLWriteShotPPM,
    mglChooseGuardBand,
    mglChooseMtexBufferSize,
    mglChooseNumberOfBuffers,
    mglChoosePixelDepth,
    mglChooseTextureBufferSize,
    mglChooseVertexBufferSize,
    mglChooseWindowMode,
    mglGetSupportedScreenModes,
    mglProhibitAlphaFallback,
    mglProhibitMipMapping,
    mglProposeCloseDesktop,
    GLPolygonOffset,
    PPCStub_GLClientActiveTextureARB,
    GLInterleavedArrays,
    PPC_GLMultiDrawArrays,
    PPCStub_GLBlendEquation,
    PPC_GLBlendFuncSeparate,
    PPCStub_GLIsTexture,
    PPCStub_mglChooseZBufferDepth,
    /* V27 append-only tail. */
    PPCStub_GLLineWidth,
    PPCStub_GLTexGenfv,
    PPCStub_GLCopyTexImage2D,
    PPCStub_GLCopyTexSubImage2D,
    PPCStub_GLAreTexturesResident,
    PPCStub_GLEdgeFlag,
    PPCStub_GLEdgeFlagPointer,
    PPCStub_GLEdgeFlagv,
    PPCStub_GLGetDoublev,
    PPCStub_GLGetPointerv,
    PPCStub_GLIndexi,
    PPCStub_GLIndexiv,
    PPCStub_GLIndexPointer,
    PPCStub_GLPrioritizeTextures,
    PPCStub_GLReadBuffer,
    MGLCreateContextFromWindow,
    MGLCreateContextFromBitMap,
    /* V29 tail: slots 154-204; never insert into the old prefix. */
    PPCV29_GLLightf,
    PPCV29_GLLightfv,
    PPCV29_GLMaterialf,
    PPCV29_GLMaterialfv,
    PPCV29_GLLightModelf,
    PPCV29_GLLightModelfv,
    PPCV29_GLGetLightfv,
    PPCV29_GLGetMaterialfv,
    PPCV29_GLGenLists,
    PPCV29_GLDeleteLists,
    PPCV29_GLIsList,
    PPCV29_GLNewList,
    PPCV29_GLEndList,
    PPCV29_GLCallList,
    PPCV29_GLUBuild2DMipmaps,
    PPCV29_GLUErrorString,
    PPCV29_GLUNewQuadric,
    PPCV29_GLUDeleteQuadric,
    PPCV29_GLUQuadricNormals,
    PPCV29_GLUQuadricTexture,
    PPCV29_GLUQuadricDrawStyle,
    PPCV29_GLUQuadricOrientation,
    PPCV29_GLUCylinder,
    PPCV29_GLUSphere,
    PPCV29_GLUDisk,
    PPCV29_GLUTInit,
    PPCV29_GLUTInitDisplayMode,
    PPCV29_GLUTInitWindowSize,
    PPCV29_GLUTInitWindowPosition,
    PPCV29_GLUTCreateWindow,
    PPCV29_GLUTMainLoop,
    PPCV29_GLUTDisplayFunc,
    PPCV29_GLUTIdleFunc,
    PPCV29_GLUTKeyboardFunc,
    PPCV29_GLUTReshapeFunc,
    PPCV29_GLUTSwapBuffers,
    PPCV29_GLUTPostRedisplay,
    PPCV29_GLUTGet,
    PPCV29_GLUTGameModeString,
    PPCV29_GLUTEnterGameMode,
    PPCV29_GLUTLeaveGameMode,
    PPCV29_GLUTGameModeGet,
    PPCV29_GLUTSolidCube,
    PPCV29_GLUTSolidSphere,
    PPCV29_GLUTSolidCone,
    PPCV29_GLUTSolidTorus,
    PPCV29_GLUTSolidDodecahedron,
    PPCV29_GLNormalPointer,
    PPCV29_GLUQuadricCallback,
    PPCV29_GLColorMaterial,
    PPCV29_GLTexEnvfv,
    GLNormal3fv, /* 29.1: slot 205. */
};

/* The only MiniGL bootstrap symbol the WarpOS client resolves by name. */
const MGLDispatchTable * __saveds MGLGetDispatchTable(void)
{
    return &PPCDispatchTable;
}

/* Hyperion DLL runtime requires these three resource exports. */
void * __saveds dllFindResource(int type, char *name)
{
    (void)type; (void)name;
    return 0;
}
void * __saveds dllLoadResource(void *resource)
{
    (void)resource;
    return 0;
}
void __saveds dllFreeResource(void *resource)
{
    (void)resource;
}

/* Export table consumed by dllstartup.c. */
dll_tExportSymbol DLL_ExportSymbols[] = {
    { (void *)dllFindResource,        (char *)"dllFindResource" },
    { (void *)dllLoadResource,        (char *)"dllLoadResource" },
    { (void *)dllFreeResource,        (char *)"dllFreeResource" },
    { (void *)MGLGetDispatchTable,    (char *)"MGLGetDispatchTable" },
    { 0, 0 }
};

/* No implicit imports are required by the MiniGL PPC DLL itself. */
dll_tImportSymbol DLL_ImportSymbols[] = {
    { 0, 0, 0, 0 }
};

int __saveds DLL_Init(void)
{
    mini_CurrentContext = (GLcontext)0;
    return 1;
}

void __saveds DLL_DeInit(void)
{
    mini_CurrentContext = (GLcontext)0;
}

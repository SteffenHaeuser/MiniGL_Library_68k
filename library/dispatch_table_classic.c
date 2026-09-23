#define MINIGL_LIBRARY_BUILD 1
#include <exec/types.h>
#include <libraries/minigl_dispatch.h>
#include "context_access.h"

/* Present in backend archives but not declared by every historical header. */
extern void MGLLockMode(GLcontext context, GLenum lockMode);
extern void mglChooseNumberOfBuffers(int number);
extern void mglChooseVertexBufferSize(int size);
extern void mglChooseWindowMode(GLboolean flag);
extern void mglProposeCloseDesktop(GLboolean closeme);

/* Functions added by PiStorm3D but not present in Classic MiniGL. */
static void ClassicStub_GLColorMask(GLcontext context, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) { (void)0; }
static void ClassicStub_MGLClearPointer(GLcontext context) { (void)0; }
static void * ClassicStub_MGLGetInputWindowHandle(GLcontext context) { return (void *)0; }
static void ClassicStub_MGLSetPointer(GLcontext context) { (void)0; }
static void ClassicStub_mglChooseGuardBand(GLboolean flag) { (void)0; }
static void ClassicStub_mglChooseMtexBufferSize(int size) { (void)0; }
static void ClassicStub_mglChooseTextureBufferSize(int size) { (void)0; }
static GLint ClassicStub_mglGetSupportedScreenModes(MGLScreenModeCallback CallbackFn) { return 0; }
static void ClassicStub_mglProhibitAlphaFallback(GLboolean flag) { (void)0; }
static void ClassicStub_mglProhibitMipMapping(GLboolean flag) { (void)0; }

static void ClassicStub_GLPolygonOffset(GLcontext context, GLfloat factor, GLfloat units)
{
    (void)context; (void)factor; (void)units;
}

/* Classic MiniGL has no GLLineWidth to dispatch to -- the entry point is new
 * in MiniGLV3D (2026-09-12, audit fix 4). Stubbed so the slot exists and the
 * two tables stay the same shape; classic lines stay one pixel wide. */
static void ClassicStub_GLLineWidth(GLcontext context, GLfloat width)
{
    (void)context; (void)width;
}

/* Nor any glTexGenfv (2026-09-12, audit fix 7): Classic's texgen is sphere map
 * only and has no plane equations to set, so there is nothing to forward to.
 * Stubbed to keep the tables the same shape. */
static void ClassicStub_GLTexGenfv(GLcontext context, GLenum coord, GLenum pname, const GLfloat *params)
{
    (void)context; (void)coord; (void)pname; (void)params;
}

/* Classic MiniGL has neither framebuffer-to-texture copy (2026-09-12,
 * audit fix 9), so both stub out and its textures stay upload-only. */
static void ClassicStub_GLCopyTexImage2D(GLcontext context, GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border)
{
    (void)context; (void)target; (void)level; (void)internalformat;
    (void)x; (void)y; (void)width; (void)height; (void)border;
}

static void ClassicStub_GLCopyTexSubImage2D(GLcontext context, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{
    (void)context; (void)target; (void)level; (void)xoffset; (void)yoffset;
    (void)x; (void)y; (void)width; (void)height;
}

/* Nor the eleven entry points of GL 1.1 audit item 10 (2026-09-12). Classic's
 * own header, mgl_classic/gl.h, has the same shape MiniGLV3D's had: live
 * macros over commented-out prototypes, GLGetPointerv's alone left live.
 * The Classic library is not in this tree, so these follow its header and
 * stub out rather than name symbols its link may not have.
 *
 * The three that hand information back answer deterministically rather than
 * leaving the caller's memory as it was: no pointer, one zero, and
 * "resident". Classic cannot know residency, and saying resident asks the
 * caller to do nothing, where "not resident" could send it re-uploading its
 * textures. */
static GLboolean ClassicStub_GLAreTexturesResident(GLcontext context, GLsizei n, const GLuint *textures, GLboolean *residences)
{
    (void)context; (void)n; (void)textures; (void)residences;
    return GL_TRUE;
}

static void ClassicStub_GLEdgeFlag(GLcontext context, GLboolean flag) { (void)context; (void)flag; }
static void ClassicStub_GLEdgeFlagPointer(GLcontext context, GLsizei stride, const GLvoid *pointer) { (void)context; (void)stride; (void)pointer; }
static void ClassicStub_GLEdgeFlagv(GLcontext context, const GLboolean *flag) { (void)context; (void)flag; }

static void ClassicStub_GLGetDoublev(GLcontext context, GLenum pname, GLdouble *params)
{
    (void)context; (void)pname;
    if (params) params[0] = 0.0;
}

static void ClassicStub_GLGetPointerv(GLcontext context, GLenum pname, GLvoid **params)
{
    (void)context; (void)pname;
    if (params) *params = (GLvoid *)0;
}

static void ClassicStub_GLIndexi(GLcontext context, GLint c) { (void)context; (void)c; }
static void ClassicStub_GLIndexiv(GLcontext context, const GLint *c) { (void)context; (void)c; }
static void ClassicStub_GLIndexPointer(GLcontext context, GLenum type, GLsizei stride, const GLvoid *pointer) { (void)context; (void)type; (void)stride; (void)pointer; }
static void ClassicStub_GLPrioritizeTextures(GLcontext context, GLsizei n, const GLuint *textures, const GLclampf *priorities) { (void)context; (void)n; (void)textures; (void)priorities; }
static void ClassicStub_GLReadBuffer(GLcontext context, GLenum mode) { (void)context; (void)mode; }
/* Classic MiniGL has no way to render into a caller-supplied window: it owns
 * its own Warp3D display. Answer NULL so a host can detect the refusal and fall
 * back, rather than getting a context that draws nowhere. (2026-09-20) */
static void * ClassicStub_MGLCreateContextFromWindow(struct Window *window) { (void)window; return NULL; }
static void * ClassicStub_MGLCreateContextFromBitMap(struct BitMap *bitmap) { (void)bitmap; return NULL; }

/* Appended 2026-09-08. Classic MiniGL has GLInterleavedArrays but none of the
 * other three (no ARB multitexture client state, no glIsTexture, and
 * mglChooseZBufferDepth is a MiniGLV3D-only API), so they are stubbed. */
static void ClassicStub_GLClientActiveTextureARB(GLcontext context, GLenum unit) { (void)context; (void)unit; }
static void ClassicStub_GLMultiDrawArrays(GLcontext context, GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount) { (void)context; (void)mode; (void)first; (void)count; (void)primcount; }
static GLboolean ClassicStub_GLIsTexture(GLcontext context, GLuint texture) { (void)context; (void)texture; return GL_FALSE; }
static void ClassicStub_mglChooseZBufferDepth(int bits) { (void)bits; }

/* Appended 2026-09-08. Classic MiniGL has neither glBlendEquation nor
 * glBlendFuncSeparate (verified against mgl_classic/gl.h), so both stub out.
 * Silently ignoring them degrades to plain ADD blending with the colour
 * factors on both channels -- which is exactly what Classic already does. */
static void ClassicStub_GLBlendEquation(GLcontext context, GLenum mode) { (void)context; (void)mode; }
static void ClassicStub_GLBlendFuncSeparate(GLcontext context, GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha)
{
    (void)srcAlpha; (void)dstAlpha;
    /* Best effort: apply the RGB pair through the API Classic does have,
     * rather than dropping the call entirely. */
    GLBlendFunc(context, srcRGB, dstRGB);
}

const MGLDispatchTable MiniGLDispatchTable = {
    MINIGL_DISPATCH_ABI_VERSION,
    (ULONG)sizeof(MGLDispatchTable),
    MINIGL_BACKEND_FLAG_CLASSIC,
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
    ClassicStub_GLColorMask,
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
    ClassicStub_MGLClearPointer,
    MGLCreateContext,
    MGLCreateContextFromID,
    MGLDeleteContext,
    MGLDrawMultitexBuffer,
    MGLEnableSync,
    MGLExit,
    ClassicStub_MGLGetInputWindowHandle,
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
    MGLResizeContext,
    ClassicStub_MGLSetPointer,
    MGLSetState,
    MGLSetZOffset,
    MGLSpecialFunc,
    MGLSwitchDisplay,
    MGLTexMemStat,
    MGLUnlockDisplay,
    MGLWriteShotPPM,
    ClassicStub_mglChooseGuardBand,
    ClassicStub_mglChooseMtexBufferSize,
    mglChooseNumberOfBuffers,
    mglChoosePixelDepth,
    ClassicStub_mglChooseTextureBufferSize,
    mglChooseVertexBufferSize,
    mglChooseWindowMode,
    ClassicStub_mglGetSupportedScreenModes,
    ClassicStub_mglProhibitAlphaFallback,
    ClassicStub_mglProhibitMipMapping,
    mglProposeCloseDesktop,
    ClassicStub_GLPolygonOffset,
    /* --- Published V23 order (2026-09-11), same as the struct (positional init). --- */
    ClassicStub_GLClientActiveTextureARB,
    GLInterleavedArrays,
    ClassicStub_GLMultiDrawArrays,
    ClassicStub_GLBlendEquation,
    ClassicStub_GLBlendFuncSeparate,
    ClassicStub_GLIsTexture,
    ClassicStub_mglChooseZBufferDepth,
    /* --- APPENDED 2026-09-12 (audit fix 4), matching the struct. --- */
    ClassicStub_GLLineWidth,
    /* --- APPENDED 2026-09-12 (audit fix 7). --- */
    ClassicStub_GLTexGenfv,
    /* --- APPENDED 2026-09-12 (audit fix 9), 139 and 140. --- */
    ClassicStub_GLCopyTexImage2D,
    ClassicStub_GLCopyTexSubImage2D,
    /* --- APPENDED 2026-09-12 (audit item 10), 141 to 151. --- */
    ClassicStub_GLAreTexturesResident,
    ClassicStub_GLEdgeFlag,
    ClassicStub_GLEdgeFlagPointer,
    ClassicStub_GLEdgeFlagv,
    ClassicStub_GLGetDoublev,
    ClassicStub_GLGetPointerv,
    ClassicStub_GLIndexi,
    ClassicStub_GLIndexiv,
    ClassicStub_GLIndexPointer,
    ClassicStub_GLPrioritizeTextures,
    ClassicStub_GLReadBuffer,
    ClassicStub_MGLCreateContextFromWindow,
    ClassicStub_MGLCreateContextFromBitMap,
};

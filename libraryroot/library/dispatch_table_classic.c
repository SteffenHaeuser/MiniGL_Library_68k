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

/* The Classic Q3 backend declares MGLResizeContext, but its implementation in
 * context.c is disabled with #if 0 and therefore exports no such symbol. */
static GLboolean ClassicStub_MGLResizeContext(GLcontext context, GLsizei width, GLsizei height)
{
    (void)context; (void)width; (void)height;
    return GL_FALSE; /* V29 reports that no resize took place. */
}

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

/* New unsupported core GL entry points report failure without changing state.
 * Do not use GLFlagError: Classic's release config compiles that macro out. */
static void ClassicUnsupported(GLcontext context)
{
    if (context && context->CurrentError == GL_NO_ERROR)
        context->CurrentError = GL_INVALID_OPERATION;
}

/* Stub bodies for the 47 entries appended for 28.0. Base MiniGL has no
 * lighting, no display lists, no GLU quadrics and no GLUT at all. */
static void ClassicStub_GLLightf(GLcontext context, GLenum light, GLenum pname, GLfloat param) { (void)context; (void)light; (void)pname; (void)param;  ClassicUnsupported(context); }
static void ClassicStub_GLLightfv(GLcontext context, GLenum light, GLenum pname, const GLfloat *params) { (void)context; (void)light; (void)pname; (void)params;  ClassicUnsupported(context); }
static void ClassicStub_GLMaterialf(GLcontext context, GLenum face, GLenum pname, GLfloat param) { (void)context; (void)face; (void)pname; (void)param;  ClassicUnsupported(context); }
static void ClassicStub_GLMaterialfv(GLcontext context, GLenum face, GLenum pname, const GLfloat *params) { (void)context; (void)face; (void)pname; (void)params;  ClassicUnsupported(context); }
static void ClassicStub_GLLightModelf(GLcontext context, GLenum pname, GLfloat param) { (void)context; (void)pname; (void)param;  ClassicUnsupported(context); }
static void ClassicStub_GLLightModelfv(GLcontext context, GLenum pname, const GLfloat *params) { (void)context; (void)pname; (void)params;  ClassicUnsupported(context); }
static void ClassicStub_GLGetLightfv(GLcontext context, GLenum light, GLenum pname, GLfloat *params) { (void)context; (void)light; (void)pname; (void)params;  ClassicUnsupported(context); }
static void ClassicStub_GLGetMaterialfv(GLcontext context, GLenum face, GLenum pname, GLfloat *params) { (void)context; (void)face; (void)pname; (void)params;  ClassicUnsupported(context); }
static GLuint ClassicStub_GLGenLists(GLcontext context, GLsizei range) { (void)context; (void)range; return 0; }
static void ClassicStub_GLDeleteLists(GLcontext context, GLuint list, GLsizei range) { (void)context; (void)list; (void)range; }
static GLboolean ClassicStub_GLIsList(GLcontext context, GLuint list) { (void)context; (void)list; return GL_FALSE; }
static void ClassicStub_GLNewList(GLcontext context, GLuint list, GLenum mode) { (void)context; (void)list; (void)mode;  ClassicUnsupported(context); }
static void ClassicStub_GLEndList(GLcontext context) { (void)context;  ClassicUnsupported(context); }
static void ClassicStub_GLCallList(GLcontext context, GLuint list) { (void)context; (void)list;  ClassicUnsupported(context); }
static GLint ClassicStub_GLUBuild2DMipmaps(GLcontext context, GLenum target, GLint internalFormat, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *data) { (void)context; (void)target; (void)internalFormat; (void)width; (void)height; (void)format; (void)type; (void)data; return GLU_INVALID_OPERATION; }
/* GLU error text does not require a renderer. */
static const GLubyte *Classic_GLUErrorString(GLenum errCode)
{
    switch (errCode) {
    case GL_NO_ERROR: return (const GLubyte *)"no error";
    case GL_INVALID_ENUM: case GLU_INVALID_ENUM: return (const GLubyte *)"invalid enum";
    case GL_INVALID_VALUE: case GLU_INVALID_VALUE: return (const GLubyte *)"invalid value";
    case GL_INVALID_OPERATION: case GLU_INVALID_OPERATION: return (const GLubyte *)"invalid operation";
    case GL_OUT_OF_MEMORY: case GLU_OUT_OF_MEMORY: return (const GLubyte *)"out of memory";
    case GL_STACK_OVERFLOW: return (const GLubyte *)"stack overflow";
    case GL_STACK_UNDERFLOW: return (const GLubyte *)"stack underflow";
    case GLU_INCOMPATIBLE_GL_VERSION: return (const GLubyte *)"incompatible GL version";
    default: return (const GLubyte *)0;
    }
}
static GLUquadricObj * ClassicStub_GLUNewQuadric(void) { return NULL; }
static void ClassicStub_GLUDeleteQuadric(GLUquadricObj *q) { (void)q; }
static void ClassicStub_GLUQuadricNormals(GLUquadricObj *q, GLenum normals) { (void)q; (void)normals; }
static void ClassicStub_GLUQuadricTexture(GLUquadricObj *q, GLboolean textureCoords) { (void)q; (void)textureCoords; }
static void ClassicStub_GLUQuadricDrawStyle(GLUquadricObj *q, GLenum drawStyle) { (void)q; (void)drawStyle; }
static void ClassicStub_GLUQuadricOrientation(GLUquadricObj *q, GLenum orientation) { (void)q; (void)orientation; }
static void ClassicStub_GLUCylinder(GLUquadricObj *q, GLdouble base, GLdouble top, GLdouble height, GLint slices, GLint stacks) { (void)q; (void)base; (void)top; (void)height; (void)slices; (void)stacks; }
static void ClassicStub_GLUSphere(GLUquadricObj *q, GLdouble radius, GLint slices, GLint stacks) { (void)q; (void)radius; (void)slices; (void)stacks; }
static void ClassicStub_GLUDisk(GLUquadricObj *q, GLdouble inner, GLdouble outer, GLint slices, GLint loops) { (void)q; (void)inner; (void)outer; (void)slices; (void)loops; }
static void ClassicStub_GLUTInit(int *argcp, char **argv) { (void)argcp; (void)argv; }
static void ClassicStub_GLUTInitDisplayMode(unsigned int mode) { (void)mode; }
static void ClassicStub_GLUTInitWindowSize(int width, int height) { (void)width; (void)height; }
static void ClassicStub_GLUTInitWindowPosition(int x, int y) { (void)x; (void)y; }
static int ClassicStub_GLUTCreateWindow(const char *title) { (void)title; return 0; }
static void ClassicStub_GLUTMainLoop(void) { }
static void ClassicStub_GLUTDisplayFunc(void (*func)(void)) { (void)func; }
static void ClassicStub_GLUTIdleFunc(void (*func)(void)) { (void)func; }
static void ClassicStub_GLUTKeyboardFunc(void (*func)(unsigned char key, int x, int y)) { (void)func; }
static void ClassicStub_GLUTReshapeFunc(void (*func)(int width, int height)) { (void)func; }
static void ClassicStub_GLUTSwapBuffers(void) { }
static void ClassicStub_GLUTPostRedisplay(void) { }
static int ClassicStub_GLUTGet(GLenum state) { (void)state; return 0; }
static void ClassicStub_GLUTGameModeString(const char *string) { (void)string; }
static int ClassicStub_GLUTEnterGameMode(void) { return 0; }
static void ClassicStub_GLUTLeaveGameMode(void) { }
static int ClassicStub_GLUTGameModeGet(GLenum query) { (void)query; return 0; }
static void ClassicStub_GLUTSolidCube(GLdouble size) { (void)size; }
static void ClassicStub_GLUTSolidSphere(GLdouble radius, GLint slices, GLint stacks) { (void)radius; (void)slices; (void)stacks; }
static void ClassicStub_GLUTSolidCone(GLdouble base, GLdouble height, GLint slices, GLint stacks) { (void)base; (void)height; (void)slices; (void)stacks; }
static void ClassicStub_GLUTSolidTorus(GLdouble innerRadius, GLdouble outerRadius, GLint sides, GLint rings) { (void)innerRadius; (void)outerRadius; (void)sides; (void)rings; }
static void ClassicStub_GLUTSolidDodecahedron(void) { }
static void ClassicStub_GLNormalPointer(GLcontext context, GLenum type, GLsizei stride, const GLvoid *pointer) { (void)type; (void)stride; (void)pointer; ClassicUnsupported(context); }
static void ClassicStub_GLUQuadricCallback(GLUquadricObj *q, GLenum which, MGLUfuncptr fn) { (void)q; (void)which; (void)fn; }
static void ClassicStub_GLColorMaterial(GLcontext context, GLenum face, GLenum mode) { (void)context; (void)face; (void)mode;  ClassicUnsupported(context); }
static void ClassicStub_GLTexEnvfv(GLcontext context, GLenum target, GLenum pname, const GLfloat *params)
{
    if (!context) return;
    if (!params) { ClassicUnsupported(context); return; }
    if (target != GL_TEXTURE_ENV || pname != GL_TEXTURE_ENV_MODE) {
        if (context->CurrentError == GL_NO_ERROR) context->CurrentError = GL_INVALID_ENUM;
        return;
    }
    /* Classic's Warp3D TexEnvi supports MODULATE, DECAL and REPLACE. */
    if (params[0] != GL_MODULATE && params[0] != GL_DECAL && params[0] != GL_REPLACE) {
        if (context->CurrentError == GL_NO_ERROR) context->CurrentError = GL_INVALID_ENUM;
        return;
    }
    GLTexEnvi(context, target, pname, (GLint)params[0]);
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
    ClassicStub_MGLResizeContext,
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
    MGLCreateContextFromWindow,
    MGLCreateContextFromBitMap,
    /* core GL -- fixed-function lighting */
    ClassicStub_GLLightf,
    ClassicStub_GLLightfv,
    ClassicStub_GLMaterialf,
    ClassicStub_GLMaterialfv,
    ClassicStub_GLLightModelf,
    ClassicStub_GLLightModelfv,
    ClassicStub_GLGetLightfv,
    ClassicStub_GLGetMaterialfv,

    /* core GL -- display lists */
    ClassicStub_GLGenLists,
    ClassicStub_GLDeleteLists,
    ClassicStub_GLIsList,
    ClassicStub_GLNewList,
    ClassicStub_GLEndList,
    ClassicStub_GLCallList,

    /* GLU */
    ClassicStub_GLUBuild2DMipmaps,
    Classic_GLUErrorString,
    ClassicStub_GLUNewQuadric,
    ClassicStub_GLUDeleteQuadric,
    ClassicStub_GLUQuadricNormals,
    ClassicStub_GLUQuadricTexture,
    ClassicStub_GLUQuadricDrawStyle,
    ClassicStub_GLUQuadricOrientation,
    ClassicStub_GLUCylinder,
    ClassicStub_GLUSphere,
    ClassicStub_GLUDisk,

    /* GLUT */
    ClassicStub_GLUTInit,
    ClassicStub_GLUTInitDisplayMode,
    ClassicStub_GLUTInitWindowSize,
    ClassicStub_GLUTInitWindowPosition,
    ClassicStub_GLUTCreateWindow,
    ClassicStub_GLUTMainLoop,
    ClassicStub_GLUTDisplayFunc,
    ClassicStub_GLUTIdleFunc,
    ClassicStub_GLUTKeyboardFunc,
    ClassicStub_GLUTReshapeFunc,
    ClassicStub_GLUTSwapBuffers,
    ClassicStub_GLUTPostRedisplay,
    ClassicStub_GLUTGet,
    ClassicStub_GLUTGameModeString,
    ClassicStub_GLUTEnterGameMode,
    ClassicStub_GLUTLeaveGameMode,
    ClassicStub_GLUTGameModeGet,
    ClassicStub_GLUTSolidCube,
    ClassicStub_GLUTSolidSphere,
    ClassicStub_GLUTSolidCone,
    ClassicStub_GLUTSolidTorus,
    ClassicStub_GLUTSolidDodecahedron,
    ClassicStub_GLNormalPointer,
    ClassicStub_GLUQuadricCallback,
    ClassicStub_GLColorMaterial,
    ClassicStub_GLTexEnvfv,
    GLNormal3fv, /* 29.1: append-only slot 205. */
};

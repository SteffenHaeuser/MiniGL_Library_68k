#define MINIGL_LIBRARY_BUILD 1
#include <exec/types.h>
#include <stdio.h>
#include <libraries/minigl_dispatch.h>
#include "dll.h"

GLcontext mini_CurrentContext = (GLcontext)0;

static struct GLcontext_t fake_context_storage;

static void stub_call(const char *name)
{
    printf("[minigl.ppc.stub] %s\n", name);
}

void Stub_GLActiveTextureARB(GLcontext context, GLenum unit) { stub_call("GLActiveTextureARB"); }
void Stub_GLAlphaFunc(GLcontext context, GLenum func, GLclampf ref) { stub_call("GLAlphaFunc"); }
void Stub_GLArrayElement(GLcontext context, GLint i) { stub_call("GLArrayElement"); }
void Stub_GLBegin(GLcontext context, GLenum mode) { stub_call("GLBegin"); }
void Stub_GLBindTexture(GLcontext context, GLenum target, GLuint texture) { stub_call("GLBindTexture"); }
void Stub_GLBlendFunc(GLcontext context, GLenum sfactor, GLenum dfactor) { stub_call("GLBlendFunc"); }
void Stub_GLClear(GLcontext context, GLbitfield mask) { stub_call("GLClear"); }
void Stub_GLClearColor(GLcontext context, GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) { stub_call("GLClearColor"); }
void Stub_GLClearDepth(GLcontext context, GLclampd depth) { stub_call("GLClearDepth"); }
void Stub_GLColor3fv(GLcontext context, GLfloat *v) { stub_call("GLColor3fv"); }
void Stub_GLColor3ubv(GLcontext context, GLubyte *v) { stub_call("GLColor3ubv"); }
void Stub_GLColor4f(GLcontext context, GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) { stub_call("GLColor4f"); }
void Stub_GLColor4fv(GLcontext context, GLfloat *v) { stub_call("GLColor4fv"); }
void Stub_GLColor4ub(GLcontext context, GLubyte red, GLubyte green, GLubyte blue, GLubyte alhpa) { stub_call("GLColor4ub"); }
void Stub_GLColor4ubv(GLcontext context, GLubyte *v) { stub_call("GLColor4ubv"); }
void Stub_GLColorMask(GLcontext context, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) { stub_call("GLColorMask"); }
void Stub_GLColorPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) { stub_call("GLColorPointer"); }
void Stub_GLColorTable(GLcontext context, GLenum target, GLenum internalformat, GLint width, GLenum format, GLenum type, GLvoid *data) { stub_call("GLColorTable"); }
void Stub_GLCullFace(GLcontext context, GLenum mode) { stub_call("GLCullFace"); }
void Stub_GLDeleteTextures(GLcontext context, GLsizei n, const GLuint *textures) { stub_call("GLDeleteTextures"); }
void Stub_GLDepthFunc(GLcontext context, GLenum func) { stub_call("GLDepthFunc"); }
void Stub_GLDepthMask(GLcontext context, GLboolean flag) { stub_call("GLDepthMask"); }
void Stub_GLDepthRange(GLcontext context, GLclampd n, GLclampd f) { stub_call("GLDepthRange"); }
void Stub_GLDisableClientState(GLcontext context, GLenum cap) { stub_call("GLDisableClientState"); }
void Stub_GLDrawArrays(GLcontext context, GLenum mode, GLint first, GLsizei count) { stub_call("GLDrawArrays"); }
void Stub_GLDrawBuffer(GLcontext context, GLenum mode) { stub_call("GLDrawBuffer"); }
void Stub_GLDrawElements(GLcontext context, GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) { stub_call("GLDrawElements"); }
void Stub_GLEnableClientState(GLcontext context, GLenum cap) { stub_call("GLEnableClientState"); }
void Stub_GLEnd(GLcontext context) { stub_call("GLEnd"); }
void Stub_GLFinish(GLcontext context) { stub_call("GLFinish"); }
void Stub_GLFlush(GLcontext context) { stub_call("GLFlush"); }
void Stub_GLFogf(GLcontext context, GLenum pname, GLfloat param) { stub_call("GLFogf"); }
void Stub_GLFogfv(GLcontext context, GLenum pname, GLfloat *param) { stub_call("GLFogfv"); }
void Stub_GLFrontFace(GLcontext context, GLenum mode) { stub_call("GLFrontFace"); }
void Stub_GLFrustum(GLcontext context, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar) { stub_call("GLFrustum"); }
void Stub_GLGenTextures(GLcontext context, GLsizei n, GLuint *textures) { stub_call("GLGenTextures"); }
void Stub_GLGetBooleanv(GLcontext context, GLenum pname, GLboolean *params) { stub_call("GLGetBooleanv"); }
GLenum Stub_GLGetError(GLcontext context) { GLenum error = context ? context->CurrentError : GL_NO_ERROR; stub_call("GLGetError"); if (context) context->CurrentError = GL_NO_ERROR; return error; }
void Stub_GLGetFloatv(GLcontext context, GLenum pname, GLfloat *params) { stub_call("GLGetFloatv"); }
void Stub_GLGetIntegerv(GLcontext context, GLenum pname, GLint *params) { stub_call("GLGetIntegerv"); }
const GLubyte* Stub_GLGetString(GLcontext context, GLenum name) { static const GLubyte text[]="MiniGL Direct Dispatch Stub"; stub_call("GLGetString"); return text; }
void Stub_GLHint(GLcontext context, GLenum target, GLenum mode) { stub_call("GLHint"); }
GLboolean Stub_GLIsEnabled(GLcontext context, GLenum cap) { stub_call("GLIsEnabled"); return GL_TRUE; }
void Stub_GLLoadIdentity(GLcontext context) { stub_call("GLLoadIdentity"); }
void Stub_GLLoadMatrixd(GLcontext context, const GLdouble *m) { stub_call("GLLoadMatrixd"); }
void Stub_GLLoadMatrixf(GLcontext context, const GLfloat *m) { stub_call("GLLoadMatrixf"); }
void Stub_GLLockArrays(GLcontext context, GLuint first, GLsizei count) { stub_call("GLLockArrays"); }
void Stub_GLMatrixMode(GLcontext context, GLenum mode) { stub_call("GLMatrixMode"); }
void Stub_GLMultMatrixd(GLcontext context, const GLdouble *m) { stub_call("GLMultMatrixd"); }
void Stub_GLMultMatrixf(GLcontext context, const GLfloat *m) { stub_call("GLMultMatrixf"); }
void Stub_GLMultiTexCoord2fARB(GLcontext context, GLenum unit, GLfloat s, GLfloat t) { stub_call("GLMultiTexCoord2fARB"); }
void Stub_GLMultiTexCoord2fvARB(GLcontext context, GLenum unit, GLfloat *v) { stub_call("GLMultiTexCoord2fvARB"); }
void Stub_GLNormal3f(GLcontext context, GLfloat x, GLfloat y, GLfloat z) { stub_call("GLNormal3f"); }
void Stub_GLOrtho(GLcontext context, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar) { stub_call("GLOrtho"); }
void Stub_GLPixelStorei(GLcontext context, GLenum pname, GLint param) { stub_call("GLPixelStorei"); }
void Stub_GLPointSize(GLcontext context, GLfloat size) { stub_call("GLPointSize"); }
void Stub_GLPolygonMode(GLcontext context, GLenum face, GLenum mode) { stub_call("GLPolygonMode"); }
void Stub_GLPopMatrix(GLcontext context) { stub_call("GLPopMatrix"); }
void Stub_GLPushMatrix(GLcontext context) { stub_call("GLPushMatrix"); }
void Stub_GLReadPixels(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels) { stub_call("GLReadPixels"); }
void Stub_GLRotated(GLcontext context, GLdouble angle, GLdouble x, GLdouble y, GLdouble z) { stub_call("GLRotated"); }
void Stub_GLRotatef(GLcontext context, GLfloat angle, GLfloat x, GLfloat y, GLfloat z) { stub_call("GLRotatef"); }
void Stub_GLRotatefEXT(GLcontext context, GLfloat angle, const GLint xyz) { stub_call("GLRotatefEXT"); }
void Stub_GLRotatefEXTs(GLcontext context, GLfloat sin_an, GLfloat cos_an, const GLint xyz) { stub_call("GLRotatefEXTs"); }
void Stub_GLScaled(GLcontext context, GLdouble x, GLdouble y, GLdouble z) { stub_call("GLScaled"); }
void Stub_GLScalef(GLcontext context, GLfloat x, GLfloat y, GLfloat z) { stub_call("GLScalef"); }
void Stub_GLScissor(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height) { stub_call("GLScissor"); }
void Stub_GLShadeModel(GLcontext context, GLenum mode) { stub_call("GLShadeModel"); }
void Stub_GLTexCoord2f(GLcontext context, GLfloat s, GLfloat t) { stub_call("GLTexCoord2f"); }
void Stub_GLTexCoord2fv(GLcontext context, GLfloat *v) { stub_call("GLTexCoord2fv"); }
void Stub_GLTexCoord4f(GLcontext context, GLfloat s, GLfloat t, GLfloat r, GLfloat q) { stub_call("GLTexCoord4f"); }
void Stub_GLTexCoord4fv(GLcontext context, GLfloat *v) { stub_call("GLTexCoord4fv"); }
void Stub_GLTexCoordPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) { stub_call("GLTexCoordPointer"); }
void Stub_GLTexEnvi(GLcontext context, GLenum target, GLenum pname, GLint param) { stub_call("GLTexEnvi"); }
void Stub_GLTexGeni(GLcontext context, GLenum coord, GLenum mode, GLenum map) { stub_call("GLTexGeni"); }
void Stub_GLTexImage2D(GLcontext context, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels) { stub_call("GLTexImage2D"); }
void Stub_GLTexParameteri(GLcontext context, GLenum target, GLenum pname, GLint param) { stub_call("GLTexParameteri"); }
void Stub_GLTexSubImage2D(GLcontext context, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels) { stub_call("GLTexSubImage2D"); }
void Stub_GLTranslated(GLcontext context, GLdouble x, GLdouble y, GLdouble z) { stub_call("GLTranslated"); }
void Stub_GLTranslatef(GLcontext context, GLfloat x, GLfloat y, GLfloat z) { stub_call("GLTranslatef"); }
void Stub_GLULookAt(GLfloat ex, GLfloat ey, GLfloat ez, GLfloat cx, GLfloat cy, GLfloat cz, GLfloat ux, GLfloat uy, GLfloat uz) { stub_call("GLULookAt"); }
void Stub_GLUPerspective(GLfloat fovy, GLfloat aspect, GLfloat znear, GLfloat zfar) { stub_call("GLUPerspective"); }
void Stub_GLUnlockArrays(GLcontext context) { stub_call("GLUnlockArrays"); }
void Stub_GLVertex2fv(GLcontext context, GLfloat *v) { stub_call("GLVertex2fv"); }
void Stub_GLVertex3fv(GLcontext context, GLfloat *v) { stub_call("GLVertex3fv"); }
void Stub_GLVertex4f(GLcontext context, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { stub_call("GLVertex4f"); }
void Stub_GLVertex4fv(GLcontext context, GLfloat *v) { stub_call("GLVertex4fv"); }
void Stub_GLVertexPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) { stub_call("GLVertexPointer"); }
void Stub_GLViewport(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height) { stub_call("GLViewport"); }
void Stub_MGLClearPointer(GLcontext context) { stub_call("MGLClearPointer"); }
void * Stub_MGLCreateContext(int offx, int offy, int w, int h) { stub_call("MGLCreateContext"); mini_CurrentContext=(GLcontext)&fake_context_storage; return (void *)mini_CurrentContext; }
void * Stub_MGLCreateContextFromID(GLint ID, GLint *w, GLint *h) { stub_call("MGLCreateContextFromID"); if (w) *w=0; if (h) *h=0; mini_CurrentContext=(GLcontext)&fake_context_storage; return (void *)mini_CurrentContext; }
void Stub_MGLDeleteContext(GLcontext context) { stub_call("MGLDeleteContext"); mini_CurrentContext=(GLcontext)0; }
void Stub_MGLDrawMultitexBuffer(GLcontext context, GLenum BSrc, GLenum BDst, GLenum TexEnv) { stub_call("MGLDrawMultitexBuffer"); }
void Stub_MGLEnableSync(GLcontext context, GLboolean enable) { stub_call("MGLEnableSync"); }
void Stub_MGLExit(GLcontext context) { stub_call("MGLExit"); }
void * Stub_MGLGetInputWindowHandle(GLcontext context) { stub_call("MGLGetInputWindowHandle"); return (void *)0; }
void * Stub_MGLGetWindowHandle(GLcontext context) { stub_call("MGLGetWindowHandle"); return (void *)0; }
void Stub_MGLIdleFunc(GLcontext context, IdleFn i) { stub_call("MGLIdleFunc"); }
void Stub_MGLKeyFunc(GLcontext context, KeyHandlerFn k) { stub_call("MGLKeyFunc"); }
GLboolean Stub_MGLLockBack(GLcontext context, MGLLockInfo *info) { stub_call("MGLLockBack"); return GL_TRUE; }
GLboolean Stub_MGLLockDisplay(GLcontext context) { stub_call("MGLLockDisplay"); return GL_TRUE; }
void Stub_MGLLockMode(GLcontext context, GLenum lockMode) { stub_call("MGLLockMode"); }
void Stub_MGLMainLoop(GLcontext context) { stub_call("MGLMainLoop"); }
void Stub_MGLMinTriArea(GLcontext context, GLfloat area) { stub_call("MGLMinTriArea"); }
void Stub_MGLMouseFunc(GLcontext context, MouseHandlerFn m) { stub_call("MGLMouseFunc"); }
void Stub_MGLPrintMatrix(GLcontext context, int mode) { stub_call("MGLPrintMatrix"); }
void Stub_MGLPrintMatrixStack(GLcontext context, int mode) { stub_call("MGLPrintMatrixStack"); }
GLboolean Stub_MGLResizeContext(GLcontext context, GLsizei width, GLsizei height) { stub_call("MGLResizeContext"); return GL_FALSE; }
void Stub_MGLSetPointer(GLcontext context) { stub_call("MGLSetPointer"); }
void Stub_MGLSetState(GLcontext context, GLenum cap, GLboolean state) { stub_call("MGLSetState"); }
void Stub_MGLSetZOffset(GLcontext context, GLfloat offset) { stub_call("MGLSetZOffset"); }
void Stub_MGLSpecialFunc(GLcontext context, SpecialHandlerFn s) { stub_call("MGLSpecialFunc"); }
void Stub_MGLSwitchDisplay(GLcontext context) { stub_call("MGLSwitchDisplay"); }
void Stub_MGLTexMemStat(GLcontext context, GLint *Current, GLint *Peak) { stub_call("MGLTexMemStat"); }
void Stub_MGLUnlockDisplay(GLcontext context) { stub_call("MGLUnlockDisplay"); }
void Stub_MGLWriteShotPPM(GLcontext context, char *filename) { stub_call("MGLWriteShotPPM"); }
void Stub_mglChooseGuardBand(GLboolean flag) { stub_call("mglChooseGuardBand"); }
void Stub_mglChooseMtexBufferSize(int size) { stub_call("mglChooseMtexBufferSize"); }
void Stub_mglChooseNumberOfBuffers(int number) { stub_call("mglChooseNumberOfBuffers"); }
void Stub_mglChoosePixelDepth(int depth) { stub_call("mglChoosePixelDepth"); }
void Stub_mglChooseTextureBufferSize(int size) { stub_call("mglChooseTextureBufferSize"); }
void Stub_mglChooseVertexBufferSize(int size) { stub_call("mglChooseVertexBufferSize"); }
void Stub_mglChooseWindowMode(GLboolean flag) { stub_call("mglChooseWindowMode"); }
GLint Stub_mglGetSupportedScreenModes(MGLScreenModeCallback CallbackFn) { stub_call("mglGetSupportedScreenModes"); return 0; }
void Stub_mglProhibitAlphaFallback(GLboolean flag) { stub_call("mglProhibitAlphaFallback"); }
void Stub_mglProhibitMipMapping(GLboolean flag) { stub_call("mglProhibitMipMapping"); }
void Stub_mglProposeCloseDesktop(GLboolean closeme) { stub_call("mglProposeCloseDesktop"); }

static void Stub_GLPolygonOffset(GLcontext context, GLfloat factor, GLfloat units)
{
    (void)context; (void)factor; (void)units; stub_call("GLPolygonOffset");
}

static void Stub_GLClientActiveTextureARB(GLcontext context, GLenum unit)
{
    (void)context; (void)unit; stub_call("GLClientActiveTextureARB");
}
static void Stub_GLInterleavedArrays(GLcontext context, GLenum format, GLsizei stride, const GLvoid *pointer)
{
    (void)context; (void)format; (void)stride; (void)pointer; stub_call("GLInterleavedArrays");
}
static void Stub_GLMultiDrawArrays(GLcontext context, GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount)
{
    (void)context; (void)mode; (void)first; (void)count; (void)primcount; stub_call("GLMultiDrawArrays");
}
static void Stub_GLBlendEquation(GLcontext context, GLenum mode)
{
    (void)context; (void)mode; stub_call("GLBlendEquation");
}
static void Stub_GLBlendFuncSeparate(GLcontext context, GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha)
{
    (void)context; (void)srcRGB; (void)dstRGB; (void)srcAlpha; (void)dstAlpha; stub_call("GLBlendFuncSeparate");
}
static GLboolean Stub_GLIsTexture(GLcontext context, GLuint texture)
{
    (void)context; (void)texture; stub_call("GLIsTexture"); return GL_FALSE;
}
static void Stub_mglChooseZBufferDepth(int bits)
{
    (void)bits; stub_call("mglChooseZBufferDepth");
}

void Stub_GLLineWidth(GLcontext context, GLfloat width) { stub_call("GLLineWidth"); }
void Stub_GLTexGenfv(GLcontext context, GLenum coord, GLenum pname, const GLfloat *params) { stub_call("GLTexGenfv"); }
void Stub_GLCopyTexImage2D(GLcontext context, GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border) { stub_call("GLCopyTexImage2D"); }
void Stub_GLCopyTexSubImage2D(GLcontext context, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height) { stub_call("GLCopyTexSubImage2D"); }
GLboolean Stub_GLAreTexturesResident(GLcontext context, GLsizei n, const GLuint *textures, GLboolean *residences) { stub_call("GLAreTexturesResident"); return GL_TRUE; }
void Stub_GLEdgeFlag(GLcontext context, GLboolean flag) { stub_call("GLEdgeFlag"); }
void Stub_GLEdgeFlagPointer(GLcontext context, GLsizei stride, const GLvoid *pointer) { stub_call("GLEdgeFlagPointer"); }
void Stub_GLEdgeFlagv(GLcontext context, const GLboolean *flag) { stub_call("GLEdgeFlagv"); }
void Stub_GLGetDoublev(GLcontext context, GLenum pname, GLdouble *params) { stub_call("GLGetDoublev"); if (params) params[0] = 0.0; }
void Stub_GLGetPointerv(GLcontext context, GLenum pname, GLvoid **params) { stub_call("GLGetPointerv"); if (params) *params = (GLvoid *)0; }
void Stub_GLIndexi(GLcontext context, GLint c) { stub_call("GLIndexi"); }
void Stub_GLIndexiv(GLcontext context, const GLint *c) { stub_call("GLIndexiv"); }
void Stub_GLIndexPointer(GLcontext context, GLenum type, GLsizei stride, const GLvoid *pointer) { stub_call("GLIndexPointer"); }
void Stub_GLPrioritizeTextures(GLcontext context, GLsizei n, const GLuint *textures, const GLclampf *priorities) { stub_call("GLPrioritizeTextures"); }
void Stub_GLReadBuffer(GLcontext context, GLenum mode) { stub_call("GLReadBuffer"); }
void * Stub_MGLCreateContextFromWindow(struct Window *window) { stub_call("MGLCreateContextFromWindow"); return (void *)&fake_context_storage; }
void * Stub_MGLCreateContextFromBitMap(struct BitMap *bitmap) { stub_call("MGLCreateContextFromBitMap"); return (void *)&fake_context_storage; }

#define MINIGL_V29_TEXENVI Stub_GLTexEnvi
#include "minigl_v29_fallbacks.h"

void Stub_GLNormal3fv(GLcontext context, GLfloat *n)
{
    if (!n) return;
    Stub_GLNormal3f(context, n[0], n[1], n[2]);
}

const MGLDispatchTable MiniGLDispatchTable = {
    MINIGL_DISPATCH_ABI_VERSION,
    (ULONG)sizeof(MGLDispatchTable),
    (MINIGL_BACKEND_FLAG_STUB | MINIGL_BACKEND_FLAG_PPC),
    0,
    &mini_CurrentContext,
    Stub_GLActiveTextureARB,
    Stub_GLAlphaFunc,
    Stub_GLArrayElement,
    Stub_GLBegin,
    Stub_GLBindTexture,
    Stub_GLBlendFunc,
    Stub_GLClear,
    Stub_GLClearColor,
    Stub_GLClearDepth,
    Stub_GLColor3fv,
    Stub_GLColor3ubv,
    Stub_GLColor4f,
    Stub_GLColor4fv,
    Stub_GLColor4ub,
    Stub_GLColor4ubv,
    Stub_GLColorMask,
    Stub_GLColorPointer,
    Stub_GLColorTable,
    Stub_GLCullFace,
    Stub_GLDeleteTextures,
    Stub_GLDepthFunc,
    Stub_GLDepthMask,
    Stub_GLDepthRange,
    Stub_GLDisableClientState,
    Stub_GLDrawArrays,
    Stub_GLDrawBuffer,
    Stub_GLDrawElements,
    Stub_GLEnableClientState,
    Stub_GLEnd,
    Stub_GLFinish,
    Stub_GLFlush,
    Stub_GLFogf,
    Stub_GLFogfv,
    Stub_GLFrontFace,
    Stub_GLFrustum,
    Stub_GLGenTextures,
    Stub_GLGetBooleanv,
    Stub_GLGetError,
    Stub_GLGetFloatv,
    Stub_GLGetIntegerv,
    Stub_GLGetString,
    Stub_GLHint,
    Stub_GLIsEnabled,
    Stub_GLLoadIdentity,
    Stub_GLLoadMatrixd,
    Stub_GLLoadMatrixf,
    Stub_GLLockArrays,
    Stub_GLMatrixMode,
    Stub_GLMultMatrixd,
    Stub_GLMultMatrixf,
    Stub_GLMultiTexCoord2fARB,
    Stub_GLMultiTexCoord2fvARB,
    Stub_GLNormal3f,
    Stub_GLOrtho,
    Stub_GLPixelStorei,
    Stub_GLPointSize,
    Stub_GLPolygonMode,
    Stub_GLPopMatrix,
    Stub_GLPushMatrix,
    Stub_GLReadPixels,
    Stub_GLRotated,
    Stub_GLRotatef,
    Stub_GLRotatefEXT,
    Stub_GLRotatefEXTs,
    Stub_GLScaled,
    Stub_GLScalef,
    Stub_GLScissor,
    Stub_GLShadeModel,
    Stub_GLTexCoord2f,
    Stub_GLTexCoord2fv,
    Stub_GLTexCoord4f,
    Stub_GLTexCoord4fv,
    Stub_GLTexCoordPointer,
    Stub_GLTexEnvi,
    Stub_GLTexGeni,
    Stub_GLTexImage2D,
    Stub_GLTexParameteri,
    Stub_GLTexSubImage2D,
    Stub_GLTranslated,
    Stub_GLTranslatef,
    Stub_GLULookAt,
    Stub_GLUPerspective,
    Stub_GLUnlockArrays,
    Stub_GLVertex2fv,
    Stub_GLVertex3fv,
    Stub_GLVertex4f,
    Stub_GLVertex4fv,
    Stub_GLVertexPointer,
    Stub_GLViewport,
    Stub_MGLClearPointer,
    Stub_MGLCreateContext,
    Stub_MGLCreateContextFromID,
    Stub_MGLDeleteContext,
    Stub_MGLDrawMultitexBuffer,
    Stub_MGLEnableSync,
    Stub_MGLExit,
    Stub_MGLGetInputWindowHandle,
    Stub_MGLGetWindowHandle,
    Stub_MGLIdleFunc,
    Stub_MGLKeyFunc,
    Stub_MGLLockBack,
    Stub_MGLLockDisplay,
    Stub_MGLLockMode,
    Stub_MGLMainLoop,
    Stub_MGLMinTriArea,
    Stub_MGLMouseFunc,
    Stub_MGLPrintMatrix,
    Stub_MGLPrintMatrixStack,
    Stub_MGLResizeContext,
    Stub_MGLSetPointer,
    Stub_MGLSetState,
    Stub_MGLSetZOffset,
    Stub_MGLSpecialFunc,
    Stub_MGLSwitchDisplay,
    Stub_MGLTexMemStat,
    Stub_MGLUnlockDisplay,
    Stub_MGLWriteShotPPM,
    Stub_mglChooseGuardBand,
    Stub_mglChooseMtexBufferSize,
    Stub_mglChooseNumberOfBuffers,
    Stub_mglChoosePixelDepth,
    Stub_mglChooseTextureBufferSize,
    Stub_mglChooseVertexBufferSize,
    Stub_mglChooseWindowMode,
    Stub_mglGetSupportedScreenModes,
    Stub_mglProhibitAlphaFallback,
    Stub_mglProhibitMipMapping,
    Stub_mglProposeCloseDesktop,
    Stub_GLPolygonOffset,
    Stub_GLClientActiveTextureARB,
    Stub_GLInterleavedArrays,
    Stub_GLMultiDrawArrays,
    Stub_GLBlendEquation,
    Stub_GLBlendFuncSeparate,
    Stub_GLIsTexture,
    Stub_mglChooseZBufferDepth,
    /* V27 append-only tail. */
    Stub_GLLineWidth,
    Stub_GLTexGenfv,
    Stub_GLCopyTexImage2D,
    Stub_GLCopyTexSubImage2D,
    Stub_GLAreTexturesResident,
    Stub_GLEdgeFlag,
    Stub_GLEdgeFlagPointer,
    Stub_GLEdgeFlagv,
    Stub_GLGetDoublev,
    Stub_GLGetPointerv,
    Stub_GLIndexi,
    Stub_GLIndexiv,
    Stub_GLIndexPointer,
    Stub_GLPrioritizeTextures,
    Stub_GLReadBuffer,
    Stub_MGLCreateContextFromWindow,
    Stub_MGLCreateContextFromBitMap,
    /* V29 tail. */
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
    Stub_GLNormal3fv, /* 29.1: slot 205. */
};


const MGLDispatchTable * __saveds MGLGetDispatchTable(void)
{
    return &MiniGLDispatchTable;
}

void * __saveds dllFindResource(int type, char *name)
{
    (void)type;
    (void)name;
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

dll_tExportSymbol DLL_ExportSymbols[] = {
    { (void *)dllFindResource,     (char *)"dllFindResource" },
    { (void *)dllLoadResource,     (char *)"dllLoadResource" },
    { (void *)dllFreeResource,     (char *)"dllFreeResource" },
    { (void *)MGLGetDispatchTable, (char *)"MGLGetDispatchTable" },
    { 0, 0 }
};

dll_tImportSymbol DLL_ImportSymbols[] = {
    { 0, 0, 0, 0 }
};

int __saveds DLL_Init(void)
{
    mini_CurrentContext = (GLcontext)0;
    fake_context_storage.CurrentPrimitive = GL_BASE;
    fake_context_storage.CurrentError = GL_NO_ERROR;
    printf("[minigl.ppc.stub] DLL_Init\n");
    return 1;
}

void __saveds DLL_DeInit(void)
{
    printf("[minigl.ppc.stub] DLL_DeInit\n");
    mini_CurrentContext = (GLcontext)0;
}

#define MINIGL_LIBRARY_BUILD 1
#include <exec/types.h>
#include <libraries/minigl_dispatch.h>
#include "context_access.h"
#include "debug_output.h"

static ULONG fake_context_storage[64];

static void stub_call(const char *name)
{
    dbg_puts("[minigl.direct stub] ");
    dbg_puts(name);
    dbg_newline();
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
GLenum Stub_GLGetError(GLcontext context) { stub_call("GLGetError"); return 0; }
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
void * Stub_MGLCreateContext(int offx, int offy, int w, int h) { stub_call("MGLCreateContext"); mini_CurrentContext=(GLcontext)&fake_context_storage[0]; return (void *)mini_CurrentContext; }
void * Stub_MGLCreateContextFromID(GLint ID, GLint *w, GLint *h) { stub_call("MGLCreateContextFromID"); if (w) *w=0; if (h) *h=0; mini_CurrentContext=(GLcontext)&fake_context_storage[0]; return (void *)mini_CurrentContext; }
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
void Stub_MGLResizeContext(GLcontext context, GLsizei width, GLsizei height) { stub_call("MGLResizeContext"); }
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
static void Stub_GLClientActiveTextureARB(GLcontext context, GLenum unit) { (void)context; (void)unit; stub_call("GLClientActiveTextureARB"); }
static void Stub_GLInterleavedArrays(GLcontext context, GLenum format, GLsizei stride, const GLvoid *pointer) { (void)context; (void)format; (void)stride; (void)pointer; stub_call("GLInterleavedArrays"); }
static void Stub_GLMultiDrawArrays(GLcontext context, GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount) { (void)context; (void)mode; (void)first; (void)count; (void)primcount; stub_call("GLMultiDrawArrays"); }
static void Stub_GLBlendEquation(GLcontext context, GLenum mode) { (void)context; (void)mode; stub_call("GLBlendEquation"); }
static void Stub_GLBlendFuncSeparate(GLcontext context, GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) { (void)context; (void)srcRGB; (void)dstRGB; (void)srcAlpha; (void)dstAlpha; stub_call("GLBlendFuncSeparate"); }
static GLboolean Stub_GLIsTexture(GLcontext context, GLuint texture) { (void)context; (void)texture; stub_call("GLIsTexture"); return GL_FALSE; }
static void Stub_mglChooseZBufferDepth(int bits) { (void)bits; stub_call("mglChooseZBufferDepth"); }

const MGLDispatchTable MiniGLDispatchTable = {
    MINIGL_DISPATCH_ABI_VERSION,
    (ULONG)sizeof(MGLDispatchTable),
    MINIGL_BACKEND_FLAG_STUB,
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
};

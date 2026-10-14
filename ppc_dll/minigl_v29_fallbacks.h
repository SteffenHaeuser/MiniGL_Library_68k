/* Adapted from the supplied Classic V29 adapter. */
#ifndef MINIGL_V29_FALLBACKS_H
#define MINIGL_V29_FALLBACKS_H
#ifndef MINIGL_V29_TEXENVI
#define MINIGL_V29_TEXENVI GLTexEnvi
#endif
/* New unsupported core GL entry points report failure without changing state.
 * Do not use GLFlagError: Classic's release config compiles that macro out. */
static void PPCV29_Unsupported(GLcontext context)
{
    if (context && context->CurrentError == GL_NO_ERROR)
        context->CurrentError = GL_INVALID_OPERATION;
}

/* Stub bodies for the 47 entries appended for 28.0. Base MiniGL has no
 * lighting, no display lists, no GLU quadrics and no GLUT at all. */
static void PPCV29_GLLightf(GLcontext context, GLenum light, GLenum pname, GLfloat param) { (void)context; (void)light; (void)pname; (void)param;  PPCV29_Unsupported(context); }
static void PPCV29_GLLightfv(GLcontext context, GLenum light, GLenum pname, const GLfloat *params) { (void)context; (void)light; (void)pname; (void)params;  PPCV29_Unsupported(context); }
static void PPCV29_GLMaterialf(GLcontext context, GLenum face, GLenum pname, GLfloat param) { (void)context; (void)face; (void)pname; (void)param;  PPCV29_Unsupported(context); }
static void PPCV29_GLMaterialfv(GLcontext context, GLenum face, GLenum pname, const GLfloat *params) { (void)context; (void)face; (void)pname; (void)params;  PPCV29_Unsupported(context); }
static void PPCV29_GLLightModelf(GLcontext context, GLenum pname, GLfloat param) { (void)context; (void)pname; (void)param;  PPCV29_Unsupported(context); }
static void PPCV29_GLLightModelfv(GLcontext context, GLenum pname, const GLfloat *params) { (void)context; (void)pname; (void)params;  PPCV29_Unsupported(context); }
static void PPCV29_GLGetLightfv(GLcontext context, GLenum light, GLenum pname, GLfloat *params) { (void)context; (void)light; (void)pname; (void)params;  PPCV29_Unsupported(context); }
static void PPCV29_GLGetMaterialfv(GLcontext context, GLenum face, GLenum pname, GLfloat *params) { (void)context; (void)face; (void)pname; (void)params;  PPCV29_Unsupported(context); }
static GLuint PPCV29_GLGenLists(GLcontext context, GLsizei range) { (void)context; (void)range; return 0; }
static void PPCV29_GLDeleteLists(GLcontext context, GLuint list, GLsizei range) { (void)context; (void)list; (void)range; }
static GLboolean PPCV29_GLIsList(GLcontext context, GLuint list) { (void)context; (void)list; return GL_FALSE; }
static void PPCV29_GLNewList(GLcontext context, GLuint list, GLenum mode) { (void)context; (void)list; (void)mode;  PPCV29_Unsupported(context); }
static void PPCV29_GLEndList(GLcontext context) { (void)context;  PPCV29_Unsupported(context); }
static void PPCV29_GLCallList(GLcontext context, GLuint list) { (void)context; (void)list;  PPCV29_Unsupported(context); }
static GLint PPCV29_GLUBuild2DMipmaps(GLcontext context, GLenum target, GLint internalFormat, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *data) { (void)context; (void)target; (void)internalFormat; (void)width; (void)height; (void)format; (void)type; (void)data; return GLU_INVALID_OPERATION; }
/* GLU error text does not require a renderer. */
static const GLubyte *PPCV29_GLUErrorString(GLenum errCode)
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
static GLUquadricObj * PPCV29_GLUNewQuadric(void) { return NULL; }
static void PPCV29_GLUDeleteQuadric(GLUquadricObj *q) { (void)q; }
static void PPCV29_GLUQuadricNormals(GLUquadricObj *q, GLenum normals) { (void)q; (void)normals; }
static void PPCV29_GLUQuadricTexture(GLUquadricObj *q, GLboolean textureCoords) { (void)q; (void)textureCoords; }
static void PPCV29_GLUQuadricDrawStyle(GLUquadricObj *q, GLenum drawStyle) { (void)q; (void)drawStyle; }
static void PPCV29_GLUQuadricOrientation(GLUquadricObj *q, GLenum orientation) { (void)q; (void)orientation; }
static void PPCV29_GLUCylinder(GLUquadricObj *q, GLdouble base, GLdouble top, GLdouble height, GLint slices, GLint stacks) { (void)q; (void)base; (void)top; (void)height; (void)slices; (void)stacks; }
static void PPCV29_GLUSphere(GLUquadricObj *q, GLdouble radius, GLint slices, GLint stacks) { (void)q; (void)radius; (void)slices; (void)stacks; }
static void PPCV29_GLUDisk(GLUquadricObj *q, GLdouble inner, GLdouble outer, GLint slices, GLint loops) { (void)q; (void)inner; (void)outer; (void)slices; (void)loops; }
static void PPCV29_GLUTInit(int *argcp, char **argv) { (void)argcp; (void)argv; }
static void PPCV29_GLUTInitDisplayMode(unsigned int mode) { (void)mode; }
static void PPCV29_GLUTInitWindowSize(int width, int height) { (void)width; (void)height; }
static void PPCV29_GLUTInitWindowPosition(int x, int y) { (void)x; (void)y; }
static int PPCV29_GLUTCreateWindow(const char *title) { (void)title; return 0; }
static void PPCV29_GLUTMainLoop(void) { }
static void PPCV29_GLUTDisplayFunc(void (*func)(void)) { (void)func; }
static void PPCV29_GLUTIdleFunc(void (*func)(void)) { (void)func; }
static void PPCV29_GLUTKeyboardFunc(void (*func)(unsigned char key, int x, int y)) { (void)func; }
static void PPCV29_GLUTReshapeFunc(void (*func)(int width, int height)) { (void)func; }
static void PPCV29_GLUTSwapBuffers(void) { }
static void PPCV29_GLUTPostRedisplay(void) { }
static int PPCV29_GLUTGet(GLenum state) { (void)state; return 0; }
static void PPCV29_GLUTGameModeString(const char *string) { (void)string; }
static int PPCV29_GLUTEnterGameMode(void) { return 0; }
static void PPCV29_GLUTLeaveGameMode(void) { }
static int PPCV29_GLUTGameModeGet(GLenum query) { (void)query; return 0; }
static void PPCV29_GLUTSolidCube(GLdouble size) { (void)size; }
static void PPCV29_GLUTSolidSphere(GLdouble radius, GLint slices, GLint stacks) { (void)radius; (void)slices; (void)stacks; }
static void PPCV29_GLUTSolidCone(GLdouble base, GLdouble height, GLint slices, GLint stacks) { (void)base; (void)height; (void)slices; (void)stacks; }
static void PPCV29_GLUTSolidTorus(GLdouble innerRadius, GLdouble outerRadius, GLint sides, GLint rings) { (void)innerRadius; (void)outerRadius; (void)sides; (void)rings; }
static void PPCV29_GLUTSolidDodecahedron(void) { }
static void PPCV29_GLNormalPointer(GLcontext context, GLenum type, GLsizei stride, const GLvoid *pointer) { (void)type; (void)stride; (void)pointer; PPCV29_Unsupported(context); }
static void PPCV29_GLUQuadricCallback(GLUquadricObj *q, GLenum which, MGLUfuncptr fn) { (void)q; (void)which; (void)fn; }
static void PPCV29_GLColorMaterial(GLcontext context, GLenum face, GLenum mode) { (void)context; (void)face; (void)mode;  PPCV29_Unsupported(context); }
static void PPCV29_GLTexEnvfv(GLcontext context, GLenum target, GLenum pname, const GLfloat *params)
{
    if (!context) return;
    if (!params) { PPCV29_Unsupported(context); return; }
    if (target != GL_TEXTURE_ENV || pname != GL_TEXTURE_ENV_MODE) {
        if (context->CurrentError == GL_NO_ERROR) context->CurrentError = GL_INVALID_ENUM;
        return;
    }
    /* Classic's Warp3D TexEnvi supports MODULATE, DECAL and REPLACE. */
    if (params[0] != GL_MODULATE && params[0] != GL_DECAL && params[0] != GL_REPLACE) {
        if (context->CurrentError == GL_NO_ERROR) context->CurrentError = GL_INVALID_ENUM;
        return;
    }
    MINIGL_V29_TEXENVI(context, target, pname, (GLint)params[0]);
}




#endif

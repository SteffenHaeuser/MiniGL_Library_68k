/*
 * (C) 2025-2026 Dennis van der Boon
 *
 * GLUT shim for MiniGLV3D. GLUT itself does not exist on this platform, so this
 * header declares the subset a GLUT application actually needs and
 * gl/src/glut.c and gl/src/glutshapes.c implement it over the real MGL* entry
 * points.
 *
 * IT IS IN THE LIBRARY AND IN THE STATIC ARCHIVE. glut.o and glutshapes.o are
 * archive members, and all 22 GLUT entry points carry dispatch slots, so a
 * minigl.library client reaches them the same way it reaches gl and glu. Every
 * function here is still written purely in terms of the public gl and mgl entry
 * points, so nothing in it needs driver internals.
 *
 * The enum values below are the REAL GLUT values, not positional ones, so an
 * application compiled against a genuine glut.h elsewhere passes the same
 * numbers here and behaves identically.
 *
 * The extern "C" guard is load-bearing: a C++ client needs it, and without
 * it every name here mangles and nothing links. (mgl/minigl.h has no such
 * guard, which is why a C++ client cannot currently call minigl.library
 * directly.)
 */

#ifndef MGL_GLUT_H
#define MGL_GLUT_H

#include <mgl/gl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GLUTInitDisplayMode bits. GLUT_RGB, GLUT_RGBA and GLUT_SINGLE are all 0 --
 * that is GLUT's own definition, not an omission: they are the defaults and
 * carry no bit. */
#define GLUT_RGB                    0x0000
#define GLUT_RGBA                   0x0000
#define GLUT_INDEX                  0x0001
#define GLUT_SINGLE                 0x0000
#define GLUT_DOUBLE                 0x0002
#define GLUT_ACCUM                  0x0004
#define GLUT_ALPHA                  0x0008
#define GLUT_DEPTH                  0x0010
#define GLUT_STENCIL                0x0020
#define GLUT_MULTISAMPLE            0x0080
#define GLUT_STEREO                 0x0100
#define GLUT_LUMINANCE              0x0200

/* GLUTGameModeGet queries. */
#define GLUT_GAME_MODE_ACTIVE           0x0000
#define GLUT_GAME_MODE_POSSIBLE         0x0001
#define GLUT_GAME_MODE_WIDTH            0x0002
#define GLUT_GAME_MODE_HEIGHT           0x0003
#define GLUT_GAME_MODE_PIXEL_DEPTH      0x0004
#define GLUT_GAME_MODE_REFRESH_RATE     0x0005
#define GLUT_GAME_MODE_DISPLAY_CHANGED  0x0006

/*
 * GLUTGet queries. SPELLED DECIMAL, which GLUT's own header does for exactly
 * these and nothing else -- they are a dense run (100-123, 200-203, 300, 400,
 * 500-504) and hex hides the sequence. GLUT_ELAPSED_TIME below is 0x02BC,
 * which is GLUT's 700, and that agreement is the evidence this header's
 * GLUT numbering is GLUT's own.
 *
 * Every value here is glut-master/include/GL/glut.h's: GLUT spells these
 * ((GLenum) 100) and its SPECIAL-KEY tokens a bare 100, and the two namespaces
 * COLLIDE in value -- GLUT_KEY_LEFT is also 100. Taking the cast as the
 * discriminator is what keeps the query names off the key names.
 *
 * GLUT_CURSOR_INHERIT is 100 as well, a third namespace sharing that value.
 * That is GLUT's own doing (glut.h:576) and harmless, since a cursor constant
 * and a query are never passed to the same function.
 *
 * See glutGet() in the AutoDoc for what each answers and why a zero here is a
 * fact rather than a placeholder.
 */
#define GLUT_WINDOW_X                   100
#define GLUT_WINDOW_Y                   101
#define GLUT_WINDOW_WIDTH               102
#define GLUT_WINDOW_HEIGHT              103
#define GLUT_WINDOW_BUFFER_SIZE         104
#define GLUT_WINDOW_STENCIL_SIZE        105
#define GLUT_WINDOW_DEPTH_SIZE          106
#define GLUT_WINDOW_RED_SIZE            107
#define GLUT_WINDOW_GREEN_SIZE          108
#define GLUT_WINDOW_BLUE_SIZE           109
#define GLUT_WINDOW_ALPHA_SIZE          110
#define GLUT_WINDOW_ACCUM_RED_SIZE      111
#define GLUT_WINDOW_ACCUM_GREEN_SIZE    112
#define GLUT_WINDOW_ACCUM_BLUE_SIZE     113
#define GLUT_WINDOW_ACCUM_ALPHA_SIZE    114
#define GLUT_WINDOW_DOUBLEBUFFER        115
#define GLUT_WINDOW_RGBA                116
#define GLUT_WINDOW_PARENT              117
#define GLUT_WINDOW_NUM_CHILDREN        118
#define GLUT_WINDOW_COLORMAP_SIZE       119
#define GLUT_WINDOW_NUM_SAMPLES         120
#define GLUT_WINDOW_STEREO              121
#define GLUT_WINDOW_CURSOR              122
#define GLUT_WINDOW_FORMAT_ID           123
#define GLUT_SCREEN_WIDTH               200
#define GLUT_SCREEN_HEIGHT              201
#define GLUT_SCREEN_WIDTH_MM            202
#define GLUT_SCREEN_HEIGHT_MM           203
#define GLUT_MENU_NUM_ITEMS             300
#define GLUT_DISPLAY_MODE_POSSIBLE      400
#define GLUT_INIT_WINDOW_X              500
#define GLUT_INIT_WINDOW_Y              501
#define GLUT_INIT_WINDOW_WIDTH          502
#define GLUT_INIT_WINDOW_HEIGHT         503
#define GLUT_INIT_DISPLAY_MODE          504

/* glutGet(GLUT_WINDOW_CURSOR)'s answer: the cursor a window has when nothing
 * has called glutSetCursor, which is GLUT's own initial value for every window
 * (glut_win.c:671). There is no glutSetCursor here to change it. */
#define GLUT_CURSOR_INHERIT             100

/* Lifecycle. */
extern void GLUTInit(int *argcp, char **argv);
extern void GLUTInitDisplayMode(unsigned int mode);
extern void GLUTInitWindowSize(int width, int height);
extern void GLUTInitWindowPosition(int x, int y);
extern int  GLUTCreateWindow(const char *title);
extern void GLUTMainLoop(void);

/* Callback registration. GLUTKeyboardFunc's (key, x, y) signature is GLUT's;
 * the driver's own KeyHandlerFn takes only the key, so mglglut_main.c passes
 * x = y = 0. MiniGLV3D delivers no mouse position with a keystroke. */
extern void GLUTDisplayFunc(void (*func)(void));
extern void GLUTIdleFunc(void (*func)(void));
extern void GLUTKeyboardFunc(void (*func)(unsigned char key, int x, int y));
extern void GLUTReshapeFunc(void (*func)(int width, int height));

/* Frame control. */
extern void GLUTSwapBuffers(void);
extern void GLUTPostRedisplay(void);

/*
 * GLUTGet. GLUT_ELAPSED_TIME is the only query implemented, because it is the
 * one a port actually needs: an application whose animation advances per FRAME
 * runs at whatever speed the hardware happens to manage, so it needs a real
 * clock to pace itself against. GLUT's own value for it is 700.
 *
 * Returns milliseconds since GLUTInit. Monotonic, and never returns a stuck
 * value -- if timer.device cannot be opened it falls back to DateStamp, which is
 * coarser (20 ms) but still advances. A caller should nonetheless tolerate a
 * zero delta between two calls, which is normal on any clock read twice inside
 * one tick.
 */
#define GLUT_ELAPSED_TIME               0x02BC
extern int  GLUTGet(GLenum state);

/* Fullscreen ("game mode"). GLUTGameModeString parses GLUT's own
 * "WIDTHxHEIGHT:BPP@REFRESH" form; every field is optional. */
extern void GLUTGameModeString(const char *string);
extern int  GLUTEnterGameMode(void);
extern void GLUTLeaveGameMode(void);
extern int  GLUTGameModeGet(GLenum query);

/* Solid shapes. These take GLdouble because GLUT does; the tessellators
 * narrow to GLfloat internally, which is what the driver consumes. */
extern void GLUTSolidCube(GLdouble size);
extern void GLUTSolidSphere(GLdouble radius, GLint slices, GLint stacks);
extern void GLUTSolidCone(GLdouble base, GLdouble height, GLint slices, GLint stacks);
extern void GLUTSolidTorus(GLdouble innerRadius, GLdouble outerRadius, GLint sides, GLint rings);
extern void GLUTSolidDodecahedron(void);


#ifndef USE_MGLAPI

/*
 * The glut* spellings an application writes, mapped onto the driver entry
 * points above. Same shape as every gl* and glu* macro in <mgl/gl.h>: the
 * exported name is prefixed so that it can carry a library dispatch stub later,
 * while the application keeps writing standard GLUT.
 */
#define glutCreateWindow(title) GLUTCreateWindow(title)
#define glutDisplayFunc(fn) GLUTDisplayFunc(fn)
#define glutEnterGameMode() GLUTEnterGameMode()
#define glutGameModeGet(mode) GLUTGameModeGet(mode)
#define glutGameModeString(s) GLUTGameModeString(s)
#define glutGet(state) GLUTGet(state)
#define glutIdleFunc(fn) GLUTIdleFunc(fn)
#define glutInit(argcp, argv) GLUTInit(argcp, argv)
#define glutInitDisplayMode(mode) GLUTInitDisplayMode(mode)
#define glutInitWindowPosition(x, y) GLUTInitWindowPosition(x, y)
#define glutInitWindowSize(w, h) GLUTInitWindowSize(w, h)
#define glutKeyboardFunc(fn) GLUTKeyboardFunc(fn)
#define glutLeaveGameMode() GLUTLeaveGameMode()
/* freeglut's name for leaving the main loop, which GLUT 3.7 had no way to do.
 * GLUTMainLoop ends when the context's Running flag clears, and MGLExit is
 * what clears it -- GLUTLeaveGameMode already relies on that. So this is the
 * existing mechanism under the name a freeglut program expects, not a new
 * entry point. */
#define glutLeaveMainLoop() MGLExit(mini_CurrentContext)
#define glutMainLoop() GLUTMainLoop()
#define glutPostRedisplay() GLUTPostRedisplay()
#define glutReshapeFunc(fn) GLUTReshapeFunc(fn)
#define glutSolidCone(base, height, sl, st) GLUTSolidCone(base, height, sl, st)
#define glutSolidCube(size) GLUTSolidCube(size)
#define glutSolidDodecahedron() GLUTSolidDodecahedron()
#define glutSolidSphere(r, sl, st) GLUTSolidSphere(r, sl, st)
#define glutSolidTorus(inner, outer, sides, rings) GLUTSolidTorus(inner, outer, sides, rings)
#define glutSwapBuffers() GLUTSwapBuffers()

#endif /* USE_MGLAPI not defined -- a library client gets its glut* macros
                * from <libraries/minigl_dispatch.h> instead */

#ifdef __cplusplus
}
#endif

#endif /* MGLGLUT_H */

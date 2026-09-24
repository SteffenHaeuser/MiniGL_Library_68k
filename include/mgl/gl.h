/*
 * $Id: gl.h,v 1.10 2000/03/15 17:58:19 hfrieden Exp $
 *
 * $Date: 2000/03/15 17:58:19 $
 * $Revision: 1.10 $
 *
 * (C) 1999 by Hyperion
 * All rights reserved
 *
 * This file is part of the MiniGL library project
 * See the file Licence.txt for more details
 *
 */

#ifndef GL_H_
#define GL_H_

#ifdef __cplusplus
extern "C"
{
#endif

#if defined(__PPC__) || defined (__VBCC__) || defined(__STORM__)
#define mykprintf kprintf
#endif

#include "mgl/config.h"
#include "mgl/log.h"

#ifndef NLOGGING
extern int MGLDebugLevel;
#define mglSetDebugLevel(level) \
	MGLDebugLevel = level
#endif


/*
	GL types
*/

typedef void            GLvoid;
//typedef unsigned char   GLboolean;
typedef char            GLbyte;
typedef unsigned char   GLubyte;
typedef short           GLshort;
typedef unsigned short  GLushort;
typedef int             GLint;
typedef unsigned int    GLuint;
typedef unsigned int    GLboolean;
typedef long            GLsizei;
typedef unsigned long   GLbitfield;
typedef float           GLfloat;
typedef double          GLdouble;
typedef float           GLclampf;
typedef double          GLclampd;
//typedef int             GLenum;
typedef unsigned int    GLenum;

/*
	GL enum
	Currently a dummy
*/

#define MAX_TEXUNIT 2

enum
{
	GL_BASE             = 0,
      GL_NO_ERROR         = 0,
	GL_ALPHA,
	GL_ALPHA8,
	GL_ALPHA_BITS,
	GL_ALPHA_SCALE,
	GL_ALPHA_TEST,
	GL_ALPHA_TEST_FUNC,
	GL_ALPHA_TEST_REF,
	GL_ALWAYS,
	GL_AUX_BUFFERS,
	GL_BACK,
	GL_BACK_LEFT,
	GL_BACK_RIGHT,
	GL_BLEND,
	GL_BLEND_DST,
	GL_BLEND_SRC,
	GL_BLUE,
	GL_BLUE_BITS,
	GL_BYTE,
	GL_C3F_V3F,
	GL_C4UB_V2F,
	GL_C4UB_V3F,
	GL_CCW,
	GL_COLOR_ARRAY,
	GL_COLOR_ARRAY_POINTER,
	GL_COLOR_ARRAY_SIZE,
	GL_COLOR_ARRAY_STRIDE,
	GL_COLOR_ARRAY_TYPE,
	GL_COLOR_CLEAR_VALUE,
	GL_COLOR_INDEX,
	GL_CULL_FACE,
	GL_CULL_FACE_MODE,
	GL_CURRENT_COLOR,
	GL_CURRENT_INDEX,
	GL_CURRENT_TEXTURE_COORDS,
	GL_CW,
	GL_DECAL,
	GL_DEPTH_BITS,
	GL_DEPTH_CLEAR_VALUE,
	GL_DEPTH_COMPONENT,
	GL_DEPTH_FUNC,
	GL_DEPTH_RANGE,
	GL_DEPTH_SCALE,
	GL_DEPTH_TEST,
	GL_DEPTH_WRITEMASK,
	GL_DITHER,
	GL_DONT_CARE,
	GL_DOUBLE,
	GL_DOUBLEBUFFER,
	GL_DRAW_BUFFER,
	GL_DST_ALPHA,
	GL_DST_COLOR,
	GL_EDGE_FLAG,
	GL_EDGE_FLAG_ARRAY,
	GL_EDGE_FLAG_ARRAY_POINTER,
	GL_EDGE_FLAG_ARRAY_STRIDE,
	GL_EQUAL,
	GL_EXTENSIONS,
	GL_FASTEST,
	GL_FLAT,
	GL_FLOAT,
	GL_FOG,
	GL_FOG_COLOR,
	GL_FOG_DENSITY,
	GL_FOG_END,
	GL_FOG_HINT,
	GL_FOG_INDEX,
	GL_FOG_MODE,
	GL_FOG_START,
	GL_FRONT_AND_BACK,
	GL_FRONT,
	GL_FRONT_FACE,
	GL_FRONT_LEFT,
	GL_FRONT_RIGHT,
	GL_GEQUAL,
	GL_GREATER,
	GL_GREEN,
	GL_GREEN_BITS,
	GL_INDEX_ARRAY,
	GL_INDEX_ARRAY_POINTER,
	GL_INDEX_ARRAY_STRIDE,
	GL_INDEX_ARRAY_TYPE,
	GL_INDEX_BITS,
	GL_INDEX_CLEAR_VALUE,
	GL_INDEX_MODE,
	GL_INT,
	GL_INTENSITY,
	GL_INTENSITY8,
	GL_INVALID_ENUM,
	GL_INVALID_OPERATION,
	GL_INVALID_VALUE,
	GL_LEFT,
	GL_LEQUAL,
	GL_LESS,
	GL_LINEAR,
	GL_LINEAR_MIPMAP_LINEAR,
	GL_LINEAR_MIPMAP_NEAREST,
	GL_LINES,
	GL_LINE_LOOP,
	GL_LINE_STRIP,
	GL_LUMINANCE,
	GL_LUMINANCE8,
	GL_LUMINANCE8_ALPHA8,
	GL_LUMINANCE_ALPHA,
	GL_MATRIX_MODE,
	GL_MAX_TEXTURE_SIZE,
	GL_MAX_VIEWPORT_DIMS,
	GL_MODELVIEW,
	GL_MODELVIEW_MATRIX,
	GL_MODELVIEW_STACK_DEPTH,
	GL_MODULATE,
	GL_NEVER,
	GL_NEAREST,
	GL_NEAREST_MIPMAP_NEAREST,
	GL_NEAREST_MIPMAP_LINEAR,
	GL_NICEST,
	GL_NONE,
	GL_NOTEQUAL,
	GL_ONE,
	GL_ONE_MINUS_DST_ALPHA,
	GL_ONE_MINUS_DST_COLOR,
	GL_ONE_MINUS_SRC_ALPHA,
	GL_ONE_MINUS_SRC_COLOR,
	GL_OUT_OF_MEMORY,
	GL_PACK_ALIGNMENT,
	GL_PACK_LSB_FIRST,
	GL_PACK_ROW_LENGTH,
	GL_PACK_SKIP_PIXELS,
	GL_PACK_SKIP_ROWS,
	GL_PACK_SWAP_BYTES,
	GL_PERSPECTIVE_CORRECTION_HINT,
	GL_POINTS,
	GL_POLYGON_MODE,
	GL_POLYGON_OFFSET,
	GL_POLYGON_OFFSET_FACTOR,
	GL_POLYGON_OFFSET_FILL,
	GL_POLYGON_OFFSET_LINE,
	GL_POLYGON_OFFSET_POINT,
	GL_POLYGON_OFFSET_UNITS,
	GL_POLYGON,
	GL_PROJECTION,
	GL_PROJECTION_MATRIX,
	GL_PROJECTION_STACK_DEPTH,
	GL_QUADS,
	GL_QUAD_STRIP,
	GL_READ_BUFFER,
	GL_RED,
	GL_RED_BITS,
	GL_RENDERER,
	GL_REPLACE,
	GL_REPEAT,
	GL_RGB,
	GL_RGB5,
	GL_RGB5_A1,
	GL_RGB8,
	GL_RGBA,
	GL_RGBA8,
	GL_RGBA_MODE,
	GL_RIGHT,
	GL_SCISSOR_BOX,
	GL_SCISSOR_TEST,
	GL_SHADE_MODEL,
	GL_SHORT,
	GL_SMOOTH,
	GL_SRC_ALPHA,
	GL_SRC_ALPHA_SATURATE,
	GL_SRC_COLOR,
	GL_STACK_OVERFLOW,
	GL_STACK_UNDERFLOW,
	GL_STEREO,
	GL_T2F_C3F_V3F,
	GL_T2F_C4UB_V3F,
	GL_T2F_V3F,
	GL_TABLE_TOO_LARGE,
	GL_TEXTURE_2D,
	GL_TEXTURE_2D_BINDING,
	GL_TEXTURE_COORD_ARRAY,
	GL_TEXTURE_COORD_ARRAY_SIZE,
	GL_TEXTURE_COORD_ARRAY_STRIDE,
	GL_TEXTURE_COORD_ARRAY_TYPE,
	GL_TEXTURE_DOOR_ARRAY_POINTER,
	GL_TEXTURE_ENV,
	GL_TEXTURE_ENV_COLOR,
	GL_TEXTURE_ENV_MODE,
	GL_TEXTURE_MAG_FILTER,
	GL_TEXTURE_MIN_FILTER,
	GL_TEXTURE_PRIORITY,
	GL_TEXTURE_WRAP_S,
	GL_TEXTURE_WRAP_T,
	GL_TEXTURE_GEN_S,
	GL_TEXTURE_GEN_T,
	GL_TRIANGLES,
	GL_TRIANGLE_FAN,
	GL_TRIANGLE_STRIP,
	GL_UNPACK_ALIGNMENT,
	GL_UNPACK_LSB_FIRST,
	GL_UNPACK_ROW_LENGTH,
	GL_UNPACK_SKIP_PIXELS,
	GL_UNPACK_SKIP_ROWS,
	GL_UNPACK_SWAP_BYTES,
	GL_UNSIGNED_BYTE,
	GL_UNSIGNED_INT,
	GL_UNSIGNED_SHORT,
	GL_V2F,
	GL_V3F,
	GL_VENDOR,
	GL_VERSION,
	GL_VERTEX_ARRAY,
	GL_VERTEX_ARRAY_POINTER,
	GL_VERTEX_ARRAY_SIZE,
	GL_VERTEX_ARRAY_STRIDE,
	GL_VERTEX_ARRAY_TYPE,
	GL_VIEWPORT,
	GL_ZERO,
	GL_POINT_SMOOTH,
	GL_CLAMP,
	GL_EXP,
	GL_EXP2,
	GL_TEXTURE_GEN_MODE,
	GL_SPHERE_MAP,
	GL_T,
	GL_S,
	GL_FILL,
	/* Quake2/MiniGLV3D multitexture wire-up (2026-08-15): these 4 used to
	 * be plain auto-numbered enum members (small sequential integers,
	 * whatever position they happened to land at), NOT the real OpenGL
	 * spec hex values (0x84C0-0x84C3). Harmless for any MiniGLV3D-internal
	 * consumer (mtex.c etc.) that only ever references them by NAME, since
	 * GLActiveTextureARB/GLMultiTexCoord2fARB (texture.c/vertexbuffer_min.c)
	 * only ever use them via relative subtraction (`unit - GL_TEXTURE0_ARB`)
	 * -- but fatal for any REAL external GL client (Quake2's qgl_amiga.c
	 * shim) that passes the genuine spec value across the API boundary:
	 * `unit - GL_TEXTURE0_ARB` then subtracts this enum's small native
	 * value from Quake2's real ~33984 value, producing a wildly out-of-
	 * range context->ActiveTexture used to index Texture2D_State[]/
	 * TexEnv[]/textureObjects[] -- corrupting arbitrary nearby memory on
	 * every multitexture call, confirmed as the cause of Quake2's map-
	 * load-time corruption and exit-time crash once real multitexture
	 * was wired up. Fixed at the source: real spec values here so ANY
	 * real GL client (not just this specific shim) gets correct behavior.
	 * GL_MAX_TEXTURE_UNITS_ARB explicitly re-anchored to GL_FILL+1 so
	 * MGL_LOCK_AUTOMATIC/_MANUAL/_SMART (and anything else auto-numbered
	 * after it) keep their ORIGINAL values, unaffected by this fix. */
	GL_TEXTURE0_ARB = 0x84C0,
	GL_TEXTURE1_ARB,
	GL_TEXTURE2_ARB,
	GL_TEXTURE3_ARB,
	GL_MAX_TEXTURE_UNITS_ARB = GL_FILL + 1,
	MGL_LOCK_AUTOMATIC,
	MGL_LOCK_MANUAL,
	MGL_LOCK_SMART,
	MGL_FLATFAN,
	MGL_FLATSTRIP,
	MGL_PERSPECTIVE_MAPPING,
	MGL_W_ONE_HINT,
	MGL_Z_OFFSET,
	/* Positional value, kept as-is: MiniGLV3D-internal callers reference it
	 * by name only. A REAL external GL client passes the shared-palette
	 * target below instead, which carries the genuine spec value -- same
	 * distinction (and the same trap) as GL_TEXTURE0_ARB above. */
	GL_COLOR_TABLE,
	MGL_UBYTE_BGRA,
	MGL_UBYTE_ARGB,
	MGL_UNSIGNED_SHORT_5_6_5,
	MGL_UNSIGNED_SHORT_4_4_4_4,
	MGL_FIXPOINTTRANS_HINT,
	MGL_ARRAY_TRANSFORMATIONS,
	/* APPEND ONLY, and this is why: the enum above is auto-numbered, so a
	 * name inserted in alphabetical order silently renumbers every constant
	 * after it and breaks every object file and prebuilt archive compiled
	 * against the older header. GL_POINT_SMOOTH through MGL_* were all
	 * appended for the same reason. Do not insert; add here.
	 *
	 * GL_COLOR_WRITEMASK (2026-09-12): the state has been kept since the
	 * colour-mask work (ColorMaskR..A) but the query could not be NAMED, so
	 * glGetBooleanv(GL_COLOR_WRITEMASK) did not compile. others.c answers it
	 * with all four elements. */
	GL_COLOR_WRITEMASK,
	/* GL_LINE_WIDTH (2026-09-12, audit fix 4): glLineWidth is ported now,
	 * so the width it sets can be read back. Appended, per the rule above. */
	GL_LINE_WIDTH,
	/* Texture coordinate generation, 2026-09-12 (audit fix 7). These carry
	 * their REAL GL VALUES rather than the next positional numbers, for the
	 * same reason GL_TEXTURE0_ARB does above: a client with its own GL
	 * headers passes 0x2400/0x2401/0x2501/0x2502 across the boundary, and a
	 * positional value here would silently mean something else. GL_SPHERE_MAP
	 * and GL_TEXTURE_GEN_MODE above keep their historical positional values,
	 * so the driver accepts BOTH spellings of those two (texture.c).
	 *
	 * NOTE for the next person appending: the auto-numbering STOPS here. A
	 * name added after these continues from 0x2503, so give it an explicit
	 * value or re-anchor it the way GL_MAX_TEXTURE_UNITS_ARB does. */
	GL_EYE_LINEAR    = 0x2400,
	GL_OBJECT_LINEAR = 0x2401,
	GL_OBJECT_PLANE  = 0x2501,
	GL_EYE_PLANE     = 0x2502,

	/* GL_TEXTURE, the third matrix mode (2026-09-19). Base MiniGL never had it
	 * and neither did we, so glMatrixMode(GL_TEXTURE) was silently accepted and
	 * then corrupted two other matrices -- see matrix.c's GLMatrixMode.
	 *
	 * APPENDED, and with an EXPLICIT value, for two separate reasons.
	 * Appended because this enum is auto-numbered and positional: a third-party
	 * client compiles against base MiniGL's own gl.h and passes ITS numbers at
	 * runtime, so inserting a name in alphabetical position would shift every
	 * later member and break every such client. Verified 2026-09-19: base
	 * MiniGL has 230 members, we had 236, and all six of ours are appended, so
	 * the shared prefix matches value for value.
	 * Explicit because the note above says auto-numbering stops at GL_EYE_PLANE.
	 *
	 * 0x1702 is the REAL OpenGL value for GL_TEXTURE, which costs nothing here
	 * (no collision: the auto-numbered members occupy 0..~236 and the explicit
	 * ones are 0x2400+/0x84C0) and buys something -- an application built
	 * against standard GL headers passes 0x1702, and now reaches the right
	 * matrix instead of quietly wrecking the projection. */
	GL_TEXTURE       = 0x1702,

	/* The two read-back queries that go with it, matching what the other two
	 * matrix modes already answer in others.c (GL_MODELVIEW_MATRIX /
	 * GL_MODELVIEW_STACK_DEPTH and the projection pair). Real GL values, same
	 * reasoning as GL_TEXTURE above; no collision with the auto-numbered
	 * members, which stop around 236. */
	GL_TEXTURE_MATRIX      = 0x0BA8,
	GL_TEXTURE_STACK_DEPTH = 0x0BA5,
};

/* The correct spelling of the enum member above that reads
 * GL_TEXTURE_DOOR_ARRAY_POINTER -- a typo inherited from base MiniGL's header,
 * which left glGetPointerv's texcoord query unnameable by its GL name. An
 * alias rather than a rename, so the member keeps its position and value and
 * the old spelling still compiles. Added 2026-09-12 with glGetPointerv. */
#define GL_TEXTURE_COORD_ARRAY_POINTER GL_TEXTURE_DOOR_ARRAY_POINTER

#define GL_COLOR_BUFFER_BIT     0x00000001
#define GL_DEPTH_BUFFER_BIT     0x00000002

#define GL_TRUE                 1
#define GL_FALSE                0

#define MGL_BUTTON_LEFT         0x00000001
#define MGL_BUTTON_RIGHT        0x00000002
#define MGL_BUTTON_MID          0x00000004

#define MGL_SM_BESTMODE         0xFFFFFFFF
#define MGL_SM_WINDOWMODE       0x00000000

typedef struct MGLColor_t
{
	GLfloat r,g,b,a;
} MGLColor;

//Surgeon: w-coord added (currently used for padding)
typedef struct MGLNormal_t
{
	GLfloat x,y,z,w;
} MGLNormal;

#include "mgl/vertexbuffer.h"
#include "mgl/context.h"
#include "mgl/clip.h"
#include "mgl/modes.h"

/*
	The current context is refered to as an extern variable, which
	is a pointer to the context.
*/

extern GLcontext mini_CurrentContext;

#ifndef GLNDEBUG
	#define GLASSERT(c) assert(c)
	#define dprintf(x) printf x
#else
	#define GLASSERT(c)
	#define dprintf(x)
#endif

/*
	GLFlagError records an error for glGetError when `c` is true, keeping
	the first one until glGetError reads it, as GL specifies.

	Since 2026-09-13 (GL_NOERRORCHECK no longer defined in config.h) it
	neither returns nor prints. Base MiniGL's version did both, but its
	return was compiled out here for years, so every call site stops
	explicitly -- several in functions that return a value -- and a game
	repeating a refused call every frame must not flood kprintf.
*/
#ifndef GL_NOERRORCHECK
	#define GLFlagError(context,c,err) do {\
		if ((c) && (context)->CurrentError == GL_NO_ERROR)\
			(context)->CurrentError = (err);\
	} while (0)
#else
	#define GLFlagError(context,c,err)
#endif


/*
	Prototypes and appropriate defines
	These are derived from the OpenGL manpages
	Some defines are duplicated with EXT suffix, to be compatible.
	Additionally, some of these may not be needed (Maybe glBegin).
	There may also be a problem with floating point parameters for
	certain compilers. May be addressed in the macros.
*/

void        GLActiveTextureARB(GLcontext context, GLenum unit);
void        GLMultiTexCoord2fARB(GLcontext context, GLenum unit, GLfloat s, GLfloat t);
void        GLMultiTexCoord2fvARB(GLcontext context, GLenum unit, GLfloat *v);
void        GLClientActiveTextureARB(GLcontext context, GLenum unit);

void MGLDrawMultitexBuffer (GLcontext context, GLenum BSrc, GLenum BDst, GLenum TexEnv);

/*
 * Phase L (glquake full build/link): mglq3-specific extensions, not in
 * the original base MiniGL -- glquake calls the bare mglSetPointer()/
 * mglClearPointer() macros directly (gl_vidamiga.c, menu.c). See
 * context.c's own MGLSetPointer/MGLClearPointer comment for the
 * implementation (thin wrappers around vid_Pointer/vid_DeletePointer,
 * already existed internally).
 */
void MGLSetPointer(GLcontext context);
void MGLClearPointer(GLcontext context);



/* Eleven prototypes below were commented out while their gl* macros stayed
 * live, so a call compiled and then failed to link on an internal GL* name --
 * glGetPointerv's prototype was live too, so it compiled without so much as a
 * warning. All eleven are implemented as of 2026-09-12 (GL 1.1 audit item 10):
 * GLAreTexturesResident, GLEdgeFlag, GLEdgeFlagPointer, GLEdgeFlagv,
 * GLGetDoublev, GLGetPointerv, GLIndexi, GLIndexiv, GLIndexPointer,
 * GLPrioritizeTextures and GLReadBuffer. GLEdgeFlagPointer takes GL 1.1's
 * `const GLvoid *`, not the `const GLboolean *` of the old commented line:
 * GLboolean is 4 bytes in this header, so that parameter would draw an
 * incompatible-pointer warning for a real GL one-byte boolean array. */
void        GLAlphaFunc(GLcontext context, GLenum func, GLclampf ref);
GLboolean   GLAreTexturesResident(GLcontext context, GLsizei n, const GLuint *textures, GLboolean *residences);
void        GLArrayElement(GLcontext context, GLint i);
void        GLBegin(GLcontext context, GLenum mode);
void        GLBindTexture(GLcontext context, GLenum target, GLuint texture);
void        GLBlendFunc(GLcontext context, GLenum sfactor, GLenum dfactor);
/* Added 2026-09-08. BlendCfg() always took independent colour/alpha triples,
 * so these are pass-through, not new hardware paths. A non-ADD equation
 * forces the draw onto hardware blend -- every software blend shader hardcodes
 * ADD math; see gl_EmitCullBlendState. */
void        GLBlendEquation(GLcontext context, GLenum mode);
void        GLBlendFuncSeparate(GLcontext context, GLenum srcRGB, GLenum dstRGB,
                                GLenum srcAlpha, GLenum dstAlpha);
void        GLColorMask(GLcontext context, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
void        GLClear(GLcontext context, GLbitfield mask);
void        GLClearColor(GLcontext context, GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
void        GLClearDepth(GLcontext context, GLclampd depth);
void        GLColor3fv(GLcontext context, GLfloat *v);
void        GLColor3ubv(GLcontext context, GLubyte *v);
void        GLColor4f(GLcontext context, GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void        GLColor4fv(GLcontext context, GLfloat *v);
void        GLColor4ub(GLcontext context, GLubyte red, GLubyte green, GLubyte blue, GLubyte alhpa);
void        GLColor4ubv(GLcontext context, GLubyte *v);
//void        GLColorPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void        GLColorTable(GLcontext context, GLenum target, GLenum internalformat, GLint width, GLenum format, GLenum type, GLvoid *data);
void        GLCullFace(GLcontext context, GLenum mode);
void        GLDeleteTextures(GLcontext context, GLsizei n, const GLuint *textures);
void        GLDepthFunc(GLcontext context, GLenum func);
void        GLDepthMask(GLcontext context, GLboolean flag);
void        GLDepthRange(GLcontext context, GLclampd n, GLclampd f);
void        GLDisableClientState(GLcontext context, GLenum cap);
//void        GLDrawArrays(GLcontext context, GLenum mode, GLint first, GLsizei count);
void        GLDrawBuffer(GLcontext context, GLenum mode);
//void        GLDrawElements(GLcontext context, GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
void        GLEdgeFlag(GLcontext context, GLboolean flag);
void        GLEdgeFlagPointer(GLcontext context, GLsizei stride, const GLvoid *pointer);
void        GLEdgeFlagv(GLcontext context, const GLboolean *flag);
void        GLEnableClientState(GLcontext context, GLenum cap);

void        GLInterleavedArrays(GLcontext context, GLenum format, GLsizei stride, const GLvoid *pointer);

void        GLEnd(GLcontext context);
void        GLFinish(GLcontext context);
void        GLFlush(GLcontext context);
void        GLFogf(GLcontext context, GLenum pname, GLfloat param);
void        GLFogfv(GLcontext context, GLenum pname, GLfloat *param);
void        GLFrontFace(GLcontext context, GLenum mode);
void        GLFrustum(GLcontext context, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
void        GLGenTextures(GLcontext context, GLsizei n, GLuint *textures);
void        GLGetBooleanv(GLcontext context, GLenum pname, GLboolean *params);
void        GLGetDoublev(GLcontext context, GLenum pname, GLdouble *params);
GLenum      GLGetError(GLcontext context);
void        GLGetFloatv(GLcontext context, GLenum pname, GLfloat *params);
void        GLGetIntegerv(GLcontext context, GLenum pname, GLint *params);
void        GLGetPointerv(GLcontext context, GLenum pname, GLvoid* *params);
const GLubyte* GLGetString(GLcontext context, GLenum name);
void        GLHint(GLcontext context, GLenum target, GLenum mode);
void        GLIndexi(GLcontext context, GLint c);
void        GLIndexiv(GLcontext context, const GLint *c);
void        GLIndexPointer(GLcontext context, GLenum type, GLsizei stride, const GLvoid *pointer);
//void        GLInterleavedArrays(GLcontext context, GLenum format, GLsizei stride, const GLvoid *pointer);
GLboolean   GLIsEnabled(GLcontext context, GLenum cap);
/* Implemented 2026-09-08. The prototype was commented out while the
 * glIsTexture() macro below stayed live, so any caller compiled but failed to
 * link on an undefined GLIsTexture -- worse than an honest omission. */
GLboolean   GLIsTexture(GLcontext context, GLuint texture);
void        GLLoadIdentity(GLcontext context);
void        GLLoadMatrixd(GLcontext context, const GLdouble *m);
void        GLLoadMatrixf(GLcontext context, const GLfloat *m);
void        GLMatrixMode(GLcontext context, GLenum mode);
void        GLMultMatrixd(GLcontext context, const GLdouble *m);
void        GLMultMatrixf(GLcontext context, const GLfloat *m);
void        GLNormal3f(GLcontext context, GLfloat x, GLfloat y, GLfloat z);
void        GLOrtho(GLcontext context, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
void        GLPixelStorei(GLcontext context, GLenum pname, GLint param);
void        GLPolygonMode(GLcontext context, GLenum face, GLenum mode);
void        GLPolygonOffset(GLcontext context, GLfloat factor, GLfloat units);
//surgeon:
void	      GLPointSize(GLcontext context, GLfloat size);
void	      GLLineWidth(GLcontext context, GLfloat width);

void        GLPopMatrix(GLcontext context);
void        GLPrioritizeTextures(GLcontext context, GLsizei n, const GLuint *textures, const GLclampf *priorities);
void        GLPushMatrix(GLcontext context);
void        GLReadBuffer(GLcontext context, GLenum mode);
void        GLReadPixels(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels);
void        GLRotated(GLcontext context, GLdouble angle, GLdouble x, GLdouble y, GLdouble z);
void        GLRotatef(GLcontext context, GLfloat angle, GLfloat x, GLfloat y, GLfloat z);

//simplified rotation-routine
//first 3 matches the values of corresponding matrix flags

#define GLROT_001		0x800
#define GLROT_010		0x1000
#define GLROT_100		0x2000

#define GLROT_011		0x4000
#define GLROT_101		0x8000
#define GLROT_110		0x10000
#define GLROT_111		0x20000

void        GLRotatefEXT(GLcontext context, GLfloat angle, const GLint xyz);

void        GLRotatefEXTs(GLcontext context, GLfloat sin_an, GLfloat cos_an, const GLint xyz);

void        GLScaled(GLcontext context, GLdouble x, GLdouble y, GLdouble z);
void        GLScalef(GLcontext context, GLfloat x, GLfloat y, GLfloat z);
void        GLScissor(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height);
void        GLShadeModel(GLcontext context, GLenum mode);
void        GLTexCoord2f(GLcontext context, GLfloat s, GLfloat t);
void        GLTexCoord2fv(GLcontext context, GLfloat *v);
void        GLTexCoord4f(GLcontext context, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
void        GLTexCoord4fv(GLcontext context, GLfloat *v);
//void        GLTexCoordPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void        GLTexEnvi(GLcontext context, GLenum target, GLenum pname, GLint param);
void        GLTexGeni(GLcontext context, GLenum coord, GLenum mode, GLenum map);
void        GLCopyTexSubImage2D(GLcontext context, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);
void        GLCopyTexImage2D(GLcontext context, GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
void        GLTexGenfv(GLcontext context, GLenum coord, GLenum pname, const GLfloat *params);
void        GLTexImage2D(GLcontext context, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void        GLTexParameteri(GLcontext context, GLenum target, GLenum pname, GLint param);
void        GLTexSubImage2D(GLcontext context, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
void        GLTranslated(GLcontext context, GLdouble x, GLdouble y, GLdouble z);
void        GLTranslatef(GLcontext context, GLfloat x, GLfloat y, GLfloat z);
void        GLVertex4f(GLcontext context, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void        GLVertex2f(GLcontext context, GLfloat x, GLfloat y);

void        GLVertex4fv(GLcontext context, GLfloat *v);
void        GLVertex3fv(GLcontext context, GLfloat *v);
void        GLVertex2fv(GLcontext context, GLfloat *v);

//void        GLVertexPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void        GLViewport(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height);

void		GLLockArrays(GLcontext context, GLuint first, GLsizei count);
void		GLUnlockArrays(GLcontext context);

void        mglChooseGuardBand(GLboolean flag);
void        mglChooseNumberOfBuffers(int number);
/* GL_EXT_shared_texture_palette's target for glColorTableEXT. REAL SPEC VALUE
 * (0x81FB), deliberately a #define rather than a member of the enum above:
 * external GL clients pass the genuine value across the API boundary, and this
 * header's enum is positionally numbered. Getting that wrong is exactly the
 * GL_TEXTURE0_ARB bug documented near GL_COLOR_TABLE -- it corrupted memory on
 * every multitexture call until the values were re-anchored. */
#define GL_SHARED_TEXTURE_PALETTE_EXT 0x81FB

/* glBlendEquation modes, added 2026-09-08. REAL SPEC VALUES, and #defines
 * rather than members of the enum above for exactly the reason spelled out
 * for GL_SHARED_TEXTURE_PALETTE_EXT: that enum is positionally numbered and
 * alphabetically ordered, so inserting GL_FUNC_ADD into it would renumber
 * every member after it -- silently changing GL_SRC_ALPHA's value for any
 * client already compiled against the old header while minigl.library kept
 * the new one. All five map onto real V3D blend modes (V3D_BLEND_MODE_ADD/
 * SUB/RSUB/MIN/MAX); the hardware also has MUL/SCREEN/DARKEN/LIGHTEN, which
 * core GL has no way to name. */
#define GL_FUNC_ADD                   0x8006
#define GL_MIN                        0x8007
#define GL_MAX                        0x8008
#define GL_BLEND_EQUATION             0x8009
#define GL_FUNC_SUBTRACT              0x800A
#define GL_FUNC_REVERSE_SUBTRACT      0x800B

void        mglChoosePixelDepth(int depth);       /* SCREEN bits-per-pixel */
/* Z-BUFFER precision in bits -- NOT the screen depth above. 32 = D32F
 * (default), 16 = D16. Call before MGLCreateContext; the Z buffer is sized
 * from this at context init. Any other value is treated as 32. D16 is a
 * measured downgrade -- see the implementation comment in gl/src/context.c. */
#define MGL_ZBUFFER_32F  32
#define MGL_ZBUFFER_16   16
void        mglChooseZBufferDepth(int bits);
void        mglChooseTextureBufferSize(int size);
void        mglChooseVertexBufferSize(int size);
void        mglChooseMtexBufferSize(int size);
void        mglChooseWindowMode(GLboolean flag);
void *      MGLCreateContext(int offx, int offy, int w, int h);
void        MGLDeleteContext(GLcontext context);
void        MGLEnableSync(GLcontext context, GLboolean enable);
void        MGLExit(GLcontext context);
void *      MGLGetWindowHandle(GLcontext context);
void *      MGLGetInputWindowHandle(GLcontext context);
void        MGLIdleFunc(GLcontext context, IdleFn i);
void        MGLKeyFunc(GLcontext context, KeyHandlerFn k);
GLboolean   MGLLockDisplay(GLcontext context);
void        MGLMainLoop(GLcontext context);
void	      MGLMinTriArea(GLcontext context, GLfloat area);
void        MGLMouseFunc(GLcontext context, MouseHandlerFn m);
void        mglProhibitAlphaFallback(GLboolean flag);
void        mglProhibitMipMapping(GLboolean flag);
void        mglProposeCloseDesktop(GLboolean closeme);
void        MGLResizeContext(GLcontext context, GLsizei width, GLsizei height);
void        MGLSetState(GLcontext context, GLenum cap, GLboolean state);
void        MGLSpecialFunc(GLcontext context, SpecialHandlerFn s);
void        MGLSwitchBuffer(GLcontext context, int bufnr);
void        MGLSwitchDisplay(GLcontext context);
void        MGLTexMemStat(GLcontext context, GLint *Current, GLint *Peak);
void        MGLUnlockDisplay(GLcontext context);
void        MGLWriteShotPPM(GLcontext context, char *filename);
GLboolean        MGLInit(void);
void        MGLTerm(void);

#ifdef AUTOMATIC_LOCKING_ENABLE
void        MGLLockMode(GLcontext context, GLenum lockMode);
#endif

void        MGLPrintMatrix(GLcontext context, int mode);
void        MGLPrintMatrixStack(GLcontext context, int mode);

void        MGLSetZOffset(GLcontext context, GLfloat offset);

void        GLULookAt(GLfloat ex, GLfloat ey, GLfloat ez, GLfloat cx, GLfloat cy, GLfloat cz, GLfloat ux, GLfloat uy, GLfloat uz);
void        GLUPerspective(GLfloat fovy, GLfloat aspect, GLfloat znear, GLfloat zfar);

GLint       mglGetSupportedScreenModes(MGLScreenModeCallback CallbackFn);
void *      MGLCreateContextFromID(GLint ID, GLint *w, GLint *h);
GLboolean   MGLLockBack(GLcontext context, MGLLockInfo *info);

/*
 * Render into a window the APPLICATION already opened, instead of the driver
 * opening its own (2026-09-20). For hosts that own their window -- SDL, or any
 * AmigaOS program with its own event loop and layout.
 *
 * `window` must stay open for the life of the context; the driver never closes
 * it and never unlocks its screen. Size comes from the window's INNER area, so
 * borders and any gadgets are excluded. Everything after creation behaves
 * exactly like an ordinary windowed context: the per-frame present is the same
 * ClipBlit, and MGLDeleteContext frees the off-screen bitmap, rastport and
 * backend state while leaving the window itself alone.
 *
 * Returns NULL on failure, like MGLCreateContext.
 *
 * SHUTDOWN ORDER: close your window BEFORE calling MGLTerm, or open your own
 * IntuitionBase.
 *
 * Not because the library goes away -- Intuition is ROM-resident and
 * CloseLibrary only decrements lib_OpenCnt. It is that MGLTerm then does
 * `IntuitionBase = NULL` (init.c), and proto/intuition.h's inline stubs load
 * that global into A6 and jsr off it. A host resolving CloseWindow through the
 * driver's base is therefore calling through a null pointer, on a call it has
 * every reason to think is safe. Found by milestone_l52_context_from_window
 * build 1, where it read as a driver teardown bug until the log showed both
 * contexts had already been deleted cleanly.
 *
 * Declared here as a PROTOTYPE, deliberately nowhere near gl.h's enum: that
 * enum is sequential and auto-numbered, its integers cross the library
 * boundary, and inserting into it would silently renumber every later value for
 * any client compiled against an older header.
 */
void *      MGLCreateContextFromWindow(struct Window *window);

/*
 * Render into a bitmap the APPLICATION owns, and presume nothing about how it
 * reaches the screen (2026-09-20).
 *
 * The difference from MGLCreateContextFromWindow is WHO PRESENTS.
 * MGLCreateContextFromWindow allocates its own off-screen bitmap and ClipBlits
 * it into the window on every mglSwitchDisplay. This one renders straight into
 * the caller's bitmap and blits NOTHING: mglSwitchDisplay finishes the frame
 * and returns, leaving presentation entirely to the host.
 *
 * That is the point of it. A program already presenting by its own means -- one
 * compositing several renderers into a window, or driving its own double
 * buffering -- cannot use the window form, because two libraries blitting into
 * one window is last-writer-wins and the loser's frame vanishes. Here nobody
 * competes: we fill the bitmap, the host decides what happens to it.
 *
 * REQUIREMENTS on `bitmap`: FOUR BYTES PER PIXEL, and CGX-lockable
 * (LockBitMapTags must yield a base address and bytes-per-row). V3D writes
 * RGBA8 directly and this driver has no format-conversion step, so a 16-bit
 * bitmap renders garbage rather than failing cleanly. Both are checked and the
 * call refused. AllocBitMap(w, h, 32, BMF_MINPLANES|BMF_DISPLAYABLE, friend) is
 * what the driver allocates for itself and is what to pass.
 *
 * Note this is a BYTES-PER-PIXEL requirement, not a depth one. A truecolour
 * bitmap reports GetBitMapAttr(BMA_DEPTH) == 24 on CGX -- the eighth byte is
 * alpha, and alpha is not depth -- so "32 bits deep" is the wrong thing to ask
 * for and the wrong thing to test. The check uses bytes-per-row instead.
 *
 * The bitmap must outlive the context, and the driver never frees it. Size is
 * taken from the bitmap itself.
 *
 * Returns NULL on failure. The MGLTerm shutdown-order note above applies here
 * too if the host uses the driver's IntuitionBase.
 *
 * NOTE this is NEW API with no precedent -- unlike MGLCreateContextFromWindow,
 * which matches V18's name and signature, nothing existing expects it.
 */
void *      MGLCreateContextFromBitMap(struct BitMap *bitmap);

void        GLEnableClientState(GLcontext context, GLenum state);
void        GLDisableClientState(GLcontext context, GLenum state);

void        GLTexCoordPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void        GLColorPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void        GLVertexPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);

void        GLDrawElements(GLcontext context, GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
void        GLDrawArrays(GLcontext context, GLenum mode, GLint first, GLsizei count);
void        GLMultiDrawArrays(GLcontext context, GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount);

/*
** These macros define the OpenGL compatibility macros. If you don't want them,
** define USE_MGLAPI before including this file.
*/


#ifdef USE_MGLAPI
	#include "mgl/minigl.h"

#else

#define glActiveTextureARB(unit)                GLActiveTextureARB(mini_CurrentContext, unit)

#define glClientActiveTextureARB(unit)           GLClientActiveTextureARB(mini_CurrentContext, unit)

#define glMultiTexCoord2fARB(a, u, v)           GLMultiTexCoord2fARB(mini_CurrentContext, a, u, v)

#define glMultiTexCoord2fvARB(a, v)             GLMultiTexCoord2fvARB(mini_CurrentContext, a,  v)

#define mglDrawMultitexBuffer(bsrc, bdst, env) MGLDrawMultitexBuffer(mini_CurrentContext, bsrc, bdst, env)

#define mglSetPointer() MGLSetPointer(mini_CurrentContext)
#define mglClearPointer() MGLClearPointer(mini_CurrentContext)

#define glAlphaFunc(func, ref) GLAlphaFunc(mini_CurrentContext, func, ref)

#define glColorMask(r, g, b, a) GLColorMask(mini_CurrentContext, r, g, b, a)

#define glAreTexturesResident(n, textures, residences) GLAreTexturesResident(mini_CurrentContext, n, textures, residences)

#define glArrayElement(i) GLArrayElement(mini_CurrentContext, i)

#define glArrayElementEXT(i) GLArrayElement(mini_CurrentContext, i)

#define glBegin(mode) GLBegin(mini_CurrentContext, mode)
#define glEnd() GLEnd(mini_CurrentContext)

#define glTexGeni(coord,mode,map) GLTexGeni(mini_CurrentContext, coord, mode, map)

#define glCopyTexSubImage2D(target,level,xoffset,yoffset,x,y,width,height) GLCopyTexSubImage2D(mini_CurrentContext, target, level, xoffset, yoffset, x, y, width, height)

#define glCopyTexImage2D(target,level,internalformat,x,y,width,height,border) GLCopyTexImage2D(mini_CurrentContext, target, level, internalformat, x, y, width, height, border)

#define glTexGenfv(coord,pname,params) GLTexGenfv(mini_CurrentContext, coord, pname, params)

#define glBindTexture(target, texture) GLBindTexture(mini_CurrentContext, target, texture)

#define glBlendFunc(sfactor, dfactor) GLBlendFunc(mini_CurrentContext, sfactor, dfactor)

#define glBlendEquation(mode) GLBlendEquation(mini_CurrentContext, mode)

#define glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha) \
	GLBlendFuncSeparate(mini_CurrentContext, srcRGB, dstRGB, srcAlpha, dstAlpha)

#define glClear(mask) GLClear(mini_CurrentContext, mask)

#define glClearColor(red, green, blue, alpha) GLClearColor(mini_CurrentContext, red, green, blue, alpha)

#define glClearDepth(depth) GLClearDepth(mini_CurrentContext, depth)

#define glColorPointer(size, type, stride, pointer) GLColorPointer(mini_CurrentContext, size, type, stride, pointer)

#define glColorPointerEXT(size, type, stride, pointer) GLColorPointer(mini_CurrentContext, size, type, stride, pointer)

#define glColorTable(target, internalformat, width, format, type, data) GLColorTable(mini_CurrentContext, target, internalformat, width, format, type, data)

#define glCullFace(mode) GLCullFace(mini_CurrentContext, mode)

#define glDeleteTextures(n, textures) GLDeleteTextures(mini_CurrentContext, n, textures)

#define glDepthFunc(func) GLDepthFunc(mini_CurrentContext, func)

#define glDepthMask(flag) GLDepthMask(mini_CurrentContext, flag)

#define glEnable(cap) MGLSetState(mini_CurrentContext, cap, GL_TRUE)

#define glDisable(cap) MGLSetState(mini_CurrentContext, cap, GL_FALSE)

#define glDisableClientState(cap) GLDisableClientState(mini_CurrentContext, cap)

#define glEnableClientState(cap) GLEnableClientState(mini_CurrentContext, cap)

#define glInterleavedArrays(format, stride, pointer) GLInterleavedArrays(mini_CurrentContext, format, stride, pointer)

#define glDrawArrays(mode, first, count) GLDrawArrays(mini_CurrentContext, mode, first, count)

#define glDrawArraysEXT(mode, first, count) GLDrawArrays(mini_CurrentContext, mode, first, count)

#define glMultiDrawArrays(mode, first, count, primcount) GLMultiDrawArrays(mini_CurrentContext, mode, first, count, primcount)

#define glDrawBuffer(mode) GLDrawBuffer(mini_CurrentContext, mode)

#define glDrawElements(mode, count, type, indices) GLDrawElements(mini_CurrentContext, mode, count, type, indices)

#define glEdgeFlag(flag) GLEdgeFlag(mini_CurrentContext, flag)

#define glEdgeFlagv(flag) GLEdgeFlagv(mini_CurrentContext, flag)

#define glEdgeFlagPointer(stride, flags) GLEdgeFlagPointer(mini_CurrentContext, stride, flags)

#define glEdgeFlagPointerEXT(stride, flags) GLEdgeFlagPointer(mini_CurrentContext, stride, flags)

#define glFinish() GLFinish(mini_CurrentContext)

#define glFlush() GLFlush(mini_CurrentContext)

#define glFogf(pname, param) GLFogf(mini_CurrentContext, pname, param)

#define glFogfv(pname, param) GLFogfv(mini_CurrentContext, pname, param)

#define glFogi(pname, param) glFogf(pname, (GLfloat)param)
#define glFrontFace(mode) GLFrontFace(mini_CurrentContext, mode)

#define glFrustum(left, right, bottom, top, zNear, zFar) GLFrustum(mini_CurrentContext, left, right, bottom, top, zNear, zFar)

#define glGenTextures(n, textures) GLGenTextures(mini_CurrentContext, n, textures)

#define glGetError() GLGetError(mini_CurrentContext)

#define glGetBooleanv(pname, params) GLGetBooleanv(mini_CurrentContext, pname, params);

#define glGetDoublev(pname, params) GLGetDoublev(mini_CurrentContext, pname, params)

#define glGetFloatv(pname, params) GLGetFloatv(mini_CurrentContext, pname, params)

#define glGetIntegerv(pname, params) GLGetIntegerv(mini_CurrentContext, pname, params)

#define glGetPointerv(pname, params) GLGetPointerv(mini_CurrentContext, pname, params)

#define glGetPointervEXT(pname, params) GLGetPointerv(mini_CurrentContext, pname, params)

#define glGetString(name) GLGetString(mini_CurrentContext, name)

#define glHint(target, mode) GLHint(mini_CurrentContext, target, mode)

#define glIndexi(c) GLIndexi(mini_CurrentContext, c)

/* Was GLIndexfv -- a function that exists in no MiniGL, handed an int pointer.
 * Inherited from base MiniGL's header; corrected 2026-09-12 with the function. */
#define glIndexiv(c) GLIndexiv(mini_CurrentContext, c)

#define glIndexPointer(type, stride, pointer) GLIndexPointer(mini_CurrentContext, type, stride, pointer)

#define glIndexPointerEXT(type, stride, pointer) GLIndexPointer(mini_CurrentContext, type, stride, pointer)

#define glInterleavedArrays(format, stride, pointer) GLInterleavedArrays(mini_CurrentContext, format, stride, pointer)

#define glIsEnabled(cap) GLIsEnabled(mini_CurrentContext, cap)

#define glIsTexture(texture) GLIsTexture(mini_CurrentContext, texture)

#define glLoadIdentity() GLLoadIdentity(mini_CurrentContext)

#define glLoadMatrixf(m) GLLoadMatrixf(mini_CurrentContext, m)

#define glLoadMatrixd(m) GLLoadMatrixd(mini_CurrentContext, m)

#define glMatrixMode(mode) GLMatrixMode(mini_CurrentContext, mode)

#define glMultMatrixd(m) GLMultMatrixd(mini_CurrentContext, m)

#define glMultMatrixf(m) GLMultMatrixf(mini_CurrentContext, m)

#define glOrtho(left, right, bottom, top, zNear, zFar) GLOrtho(mini_CurrentContext, left, right, bottom, top, zNear, zFar)

#define glPixelStorei(pname, param) GLPixelStorei(mini_CurrentContext, pname, param)

#define glPixelStoref(pname, param) GLPixelStorei(mini_CurrentContext, pname, (int)(param))

#define glPolygonMode(face, mode) GLPolygonMode(mini_CurrentContext, face, mode)

#define glPolygonOffset(factor, units) GLPolygonOffset(mini_CurrentContext, factor, units)

#define glPointSize(s) GLPointSize(mini_CurrentContext, s)

#define glLineWidth(w) GLLineWidth(mini_CurrentContext, w)

#define glPushMatrix() GLPushMatrix(mini_CurrentContext)

#define glPopMatrix() GLPopMatrix(mini_CurrentContext)

#define glPrioritizeTextures(n, textures, pri) GLPrioritizeTextures(mini_CurrentContext, n, textures, pri)

#define glReadBuffer(mode) GLReadBuffer(mini_CurrentContext, mode)

#define glReadPixels(x, y, width, height, format, type, pixels) GLReadPixels(mini_CurrentContext, x, y, width, height, format, type, pixels)

#define glRotated(angle, x, y, z) GLRotated(mini_CurrentContext, angle, x, y, z)

#define glRotatef(angle, x, y, z) GLRotatef(mini_CurrentContext, (angle), (x), (y), (z))

#define glRotatefEXT(angle, xyz) GLRotatefEXT(mini_CurrentContext, angle, xyz)

#define glRotatefEXTs(sin_an, cos_an, xyz) GLRotatefEXTs(mini_CurrentContext, sin_an, cos_an, xyz)

#define glScaled(x, y, z) GLScaled(mini_CurrentContext, x, y, z)

#define glScalef(x, y, z) GLScaled(mini_CurrentContext, (GLdouble)(x), (GLdouble)(y), (GLdouble)(z))

#define glScissor(x, y, w, h) GLScissor(mini_CurrentContext, x, y, w, h)

#define glShadeModel(mode) GLShadeModel(mini_CurrentContext, mode)

#define glTexCoord2f(s, t) GLTexCoord2f(mini_CurrentContext, s, t)

#define glTexCoord2fv(v) GLTexCoord2fv(mini_CurrentContext, v)

#define glTexCoord4f(s, t, r, q) GLTexCoord4f(mini_CurrentContext, s, t, r, q)

#define glTexCoord4fv(v) GLTexCoord4fv(mini_CurrentContext, v)

#define glTexCoordPointer(size, type, stride, pointer) GLTexCoordPointer(mini_CurrentContext, size, type, stride, pointer)

#define glTexCoordPointerEXT(size, type, stride, pointer) GLTexCoordPointer(mini_CurrentContext, size, type, stride, pointer)

#define glTexImage2D(target, level, internal, width, height, border, format, type, pixels) GLTexImage2D(mini_CurrentContext, target, level, internal, width, height, border, format, type, pixels)

#define glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels) GLTexSubImage2D(mini_CurrentContext, target, level, xoffset, yoffset, width, height, format, type, pixels)

#define glTranslated(x, y, z) GLTranslated(mini_CurrentContext, x, y, z)

#define glTranslatef(x, y, z) GLTranslatef(mini_CurrentContext, x, y, z)

#define glViewport(x, y, width, height) GLViewport(mini_CurrentContext, x, y, width, height)

#define glVertex4f(x,y,z,w) GLVertex4f(mini_CurrentContext, x,y,z,w)

#define glVertex3f(x,y,z)   GLVertex4f(mini_CurrentContext, x,y,z,    1.f)

#define glVertex2f(x,y)     GLVertex2f(mini_CurrentContext, x,y)

#define glVertex2i(x,y)     GLVertex2f(mini_CurrentContext, (GLfloat)x, (GLfloat)y)

#define glVertex4fv(v)      GLVertex4fv(mini_CurrentContext, v);
#define glVertex3fv(v)      GLVertex3fv(mini_CurrentContext, v);
#define glVertex2fv(v)     GLVertex2fv(mini_CurrentContext, v)


#define glVertexPointer(size, type, stride, pointer) GLVertexPointer(mini_CurrentContext, size, type, stride, pointer)

#define glVertexPointerEXT(size, type, stride, pointer) GLVertexPointer(mini_CurrentContext, size, type, stride, pointer)

#define glDepthRange(n,f) GLDepthRange(mini_CurrentContext, n,f)

#define glLockArrays(f,c) GLLockArrays(mini_CurrentContext, f,c)
#define glUnlockArrays() GLUnlockArrays(mini_CurrentContext)

/*
#define mglCreateContext(offx, offy, w, h) mini_CurrentContext = MGLCreateContext(offx, offy, w,h)
*/

//Olivier Fabre
#define mglCreateContext(offx, offy, w, h) (mini_CurrentContext = MGLCreateContext(offx, offy, w,h))

#define mglResizeContext(width, height) MGLResizeContext(mini_CurrentContext, width, height)
#define mglSwitchBuffer(bufnr) MGLSwitchBuffer(mini_CurrentContext, bufnr)
#define mglDeleteContext() MGLDeleteContext(mini_CurrentContext)
#define mglGetWindowHandle() MGLGetWindowHandle(mini_CurrentContext)
#define mglGetInputWindowHandle() MGLGetInputWindowHandle(mini_CurrentContext)
#define mglSwitchDisplay() MGLSwitchDisplay(mini_CurrentContext)
#define mglLockDisplay() MGLLockDisplay(mini_CurrentContext)
#define mglUnlockDisplay() MGLUnlockDisplay(mini_CurrentContext)


#define glColor4f(red, green, blue, alpha)  GLColor4f(mini_CurrentContext, red, green, blue, alpha)

#define glColor4fv(v)                       GLColor4fv(mini_CurrentContext, v)

#define glColor3f(red,green,blue)           GLColor4f(mini_CurrentContext, red, green, blue, 1.0)

#define glColor3fv(v)                       GLColor3fv(mini_CurrentContext,v)

#define glColor4ub(r,g,b,a) GLColor4ub(mini_CurrentContext, r,g,b,a)

#define glColor4ubv(v)      GLColor4ubv(mini_CurrentContext, v)

#define glColor3ub(r,g,b)   GLColor4ub(mini_CurrentContext, r,g,b,255)

#define glColor3ubv(v)      GLColor3ubv(mini_CurrentContext, v)

#define glNormal3f(x,y,z) GLNormal3f(mini_CurrentContext, x,y,z)

#define glcopTexEnvi(target, pname, param) GLTexEnvi(mini_CurrentContext, target, pname, param)

#define glTexEnvf(target, pname, param)  GLTexEnvi(mini_CurrentContext, target, pname, (GLint)param)

#define glTexEnvi(target, pname, param)  GLTexEnvi(mini_CurrentContext, target, pname, (GLint)param)

#define glTexEnviv(target, pname, param) GLTexEnvi(mini_CurrentContext, target, pname, *(param))

#define glTexEnvfv(target, pname, param) GLTexEnvi(mini_CurrentContext, target, pname, (GLint)(*(param)))

#define glTexParameteri(target, pname, param) GLTexParameteri(mini_CurrentContext, target, pname, param)

#define glTexParameterf(target, pname, param) glTexParameteri(target, pname, (GLint)param)

#define glTexParameteriv(target, pname, param) glTexParameteri(target, pname, *(param))

#define glTexParameterfv(target, pname, param) glTexParameteri(target, pname, (GLint)*(param))

#define mglEnableSync(enable) MGLEnableSync(mini_CurrentContext, enable)

#define mglWriteShotPPM(filename) MGLWriteShotPPM(mini_CurrentContext, filename)

#define mglTexMemStat(Current, Peak) MGLTexMemStat(mini_CurrentContext, Current, Peak)

#define mglSetZOffset(offset) MGLSetZOffset(mini_CurrentContext, offset)

#define mglCreateContextFromID(ID, w, h) MGLCreateContextFromID(ID, w, h)
/* Assigns mini_CurrentContext, matching mglCreateContext above -- without that
 * the caller gets a context back and every later gl* call still goes to
 * whichever one was current before. (2026-09-20) */
#define mglCreateContextFromWindow(win) (mini_CurrentContext = MGLCreateContextFromWindow(win))
#define mglCreateContextFromBitMap(bm)  (mini_CurrentContext = MGLCreateContextFromBitMap(bm))

#define mglLockBack(info) MGLLockBack(mini_CurrentContext, info)

//#define glEnableClientState(state) GLEnableClientState(mini_CurrentContext, state)
//#define glDisableClientState(state) GLDisableClientState(mini_CurrentContext, state)

#define glTexCoordPointer(size, type, stride, pointer) GLTexCoordPointer(mini_CurrentContext, size, type, stride, pointer)

#define glColorPointer(size, type, stride, pointer) GLColorPointer(mini_CurrentContext, size, type, stride, pointer)

#define glVertexPointer(size, type, stride, pointer) GLVertexPointer(mini_CurrentContext, size, type, stride, pointer)

#define glDrawElements(mode, count, type, indices) GLDrawElements(mini_CurrentContext, mode, count, type, indices)

#define glDrawArrays(mode, first, count) GLDrawArrays(mini_CurrentContext, mode, first, count)

#ifdef AUTOMATIC_LOCKING_ENABLE
	#define mglLockMode(lockMode) MGLLockMode(mini_CurrentContext, lockMode)
#else
	#define mglLockMode(lockMode) (NULL)
#endif

#define mglKeyFunc(k) MGLKeyFunc(mini_CurrentContext, k)
#define mglSpecialFunc(s) MGLSpecialFunc(mini_CurrentContext, s)
#define mglMouseFunc(m) MGLMouseFunc(mini_CurrentContext, m)
#define mglMinTriArea(a) MGLMinTriArea(mini_CurrentContext, a)
#define mglIdleFunc(i) MGLIdleFunc(mini_CurrentContext, i)

#define mglExit() MGLExit(mini_CurrentContext)
#define mglMainLoop() MGLMainLoop(mini_CurrentContext)

#ifndef GLNDEBUG
#define mglPrintMatrix(mode) MGLPrintMatrix(mini_CurrentContext, mode)
#define mglPrintMatrixStack(mode) MGLPrintMatrixStack(mini_CurrentContext, mode)
#endif

#define gluLookAt(ex, ey, ez, cx, cy, cz, ux, uy, uz) GLULookAt(ex, ey, ez, cx, cy, cz, ux, uy, uz);
#define gluPerspective(fovy, aspect, znear, zfar) GLUPerspective(fovy, aspect, znear, zfar);


#endif // USE_MGLAPI not defined

#ifdef __cplusplus
}
#endif


#endif

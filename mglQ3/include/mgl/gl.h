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

/* V29 / dispatch ABI 5: exact public SDK token values. */
#define MAX_TEXUNIT 2
#define GL_BASE                          0x7000
#define GL_NO_ERROR                      0x0000
#define GL_ALPHA                         0x1906
#define GL_ALPHA8                        0x803C
#define GL_ALPHA_BITS                    0x0D55
#define GL_ALPHA_SCALE                   0x0D1C
#define GL_ALPHA_TEST                    0x0BC0
#define GL_ALPHA_TEST_FUNC               0x0BC1
#define GL_ALPHA_TEST_REF                0x0BC2
#define GL_ALWAYS                        0x0207
#define GL_AUX_BUFFERS                   0x0C00
#define GL_BACK                          0x0405
#define GL_BACK_LEFT                     0x0402
#define GL_BACK_RIGHT                    0x0403
#define GL_BLEND                         0x0BE2
#define GL_BLEND_DST                     0x0BE0
#define GL_BLEND_DST_ALPHA               0x80CA
#define GL_BLEND_DST_RGB                 0x80C8
#define GL_BLEND_SRC                     0x0BE1
#define GL_BLEND_SRC_ALPHA               0x80CB
#define GL_BLEND_SRC_RGB                 0x80C9
#define GL_BLUE                          0x1905
#define GL_BLUE_BITS                     0x0D54
#define GL_BYTE                          0x1400
#define GL_C3F_V3F                       0x2A24
#define GL_C4UB_V2F                      0x2A22
#define GL_C4UB_V3F                      0x2A23
#define GL_CCW                           0x0901
#define GL_COLOR_ARRAY                   0x8076
#define GL_COLOR_ARRAY_POINTER           0x8090
#define GL_COLOR_ARRAY_SIZE              0x8081
#define GL_COLOR_ARRAY_STRIDE            0x8083
#define GL_COLOR_ARRAY_TYPE              0x8082
#define GL_COLOR_CLEAR_VALUE             0x0C22
#define GL_COLOR_INDEX                   0x1900
#define GL_CULL_FACE                     0x0B44
#define GL_CULL_FACE_MODE                0x0B45
#define GL_CURRENT_COLOR                 0x0B00
#define GL_CURRENT_INDEX                 0x0B01
#define GL_CURRENT_TEXTURE_COORDS        0x0B03
#define GL_CW                            0x0900
#define GL_DECAL                         0x2101
#define GL_DEPTH_BITS                    0x0D56
#define GL_DEPTH_CLEAR_VALUE             0x0B73
#define GL_DEPTH_COMPONENT               0x1902
#define GL_DEPTH_FUNC                    0x0B74
#define GL_DEPTH_RANGE                   0x0B70
#define GL_DEPTH_SCALE                   0x0D1E
#define GL_DEPTH_TEST                    0x0B71
#define GL_DEPTH_WRITEMASK               0x0B72
#define GL_DITHER                        0x0BD0
#define GL_DONT_CARE                     0x1100
#define GL_DOUBLE                        0x140A
#define GL_DOUBLEBUFFER                  0x0C32
#define GL_DRAW_BUFFER                   0x0C01
#define GL_DST_ALPHA                     0x0304
#define GL_DST_COLOR                     0x0306
#define GL_EDGE_FLAG                     0x0B43
#define GL_EDGE_FLAG_ARRAY               0x8079
#define GL_EDGE_FLAG_ARRAY_POINTER       0x8093
#define GL_EDGE_FLAG_ARRAY_STRIDE        0x808C
#define GL_EQUAL                         0x0202
#define GL_EXTENSIONS                    0x1F03
#define GL_FASTEST                       0x1101
#define GL_FLAT                          0x1D00
#define GL_FLOAT                         0x1406
#define GL_FOG                           0x0B60
#define GL_FOG_COLOR                     0x0B66
#define GL_FOG_DENSITY                   0x0B62
#define GL_FOG_END                       0x0B64
#define GL_FOG_HINT                      0x0C54
#define GL_FOG_INDEX                     0x0B61
#define GL_FOG_MODE                      0x0B65
#define GL_FOG_START                     0x0B63
#define GL_FRONT_AND_BACK                0x0408
#define GL_FRONT                         0x0404
#define GL_FRONT_FACE                    0x0B46
#define GL_FRONT_LEFT                    0x0400
#define GL_FRONT_RIGHT                   0x0401
#define GL_GEQUAL                        0x0206
#define GL_GREATER                       0x0204
#define GL_GREEN                         0x1904
#define GL_GREEN_BITS                    0x0D53
#define GL_INDEX_ARRAY                   0x8077
#define GL_INDEX_ARRAY_POINTER           0x8091
#define GL_INDEX_ARRAY_STRIDE            0x8086
#define GL_INDEX_ARRAY_TYPE              0x8085
#define GL_INDEX_BITS                    0x0D51
#define GL_INDEX_CLEAR_VALUE             0x0C20
#define GL_INDEX_MODE                    0x0C30
#define GL_INT                           0x1404
#define GL_INTENSITY                     0x8049
#define GL_INTENSITY8                    0x804B
#define GL_LEFT                          0x0406
#define GL_LEQUAL                        0x0203
#define GL_LESS                          0x0201
#define GL_LINEAR                        0x2601
#define GL_LINEAR_MIPMAP_LINEAR          0x2703
#define GL_LINEAR_MIPMAP_NEAREST         0x2701
#define GL_LINES                         0x0001
#define GL_LINE_LOOP                     0x0002
#define GL_LINE_STRIP                    0x0003
#define GL_LUMINANCE                     0x1909
#define GL_LUMINANCE8                    0x8040
#define GL_LUMINANCE8_ALPHA8             0x8045
#define GL_LUMINANCE_ALPHA               0x190A
#define GL_MATRIX_MODE                   0x0BA0
#define GL_MAX_TEXTURE_SIZE              0x0D33
#define GL_MAX_VIEWPORT_DIMS             0x0D3A
#define GL_MODELVIEW                     0x1700
#define GL_MODELVIEW_MATRIX              0x0BA6
#define GL_MODELVIEW_STACK_DEPTH         0x0BA3
#define GL_ADD                           0x0104
#define GL_MODULATE                      0x2100
#define GL_NEVER                         0x0200
#define GL_NEAREST                       0x2600
#define GL_NEAREST_MIPMAP_NEAREST        0x2700
#define GL_NEAREST_MIPMAP_LINEAR         0x2702
#define GL_NICEST                        0x1102
#define GL_NONE                          0x0000
#define GL_NOTEQUAL                      0x0205
#define GL_ONE                           0x0001
#define GL_ONE_MINUS_DST_ALPHA           0x0305
#define GL_ONE_MINUS_DST_COLOR           0x0307
#define GL_ONE_MINUS_SRC_ALPHA           0x0303
#define GL_ONE_MINUS_SRC_COLOR           0x0301
#define GL_PACK_ALIGNMENT                0x0D05
#define GL_PACK_LSB_FIRST                0x0D01
#define GL_PACK_ROW_LENGTH               0x0D02
#define GL_PACK_SKIP_PIXELS              0x0D04
#define GL_PACK_SKIP_ROWS                0x0D03
#define GL_PACK_SWAP_BYTES               0x0D00
#define GL_PERSPECTIVE_CORRECTION_HINT   0x0C50
#define GL_POINT_SMOOTH_HINT             0x0C51
#define GL_LINE_SMOOTH_HINT              0x0C52
#define GL_POLYGON_SMOOTH_HINT           0x0C53
#define GL_POINTS                        0x0000
#define GL_POLYGON_MODE                  0x0B40
#define GL_POLYGON_OFFSET                0x7001
#define GL_POLYGON_OFFSET_FACTOR         0x8038
#define GL_POLYGON_OFFSET_FILL           0x8037
#define GL_POLYGON_OFFSET_LINE           0x2A02
#define GL_POLYGON_OFFSET_POINT          0x2A01
#define GL_POLYGON_OFFSET_UNITS          0x2A00
#define GL_POLYGON                       0x0009
#define GL_PROJECTION                    0x1701
#define GL_PROJECTION_MATRIX             0x0BA7
#define GL_PROJECTION_STACK_DEPTH        0x0BA4
#define GL_QUADS                         0x0007
#define GL_QUAD_STRIP                    0x0008
#define GL_READ_BUFFER                   0x0C02
#define GL_RED                           0x1903
#define GL_RED_BITS                      0x0D52
#define GL_RENDERER                      0x1F01
#define GL_REPLACE                       0x1E01
#define GL_REPEAT                        0x2901
#define GL_RGB                           0x1907
#define GL_RGB5                          0x8050
#define GL_RGB5_A1                       0x8057
#define GL_RGB8                          0x8051
#define GL_RGBA                          0x1908
#define GL_RGBA8                         0x8058
#define GL_RGBA_MODE                     0x0C31
#define GL_RIGHT                         0x0407
#define GL_SCISSOR_BOX                   0x0C10
#define GL_SCISSOR_TEST                  0x0C11
#define GL_SHADE_MODEL                   0x0B54
#define GL_SHORT                         0x1402
#define GL_SMOOTH                        0x1D01
#define GL_SRC_ALPHA                     0x0302
#define GL_SRC_ALPHA_SATURATE            0x0308
#define GL_SRC_COLOR                     0x0300
#define GL_STEREO                        0x0C33
#define GL_T2F_C3F_V3F                   0x2A2A
#define GL_T2F_C4UB_V3F                  0x2A29
#define GL_T2F_V3F                       0x2A27
#define GL_TABLE_TOO_LARGE               0x8031
#define GL_TEXTURE_2D                    0x0DE1
#define GL_TEXTURE_2D_BINDING            0x8069
#define GL_TEXTURE_COORD_ARRAY           0x8078
#define GL_TEXTURE_COORD_ARRAY_SIZE      0x8088
#define GL_TEXTURE_COORD_ARRAY_STRIDE    0x808A
#define GL_TEXTURE_COORD_ARRAY_TYPE      0x8089
#define GL_TEXTURE_DOOR_ARRAY_POINTER    0x8092
#define GL_TEXTURE_ENV                   0x2300
#define GL_TEXTURE_ENV_COLOR             0x2201
#define GL_TEXTURE_ENV_MODE              0x2200
#define GL_TEXTURE_MAG_FILTER            0x2800
#define GL_TEXTURE_MIN_FILTER            0x2801
#define GL_TEXTURE_PRIORITY              0x8066
#define GL_TEXTURE_WRAP_S                0x2802
#define GL_TEXTURE_WRAP_T                0x2803
#define GL_TEXTURE_GEN_S                 0x0C60
#define GL_TEXTURE_GEN_T                 0x0C61
#define GL_TRIANGLES                     0x0004
#define GL_TRIANGLE_FAN                  0x0006
#define GL_TRIANGLE_STRIP                0x0005
#define GL_UNPACK_ALIGNMENT              0x0CF5
#define GL_UNPACK_LSB_FIRST              0x0CF1
#define GL_UNPACK_ROW_LENGTH             0x0CF2
#define GL_UNPACK_SKIP_PIXELS            0x0CF4
#define GL_UNPACK_SKIP_ROWS              0x0CF3
#define GL_UNPACK_SWAP_BYTES             0x0CF0
#define GL_UNSIGNED_BYTE                 0x1401
#define GL_UNSIGNED_INT                  0x1405
#define GL_UNSIGNED_SHORT                0x1403
#define GL_V2F                           0x2A20
#define GL_V3F                           0x2A21
#define GL_VENDOR                        0x1F00
#define GL_VERSION                       0x1F02
#define GL_VERTEX_ARRAY                  0x8074
#define GL_VERTEX_ARRAY_POINTER          0x808E
#define GL_VERTEX_ARRAY_SIZE             0x807A
#define GL_VERTEX_ARRAY_STRIDE           0x807C
#define GL_VERTEX_ARRAY_TYPE             0x807B
#define GL_VIEWPORT                      0x0BA2
#define GL_ZERO                          0x0000
#define GL_POINT_SMOOTH                  0x0B10
#define GL_CLAMP                         0x2900
#define GL_EXP                           0x0800
#define GL_EXP2                          0x0801
#define GL_TEXTURE_GEN_MODE              0x2500
#define GL_SPHERE_MAP                    0x2402
#define GL_T                             0x2001
#define GL_S                             0x2000
#define GL_FILL                          0x1B02
#define GL_TEXTURE0_ARB                  0x84C0
#define GL_TEXTURE1_ARB                  0x84C1
#define GL_TEXTURE2_ARB                  0x84C2
#define GL_TEXTURE3_ARB                  0x84C3
#define GL_TEXTURE0                      0x84C0
#define GL_TEXTURE1                      0x84C1
#define GL_TEXTURE2                      0x84C2
#define GL_TEXTURE3                      0x84C3
#define GL_MAX_TEXTURE_UNITS_ARB         0x84E2
#define MGL_LOCK_AUTOMATIC               0x7010
#define MGL_LOCK_MANUAL                  0x7011
#define MGL_LOCK_SMART                   0x7012
#define MGL_PERSPECTIVE_MAPPING          0x7015
#define MGL_W_ONE_HINT                   0x7016
#define MGL_Z_OFFSET                     0x7017
#define GL_COLOR_TABLE                   0x80D0
#define MGL_UBYTE_BGRA                   0x7018
#define MGL_UBYTE_ARGB                   0x7019
#define MGL_UNSIGNED_SHORT_5_6_5         0x701A
#define MGL_UNSIGNED_SHORT_4_4_4_4       0x701B
#define MGL_FIXPOINTTRANS_HINT           0x701C
#define MGL_ARRAY_TRANSFORMATIONS        0x701D
#define GL_COLOR_WRITEMASK               0x0C23
#define GL_LINE_WIDTH                    0x0B21
#define GL_EYE_LINEAR                    0x2400
#define GL_OBJECT_LINEAR                 0x2401
#define GL_OBJECT_PLANE                  0x2501
#define GL_EYE_PLANE                     0x2502
#define GL_TEXTURE                       0x1702
#define GL_TEXTURE_MATRIX                0x0BA8
#define GL_TEXTURE_STACK_DEPTH           0x0BA5
#define GL_POINT_SIZE                    0x0B11
#define GL_TEXTURE_COORD_ARRAY_POINTER GL_TEXTURE_DOOR_ARRAY_POINTER
#define GL_LINE_SMOOTH                   0x0B20
#define GL_LINE_STIPPLE                  0x0B24
#define GL_POLYGON_SMOOTH                0x0B41
#define GL_POLYGON_STIPPLE               0x0B42
#define GL_COLOR_MATERIAL_FACE           0x0B55
#define GL_COLOR_MATERIAL_PARAMETER      0x0B56
#define GL_COLOR_MATERIAL                0x0B57
#define GL_NORMALIZE                     0x0BA1
#define GL_STENCIL_TEST                  0x0B90
#define GL_LOGIC_OP                      0x0BF1
#define GL_INDEX_LOGIC_OP                0x0BF1
#define GL_COLOR_LOGIC_OP                0x0BF2
#define GL_TEXTURE_GEN_R                 0x0C62
#define GL_TEXTURE_GEN_Q                 0x0C63
#define GL_AUTO_NORMAL                   0x0D80
#define GL_TEXTURE_1D                    0x0DE0
#define GL_CLIP_PLANE0                   0x3000
#define GL_CLIP_PLANE1                   0x3001
#define GL_CLIP_PLANE2                   0x3002
#define GL_CLIP_PLANE3                   0x3003
#define GL_CLIP_PLANE4                   0x3004
#define GL_CLIP_PLANE5                   0x3005
#define GL_MAP1_COLOR_4                  0x0D90
#define GL_MAP1_INDEX                    0x0D91
#define GL_MAP1_NORMAL                   0x0D92
#define GL_MAP1_TEXTURE_COORD_1          0x0D93
#define GL_MAP1_TEXTURE_COORD_2          0x0D94
#define GL_MAP1_TEXTURE_COORD_3          0x0D95
#define GL_MAP1_TEXTURE_COORD_4          0x0D96
#define GL_MAP1_VERTEX_3                 0x0D97
#define GL_MAP1_VERTEX_4                 0x0D98
#define GL_MAP2_COLOR_4                  0x0DB0
#define GL_MAP2_INDEX                    0x0DB1
#define GL_MAP2_NORMAL                   0x0DB2
#define GL_MAP2_TEXTURE_COORD_1          0x0DB3
#define GL_MAP2_TEXTURE_COORD_2          0x0DB4
#define GL_MAP2_TEXTURE_COORD_3          0x0DB5
#define GL_MAP2_TEXTURE_COORD_4          0x0DB6
#define GL_MAP2_VERTEX_3                 0x0DB7
#define GL_MAP2_VERTEX_4                 0x0DB8
#define GL_COLOR_BUFFER_BIT     0x00004000
#define GL_DEPTH_BUFFER_BIT     0x00000100
#define GL_TRUE                 1
#define GL_FALSE                0
#define MGL_BUTTON_LEFT         0x00000001
#define MGL_BUTTON_RIGHT        0x00000002
#define MGL_BUTTON_MID          0x00000004
#define MGL_SM_BESTMODE         0xFFFFFFFF
#define MGL_SM_WINDOWMODE       0x00000000


#define GL_SHARED_TEXTURE_PALETTE_EXT 0x81FB
#define GL_FUNC_ADD                   0x8006
#define GL_MIN                        0x8007
#define GL_MAX                        0x8008
#define GL_BLEND_EQUATION             0x8009
#define GL_FUNC_SUBTRACT              0x800A
#define GL_FUNC_REVERSE_SUBTRACT      0x800B
#define MGL_ZBUFFER_32F  32
#define MGL_ZBUFFER_16   16
#define GL_CURRENT_NORMAL  0x0B02
#define GL_COMPILE              0x1300
#define GL_COMPILE_AND_EXECUTE  0x1301
#define GL_LIST_BASE            0x0B32
#define GL_LIST_INDEX           0x0B33
#define GL_LIST_MODE            0x0B30
#define GL_LIGHTING                 0x0B50
#define GL_LIGHT_MODEL_LOCAL_VIEWER 0x0B51
#define GL_LIGHT_MODEL_TWO_SIDE     0x0B52
#define GL_LIGHT_MODEL_AMBIENT      0x0B53
#define GL_LIGHT0                   0x4000
#define GL_LIGHT1                   0x4001
#define GL_LIGHT2                   0x4002
#define GL_LIGHT3                   0x4003
#define GL_LIGHT4                   0x4004
#define GL_LIGHT5                   0x4005
#define GL_LIGHT6                   0x4006
#define GL_LIGHT7                   0x4007
#define GL_AMBIENT                  0x1200
#define GL_DIFFUSE                  0x1201
#define GL_SPECULAR                 0x1202
#define GL_POSITION                 0x1203
#define GL_SPOT_DIRECTION           0x1204
#define GL_SPOT_EXPONENT            0x1205
#define GL_SPOT_CUTOFF              0x1206
#define GL_CONSTANT_ATTENUATION     0x1207
#define GL_LINEAR_ATTENUATION       0x1208
#define GL_QUADRATIC_ATTENUATION    0x1209
#define GL_EMISSION                 0x1600
#define GL_SHININESS                0x1601
#define GL_AMBIENT_AND_DIFFUSE      0x1602
#define GL_INVALID_ENUM       0x0500
#define GL_INVALID_VALUE      0x0501
#define GL_INVALID_OPERATION  0x0502
#define GL_STACK_OVERFLOW     0x0503
#define GL_STACK_UNDERFLOW    0x0504
#define GL_OUT_OF_MEMORY      0x0505
#define GL_POINT              0x1B00
#define GL_LINE               0x1B01
#define GL_NORMAL_ARRAY          0x8075
#define GL_NORMAL_ARRAY_TYPE     0x807E
#define GL_NORMAL_ARRAY_STRIDE   0x807F
#define GL_NORMAL_ARRAY_POINTER  0x808F

/* Opaque V29 GLU API types; Classic does not implement quadrics. */
#define GLU_SMOOTH         100000
#define GLU_FLAT           100001
#define GLU_NONE           100002
#define GLU_POINT          100010
#define GLU_LINE           100011
#define GLU_FILL           100012
#define GLU_SILHOUETTE     100013
#define GLU_OUTSIDE        100020
#define GLU_INSIDE         100021
#define GLU_INVALID_ENUM   100900
#define GLU_INVALID_VALUE  100901
#define GLU_OUT_OF_MEMORY  100902
#define GLU_INCOMPATIBLE_GL_VERSION 100903
#define GLU_INVALID_OPERATION       100904
#define GLU_ERROR          100103
typedef struct GLUquadricObj_t GLUquadricObj;
typedef GLUquadricObj GLUquadric;
typedef void (*MGLUfuncptr)();

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

#ifndef GL_NOERRORCHECK
	extern int kprintf(char *, ...);
	#define GLFlagError(context,c,err) while ((c)) {\
		context->CurrentError = err;\
		kprintf("GLError: %ld at %s:%ld\n", (int)err, __FILE__,(int)__LINE__);\
		return;\
	}
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

void        MGLDrawMultitexBuffer (GLcontext context, GLenum BSrc, GLenum BDst, GLenum TexEnv);



void        GLAlphaFunc(GLcontext context, GLenum func, GLclampf ref);
//GLboolean   GLAreTexturesResident(GLcontext context, GLsizei n, const GLuint *textures, GLboolean *residences);
void        GLArrayElement(GLcontext context, GLint i);
void        GLBegin(GLcontext context, GLenum mode);
void        GLBindTexture(GLcontext context, GLenum target, GLuint texture);
void        GLBlendFunc(GLcontext context, GLenum sfactor, GLenum dfactor);
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

void	    GLColorMask(GLcontext context, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha); // Cowcat

void        GLCullFace(GLcontext context, GLenum mode);
void        GLDeleteTextures(GLcontext context, GLsizei n, const GLuint *textures);
void        GLDepthFunc(GLcontext context, GLenum func);
void        GLDepthMask(GLcontext context, GLboolean flag);
void        GLDepthRange(GLcontext context, GLclampd n, GLclampd f);
void        GLDisableClientState(GLcontext context, GLenum cap);
//void        GLDrawArrays(GLcontext context, GLenum mode, GLint first, GLsizei count);
void        GLDrawBuffer(GLcontext context, GLenum mode);
//void        GLDrawElements(GLcontext context, GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
//void        GLEdgeFlag(GLcontext context, GLboolean flag);
//void        GLEdgeFlagPointer(GLcontext context, GLsizei stride, const GLboolean *flags);
//void        GLEdgeFlagv(GLcontext context, const GLboolean *flag);
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
//void        GLGetDoublev(GLcontext context, GLenum pname, GLdouble *params);
GLenum      GLGetError(GLcontext context);
void        GLGetFloatv(GLcontext context, GLenum pname, GLfloat *params);
void        GLGetIntegerv(GLcontext context, GLenum pname, GLint *params);
void        GLGetPointerv(GLcontext context, GLenum pname, GLvoid* *params);
const GLubyte* GLGetString(GLcontext context, GLenum name);
void        GLHint(GLcontext context, GLenum target, GLenum mode);
//void        GLIndexi(GLcontext context, GLint c);
//void        GLIndexiv(GLcontext context, const GLint *c);
//void        GLIndexPointer(GLcontext context, GLenum type, GLsizei stride, const GLvoid *pointer);
//void        GLInterleavedArrays(GLcontext context, GLenum format, GLsizei stride, const GLvoid *pointer);
GLboolean   GLIsEnabled(GLcontext context, GLenum cap);
//GLboolean   GLIsTexture(GLcontext context, GLuint texture);
void        GLLoadIdentity(GLcontext context);
void        GLLoadMatrixd(GLcontext context, const GLdouble *m);
void        GLLoadMatrixf(GLcontext context, const GLfloat *m);
void        GLMatrixMode(GLcontext context, GLenum mode);
void        GLMultMatrixd(GLcontext context, const GLdouble *m);
void        GLMultMatrixf(GLcontext context, const GLfloat *m);
void        GLNormal3f(GLcontext context, GLfloat x, GLfloat y, GLfloat z);
void        GLNormal3fv(GLcontext context, GLfloat *n);
void        GLOrtho(GLcontext context, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
void        GLPixelStorei(GLcontext context, GLenum pname, GLint param);
void        GLPolygonMode(GLcontext context, GLenum face, GLenum mode);
void        GLPolygonOffset(GLcontext context, GLfloat factor, GLfloat units);
//surgeon:
void	      GLPointSize(GLcontext context, GLfloat size);

void        GLPopMatrix(GLcontext context);
//void        GLPrioritizeTextures(GLcontext context, GLsizei n, const GLuint *textures, const GLclampf *priorities);
void        GLPushMatrix(GLcontext context);
//void        GLReadBuffer(GLcontext context, GLenum mode);
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
void        mglChoosePixelDepth(int depth);
void        mglChooseTextureBufferSize(int size);
void        mglChooseVertexBufferSize(int size);
void        mglChooseMtexBufferSize(int size);
void        mglChooseWindowMode(GLboolean flag);
void *      MGLCreateContext(int offx, int offy, int w, int h);
void        MGLDeleteContext(GLcontext context);
void        MGLEnableSync(GLcontext context, GLboolean enable);
void        MGLExit(GLcontext context);
void *      MGLGetWindowHandle(GLcontext context);
void        MGLEmergencyCloseScreen(GLcontext context);
void *      MGLCreateContextFromWindow(struct Window *window);
void *      MGLCreateContextFromBitMap(struct BitMap *bitmap);
void        MGLIdleFunc(GLcontext context, IdleFn i);
void        MGLKeyFunc(GLcontext context, KeyHandlerFn k);
GLboolean   MGLLockDisplay(GLcontext context);
void        MGLMainLoop(GLcontext context);
void	      MGLMinTriArea(GLcontext context, GLfloat area);
void        MGLMouseFunc(GLcontext context, MouseHandlerFn m);
void        mglProhibitAlphaFallback(GLboolean flag);
void        mglProhibitMipMapping(GLboolean flag);
void        mglProposeCloseDesktop(GLboolean closeme);
GLboolean   MGLResizeContext(GLcontext context, GLsizei width, GLsizei height);
void        MGLSetState(GLcontext context, GLenum cap, GLboolean state);
void        MGLSpecialFunc(GLcontext context, SpecialHandlerFn s);
void        MGLSwitchBuffer(GLcontext context, int bufnr);
void        MGLSwitchDisplay(GLcontext context);
void        MGLTexMemStat(GLcontext context, GLint *Current, GLint *Peak);
void        MGLUnlockDisplay(GLcontext context);

//Cowcat
void        MGLSetPointer(GLcontext context);
void        MGLClearPointer(GLcontext context);
//

void        MGLWriteShotPPM(GLcontext context, char *filename);
GLboolean   MGLInit(void);
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

void        GLEnableClientState(GLcontext context, GLenum state);
void        GLDisableClientState(GLcontext context, GLenum state);

void        GLTexCoordPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void        GLColorPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void        GLVertexPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);

void        GLDrawElements(GLcontext context, GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
void        GLDrawArrays(GLcontext context, GLenum mode, GLint first, GLsizei count);

/*
** These macros define the OpenGL compatibility macros. If you don't want them,
** define USE_MGLAPI before including this file.
*/


#ifdef USE_MGLAPI
	#include "mgl/minigl.h"

#else

#define glActiveTextureARB(unit)                GLActiveTextureARB(mini_CurrentContext, unit)

#define glMultiTexCoord2fARB(a, u, v)           GLMultiTexCoord2fARB(mini_CurrentContext, a, u, v)

#define glMultiTexCoord2fvARB(a, v)             GLMultiTexCoord2fvARB(mini_CurrentContext, a,  v)

#define mglDrawMultitexBuffer(bsrc, bdst, env) MGLDrawMultitexBuffer(mini_CurrentContext, bsrc, bdst, env)

#define glAlphaFunc(func, ref) GLAlphaFunc(mini_CurrentContext, func, ref)

#define glAreTexturesResident(n, textures, residences) GLAreTexturesResident(mini_CurrentContext, n, textures, residences)

#define glArrayElement(i) GLArrayElement(mini_CurrentContext, i)

#define glArrayElementEXT(i) GLArrayElement(mini_CurrentContext, i)

#define glBegin(mode) GLBegin(mini_CurrentContext, mode)
#define glEnd() GLEnd(mini_CurrentContext)

#define glTexGeni(coord,mode,map) GLTexGeni(mini_CurrentContext, coord, mode, map)

#define glBindTexture(target, texture) GLBindTexture(mini_CurrentContext, target, texture)

#define glBlendFunc(sfactor, dfactor) GLBlendFunc(mini_CurrentContext, sfactor, dfactor)

#define glClear(mask) GLClear(mini_CurrentContext, mask)

#define glClearColor(red, green, blue, alpha) GLClearColor(mini_CurrentContext, red, green, blue, alpha)

#define glClearDepth(depth) GLClearDepth(mini_CurrentContext, depth)

#define glColorPointer(size, type, stride, pointer) GLColorPointer(mini_CurrentContext, size, type, stride, pointer)

#define glColorPointerEXT(size, type, stride, pointer) GLColorPointer(mini_CurrentContext, size, type, stride, pointer)

#define glColorTable(target, internalformat, width, format, type, data) GLColorTable(mini_CurrentContext, target, internalformat, width, format, type, data)

#define glColorMask(red, green, blue, alpha) GLColorMask(mini_CurrentContext, red, green, blue, alpha) // Cowcat

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

#define glIndexiv(c) GLIndexfv(mini_CurrentContext, c)

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

#define glPushMatrix() GLPushMatrix(mini_CurrentContext)

#define glPopMatrix() GLPopMatrix(mini_CurrentContext)

#define glPrioritizeTextures(n, textures, pri) GLPrioritizeTextures(mini_CurrentContext, n, textures, pri)

#define glReadBuffer(mode) GLReadBuffer(mini_CurrentContext, mode)

#define glReadPixels(x, y, width, height, format, type, pixels) GLReadPixels(mini_CurrentContext, x, y, width, height, format, type, pixels)

#define glRotated(angle, x, y, z) GLRotated(mini_CurrentContext, angle, x, y, z)

#define glRotatef(angle, x, y, z) GLRotatef(mini_CurrentContext, (angle), (x), (y), (z))

#define glRotatefEXT(angle, xyz) GLRotatefEXT(mini_CurrentContext, angle, xyz)

#define glRotatefEXTs(sin_an, cos_an, xyz) GLRotatefEXTs(mini_CurrentContext, sin_an, cos_an, xyz)

#define glScalef(x, y, z) GLScalef(mini_CurrentContext, x, y, z) // Cowcat

#define glScaled(x, y, z) GLScaled(mini_CurrentContext, (GLdouble)(x), (GLdouble)(y), (GLdouble)(z)) // Cowcat

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
#define mglCreateContextFromWindow(window) (mini_CurrentContext = MGLCreateContextFromWindow((void *)(window)))

#define mglResizeContext(width, height) MGLResizeContext(mini_CurrentContext, width, height)
#define mglSwitchBuffer(bufnr) MGLSwitchBuffer(mini_CurrentContext, bufnr)
#define mglDeleteContext() MGLDeleteContext(mini_CurrentContext)
#define mglGetWindowHandle() MGLGetWindowHandle(mini_CurrentContext)
#define mglEmergencyCloseScreen() MGLEmergencyCloseScreen(mini_CurrentContext)
#define mglSwitchDisplay() MGLSwitchDisplay(mini_CurrentContext)
#define mglLockDisplay() MGLLockDisplay(mini_CurrentContext)
#define mglUnlockDisplay() MGLUnlockDisplay(mini_CurrentContext)

//Cowcat
#define mglSetPointer() MGLSetPointer(mini_CurrentContext)
#define mglClearPointer() MGLClearPointer(mini_CurrentContext)

#define glColor4f(red, green, blue, alpha)  GLColor4f(mini_CurrentContext, red, green, blue, alpha)

#define glColor4fv(v)                       GLColor4fv(mini_CurrentContext, v)

#define glColor3f(red,green,blue)           GLColor4f(mini_CurrentContext, red, green, blue, 1.0)

#define glColor3fv(v)                       GLColor3fv(mini_CurrentContext,v)

#define glColor4ub(r,g,b,a) GLColor4ub(mini_CurrentContext, r,g,b,a)

#define glColor4ubv(v)      GLColor4ubv(mini_CurrentContext, v)

#define glColor3ub(r,g,b)   GLColor4ub(mini_CurrentContext, r,g,b,255)

#define glColor3ubv(v)      GLColor3ubv(mini_CurrentContext, v)

#define glNormal3f(x,y,z) GLNormal3f(mini_CurrentContext, x,y,z)
#define glNormal3fv(v) GLNormal3fv(mini_CurrentContext, v)

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
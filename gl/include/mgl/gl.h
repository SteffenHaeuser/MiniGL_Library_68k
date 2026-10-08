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

/*
 * Modified by Dennis van der Boon for use on the PiStorm with Pi4.
 * Copyright 2025-2026.
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
typedef unsigned int    GLenum;

/*
	GL enum
	Mostly positional values, not the real OpenGL constants; see the
	notes on the members that carry real ones
*/

#define MAX_TEXUNIT 2

/*
 * GL TOKENS CARRY OPENGL'S OWN VALUES.
 *
 * Auto-numbering, as in the enum inherited from Hyperion's MiniGL, would give
 * GL_INVALID_ENUM 0x58 rather than 0x0500, so a program written against a real
 * GL header would pass values that mean something else here.
 *
 * #defines, not an enum, because real GL tokens legitimately SHARE values:
 * GL_ZERO, GL_POINTS, GL_FALSE and GL_NONE are all 0. An enum can hold
 * duplicates, but any switch naming two of them stops compiling.
 *
 * DRIVER-PRIVATE TOKENS take 0x7000..0x7FFF, a range the authoritative
 * header leaves entirely unused. GL_BASE is the important one: it marks
 * "no glBegin block open", and it cannot be 0 -- GL_POINTS is.
 *
 * TOKEN VALUES ARE ABI, and a mismatch breaks SILENTLY -- values are passed,
 * not linked. MINIGL_DISPATCH_ABI_VERSION gates it, so the library refuses a
 * client built against different values out loud.
 */

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
/* The separate-blend query names. GL assigns DST before SRC in this range --
 * 0x80C8 is the DESTINATION factor -- which is the reverse of how the pairs
 * are usually written. Values checked against Mesa's glext.h. */
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
/* GL_ADD is NOT GL 1.1: it is EXT_texture_env_add, core in GL 1.3. Named
 * here because the texture environment accepts it. */
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
/* The three antialiasing hints, accepted and dropped by glHint like the two
 * above: V3D exposes no equivalent, and GL lets an implementation ignore any
 * hint. Named so a conformant client can set them without a GL_INVALID_ENUM. */
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
	/* These four carry their real OpenGL values (0x84C0-0x84C3), not
	 * positional ones: a client built against standard GL headers passes
	 * the real value across the API boundary, and the driver takes the unit
	 * as `unit - GL_TEXTURE0_ARB` (GLActiveTextureARB,
	 * GLClientActiveTextureARB, GLMultiTexCoord2fARB), refusing one outside
	 * the first MAX_TEXUNIT units with GL_INVALID_ENUM.
	 * GL_MAX_TEXTURE_UNITS_ARB is re-anchored to GL_FILL+1 so the members
	 * after it continue the positional numbering. */
#define GL_TEXTURE0_ARB                  0x84C0
#define GL_TEXTURE1_ARB                  0x84C1
#define GL_TEXTURE2_ARB                  0x84C2
#define GL_TEXTURE3_ARB                  0x84C3
	/* The unsuffixed spellings, which multitexture took core in GL 1.3 and
	 * which a client built against standard GL headers uses. Identical values,
	 * so `unit - GL_TEXTURE0_ARB` arithmetic in the driver is unaffected. */
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
	/* Positional value, kept as-is: a client compiled against this header
	 * references it by name. A client with its own GL headers must pass the
	 * shared-palette target below instead, which carries the genuine spec
	 * value -- same distinction as GL_TEXTURE0_ARB above. glColorTable
	 * accepts these two targets and refuses any other with
	 * GL_INVALID_OPERATION (texture.c). */
#define GL_COLOR_TABLE                   0x80D0
#define MGL_UBYTE_BGRA                   0x7018
#define MGL_UBYTE_ARGB                   0x7019
#define MGL_UNSIGNED_SHORT_5_6_5         0x701A
#define MGL_UNSIGNED_SHORT_4_4_4_4       0x701B
#define MGL_FIXPOINTTRANS_HINT           0x701C
#define MGL_ARRAY_TRANSFORMATIONS        0x701D
	/* APPEND ONLY, and this is why: the enum above is auto-numbered, so a
	 * name inserted in alphabetical order silently renumbers every constant
	 * after it and breaks every object file and prebuilt archive compiled
	 * against the older header, and every third-party client compiled
	 * against base MiniGL's own gl.h, which passes that header's numbers at
	 * runtime (they match this enum from GL_BASE through GL_FILL). Do not
	 * insert; add at the end.
	 *
	 * GL_COLOR_WRITEMASK: the glColorMask state (ColorMaskR..A), answered
	 * with all four elements by the glGet* queries in others.c. */
#define GL_COLOR_WRITEMASK               0x0C23
	/* GL_LINE_WIDTH: the width glLineWidth sets, read back the same way. */
#define GL_LINE_WIDTH                    0x0B21
	/* Texture coordinate generation. These carry their REAL GL VALUES rather
	 * than the next positional numbers, for the same reason GL_TEXTURE0_ARB
	 * does above: a client with its own GL headers passes
	 * 0x2400/0x2401/0x2501/0x2502 across the boundary, and a positional value
	 * here would silently mean something else. GL_SPHERE_MAP and
	 * GL_TEXTURE_GEN_MODE above keep their positional values, so the driver
	 * accepts BOTH spellings of those two (texture.c, draw.c).
	 *
	 * The positional numbering STOPS here: every member from this one on has
	 * an explicit value. A name appended without one continues from the last
	 * member's value, so give it an explicit value or re-anchor it the way
	 * GL_MAX_TEXTURE_UNITS_ARB does. */
#define GL_EYE_LINEAR                    0x2400
#define GL_OBJECT_LINEAR                 0x2401
#define GL_OBJECT_PLANE                  0x2501
#define GL_EYE_PLANE                     0x2502

	/* GL_TEXTURE, the third matrix mode, accepted by glMatrixMode (matrix.c).
	 * Base MiniGL does not have it. APPENDED, per the rule above, and with an
	 * EXPLICIT value, per the note above.
	 *
	 * 0x1702 is the REAL OpenGL value for GL_TEXTURE and collides with no
	 * other member (the positional ones end at 240, GL_LINE_WIDTH), so an
	 * application built against standard GL headers, which passes 0x1702,
	 * reaches the texture matrix too. */
#define GL_TEXTURE                       0x1702

	/* The two read-back queries that go with it, matching what the other two
	 * matrix modes answer in others.c (GL_MODELVIEW_MATRIX /
	 * GL_MODELVIEW_STACK_DEPTH and the projection pair). Real GL values, same
	 * reasoning as GL_TEXTURE above; no collision with the auto-numbered
	 * members, which end at 240. */
#define GL_TEXTURE_MATRIX                0x0BA8
#define GL_TEXTURE_STACK_DEPTH           0x0BA5

	/* GL_POINT_SIZE: the size glPointSize sets, read back the same way --
	 * the sibling of GL_LINE_WIDTH above. Real GL value, per the rule
	 * above; no collision with the auto-numbered members, which end at 240. */
#define GL_POINT_SIZE                    0x0B11

/* The correct spelling of the enum member above that reads
 * GL_TEXTURE_DOOR_ARRAY_POINTER -- a typo inherited from base MiniGL's header
 * -- so glGetPointerv's texcoord query can be named by its GL name. An alias
 * rather than a rename, so the member keeps its position and value and the
 * old spelling still compiles. */
#define GL_TEXTURE_COORD_ARRAY_POINTER GL_TEXTURE_DOOR_ARRAY_POINTER

/* ==========================================================================
 * LEGAL GL 1.1 CAPABILITIES THIS DRIVER DOES NOT IMPLEMENT.
 *
 * glIsEnabled answers GL_FALSE for every one of these and records NO error,
 * because GL says querying an unimplemented capability is valid and the answer
 * is false -- a conformant save-and-restore helper queries everything it might
 * touch, and an error there is fatal to a client that asserts on glGetError
 * (a client application does). glEnable/glDisable still refuse them.
 *
 * APPENDED, per the rule above, and every one carries its REAL OpenGL value for
 * the same reason GL_TEXTURE0_ARB and the texgen four do: a client with its own
 * GL headers passes the real number across the boundary. None collides with the
 * auto-numbered members, which end at 240, nor with any name already here.
 *
 * Every value is one that TWO real GL headers agree on across all 37, the
 * amiga-gcc toolchain's and MESA's. 0x0BF1 carries both spellings because
 * the real headers do: GL 1.0's GL_LOGIC_OP and GL 1.1's GL_INDEX_LOGIC_OP.
 *
 * See glIsEnabled (others.c), which matches them by name.
 */
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
/* Evaluators, absent entirely. */
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

/* OpenGL's own values. These are a MASK, so a program that passes
 * GL's 0x4000 while the driver tests 0x1 gets a glClear that silently
 * does nothing -- nothing fails, the frame is simply not cleared. */
#define GL_COLOR_BUFFER_BIT     0x00004000
#define GL_DEPTH_BUFFER_BIT     0x00000100

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

	Unlike base MiniGL's version it neither returns nor prints: every call
	site stops explicitly -- some in functions that return a value -- and a
	game repeating a refused call every frame must not flood kprintf.
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
*/

void        GLActiveTextureARB(GLcontext context, GLenum unit);
void        GLMultiTexCoord2fARB(GLcontext context, GLenum unit, GLfloat s, GLfloat t);
void        GLMultiTexCoord2fvARB(GLcontext context, GLenum unit, GLfloat *v);
void        GLClientActiveTextureARB(GLcontext context, GLenum unit);

void MGLDrawMultitexBuffer (GLcontext context, GLenum BSrc, GLenum BDst, GLenum TexEnv);

/*
 * mglq3's extensions, not in base MiniGL, called through the
 * mglSetPointer()/mglClearPointer() macros. Thin wrappers around
 * vid_Pointer/vid_DeletePointer -- see context.c.
 */
void MGLSetPointer(GLcontext context);
void MGLClearPointer(GLcontext context);



void        GLAlphaFunc(GLcontext context, GLenum func, GLclampf ref);
GLboolean   GLAreTexturesResident(GLcontext context, GLsizei n, const GLuint *textures, GLboolean *residences);
void        GLArrayElement(GLcontext context, GLint i);
void        GLBegin(GLcontext context, GLenum mode);
void        GLBindTexture(GLcontext context, GLenum target, GLuint texture);
void        GLBlendFunc(GLcontext context, GLenum sfactor, GLenum dfactor);
/* BlendCfg() takes independent colour/alpha triples, so these are
 * pass-through, not new hardware paths. */
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
void        GLColorTable(GLcontext context, GLenum target, GLenum internalformat, GLint width, GLenum format, GLenum type, GLvoid *data);
void        GLCullFace(GLcontext context, GLenum mode);
void        GLDeleteTextures(GLcontext context, GLsizei n, const GLuint *textures);
void        GLDepthFunc(GLcontext context, GLenum func);
void        GLDepthMask(GLcontext context, GLboolean flag);
void        GLDepthRange(GLcontext context, GLclampd n, GLclampd f);
void        GLDisableClientState(GLcontext context, GLenum cap);
void        GLDrawBuffer(GLcontext context, GLenum mode);
void        GLEdgeFlag(GLcontext context, GLboolean flag);
/* GL 1.1's `const GLvoid *`, not `const GLboolean *`: GLboolean is 4 bytes
 * in this header, so that parameter would draw an incompatible-pointer
 * warning for a real GL one-byte boolean array. */
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
GLboolean   GLIsEnabled(GLcontext context, GLenum cap);
GLboolean   GLIsTexture(GLcontext context, GLuint texture);
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
void        GLTexEnvi(GLcontext context, GLenum target, GLenum pname, GLint param);
void        GLTexEnvfv(GLcontext context, GLenum target, GLenum pname, const GLfloat *params);
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

void        GLViewport(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height);

void		GLLockArrays(GLcontext context, GLuint first, GLsizei count);
void		GLUnlockArrays(GLcontext context);

void        mglChooseGuardBand(GLboolean flag);
void        mglChooseNumberOfBuffers(int number);
/* GL_EXT_shared_texture_palette's target for glColorTableEXT. REAL SPEC VALUE
 * (0x81FB), deliberately a #define rather than a member of the enum above:
 * external GL clients pass the genuine value across the API boundary, and this
 * header's enum is positionally numbered -- the same reason GL_TEXTURE0_ARB
 * carries its real value. */
#define GL_SHARED_TEXTURE_PALETTE_EXT 0x81FB

/* glBlendEquation modes. REAL SPEC VALUES, and #defines rather than members
 * of the enum above: that enum is positionally numbered and alphabetically
 * ordered, so inserting GL_FUNC_ADD into it would renumber every member after
 * it -- silently changing GL_SRC_ALPHA's value for any client already
 * compiled against the old header while the library kept the new one. All
 * five map onto real V3D blend modes (V3D_BLEND_MODE_ADD/SUB/RSUB/MIN/MAX);
 * the hardware also has MUL/SCREEN/DARKEN/LIGHTEN, which core GL has no way
 * to name. */
#define GL_FUNC_ADD                   0x8006
#define GL_MIN                        0x8007
#define GL_MAX                        0x8008
#define GL_BLEND_EQUATION             0x8009
#define GL_FUNC_SUBTRACT              0x800A
#define GL_FUNC_REVERSE_SUBTRACT      0x800B

void        mglChoosePixelDepth(int depth);       /* SCREEN bits-per-pixel; accepted and ignored -- the screen opens at the display mode's own depth (context.c) */
/* Z-BUFFER precision in bits -- NOT the screen depth above. 32 = D32F
 * (default), 16 = D16. Call before MGLCreateContext; the Z buffer is sized
 * from this at context init. Any other value is treated as 32. D16 is a
 * downgrade, offered for callers who want the memory back -- see the
 * implementation comment in gl/src/context.c. */
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
/* GL_TRUE when the display was reopened at the new size. GL_FALSE means the
 * reopen FAILED and the display is now closed -- textures and GL state are
 * kept, so a retry at a size that fits recovers. */
GLboolean   MGLResizeContext(GLcontext context, GLsizei width, GLsizei height);
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

/*
 * GLU constants, as #define and not as members of the enum above. That is not a
 * style choice: this header's enum is POSITIONAL over most of its length, so a
 * name added to it takes the next implicit value and every name after an
 * insertion point shifts -- which breaks every archive already built against the
 * old numbering. A #define cannot disturb it. GLU's own values are used, all of
 * them well clear of any GL enum.
 */
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
/* The remaining two GLU error codes, for completeness: gluErrorString names
 * them and a caller switching on the callback's argument should be able to. */
#define GLU_INCOMPATIBLE_GL_VERSION 100903
#define GLU_INVALID_OPERATION       100904

/* gluQuadricCallback's `which`. GLU defines exactly one callback for a quadric
 * and this is it (glu.h:97). */
#define GLU_ERROR          100103

/*
 * GL_CURRENT_NORMAL, GL's own value. A #define and not an enum member for the
 * same reason as the GLU constants above: the enum in this header is positional
 * over most of its length, so a bare name appended to it takes the next implicit
 * value and shifts everything after any insertion point, breaking archives
 * already built against the old numbering.
 */
#define GL_CURRENT_NORMAL  0x0B02

/*
 * Quadric object. Opaque to the application, which only passes the pointer back
 * in; the struct itself is private to glu.c.
 */
typedef struct GLUquadricObj_t GLUquadricObj;
typedef GLUquadricObj GLUquadric;

/*
 * gluQuadricCallback's function argument, DELIBERATELY UNPROTOTYPED, which is
 * what GLU does: its `_GLUfuncptr` is `void (*)()` (glu.h), so an application
 * passing its own `void f(GLenum)` -- the shape every GLU example uses -- needs
 * no cast. A concrete `void (*)(GLint)` here would make the normal usage warn.
 * Named MGLUfuncptr rather than _GLUfuncptr so that a client including a real
 * glu.h as well does not hit a duplicate typedef.
 */
typedef void (*MGLUfuncptr)();

GLint       GLUBuild2DMipmaps(GLcontext context, GLenum target, GLint internalFormat, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *data);
const GLubyte *GLUErrorString(GLenum errCode);

GLUquadricObj *GLUNewQuadric(void);
void        GLUDeleteQuadric(GLUquadricObj *q);
void        GLUQuadricNormals(GLUquadricObj *q, GLenum normals);
void        GLUQuadricTexture(GLUquadricObj *q, GLboolean textureCoords);
void        GLUQuadricDrawStyle(GLUquadricObj *q, GLenum drawStyle);
void        GLUQuadricOrientation(GLUquadricObj *q, GLenum orientation);
void        GLUQuadricCallback(GLUquadricObj *q, GLenum which, MGLUfuncptr fn);
void        GLUCylinder(GLUquadricObj *q, GLdouble base, GLdouble top, GLdouble height, GLint slices, GLint stacks);
void        GLUSphere(GLUquadricObj *q, GLdouble radius, GLint slices, GLint stacks);
void        GLUDisk(GLUquadricObj *q, GLdouble inner, GLdouble outer, GLint slices, GLint loops);

/* ==========================================================================
 * DISPLAY LISTS
 *
 * A list here is a CAPTURED VERTEX BATCH, not a recorded command stream. What
 * glNewList..glEndList keeps is the contents of the vertex and normal buffers
 * that the enclosed glBegin/glEnd blocks produced, plus the primitive and the
 * draw function chosen for each; glCallList replays a batch by pointing the
 * context's buffers at the captured arrays and running the ordinary GLEnd path.
 *
 * MESA does the same thing -- its OPCODE_VERTEX_LIST holds vertices, not one
 * command node per glVertex call -- and the reason is measurable here rather
 * than stylistic: a client application's scene03 list is a single glBegin(GL_QUADS) block of
 * 127*127*4 = 64,516 vertices. A command stream would replay 64,516 glVertex3f
 * calls every frame. The batch design replays none, and reuses d_DrawQuads'
 * existing 4-slot flat-colour batching untouched.
 *
 * Two things a caller must know, because they are consequences of the design
 * rather than oversights:
 *
 *   - CAPACITY. The capture can only hold what the vertex buffer held, so a
 *     block larger than VertexBufferSize is truncated at capture time exactly as
 *     it would be when drawn directly. scene03's 64,516 needs
 *     mglChooseVertexBufferSize before MGLInit; at the 256-entry default the
 *     list records 256 vertices and no error distinguishes that from success.
 *
 *   - NOT BAKED. Nothing is pre-transformed. Texture coordinates generated by
 *     GL_SPHERE_MAP texgen come from the modelview in force at REPLAY time, and
 *     the current colour likewise, so a list drawn under a rotating modelview
 *     still animates. Baking would have broken exactly that: scene06's ship is
 *     sphere-mapped and rotates every frame.
 * ========================================================================== */

/*
 * GL_COMPILE / GL_COMPILE_AND_EXECUTE with GL's own values, as #defines and not
 * enum members -- the same hazard the GLU constants above avoid. The big enum in
 * this header is positional over most of its length, so a bare name appended to
 * it takes the next implicit value: appending here would land on 0x0BA6..0x0BA8
 * and collide with GL_TEXTURE_MATRIX, and inserting earlier would shift every
 * member after the insertion point and break archives already built.
 */
#define GL_COMPILE              0x1300
#define GL_COMPILE_AND_EXECUTE  0x1301
#define GL_LIST_BASE            0x0B32
#define GL_LIST_INDEX           0x0B33
#define GL_LIST_MODE            0x0B30

/* Internal: GLEnd calls this to capture a block while a list is being compiled.
 * Declared here rather than in a private header because gl.h is the only header
 * vertexbuffer_min.c and dlist.c share. Returns GL_TRUE when GLEnd must NOT draw. */
GLboolean   dl_CaptureBatch(GLcontext context);

GLuint      GLGenLists(GLcontext context, GLsizei range);
void        GLDeleteLists(GLcontext context, GLuint list, GLsizei range);
GLboolean   GLIsList(GLcontext context, GLuint list);
void        GLNewList(GLcontext context, GLuint list, GLenum mode);
void        GLEndList(GLcontext context);
void        GLCallList(GLcontext context, GLuint list);

/* ==========================================================================
 * LIGHTING
 *
 * GL 1.1 fixed-function lighting. The per-vertex lit colour is computed by a
 * QPU VERTEX shader, not on the CPU: the shader occupies a reclaimed dead
 * shader slot and writes the lit colour out on the varying the smooth shaders
 * already carry, so a lit draw costs no new attribute record and no new
 * varying. What stays on the CPU is this state, its defaults, and the
 * light x material fold that turns it into the shader's uniform tail.
 *
 * ONE capability is deliberately NOT implemented, and saying so here is
 * cheaper than having it discovered:
 *
 *   - GL_LIGHT_MODEL_TWO_SIDE. The vertex stage has no facing input and there
 *     is no front/back varying select, so a back-facing vertex cannot be given
 *     the back material. Accepted and stored, never acted on.
 *
 * GL_COLOR_MATERIAL IS implemented, and costs a lit draw the colour attribute
 * record that being lit otherwise frees, plus its own vertex shader per lit
 * shape. It is paid only by a draw that enables it; see glColorMaterial.
 *
 * GL_SPECULAR is stored and folded but the shader does not yet consume it; see
 * the note on GLMaterialf in light.c.
 * ========================================================================== */

/*
 * The lighting tokens with GL's own values, as #defines and not enum members --
 * the same hazard GL_COMPILE and the GLU constants above avoid. The big enum in
 * this header is positional up to GL_LINE_WIDTH, so a bare name appended to it
 * takes the next implicit value: nine bare names would land on 0x0BA6..0x0BA8
 * and the third would collide with GL_TEXTURE_MATRIX -- and that third name is
 * GL_LIGHT1, which is the light every one of a client application's glLightfv calls names.
 *
 * Do not be alarmed by 0x4000 appearing twice in this header: the other one is
 * GLROT_011, a bit in the matrix-flags family (GLROT_* above), which is not a
 * GLenum token and is never compared against one.
 *
 * GL_FRONT, GL_BACK and GL_FRONT_AND_BACK are NOT here -- they are already
 * positional members of the enum above. GLMaterial* therefore accepts both
 * those positional values and GL's real 0x0404/0x0405/0x0408, exactly as
 * GL_SPHERE_MAP is accepted in both spellings (see draw.c's
 * MGL_SPHERE_MAP_SPEC). The positional members stop at 240, so the two
 * numberings cannot collide.
 */
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

/* GL_NORMALIZE and GL_RESCALE_NORMAL are absent ON PURPOSE, not overlooked.
 * Without them the GL-conformant normal transform is n' = n . M_inv3x3 with NO
 * renormalisation, which is what lets the light direction be carried into
 * object space once per draw instead of every normal being transformed and
 * renormalised per vertex. Adding the token would oblige the shader to
 * renormalise, so do not add it without the shader work to honour it. */

void        GLLightf(GLcontext context, GLenum light, GLenum pname, GLfloat param);
void        GLLightfv(GLcontext context, GLenum light, GLenum pname, const GLfloat *params);
void        GLMaterialf(GLcontext context, GLenum face, GLenum pname, GLfloat param);
void        GLMaterialfv(GLcontext context, GLenum face, GLenum pname, const GLfloat *params);
void        GLColorMaterial(GLcontext context, GLenum face, GLenum mode);
void        GLLightModelf(GLcontext context, GLenum pname, GLfloat param);
void        GLLightModelfv(GLcontext context, GLenum pname, const GLfloat *params);
void        GLGetLightfv(GLcontext context, GLenum light, GLenum pname, GLfloat *params);
void        GLGetMaterialfv(GLcontext context, GLenum face, GLenum pname, GLfloat *params);

/* Internal: rebuild the folded light x material products. Called by the setters
 * above and by MGLSetState when GL_LIGHTING or a GL_LIGHTi changes; declared
 * here because gl.h is the only header light.c and context.c share. */
void        light_Fold(GLcontext context);

/* Internal: the GL 1.1 lighting defaults, once per context. They live in light.c
 * beside the code that reads them rather than in context.c's defaults block,
 * because two of them are easy to get wrong and the reasoning belongs next to
 * the values -- light 0's defaults differ from lights 1-7, and the default light
 * is directional. */
void        light_SetDefaults(GLcontext context);

GLint       mglGetSupportedScreenModes(MGLScreenModeCallback CallbackFn);
void *      MGLCreateContextFromID(GLint ID, GLint *w, GLint *h);
GLboolean   MGLLockBack(GLcontext context, MGLLockInfo *info);

/*
 * Render into a window the APPLICATION already opened, instead of the driver
 * opening its own. For hosts that own their window -- any AmigaOS program
 * with its own event loop and layout.
 *
 * `window` must stay open for the life of the context; the driver never closes
 * it and never unlocks its screen. Size comes from the window's INNER area, so
 * the borders are excluded. Everything after creation behaves like an
 * ordinary windowed context: the per-frame present is the same ClipBlit, and
 * MGLDeleteContext frees the off-screen bitmap, rastport and backend state
 * while leaving the window itself alone.
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
 * every reason to think is safe.
 */
void *      MGLCreateContextFromWindow(struct Window *window);

/*
 * Render into a bitmap the APPLICATION owns, and presume nothing about how it
 * reaches the screen.
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
 * bitmap would render garbage rather than fail cleanly. Both are checked and
 * the call refused. AllocBitMap(w, h, 32, BMF_MINPLANES|BMF_DISPLAYABLE, friend)
 * is what the driver allocates for itself and is what to pass.
 *
 * Note this is a BYTES-PER-PIXEL requirement, not a depth one. A truecolour
 * bitmap reports GetBitMapAttr(BMA_DEPTH) == 24 on CGX -- alpha is not
 * depth -- so the check tests bytes-per-row, not depth.
 *
 * The bitmap must outlive the context, and the driver never frees it. Size is
 * taken from the bitmap itself.
 *
 * Returns NULL on failure. The MGLTerm shutdown-order note above applies here
 * too if the host uses the driver's IntuitionBase.
 *
 * This is MiniGLV3D's own API, which no existing MiniGL program expects;
 * MGLCreateContextFromWindow, by contrast, keeps MiniGL V18's name.
 */
void *      MGLCreateContextFromBitMap(struct BitMap *bitmap);

void        GLEnableClientState(GLcontext context, GLenum state);
void        GLDisableClientState(GLcontext context, GLenum state);

void        GLTexCoordPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void        GLColorPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void        GLVertexPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void        GLNormalPointer(GLcontext context, GLenum type, GLsizei stride, const GLvoid *pointer);

/*
 * THE GL ERROR CODES CARRY OPENGL'S OWN VALUES, as #defines and NOT as members
 * of the alphabetical enum above. An enumerator there would be auto-numbered, so
 * glGetError would answer e.g. 0x58 where a real GL header says 0x0500, and any
 * program comparing against that header's value would get the wrong answer.
 *
 * They are #defines for the same reason GL_NORMAL_ARRAY below is: that enum is
 * auto-numbered, so giving these six explicit values in place would have shifted
 * every token after them and broken the values baked into already-compiled
 * clients. Their enum slots are kept as GL_reserved_was_* placeholders instead,
 * which holds the numbering of everything else exactly where it was.
 *
 * GL_NO_ERROR stays an enumerator: it is explicitly 0 up there, which is already
 * OpenGL's value, so nothing needed to move.
 */
#define GL_INVALID_ENUM       0x0500
#define GL_INVALID_VALUE      0x0501
#define GL_INVALID_OPERATION  0x0502
#define GL_STACK_OVERFLOW     0x0503
#define GL_STACK_UNDERFLOW    0x0504
#define GL_OUT_OF_MEMORY      0x0505

/*
 * POLYGON MODES. GL_FILL is already an enumerator above; without GL_POINT and
 * GL_LINE an application could not name those two at all and glPolygonMode
 * could be given nothing but GL_FILL. They are #defines carrying
 * OpenGL's own values for the reason the error codes above are: adding them to
 * that auto-numbered enum would shift every token after them and break the
 * values compiled into existing clients. GL_FILL keeps its enumerator value --
 * the three only have to be distinct, and these two are far above anything the
 * enum reaches.
 */
#define GL_POINT              0x1B00
#define GL_LINE               0x1B01

/*
 * Vertex-array normals, carrying OpenGL's own values like everything else
 * here. glNormalPointer is dispatched to library clients, so without these a
 * client could call it and then have no way to name the array to
 * glEnableClientState -- the entry point reachable, the feature not.
 */
#define GL_NORMAL_ARRAY          0x8075
#define GL_NORMAL_ARRAY_TYPE     0x807E
#define GL_NORMAL_ARRAY_STRIDE   0x807F
#define GL_NORMAL_ARRAY_POINTER  0x808F

void        GLDrawElements(GLcontext context, GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
void        GLDrawArrays(GLcontext context, GLenum mode, GLint first, GLsizei count);
void        GLMultiDrawArrays(GLcontext context, GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount);

/*
** The OpenGL compatibility macros. Each expands to the GL* entry point with the
** current context threaded through, so a client dereferences NOTHING of
** GLcontext -- the struct's layout is the library's business alone, and
** structSize guards the one thing a client does depend on, the dispatch table.
**
** THERE IS NO SECOND PATH HERE. A USE_MGLAPI alternative that inlined the hot
** entry points into the caller would be a compatibility surface no build
** compiles, so nothing would keep it honest -- and its glArrayElement must
** carry both the UsedArrayElement flag and the texcoord snapshot, either of
** which fails silently: the first loses the geometry outright, the second
** stamps a whole strip with one corner's texcoord. Every GLcontext change
** would have to reason about it.
**
** THE SWITCH ITSELF IS LOAD-BEARING: <libraries/minigl_dispatch.h> defines
** USE_MGLAPI around its own includes and restores it afterwards, so that these
** macros are suppressed and its dispatch wrappers define the gl* names instead.
** A library client must not get GLFoo(mini_CurrentContext, ...) -- that compiles
** this driver's context layout into the client. The block below is guarded that
** way, as <mgl/glut.h> guards its glut* block, so the client copy of this header
** needs no divergence from this one -- it is kept in step by copying this one
** over it.
*/

#ifndef USE_MGLAPI

#define glActiveTextureARB(unit)                GLActiveTextureARB(mini_CurrentContext, unit)

#define glClientActiveTextureARB(unit)           GLClientActiveTextureARB(mini_CurrentContext, unit)

/* The unsuffixed spellings multitexture took core in GL 1.3. Aliases onto the
 * same entry points, so they need no dispatch slot of their own -- the driver
 * side is one function either way. <libraries/minigl_dispatch.h> repeats them
 * for a library client, because this block is hidden when USE_MGLAPI is set. */
#define glActiveTexture(unit)                   GLActiveTextureARB(mini_CurrentContext, unit)

#define glClientActiveTexture(unit)             GLClientActiveTextureARB(mini_CurrentContext, unit)

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

#define glGetBooleanv(pname, params) GLGetBooleanv(mini_CurrentContext, pname, params)

#define glGetDoublev(pname, params) GLGetDoublev(mini_CurrentContext, pname, params)

#define glGetFloatv(pname, params) GLGetFloatv(mini_CurrentContext, pname, params)

#define glGetIntegerv(pname, params) GLGetIntegerv(mini_CurrentContext, pname, params)

#define glGetPointerv(pname, params) GLGetPointerv(mini_CurrentContext, pname, params)

#define glGetPointervEXT(pname, params) GLGetPointerv(mini_CurrentContext, pname, params)

#define glGetString(name) GLGetString(mini_CurrentContext, name)

#define glHint(target, mode) GLHint(mini_CurrentContext, target, mode)

#define glIndexi(c) GLIndexi(mini_CurrentContext, c)

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

#define glVertex4fv(v)      GLVertex4fv(mini_CurrentContext, v)
#define glVertex3fv(v)      GLVertex3fv(mini_CurrentContext, v)
#define glVertex2fv(v)     GLVertex2fv(mini_CurrentContext, v)


#define glVertexPointer(size, type, stride, pointer) GLVertexPointer(mini_CurrentContext, size, type, stride, pointer)

#define glVertexPointerEXT(size, type, stride, pointer) GLVertexPointer(mini_CurrentContext, size, type, stride, pointer)

#define glDepthRange(n,f) GLDepthRange(mini_CurrentContext, n,f)

#define glLockArrays(f,c) GLLockArrays(mini_CurrentContext, f,c)
#define glUnlockArrays() GLUnlockArrays(mini_CurrentContext)

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

#define glNormal3fv(v)    GLNormal3fv(mini_CurrentContext, v)

#define glcopTexEnvi(target, pname, param) GLTexEnvi(mini_CurrentContext, target, pname, param)

#define glTexEnvf(target, pname, param)  GLTexEnvi(mini_CurrentContext, target, pname, (GLint)param)

#define glTexEnvi(target, pname, param)  GLTexEnvi(mini_CurrentContext, target, pname, (GLint)param)

#define glTexEnviv(target, pname, param) GLTexEnvi(mini_CurrentContext, target, pname, *(param))

/* A four-float entry point of its own: GL_TEXTURE_ENV_COLOR cannot be carried
 * by the integer spelling, and GL_BLEND needs that colour. */
#define glTexEnvfv(target, pname, param) GLTexEnvfv(mini_CurrentContext, target, pname, param)

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
 * whichever one was current before. */
#define mglCreateContextFromWindow(win) (mini_CurrentContext = MGLCreateContextFromWindow(win))
#define mglCreateContextFromBitMap(bm)  (mini_CurrentContext = MGLCreateContextFromBitMap(bm))

#define mglLockBack(info) MGLLockBack(mini_CurrentContext, info)

#define glTexCoordPointer(size, type, stride, pointer) GLTexCoordPointer(mini_CurrentContext, size, type, stride, pointer)

#define glColorPointer(size, type, stride, pointer) GLColorPointer(mini_CurrentContext, size, type, stride, pointer)

#define glVertexPointer(size, type, stride, pointer) GLVertexPointer(mini_CurrentContext, size, type, stride, pointer)

#define glNormalPointer(type, stride, pointer) GLNormalPointer(mini_CurrentContext, type, stride, pointer)

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

/*
 * No trailing semicolon in either body. A function-like macro that ends in one
 * expands `if (c) gluPerspective(...); else ...` into
 * `if (c) GLUPerspective(...);; else ...`, where the empty statement leaves the
 * else with no matching if -- the compiler reports "expected '}' before 'else'"
 * at the call site and the whole translation unit fails. Every other macro in
 * this header already omits it.
 *
 * Callers write their own semicolon, so the expansion is a single statement.
 */
#define gluLookAt(ex, ey, ez, cx, cy, cz, ux, uy, uz) GLULookAt(ex, ey, ez, cx, cy, cz, ux, uy, uz)
#define gluPerspective(fovy, aspect, znear, zfar) GLUPerspective(fovy, aspect, znear, zfar)

/*
 * glTexCoord2d. GL defines both the float and the double form; this context
 * carries the float one, so the double form is a narrowing cast and nothing
 * more. A macro rather than a function so it costs no call, and there is no
 * rounding question because float-to-float narrowing is not float-to-int.
 */
#define glTexCoord2d(s, t) GLTexCoord2f(mini_CurrentContext, (GLfloat)(s), (GLfloat)(t))

/*
 * THE REMAINING GL 1.1 SPELLINGS of glTexCoord and glVertex. GL names every
 * combination of 1-4 components and d/f/i/s, each with a vector form; without
 * all of them a program written against standard GL headers fails to compile
 * rather than failing at run time.
 *
 * Pure aliases onto entry points that already exist, which is why they need no
 * dispatch slot: 1-component texcoords default t to 0, 3-component ones pass r
 * and the driver drops it, 2-component vertices go to the 2f entry point and
 * 3-component ones default w to 1. Vector forms expand elementwise, there being
 * no GL*4dv entry point to call.
 *
 * Both linkages must carry every one of these spellings, and 44 macros here
 * plus 44 more in <libraries/minigl_dispatch.h> are too many to keep in step by
 * eye.
 */
#define glTexCoord1d(s) GLTexCoord2f(mini_CurrentContext, (GLfloat)(s), 0.f)
#define glTexCoord1dv(v) GLTexCoord2f(mini_CurrentContext, (GLfloat)(v)[0], 0.f)
#define glTexCoord1f(s) GLTexCoord2f(mini_CurrentContext, (GLfloat)(s), 0.f)
#define glTexCoord1fv(v) GLTexCoord2f(mini_CurrentContext, (GLfloat)(v)[0], 0.f)
#define glTexCoord1i(s) GLTexCoord2f(mini_CurrentContext, (GLfloat)(s), 0.f)
#define glTexCoord1iv(v) GLTexCoord2f(mini_CurrentContext, (GLfloat)(v)[0], 0.f)
#define glTexCoord1s(s) GLTexCoord2f(mini_CurrentContext, (GLfloat)(s), 0.f)
#define glTexCoord1sv(v) GLTexCoord2f(mini_CurrentContext, (GLfloat)(v)[0], 0.f)
#define glTexCoord2dv(v) GLTexCoord2f(mini_CurrentContext, (GLfloat)(v)[0], (GLfloat)(v)[1])
#define glTexCoord2i(s, t) GLTexCoord2f(mini_CurrentContext, (GLfloat)(s), (GLfloat)(t))
#define glTexCoord2iv(v) GLTexCoord2f(mini_CurrentContext, (GLfloat)(v)[0], (GLfloat)(v)[1])
#define glTexCoord2s(s, t) GLTexCoord2f(mini_CurrentContext, (GLfloat)(s), (GLfloat)(t))
#define glTexCoord2sv(v) GLTexCoord2f(mini_CurrentContext, (GLfloat)(v)[0], (GLfloat)(v)[1])
#define glTexCoord3d(s, t, r) GLTexCoord4f(mini_CurrentContext, (GLfloat)(s), (GLfloat)(t), (GLfloat)(r), 1.f)
#define glTexCoord3dv(v) GLTexCoord4f(mini_CurrentContext, (GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], 1.f)
#define glTexCoord3f(s, t, r) GLTexCoord4f(mini_CurrentContext, (GLfloat)(s), (GLfloat)(t), (GLfloat)(r), 1.f)
#define glTexCoord3fv(v) GLTexCoord4f(mini_CurrentContext, (GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], 1.f)
#define glTexCoord3i(s, t, r) GLTexCoord4f(mini_CurrentContext, (GLfloat)(s), (GLfloat)(t), (GLfloat)(r), 1.f)
#define glTexCoord3iv(v) GLTexCoord4f(mini_CurrentContext, (GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], 1.f)
#define glTexCoord3s(s, t, r) GLTexCoord4f(mini_CurrentContext, (GLfloat)(s), (GLfloat)(t), (GLfloat)(r), 1.f)
#define glTexCoord3sv(v) GLTexCoord4f(mini_CurrentContext, (GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], 1.f)
#define glTexCoord4d(s, t, r, q) GLTexCoord4f(mini_CurrentContext, (GLfloat)(s), (GLfloat)(t), (GLfloat)(r), (GLfloat)(q))
#define glTexCoord4dv(v) GLTexCoord4f(mini_CurrentContext, (GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], (GLfloat)(v)[3])
#define glTexCoord4i(s, t, r, q) GLTexCoord4f(mini_CurrentContext, (GLfloat)(s), (GLfloat)(t), (GLfloat)(r), (GLfloat)(q))
#define glTexCoord4iv(v) GLTexCoord4f(mini_CurrentContext, (GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], (GLfloat)(v)[3])
#define glTexCoord4s(s, t, r, q) GLTexCoord4f(mini_CurrentContext, (GLfloat)(s), (GLfloat)(t), (GLfloat)(r), (GLfloat)(q))
#define glTexCoord4sv(v) GLTexCoord4f(mini_CurrentContext, (GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], (GLfloat)(v)[3])

#define glVertex2d(x, y) GLVertex2f(mini_CurrentContext, (GLfloat)(x), (GLfloat)(y))
#define glVertex2dv(v) GLVertex2f(mini_CurrentContext, (GLfloat)(v)[0], (GLfloat)(v)[1])
#define glVertex2iv(v) GLVertex2f(mini_CurrentContext, (GLfloat)(v)[0], (GLfloat)(v)[1])
#define glVertex2s(x, y) GLVertex2f(mini_CurrentContext, (GLfloat)(x), (GLfloat)(y))
#define glVertex2sv(v) GLVertex2f(mini_CurrentContext, (GLfloat)(v)[0], (GLfloat)(v)[1])
#define glVertex3d(x, y, z) GLVertex4f(mini_CurrentContext, (GLfloat)(x), (GLfloat)(y), (GLfloat)(z), 1.f)
#define glVertex3dv(v) GLVertex4f(mini_CurrentContext, (GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], 1.f)
#define glVertex3i(x, y, z) GLVertex4f(mini_CurrentContext, (GLfloat)(x), (GLfloat)(y), (GLfloat)(z), 1.f)
#define glVertex3iv(v) GLVertex4f(mini_CurrentContext, (GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], 1.f)
#define glVertex3s(x, y, z) GLVertex4f(mini_CurrentContext, (GLfloat)(x), (GLfloat)(y), (GLfloat)(z), 1.f)
#define glVertex3sv(v) GLVertex4f(mini_CurrentContext, (GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], 1.f)
#define glVertex4d(x, y, z, w) GLVertex4f(mini_CurrentContext, (GLfloat)(x), (GLfloat)(y), (GLfloat)(z), (GLfloat)(w))
#define glVertex4dv(v) GLVertex4f(mini_CurrentContext, (GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], (GLfloat)(v)[3])
#define glVertex4i(x, y, z, w) GLVertex4f(mini_CurrentContext, (GLfloat)(x), (GLfloat)(y), (GLfloat)(z), (GLfloat)(w))
#define glVertex4iv(v) GLVertex4f(mini_CurrentContext, (GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], (GLfloat)(v)[3])
#define glVertex4s(x, y, z, w) GLVertex4f(mini_CurrentContext, (GLfloat)(x), (GLfloat)(y), (GLfloat)(z), (GLfloat)(w))
#define glVertex4sv(v) GLVertex4f(mini_CurrentContext, (GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], (GLfloat)(v)[3])

/* The EXT spellings of the compiled-vertex-array lock, which the other EXT
 * array aliases here already have (glDrawArraysEXT and friends). Same entry
 * points; EXT_compiled_vertex_array is where these names come from. */
#define glLockArraysEXT(f, c) GLLockArrays(mini_CurrentContext, f, c)
#define glUnlockArraysEXT()   GLUnlockArrays(mini_CurrentContext)

/*
 * GLU. gluBuild2DMipmaps takes the context because it saves, forces and
 * restores the unpack state around the levels it generates; the quadrics make
 * only public gl calls and need none.
 */
#define gluBuild2DMipmaps(target, internalFormat, width, height, format, type, data) GLUBuild2DMipmaps(mini_CurrentContext, target, internalFormat, width, height, format, type, data)
#define gluErrorString(errCode) GLUErrorString(errCode)

#define gluNewQuadric() GLUNewQuadric()
#define gluDeleteQuadric(q) GLUDeleteQuadric(q)
#define gluQuadricNormals(q, normals) GLUQuadricNormals(q, normals)
#define gluQuadricTexture(q, textureCoords) GLUQuadricTexture(q, textureCoords)
#define gluQuadricDrawStyle(q, drawStyle) GLUQuadricDrawStyle(q, drawStyle)
#define gluQuadricOrientation(q, orientation) GLUQuadricOrientation(q, orientation)
#define gluQuadricCallback(q, which, fn) GLUQuadricCallback(q, which, fn)
#define gluCylinder(q, base, top, height, slices, stacks) GLUCylinder(q, base, top, height, slices, stacks)
#define gluSphere(q, radius, slices, stacks) GLUSphere(q, radius, slices, stacks)
#define gluDisk(q, inner, outer, slices, loops) GLUDisk(q, inner, outer, slices, loops)

/* Display lists. Same context-injecting shape as every other gl* macro here. */
#define glGenLists(range) GLGenLists(mini_CurrentContext, range)
#define glDeleteLists(list, range) GLDeleteLists(mini_CurrentContext, list, range)
#define glIsList(list) GLIsList(mini_CurrentContext, list)
#define glNewList(list, mode) GLNewList(mini_CurrentContext, list, mode)
#define glEndList() GLEndList(mini_CurrentContext)
#define glCallList(list) GLCallList(mini_CurrentContext, list)

/* Lighting. Same context-injecting shape. The i and d spellings go through the
 * float entry points rather than getting their own: GL defines them as the same
 * state with a different argument type, and a light or material parameter is a
 * float everywhere in this driver. */
#define glLightf(light, pname, param) GLLightf(mini_CurrentContext, light, pname, param)
#define glLightfv(light, pname, params) GLLightfv(mini_CurrentContext, light, pname, params)
#define glLighti(light, pname, param) GLLightf(mini_CurrentContext, light, pname, (GLfloat)(param))
#define glMaterialf(face, pname, param) GLMaterialf(mini_CurrentContext, face, pname, param)
#define glMaterialfv(face, pname, params) GLMaterialfv(mini_CurrentContext, face, pname, params)
#define glMateriali(face, pname, param) GLMaterialf(mini_CurrentContext, face, pname, (GLfloat)(param))
#define glColorMaterial(face, mode) GLColorMaterial(mini_CurrentContext, face, mode)
#define glLightModelf(pname, param) GLLightModelf(mini_CurrentContext, pname, param)
#define glLightModelfv(pname, params) GLLightModelfv(mini_CurrentContext, pname, params)
#define glLightModeli(pname, param) GLLightModelf(mini_CurrentContext, pname, (GLfloat)(param))
#define glGetLightfv(light, pname, params) GLGetLightfv(mini_CurrentContext, light, pname, params)
#define glGetMaterialfv(face, pname, params) GLGetMaterialfv(mini_CurrentContext, face, pname, params)

#endif /* USE_MGLAPI not defined -- a library client gets its gl*, glu* and
        * mgl* macros from <libraries/minigl_dispatch.h> instead, the same way
        * <mgl/glut.h> hands over its glut* block */


#ifdef __cplusplus
}
#endif


#endif

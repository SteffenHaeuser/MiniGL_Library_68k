/*
 * $Id: context.h,v 1.1.1.1 2000/04/07 19:44:51 tfrieden Exp $
 *
 * $Date: 2000/04/07 19:44:51 $
 * $Revision: 1.1.1.1 $
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

/*
 * MiniGLV3D fork of MiniGL/include/mgl/context.h.
 *
 * GLcontext_t is redefined around V3DDevice/V3DContext instead of
 * W3D_Context. MiniGL's GL-API bookkeeping (matrix stacks, vertex/normal
 * buffers, boolean GL_* state) is kept -- this is not a redesign of
 * MiniGL's state machine. The original's fields that named a Warp3D type
 * directly could not compile without Warp3D.h; they, and its other w3d*
 * fields, are handled as follows:
 *  - W3D_Context* w3dContext -> V3DDevice device and V3DContext backend,
 *    embedded directly rather than through a pointer.
 *  - W3D_Fog w3dFog, W3D_Scissor scissor -> dropped; the backend context
 *    holds this state (backend.fog_*, backend.scissor_*). w3dFogMode is
 *    dropped too: backend.fog_mode is the one source of truth.
 *  - W3D_Color CurrentColor -> MGLColor CurrentColor (a plain r/g/b/a
 *    float struct).
 *  - W3D_Double ClearDepth -> double, W3D_Double's own underlying type.
 *  - W3D_Texture** w3dTexBuffer, GLubyte** w3dTexMemory, GLint
 *    TexBufferSize -> V3DTexture** textureObjects (backend texture objects
 *    indexed by GL texture name) + textureObjectCount. w3dTexMemory (a
 *    parallel raw-CPU-data mirror per Warp3D texture) has no analog --
 *    V3DTexture owns its uploaded GPU memory directly (texture_mem), with
 *    no separate CPU-side copy. The texture CURRENTLY BOUND to each unit
 *    (as opposed to this all-textures-by-name array) is V3DContext's job
 *    (backend.bound_texture[V3D_MAX_TEXUNIT]); texture.c binds by writing
 *    backend.bound_texture[unit].
 *  - ULONG w3dChipID, w3dFormat, w3dAlphaFormat; GLint w3dBytesPerTexel
 *    -> dropped. They held the Warp3D chip ID (for a chip-specific
 *    texcoord quirk) and the texture formats and texel size used on that
 *    card; V3D is one fixed, known target (RGBA8 always), with nothing to
 *    choose.
 *  - struct Window* w3dWindow, struct Screen* w3dScreen, struct BitMap*
 *    w3dBitMap, struct RastPort* w3dRastPort, GLboolean w3dLocked -> kept
 *    (not Warp3D types), renamed v3dWindow, v3dScreen, v3dBitMap,
 *    v3dRastPort, v3dLocked. struct ScreenBuffer* Buffers[3] is kept
 *    unchanged. The fields bitmapLock, vmembase and bprow are new: the
 *    bitmap lock, and the direct framebuffer address and stride it
 *    provides (context.c).
 */

#ifndef __CONTEXT_H
#define __CONTEXT_H

#include "mgl/matrix.h"
#include "mgl/config.h"
#include "mgl/vertexbuffer.h"

#include "../../../backend/include/v3d_device.h"
#include "../../../backend/include/v3d_context.h"
#include "../../../backend/include/v3d_texture.h"

/* Amiga alignment for the OS structs intuition.h declares, and for
 * LockTimeHandle below. VBCC needs it told; GCC lays these out that way of its
 * own accord and only warns -Wunknown-pragmas at every include site, so the
 * pair is guarded rather than dropped. Guarding changes no layout for either
 * compiler -- GCC already ignored both lines. */
#ifdef __VBCC__
#pragma amiga-align
#endif

#include <intuition/intuition.h>

typedef struct LockTimeHandle_s
{
	ULONG s_hi, s_lo;
	ULONG e_freq;
} LockTimeHandle;

#ifdef __VBCC__
#pragma default-align
#endif

typedef struct GLcontext_t * GLcontext;

struct GLcontext_t;

typedef void (*DrawFn)(struct GLcontext_t *);

typedef enum
{
	MGLKEY_F1, MGLKEY_F2, MGLKEY_F3, MGLKEY_F4, MGLKEY_F5, MGLKEY_F6, MGLKEY_F7, MGLKEY_F8,
	MGLKEY_F9, MGLKEY_F10,
	MGLKEY_CUP, MGLKEY_CDOWN, MGLKEY_CLEFT, MGLKEY_CRIGHT
} MGLspecial;

typedef enum
{
	GLCS_TEXTURE = 0x01,
	GLCS_COLOR   = 0x02,
	GLCS_VERTEX  = 0x04,
	/* Vertex-array multitexture: unit 1's own GL_TEXTURE_COORD_ARRAY
	 * enable, set/cleared by GLEnableClientState/GLDisableClientState
	 * while ClientActiveTexture==1 -- see GatherVertexFromArray/
	 * GatherVertexFromIndex for the consuming side. */
	GLCS_TEXTURE1 = 0x08,
	/* GL_INDEX_ARRAY and GL_EDGE_FLAG_ARRAY: tracked so
	 * glEnableClientState accepts them, never drawn from -- see
	 * GLIndexPointer (vertexarray.c). */
	GLCS_INDEX    = 0x10,
	GLCS_EDGEFLAG = 0x20,
	/* GL_NORMAL_ARRAY. Unlike INDEX and EDGEFLAG above, this one IS drawn
	 * from: it is what lets an array draw carry a per-vertex normal instead
	 * of pinning every vertex to NormalBuffer[0]. See GLNormalPointer and
	 * GatherVertexFromArray (vertexarray.c). */
	GLCS_NORMAL   = 0x40,
	GLCS_MASK    = 0x7F,
} ClientStates;

typedef void (*KeyHandlerFn)(char key);
typedef void (*SpecialHandlerFn)(MGLspecial special_key);
typedef void (*MouseHandlerFn)(GLint x, GLint y, GLbitfield buttons);
typedef void (*IdleFn)(void);


/*
ArrayPointer and stride storage:

We define all pointers as UBYTE to avoid problems with badly aligned arrays.
*/


#define AP_FIXPOINT           1<<0
#define AP_COMPILED           1<<1
#define AP_CLIP_BYPASS        1<<2
#define AP_CHECK_OUTCODES     1<<3
#define AP_VOLATILE           1<<4 //current array range is "recycled" after drawing operation so it is okay to modify data in the current arrays.


/*
 * The original's w_buffer/w_off fields existed purely to feed Warp3D's own
 * hardware array-pointer register a synthetic W component via raw pointer
 * arithmetic (see the MiniGL source's own GLTexCoordPointer/
 * GLVertexPointer) -- a hardware-interface trick with no V3D equivalent,
 * since this port always gathers app-array data into
 * context->VertexBuffer[] itself rather than handing a raw pointer+stride
 * to a hardware register the way Warp3D did. They are dropped;
 * texcoordsize/vertexsize are added instead (the gather step needs to
 * know how many components the app actually supplied, not a byte offset
 * into someone else's buffer). colormode is repurposed too -- see
 * MGLAColorMode below. GL_INT vertex arrays keep the original's
 * AP_FIXPOINT flag in `state` (set/cleared by GLVertexPointer, cleared by
 * GLInterleavedArrays); the gather step converts the ints with (float),
 * which is the original's value semantics -- see vertexarray.c's header
 * comment.
 */
typedef enum
{
	MGLA_COLOR_UBYTE_RGB  = 0,
	MGLA_COLOR_UBYTE_RGBA = 1,
	MGLA_COLOR_FLOAT_RGB  = 2,
	MGLA_COLOR_FLOAT_RGBA = 3,

	/* glColorPointer(4, MGL_UBYTE_ARGB, ...): a real, distinct memory
	 * layout, not just a differently-named alias for RGBA. Byte order as
	 * in the original's Convert_UB_ARGB: byte0=A, byte1=R, byte2=G,
	 * byte3=B, a genuine 4-way permutation. See vertexarray.c's own
	 * GLColorPointer/GatherVertexFromArray comments for the gather-side
	 * implementation. */
	MGLA_COLOR_UBYTE_ARGB = 4,

	/* MGL_UBYTE_BGRA: the original's Convert_UB_BGR/Convert_UB_BGRA read
	 * byte0=B, byte1=G, byte2=R and, for size 4, byte3=A. Size 3 -> BGR,
	 * size 4 -> BGRA. */
	MGLA_COLOR_UBYTE_BGR  = 5,
	MGLA_COLOR_UBYTE_BGRA = 6
} MGLAColorMode;

typedef struct MGLAPointer_s
{
	GLubyte	*texcoords;	//application array
	GLint	texcoordstride;
	GLint	texcoordsize;	//1-4 as supplied by glTexCoordPointer; only the first 2 (u,v) are gathered -- q is stored as 1.0, so an array's q has no effect (glTexCoord4f's q does)

	/* Vertex-array multitexture: unit 1's own texcoord array, set by
	 * glTexCoordPointer while ClientActiveTexture==1 -- mirrors the unit-0
	 * fields above exactly. See GLCS_TEXTURE1 (this file) and
	 * GatherVertexFromArray/GatherVertexFromIndex (vertexarray.c/
	 * vertexelements.c) for the consuming side. */
	GLubyte	*texcoords1;	//application array, unit 1
	GLint	texcoordstride1;
	GLint	texcoordsize1;

	GLubyte	*colors;	//application array
	GLint	colorstride;
	ULONG	colormode;	//MGLAColorMode, NOT a w3d bitfield

	GLubyte	*verts;		//application array
	GLint	vertexstride;
	GLint	vertexsize;	//2-4 as supplied by glVertexPointer; w defaults to 1.0 if not supplied (size<4)

	//GL_EXT_compiled_vertex_arrays:
	GLuint	lockfirst;	//start of locked range
	GLsizei	locksize;
	GLuint	transformed;	//vertexbuffer offset

	GLbitfield	state;	//pipeline state (AP_##)

} MGLAPointer;


typedef struct GLarray_t
{
	/*
	** Vertex array
	*/

	GLint       size;           /* Number of elements per entry (mostly 3 or 4) */
	GLenum      type;           /* Data type of entries */
	GLsizei     stride;         /* How to reach the next array element */
	GLvoid*     pointer;        /* Pointer to the actual data */
} GLarray;

/*
 * Lighting state. GL 1.1's full per-light and material parameter set is stored
 * whether or not the shader consumes it yet, for one reason: glGetLightfv and
 * glGetMaterialfv must answer with what the application set, and a parameter
 * that is dropped on the way in cannot be answered with later. What is NOT
 * consumed is documented in gl.h rather than being silently absent here.
 *
 * MGL_MAX_LIGHTS is 8, GL 1.1's minimum and, in this driver, its maximum: the
 * lit shader unrolls each light inline because the QPU text assembler rejects
 * branch instructions, so the number of lights a variant supports is baked into
 * that variant. Enabling more lights than the widest variant supports clamps,
 * with one line on the serial console naming the clamp -- it does not fail.
 */
#define MGL_MAX_LIGHTS 8

/* What glGetIntegerv(GL_MAX_TEXTURE_SIZE) answers AND what glTexImage2D
 * rejects above -- ONE number, never two copies that have to agree.
 *
 * It is V3D 4.2's OWN LIMIT, 4096, and not a figure chosen here. A lower value
 * makes glTexImage2D refuse textures the hardware samples perfectly well, with
 * GL_INVALID_VALUE and no upload, so the draw shows blank geometry instead of
 * failing loudly: a client application's credits scroller alone is 512x2048, and it is the
 * only one of its 121 textures above 1024.
 *
 * 4096 agrees with MESA: V3D_MAX_MIP_LEVELS is 13 (v3d_limits.h), i.e. 4096
 * down to 1x1. Base MiniGL answers 256, which is not a hardware limit either,
 * and an application that sizes its textures from the answer downscales
 * anything larger to fit it.
 *
 * COST, stated rather than implied: nothing, until an application asks for it.
 * A 4096x4096 RGBA8 level 0 would be 64MB plus a third for its chain, which is
 * the application's choice to make on a target with RAM to spare. */
#define MGL_MAX_TEXTURE_SIZE 4096

typedef struct MGLLight_t
{
	GLfloat     Ambient[4];
	GLfloat     Diffuse[4];
	GLfloat     Specular[4];

	/* EYE SPACE, not the coordinates the application passed. GL specifies that
	 * glLightfv(GL_POSITION) transforms its argument by the MODELVIEW MATRIX IN
	 * FORCE AT THE TIME OF THE CALL, so the transform happens in the setter and
	 * this field is already in eye coordinates. Getting that wrong is invisible
	 * in a scene that sets its lights under an identity modelview and wrong in
	 * every other one.
	 *
	 * The 4th component is the light's TYPE and must be preserved, not
	 * normalised away: w == 0 is a directional light, whose position is a
	 * direction, and w != 0 is a positional (point) light, whose direction
	 * varies per vertex. */
	GLfloat     Position[4];

	GLfloat     SpotDirection[3];
	GLfloat     SpotExponent;
	GLfloat     SpotCutoff;

	/* GL defaults (1,0,0): constant 1, no linear or quadratic falloff, i.e. no
	 * distance attenuation at all unless the application asks for it. */
	GLfloat     ConstantAttenuation;
	GLfloat     LinearAttenuation;
	GLfloat     QuadraticAttenuation;
} MGLLight;

typedef struct MGLMaterial_t
{
	GLfloat     Ambient[4];
	GLfloat     Diffuse[4];
	GLfloat     Specular[4];
	GLfloat     Emission[4];
	GLfloat     Shininess;
} MGLMaterial;

struct GLcontext_t
{
	/*
	** The primitive with which glBegin was called,
	** or GL_BASE if outside glBegin/glEnd
	*/

	GLenum      CurrentPrimitive;

	/*
	** Current error
	*/
	GLenum      CurrentError;

	/*
	** The ModelView/Projection matrix stack.
	** Note that the topmost (= current) matrix is not the
	** top of the stack, but rather one of the ModelView[]/Projection[] below.
	** This makes copying the matrices unnecessary...
	*/
	int         ModelViewStackPointer;
	Matrix      ModelViewStack[MODELVIEW_STACK_SIZE];

	int         ProjectionStackPointer;
	Matrix      ProjectionStack[PROJECTION_STACK_SIZE];

	/*
	** The current ModelView/Projeciton matrix.
	** The matrix multiplication routine will switch between those
	** two to avoid copying stuff.
	*/
	GLuint      ModelViewNr;
	Matrix      ModelView[2];

	#define     CurrentMV (&(context->ModelView[context->ModelViewNr]))
	#define     SwitchMV  context->ModelViewNr = !(context->ModelViewNr)

	GLuint      ProjectionNr;
	Matrix      Projection[2];

	#define     CurrentP (&(context->Projection[context->ProjectionNr]))
	#define     SwitchP  context->ProjectionNr = !(context->ProjectionNr)

	// The current matrix mode
	GLuint      CurrentMatrixMode;


	/*
	** flexible buffer reserved for vertexarrays
	*/

	GLfloat   * WBuffer;

	/* glArrayElement's recorded indices. THIRTY-TWO BIT: at UWORD width an
	 * index of 65536 or more silently wraps to the wrong vertex. The pointer
	 * is the same size either way, so no GLcontext offset depends on the
	 * ELEMENT width, and minigl.h's inline glArrayElement writes into this
	 * array directly. A client compiled against a 16-bit-element header
	 * writes 16-bit values here and nothing refuses it: structSize covers the
	 * dispatch table, not this struct, and the abiVersion guard only protects a
	 * new client from an old library, never the reverse. Everything that uses
	 * glArrayElement has to be built against this header. */
	GLuint    * ElementIndex;

	/* glArrayElement(i) must behave like real OpenGL and use the CURRENT
	 * texcoord (as of THIS call) for any vertex whose
	 * GL_TEXTURE_COORD_ARRAY isn't bound -- e.g. a model renderer that
	 * calls glTexCoord2f() then glArrayElement() per vertex, inside one
	 * glBegin/glEnd block, because its texcoords are per-seam-corner and
	 * not indexable by vertex the way position/color are. Gathering is
	 * deferred to GLEnd/GLDrawElements, and by then the live
	 * CurrentTexS/CurrentTexT have been overwritten by every later
	 * glTexCoord2f call in the same primitive: reading them there would
	 * stamp every vertex of a strip/fan with the LAST vertex's texcoord.
	 * So GLArrayElement (vertexbuffer_min.c) snapshots CurrentTexS/T into
	 * these parallel arrays at the same slot as ElementIndex;
	 * GatherVertexFromIndex (vertexelements.c) reads the snapshot for that
	 * slot instead of the live scalar, but only when gathering the
	 * internal GLEnd-driven batch (indices == context->ElementIndex) -- a
	 * direct glDrawElements() call with the caller's own index array never
	 * populates these, so it uses the live CurrentTexS/T scalar. */
	GLfloat   * ElementTexS;
	GLfloat   * ElementTexT;

	/*
	** Vertex buffers
	** A call to glVertex*() will fill one entry of the vertex buffer
	** with the current data. glEnd() will go over this data and
	** draw the primitives based on this.
	*/

	MGLVertex * VertexBuffer;
	GLuint      VertexBufferPointer;        // Next free entry
	GLuint      VertexBufferSize;           // Size of the buffer

	/*
	** Surgeon: normal buffer, with the position in the buffer recorded
	** in glVertex*() functions.
	*/

	MGLNormal * NormalBuffer;
	GLuint	    NormalBufferPointer;

	/*
	** Current colors
	*/
	GLuint      ClearColor;
	double      ClearDepth;
	MGLColor    CurrentColor;

 //Surgeon: minimize color-update calls
	GLboolean   UpdateCurrentColor;

	GLfloat     CurrentTexS, CurrentTexT, CurrentTexQ;
	GLboolean   CurrentTexQValid;
	/*
	** The flag indicates wether the combined matrix is valid or not.
	** If it indicates GL_TRUE, the CombinedMatrix field contains the
	** product of the ModelView and Projection matrix.
	*/
	GLboolean   CombinedValid;
	Matrix      CombinedMatrix;

	/*
	** Scale factors for the transformation of normalized coordinates
	** to window coordinates. The *x and *y values are set by glViewPort.
	** *z is set by glDepthRange, which also sets near and far.
	** Single precision -- surgeon: double precision not needed.
	*/

	GLfloat    sx,ax;
	GLfloat    sy,ay;
	GLfloat    sz,az;
	GLdouble    near,far;

	GLuint	    ClipFlags;	//surgeon: viewport flags used with guardband clipping
	GLboolean    GuardBand;	//surgeon

	// CullFace mode
	GLenum      CurrentCullFace;
	GLenum      CurrentFrontFace;

	// Sign extracted from above
	//  0 means no culling
	//  1 means back + ccw or front + cw
	// -1 means back + cw  or front + ccw

	GLint	    CurrentCullSign;

	// Pixel states
	GLint       PackAlign;
	GLint       UnpackAlign;

	/*
	** GL Rendering States
	*/
	GLboolean   AlphaTest_State;
	GLboolean   Blend_State;
	GLboolean   Texture2D_State[MAX_TEXUNIT];
	GLboolean   TextureGenS_State;
	GLboolean   TextureGenT_State;
	GLboolean   Fog_State;
	GLboolean   Scissor_State;
	GLboolean   CullFace_State;
	GLboolean   DepthTest_State;
	GLboolean   PointSmooth_State;
	GLboolean   Dither_State;
	GLboolean   ZOffset_State;
	/* GL_POLYGON_OFFSET_FILL's own flag, separate from ZOffset_State
	 * (MGL_Z_OFFSET): the two entry points carry different quantities --
	 * see ZOffset below. */
	GLboolean   PolygonOffsetFill_State;

	/* Per-channel write enable, glColorMask. Real GL default is TRUE for
	 * all 4 (nothing masked). Read fresh every draw call by
	 * gl_EmitCullBlendState (draw.c), same "rebuild from live GL state"
	 * pattern as Blend_State/CullFace_State -- an application can change
	 * it between draws within a single frame, so it cannot be set once per
	 * render pass the way gl_EnsureDrawState's other state is. See
	 * gl_EmitCullBlendState for the v3d_OP_COLOR_WRITE_MASKS bit layout. */
	GLboolean   ColorMaskR;
	GLboolean   ColorMaskG;
	GLboolean   ColorMaskB;
	GLboolean   ColorMaskA;

	/*
	** 'Internal' states
	*/

	GLboolean   FogDirty;

	GLdouble    FogStart;
	GLdouble    FogEnd;

	/*
	** Drawing and clipping functions for the current primitive
	*/

	DrawFn      CurrentDraw;

	/*
	** V3D backend -- in place of the original's Warp3D-specific fields
	** (see this file's header comment for the field-by-field mapping).
	*/

	/* Named "backend", not "context" -- almost every MiniGL function takes
	 * a parameter literally named `context` (GLcontext context), and
	 * `context->context` would be a needless readability/typo trap. */
	V3DDevice               device;
	V3DContext               backend;

	/* This struct's own allocation record: MGLCreateContext and its
	 * FromWindow/FromBitMap variants allocate the context with
	 * v3d_mem_alloc (MEMF_CLEAR). Held directly on the struct so
	 * MGLDeleteContext can free it via the normal
	 * v3d_mem_free(device, &selfMem) path. */
	v3d_mem                 selfMem;

	struct Window *         v3dWindow;
	struct Screen *         v3dScreen;

	/* The window for IDCMP input, read by MGLMainLoop (others.c) --
	 * NOT the same signal as v3dWindow (which means "true windowed
	 * RENDERING mode, v3dBitMap is the real render target" throughout
	 * this codebase). In real windowed mode, inputWindow == v3dWindow
	 * (one window serves both purposes). In fullscreen mode, v3dWindow
	 * stays NULL (correctly, for every other v3dWindow-fork in this
	 * codebase) but inputWindow points at a borderless backdrop
	 * window opened purely so Intuition has somewhere to deliver
	 * keyboard/mouse messages -- a bare Screen has no message port of
	 * its own. Input handling must therefore use this field, never
	 * v3dWindow, which is NULL in fullscreen. */
	struct Window *         inputWindow;

	GLboolean		ArrayTexBound;

	GLint                   CurrentBinding;

	//Multitexture
	GLint                   VirtualBinding;
	GLuint			VirtualTexUnits; //Surgeon
	GLuint	    		ActiveTexture; //THF
	/* Vertex-array multitexture: the CLIENT active texture unit
	 * (glClientActiveTextureARB), which array glTexCoordPointer/
	 * glEnableClientState(GL_TEXTURE_COORD_ARRAY) target -- kept as its own
	 * field rather than reusing ActiveTexture above (the SERVER active
	 * unit, for binding/texenv) since real GL spec keeps these
	 * independent. */
	GLuint			ClientActiveTexture;

	V3DTexture **           textureObjects;   /* indexed by GL texture name (glGenTextures) */
	GLint                   textureObjectCount;
	struct ScreenBuffer *   Buffers[3];   /* double buffering (fullscreen, 2 or 3 buffers requested): [0] and [1]; NULL otherwise */
	struct BitMap *         v3dBitMap; // If in windowed mode
	struct RastPort *       v3dRastPort; // for windowed ClipBlit mode
	int                     BufNr;        /* double buffering: the back buffer being rendered into */
	int                     NumBuffers;   /* 2 = double buffering; 0 = single buffer (fullscreen or windowed) */

	APTR                    bitmapLock;   /* handle from LockBitMapTags */
	ULONG                   vmembase;     /* direct framebuffer address, from bitmapLock */
	ULONG                   bprow;        /* bytes per row, from bitmapLock */

	GLboolean               v3dLocked;

#ifdef AUTOMATIC_LOCKING_ENABLE
	GLenum                  LockMode;
	LockTimeHandle          LockTime;
#endif
	GLboolean               DoSync;       /* stored by MGLEnableSync, no effect: the buffer count decides vsync */

	GLenum                  TexEnv[MAX_TEXUNIT];
	GLenum                  CurTexEnv;
	/*
	 * FOUR GLenum-sized slots, in use below as RequestedBuffers,
	 * CurPolygonModeBack, CloseWorkbench and PerspectiveMapping_State.
	 *
	 * A field here is renamed in place rather than deleted: minigl.h's
	 * inlines bake GLcontext offsets into already-compiled client objects, so
	 * removing a field would move every field after it. A rename is free
	 * because minigl.h dereferences only CurrentColor, VertexBuffer(Pointer),
	 * UpdateCurrentColor, NormalBuffer(Pointer), ShadeModel, ClientState,
	 * CurrentTexQValid, ElementIndex, CurrentPointSize and ActiveTexture --
	 * none of these four. Two GLboolean-sized slots are still free further
	 * down (unused_5, unused_6); reuse those before appending.
	 */
	/* Buffer count the application ASKED mglChooseNumberOfBuffers for, not the
	 * count it got: every request is downgraded to 1 unless the library is built
	 * with MGLV3D_DOUBLE_BUFFER_ENABLED (context.c). glDrawBuffer/glReadBuffer
	 * validate the BACK names against this, so a program that asked for double
	 * buffering may name the second buffer and one that never asked still gets
	 * GL 1.1's GL_INVALID_OPERATION. Reused from the reserved slots below. */
	GLuint                  RequestedBuffers;
	/* glPolygonMode's BACK mode; CurPolygonMode is the front one. GL keeps a
	 * mode per face and glGetIntegerv(GL_POLYGON_MODE) returns both. Reused
	 * from the reserved slots above rather than appended, so the struct does
	 * not grow. */
	GLenum                  CurPolygonModeBack;
	/* mglProposeCloseDesktop's request, captured when the display opens so the
	 * close path reopens the Workbench only if THIS context closed it. The
	 * request itself stays a file static like the other mglChoose* ones, but a
	 * static in the resident library is per-LIBRARY, so without this capture
	 * one program's choice reached the next. Reused from the reserved slots. */
	GLboolean               CloseWorkbench;
	/* mglSetState(MGL_PERSPECTIVE_MAPPING)'s flag, stored so glIsEnabled can
	 * answer it. Nothing reads it to render: perspective correction is not
	 * optional on this hardware and the non-perspective CL packet is never
	 * emitted, so this records what the application asked for, the way the
	 * three state-only capabilities below do. Defaults GL_TRUE, which is what
	 * the hardware does. Reused from the reserved slots. */
	GLboolean               PerspectiveMapping_State;

	GLfloat                 FogRange;
	GLfloat                 FogMult;
	GLenum                  ShadeModel;
	GLboolean               DepthMask;

	GLboolean               NoMipMapping;
	GLboolean               NoFallbackAlpha;

	KeyHandlerFn            KeyHandler;
	MouseHandlerFn          MouseHandler;
	SpecialHandlerFn        SpecialHandler;
	IdleFn                  Idle;
	GLboolean               Running;

	GLenum              SrcAlpha;
	GLenum              DstAlpha;
	GLboolean               AlphaFellBack;

	GLfloat                 InvRot[9];

	GLboolean       InvRotValid;
	GLboolean       WOne_Hint;
	GLboolean       FixpointTrans_Hint; //Surgeon

	/* MGLSetZOffset and glPolygonOffset are different quantities.
	 * glPolygonOffset's `units` are MULTIPLES OF THE MINIMUM RESOLVABLE
	 * DEPTH DIFFERENCE (the GL definition, and what the DEPTH_OFFSET
	 * packet's field means). MGLSetZOffset's value is a DIRECT depth delta
	 * in [0,1] -- the Warp3D-era convention. Passing a value like 0.0009 as
	 * an MRD multiple makes it vanish.
	 *
	 * So they are tracked separately and draw.c converts the ZOffset one.
	 * ZOffset is the MGLSetZOffset value verbatim; do not "simplify" these
	 * back into one field. */
	GLfloat         ZOffset;              /* MGLSetZOffset: direct [0,1] delta */
	GLfloat         PolygonOffsetFactor;  /* glPolygonOffset: slope factor */
	GLfloat         PolygonOffsetUnits;   /* glPolygonOffset: MRD multiples */

	void           *PaletteData;
	GLenum          PaletteFormat;
	GLint           PaletteSize;

/* Begin Joe Sera Sept. 23 2000 */
	/*
	** GL Current Modes and States for glGetIntegerv
	*/
	GLint CurPolygonMode ;      /* GL_POLYGON_MODE       */
	GLint CurShadeModel ;       /* GL_SHADE_MODEL        */
	GLint CurBlendSrc ;         /* GL_BLEND_SRC          */
	GLint CurBlendDst ;         /* GL_BLEND_DST          */
	GLint CurUnpackRowLength ;  /* GL_UNPACK_ROW_LENGTH  */
	GLint CurUnpackSkipPixels ; /* GL_UNPACK_SKIP_PIXELS */
	GLint CurUnpackSkipRows ;   /* GL_UNPACK_SKIP_ROWS   */

/* End Joe Sera Sept. 23 2000 */

/* Begin Joe Sera Oct. 21, 2000  */
	/*
	 * RESERVED, two more 4-byte slots -- GLboolean is unsigned int here, so
	 * these hold anything the GLenum-sized four above could. Depth
	 * state lives in DepthMask and DepthTest_State, the fields draw.c and
	 * mgl_QueryState read. Renamed rather than deleted: removing a field moves
	 * every field after it, and minigl.h bakes offsets into compiled clients.
	 */
	GLboolean unused_5 ;
	GLboolean unused_6 ;


/* End Joe Sera Oct. 21 2000 */

	/*
	** Client state
	*/

	GLbitfield      ClientState;        /* Current client state mask */

	/* Whether THIS glBegin/glEnd sequence used glArrayElement: reset to
	 * GL_FALSE by GLBegin (vertexbuffer_min.c), set GL_TRUE by
	 * GLArrayElement (same file), and checked by GLEnd to decide whether
	 * to draw the sequence through GLDrawElements. GLEnd must not use the
	 * PERSISTENT ClientState&GLCS_VERTEX bit for this: a client may leave
	 * GL_VERTEX_ARRAY permanently enabled for ITS OWN array-based
	 * rendering (standard, portable OpenGL usage) while ALSO, separately,
	 * issuing plain glVertex-based immediate-mode sequences elsewhere.
	 * Real OpenGL semantics distinguish "is GL_VERTEX_ARRAY enabled" from
	 * "did THIS SPECIFIC sequence call glArrayElement" -- these are
	 * independent. GLDrawArrays/GLDrawElements's own use of
	 * ClientState/GLCS_VERTEX is unrelated. */
	GLboolean	UsedArrayElement;

	MGLAPointer	ArrayPointer;
	GLboolean	VertexArrayPipeline;

	GLfloat         MinTriArea;         /* Minimal area of triangles to be drawn -- stored by MGLMinTriArea, read by nothing */

	GLfloat         CurrentPointSize;   /* diameter */

	GLubyte*        GeneratedTextures;  /* Array to keep track of generated textures */

	/* The GL-visible viewport and scissor rectangles, kept solely so
	 * glGetIntegerv can answer GL_VIEWPORT and GL_SCISSOR_BOX with all four
	 * elements. The driver's own copies cannot be handed back: ax/ay/sx/sy
	 * are a centre and a half-extent in a top-origin frame, and
	 * backend.scissor_y is likewise flipped. Appended after the existing
	 * fields on purpose -- no existing field offset moves, so a client built
	 * against an older copy of this header still reads every field it knows
	 * at the same place. Order is GL's: x, y, width, height, with y measured
	 * from the bottom. */
	GLint           ViewportBox[4];
	GLint           ScissorBox[4];

	/* glLineWidth's value. The sibling CurrentPointSize sits further up
	 * next to MinTriArea; this one is appended here for the same reason as
	 * the two rectangles above -- adding it beside its sibling would move
	 * every field after it, and minigl.h's inline surface bakes field
	 * offsets into client objects. Width in pixels, GL default 1.0, read by
	 * gl_EnsureDrawState (draw.c) once per render pass, not per draw. */
	GLfloat         CurrentLineWidth;

	/* What glTexGeni asked for, per coordinate. Defaults to GL_SPHERE_MAP
	 * rather than GL's own GL_EYE_LINEAR -- see GLTexGeni (texture.c) for
	 * why that deviation is deliberate. */
	GLenum          TexGenModeS;
	GLenum          TexGenModeT;

	/* The plane equations the two linear texgen modes are defined by.
	 * GL defaults: S = (1,0,0,0), T = (0,1,0,0), for both the object and
	 * the eye set.
	 *
	 * The EYE planes are stored ALREADY TRANSFORMED by the inverse of the
	 * modelview that was current when glTexGenfv was called, which is what GL
	 * specifies -- the transform happens once at specification time, not per
	 * vertex, so a later modelview change does not move the plane. */
	GLfloat         ObjectPlaneS[4];
	GLfloat         ObjectPlaneT[4];
	GLfloat         EyePlaneS[4];
	GLfloat         EyePlaneT[4];

	/* THE current texture coordinate, per unit. GL says this is persistent
	 * state that each glVertex captures; GLVertex4f latches these exactly as
	 * it latches CurrentColor.
	 *
	 * These hold SLOT-READY values, which is why they are separate from
	 * CurrentTexS/CurrentTexT above: those keep the RAW s,t that the vertex
	 * array gather falls back on, while glTexCoord4f stores s/q and t/q here
	 * -- its Glide-shim callers supply s,t already divided by w. Merging the
	 * two would silently change one path or the other. */
	GLfloat         CurTexU0, CurTexV0, CurTexQ0;
	GLfloat         CurTexU1, CurTexV1;

	/* The state behind the index, edge-flag and read-buffer entry points.
	 * Appended for the same reason as everything above: no existing field
	 * offset moves.
	 *
	 * None of it reaches a pixel, and each for a reason GL itself gives. The
	 * current index and the index array have no effect in RGBA mode, the only
	 * mode this context has. The edge flag and its array only mark boundary
	 * edges for glPolygonMode GL_LINE/GL_POINT; GL_LINE draws now, but the flag
	 * cannot be honoured without a per-vertex field in MGLVertex, whose layout
	 * is part of the compiled-client ABI -- see GLEdgeFlag's own comment. The
	 * read buffer can only ever be the front, which is where
	 * GLReadPixels already reads. What an application CAN observe is the state
	 * itself, through the queries -- so it is kept, with GL's defaults. */
	GLfloat         CurrentIndex;          /* GL_CURRENT_INDEX, default 1 -- GL keeps it as a float */
	GLboolean       CurrentEdgeFlag;       /* GL_EDGE_FLAG, default GL_TRUE */
	GLenum          ReadBufferMode;        /* GL_READ_BUFFER, default GL_FRONT */
	const GLvoid   *IndexArrayPointer;     /* glIndexPointer, default NULL */
	GLenum          IndexArrayType;        /* GL_INDEX_ARRAY_TYPE, default GL_FLOAT */
	GLsizei         IndexArrayStride;      /* GL_INDEX_ARRAY_STRIDE, as given, default 0 */
	const GLvoid   *EdgeFlagArrayPointer;  /* glEdgeFlagPointer, default NULL */
	GLsizei         EdgeFlagArrayStride;   /* GL_EDGE_FLAG_ARRAY_STRIDE, as given, default 0 */

	/* Three capabilities glEnable keeps as STATE ONLY. MGLSetState records
	 * GL_INVALID_ENUM for a capability it does not know, so these three --
	 * legal names with nothing for the driver to do -- are named explicitly
	 * rather than turned into errors:
	 *  - GL_POLYGON_OFFSET_LINE / GL_POLYGON_OFFSET_POINT offset polygons drawn
	 *    in line or point polygon mode. GL_LINE draws as real lines here
	 *    by the rasterizer's own fill bits (CFG_BITS, set in draw.c) rather
	 *    than as offsettable polygons, so neither offset has anything to apply
	 *    to. draw.c's depth offset reads PolygonOffsetFill_State, never these
	 *    two, and must go on doing so.
	 *  - GL_SHARED_TEXTURE_PALETTE_EXT picks the shared palette over per-texture
	 *    ones. This driver has only the shared palette (GLColorTable), applied
	 *    at upload, so it is in effect either way.
	 * Stored so glIsEnabled and the getters answer truthfully. GL's default for
	 * all three is disabled. Appended, like everything above. */
	GLboolean       PolygonOffsetLine_State;
	GLboolean       PolygonOffsetPoint_State;
	GLboolean       SharedTexturePalette_State;

	/* The pack half of the pixel store state for glReadPixels (PackAlign is
	 * further up): row length, the two skips and byte swapping shape the
	 * destination of every read (others.c, GLReadPixels). GL_PACK_LSB_FIRST
	 * is stored and answered but only matters for GL_BITMAP, which an RGBA
	 * context without a stencil buffer never reads. GL defaults: 0 / FALSE.
	 * Appended, like everything above. */
	GLint           PackRowLength;
	GLint           PackSkipPixels;
	GLint           PackSkipRows;
	GLboolean       PackSwapBytes;
	GLboolean       PackLsbFirst;

	/* TRUE for a render pass that continues a frame after a pass split
	 * (gl_FrameBegin's force_new_pass branch: a mid-frame colour/depth clear
	 * or a mid-frame glReadPixels). Such a pass loads the depth its
	 * intermediate pass stored, and that load must not count as the
	 * application relying on depth persisting across FRAMES
	 * (depth_persist_seen, v3d_context.h) -- otherwise one readback would
	 * turn the depth-store skip off for the rest of the session. */
	GLboolean       PassContinuesSplit;

	/* THE TEXTURE MATRIX. GL_TEXTURE is a standard GL 1.1 matrix mode that
	 * base MiniGL does not have. Every matrix-mode selector must test for
	 * GL_TEXTURE explicitly: CMATRIX/OMATRIX/SMATRIX below and the selectors
	 * in GLLoadIdentity, GLPushMatrix and GLPopMatrix (matrix.c). Their
	 * fall-through arms differ -- PROJECTION in CMATRIX and GLLoadIdentity,
	 * MODELVIEW in GLPushMatrix/GLPopMatrix -- so a selector without its own
	 * GL_TEXTURE arm sends texture-matrix calls to one of the other two
	 * matrices, and a push/load identity/scale/pop block hits both of them.
	 *
	 * APPENDED after the existing fields: this context is shared with
	 * minigl.library's clients, so no existing field may move. Same rule as
	 * V3DTexture.no_alpha.
	 *
	 * The double-buffered Texture[2] pair mirrors ModelView/Projection so
	 * CMATRIX/OMATRIX/SMATRIX handle it the same way, with one extra arm
	 * each (below). */
	GLuint      TextureNr;
	Matrix      Texture[2];
	int         TextureStackPointer;
	Matrix      TextureStack[TEXTURE_STACK_SIZE];

	/* MGLCreateContextFromWindow: TRUE when v3dWindow belongs to the
	 * APPLICATION, not to us -- it handed us an already-open window to
	 * render into instead of letting the driver open its own.
	 *
	 * THE POLARITY IS DELIBERATE AND LOAD-BEARING. This says "external", never
	 * "owns". A context is allocated with MEMF_CLEAR, so every field starts at
	 * zero; with this sense, zero means "the driver owns the window", which is
	 * exactly what every other path does. Invert it to an OwnsWindow
	 * flag and every fullscreen and ordinary windowed context silently reads as
	 * unowned, so vid_CloseWindow stops calling CloseWindow and leaks the window
	 * on every teardown.
	 *
	 * vid_CloseWindow consults it for two calls only: CloseWindow, and
	 * UnlockPubScreen -- an adopted window's screen was never locked by us
	 * either, and unlocking a pubscreen we did not lock would corrupt its lock
	 * count. Everything else in teardown is ours and is freed regardless.
	 *
	 * APPENDED after the existing fields: this context is shared with
	 * minigl.library's clients, so no existing field may move. */
	GLboolean   ExternalWindow;

	/* MGLCreateContextFromBitMap: TRUE when v3dBitMap belongs to the
	 * APPLICATION -- it handed us a bitmap to render into and does its own
	 * presentation. Distinct from ExternalWindow, which means the opposite
	 * split: their window, OUR offscreen bitmap.
	 *
	 * Same polarity discipline and the same reason: MEMF_CLEAR makes zero mean
	 * "the driver owns the bitmap", which is what every other path does, so
	 * vid_CloseWindow calls FreeBitMap for them.
	 *
	 * It also suppresses the ClipBlit in MGLSwitchDisplay. There is no window
	 * to blit into -- v3dWindow is NULL for these contexts -- and more
	 * importantly the host is presenting this bitmap itself, so a blit from us
	 * would be wrong even if there were somewhere to send it. This is what lets
	 * a program composite our output alongside another renderer's instead of
	 * the two fighting over one window. */
	GLboolean   ExternalBitMap;

	/* LIGHTING STATE. glEnable(GL_LIGHTING) and the GL_LIGHTi flags, the eight
	 * lights, the front material, the light model, and the folded products the
	 * lit vertex shader's uniform tail is written from.
	 *
	 * APPENDED after the existing fields, per the rule above: this context is
	 * shared with minigl.library's clients, so no existing field may move.
	 *
	 * Lit draws are selected by Lighting_State AND a non-zero LightMask -- GL
	 * lights nothing when GL_LIGHTING is on but every light is off, and that is
	 * not the same as lighting being disabled, because the material's emission
	 * and the light-model ambient still apply. Both conditions are read in
	 * draw.c; neither is sufficient alone. */
	GLboolean   Lighting_State;
	MGLLight    Light[MGL_MAX_LIGHTS];
	MGLMaterial Material;

	/* GL_LIGHT_MODEL_AMBIENT. The other two light-model parameters are stored
	 * and never acted on -- see the note in gl.h on why two-sided lighting is
	 * out of reach at this vertex stage. */
	GLfloat     LightModelAmbient[4];
	GLboolean   LightModelTwoSide;
	GLboolean   LightModelLocalViewer;

	/* Which lights are enabled, bit i for GL_LIGHTi. A MASK and not a count,
	 * because the shader's uniform tail is COMPACTED: whichever lights are on
	 * are written into tail slots 0..n-1 in ascending light order, so two draws
	 * with the same NUMBER of lights but different lights enabled need different
	 * uniform blocks. The uniform memo key therefore carries the mask, not the
	 * count -- the count is a function of the mask.
	 *
	 * This is load-bearing rather than tidy: the demo lights exclusively through
	 * GL_LIGHT1 and never touches GL_LIGHT0, and GL gives light 0 a default
	 * diffuse and specular of (1,1,1,1) while lights 1-7 get (0,0,0,1). An
	 * implementation that assumed light 0 was the active one would render the
	 * whole demo black. */
	GLuint      LightMask;

	/* Bumped by every glLight*, glMaterial*, glLightModel* and by MGLSetState on
	 * GL_LIGHTING or a GL_LIGHTi. The uniform memo in draw.c compares it, the
	 * same way g_mglv3d_combined_serial covers the modelview and InvRot: without
	 * it a second lit draw in one frame reuses the first draw's uniform block,
	 * because every other term of that key still matches. */
	GLuint      LightSerial;

	/* THE FOLD, recomputed by light_Fold whenever any of the above changes.
	 * These are what the uniform tail is written from, so the per-draw path does
	 * no light x material arithmetic at all.
	 *
	 * LitBase is the whole light-independent term: material emission, plus the
	 * light-model ambient times the material ambient, plus each ENABLED light's
	 * ambient times the material ambient. Its alpha is the material diffuse
	 * alpha, which is what GL gives a lit vertex.
	 *
	 * LitDiffuse[i] is light i's diffuse times the material diffuse, per
	 * channel. Only the enabled lights' entries are meaningful, and they are
	 * indexed by LIGHT NUMBER here -- the compaction into tail slots happens at
	 * uniform-write time, not here, so that this array can be read by light
	 * number when answering glGetLightfv.
	 *
	 * LitSpecular[i] is light i's specular times the material specular, same
	 * indexing. */
	GLfloat     LitBase[4];
	GLfloat     LitDiffuse[MGL_MAX_LIGHTS][3];
	GLfloat     LitSpecular[MGL_MAX_LIGHTS][3];

	/* GL_DRAW_BUFFER, default GL_FRONT. Read only by the glGet query -- this
	 * context is single-buffered so drawing goes to the one buffer whatever it
	 * says. APPENDED, per the rule above: no existing field may move. */
	GLenum      DrawBufferMode;

	/*
	 * GL_NORMAL_ARRAY (glNormalPointer). APPENDED here rather than added to
	 * MGLAPointer, which is where the other array pointers live: MGLAPointer
	 * is a BY-VALUE member of this struct, so growing it would move every
	 * field after ArrayPointer and break the GLcontext offsets minigl.h's
	 * inlines bake into already-compiled clients.
	 *
	 * NormalArrayStride is kept as the application gave it, for the
	 * GL_NORMAL_ARRAY_STRIDE query; NormalArrayStep is the resolved byte step
	 * the gather actually walks, so the draw path never re-derives it.
	 */
	const GLvoid *NormalArrayPointer;
	GLenum        NormalArrayType;
	GLsizei       NormalArrayStride;
	GLint         NormalArrayStep;

	/*
	 * GL_COLOR_MATERIAL: the named material component tracks the per-vertex
	 * colour. APPENDED, per the rule above.
	 *
	 * ColorMaterialMode is stored as GL's own token so glGet can answer
	 * GL_COLOR_MATERIAL_PARAMETER with what was set; light_Fold turns it into
	 * the four selectors below. The face is stored for the same reason and is
	 * not otherwise read -- there is one material here, not GL's pair.
	 *
	 * THE SECOND FOLD. Every tracked term is LINEAR in the vertex colour, so
	 * each one folds to K0 + K1 * C with both halves known once the state is
	 * set. K0 is the existing LitBase/LitDiffuse/LitSpecular -- with the
	 * tracked material component taken as zero there -- and the three arrays
	 * here are K1. With colour material off every K1 is zero, which is why the
	 * uniform tail could be EXTENDED rather than rearranged: words 0..34 keep
	 * their meaning and the five shaders that read them are untouched.
	 *
	 * LitBaseC's alpha is the alpha half of the same split: GL gives a lit
	 * vertex the material DIFFUSE alpha, so tracking diffuse makes the vertex
	 * colour's own alpha the answer.
	 */
	GLboolean   ColorMaterial_State;
	GLenum      ColorMaterialFace;
	GLenum      ColorMaterialMode;
	GLfloat     LitBaseC[4];
	GLfloat     LitDiffuseC[MGL_MAX_LIGHTS][3];
	GLfloat     LitSpecularC[MGL_MAX_LIGHTS][3];

	/*
	 * GL_TEXTURE_ENV_COLOR, per unit as the mode is (GL 1.1 3.8.9). Read only
	 * by GL_BLEND, the one mode whose equation has a constant in it. GL's
	 * default is (0,0,0,0).
	 *
	 * APPENDED here rather than beside TexEnv[], where it reads better and
	 * would shift every field after it -- minigl.h's inlines bake GLcontext
	 * offsets into already-compiled clients, so nothing may move.
	 */
	GLfloat     TexEnvColor[MAX_TEXUNIT][4];

};

#define     CurrentT  (&(context->Texture[context->TextureNr]))
#define     SwitchT   context->TextureNr = !(context->TextureNr)


/*
** The CMATRIX macro give the address of the currently
** active matrix, depending on the matrix mode.
** The OMATRIX macro gives the address of the secondary matrix
** The SMATRIX macro switches the active and backup matrix
*/
/* THREE-way: GL_MODELVIEW, GL_TEXTURE (its own storage), and PROJECTION for
 * anything else -- an unrecognised mode falls back to PROJECTION rather than
 * reading past the struct. Without the GL_TEXTURE arm, glMatrixMode(GL_TEXTURE)
 * would aim every glScalef/glTranslatef/glMultMatrix at the projection matrix. */
#define CMATRIX(context) context->CurrentMatrixMode == GL_MODELVIEW ?\
	(&(context->ModelView[context->ModelViewNr])):\
	(context->CurrentMatrixMode == GL_TEXTURE ?\
	(&(context->Texture[context->TextureNr])):\
	(&(context->Projection[context->ProjectionNr])))

#define OMATRIX(context) context->CurrentMatrixMode == GL_MODELVIEW ?\
	(&(context->ModelView[!(context->ModelViewNr)])):\
	(context->CurrentMatrixMode == GL_TEXTURE ?\
	(&(context->Texture[!(context->TextureNr)])):\
	(&(context->Projection[!(context->ProjectionNr)])))

#define SMATRIX(context) if (context->CurrentMatrixMode == GL_MODELVIEW)\
	context->ModelViewNr = !(context->ModelViewNr);\
   else if (context->CurrentMatrixMode == GL_TEXTURE)\
	context->TextureNr = !(context->TextureNr);\
   else context->ProjectionNr = !(context->ProjectionNr)

#endif

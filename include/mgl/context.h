/*
 * MiniGLV3D fork of MiniGL/include/mgl/context.h -- Phase G.
 *
 * GLcontext_t redefined around V3DDevice/V3DContext instead of
 * W3D_Context. Pure GL-API bookkeeping (matrix stacks, vertex/normal
 * buffers, boolean GL_* state, client-array state, etc.) is UNCHANGED --
 * this is not a redesign of MiniGL's state machine, only of the fields
 * that named a Warp3D type directly and therefore could not compile
 * without Warp3D.h.
 *
 * Field-by-field disposition of every W3D_*-typed field the original had:
 *  - W3D_Context* w3dContext -> replaced by embedding V3DDevice device
 *    and V3DContext context directly (matches every demo's own pattern --
 *    no pointer indirection, no separate alloc/free step for the backend
 *    context).
 *  - W3D_Fog w3dFog, W3D_Scissor scissor -> DROPPED. V3DContext already
 *    owns this state field-for-field (backend.fog_start/fog_end/
 *    fog_density/fog_r/g/b/a matches W3D_Fog exactly; backend.scissor_x/
 *    y/w/h matches W3D_Scissor exactly) -- Phase H's fog.c/others.c read
 *    and write backend.fog_* / backend.scissor_* directly instead of a separate
 *    GLcontext-side copy. Also dropped the now-redundant w3dFogMode
 *    (backend.fog_mode is the one source of truth).
 *  - W3D_Color CurrentColor -> replaced by MGLColor CurrentColor (a
 *    plain r/g/b/a float struct, defined in vertexbuffer.h -- reverts
 *    the original's own "//was MGLColor" comment, which noted this field
 *    used to be exactly this shape before being changed to W3D_Color for
 *    a since-irrelevant Warp3D optimization).
 *  - W3D_Double ClearDepth -> double (W3D_Double's own underlying type,
 *    per Warp3D.h: typedef double W3D_Double -- no behavior change, just
 *    naming the real type directly instead of through a Warp3D alias).
 *  - W3D_Texture** w3dTexBuffer, GLubyte** w3dTexMemory, GLint
 *    TexBufferSize -> replaced by V3DTexture** textureObjects (array of
 *    backend texture objects indexed by GL texture name, GLuint
 *    glGenTextures's job) + textureObjectCount. w3dTexMemory (a parallel
 *    raw-CPU-data mirror per Warp3D texture) has no analog -- V3DTexture
 *    already owns its uploaded GPU memory directly (texture_mem), no
 *    separate CPU-side shadow copy needed. Per-unit CURRENTLY BOUND
 *    texture (as opposed to this all-textures-by-name array) is already
 *    V3DContext's job too (backend.bound_texture[V3D_MAX_TEXUNIT]) --
 *    Phase H's texture.c binds by writing backend.bound_texture[unit],
 *    not a new GLcontext-side field.
 *  - ULONG w3dChipID, w3dFormat, w3dAlphaFormat; GLint w3dBytesPerTexel
 *    -> DROPPED entirely. Warp3D-hardware-capability-negotiation fields
 *    (which of several possible pixel formats does THIS card support) --
 *    V3D is one fixed, known target (RGBA8 always), nothing to negotiate.
 *  - struct BitMap* w3dBitMap, struct RastPort* w3dRastPort -> kept
 *    (CyberGraphX/intuition types, not Warp3D-specific), plus NEW fields
 *    (bitmapLock, vmembase, bprow) matching what every demo's own
 *    Milestone1Screen-adjacent local state already does for the direct
 *    framebuffer address/stride -- promoted from demo-local to
 *    GLcontext-owned since context.c (Phase H) needs it, not just demos.
 *  - struct Window* w3dWindow, struct Screen* w3dScreen, struct
 *    ScreenBuffer* Buffers[3] -> kept unchanged (not Warp3D types).
 *
 * Everything else below this comment block is identical to the original
 * MiniGL/include/mgl/context.h.
 */

#ifndef __CONTEXT_H
#define __CONTEXT_H

#include "mgl/matrix.h"
#include "mgl/config.h"
#include "mgl/vertexbuffer.h"

#include "../../../backend/include/v3d_device.h"
#include "../../../backend/include/v3d_context.h"
#include "../../../backend/include/v3d_texture.h"

#pragma amiga-align

#include <intuition/intuition.h>

typedef struct LockTimeHandle_s
{
	ULONG s_hi, s_lo;
	ULONG e_freq;
} LockTimeHandle;

#pragma default-align

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
	/* Real vertex-array multitexture (2026-08-25): unit 1's own
	 * GL_TEXTURE_COORD_ARRAY enable, set/cleared by
	 * GLEnableClientState/GLDisableClientState while
	 * ClientActiveTexture==1 -- see GatherVertexFromArray/
	 * GatherVertexFromIndex for the consuming side. */
	GLCS_TEXTURE1 = 0x08,
	/* GL_INDEX_ARRAY and GL_EDGE_FLAG_ARRAY (2026-09-12, GL 1.1 audit item
	 * 10): tracked so glEnableClientState accepts them, never drawn from --
	 * see GLIndexPointer (vertexarray.c). */
	GLCS_INDEX    = 0x10,
	GLCS_EDGEFLAG = 0x20,
	GLCS_MASK    = 0x3F,
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
 * Phase J redesign: the original's w_buffer/w_off fields existed purely
 * to feed Warp3D's own hardware array-pointer register a synthetic W
 * component via raw pointer arithmetic (see the real MiniGL source's own
 * GLTexCoordPointer/GLVertexPointer) -- a hardware-interface trick with
 * no V3D equivalent, since this port always gathers app-array data into
 * context->VertexBuffer[] itself (Stage 3/4) rather than handing a raw
 * pointer+stride to a hardware register the way Warp3D did. Dropped;
 * texcoordsize/vertexsize added instead (this port's own gather step
 * needs to know how many components the app actually supplied, not a
 * byte offset into someone else's buffer). colormode/vertexmode are
 * repurposed too -- see MGLAColorMode below. GL_INT vertex arrays keep the
 * original's AP_FIXPOINT flag in `state` (set/cleared by GLVertexPointer,
 * cleared by GLInterleavedArrays); the gather step converts the ints with
 * (float), which is the original's value semantics -- see vertexarray.c's
 * header comment.
 */
typedef enum
{
	MGLA_COLOR_UBYTE_RGB  = 0,
	MGLA_COLOR_UBYTE_RGBA = 1,
	MGLA_COLOR_FLOAT_RGB  = 2,
	MGLA_COLOR_FLOAT_RGBA = 3,

	/* Phase L3 (glquake port): glquake calls glColorPointer(4,
	 * MGL_UBYTE_ARGB, ...) whenever gl_smoothmodels is true (defaults to
	 * on) -- a real, distinct memory layout, not just a differently-named
	 * alias for RGBA. Byte order confirmed against the original mglq3
	 * fork's own Convert_UB_ARGB: byte0=A, byte1=R, byte2=G, byte3=B, a
	 * genuine 4-way permutation. See vertexarray.c's own GLColorPointer/
	 * GatherVertexFromArray comments for the gather-side implementation. */
	MGLA_COLOR_UBYTE_ARGB = 4,

	/* MGL_UBYTE_BGRA (parity audit, closed 2026-09-11): the original's
	 * Convert_UB_BGR/Convert_UB_BGRA read byte0=B, byte1=G, byte2=R and,
	 * for size 4, byte3=A. Size 3 -> BGR, size 4 -> BGRA. */
	MGLA_COLOR_UBYTE_BGR  = 5,
	MGLA_COLOR_UBYTE_BGRA = 6
} MGLAColorMode;

typedef struct MGLAPointer_s
{
	GLubyte	*texcoords;	//application array
	GLint	texcoordstride;
	GLint	texcoordsize;	//1-4 as supplied by glTexCoordPointer; only the first 2 (u,v) are ever consumed (this port's shaders have no q/w texcoord input)

	/* Real vertex-array multitexture (2026-08-25, ioq3 r_vertexLight=0
	 * stall investigation): unit 1's own texcoord array, set by
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

	//GL_EXT_compiled_vertex_arrays (Stage 4+ follow-up, fields present but unused until then):
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

	// The current matrix mode (GL_MODELVIEW or GL_PROJECTION)
	GLuint      CurrentMatrixMode;


	/*
	** flexible buffer reserved for vertexarrays
	*/

	GLfloat   * WBuffer;

	UWORD     * ElementIndex; //for glArrayElement and index conversion in glDrawElements

	/* Quake2 investigation (2026-08-16), REAL BUG FIX: glArrayElement(i)
	 * must behave like real OpenGL and use the CURRENT texcoord (as of
	 * THIS call) for any vertex whose GL_TEXTURE_COORD_ARRAY isn't
	 * bound -- e.g. gl_mesh.c's alias-model path calls qglTexCoord2f()
	 * then qglArrayElement() per vertex, inside one glBegin/glEnd block,
	 * relying on exactly this snapshot-per-call semantic (it can't use a
	 * real texcoord array: MD2 texcoords are per-seam-corner, not
	 * indexable by vertex the way position/color are). GLArrayElement
	 * only records the vertex index though -- gathering is deferred to
	 * GLEnd/GLDrawElements, which used to read CurrentTexS/CurrentTexT
	 * at THAT later time, by when it had already been overwritten by
	 * every subsequent qglTexCoord2f call in the same primitive, so
	 * every vertex in a strip/fan ended up stamped with the LAST
	 * vertex's texcoord (hardware-confirmed symptom: textures went flat
	 * -- one uniform texel per primitive -- while lighting stayed
	 * correct, since color already came from a real per-vertex array).
	 * Fix: GLArrayElement (vertexbuffer_min.c) snapshots CurrentTexS/T
	 * into these parallel arrays at the same slot as ElementIndex;
	 * GatherVertexFromIndex (vertexelements.c) reads the snapshot for
	 * that slot instead of the live scalar, but only when gathering the
	 * internal GLEnd-driven batch (indices == context->ElementIndex) --
	 * a genuine direct glDrawElements() call with the caller's own index
	 * array (e.g. demos/varray.c) never populated these, so it keeps
	 * using the live CurrentTexS/T scalar exactly as before. */
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
	/* SPLIT (2026-08-30) from ZOffset_State -- see context.c's
	 * MGLSetState comment for why GL_POLYGON_OFFSET_FILL and MGL_Z_OFFSET
	 * no longer share one flag. */
	GLboolean   PolygonOffsetFill_State;

	/* Phase L4 (glquake port): per-channel write enable, glColorMask.
	 * Real GL default is TRUE for all 4 (nothing masked). Read fresh every
	 * draw call by gl_EmitCullBlendState (draw.c), same "rebuild from live
	 * GL state" pattern as Blend_State/CullFace_State -- glquake toggles
	 * this twice within a single frame (r_stereo anaglyph mode), so it
	 * cannot be a once-per-frame value the way gl_EnsureDrawState's other
	 * state is. See gl_EmitCullBlendState's own comment for the confirmed
	 * V3D_OP_COLOR_WRITE_MASKS bit-layout derivation (cross-checked
	 * against MESA's v3dx_emit.c/nir_to_vir.c). */
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
	** V3D backend -- replaces the Warp3D-specific fields this struct used
	** to carry directly (see this file's header comment for the full
	** field-by-field mapping).
	*/

	/* Named "backend", not "context" -- almost every MiniGL function takes
	 * a parameter literally named `context` (GLcontext context), and
	 * `context->context` would be a needless readability/typo trap. */
	V3DDevice               device;
	V3DContext               backend;

	/* PiStorm3D allocator-consistency cleanup (2026-08-29): this struct's
	 * own allocation record (MGLCreateContext uses v3d_mem_alloc/
	 * MEMF_CLEAR instead of malloc+memset now, matching every other
	 * allocation in this driver). Held directly on the struct so
	 * MGLDeleteContext can free it correctly later via the normal
	 * v3d_mem_free(device, &selfMem) path -- same convention as every
	 * other v3d_mem this driver frees. */
	v3d_mem                 selfMem;

	struct Window *         v3dWindow;
	struct Screen *         v3dScreen;

	/* Dedicated to MGLMainLoop's (others.c) IDCMP input needs only --
	 * NOT the same signal as v3dWindow (which means "true windowed
	 * RENDERING mode, v3dBitMap is the real render target" throughout
	 * this codebase). In real windowed mode, inputWindow == v3dWindow
	 * (one window serves both purposes). In fullscreen mode, v3dWindow
	 * stays NULL (correctly, for every other v3dWindow-fork in this
	 * codebase) but inputWindow points at a small borderless backdrop
	 * window opened purely so Intuition has somewhere to deliver
	 * keyboard/mouse messages -- a bare Screen has no message port of
	 * its own. Found via a real crash: MGLMainLoop used to call
	 * mglGetWindowHandle() (== v3dWindow) unconditionally, NULL in every
	 * normal fullscreen build, causing GetMsg(NULL->UserPort) -- a
	 * guaranteed crash for any interactive fullscreen demo. */
	struct Window *         inputWindow;

	GLboolean		ArrayTexBound;

	GLint                   CurrentBinding;

	//Multitexture
	GLint                   VirtualBinding;
	GLuint			VirtualTexUnits; //Surgeon
	GLuint	    		ActiveTexture; //THF
	/* Real vertex-array multitexture (2026-08-25): the CLIENT active
	 * texture unit (glClientActiveTextureARB), which array glTexCoordPointer/
	 * glEnableClientState(GL_TEXTURE_COORD_ARRAY) target -- kept as its own
	 * field rather than reusing ActiveTexture above (the SERVER active
	 * unit, for binding/texenv) since real GL spec keeps these
	 * independent, even though every current caller (GL_SelectTexture,
	 * ioq3's tr_backend.c) always sets both together with the same unit. */
	GLuint			ClientActiveTexture;

	V3DTexture **           textureObjects;   /* indexed by GL texture name (glGenTextures) */
	GLint                   textureObjectCount;
	struct ScreenBuffer *   Buffers[3];   /* double buffering (fullscreen, 2 or 3 buffers requested): [0] and [1]; NULL otherwise */
	struct BitMap *         v3dBitMap; // If in windowed mode
	struct RastPort *       v3dRastPort; // for windowed ClipBlit mode
	int                     BufNr;        /* double buffering: the back buffer being rendered into */
	int                     NumBuffers;   /* 2 = double buffering; 0 = single buffer (fullscreen or windowed) */

	APTR                    bitmapLock;   /* handle from LockBitMapTags, matches every demo's own pattern */
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
	GLenum                  MinFilter;
	GLenum                  MagFilter;
	GLenum                  WrapS;
	GLenum                  WrapT;

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

	/* UN-SHARED 2026-09-08. These used to be one pair, on the assumption that
	 * "MGLSetZOffset(offset) is equivalent to GLPolygonOffset(factor=0,
	 * units=offset)". HARDWARE REFUTED THAT: a 6-band sweep at 1024x768
	 * (demos/depth_offset_verify) showed glPolygonOffset units of -2, -10,
	 * -1000 all break a depth tie correctly, while MGLSetZOffset's real
	 * in-game value of -0.0009 -- fed into the same `units` field -- did
	 * nothing at all.
	 *
	 * They are different quantities. glPolygonOffset's `units` are MULTIPLES
	 * OF THE MINIMUM RESOLVABLE DEPTH DIFFERENCE (the GL definition, and what
	 * the DEPTH_OFFSET packet's field means). MGLSetZOffset's value is a
	 * DIRECT depth delta in [0,1] -- the Warp3D-era convention glquake was
	 * written against. Passing 0.0009 as an MRD multiple makes it vanish.
	 *
	 * So they are tracked separately now and draw.c converts the ZOffset one.
	 * ZOffset is still the MGLSetZOffset value verbatim; do not "simplify"
	 * these back into one field. */
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
	** GL Current Modes and States for glGetBooleanv
	*/

	GLboolean CurWriteMask ;    /* GL_DEPTH_WRITE_MASK   */
	GLboolean CurDepthTest ;    /* GL_DEPTH_TEST         */


/* End Joe Sera Oct. 21 2000 */

	/*
	** Client state
	*/

	GLbitfield      ClientState;        /* Current client state mask */

	/* FIX (2026-08-18, ioq3 black-screen root cause): GLEnd previously
	 * checked the PERSISTENT ClientState&GLCS_VERTEX bit to decide
	 * whether a glBegin/End sequence used glArrayElement -- wrong for
	 * any client that leaves GL_VERTEX_ARRAY permanently enabled for
	 * ITS OWN array-based rendering (standard, portable OpenGL usage)
	 * while ALSO, separately, issuing plain glVertex-based immediate-mode
	 * sequences elsewhere (ioq3's cinematic/debug-image draws, real
	 * ioquake3-side code, not this project's own). Real OpenGL semantics
	 * distinguish "is GL_VERTEX_ARRAY enabled" from "did THIS SPECIFIC
	 * sequence call glArrayElement" -- these are independent. Reset to
	 * GL_FALSE by GLBegin (vertexbuffer_min.c), set GL_TRUE by
	 * GLArrayElement (same file) -- GLEnd now checks this instead of
	 * ClientState, matching real GL semantics for every consumer
	 * (glquake/Quake2/ioq3 alike). ClientState/GLCS_VERTEX itself is
	 * untouched -- GLDrawArrays/GLDrawElements's own, legitimate use of
	 * it (vertexarray.c) is unrelated to this fix. */
	GLboolean	UsedArrayElement;

	MGLAPointer	ArrayPointer;
	GLboolean	VertexArrayPipeline;

	GLfloat         MinTriArea;         /* Minimal area of triangles to be drawn (smaller ones will be rejected)*/

	GLfloat         CurrentPointSize;   /* diameter */

	GLubyte*        GeneratedTextures;  /* Array to keep track of generated textures */

	/* The GL-visible viewport and scissor rectangles, kept solely so
	 * glGetIntegerv can answer GL_VIEWPORT and GL_SCISSOR_BOX with all four
	 * elements (2026-09-12, GL 1.1 audit). The driver's own copies cannot be
	 * handed back: ax/ay/sx/sy are a centre and a half-extent in a
	 * top-origin frame, and backend.scissor_y is likewise flipped. Appended
	 * at the END of this struct on purpose -- no existing field offset moves,
	 * so a client built against an older copy of this header still reads
	 * every field it knows at the same place. Order is GL's: x, y, width,
	 * height, with y measured from the bottom. */
	GLint           ViewportBox[4];
	GLint           ScissorBox[4];

	/* glLineWidth's value (2026-09-12, GL 1.1 audit fix 4). The sibling
	 * CurrentPointSize sits further up next to MinTriArea; this one is
	 * appended here for the same reason as the two rectangles above --
	 * adding it beside its sibling would move every field after it, and
	 * minigl.h's inline surface bakes field offsets into client objects.
	 * Width in pixels, GL default 1.0, read by gl_EnsureDrawState (draw.c)
	 * once per FRAME, not per draw. */
	GLfloat         CurrentLineWidth;

	/* What glTexGeni asked for, per coordinate (2026-09-12, GL 1.1 audit
	 * fix 7). GLTexGeni used to have an EMPTY body, so the mode argument was
	 * discarded and v_GenTexCoords produced a sphere map whatever was asked
	 * for. Defaulted to GL_SPHERE_MAP rather than GL's own GL_EYE_LINEAR --
	 * see GLTexGeni (texture.c) for why that deviation is deliberate. */
	GLenum          TexGenModeS;
	GLenum          TexGenModeT;

	/* The plane equations the two linear texgen modes are defined by
	 * (2026-09-12, audit fix 7). GL defaults: S = (1,0,0,0), T = (0,1,0,0),
	 * for both the object and the eye set.
	 *
	 * The EYE planes are stored ALREADY TRANSFORMED by the inverse of the
	 * modelview that was current when glTexGenfv was called, which is what GL
	 * specifies -- the transform happens once at specification time, not per
	 * vertex, so a later modelview change does not move the plane. */
	GLfloat         ObjectPlaneS[4];
	GLfloat         ObjectPlaneT[4];
	GLfloat         EyePlaneS[4];
	GLfloat         EyePlaneT[4];

	/* THE current texture coordinate, per unit (2026-09-12, GL 1.1 audit
	 * fix 8). GL says this is persistent state that each glVertex captures;
	 * glTexCoord2f used to write straight into the vertex slot about to be
	 * filled, so one texcoord followed by three vertices textured only the
	 * first and the other two sampled whatever those slots held in the
	 * PREVIOUS block. GLVertex4f now latches these exactly as it already
	 * latched CurrentColor.
	 *
	 * These hold SLOT-READY values, which is why they are separate from
	 * CurrentTexS/CurrentTexT above: those keep the RAW s,t that the vertex
	 * array gather falls back on, while glTexCoord4f stores s/q and t/q here
	 * -- its Glide-shim callers supply s,t already divided by w. Merging the
	 * two would silently change one path or the other. */
	GLfloat         CurTexU0, CurTexV0, CurTexQ0;
	GLfloat         CurTexU1, CurTexV1;

	/* The state behind eleven entry points whose gl.h macros had no function
	 * behind them until 2026-09-12 (GL 1.1 audit item 10). Appended for the
	 * same reason as everything above: no existing field offset moves.
	 *
	 * None of it reaches a pixel, and each for a reason GL itself gives. The
	 * current index and the index array have no effect in RGBA mode, the only
	 * mode this context has. The edge flag and its array only mark boundary
	 * edges for glPolygonMode GL_LINE/GL_POINT, which this driver does not
	 * implement. The read buffer can only ever be the front, which is where
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

	/* Three capabilities glEnable keeps as STATE ONLY (2026-09-13, GL 1.1
	 * audit headline 01, "the state machine accepts anything"). MGLSetState
	 * now records GL_INVALID_ENUM for a capability it does not know, so these
	 * three -- legal names this header defines, with nothing for the driver to
	 * do -- are named explicitly rather than turned into errors:
	 *  - GL_POLYGON_OFFSET_LINE / GL_POLYGON_OFFSET_POINT offset polygons drawn
	 *    in line or point polygon mode. glPolygonMode is inert here, so there
	 *    is never such a polygon to offset. draw.c's depth offset reads
	 *    PolygonOffsetFill_State alone, and must go on doing so.
	 *  - GL_SHARED_TEXTURE_PALETTE_EXT picks the shared palette over per-texture
	 *    ones. This driver has only the shared palette (GLColorTable), applied
	 *    at upload, so it is in effect either way. Quake2 and Heretic II enable
	 *    it on every start.
	 * Stored so glIsEnabled and the getters answer truthfully. GL's default for
	 * all three is disabled. Appended, like everything above. */
	GLboolean       PolygonOffsetLine_State;
	GLboolean       PolygonOffsetPoint_State;
	GLboolean       SharedTexturePalette_State;

	/* glReadPixels to the GL 1.1 standard (2026-09-15). The pack half of the
	 * pixel store state -- PackAlign above predates it; row length, the two
	 * skips and byte swapping now shape the destination of every read
	 * (others.c, GLReadPixels). GL_PACK_LSB_FIRST is stored and answered but
	 * only matters for GL_BITMAP, which an RGBA context without a stencil
	 * buffer never reads. GL defaults: 0 / FALSE. Appended, like everything
	 * above. */
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

	/* THE TEXTURE MATRIX (2026-09-19). GL_TEXTURE is a standard GL 1.1 matrix
	 * mode that neither base MiniGL nor this driver ever had. Before this, every
	 * matrix selector was a TWO-way test and they disagreed about where an
	 * unrecognised mode fell: glLoadIdentity and CMATRIX/OMATRIX/SMATRIX treated
	 * anything that was not GL_MODELVIEW as PROJECTION, while glPushMatrix and
	 * glPopMatrix treated anything that was not GL_PROJECTION as MODELVIEW. So an
	 * ordinary texture-matrix block -- push, load identity, scale, draw, pop --
	 * pushed the MODELVIEW stack, wiped and scaled the PROJECTION matrix, then
	 * popped MODELVIEW. GLASSERT compiles to nothing in a release build, so the
	 * guard meant to reject the mode never fired.
	 *
	 * APPENDED at the very end of the struct, like every addition above it: this
	 * context is shared with minigl.library's clients, so no existing field may
	 * move. Same rule as V3DTexture.no_alpha.
	 *
	 * The double-buffered Texture[2] pair mirrors ModelView/Projection so the
	 * same CMATRIX/OMATRIX/SMATRIX machinery works unchanged. */
	GLuint      TextureNr;
	Matrix      Texture[2];
	int         TextureStackPointer;
	Matrix      TextureStack[TEXTURE_STACK_SIZE];

	/* MGLCreateContextFromWindow (2026-09-20): TRUE when v3dWindow belongs to
	 * the APPLICATION, not to us -- it handed us an already-open window to
	 * render into instead of letting the driver open its own.
	 *
	 * THE POLARITY IS DELIBERATE AND LOAD-BEARING. This says "external", never
	 * "owns". A context is allocated with MEMF_CLEAR, so every field starts at
	 * zero; with this sense, zero means "the driver owns the window", which is
	 * exactly what every existing path already does. Invert it to an OwnsWindow
	 * flag and every fullscreen and ordinary windowed context silently reads as
	 * unowned, so vid_CloseWindow stops calling CloseWindow and leaks the window
	 * on every teardown.
	 *
	 * vid_CloseWindow consults it for two calls only: CloseWindow, and
	 * UnlockPubScreen -- an adopted window's screen was never locked by us
	 * either, and unlocking a pubscreen we did not lock would corrupt its lock
	 * count. Everything else in teardown is ours and is freed regardless.
	 *
	 * APPENDED at the very end, like every addition above it: this context is
	 * shared with minigl.library's clients, so no existing field may move. The
	 * 2026-09-02 V18 port got this wrong -- it added an UNGATED externalWindow
	 * field mid-struct, which shifts offsets for the statically-linked games
	 * too, and was reverted for it. */
	GLboolean   ExternalWindow;

	/* MGLCreateContextFromBitMap (2026-09-20): TRUE when v3dBitMap belongs to
	 * the APPLICATION -- it handed us a bitmap to render into and does its own
	 * presentation. Distinct from ExternalWindow, which means the opposite
	 * split: their window, OUR offscreen bitmap.
	 *
	 * Same polarity discipline and the same reason: MEMF_CLEAR makes zero mean
	 * "the driver owns the bitmap", which is what every other path does, so
	 * vid_CloseWindow keeps calling FreeBitMap for them unchanged.
	 *
	 * It also suppresses the ClipBlit in MGLSwitchDisplay. There is no window
	 * to blit into -- v3dWindow is NULL for these contexts -- and more
	 * importantly the host is presenting this bitmap itself, so a blit from us
	 * would be wrong even if there were somewhere to send it. This is what lets
	 * a program composite our output alongside another renderer's instead of
	 * the two fighting over one window. */
	GLboolean   ExternalBitMap;

};

#define     CurrentT  (&(context->Texture[context->TextureNr]))
#define     SwitchT   context->TextureNr = !(context->TextureNr)


/*
** The CMATRIX macro give the address of the currently
** active matrix, depending on the matrix mode.
** The OMATRIX macro gives the address of the secondary matrix
** The SMATRIX macro switches the active and backup matrix
*/
/* THREE-way as of 2026-09-19. These were two-way tests where anything that was
 * not GL_MODELVIEW fell through to PROJECTION, so glMatrixMode(GL_TEXTURE)
 * silently aimed every glScalef/glTranslatef/glMultMatrix at the projection
 * matrix. GL_TEXTURE now has its own storage; any mode that is still
 * unrecognised keeps the old PROJECTION fallback rather than reading past the
 * struct. */
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

/*
 * $Id: context.c,v 1.1.1.1 2000/04/07 19:44:51 hfrieden Exp $
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
 * MiniGLV3D fork of MiniGL/src/context.c, the context-lifecycle layer.
 *
 * Almost every function in the original either opened a Warp3D
 * context/screen/window directly or touched W3D_Context internals
 * (CPFlags/VPFlags/TPFlags, the Vertex Array Pipeline fast-path fields)
 * that have no V3D equivalent. The decisions below are referenced by name
 * from the functions that depend on them:
 *
 * DISPLAY OPENING -- the original had two independent, Warp3D-coupled
 * paths: vid_OpenDisplay (fullscreen, custom screen + AllocScreenBuffer-
 * based double buffering, flipped via ChangeScreenBuffer) and
 * vid_OpenWindow (windowed, AllocBitMap + ClipBlit each frame). This
 * backend has no "swap buffers" primitive: V3D renders directly into the
 * memory of a locked bitmap, at the address and stride LockBitMapTags
 * returns (context->vmembase/bprow).
 *   - vid_OpenDisplay (fullscreen): opens a custom screen in a
 *     PIXFMT_BGRA32 mode of the requested size. Single-buffer mode (the
 *     default) renders into the screen's own bitmap
 *     (screen->RastPort.BitMap). It also opens a borderless backdrop
 *     window on that screen purely for Intuition input (IDCMP) -- the
 *     original did this too (its own vid_OpenDisplay opens a
 *     WFLG_BACKDROP|WFLG_BORDERLESS window even in "fullscreen" mode):
 *     MGLMainLoop (others.c) reads key/mouse events from that window's
 *     UserPort, so without it MGLMainLoop cannot run in fullscreen. The
 *     window is context->inputWindow, separate from context->v3dWindow,
 *     which stays NULL in fullscreen. A LockBitMapTags lock held on the
 *     screen's bitmap for the whole session hangs once that window opens
 *     -- cybergraphics.library forbids library calls while a bitmap is
 *     locked -- so the bitmap is locked per frame instead (see
 *     vid_OpenDisplay's own comment).
 *   - vid_OpenWindow (windowed): opens a window on the default public
 *     screen (LockPubScreen(NULL)), as the original did, and an AllocBitMap
 *     for the drawable area -- 32-bit, unlike the original's 8-bit
 *     offscreen bitmap: V3D writes RGBA8 with no format-conversion step,
 *     so the offscreen target must already be the format V3D writes.
 *     LockBitMapTags on that bitmap gives the same direct-address/stride
 *     access as fullscreen mode. MGLSwitchDisplay copies it into the
 *     window with the original's ClipBlit call (AmigaOS/CGX API, no Warp3D
 *     coupling).
 *   - MGLSwitchDisplay: fullscreen single-buffer mode (the default) has
 *     nothing to do after the present (it already draws directly into the
 *     visible screen bitmap); windowed mode keeps the original's ClipBlit
 *     call.
 *   - Buffers[3]/NumBuffers/BufNr: the buffer count is chosen at runtime
 *     with mglChooseNumberOfBuffers, but every request gets single
 *     buffering unless the library is built with
 *     MGLV3D_DOUBLE_BUFFER_ENABLED=1 (see the newNumberOfBuffers
 *     declaration comment for why). When enabled:
 *     1 (the default) = single buffer, no vsync. 2 or 3 = fullscreen
 *     double buffering with vsync: two AllocScreenBuffer buffers, render
 *     into the back one, wait for the render, then one ChangeScreenBuffer
 *     (the original MiniGL/src/context.c pattern). On this RTG stack that
 *     call itself waits about one refresh, so the frame rate follows the
 *     refresh rate and a frame never tears; 3 buffers would bring nothing
 *     over 2 here, so 3 means 2. mglEnableSync is accepted and has no
 *     effect: the buffer count decides. See vid_OpenDisplay's and
 *     MGLSwitchDisplay's own comments.
 *   - MGLResizeContext: the original had a separate ~170-line
 *     vid_ReopenDisplay that duplicated most of vid_OpenDisplay just to
 *     resize a live screen while preserving the Warp3D context. Here it is
 *     close-then-reopen at the new size, which tears down and rebuilds
 *     both the Amiga screen/window and the backend's V3DContext, whose GPU
 *     memory pools are sized at create/resize time (v3d_context.h).
 *
 * GLClear -- the original's GLClear was Warp3D immediate-mode: lock
 * hardware, W3D_ClearDrawRegion/W3D_ClearZBuffer touch memory directly,
 * unlock. There is no such immediate "clear now" primitive in this
 * tile-based architecture -- clearing is declared as part of a
 * binning+render frame submission (TileRenderingModeCFGClearColorsPart1 /
 * ClearTileBuffers / TileRenderingModeCFGZSClearValues). A full-drawable
 * GLClear records what to clear and gl_FramePresent emits it; see GLClear's
 * own comments.
 *
 * MGLSetState -- the original immediately toggled W3D_SetState booleans
 * (alpha test, blend, texture mapping, fog, depth test, dither, point
 * smooth, perspective). Every W3D_SetState call is dropped; each case
 * updates its GLcontext-level field (AlphaTest_State, Blend_State,
 * Texture2D_State[], CullFace_State, etc.), which others.c's
 * GLGetBooleanv/GLIsEnabled read back and which draw.c reads when it sets
 * up each draw (e.g. Blend_State for the blend enable, Texture2D_State[]
 * for the textured shader variant). Alpha testing, for one, is a SHADER
 * VARIANT choice on V3D (dedicated alpha-test fragment shaders), not a
 * state bit. Two cases also mirror directly into V3DContext fields:
 * GL_ALPHA_TEST -> backend.alpha_test_enable, GL_SCISSOR_TEST ->
 * backend.scissor_enable (fog's equivalent field, backend.fog_enable, is
 * deliberately NOT mirrored here -- fog_Set's sync (fog.c, inlined in
 * draw.c) owns that field, and a second writer would serve no purpose).
 * MGL_PERSPECTIVE_MAPPING had no stored state at all even in the original
 * (pure immediate W3D_SetState, nothing to query back) -- a no-op here,
 * kept as an explicit empty case.
 *
 * GLScissor -- the original wrote to TWO different destinations depending
 * on whether scissor testing was CURRENTLY enabled: an immediate
 * W3D_SetScissor call when enabled, or a stashed "restore-to-this-when-
 * disabled" rect when disabled (MGLSetState's GL_SCISSOR_TEST case restored
 * that stash on disable). Here draw.c's per-draw ClipWindow packet reads
 * backend.scissor_x/y/w/h *and* backend.scissor_enable together -- with
 * scissor_enable off it uses the viewport rectangle alone -- so GLScissor
 * writes backend.scissor_x/y/w/h regardless of the current enable state. The Y-flip uses `context->backend.height` (the
 * render-target height V3D was sized to, via v3d_context_init) instead of
 * the original's `context->w3dWindow->Height`, so GLScissor is correct in
 * every display mode and does not depend on a window existing at all.
 *
 * MGLInitContext -- the original's Warp3D-typed initialisation is replaced
 * as follows:
 *  (1) w3dTexBuffer/w3dTexMemory allocation -> textureObjects allocation
 *      (context.h's rename; w3dTexMemory has no analog, dropped).
 *  (2) context->w3dFog.fog_start/end/density/fog_color.r/g/b (W3D_Fog-typed)
 *      -> context->backend.fog_start/end/density/fog_r/g/b (matches fog.c's
 *      own field mapping).
 *  (3) The "Vertex Array Pipeline" block (context->w3dContext->CPFlags/
 *      VPFlags/TPFlags/TPVOffs, context->ArrayPointer.colormode/vertexmode
 *      using W3D_COLOR_FLOAT/W3D_CMODE_RGBA/W3D_VERTEX_F_F_D) configured
 *      Warp3D's OWN vertex-array hardware acceleration fast path -- V3D's
 *      native equivalent is the attribute-record assembly
 *      (VertexArrayPrims/glShaderStateAttributeRecord), architecturally
 *      different. The CPFlags/VPFlags/TPFlags/TPVOffs lines (literal
 *      W3D_Context field writes) are dropped entirely -- no such struct
 *      exists here. The plain pointer/stride fields
 *      (ArrayPointer.colors/texcoords/verts) ARE ported, using the
 *      corrected MGLVertex_t field paths: `.color` (top-level field, not
 *      `.v.color`) and `.v.u0`/`.v.v0` (not `.v.u`/`.v.v`).
 *
 * GLDepthFunc -- GL_NEVER..GL_ALWAYS map onto the V3D_Z_* IDs
 * (backend/include/v3d_context.h), stored in backend.zmode; see that
 * enum's own comment for how the draw path consumes it.
 *
 * GLDepthMask/GLClearColor/GLClearDepth -- pure state, stored in
 * GLcontext_t fields; the original's W3D_SetState call (GLDepthMask) and
 * W3D_Double cast (GLClearDepth) are dropped.
 *
 * MGLLockDisplay/MGLUnlockDisplay/MGLLockBack/MGLLockMode -- Warp3D's
 * "lock hardware, get a direct framebuffer pointer, unlock" model doesn't
 * exist in this backend (the only hardware-touching primitive is submitting
 * a whole binning/render CL -- see GLClear above). These are state-only
 * stubs with no hardware call; MGLLockDisplay/MGLUnlockDisplay/MGLLockBack
 * still track context->v3dLocked. MGLLockBack's MGLLockInfo
 * (width/height/pixel_format/base_address/pitch) is filled from
 * context->backend.width/height and context->vmembase/bprow instead of
 * W3D_Context's fields -- format is always RGBA8 (V3D has one fixed
 * format, no negotiation, matching context.h's own w3dFormat-dropped
 * rationale).
 *
 * mglGetSupportedScreenModes / MGLCreateContextFromID -- in the original,
 * mglGetSupportedScreenModes is built on Warp3D's own screen-mode
 * enumeration API (W3D_GetScreenmodeList / W3D_ScreenMode), which has no
 * V3D equivalent (V3D isn't a Warp3D driver, it doesn't register mode
 * lists with one), and MGLCreateContextFromID opens a Warp3D context on a
 * mode ID from that list. Both are stubs here rather than an AmigaOS-native mode-enumeration
 * replacement. MGLCreateContext (the explicit-w/h constructor) is fully
 * ported.
 */

#include "sysinc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../backend/include/v3d_device.h"
#include "../../backend/include/v3d_context.h"
#include "../../backend/include/v3d_commands.h"
#include "../../backend/include/v3d_clbuf.h"
#include "../../backend/hw/v3d_hw.h"
#include "../../backend/hw/v3d_regs.h"
#include "../../backend/hw/v3d_submit_timeout.h"
/*
 * gl_FramePresent and MGLFlushPendingRender wait for the GPU only through
 * the bounded-timeout functions (v3d_wait_binning_timeout/
 * v3d_wait_render_timeout, backend/hw/v3d_submit_timeout.c): an unbounded
 * wait genuinely, if rarely, never completes on real hardware (not a logic
 * bug -- an actual GPU-completion-signal miss), and with no timeout that's
 * an unrecoverable hard hang.
 *
 * PER-FRAME LOCKING: vid_OpenDisplay never locks at open time;
 * gl_FramePresent locks fresh immediately before each render, and
 * MGLFlushPendingRender releases the lock after waiting for that render
 * (see vid_OpenDisplay's own comment for the cybergraphics.library rule
 * behind this).
 *
 * DOUBLE BUFFERING uses the same per-frame lock: gl_FramePresent locks the
 * current back buffer (context->Buffers[context->BufNr]->sb_BitMap)
 * instead of the screen bitmap. Without a real lock held during the GPU
 * render into a buffer, the display shows a stuck or repeated buffer, and a
 * lock held across the flip shows nothing at all. So the lock is held for
 * the render only and released before ChangeScreenBuffer.
 */
#include "../../backend/hw/v3d_debug.h"

static char rcsid[] = "$Id: context.c,v 1.1.1.1 2000/04/07 19:44:51 hfrieden Exp $";


extern void GLMatrixInit(GLcontext context);
GLboolean MGLInitContext(GLcontext context);

extern struct IntuitionBase *IntuitionBase;
extern struct ExecBase *SysBase;
extern struct GfxBase *GfxBase;
extern struct DosLibrary *DOSBase;
extern struct Library *CyberGfxBase;

GLcontext mini_CurrentContext;

extern void tex_FreeTextures(GLcontext context);

#ifndef NLOGGING
int MGLDebugLevel;
#endif

/* Default values for new context */

/* Surgeon: bumped newVertexBufferSize from 40 to 256 because of increased
 * requirement for clipping space due to buffering (MiniGL source). */

static int newVertexBufferSize = 256;            /* Default: 256 entries in vertex buffer */
static int newMTBufferSize    = 4096;            /* Default: 4096 verts storage-space */

/* 4096, not the original's 2048: Quake2's own texture numbers run past
 * 2048. A name at or above this size is refused (tex_NameInRange,
 * texture.c) rather than used as an index into
 * context->textureObjects[]/GeneratedTextures[], so this size is headroom,
 * not a safety margin. Each entry costs sizeof(V3DTexture*) +
 * sizeof(GLubyte). */
static int newTextureBufferSize = 4096;          /* Default: 4096 texture objects */
static GLboolean newWindowMode = GL_FALSE;       /* Default: Use fullscreen instead of window */
static GLboolean clw = GL_FALSE;                 /* Default: Keep workbench open */
static GLboolean newNoMipMapping = GL_TRUE;      /* Default: No mipmapping */
static GLboolean newNoFallbackAlpha = GL_FALSE;  /* Default: Fall back to supported blend mode */
static GLboolean newGuardBand = GL_FALSE;        /* Default: guardband clipping is off */

/* newNumberOfBuffers: read by vid_OpenDisplay only -- 1 = single buffer
 * without vsync, 2 or 3 = fullscreen double buffering with vsync (see this
 * file's header comment). Windowed mode always renders single-buffer.
 *
 * DOUBLE BUFFERING IS OFF BY DEFAULT: MGLV3D_DOUBLE_BUFFER_ENABLED is 0,
 * so every request silently gets single buffering. The code is compiled
 * either way; build with -DMGLV3D_DOUBLE_BUFFER_ENABLED=1 to enable it. Why
 * off: on the Pi 4 RTG stack ChangeScreenBuffer sometimes holds a frame on
 * screen for 2-5 refreshes, so vsync mode stutters.
 *
 * newPixelDepth is accepted-but-vestigial API -- V3D always renders 32-bit
 * RGBA8, so the setter is kept only so mglChoosePixelDepth callers still
 * link and don't error. vid_OpenDisplay does not pass it as SA_Depth:
 * CyberGraphX's CYBRIDATTR_DEPTH only counts RGB, not alpha, so the
 * PIXFMT_BGRA32 mode vid_OpenDisplay searches for (32-bit storage:
 * 8+8+8 color + 8 alpha) reports its own depth as 24, not 32, and
 * requesting SA_Depth=32 against that mode can make OpenScreenTags fail
 * with errorCode=7 (OSERR_TOODEEP). SA_Depth is the mode's real depth. */
#ifndef MGLV3D_DOUBLE_BUFFER_ENABLED
#define MGLV3D_DOUBLE_BUFFER_ENABLED 0
#endif
static int newNumberOfBuffers = 1;
static int newPixelDepth = 24;

static UWORD *MousePointer = 0;

/* OF: AllocVec size 8 (was 12) with MEMF_PUBLIC, and the SetPointer
 * arguments 0, 0, 0, 0 (were 1, 16, 0, 0) -- marked "OF" in the MiniGL
 * source. */
static void vid_Pointer(struct Window *window)
{
	if (!MousePointer)
	{
		MousePointer = AllocVec(8, MEMF_CLEAR|MEMF_CHIP|MEMF_PUBLIC);
	}

	if (window)         SetPointer(window, MousePointer, 0, 0, 0, 0);
}

static void vid_DeletePointer(struct Window *window)
{
	if (window)         ClearPointer(window);
	FreeVec(MousePointer);
	MousePointer = 0;
}

/*
 * MGLSetPointer/MGLClearPointer are mglq3-specific extensions (not in the
 * original base MiniGL), called through the bare
 * mglSetPointer()/mglClearPointer() macros (gl.h). Thin wrappers around
 * vid_Pointer/vid_DeletePointer above, matching mglq3's own
 * MGLSetPointer(context) -> vid_Pointer(context->w3dWindow) pattern
 * exactly (w3dWindow is v3dWindow in this port, see context.h's own
 * field-rename comment). Marked "Cowcat" in mglq3's context.c.
 */
void MGLSetPointer(GLcontext context)
{
	vid_Pointer(context->v3dWindow);
}

void MGLClearPointer(GLcontext context)
{
	vid_DeletePointer(context->v3dWindow);
}

void GLScissor(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height)
{
	/* Keep GL's own rectangle for glGetIntegerv(GL_SCISSOR_BOX): the backend
	 * copy below is top-origin and 16-bit, so it cannot be handed back. */
	context->ScissorBox[0] = x;
	context->ScissorBox[1] = y;
	context->ScissorBox[2] = (GLint)width;
	context->ScissorBox[3] = (GLint)height;

	/* Unconditional -- see this file's header comment on why the
	 * original's enabled-vs-disabled dual destination is gone. */
	context->backend.scissor_x = (v3d_u16)x;
	context->backend.scissor_y = (v3d_u16)(context->backend.height - y - height);
	context->backend.scissor_w = (v3d_u16)width;
	context->backend.scissor_h = (v3d_u16)height;
}

static void vid_CloseDisplay(GLcontext context, GLboolean keep_backend)
{
	if (context->bitmapLock)
	{
		UnLockBitMap(context->bitmapLock);
		context->bitmapLock = NULL;
	}

	/* Double buffering: put Buffers[0] (the screen's own bitmap) back on
	 * display before freeing -- the original's own teardown order
	 * (MiniGL/src/context.c:182-192). Nothing is rendering into either
	 * buffer here: MGLSwitchDisplay waits for every render before it
	 * returns in this mode. Both pointers are NULL in single-buffer mode
	 * and after a failed allocation. BufNr is the back buffer, so
	 * Buffers[1] is on display exactly when BufNr is 0. */
	if (context->Buffers[0])
	{
		if (context->Buffers[1] && context->BufNr == 0)
		{
			context->Buffers[0]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = NULL;
			while (!ChangeScreenBuffer(context->v3dScreen, context->Buffers[0]));
		}
		FreeScreenBuffer(context->v3dScreen, context->Buffers[0]);
		context->Buffers[0] = NULL;
	}
	if (context->Buffers[1])
	{
		FreeScreenBuffer(context->v3dScreen, context->Buffers[1]);
		context->Buffers[1] = NULL;
	}
	context->NumBuffers = 0;
	context->BufNr = 0;

	/* keep_backend (MGLResizeContext): the textures and the backend, with
	 * all its render state, survive; v3d_context_resize reallocates only the
	 * size-dependent part when the display reopens. */
	if (!keep_backend)
	{
		tex_FreeTextures(context);
		v3d_context_free(&context->device, &context->backend);
	}

	/* Backdrop window opened purely for MGLMainLoop's IDCMP input (see
	 * vid_OpenDisplay's own comment) -- v3dWindow stays NULL for the
	 * whole fullscreen session, so vid_DeletePointer(v3dWindow) below
	 * never actually clears a pointer from it (skips via its own NULL
	 * check), it just does the real MousePointer FreeVec unconditionally.
	 * inputWindow is the real window whose pointer vid_Pointer() actually
	 * blanked -- ClearPointer it directly here.
	 * MUST close inputWindow BEFORE CloseScreen below: it's a child
	 * window of v3dScreen (WA_CustomScreen) -- AmigaOS requires every
	 * window on a screen to close before the screen itself can, so
	 * closing it AFTER CloseScreen leaves CloseScreen silently failing,
	 * stranding both the window and the screen open. */
	if (context->inputWindow)
	{
		ClearPointer(context->inputWindow);
		CloseWindow(context->inputWindow);
		context->inputWindow = NULL;
	}

	vid_DeletePointer(context->v3dWindow);
	if (context->v3dWindow) CloseWindow(context->v3dWindow);

	if (context->v3dScreen) CloseScreen(context->v3dScreen);
	context->v3dWindow = NULL;
	context->v3dScreen = NULL;

	if (clw == GL_TRUE) OpenWorkBench();
}

/* Fullscreen path. v3dWindow stays NULL: the backdrop window opened below
 * for input is context->inputWindow.
 *
 * The screen's bitmap is never locked here. The cybergraphics.library
 * autodoc (LockBitMapTagList, NOTES) says "DON'T lock the bitmap longer
 * than for one frame. DON'T use any library calls while the bitmap is
 * locked!", and LBMI_BASEADDRESS is only valid inside the lock, so the
 * address cannot be cached after unlocking either. A lock held for the
 * whole open-to-close session hangs once the backdrop window is opened on
 * the same screen, whichever of the two comes first. gl_FramePresent
 * locks fresh immediately before each frame's render instead, and
 * MGLFlushPendingRender unlocks after waiting for that render. */
static GLboolean vid_OpenDisplay(GLcontext context, int w, int h, GLboolean keep_backend)
{
	ULONG modeID;
	ULONG errorCode;
	ULONG realDepth;
	int ret;

	if (!context)
	    return GL_FALSE;

	if (clw == GL_TRUE)
	    CloseWorkBench();

	D(("vid_OpenDisplay: enter, w=%ld h=%ld newPixelDepth=%ld\n", (LONG)w, (LONG)h, (LONG)newPixelDepth));

	/* No CYBRBIDTG_PixelFormat-style tag exists in this SDK, so there's no
	 * way to ask BestCModeIDTags for a specific pixel/channel order --
	 * walk the WHOLE display database via NextDisplayInfo (graphics.library)
	 * until we find a mode that's actually PIXFMT_BGRA32 at the requested
	 * size, or run out of modes entirely. Per NextDisplayInfo's own
	 * autodoc, passing INVALID_ID begins iteration from the START of the
	 * database. IsCyberModeID guards GetCyberIDAttr from being asked about
	 * a non-CyberGraphX (e.g. native chipset) mode ID, where
	 * CYBRIDATTR_PIXFMT isn't meaningful. */
	D(("vid_OpenDisplay: searching for a %ldx%ld PIXFMT_BGRA32 ModeID\n", (LONG)w, (LONG)h));

	modeID = NextDisplayInfo(INVALID_ID);

	while (modeID != INVALID_ID)
	{
		if (IsCyberModeID(modeID))
		{
			ULONG pixfmt = GetCyberIDAttr(CYBRIDATTR_PIXFMT, modeID);
			ULONG mw = GetCyberIDAttr(CYBRIDATTR_WIDTH, modeID);
			ULONG mh = GetCyberIDAttr(CYBRIDATTR_HEIGHT, modeID);

			if (pixfmt == PIXFMT_BGRA32 && mw == (ULONG)w && mh == (ULONG)h)
				break;
		}

		modeID = NextDisplayInfo(modeID);
	}

	if (modeID == INVALID_ID)
	{
		E(("vid_OpenDisplay: no %ldx%ld PIXFMT_BGRA32 ModeID found\n", (LONG)w, (LONG)h));
		D(("vid_OpenDisplay: no usable ModeID found\n"));
		return GL_FALSE;
	}

	D(("vid_OpenDisplay: found ModeID 0x%08lx\n", (ULONG)modeID));

	/*
	 * SA_Depth never comes from the caller-supplied newPixelDepth -- that
	 * is just whatever the last mglChoosePixelDepth() call happened to
	 * pass, and a value deeper than the mode's own depth (32 against the 24
	 * this mode reports) can make OpenScreenTags fail with OSERR_TOODEEP
	 * (see newPixelDepth's own declaration comment above). Read the ACTUAL depth of the mode just found instead --
	 * matches the mode unconditionally, regardless of what any GL client
	 * asked mglChoosePixelDepth() for.
	 */
	realDepth = GetCyberIDAttr(CYBRIDATTR_DEPTH, modeID);
	D(("vid_OpenDisplay: mode's real depth=%ld (newPixelDepth was %ld, now ignored)\n",
	   (LONG)realDepth, (LONG)newPixelDepth));

	context->v3dScreen = OpenScreenTags(NULL,
		SA_Depth,      realDepth,
		SA_DisplayID,  modeID,
		SA_Width,      (ULONG)w,
		SA_Height,     (ULONG)h,
		SA_ErrorCode,  (ULONG)&errorCode,
		SA_ShowTitle,  FALSE,
		SA_Draggable,  FALSE,
		TAG_DONE);

	if (!context->v3dScreen)
	{
		E(("vid_OpenDisplay: OpenScreenTags FAILED, modeID=%08lx errorCode=%ld -- returning GL_FALSE\n", (ULONG)modeID, (LONG)errorCode));
		D(("vid_OpenDisplay: OpenScreenTags failed, modeID=%08lx errorCode=%ld\n", (ULONG)modeID, (LONG)errorCode));
		return GL_FALSE;
	}
	D(("vid_OpenDisplay: OpenScreenTags OK, modeID=%08lx\n", (ULONG)modeID));

	/* keep_backend reuses the backend context and only resizes its
	 * size-dependent pools, so textures and driver state survive a
	 * resolution change (MGLResizeContext); a fresh open builds it. */
	ret = keep_backend
	    ? v3d_context_resize(&context->backend, &context->device, (v3d_u16)w, (v3d_u16)h)
	    : v3d_context_init(&context->backend, &context->device, (v3d_u16)w, (v3d_u16)h);

	if (ret != 0)
	{
		E(("vid_OpenDisplay: v3d_context_init/resize FAILED, ret=%ld -- returning GL_FALSE\n", (LONG)ret));
		D(("vid_OpenDisplay: v3d_context_init failed, ret=%ld\n", (LONG)ret));
		goto Duh;
	}
	D(("vid_OpenDisplay: v3d_context_init OK\n"));

	/* Never lock here at all (see this function's header comment): no lock
	 * is held while the window opens. context->bitmapLock/vmembase stay
	 * NULL/0 here; gl_FramePresent locks fresh immediately before it needs
	 * the address. Double buffering below uses the same per-frame lock. */
	context->bitmapLock = NULL;
	context->vmembase = 0;
	context->bprow = 0;
	context->Buffers[0] = NULL;
	context->Buffers[1] = NULL;
	context->Buffers[2] = NULL;
	context->NumBuffers = 0;
	context->BufNr = 0;

	/* Double buffering with vsync, when the program asked for 2 or 3
	 * buffers (mglChooseNumberOfBuffers) -- the original MiniGL's own
	 * AllocScreenBuffer pattern (MiniGL/src/context.c:465-501).
	 * Buffers[0] wraps the screen's own bitmap, already visible;
	 * Buffers[1] is a second, same-size bitmap. BufNr=1: the first frame
	 * renders into Buffers[1] while Buffers[0] stays on display.
	 *
	 * The dbi_SafeMessage reply port stays NULL (MGLSwitchDisplay sets it
	 * before every flip): ChangeScreenBuffer completes the swap inside the
	 * call on this RTG stack, with or without reply ports, so there is
	 * nothing to wait for afterwards.
	 *
	 * If either buffer can't be allocated, carry on single-buffered rather
	 * than failing the whole context -- MGLResizeContext ignores this
	 * function's result, and a game that runs without vsync beats one that
	 * doesn't start.
	 *
	 * Off by default: never taken unless built with
	 * MGLV3D_DOUBLE_BUFFER_ENABLED=1 (see the newNumberOfBuffers
	 * declaration comment). */
	if (MGLV3D_DOUBLE_BUFFER_ENABLED && newNumberOfBuffers >= 2)
	{
		context->Buffers[0] = AllocScreenBuffer(context->v3dScreen, NULL, SB_SCREEN_BITMAP);
		if (context->Buffers[0])
			context->Buffers[1] = AllocScreenBuffer(context->v3dScreen, NULL, 0);

		if (context->Buffers[0] && context->Buffers[1])
		{
			struct RastPort backrp;

			/* AllocScreenBuffer's own bitmap isn't guaranteed clear, and a
			 * first frame that loads color instead of clearing would show
			 * whatever that memory held. The original cleared every buffer
			 * at open too (MiniGL/src/context.c:584-592). Nothing is locked
			 * here, so this library call is allowed. */
			InitRastPort(&backrp);
			backrp.BitMap = context->Buffers[1]->sb_BitMap;
			FillPixelArray(&backrp, 0, 0, (UWORD)context->v3dScreen->Width,
				(UWORD)context->v3dScreen->Height, 0x00000000);

			context->NumBuffers = 2;
			context->BufNr = 1;
			D(("vid_OpenDisplay: double buffering (%ld requested, 2 used)\n", (LONG)newNumberOfBuffers));
		}
		else
		{
			E(("vid_OpenDisplay: AllocScreenBuffer failed (0=%lx 1=%lx) -- continuing single-buffered\n",
			   (ULONG)context->Buffers[0], (ULONG)context->Buffers[1]));
			if (context->Buffers[0])
			{
				FreeScreenBuffer(context->v3dScreen, context->Buffers[0]);
				context->Buffers[0] = NULL;
			}
		}
	}

	GLScissor(context, 0, 0, w, h);

	vid_Pointer(context->v3dWindow);

	D(("vid_OpenDisplay: opened %ldx%ld, tilesX=%ld tilesY=%ld\n",
	   (LONG)context->v3dScreen->Width, (LONG)context->v3dScreen->Height,
	   (LONG)context->backend.frame.tilesX, (LONG)context->backend.frame.tilesY));

	/* Always open a borderless backdrop window on this fullscreen screen,
	 * purely for IDCMP input -- a bare Screen has no message port of its
	 * own. It becomes context->inputWindow, which MGLMainLoop (others.c)
	 * reads and MGLGetWindowHandle/MGLGetInputWindowHandle return. NOT
	 * assigned to context->v3dWindow (that field means "true windowed
	 * RENDERING mode, v3dBitMap is the real render target" throughout this
	 * codebase -- setting it here would wrongly flip every v3dWindow-fork
	 * elsewhere). Failure here is non-fatal -- the screen still renders;
	 * only input through this window is lost. */
	{
		struct TagItem BackdropWinTags[8];

		BackdropWinTags[0].ti_Tag  = WA_CustomScreen;
		BackdropWinTags[0].ti_Data = (ULONG)context->v3dScreen;
		BackdropWinTags[1].ti_Tag  = WA_Width;
		BackdropWinTags[1].ti_Data = (ULONG)context->v3dScreen->Width;
		BackdropWinTags[2].ti_Tag  = WA_Height;
		BackdropWinTags[2].ti_Data = (ULONG)context->v3dScreen->Height;
		BackdropWinTags[3].ti_Tag  = WA_Left;
		BackdropWinTags[3].ti_Data = 0;
		BackdropWinTags[4].ti_Tag  = WA_Top;
		BackdropWinTags[4].ti_Data = 0;
		BackdropWinTags[5].ti_Tag  = WA_Title;
		BackdropWinTags[5].ti_Data = (ULONG)NULL;
		BackdropWinTags[6].ti_Tag  = WA_Flags;
		BackdropWinTags[6].ti_Data = WFLG_ACTIVATE | WFLG_BORDERLESS | WFLG_BACKDROP | WFLG_REPORTMOUSE | WFLG_RMBTRAP;
		BackdropWinTags[7].ti_Tag  = TAG_DONE;
		BackdropWinTags[7].ti_Data = 0;

		context->inputWindow = OpenWindowTagList(NULL, BackdropWinTags);
		if (!context->inputWindow)
		{
			D(("vid_OpenDisplay: backdrop OpenWindowTagList failed (not fatal, MGLMainLoop just won't work)\n"));
		}
		else
		{
			D(("vid_OpenDisplay: backdrop input window opened OK\n"));

			/* vid_Pointer(context->v3dWindow) above (always NULL in
			 * fullscreen) only allocates the blank pointer. Intuition
			 * shows a normal mouse pointer for this window unless told
			 * otherwise -- blank it the same way, matching the original
			 * "no visible pointer in fullscreen" behavior. */
			vid_Pointer(context->inputWindow);
		}
	}

	D(("vid_OpenDisplay: exit OK\n"));

	return GL_TRUE;

Duh:
	E(("vid_OpenDisplay: Duh -- opening of fullscreen display failed\n"));
	printf("Error: opening of fullscreen display failed\n");
	vid_CloseDisplay(context, GL_FALSE);
	return GL_FALSE;
}

static void vid_CloseWindow(GLcontext context)
{
	D(("vid_CloseWindow: entry, bitmapLock=%lx\n", (ULONG)context->bitmapLock));

	if (context->bitmapLock)
	{
		UnLockBitMap(context->bitmapLock);
		context->bitmapLock = NULL;
	}
	D(("vid_CloseWindow: after bitmapLock unlock\n"));


	tex_FreeTextures(context);
	D(("vid_CloseWindow: after tex_FreeTextures\n"));


	v3d_context_free(&context->device, &context->backend);
	D(("vid_CloseWindow: after v3d_context_free\n"));


	/* Both guarded on ExternalWindow: with MGLCreateContextFromWindow the
	 * window is the application's and its screen was never locked by us.
	 * Closing a window the host still owns, or decrementing a pubscreen lock
	 * we never took, corrupts state outside this process. Everything else
	 * below is ours and is freed either way. */
	if (context->v3dWindow && !context->ExternalWindow)   CloseWindow(context->v3dWindow);
	D(("vid_CloseWindow: after CloseWindow (external=%ld)\n", (LONG)context->ExternalWindow));


	if (context->v3dScreen && !context->ExternalWindow)   UnlockPubScreen(NULL, context->v3dScreen);
	D(("vid_CloseWindow: after UnlockPubScreen\n"));

	/* Guarded on ExternalBitMap: with MGLCreateContextFromBitMap
	 * the render target is the APPLICATION's bitmap, not one we allocated.
	 * Note this is a different split from ExternalWindow above -- an adopted
	 * WINDOW still uses an offscreen bitmap that IS ours and must be freed. */
	if (context->v3dBitMap && !context->ExternalBitMap)   FreeBitMap(context->v3dBitMap);
	D(("vid_CloseWindow: after FreeBitMap\n"));

	if (context->v3dRastPort) free(context->v3dRastPort);
	D(("vid_CloseWindow: after free(v3dRastPort)\n"));

	context->v3dWindow = NULL;
	context->v3dScreen = NULL;
	context->v3dBitMap = NULL;
	context->v3dRastPort = NULL;
	context->inputWindow = NULL;
	D(("vid_CloseWindow: exit\n"));
}

static GLboolean vid_OpenWindow(GLcontext context, int w, int h)
{
	int ret;

	struct TagItem OpenWinTags[] =
	{
		{WA_PubScreen,           0},
		{WA_InnerWidth,          0},
		{WA_InnerHeight,         0},
		{WA_Left,                30},
		{WA_Top,                 30},
		{WA_Title,               (ULONG)"MiniGLV3D Display"},
		{WA_DragBar,             TRUE},
		{WA_DepthGadget,         TRUE},
		{WA_Flags,               WFLG_ACTIVATE|WFLG_REPORTMOUSE|WFLG_RMBTRAP},
		{TAG_DONE,               0}
	};

	if (!context)
	    return GL_FALSE;

	context->v3dScreen = LockPubScreen(NULL);
	if (!context->v3dScreen) return GL_FALSE;

	OpenWinTags[0].ti_Data = (ULONG)context->v3dScreen;
	OpenWinTags[1].ti_Data = (ULONG)w;
	OpenWinTags[2].ti_Data = (ULONG)h;

	context->v3dWindow = OpenWindowTagList(NULL, OpenWinTags);
	if (!context->v3dWindow) goto Duh;

	/* 32-bit, not the original's 8-bit -- V3D writes RGBA8 directly with
	 * no format-conversion step (see this file's header comment). */
	context->v3dBitMap = AllocBitMap((ULONG)w, (ULONG)h, 32, BMF_MINPLANES|BMF_DISPLAYABLE,
		context->v3dWindow->RPort->BitMap);
	if (!context->v3dBitMap) goto Duh;

	context->v3dRastPort = malloc(sizeof(struct RastPort));
	if (!context->v3dRastPort)
	{
		printf("Error: unable to allocate rastport memory\n");
		goto Duh;
	}

	InitRastPort(context->v3dRastPort);
	context->v3dRastPort->BitMap = context->v3dBitMap;

	ret = v3d_context_init(&context->backend, &context->device, (v3d_u16)w, (v3d_u16)h);
	if (ret != 0) goto Duh;

	/* Probe only, don't hold: gl_FramePresent's real per-frame lock (see
	 * its own comment) locks this same v3dBitMap every frame, on the SAME
	 * context->bitmapLock field. Locking a bitmap that's already held, with
	 * no unlock in between, is exactly the "holding a LockBitMapTags hold
	 * across another library call" hang this project's own vid_OpenDisplay
	 * comment already documents for fullscreen mode. So: lock, confirm it
	 * succeeds (and pick up bprow/vmembase), unlock immediately, leaving
	 * the REAL per-frame lock entirely to gl_FramePresent. */
	{
		void *probe = LockBitMapTags(context->v3dBitMap,
			LBMI_BYTESPERROW, (ULONG)&context->bprow,
			LBMI_BASEADDRESS, (ULONG)&context->vmembase,
			TAG_DONE);
		if (!probe) goto Duh;
		UnLockBitMap(probe);
	}

	GLScissor(context, 0, 0, w, h);

	/* Real windowed mode: the same real window serves both rendering
	 * (v3dWindow) and MGLMainLoop's IDCMP input needs (inputWindow) --
	 * see context.h's own field comment. */
	context->inputWindow = context->v3dWindow;

	return GL_TRUE;

Duh:
	printf("Error: opening of windowed display failed\n");
	vid_CloseWindow(context);
	return GL_FALSE;
}

/*
 * vid_OpenWindow's twin for a window the APPLICATION already opened
 * (MGLCreateContextFromWindow). Everything below the window is
 * identical -- same off-screen 32-bit bitmap, same rastport, same backend init,
 * same probe lock -- so the per-frame present path needs no changes at all.
 *
 * Two things it must NOT do, and both would be silent corruption rather than a
 * visible failure:
 *   - no OpenWindowTagList: the window is the caller's.
 *   - no LockPubScreen: an adopted window's screen is already locked by whoever
 *     opened the window. Locking it again here would need a matching unlock at
 *     teardown, and getting that count wrong outlives our process.
 * context->ExternalWindow is what tells vid_CloseWindow to skip both.
 */
static GLboolean vid_AdoptWindow(GLcontext context, struct Window *window, int w, int h)
{
	int ret;

	if (!context || !window)
		return GL_FALSE;

	/* FIRST, before anything can fail. Every failure below lands on the Duh
	 * label, which calls vid_CloseWindow -- and that must already know not to
	 * close the caller's window on the way out. */
	context->ExternalWindow = GL_TRUE;

	context->v3dWindow = window;
	context->v3dScreen = window->WScreen;

	context->v3dBitMap = AllocBitMap((ULONG)w, (ULONG)h, 32, BMF_MINPLANES|BMF_DISPLAYABLE,
		window->RPort->BitMap);
	if (!context->v3dBitMap) goto Duh;

	context->v3dRastPort = malloc(sizeof(struct RastPort));
	if (!context->v3dRastPort)
	{
		printf("Error: unable to allocate rastport memory\n");
		goto Duh;
	}

	InitRastPort(context->v3dRastPort);
	context->v3dRastPort->BitMap = context->v3dBitMap;

	ret = v3d_context_init(&context->backend, &context->device, (v3d_u16)w, (v3d_u16)h);
	if (ret != 0) goto Duh;

	/* Probe only, never held -- see vid_OpenWindow's own comment for the
	 * windowed-mode hang this shape exists to avoid. */
	{
		void *probe = LockBitMapTags(context->v3dBitMap,
			LBMI_BYTESPERROW, (ULONG)&context->bprow,
			LBMI_BASEADDRESS, (ULONG)&context->vmembase,
			TAG_DONE);
		if (!probe) goto Duh;
		UnLockBitMap(probe);
	}

	GLScissor(context, 0, 0, w, h);

	/* The caller's window serves for input too, same as real windowed mode.
	 * A host with its own event loop simply never calls MGLMainLoop. */
	context->inputWindow = context->v3dWindow;

	return GL_TRUE;

Duh:
	printf("Error: adopting the application's window failed\n");
	vid_CloseWindow(context);
	return GL_FALSE;
}

/*
 * vid_AdoptWindow's sibling for a BITMAP the application owns
 * (MGLCreateContextFromBitMap). Shorter than either of the other
 * two, because it neither opens nor allocates anything: the caller's bitmap IS
 * the render target.
 *
 * v3dWindow stays NULL, deliberately -- there is no window here, and that is
 * exactly what MGLSwitchDisplay's ExternalBitMap check relies on to skip the
 * ClipBlit. The host presents; we only fill.
 */
static GLboolean vid_AdoptBitMap(GLcontext context, struct BitMap *bitmap, int w, int h)
{
	int ret;

	if (!context || !bitmap)
		return GL_FALSE;

	/* FIRST, so the Duh path below cannot free the caller's bitmap. */
	context->ExternalBitMap = GL_TRUE;

	context->v3dBitMap = bitmap;

	context->v3dRastPort = malloc(sizeof(struct RastPort));
	if (!context->v3dRastPort)
	{
		printf("Error: unable to allocate rastport memory\n");
		goto Duh;
	}

	InitRastPort(context->v3dRastPort);
	context->v3dRastPort->BitMap = context->v3dBitMap;

	ret = v3d_context_init(&context->backend, &context->device, (v3d_u16)w, (v3d_u16)h);
	if (ret != 0) goto Duh;

	/* Probe only, never held -- see vid_OpenWindow's comment. This also proves
	 * the caller's bitmap is genuinely CGX-lockable before any rendering is
	 * attempted against it. */
	{
		void *probe = LockBitMapTags(context->v3dBitMap,
			LBMI_BYTESPERROW, (ULONG)&context->bprow,
			LBMI_BASEADDRESS, (ULONG)&context->vmembase,
			TAG_DONE);
		if (!probe)
		{
			printf("Error: the supplied bitmap could not be locked\n");
			goto Duh;
		}
		UnLockBitMap(probe);

		/* FOUR BYTES PER PIXEL, checked HERE and by bytes-per-row -- NOT by
		 * depth.
		 *
		 * Depth is the wrong question: on CGX a truecolour bitmap reports
		 * depth 24, because the eighth byte is alpha and alpha is not depth.
		 * A GetBitMapAttr(BMA_DEPTH) == 32 test would reject perfectly good
		 * ARGB32 bitmaps -- including the driver's OWN AllocBitMap(...,32,...)
		 * surfaces.
		 *
		 * What actually matters is that V3D can write RGBA8 straight into this
		 * memory, i.e. four bytes per pixel. bprow comes from the lock we just
		 * did, so this costs nothing extra and tests the real requirement. A
		 * 16-bit bitmap gives bprow ~= width*2 and is refused; padding only
		 * makes bprow larger, never smaller, so the comparison is safe.
		 * (GetCyberMapAttr's CYBRMATTR_BPPIX would answer the same question
		 * with an extra call; bprow is already in hand from the lock above.) */
		if (context->bprow < (ULONG)w * 4)
		{
			E(("vid_AdoptBitMap: bprow %lu for width %ld -- needs 4 bytes/pixel\n",
			   (ULONG)context->bprow, (LONG)w));
			printf("Error: the supplied bitmap is not 32-bit (needs 4 bytes per pixel,\n"
			       "       got %lu bytes per row for a width of %d)\n",
			       (unsigned long)context->bprow, w);
			goto Duh;
		}
	}

	GLScissor(context, 0, 0, w, h);

	/* No window, so no input window either: a host that supplies a bare bitmap
	 * is running its own event loop by definition and never calls MGLMainLoop. */
	context->inputWindow = NULL;

	return GL_TRUE;

Duh:
	printf("Error: adopting the application's bitmap failed\n");
	vid_CloseWindow(context);
	return GL_FALSE;
}

void MGLResizeContext(GLcontext context, GLsizei width, GLsizei height)
{
	/* Fullscreen only -- matches the original, which only ever resized
	 * the no-w3dBitMap (fullscreen) case. Like the original's
	 * vid_ReopenDisplay, the screen and the size-dependent render memory are
	 * rebuilt while the textures and all GL/render state survive: wait for
	 * the last render, drop any pass in progress (its command lists were
	 * built for the old size; the next draw starts a fresh one), then close
	 * and reopen keeping the backend. */
	if (!context->v3dBitMap)
	{
		extern void MGLFlushPendingRender(GLcontext context);

		MGLFlushPendingRender(context);
		context->backend.frame_active = FALSE;
		vid_CloseDisplay(context, GL_TRUE);
		vid_OpenDisplay(context, (int)width, (int)height, GL_TRUE);
	}
}

/* Returns context->inputWindow, the same window MGLGetInputWindowHandle()
 * returns -- NOT context->v3dWindow, which is always NULL in fullscreen (see
 * vid_OpenDisplay's own comment for why), so a caller wanting a window to
 * read input from would get nothing there.
 *
 * This deliberately does NOT touch context->v3dWindow itself -- only this
 * function's return value differs from it. v3dWindow stays NULL in
 * fullscreen, because it is load-bearing as the windowed-vs-fullscreen mode
 * flag, not just a handle: GLViewport (viewport.c) branches on it for the
 * Y-flip origin, MGLWriteShotPPM (others.c) for its width/height/RastPort
 * source, and MGLLockBack (this file) for whether a fullscreen
 * double-buffered context has a back-buffer address to hand out. Flipping
 * v3dWindow itself would silently break all three. */
void *MGLGetWindowHandle(GLcontext context)
{
	return context->inputWindow;
}

void *MGLGetInputWindowHandle(GLcontext context)
{
	return context->inputWindow;
}

void mglChooseGuardBand(GLboolean flag)
{
	newGuardBand = flag;
}


void mglChooseVertexBufferSize(int size)
{
	newVertexBufferSize = size;
}

void mglChooseMtexBufferSize(int size)
{
	int align = size % 4; /* bufferindex is 1/4 size */

	newMTBufferSize = size;

	if(align)
	{
	  	newMTBufferSize += 4-align;
	}
}

void mglChooseTextureBufferSize(int size)
{
	newTextureBufferSize = size;
}

void mglChooseNumberOfBuffers(int number)
{
	/* Takes effect at the next MGLCreateContext/MGLResizeContext -- see the
	 * newNumberOfBuffers declaration comment above. Has no effect unless
	 * built with MGLV3D_DOUBLE_BUFFER_ENABLED=1. */
	newNumberOfBuffers = number;
}

void mglChoosePixelDepth(int depth)
{
	newPixelDepth = depth;
}

/* Z-BUFFER precision, in bits: 32 = D32F (default), 16 = D16.
 *
 * Note the neighbour directly above: mglChoosePixelDepth sets the SCREEN's
 * bits-per-pixel. This one has nothing to do with that. "Depth" means screen
 * depth almost everywhere on AmigaOS (SA_Depth, CYBRMATTR_DEPTH, and this very
 * file's own vid_OpenDisplay code), and requesting SA_Depth=32 against a
 * 24-bit mode can make OpenScreenTags fail with OSERR_TOODEEP (see
 * newPixelDepth's own declaration comment) -- hence "ZBuffer" in the name
 * rather than a bare "Depth".
 *
 * Must be called BEFORE MGLCreateContext, like the rest of the mglChoose*
 * family: the Z buffer is allocated during context init and sized from this,
 * so it cannot change while a context lives. Calling it afterwards affects
 * only the NEXT context.
 *
 * Anything other than 16 is treated as 32, validated again in v3d_context_init
 * -- a bogus value must never yield an undersized Z buffer, since that would
 * be a GPU write past the allocation rather than a visual glitch.
 *
 * D16 is a genuine downgrade in depth precision, offered only for callers who
 * want the memory back. */
void mglChooseZBufferDepth(int bits)
{
	extern int g_v3d_requested_zbuffer_bits;

	g_v3d_requested_zbuffer_bits = (bits == 16) ? 16 : 32;
}

void mglProposeCloseDesktop(GLboolean closeme)
{
	clw = closeme;
}

void mglProhibitMipMapping(GLboolean flag)
{
	newNoMipMapping = flag;
}

void mglProhibitAlphaFallback(GLboolean flag)
{
	newNoFallbackAlpha = flag;
}

void mglChooseWindowMode(GLboolean flag)
{
	newWindowMode = flag;
}


void GLClearColor(GLcontext context, GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
	red *= 255.0;
	green *= 255.0;
	blue *= 255.0;
	alpha *= 255.0;

	context->ClearColor = ((GLubyte)(alpha)<<24)
						+ ((GLubyte)(red)<<16)
						+ ((GLubyte)(green)<<8)
						+ ((GLubyte)(blue));
}

void GLDepthMask(GLcontext context, GLboolean flag)
{
	/* No immediate hardware call -- see this file's header comment. */
	context->DepthMask = flag;
}

/*
 * Recompute the early-Z direction implied by the current depth state. Called
 * only from GLDepthFunc and MGLSetState(GL_DEPTH_TEST) -- the two places that
 * can change it -- so the per-draw early-Z accumulation in draw.c does not
 * have to derive it on every draw call; see depth_ez_dir's comment
 * (v3d_context.h).
 *
 * Mapping is MESA's, from v3dx_state.c: LESS/LEQUAL pick "nearer wins",
 * GREATER/GEQUAL pick "farther wins", NEVER/EQUAL are compatible with either
 * so they cost the frame nothing, and ALWAYS/NOTEQUAL cannot be expressed as
 * a direction at all. Depth test disabled contributes no opinion, matching
 * MESA leaving ez_state UNDECIDED when depth_enabled is false.
 */
static void v3d_update_depth_ez_dir(GLcontext context)
{
	V3DContext* backend = &context->backend;

	if (context->DepthTest_State != GL_TRUE)
	{
		backend->depth_ez_dir = V3D_EZ_UNDECIDED;
		return;
	}

	switch (backend->zmode)
	{
		case V3D_Z_LESS:
		case V3D_Z_LEQUAL:   backend->depth_ez_dir = V3D_EZ_LT_LE; break;
		case V3D_Z_GREATER:
		case V3D_Z_GEQUAL:   backend->depth_ez_dir = V3D_EZ_GT_GE; break;
		case V3D_Z_NEVER:
		case V3D_Z_EQUAL:    backend->depth_ez_dir = V3D_EZ_UNDECIDED; break;
		/* ALWAYS/NOTEQUAL -- this is the arm a scissored depth clear lands
		 * in, which is why no hand-set early-Z flag is needed for it. */
		default:             backend->depth_ez_dir = V3D_EZ_DISABLED; break;
	}
}

void GLDepthFunc(GLcontext context, GLenum func)
{
	v3d_u32 zmode = V3D_Z_LESS;

	switch(func)
	{
		case GL_NEVER:      zmode = V3D_Z_NEVER; break;
		case GL_LESS:       zmode = V3D_Z_LESS; break;
		case GL_EQUAL:      zmode = V3D_Z_EQUAL; break;
		case GL_LEQUAL:     zmode = V3D_Z_LEQUAL; break;
		case GL_GREATER:    zmode = V3D_Z_GREATER; break;
		case GL_NOTEQUAL:   zmode = V3D_Z_NOTEQUAL; break;
		case GL_GEQUAL:     zmode = V3D_Z_GEQUAL; break;
		case GL_ALWAYS:     zmode = V3D_Z_ALWAYS; break;
		default:
			/* Refused, not applied: returning here leaves the current depth
			 * function unchanged instead of falling through to the
			 * assignment below, which would switch the test to LESS. */
			GLFlagError(context, 1, GL_INVALID_ENUM);
			return;
	}
	context->backend.zmode = zmode;
	v3d_update_depth_ez_dir(context);
}


void GLClearDepth(GLcontext context, GLclampd depth)
{
	context->ClearDepth = (double)depth;
}

/*
 * Frame accumulation. V3D bins across a whole frame and then renders once;
 * there is no per-call immediate-mode rasterization the way Warp3D had, so
 * a frame is built in two pieces:
 *
 *  - gl_FrameBegin: emits just the BINNING PREAMBLE, and returns without
 *    doing anything when a frame is already active (apart from the
 *    render-pass split it handles; see its own comment). Cross-checked
 *    against MESA's real v3d driver (v3dx_draw.c's v3dX(start_binning),
 *    v3dx_job.c's bcl_epilogue's FLUSH) for exactly what job startup emits
 *    -- TILE_BINNING_MODE_CFG, FLUSH_VCD_CACHE, OCCLUSION_QUERY_COUNTER,
 *    START_TILE_BINNING, and nothing else (draw.c's own terminal functions
 *    emit the actual per-draw-call state and VertexArrayPrims/
 *    IndexedPrimList in between this and the eventual FLUSH). GLClear calls
 *    this to guarantee a frame exists and then records what it wants
 *    cleared; draw.c's terminal functions call it too, for the same reason.
 *  - gl_FramePresent (further down this file): the "flush and submit" step,
 *    called from MGLSwitchDisplay -- finalizes binning (FLUSH), builds the
 *    INDIRECT+RENDER lists (consuming the recorded clear values plus
 *    whatever draw calls have appended to the binning list since
 *    gl_FrameBegin), submits both, waits for the binning, and clears
 *    frame_active.
 *
 * gl_FrameBegin is not static: draw.c needs to call it too, declared there
 * via a plain extern (matching this project's existing convention for
 * cross-file GL-layer helpers, e.g. fog_Set).
 */
/*
 * MGLFlushPendingRender is separate from gl_FrameBegin, and public, because
 * of the deferred wait below: gl_FramePresent acquires the bitmap lock and
 * does not release it, so the lock outlives the frame and is released only
 * by the next call to this function. That is fine for a plain render loop
 * (nothing runs between one frame's gl_FramePresent and the next frame's
 * gl_FrameBegin except more GL calls), but a host loop that polls Intuition
 * messages between frames -- MGLMainLoop (others.c) calls GetMsg(), a real
 * exec.library call -- would do so while the lock is still held, violating
 * cybergraphics.library's rule against any library call while a bitmap is
 * locked (see vid_OpenDisplay's own comment for the citation). Calling this
 * first guarantees no lock is outstanding across such a call.
 */
/* Set by MGLFlushPendingRender below whenever the deferred render wait
 * fails (error or timeout) -- 0=success (default), 1=error_detected,
 * 2=timed_out, matching v3d_wait_result's own values. Exposed as a
 * plain int (not the enum) so consumers don't need to include this
 * backend's own headers just to read it. Sticky: never reset back to
 * 0 once set, since a real stall at this specific wait never recovers
 * on its own -- a host application should check this after every frame
 * and exit cleanly rather than soft-lock forever retrying the same
 * permanently-failed wait. */
int g_mglv3d_last_render_wait_result = 0;

/* Set TRUE when gl_FramePresent drops a frame_corrupted frame, cleared
 * (after logging) at the very next frame that actually reaches a real
 * v3d_submit_render -- lets that one log line show exactly what state
 * the reused build_slot/state_buf is in for the first real submit after
 * a drop, without flooding the log on every ordinary frame. */
static GLboolean s_mglv3d_frame_dropped_pending_compare = GL_FALSE;

/* Window mode only: mglSwitchDisplay's ClipBlit copies the window's
 * off-screen render target on the CPU right after the GPU wrote it. The exec
 * rule for a device writing memory: CachePreDMA before the write,
 * CachePostDMA after it -- without them the ClipBlit can copy stale 64-byte
 * CPU cache lines of the previous frame. gl_FramePresent does the Pre just before
 * submitting a window render and records the range here; MGLFlushPendingRender
 * does the Post after that render's wait, while the bitmap is still locked,
 * before the ClipBlit. Fullscreen never sets it: nothing on the CPU reads the
 * screen.
 *
 * OPT-IN, OFF BY DEFAULT: build with -DMGLV3D_WINDOW_CACHE_PAIR=1 to enable
 * it. The pair covers the whole window bitmap every frame, which costs real
 * frame rate. With the switch at 0 the Pre is never done, so s_window_dma_len
 * stays 0 and the Post never runs either. glReadPixels' own pair (MGLReadbackBegin) is separate and always
 * on. */
#ifndef MGLV3D_WINDOW_CACHE_PAIR
#define MGLV3D_WINDOW_CACHE_PAIR 0
#endif
static APTR  s_window_dma_addr = NULL;
static ULONG s_window_dma_len  = 0;

void MGLFlushPendingRender(GLcontext context)
{
	V3DContext* backend = &context->backend;

	/* Frame pipelining: gl_FrameBegin calls this before it resets/reuses
	 * build_slot's 4 CL buffers for a new frame -- and if the LAST frame that
	 * used this slot submitted its render without waiting for it (see
	 * gl_FramePresent's own comment on v3d_submit_render), that is the one
	 * point where the CPU is about to start overwriting bytes the GPU might
	 * still be reading. Wait
	 * (bounded, same timeout/OOM handling as every other wait in this
	 * codebase) for it to actually finish before touching this slot. While no
	 * render is outstanding, this whole function is a no-op.
	 *
	 * render_pending is a SINGLE flag, not per-CL-slot -- there's only one
	 * framebuffer and therefore only one lock ever outstanding, independent
	 * of which CL-buffer slot is active. See its own comment
	 * (v3d_context.h). */
	if (backend->render_pending)
	{
		v3d_wait_result wr;

		wr = v3d_wait_render_timeout(backend->render_lastframe);

		if (wr != v3d_wait_result_success)
		{
			E(("MGLFlushPendingRender: deferred render wait FAILED, result=%ld (0=success,1=error,2=timeout) -- proceeding anyway, a dropped/glitched frame beats a permanent stall\n",
			   (LONG)wr));
			/* Exposed so a host application can detect a genuinely
			 * unrecoverable stall and exit cleanly -- a real CTERR/timeout
			 * at THIS specific wait never resolves itself, nothing here
			 * actually resets the V3D control thread, so every subsequent
			 * frame fails the same way. Sticky (never cleared back to 0) --
			 * once true, stays true. */
			g_mglv3d_last_render_wait_result = (int)wr;
		}

		backend->render_pending = FALSE;

		/* The render is now confirmed done (or timed out) -- ONLY now is it
		 * safe to release the bitmap lock gl_FramePresent left held on our
		 * behalf. Acquisition happens in gl_FramePresent, right before it
		 * needs the bitmap address -- see that function's own comment. */
		if (s_window_dma_len)
		{
			/* The Post for a window render's Pre (see s_window_dma_addr):
			 * after the write, still inside the lock. */
			ULONG len = s_window_dma_len;
			CachePostDMA(s_window_dma_addr, &len, 0);
			s_window_dma_len = 0;
		}
		if (context->bitmapLock)
		{
			UnLockBitMap(context->bitmapLock);
			context->bitmapLock = NULL;
		}

		/* TEXTURE RENAMING: this is the one instant in the driver at which
		 * the GPU is provably finished with a submitted command list, so it
		 * is the only place parked texture memory may be released. A texture
		 * renamed mid-frame N is named by the CL submitted at the END of
		 * frame N, and that CL is retired by the wait above -- which runs at
		 * the top of gl_FrameBegin for frame N+1. See v3d_texture_rename
		 * (backend/hw/v3d_texture.c) for the mechanism.
		 *
		 * AFTER the UnLockBitMap, not beside render_pending = FALSE: the drain
		 * calls FreeVec, and doing that while still holding the bitmap lock
		 * would widen the window the whole frame-pipelining design exists to
		 * keep narrow.
		 *
		 * ONLY ON SUCCESS. A failed wait means the render may still be running
		 * -- g_mglv3d_last_render_wait_result is sticky precisely because that
		 * state never resolves itself -- so freeing memory it could still be
		 * reading would turn a glitched frame into a corrupted heap. Parked
		 * blocks simply stay parked; they are released at teardown. */
		if (wr == v3d_wait_result_success)
		{
			v3d_texture_drain_parked(&context->device);
			/* Same rule for spill blocks and replaced state_buf blocks:
			 * released only after a successful wait for the job that uses
			 * them -- see v3d_frame.h. */
			v3d_frame_retire(&context->device, &backend->frame);
		}
	}
}

static v3d_wait_result gl_FramePresent(GLcontext context); /* forward decl -- gl_FrameBegin's force_new_pass branch below calls this directly, defined further down this file */

void gl_FrameBegin(GLcontext context)
{
	V3DContext* backend = &context->backend;
	GLboolean continues_split = GL_FALSE;

	if (backend->frame_active)
	{
		if (!backend->force_new_pass)
			return;

		/* A render-pass split was requested: force_new_pass is set by
		 * GLClear when a full-drawable clear arrives mid-pass with geometry
		 * already binned, by MGLReadbackBegin, and by texture.c's
		 * tex_SyncBeforeModify. Finalize the in-progress pass for real right
		 * now instead of silently no-op'ing (the normal behavior above), so
		 * what follows gets a genuine fresh binning pass with its own real
		 * depth clear. See force_new_pass's own comment (v3d_context.h) for
		 * the full picture, including why the finalized pass stores into
		 * scratch_color_mem instead of the real screen. */
		backend->force_new_pass = FALSE;

		if (v3d_backend_alloc_scratch_color(&context->device, backend) < 0)
		{
			/* Allocation failed (out of memory) -- degrade gracefully:
			 * skip the intermediate finalize entirely, since the pass would
			 * have nowhere to store. The pass carries on unsplit for this one
			 * frame instead of crashing or hanging, and a readback split
			 * reads the render target without this frame's unrendered draws
			 * (MGLReadbackBegin). */
			return;
		}

		backend->doing_intermediate_pass = TRUE;
		gl_FramePresent(context);
		backend->doing_intermediate_pass = FALSE;
		continues_split = GL_TRUE;
		/* fall through to the normal reset below -- starts the resuming
		 * view's own fresh pass exactly as if frame_active had been FALSE
		 * to begin with. */
	}

	MGLFlushPendingRender(context);

	/* No bitmap lock is needed here: everything below (the CL buffer resets,
	 * then the binning preamble -- TileBinningModeCfg/FLUSH_VCD_CACHE/
	 * OcclusionQueryCounter/START_TILE_BINNING) never reads context->
	 * vmembase/bprow, only backend->width/height and this driver's own
	 * CPU-side CL buffers. The lock only becomes load-bearing once something
	 * actually consumes vmembase/bprow, which first happens in
	 * gl_FramePresent's INDIRECT list (LoadTileBufferGeneral/
	 * StoreTileBufferGeneral) -- see that function's own comment for the
	 * acquire point. The release side is MGLFlushPendingRender -- the call
	 * just above, or whichever other one runs first -- once that frame's
	 * render is confirmed done by the GPU. */

	v3d_cl_reset(&backend->tile_list_mem[backend->build_slot], &backend->tile_list_buf[backend->build_slot]);
	v3d_cl_reset(&backend->binning_mem[backend->build_slot], &backend->binning_buf[backend->build_slot]);
	v3d_cl_reset(&backend->render_mem[backend->build_slot], &backend->render_buf[backend->build_slot]);
	v3d_cl_reset(&backend->state_mem[backend->build_slot], &backend->state_buf[backend->build_slot]);

	/* The CLs this slot held are gone as of the four resets above, so every
	 * cache keyed on g_v3d_cl_generation (texture state, the MESA-style packet
	 * dedup in draw.c) must see a new value BEFORE the first draw of this pass.
	 * gl_FramePresent bumps it at its build_slot flip too; bumping
	 * here as well makes "new CL => new generation" hold by construction,
	 * including for a dropped frame that reuses its slot without a flip.
	 * Monotonic, so an extra increment can never alias a cached value. */
	{ extern v3d_u32 g_v3d_cl_generation; g_v3d_cl_generation++; }

	backend->current_buf = &backend->binning_buf[backend->build_slot];
	SetBuffer(backend, &backend->binning_buf[backend->build_slot]);

	/* MESA-exact: NUMBER_OF_LAYERS=1 before the binning mode
	 * configuration, as MESA's v3d_start_binning emits for every job
	 * (v3dx_draw.c:58-65). */
	NumberOfLayers(backend, 1);
	TileBinningModeCfg(backend, v3d_TILE_ALLOCATION_INITIAL_BLOCK_SIZE_64B, v3d_TILE_ALLOCATION_BLOCK_SIZE_64B,
	                    1, V3D_INTERNAL_BPP_32, FALSE, FALSE, backend->width, backend->height);
	DoCommand(backend, v3d_OP_FLUSH_VCD_CACHE);
	OcclusionQueryCounter(backend, 0);
	DoCommand(backend, v3d_OP_START_TILE_BINNING);

	backend->frame_active = TRUE;
	/* Early-Z starts UNDECIDED every frame and is accumulated by each draw
	 * (gl_EmitPrimitiveV3D) -- see ez_state's comment (v3d_context.h). */
	backend->ez_state = V3D_EZ_UNDECIDED;
	backend->first_ez_state = V3D_EZ_UNDECIDED;
	backend->pending_clear_color = FALSE;
	backend->pending_clear_depth = FALSE;
	backend->frame_corrupted = FALSE;
	backend->draw_state_configured = FALSE;
	/* See PassContinuesSplit (context.h) and the depth load/store in
	 * gl_FramePresent. */
	context->PassContinuesSplit = continues_split;
}

static v3d_wait_result gl_FramePresent(GLcontext context)
{
	V3DContext* backend = &context->backend;
	UWORD width = backend->width;
	UWORD height = backend->height;
	ULONG tilesX = backend->frame.tilesX, tilesY = backend->frame.tilesY;
	ULONG startibuffer, endibuffer;
	ULONG supertile_w, supertile_h, frame_w_supertile, frame_h_supertile;
	ULONG tile_alloc_off;
	ULONG max_supertile_x, max_supertile_y;
	ULONG sx, sy;
	const ULONG tile_width = 64, tile_height = 64;
	const ULONG max_supertiles = 256;
	UBYTE clearColor = backend->pending_clear_color;
	UBYTE clearDepth = backend->pending_clear_depth;
	v3d_wait_result wr;
	UBYTE early_zs_clear;

	/* MESA's job->early_zs_clear (v3dx_rcl.c:751-755): depth is cleared,
	 * not loaded and not stored, so TILE_RENDERING_MODE_CFG_COMMON's
	 * early_depth_stencil_clear does the clearing and the CLEAR_TILE_BUFFERS
	 * packets leave their Z/S bit clear. Here depth is loaded iff it is not
	 * cleared, and stored iff (intermediate pass || depth_persist_seen ||
	 * (not cleared && not continuing a split)) -- the condition the generic
	 * list below uses for the STORE -- so with depth cleared this reduces to
	 * the three terms. */
	early_zs_clear = (clearDepth &&
	                  !backend->doing_intermediate_pass && !backend->depth_persist_seen) ? TRUE : FALSE;

	/* A pending scratch composite takes precedence over a later clear
	 * request. pending_clear_color is a SINGLE flag for the entire
	 * accumulated frame, set by any GLClear in it and reset only by
	 * gl_FrameBegin -- it isn't scoped to just the
	 * view that's about to present, so a GLClear(GL_COLOR_BUFFER_BIT)
	 * issued by any later view in the same accumulated pass flips it to
	 * TRUE for the frame's real, final gl_FramePresent. That would skip
	 * the color Load entirely (see the `if (!clearColor)` block below) and
	 * clear instead, throwing away the intermediate pass's output before it
	 * can be composited. Force clearColor FALSE (real Load, no hardware
	 * clear-color command) whenever has_scratch_color is true, regardless
	 * of what pending_clear_color ended up holding by the time this frame
	 * actually presents. */
	if (backend->has_scratch_color)
		clearColor = FALSE;
	extern int g_mglv3d_frame_number;

	{
		extern int g_mglv3d_frame_prim_calls, g_mglv3d_frame_prim_verts, g_mglv3d_frame_prim_maxcount;
		extern UBYTE g_mglv3d_frame_prim_types_seen;
		g_mglv3d_frame_prim_calls = 0;
		g_mglv3d_frame_prim_verts = 0;
		g_mglv3d_frame_prim_maxcount = 0;
		g_mglv3d_frame_prim_types_seen = 0;
		g_mglv3d_frame_number++;
	}

	/* TEXTURE RENAMING: everything parked so far is about to be
	 * named by the command list this present submits, so it becomes eligible
	 * for release at the NEXT successful render wait -- never before.
	 *
	 * Marked at the TOP, deliberately, rather than beside the submit. If this
	 * present goes on to drop the frame (frame_corrupted, a LockBitMapTags
	 * failure, CL overflow) no CL is submitted at all, so those blocks are
	 * named by nothing and are equally safe to release at the next wait.
	 * Marking only at the submit would strand them until teardown on exactly
	 * the paths where memory is already under stress. */
	v3d_texture_parked_mark_submitted();

	if (!backend->frame_active)
		return v3d_wait_result_success; /* nothing accumulated -- e.g. mglSwitchDisplay called twice in a row */

	/* A draw call earlier this frame (gl_EmitPrimitiveV3D, draw.c) found its
	 * own fragment-uniform stream address had gone stale mid-build (a
	 * state_buf regrow split it across two physical blocks) and flagged the
	 * whole frame rather than submit a shader state record that would feed a
	 * garbage TMU texture-fetch address to the binner: that costs a permanent
	 * binning-flush-count stall, not just a wrong-colour glitch -- see
	 * frame_corrupted's own comment (v3d_context.h). Checked here, before
	 * lock acquisition even happens -- a frame that's not going to be
	 * submitted needs neither the lock nor any of the finalize/submit work
	 * below. */
	if (backend->frame_corrupted)
	{
		E(("gl_FramePresent: frame %lu -- a draw call's fragment uniform stream went stale mid-build (state_buf regrow race) -- dropping frame, not submitting\n",
		   (ULONG)g_mglv3d_frame_number));
		{
			D(("  DROP DIAG: build_slot=%d state_buf[slot].capacity=%ld state_buf[slot].used=%ld state_mem[slot].size=%ld\n",
				(int)backend->build_slot, (LONG)backend->state_buf[backend->build_slot].capacity,
				(LONG)backend->state_buf[backend->build_slot].used, (LONG)backend->state_mem[backend->build_slot].size));
		}
		s_mglv3d_frame_dropped_pending_compare = GL_TRUE;
		backend->frame_active = FALSE;

		/* Discard any pending scratch composite when a frame is dropped.
		 *
		 * has_scratch_color is cleared by the next color LOAD step (see its
		 * consumption site's own comment further down), and that step lives
		 * BELOW this early return -- so without this, a dropped frame would
		 * leave the flag set and the next frame would load an earlier
		 * frame's intermediate-pass output out of scratch_color_mem instead
		 * of reading the render target.
		 *
		 * Discarding rather than carrying it forward is the correct choice:
		 * the scratch holds output belonging to a frame that no longer
		 * exists, so compositing it into a later, unrelated frame is exactly
		 * the staleness the flag exists to prevent.
		 *
		 * The sibling early return above (!frame_active) deliberately does
		 * NOT do this -- there no pass exists to composite into yet, so the
		 * scratch content is still legitimately pending for whichever real
		 * pass comes next. */
		backend->has_scratch_color = FALSE;
		g_mglv3d_frames_dropped++;

		return v3d_wait_result_error_detected;
	}

	/* The bitmap lock is acquired HERE, right before anything in this
	 * function needs context->vmembase/bprow (binning never touches the
	 * bitmap address at all -- see gl_FrameBegin's own comment). The
	 * release is deferred to a later MGLFlushPendingRender call, once this
	 * frame's render is confirmed done by the GPU:
	 * gl_FramePresent submits the render asynchronously and returns without
	 * waiting (see v3d_submit_render below), so it cannot be the one to
	 * unlock. At most one lock may be outstanding at a time -- hence the
	 * stale-lock stopgap below.
	 *
	 * An intermediate (split) pass STORES into scratch_color_mem, never the
	 * real screen (see the color Load/Store calls below), so it usually
	 * needs no AmigaOS bitmap lock. Except when it LOADS from the screen: a
	 * frame that did not clear colour and has no scratch composite yet
	 * starts from the screen's content. That load uses
	 * context->vmembase/bprow, which are only valid inside a lock. The lock
	 * taken here is released by the MGLFlushPendingRender that gl_FrameBegin
	 * runs right after this pass. */
	if (!backend->doing_intermediate_pass || (!clearColor && !backend->has_scratch_color))
	{
		static int s_stale_lock_count = 0;
		if (context->bitmapLock)
		{
			if (s_stale_lock_count < 5)
			{
				D(("STALE LOCK STOPGAP: bitmapLock=%lx released early at gl_FramePresent's fresh-acquire point (count=%d)\n",
					(ULONG)context->bitmapLock, s_stale_lock_count));
				s_stale_lock_count++;
			}
			UnLockBitMap(context->bitmapLock);
			context->bitmapLock = NULL;
		}
		/* Keyed on v3dBitMap, NOT v3dWindow.
		 *
		 * v3dBitMap is THE off-screen-render-target invariant: set for windowed
		 * mode, for an adopted window, and for an adopted BITMAP; NULL only for
		 * fullscreen. MGLSwitchDisplay uses exactly that test
		 * (`if (!context->v3dBitMap)` means fullscreen), and the two must
		 * agree. A context can have a render bitmap and NO window
		 * (MGLCreateContextFromBitMap); keyed on v3dWindow, such a context
		 * would fall through to the final else and dereference
		 * context->v3dScreen, which is also NULL there, so every lock -- and
		 * so every frame -- would fail. */
		if (context->v3dBitMap)
		{
			context->bitmapLock = LockBitMapTags(context->v3dBitMap,
				LBMI_BYTESPERROW, (ULONG)&context->bprow,
				LBMI_BASEADDRESS, (ULONG)&context->vmembase,
				TAG_DONE);
		}
		else if (context->NumBuffers >= 2)
		{
			/* Double buffering: render into the current back buffer, never
			 * into the screen's RastPort bitmap (which may be the one on
			 * display). */
			context->bitmapLock = LockBitMapTags(context->Buffers[context->BufNr]->sb_BitMap,
				LBMI_BYTESPERROW, (ULONG)&context->bprow,
				LBMI_BASEADDRESS, (ULONG)&context->vmembase,
				TAG_DONE);
		}
		else
		{
			context->bitmapLock = LockBitMapTags(context->v3dScreen->RastPort.BitMap,
				LBMI_BYTESPERROW, (ULONG)&context->bprow,
				LBMI_BASEADDRESS, (ULONG)&context->vmembase,
				TAG_DONE);
		}
		if (!context->bitmapLock)
		{
			/* A dropped frame beats a crash or a hang -- nothing has been
			 * submitted to the GPU yet (binning is CPU-side only so far), so
			 * just drop this frame's accumulated work and let the caller try
			 * again next frame. */
			D(("gl_FramePresent: LockBitMapTags failed\n"));
			backend->frame_active = FALSE;
			/* Same discard as the frame_corrupted drop above -- this return
			 * also sits before the color LOAD step that would consume it, so
			 * leaving the flag set would hand an earlier frame's
			 * intermediate-pass output to a later, unrelated one. See that
			 * site for the full reasoning. */
			backend->has_scratch_color = FALSE;
			g_mglv3d_frames_dropped++;
			return v3d_wait_result_error_detected;
		}
	}

	/* Finalize binning -- draw.c's terminal functions have already
	 * appended whatever glShaderState/VertexArrayPrims calls this frame
	 * needed into backend->binning_buf since gl_FrameBegin. */
	backend->current_buf = &backend->binning_buf[backend->build_slot];
	SetBuffer(backend, &backend->binning_buf[backend->build_slot]);
	DoCommand(backend, v3d_OP_FLUSH);

	/* ================= INDIRECT ================= */
	backend->current_buf = &backend->tile_list_buf[backend->build_slot];
	SetBuffer(backend, &backend->tile_list_buf[backend->build_slot]);

	startibuffer = CurrentBufferAddress(backend);

	DoCommand(backend, v3d_OP_TILE_COORDINATES_IMPLICIT);

	/* When the app skips glClear() for a given buffer, V3D semantics require
	 * LOADING that buffer's existing content from system memory here
	 * (between TILE_COORDINATES_IMPLICIT and END_OF_LOADS) -- see
	 * LoadTileBufferGeneral's own comment. Emit nothing and the on-chip tile
	 * buffer keeps whatever was left over from rendering the PREVIOUS tile
	 * in this same sweep, not this tile's real prior framebuffer content.
	 * Parameters mirror each buffer's own StoreTileBufferGeneral call just
	 * below exactly (same memory_format/image_format/r_b_swap/stride/address
	 * -- load and store describe the SAME physical layout, just opposite
	 * transfer directions). Only load a buffer that ISN'T about to be
	 * cleared -- loading and clearing the same buffer in one tile pass is
	 * redundant/conflicting, matching MESA's own mutually-exclusive
	 * load/clear bit tracking. */
	if (!clearColor)
	{
		/* has_scratch_color means an earlier intermediate pass this frame
		 * stored its color output into scratch_color_mem instead of the real
		 * screen -- load from there instead of context->vmembase, so this
		 * pass's own opaque geometry composites over that output via
		 * ordinary depth testing. Consumed here (cleared right after) so a
		 * later, unrelated ordinary frame never mistakenly loads stale
		 * scratch content. Independent of doing_intermediate_pass: a SECOND
		 * intermediate pass needs to chain off the first one's output
		 * exactly the same way the final pass does. */
		if (backend->has_scratch_color)
		{
			LoadTileBufferGeneral(backend, v3d_RENDER_TARGET_0, V3D_MEMORY_FORMAT_RASTER, FALSE,
			                       V3D_DECIMATE_MODE_SAMPLE_0, V3D_OUTPUT_IMAGE_FORMAT_RGBA8,
			                       FALSE, FALSE, TRUE, backend->scratch_stride, 0, (ULONG)backend->scratch_color_mem.hostptr);
			backend->has_scratch_color = FALSE;
		}
		else
		{
			LoadTileBufferGeneral(backend, v3d_RENDER_TARGET_0, V3D_MEMORY_FORMAT_RASTER, FALSE,
			                       V3D_DECIMATE_MODE_SAMPLE_0, V3D_OUTPUT_IMAGE_FORMAT_RGBA8,
			                       FALSE, FALSE, TRUE, context->bprow, 0, context->vmembase);
		}
	}
	if (!clearDepth)
	{
		/* This frame is reading depth it did not clear, so the app relies on
		 * the buffer persisting across frames -- store it from now on, for
		 * good. See depth_persist_seen's own comment (v3d_context.h). Set
		 * BEFORE the store below is emitted, so this very frame stores and
		 * only the one frame that got here first can be wrong.
		 *
		 * Not for a pass that continues a split frame: it loads the depth
		 * its own frame's intermediate pass just stored, which says nothing
		 * about depth persisting across frames (PassContinuesSplit,
		 * context.h). */
		if (!context->PassContinuesSplit)
			backend->depth_persist_seen = TRUE;

		LoadTileBufferGeneral(backend, v3d_Z, V3D_MEMORY_FORMAT_UIF_XOR, FALSE,
		                       V3D_DECIMATE_MODE_SAMPLE_0,
		                       (backend->zbuffer_bits == 16)
		                           ? V3D_OUTPUT_IMAGE_FORMAT_D16
		                           : V3D_OUTPUT_IMAGE_FORMAT_D32F,
		                       FALSE, FALSE, FALSE, (((height + 7) & ~7) / (2 * 4)), 0, (ULONG)backend->zbuffer_mem.hostptr);
	}

	DoCommand(backend, v3d_OP_END_OF_LOADS);
	PrimListFormat(backend, v3d_LIST_TRIANGLES, FALSE);
	SetInstanceid(backend, 0);
	BranchToImplicitTileList(backend, 0);

	/* An intermediate pass stores into scratch_color_mem instead of the
	 * real, already-visible screen -- see force_new_pass's comment
	 * (v3d_context.h) for why (in single-buffer mode there is no back buffer
	 * to hide it behind, and storing an early-finalized pass straight into
	 * the visible screen flickers). The final pass
	 * (doing_intermediate_pass FALSE) always stores to the real screen. */
	if (backend->doing_intermediate_pass)
	{
		StoreTileBufferGeneral(backend, v3d_RENDER_TARGET_0, V3D_MEMORY_FORMAT_RASTER, FALSE,
		                        V3D_DITHER_MODE_NONE, V3D_DECIMATE_MODE_SAMPLE_0, V3D_OUTPUT_IMAGE_FORMAT_RGBA8,
		                        FALSE, FALSE, TRUE, backend->scratch_stride, 0, (ULONG)backend->scratch_color_mem.hostptr);
		/* has_scratch_color is NOT set here -- the CL buffer overflow check
		 * further down can still drop this whole frame before it's ever
		 * actually submitted to the GPU, in which case scratch_color_mem
		 * never really gets written. Set only once we know this pass is
		 * actually reaching SUBMIT, right alongside render_pending. */
	}
	else
	{
		/* GL_DITHER: base MiniGL passed it to Warp3D (W3D_DITHERING, on at
		 * context creation, as GL's default is). V3D dithers at store time,
		 * so only this store -- the finished frame -- follows the flag, as it
		 * stands when the frame is presented (one value per frame, like
		 * POINT_SIZE and LINE_WIDTH per pass). The scratch store above and a
		 * readback's own pass stay undithered: a split frame is stored here
		 * again at its end, and a read must return what the frame holds. The
		 * output is RGBA8, the same precision the tile buffer renders at, so
		 * the hardware has no finer value to dither from. */
		StoreTileBufferGeneral(backend, v3d_RENDER_TARGET_0, V3D_MEMORY_FORMAT_RASTER, FALSE,
		                        context->Dither_State ? V3D_DITHER_MODE_RGBA : V3D_DITHER_MODE_NONE,
		                        V3D_DECIMATE_MODE_SAMPLE_0, V3D_OUTPUT_IMAGE_FORMAT_RGBA8,
		                        FALSE, FALSE, TRUE, context->bprow, 0, context->vmembase);
	}

	/* Skip the depth store when nothing can read it back. It exists only to
	 * serve a later frame that loads depth, so it is redundant when this
	 * frame cleared depth AND no frame has ever loaded it. See
	 * depth_persist_seen's comment (v3d_context.h) for why this cannot be
	 * made perfectly safe and what the one-frame cost is.
	 *
	 * The INTERMEDIATE pass always stores: a split frame's second pass can
	 * load the depth its first pass wrote, so that one is load-bearing within
	 * the frame no matter what the flags say. The pass that CONTINUES a split
	 * loads depth without that meaning a later frame will, so its load alone
	 * does not force a store (PassContinuesSplit, context.h). */
	if (backend->doing_intermediate_pass || backend->depth_persist_seen ||
	    (!clearDepth && !context->PassContinuesSplit))
	{
		StoreTileBufferGeneral(backend, v3d_Z, V3D_MEMORY_FORMAT_UIF_XOR, FALSE,
		                        V3D_DITHER_MODE_NONE, V3D_DECIMATE_MODE_SAMPLE_0,
		                        (backend->zbuffer_bits == 16)
		                            ? V3D_OUTPUT_IMAGE_FORMAT_D16
		                            : V3D_OUTPUT_IMAGE_FORMAT_D32F,
		                        FALSE, FALSE, FALSE, (((height + 7) & ~7) / (2 * 4)), 0, (ULONG)backend->zbuffer_mem.hostptr);
	}

	/* MESA-exact, v3dx_rcl.c:325-336: "GFXH-1461/GFXH-1689:
	 * The per-buffer store command's clear buffer bit is broken for
	 * depth/stencil. In addition, the clear packet's Z/S bit is broken, but
	 * the RTs bit ends up clearing Z/S." So MESA on 4.2 emits the packet
	 * only when something is cleared, always with clear_all_render_targets
	 * set, and with the Z/S bit = !early_zs_clear. Colour is LOADed at the
	 * start of every tile whenever it is not cleared (above), and this
	 * packet sits after the STORE, so clearing the tile buffer here is
	 * invisible. The obvious (z=clearDepth, rt=clearColor) form is the exact
	 * path MESA avoids. */
	if (clearDepth || clearColor)
		ClearTileBuffers(backend, early_zs_clear ? FALSE : TRUE, TRUE);
	DoCommand(backend, v3d_OP_END_OF_TILE_MARKER);
	DoCommand(backend, v3d_OP_RETURN_FROM_SUB_LIST);

	endibuffer = CurrentBufferAddress(backend);

	/* ================= RENDER ================= */
	backend->current_buf = &backend->render_buf[backend->build_slot];
	SetBuffer(backend, &backend->render_buf[backend->build_slot]);

	/* Early-Z direction and disable, both derived per frame -- MESA's own
	 * switch on first_ez_state (v3dx_rcl.c). UNDECIDED maps to
	 * LT_LE-and-enabled exactly as MESA does, since a frame where nothing
	 * picked a direction is free to use either. See ez_state's comment
	 * (v3d_context.h).
	 *
	 * ERRATUM GFXH-1918, from MESA's v3d_update_job_ez: the early-Z buffer
	 * can load incorrect depth values when the frame has an ODD width or
	 * height. Kept, and still reachable, since a windowed mode can be any
	 * size. MESA's other half of the same erratum -- a 16-bit MULTISAMPLED
	 * depth buffer -- does not apply here: this driver does not
	 * multisample. */
	{
		UBYTE ez = backend->first_ez_state;
		UBYTE ez_dir = (ez == V3D_EZ_GT_GE) ? v3d_EARLY_Z_DIRECTION_GT_GE
		                                    : v3d_EARLY_Z_DIRECTION_LT_LE;
		UBYTE ez_off = (ez == V3D_EZ_DISABLED) ? TRUE : FALSE;

		if ((width & 1) || (height & 1))
			ez_off = TRUE;

		/* Z-buffer precision is per-context (mglChooseZBufferDepth, latched at
		 * context init because zbuffer_mem is sized from it). All three CL sites that name the
		 * depth format -- this one and the Load/Store pair in the generic
		 * tile list -- must agree, or the hardware reads the buffer back in
		 * a different format than it wrote. */
		/* early_depth_stencil_clear = MESA's job->early_zs_clear
		 * (v3dx_rcl.c:751-755): depth cleared, not loaded, not stored --
		 * the hardware clears Z/S per tile itself and the clear packets'
		 * Z/S bit is left clear. Computed once in gl_FramePresent above. */
		TileRenderingModeCFGCommon(backend, 1, width, height, v3d_RENDER_TARGET_MAXIMUM_32BPP,
		                            FALSE, FALSE, ez_dir, ez_off,
		                            (backend->zbuffer_bits == 16)
		                                ? V3D_INTERNAL_TYPE_DEPTH16
		                                : V3D_INTERNAL_TYPE_DEPTH32F, early_zs_clear);
	}
	/* context->ClearColor is packed 0xAARRGGBB (GLClearColor, as in the
	 * original MiniGL -- Warp3D's own convention).
	 * v3d_commands.c's TileRenderingModeCFGClearColorsPart1 wants ABGR
	 * (its own "//abgr" comment on clear_color_low_32_bits), so swap R/B
	 * here, at the point of use, rather than changing GLClearColor's own
	 * packing.
	 *
	 * Read backend->clear_color_value/clear_depth_value (snapshotted by
	 * GLClear() at call time), NOT context->ClearColor/ClearDepth live --
	 * an app may call glClearColor()/glClearDepth() again between its
	 * glClear() and the present that finally consumes it, and the clear
	 * must use the value that was current at the glClear(). */
	{
		ULONG argb = backend->clear_color_value;
		ULONG abgr = (argb & 0xFF00FF00UL) | ((argb & 0x00FF0000UL) >> 16) | ((argb & 0x000000FFUL) << 16);
		TileRenderingModeCFGClearColorsPart1(backend, 0, abgr);
	}
	TileRenderingModeCFGColor(backend, V3D_INTERNAL_BPP_32, V3D_INTERNAL_TYPE_8, 0);
	TileRenderingModeCFGZSClearValues(backend, 0, backend->clear_depth_value);
	TileListInitialBlockSize(backend, v3d_TILE_ALLOCATION_BLOCK_SIZE_64B, TRUE);

	/* From PoC/v3d_cle.c:76-78 -- supertile grid sizing. */
	supertile_w = 1;
	supertile_h = 1;
	for (;;)
	{
		frame_w_supertile = (tilesX + supertile_w - 1) / supertile_w;
		frame_h_supertile = (tilesY + supertile_h - 1) / supertile_h;

		if (frame_w_supertile * frame_h_supertile < max_supertiles)
			break;

		if (supertile_w < supertile_h)
			supertile_w++;
		else
			supertile_h++;
	}

	tile_alloc_off = 0;

	Multicore_Rendering_Tile_List_Set_Base(backend, ((ULONG)backend->tile_alloc_mem.hostptr + tile_alloc_off), 0);
	MulticoreRenderingSupertileCFG(backend, supertile_w, supertile_h, frame_w_supertile,
	                                frame_h_supertile, 1, FALSE, FALSE, tilesY, tilesX);

	TileCoordinates(backend, 0, 0);
	DoCommand(backend, v3d_OP_END_OF_LOADS);
	StoreTileBufferGeneral(backend, v3d_NONE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	/* MESA's initial dummy-tile clear (v3dx_rcl.c:645-650): always emitted,
	 * RT bit set, Z/S bit = !early_zs_clear. See the generic-list site. */
	ClearTileBuffers(backend, early_zs_clear ? FALSE : TRUE, TRUE);
	DoCommand(backend, v3d_OP_END_OF_TILE_MARKER);

	TileCoordinates(backend, 0, 0);
	DoCommand(backend, v3d_OP_END_OF_LOADS);
	StoreTileBufferGeneral(backend, v3d_NONE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	DoCommand(backend, v3d_OP_END_OF_TILE_MARKER);
	DoCommand(backend, v3d_OP_FLUSH_VCD_CACHE);

	StartAddressOfGenericTileList(backend, startibuffer, endibuffer);

	max_supertile_x = (width - 1) / (tile_width * supertile_w);
	max_supertile_y = (height - 1) / (tile_height * supertile_h);

	for (sy = 0; sy <= max_supertile_y; sy++)
	{
		for (sx = 0; sx <= max_supertile_x; sx++)
		{
			Supertile_Coordinates(backend, sx, sy);
		}
	}

	DoCommand(backend, v3d_OP_END_OF_RENDERING);


	/* Any of the four per-frame CL buffers overflowing (binning_buf/
	 * state_buf during this frame's own draw calls, or render_buf/
	 * tile_list_buf during the INDIRECT/RENDER list building just above)
	 * means this frame's control list is truncated/corrupt --
	 * v3d_buffer_claim_memory keeps every unconditional field write
	 * memory-safe (scratch fallback instead of NULL), but submitting a
	 * truncated CL to the GPU is still unsafe. Drop the whole frame here
	 * instead -- a dropped/glitched frame is far better than a hard hang
	 * requiring a power cycle. */
	if (backend->binning_buf[backend->build_slot].overflowed || backend->state_buf[backend->build_slot].overflowed ||
	    backend->render_buf[backend->build_slot].overflowed || backend->tile_list_buf[backend->build_slot].overflowed)
	{
		E(("gl_FramePresent: a CL buffer overflowed this frame (binning=%ld state=%ld render=%ld tile_list=%ld) -- dropping frame, not submitting\n",
		   (LONG)backend->binning_buf[backend->build_slot].overflowed, (LONG)backend->state_buf[backend->build_slot].overflowed,
		   (LONG)backend->render_buf[backend->build_slot].overflowed, (LONG)backend->tile_list_buf[backend->build_slot].overflowed));
		/* This early return never reaches v3d_submit_render below, so
		 * render_pending never gets set TRUE and the deferred release in a
		 * later gl_FrameBegin's MGLFlushPendingRender never fires either --
		 * the lock would leak. Every exit that does not hand ownership off
		 * via render_pending releases what this function acquired. */
		if (context->bitmapLock)
		{
			UnLockBitMap(context->bitmapLock);
			context->bitmapLock = NULL;
		}
		backend->frame_active = FALSE;
		g_mglv3d_frames_dropped++;
		return v3d_wait_result_error_detected;
	}

	/* ================= SUBMIT ================= */
	D(("gl_FramePresent: about to call v3d_context_flush_for_dma\n"));
	v3d_context_flush_for_dma(&context->device, backend);
	D(("gl_FramePresent: flush_for_dma returned, about to call v3d_invalidate_caches (1st)\n"));
	v3d_invalidate_caches();


	D(("gl_FramePresent: invalidate_caches (1st) returned, about to call v3d_submit_binning\n"));


	/* Frame pipelining, split submit+wait: this frame's own render (below)
	 * can't start until ITS binning is done (inherent V3D data dependency,
	 * render consumes binning's tile lists), so this wait is unavoidable. */
	{
		v3d_u8 lastFlush = v3d_submit_binning(&context->device, &backend->frame,
		                          (v3d_address)(ULONG)backend->binning_buf[backend->build_slot].start,
		                          (v3d_address)((ULONG)backend->binning_buf[backend->build_slot].start + backend->binning_buf[backend->build_slot].used),
		                          (v3d_address)(ULONG)backend->tile_alloc_mem.hostptr,
		                          (v3d_u32)backend->tile_alloc_mem.size,
		                          (v3d_address)(ULONG)backend->tile_state_mem.hostptr);
		wr = v3d_wait_binning_timeout(&context->device, &backend->frame, lastFlush);
	}

	D(("gl_FramePresent: v3d_wait_binning_timeout RETURNED, result=%ld (0=success, 1=error_detected, 2=timed_out)\n", (LONG)wr));

	if (wr != v3d_wait_result_success)
	{
		D(("gl_FramePresent: frame %lu -- binning wait FAILED, result=%ld (0=success,1=error,2=timeout)\n", (ULONG)g_mglv3d_frame_number, (LONG)wr));
		D(("gl_FramePresent: binning wait failed, result=%ld\n", (LONG)wr));
		/* Same as the CL-buffer-overflow check above -- this path never
		 * reaches v3d_submit_render either, so nothing else would ever
		 * release this frame's lock. */
		if (context->bitmapLock)
		{
			UnLockBitMap(context->bitmapLock);
			context->bitmapLock = NULL;
		}
		backend->frame_active = FALSE;
		g_mglv3d_frames_dropped++;
		return wr;
	}


	D(("gl_FramePresent: about to call v3d_invalidate_caches (2nd)\n"));
	v3d_invalidate_caches();
	D(("gl_FramePresent: invalidate_caches (2nd) returned, about to call v3d_submit_render\n"));

	/* This is the first real, non-dropped submit after a frame_corrupted
	 * drop -- log the same build_slot/state_buf identity here, so the line
	 * is directly comparable against the DROP DIAG line logged when that
	 * earlier frame was dropped (same slot, if build_slot never flipped in
	 * between). */
	if (s_mglv3d_frame_dropped_pending_compare)
	{
		D(("  NEXT SUBMIT DIAG: build_slot=%d state_buf[slot].capacity=%ld state_buf[slot].used=%ld state_mem[slot].size=%ld render_buf[slot].used=%ld binning_buf[slot].used=%ld\n",
			(int)backend->build_slot, (LONG)backend->state_buf[backend->build_slot].capacity,
			(LONG)backend->state_buf[backend->build_slot].used, (LONG)backend->state_mem[backend->build_slot].size,
			(LONG)backend->render_buf[backend->build_slot].used, (LONG)backend->binning_buf[backend->build_slot].used));
		s_mglv3d_frame_dropped_pending_compare = GL_FALSE;
	}

	/* Frame pipelining: submit the render WITHOUT waiting for it -- the CPU
	 * moves on to building the next frame immediately. The real completion
	 * result is discovered later, by the next MGLFlushPendingRender call;
	 * that deferred wait, not this function, is what detects a render
	 * timeout. Single flag, not per-CL-slot -- see
	 * render_pending's own comment (v3d_context.h) for why. */
	if (MGLV3D_WINDOW_CACHE_PAIR && context->v3dWindow && !backend->doing_intermediate_pass && context->vmembase)
	{
		/* Window mode, opt-in (MGLV3D_WINDOW_CACHE_PAIR): the GPU is about to
		 * write the window's render target, which the ClipBlit then reads on
		 * the CPU -- CachePreDMA before the write; the matching CachePostDMA
		 * is in MGLFlushPendingRender (see s_window_dma_addr). */
		ULONG len = context->bprow * (ULONG)height;

		s_window_dma_addr = (APTR)context->vmembase;
		s_window_dma_len  = len;
		CachePreDMA(s_window_dma_addr, &len, 0);
	}
	backend->render_lastframe = v3d_submit_render(
	                                 (v3d_address)(ULONG)backend->render_buf[backend->build_slot].start,
	                                 (v3d_address)((ULONG)backend->render_buf[backend->build_slot].start + backend->render_buf[backend->build_slot].used));
	backend->render_pending = TRUE;
	/* Blocks retired so far belong to this job -- see v3d_frame.h. */
	v3d_frame_render_submitted(&backend->frame);

	/* This pass is genuinely submitted now (the CL-buffer-overflow check
	 * above would have dropped it before reaching here) -- if it was an
	 * intermediate pass, its color output really is on its way
	 * into scratch_color_mem, so the NEXT gl_FramePresent's color LOAD
	 * should read it back instead of clearing/loading the real screen. */
	if (backend->doing_intermediate_pass)
		backend->has_scratch_color = TRUE;

	backend->build_slot = 1 - backend->build_slot;
	{ extern v3d_u32 g_v3d_cl_generation; g_v3d_cl_generation++; }

	/* Lock deliberately stays held -- acquired earlier in THIS function,
	 * released only by a later MGLFlushPendingRender call, once this
	 * frame's render is confirmed done by the GPU. */
	backend->frame_active = FALSE;
	return wr;
}

/*
 * Scissored clear. Real GL confines glClear to the scissor box whenever
 * GL_SCISSOR_TEST is on. This hardware's TLB clear (ClearTileBuffers,
 * gl_FramePresent) is all-or-nothing across every tile, so a sub-rect clear
 * cannot use it at all. MESA hits the same wall and solves it the same way:
 * v3d_clear takes the TLB path only when the clear covers the whole render
 * area, and otherwise falls back to drawing (u_blitter).
 *
 * Drawing it as a quad has a second benefit that matters more here. A quad
 * is ordered in the command stream like any other primitive, so a scissored
 * mid-frame clear needs NO pass split -- unlike the full-screen case, which
 * does (see GLClear below). An application that clears a small scissored
 * view several times per frame would otherwise pay a whole extra render
 * pass for every one of those clears.
 *
 * Confinement itself is free: the per-draw ClipWindow (gl_EmitPrimitiveV3D,
 * draw.c) is already viewport-intersect-scissor. The viewport is temporarily
 * set to the full drawable so that intersection collapses to exactly the
 * scissor rect -- glClear is confined by the scissor ALONE in real GL, never
 * by the viewport, and without this a viewport smaller than the scissor would
 * under-clear.
 *
 * The depth range is forced back to its default for the quad so the clear
 * depth maps to NDC by a plain 2d-1, rather than having to invert whatever
 * glDepthRange the application last set -- a collapsed range would divide by
 * zero.
 */
static void gl_ClearViaQuad(GLcontext context, GLbitfield mask)
{
	V3DContext* backend = &context->backend;
	GLboolean do_color = (mask & GL_COLOR_BUFFER_BIT) ? GL_TRUE : GL_FALSE;
	GLboolean do_depth = (mask & GL_DEPTH_BUFFER_BIT) ? GL_TRUE : GL_FALSE;

	/* Saved state -- everything this function disturbs, restored below. */
	GLfloat   s_ax = context->ax, s_ay = context->ay;
	GLfloat   s_sx = context->sx, s_sy = context->sy;
	GLfloat   s_sz = context->sz, s_az = context->az;
	GLuint    s_clipflags = context->ClipFlags;
	GLuint    s_matrixmode = context->CurrentMatrixMode;
	MGLColor  s_color = context->CurrentColor;
	GLboolean s_updatecolor = context->UpdateCurrentColor;
	GLboolean s_depthtest = context->DepthTest_State;
	GLboolean s_depthmask = context->DepthMask;
	GLboolean s_blend = context->Blend_State;
	GLboolean s_alpha = context->AlphaTest_State;
	GLboolean s_fog = context->Fog_State;
	GLboolean s_cull = context->CullFace_State;
	GLboolean s_tex[MAX_TEXUNIT];
	GLboolean s_cr = context->ColorMaskR, s_cg = context->ColorMaskG;
	GLboolean s_cb = context->ColorMaskB, s_ca = context->ColorMaskA;
	v3d_u32   s_zmode = backend->zmode;
	GLuint    cc = context->ClearColor;
	GLfloat   z;
	GLfloat   qx0, qx1, qy0, qy1;
	int       i;

	for (i = 0; i < MAX_TEXUNIT; i++)
	{
		s_tex[i] = context->Texture2D_State[i];
		context->Texture2D_State[i] = GL_FALSE;
	}

	GLMatrixMode(context, GL_PROJECTION);
	GLPushMatrix(context);
	GLLoadIdentity(context);
	GLMatrixMode(context, GL_MODELVIEW);
	GLPushMatrix(context);
	GLLoadIdentity(context);

	/* Full-drawable viewport, so ClipWindow collapses to the scissor rect. */
	GLViewport(context, 0, 0, (GLsizei)backend->width, (GLsizei)backend->height);
	context->sz = 0.5f;
	context->az = 0.5f;

	MGLSetState(context, GL_BLEND, GL_FALSE);
	MGLSetState(context, GL_ALPHA_TEST, GL_FALSE);
	MGLSetState(context, GL_FOG, GL_FALSE);
	MGLSetState(context, GL_CULL_FACE, GL_FALSE);

	/* Each bit writes only its own buffer: a colour-only clear must leave
	 * depth untouched, and a depth-only clear must leave colour untouched. */
	MGLSetState(context, GL_DEPTH_TEST, do_depth);
	GLDepthMask(context, do_depth);
	if (do_depth)
	{
		/* ALWAYS: the quad must write the clear depth over every pixel of
		 * the rect, whatever depth is already there. */
		GLDepthFunc(context, GL_ALWAYS);
	}

	GLColorMask(context, do_color, do_color, do_color, do_color);

	GLColor4f(context,
	          (GLfloat)((cc >> 16) & 0xFF) * (1.0f / 255.0f),
	          (GLfloat)((cc >>  8) & 0xFF) * (1.0f / 255.0f),
	          (GLfloat)( cc        & 0xFF) * (1.0f / 255.0f),
	          (GLfloat)((cc >> 24) & 0xFF) * (1.0f / 255.0f));

	z = do_depth ? (GLfloat)(context->ClearDepth * 2.0 - 1.0) : 0.0f;

	/* Size the quad to the scissor rect rather than drawing a full-screen
	 * one and leaning on ClipWindow to cut it down. ClipWindow still confines
	 * it, so this is belt-and-braces for coverage -- the reason is DEPTH: a
	 * full-screen primitive is exactly where the rasteriser's depth
	 * plane-equation setup would lose precision, since the gradient is fitted
	 * across the whole drawable to represent a constant. The viewport is
	 * already forced to the full drawable above, so screen-to-NDC here is a
	 * plain linear map with no matrix involved. Note scissor_y is TOP-origin
	 * (GLScissor converts), hence qy1 being the top edge. */
	if (backend->scissor_enable)
	{
		qx0 = 2.0f * (float)backend->scissor_x / (float)backend->width - 1.0f;
		qx1 = 2.0f * (float)(backend->scissor_x + backend->scissor_w) / (float)backend->width - 1.0f;
		qy1 = 1.0f - 2.0f * (float)backend->scissor_y / (float)backend->height;
		qy0 = 1.0f - 2.0f * (float)(backend->scissor_y + backend->scissor_h) / (float)backend->height;
	}
	else
	{
		qx0 = -1.0f; qx1 = 1.0f; qy0 = -1.0f; qy1 = 1.0f;
	}

	/* No early-Z flag is set by hand here. A depth clear draws with
	 * dtf = ALWAYS, which the early-Z direction mapping turns into
	 * V3D_EZ_DISABLED for the frame under the ordinary rule -- so the general
	 * mechanism covers this, and only for frames that really contain such a
	 * clear. See ez_state's comment (v3d_context.h). */

	GLBegin(context, GL_QUADS);
		GLVertex4f(context, qx0, qy0, z, 1.0f);
		GLVertex4f(context, qx1, qy0, z, 1.0f);
		GLVertex4f(context, qx1, qy1, z, 1.0f);
		GLVertex4f(context, qx0, qy1, z, 1.0f);
	GLEnd(context);

	/* --- restore --- */
	GLMatrixMode(context, GL_MODELVIEW);
	GLPopMatrix(context);
	GLMatrixMode(context, GL_PROJECTION);
	GLPopMatrix(context);
	GLMatrixMode(context, (GLenum)s_matrixmode);

	for (i = 0; i < MAX_TEXUNIT; i++)
		context->Texture2D_State[i] = s_tex[i];

	MGLSetState(context, GL_BLEND, s_blend);
	MGLSetState(context, GL_ALPHA_TEST, s_alpha);
	MGLSetState(context, GL_FOG, s_fog);
	MGLSetState(context, GL_CULL_FACE, s_cull);
	/* zmode first: restoring GL_DEPTH_TEST recomputes depth_ez_dir from it,
	 * and with the quad's ALWAYS still in place early-Z would stay disabled
	 * until the application next called glDepthFunc. */
	backend->zmode = s_zmode;
	MGLSetState(context, GL_DEPTH_TEST, s_depthtest);
	GLDepthMask(context, s_depthmask);
	GLColorMask(context, s_cr, s_cg, s_cb, s_ca);

	context->CurrentColor = s_color;
	context->UpdateCurrentColor = s_updatecolor;
	context->ax = s_ax; context->ay = s_ay;
	context->sx = s_sx; context->sy = s_sy;
	context->sz = s_sz; context->az = s_az;
	context->ClipFlags = s_clipflags;
}

void GLClear(GLcontext context, GLbitfield mask)
{
	V3DContext* backend = &context->backend;

	if (!(mask & (GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT)))
		return;

	/* Scissored clear: anything short of the whole drawable cannot use the
	 * TLB clear (it is per-tile, all-or-nothing), so hand it to the quad
	 * path -- which also spares it the pass split below. See
	 * gl_ClearViaQuad's own comment. */
	if (backend->scissor_enable &&
	    (backend->scissor_x > 0 || backend->scissor_y > 0 ||
	     (ULONG)(backend->scissor_x + backend->scissor_w) < (ULONG)backend->width ||
	     (ULONG)(backend->scissor_y + backend->scissor_h) < (ULONG)backend->height))
	{
		/* A pass must exist to draw into -- ordinarily one already does,
		 * but a scissored clear as a frame's very first GL call has none. */
		gl_FrameBegin(context);
		gl_ClearViaQuad(context, mask);
		return;
	}

	/* Mid-frame clear: the hardware executes a clear at TILE LOAD time, so
	 * folding one into a pass that already holds geometry applies it
	 * retroactively -- BEFORE that geometry instead of after it. Both bits
	 * are observably wrong that way: a depth reset never happens (geometry
	 * drawn after the clear still tests against the depths written before
	 * it), and pre-clear colour survives where the app asked for it to be
	 * wiped. Split the pass instead, so the clear belongs only to what
	 * follows it.
	 *
	 * Reuses force_new_pass wholesale rather than adding a second
	 * mechanism. That path already finalizes the in-progress pass into
	 * scratch_color_mem and composites it back on the next pass's tile
	 * load, which is exactly what a split needs on a single-buffered
	 * screen -- storing an early-finalized pass straight into the real,
	 * already-visible screen flickers (see force_new_pass's own comment,
	 * v3d_context.h). Setting the flag when it is already set is harmless:
	 * gl_FrameBegin acts on it once and clears it.
	 *
	 * draw_state_configured is the "this pass already holds geometry"
	 * test -- gl_EnsureDrawState (draw.c) sets it on a pass's first draw
	 * call, gl_FrameBegin resets it. Without that guard an app issuing
	 * glClear(GL_COLOR_BUFFER_BIT) and glClear(GL_DEPTH_BUFFER_BIT) as two
	 * separate calls at the top of a frame would split between them and
	 * pay for a whole empty pass that binned and stored nothing. */
	if (backend->frame_active && backend->draw_state_configured)
		backend->force_new_pass = TRUE;

	gl_FrameBegin(context);

	/* A split leaves the finalized pass's colour in scratch_color_mem for
	 * the next tile load to composite back (has_scratch_color). When the
	 * clear that CAUSED the split is a colour clear, that composite is
	 * pointless -- the load would read the scratch back only for the clear
	 * to overwrite every pixel of it -- so drop it and let the clear
	 * stand. Deliberately not the same case as gl_FramePresent's
	 * `if (has_scratch_color) clearColor = FALSE`, where the later colour
	 * clear comes from an UNRELATED view and the scratch must win (see that
	 * comment): splitting is what keeps those two apart, because each clear
	 * gets its own pass and can no longer reach back into one it never
	 * belonged to. */
	if ((mask & GL_COLOR_BUFFER_BIT) && backend->has_scratch_color)
		backend->has_scratch_color = FALSE;

	/* Snapshot NOW, not at gl_FramePresent time -- see the comment on
	 * clear_color_value/clear_depth_value in v3d_context.h. Matches
	 * MESA's v3d_clear/v3d_tlb_clear (v3dx_draw.c), which packs the
	 * clear color into the job's own state immediately rather than
	 * re-reading some later "current" value. */
	if (mask & GL_COLOR_BUFFER_BIT)
	{
		backend->pending_clear_color = TRUE;
		backend->clear_color_value = context->ClearColor;
	}
	if (mask & GL_DEPTH_BUFFER_BIT)
	{
		backend->pending_clear_depth = TRUE;
		backend->clear_depth_value = (float)context->ClearDepth;
	}
}

void MGLLockMode(GLcontext context, GLenum lockMode)
{
#ifdef AUTOMATIC_LOCKING_ENABLE
	context->LockMode = lockMode;
#endif
}

GLboolean MGLLockBack(GLcontext context, MGLLockInfo *info)
{
	if (!context->v3dWindow && context->NumBuffers >= 2)
	{
		/* Double buffering: vmembase is the buffer ChangeScreenBuffer just
		 * put on display, and no lock is held once mglSwitchDisplay returns
		 * -- there is no valid back-buffer address to hand out. Refuse
		 * rather than let the caller write into the visible buffer. */
		if (info)
		{
			info->width = context->backend.width;
			info->height = context->backend.height;
			info->depth = 32;
			info->pixel_format = 0;
			info->base_address = NULL;
			info->pitch = 0;
		}
		return GL_FALSE;
	}

	/* No hardware lock primitive exists -- see this file's header comment. */
	context->v3dLocked = GL_TRUE;

	if (info)
	{
		info->width = context->backend.width;
		info->height = context->backend.height;
		info->depth = 32;
		info->pixel_format = 0; /* RGBA8, always -- V3D has one fixed format */
		info->base_address = (void*)context->vmembase;
		info->pitch = context->bprow;
	}

	return GL_TRUE;
}

GLboolean MGLLockDisplay(GLcontext context)
{
	context->v3dLocked = GL_TRUE;
	return GL_TRUE;
}

void MGLUnlockDisplay(GLcontext context)
{
	context->v3dLocked = GL_FALSE;
}

/*
 * Readback source for glReadPixels and glCopyTex(Sub)Image2D (others.c,
 * texture.c).
 *
 * GL's commands take effect in the order issued, so a read must see every
 * pixel drawn before it, including the frame still being built. On this tile
 * renderer those draws exist only as a binning list until a pass renders
 * them, so the read finishes the pass into scratch_color_mem -- the same pass
 * split a mid-frame glClear uses (gl_FrameBegin's force_new_pass branch). The
 * screen is not written, so nothing flickers; the pass that follows loads the
 * scratch colour and the stored depth and drawing simply continues on top.
 * This is Mesa's behaviour on the same hardware (a PIPE_MAP_READ flushes the
 * job writing the buffer and waits; the next job loads colour and depth).
 *
 * THE CPU ONLY EVER READS MEMORY WRAPPED BY ITS OWN CachePreDMA/CachePostDMA.
 * The exec rule for a device that WRITES memory: clear the caches with
 * CachePreDMA before the write, flush them again with
 * CachePostDMA after it. Normal rendering needs neither -- nothing on the CPU
 * reads the screen -- so the pair lives here, around a pass this function
 * runs itself, and every read comes from that pass's scratch output:
 *   - frame in progress: the split pass renders it into scratch;
 *   - no frame in progress (after a swap, before any frame): a pass of its
 *     own that only loads the render target and stores it into scratch, and
 *     is discarded again by MGLReadbackEnd -- the next swap does no extra
 *     work.
 * A read straight from the render target after a swap can return 64-byte
 * blocks of the previous frame, so reading the render target directly is only
 * the fallback below.
 *
 * On return *base points at the top row of a width*height framebuffer in the
 * render store's layout (bytes B,G,R,A per pixel, rows top-down, *stride
 * bytes per row). MGLReadbackEnd must always be called afterwards. A non-NULL
 * *lock means the fallback path, and no library call may happen before
 * MGLReadbackEnd (cybergraphics: the address is only valid inside the lock).
 * Returns GL_FALSE, with MGLReadbackEnd still required, if nothing could be
 * read.
 *
 * Fallback: if scratch cannot be allocated or the pass was dropped, the
 * render target is read directly.
 */
static GLboolean s_readback_own_pass = GL_FALSE;

GLboolean MGLReadbackBegin(GLcontext context, UBYTE **base, ULONG *stride, APTR *lock)
{
	V3DContext* backend = &context->backend;
	struct BitMap *bm;
	ULONG bprow = 0, addr = 0;

	*base = NULL;
	*stride = 0;
	*lock = NULL;
	s_readback_own_pass = GL_FALSE;

	if (v3d_backend_alloc_scratch_color(&context->device, backend) == 0)
	{
		ULONG len;

		if (!backend->frame_active)
		{
			/* Start a pass to run the readback's own. gl_FrameBegin first
			 * waits for any render still running (the swap's, pipelined) --
			 * the render target must be complete before it is loaded. Its
			 * depth load is not the application relying on depth persisting
			 * across frames (PassContinuesSplit, context.h). */
			gl_FrameBegin(context);
			context->PassContinuesSplit = GL_TRUE;
			s_readback_own_pass = GL_TRUE;
		}

		len = backend->scratch_stride * (ULONG)backend->height;
		CachePreDMA(backend->scratch_color_mem.hostptr, &len, 0);

		backend->force_new_pass = TRUE;
		gl_FrameBegin(context);          /* the pass into scratch, and its wait */

		len = backend->scratch_stride * (ULONG)backend->height;
		CachePostDMA(backend->scratch_color_mem.hostptr, &len, 0);

		if (backend->has_scratch_color)
		{
			*base = (UBYTE *)backend->scratch_color_mem.hostptr;
			*stride = backend->scratch_stride;
			return GL_TRUE;
		}
	}

	/* Fallback: the render target, read directly. Waits for a render still
	 * running and releases the lock it holds first. Same bitmap choice as
	 * gl_FramePresent, except that double buffering reads the buffer on
	 * display (NumBuffers only reaches 2 when the library is built with
	 * MGLV3D_DOUBLE_BUFFER_ENABLED=1). */
	MGLFlushPendingRender(context);

	/* v3dBitMap, not v3dWindow -- the same test as gl_FramePresent's own
	 * acquire point. An adopted-bitmap context has a render target and no
	 * window, and would otherwise land on a NULL v3dScreen here. */
	if (context->v3dBitMap)
		bm = context->v3dBitMap;
	else if (context->NumBuffers >= 2)
		bm = context->Buffers[1 - context->BufNr]->sb_BitMap;
	else
		bm = context->v3dScreen->RastPort.BitMap;

	*lock = LockBitMapTags(bm,
		LBMI_BYTESPERROW, (ULONG)&bprow,
		LBMI_BASEADDRESS, (ULONG)&addr,
		TAG_DONE);
	if (!*lock)
		return GL_FALSE;

	*base = (UBYTE *)addr;
	*stride = bprow;
	return GL_TRUE;
}

void MGLReadbackEnd(GLcontext context, APTR lock)
{
	if (lock)
		UnLockBitMap(lock);

	/* Discard the pass MGLReadbackBegin started for itself: it holds no
	 * draws, only the scratch copy just read. Same discard as a dropped
	 * frame (gl_FramePresent's frame_corrupted path). The next GL call starts
	 * a fresh pass that loads the render target as usual. */
	if (s_readback_own_pass)
	{
		context->backend.frame_active = FALSE;
		context->backend.has_scratch_color = FALSE;
		s_readback_own_pass = GL_FALSE;
	}
}

/* Accepted, no effect: the buffer count chosen with mglChooseNumberOfBuffers
 * decides vsync (see this file's header comment). DoSync is still stored so
 * the dispatch slot and existing callers keep working. */
void MGLEnableSync(GLcontext context, GLboolean enable)
{
	context->DoSync = enable;
}

void MGLSwitchDisplay(GLcontext context)
{
	v3d_wait_result wr;

	MGLUnlockDisplay(context);

	/*
	 * The real "present" trigger: gl_FramePresent finalizes whatever
	 * GLClear/draw.c accumulated into the binning list since gl_FrameBegin
	 * and actually submits.
	 */
	wr = gl_FramePresent(context);
	if (wr != v3d_wait_result_success)
		D(("MGLSwitchDisplay: frame present failed, result=%ld\n", (LONG)wr));

	if (!context->v3dBitMap)
	{
		if (context->NumBuffers >= 2)
		{
			/* Double buffering with vsync -- see vid_OpenDisplay's own
			 * comment. gl_FramePresent submitted this frame's render into
			 * the back buffer Buffers[BufNr] without waiting; wait for it
			 * here and release its lock (MGLFlushPendingRender) BEFORE the
			 * flip -- a buffer must never be shown while the GPU still
			 * writes into it, and a lock held across ChangeScreenBuffer
			 * shows nothing at all. This gives up frame pipelining in this
			 * mode, which the refresh-locked flip below would cap anyway.
			 *
			 * Flip only when this call really submitted a render:
			 * gl_FramePresent also returns success when nothing was drawn
			 * since the last swap, and flipping then would show the frame
			 * from two swaps back. render_pending is TRUE here only after a
			 * real submit -- every earlier render was already waited for,
			 * by gl_FrameBegin or by the previous swap.
			 *
			 * One ChangeScreenBuffer call, no retry loop: it completes the
			 * swap inside the call, waiting about one refresh. If it ever
			 * returns FALSE, BufNr stays put -- that buffer was never shown,
			 * so the next frame can safely render into it again. */
			GLboolean submitted = context->backend.render_pending;

			MGLFlushPendingRender(context);

			if (submitted)
			{
				context->Buffers[context->BufNr]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = NULL;
				if (ChangeScreenBuffer(context->v3dScreen, context->Buffers[context->BufNr]))
					context->BufNr = 1 - context->BufNr;
			}
		}

		/* Fullscreen, single-buffer (default): already drawing directly
		 * into the visible screen bitmap (see this file's header
		 * comment) -- nothing to blit. */
		return;
	}

	/* The bitmap lock deliberately stays held after gl_FramePresent returns,
	 * released only by the next MGLFlushPendingRender call -- fine for
	 * fullscreen (nothing else touches the screen bitmap before then), but
	 * ClipBlit right below is a real graphics.library call against THIS SAME
	 * v3dBitMap, every single frame, unconditionally -- exactly the "DON'T
	 * use any library calls while the bitmap is locked" CGX rule (see
	 * vid_OpenDisplay's header comment). This one doesn't block forever:
	 * ClipBlit against a locked bitmap silently fails to show anything, which
	 * reads as a permanently blank window instead of a hang. Flushing here
	 * waits for THIS frame's own render (not just the previous frame's,
	 * unlike fullscreen's deferred pattern) and releases the lock before
	 * ClipBlit runs -- windowed mode loses the cross-frame pipelining overlap
	 * for its own present step, but fullscreen (which never reaches this
	 * line) keeps the full benefit. */
	MGLFlushPendingRender(context);

	/* MGLCreateContextFromBitMap: the host owns the target and presents it
	 * itself, so the frame is finished and that is all. Blitting would be
	 * wrong even if there were somewhere to send it -- v3dWindow is NULL for
	 * these contexts, so the ClipBlit below would dereference it.
	 *
	 * This is the whole point of the entry point. A program compositing our
	 * output alongside another renderer cannot use the window form, because two
	 * libraries blitting into one window is last-writer-wins. Here nobody
	 * competes. */
	if (context->ExternalBitMap)
		return;

	/* Windowed mode: unchanged from the original -- real AmigaOS/CGX
	 * ClipBlit, no Warp3D coupling. */
	ClipBlit(context->v3dRastPort,0,0,
		context->v3dWindow->RPort, context->v3dWindow->BorderLeft,
		context->v3dWindow->BorderTop,
		context->v3dWindow->Width-context->v3dWindow->BorderLeft-context->v3dWindow->BorderRight,
		context->v3dWindow->Height-context->v3dWindow->BorderTop-context->v3dWindow->BorderBottom,
		0xC0);
}



/* Multitexture buffer: */

extern void FreeMtex();
extern GLboolean AllocMtex(int size);

GLboolean MGLInitContext(GLcontext context)
{
	int i;

	context->CurrentPrimitive   = GL_BASE;
	context->CurrentError       = GL_NO_ERROR;

	D(("MGLInitContext: enter\n"));

	context->WBuffer	    = malloc(64*newVertexBufferSize);
	if (!context->WBuffer)
	{
		E(("MGLInitContext: WBuffer malloc failed (%ld bytes)\n", (LONG)(64*newVertexBufferSize)));
		return GL_FALSE;
	}

	context->ElementIndex	    = malloc(sizeof(UWORD)*(newVertexBufferSize));
	if (!context->ElementIndex)
	{
		E(("MGLInitContext: ElementIndex malloc failed\n"));
		return GL_FALSE;
	}

	context->ElementTexS	    = malloc(sizeof(GLfloat)*(newVertexBufferSize));
	if (!context->ElementTexS)
	{
		E(("MGLInitContext: ElementTexS malloc failed\n"));
		return GL_FALSE;
	}

	context->ElementTexT	    = malloc(sizeof(GLfloat)*(newVertexBufferSize));
	if (!context->ElementTexT)
	{
		E(("MGLInitContext: ElementTexT malloc failed\n"));
		return GL_FALSE;
	}

	context->VertexBuffer       = malloc(sizeof(MGLVertex)*newVertexBufferSize);
	if (!context->VertexBuffer)
	{
		E(("MGLInitContext: VertexBuffer malloc failed\n"));
		return GL_FALSE;
	}

	/* The vertex numbers the draw functions index this buffer with, one per
	 * slot (draw.c, s_seq). */
	{
		extern GLboolean d_AllocSeq(int size);

		if (!d_AllocSeq(newVertexBufferSize))
		{
			E(("MGLInitContext: vertex number table malloc failed\n"));
			return GL_FALSE;
		}
	}

	context->NormalBuffer       = malloc(sizeof(MGLNormal)*newVertexBufferSize);
	if (!context->NormalBuffer)
	{
		E(("MGLInitContext: NormalBuffer malloc failed\n"));
		return GL_FALSE;
	}

	context->VertexBufferSize   = (GLuint)newVertexBufferSize;
	context->VertexBufferPointer = 0;

	if(!AllocMtex(newMTBufferSize))
	{
		E(("MGLInitContext: AllocMtex failed\n"));
		return GL_FALSE;
	}
	D(("MGLInitContext: buffers + AllocMtex OK\n"));

	context->NormalBufferPointer = 0;

	context->NormalBuffer[0].x = 0.f;
	context->NormalBuffer[0].y = 0.f;
	context->NormalBuffer[0].z = 0.f;

	context->textureObjectCount = newTextureBufferSize;
	context->textureObjects     = malloc(sizeof(V3DTexture *) * newTextureBufferSize);
	context->GeneratedTextures  = malloc(sizeof(GLubyte) * newTextureBufferSize);

	if (!context->textureObjects || !context->GeneratedTextures)
	{
		E(("MGLInitContext: textureObjects/GeneratedTextures malloc failed\n"));
		return GL_FALSE;
	}

	/* Indicate unbound texture object(s): */

	context->CurrentBinding  = 0;
	context->VirtualBinding  = 0;
	context->ArrayTexBound	 = GL_FALSE; /* repeated by glEnd(); */

	context->ActiveTexture   = 0;
	context->VirtualTexUnits = 0;  /* disable multitex */

	for (i=0; i<newTextureBufferSize; i++)
	{
		context->textureObjects[i]	= NULL;
		context->GeneratedTextures[i]   = 0;
	}

	context->CurrentTexS = context->CurrentTexT = 0.0;
	context->CurrentTexQ = 1.0;
	context->CurrentTexQValid = GL_FALSE;

	context->PackAlign = context->UnpackAlign = 4;

	context->AlphaTest_State    = GL_FALSE;
	context->Blend_State        = GL_FALSE;
	context->TextureGenS_State  = GL_FALSE;
	context->TextureGenT_State  = GL_FALSE;
	context->Fog_State          = GL_FALSE;
	context->Scissor_State      = GL_FALSE;
	context->CullFace_State     = GL_FALSE;
	context->DepthTest_State    = GL_FALSE;
	context->PointSmooth_State  = GL_FALSE;
	context->Dither_State       = GL_TRUE;
	context->ZOffset_State      = GL_FALSE;
	context->PolygonOffsetFill_State = GL_FALSE;

	context->ColorMaskR = GL_TRUE;
	context->ColorMaskG = GL_TRUE;
	context->ColorMaskB = GL_TRUE;
	context->ColorMaskA = GL_TRUE;

	context->FogDirty           = GL_FALSE;
	context->FogStart           = 1.0;
	context->FogEnd             = 0.0;

	for (i=0; i<MAX_TEXUNIT; i++)
	{
	    context->Texture2D_State[i]	= GL_FALSE;
	    context->TexEnv[i]		= GL_MODULATE;
	}


	context->CurTexEnv = 0;

	context->MinFilter = GL_NEAREST;
	context->MagFilter = GL_NEAREST;
	context->WrapS     = GL_REPEAT;
	context->WrapT     = GL_REPEAT;

	/* backend.fog_* -- matches fog.c's own field mapping. */
	context->backend.fog_start   = 1.0f;
	context->backend.fog_end     = 0.1f;
	context->backend.fog_density = 1.0f;
	context->backend.fog_r = context->backend.fog_g = context->backend.fog_b = 0;
	context->FogRange           = 1.0;

	/* Real OpenGL mandates GL_LESS as glDepthFunc's default even if the
	 * application never calls it. backend.zmode is set ONLY inside
	 * GLDepthFunc() itself (this file), so an application that never calls
	 * it would otherwise leave zmode at whatever this struct was allocated
	 * with -- 0, i.e. V3D_Z_NEVER (v3d_context.h) -- and
	 * gl_EmitCullBlendState's dtf calculation (draw.c) reads zmode, so every
	 * depth-tested fragment would fail its compare and nothing would reach
	 * the color buffer: a fully black screen. */
	context->backend.zmode = V3D_Z_LESS;

	context->CurrentCullFace    = GL_BACK;
	context->CurrentFrontFace   = GL_CCW;
	context->CurrentCullSign    = 1;

	/* Surgeon: the current-colour defaults and UpdateCurrentColor below
	 * (MiniGL source). */
	context->CurrentColor.r = 1.0;
	context->CurrentColor.g = 1.0;
	context->CurrentColor.b = 1.0;
	context->CurrentColor.a = 1.0;
	context->UpdateCurrentColor = GL_TRUE;

	context->ShadeModel         = GL_SMOOTH;
	context->DepthMask          = GL_TRUE;

	context->ClearDepth         = 1.0;

#ifdef AUTOMATIC_LOCKING_ENABLE
	context->LockMode = MGL_LOCK_MANUAL;
#endif

	context->NoMipMapping = newNoMipMapping;
	context->NoFallbackAlpha = newNoFallbackAlpha;

	context->Idle = NULL;
	context->MouseHandler = NULL;
	context->SpecialHandler = NULL;
	context->KeyHandler = NULL;

	context->SrcAlpha = 0;
	context->DstAlpha = 0;
	context->AlphaFellBack = GL_FALSE;

	context->WOne_Hint = GL_FALSE;
	/* Surgeon (MiniGL source). */
	context->FixpointTrans_Hint = GL_FALSE;

	context->ZOffset = 0.0;
	context->PolygonOffsetFactor = 0.0;
	/* Initialised explicitly rather than left to the allocator: GL's default
	 * is 0. See alpha_func (v3d_context.c) for what relying on zero-init
	 * costs when GL's default is not the zero value. */
	context->PolygonOffsetUnits = 0.0;

	context->PaletteData = malloc(4*256);
	context->PaletteSize = 0;
	context->PaletteFormat = 0;

	/* Joe Sera: CurPolygonMode/CurShadeModel/CurBlendSrc/CurBlendDst here and
	 * the CurUnpack* fields further down (MiniGL source). */
	context->CurPolygonMode      = GL_FILL ;
	context->CurShadeModel       = GL_SMOOTH ;
	context->CurBlendSrc         = GL_ONE ;
	context->CurBlendDst         = GL_ZERO ;
	/* context->backend.blend_srcmode/blend_dstmode (the REAL hardware-facing
	 * V3D_BLEND_FACTOR_* values gl_EmitCullBlendState actually reads) are a
	 * SEPARATE representation from CurBlendSrc/CurBlendDst above -- only ever
	 * written inside GLBlendFunc() (others.c). A program that calls
	 * glEnable(GL_BLEND) but never glBlendFunc() (legal GL) would otherwise
	 * leave these at their implicit zero-init value (V3D_BLEND_FACTOR_ZERO
	 * for both), silently CONTRADICTING the correct GL_ONE/GL_ZERO default
	 * just set above -- with the fixed-function hardware blend engaging
	 * (be=TRUE), a ZERO/ZERO config makes every blended pixel resolve to pure
	 * black (src*0 + dst*0) instead of real GL's documented default (blending
	 * enabled with no glBlendFunc call = src passes through unchanged,
	 * ONE/ZERO). Explicitly synced here to match. */
	context->backend.blend_srcmode = V3D_BLEND_FACTOR_ONE;
	context->backend.blend_dstmode = V3D_BLEND_FACTOR_ZERO;
	/* Same argument as above, applied to the separate-alpha and equation
	 * state: a program that enables GL_BLEND but never calls
	 * glBlendFuncSeparate/glBlendEquation must still get GL's documented
	 * defaults, not whatever zero-init left behind. GL's default is ONE/ZERO
	 * on both channels with GL_FUNC_ADD. */
	context->backend.blend_alpha_srcmode = V3D_BLEND_FACTOR_ONE;
	context->backend.blend_alpha_dstmode = V3D_BLEND_FACTOR_ZERO;
	context->backend.blend_color_equation = V3D_BLEND_MODE_ADD;
	context->backend.blend_alpha_equation = V3D_BLEND_MODE_ADD;
	context->backend.blend_nonadd_warned = 0;
	context->CurUnpackRowLength  = 0 ;
	context->CurUnpackSkipPixels = 0 ;
	context->CurUnpackSkipRows   = 0 ;
	context->PackRowLength       = 0;
	context->PackSkipPixels      = 0;
	context->PackSkipRows        = 0;
	context->PackSwapBytes       = GL_FALSE;
	context->PackLsbFirst        = GL_FALSE;
	context->PassContinuesSplit  = GL_FALSE;

	/* Joe Sera: CurWriteMask/CurDepthTest (MiniGL source). */
	context->CurWriteMask = GL_TRUE ;
	context->CurDepthTest = GL_FALSE ;

	/* Vertex array stuff */
	context->ClientState        = 0;

	/* Surgeon: the vertex-array pipeline flag and the ArrayPointer defaults
	 * below (MiniGL source). */
	context->VertexArrayPipeline  = GL_TRUE;

	/* Corrected MGLVertex_t field paths -- `.color` is top-level, `.v.u0`/
	 * `.v.v0` (see this file's header comment). These defaults only matter
	 * if GLDrawArrays/GLDrawElements are ever called with the corresponding
	 * GLCS_* client-state bit NOT enabled (not real GL usage, but harmless
	 * to default sensibly rather than leave pointing at garbage) -- the real
	 * GLVertexPointer/GLColorPointer/GLTexCoordPointer overwrite all of this
	 * the moment an app actually calls them. w_buffer/w_off (Warp3D
	 * hardware-array-pointer-only fields) are gone -- see MGLAPointer's own
	 * comment (context.h) for why; colormode and the sizes have real
	 * V3D-native defaults (MGLAColorMode, texcoordsize/vertexsize) instead
	 * of the original's Warp3D colormode/vertexmode enums. */
	context->ArrayPointer.colors = (UBYTE *)&context->VertexBuffer->color;
	context->ArrayPointer.colorstride = sizeof(MGLVertex);
	context->ArrayPointer.colormode = MGLA_COLOR_FLOAT_RGBA;

	context->ArrayPointer.texcoords = (UBYTE *)&context->VertexBuffer->v.u0;
	context->ArrayPointer.texcoordstride = sizeof(MGLVertex);
	context->ArrayPointer.texcoordsize = 2;

	context->ArrayPointer.verts = (UBYTE *)&context->VertexBuffer->v.x;
	context->ArrayPointer.vertexstride = sizeof(MGLVertex);
	context->ArrayPointer.vertexsize = 3;

	context->ArrayPointer.lockfirst = 0;
	context->ArrayPointer.locksize = 0;
	context->ArrayPointer.transformed = 0;
	context->ArrayPointer.state = 0; /* pipeline state */

	context->VertexArrayPipeline  = GL_TRUE;

	/* Area: All triangles smaller than this will not be drawn */
	/* Surgeon: to prevent gaps, triangle-mesh areas are considered
	 * coherently. */
	context->MinTriArea         = 0.5f;

	context->CurrentPointSize     = 1.f;
	context->CurrentLineWidth     = 1.f;   /* GL's default width */
	context->TexGenModeS = context->TexGenModeT = GL_SPHERE_MAP;

	/* GL's default current texture coordinate is (0,0,0,1), per unit. */
	context->CurTexU0 = context->CurTexV0 = 0.f;
	context->CurTexQ0 = 1.f;
	context->CurTexU1 = context->CurTexV1 = 0.f;

	/* GL's defaults for the state behind the index, edge-flag and read-buffer
	 * entry points. The read buffer starts at GL_FRONT, GL's default for a
	 * single-buffered context:
	 * GL_DOUBLEBUFFER answers FALSE (others.c), and the front is where
	 * GLReadPixels reads. */
	context->CurrentIndex         = 1.f;
	context->CurrentEdgeFlag      = GL_TRUE;
	context->ReadBufferMode       = GL_FRONT;
	context->IndexArrayPointer    = NULL;
	context->IndexArrayType       = GL_FLOAT;
	context->IndexArrayStride     = 0;
	context->EdgeFlagArrayPointer = NULL;
	context->EdgeFlagArrayStride  = 0;

	/* The three state-only capabilities (context.h), all disabled by GL
	 * default. */
	context->PolygonOffsetLine_State    = GL_FALSE;
	context->PolygonOffsetPoint_State   = GL_FALSE;
	context->SharedTexturePalette_State = GL_FALSE;

	/* GL's default texgen planes: S = (1,0,0,0), T = (0,1,0,0), object and
	 * eye alike. The eye pair needs no inverse-modelview transform here --
	 * at context creation the modelview is the identity. */
	context->ObjectPlaneS[0] = context->EyePlaneS[0] = 1.f;
	context->ObjectPlaneS[1] = context->EyePlaneS[1] = 0.f;
	context->ObjectPlaneS[2] = context->EyePlaneS[2] = 0.f;
	context->ObjectPlaneS[3] = context->EyePlaneS[3] = 0.f;
	context->ObjectPlaneT[0] = context->EyePlaneT[0] = 0.f;
	context->ObjectPlaneT[1] = context->EyePlaneT[1] = 1.f;
	context->ObjectPlaneT[2] = context->EyePlaneT[2] = 0.f;
	context->ObjectPlaneT[3] = context->EyePlaneT[3] = 0.f;

	/* We set all clipflags by default: */

	context->ClipFlags = (MGL_CLIP_NEGW | MGL_CLIP_FRONT | MGL_CLIP_BACK | MGL_CLIP_RIGHT | MGL_CLIP_LEFT | MGL_CLIP_BOTTOM | MGL_CLIP_TOP);

	context->GuardBand	    = newGuardBand;

	GLMatrixInit(context);

	D(("MGLInitContext: exit OK\n"));

	return GL_TRUE;
}

void *MGLCreateContext(int offx, int offy, int w, int h)
{
	GLcontext context;
	v3d_mem selfMem;
	V3DDevice tempDevice;

	/* v3d_mem_alloc/MEMF_CLEAR instead of malloc+memset, matching every
	 * other allocation in this driver -- no explicit memset needed after,
	 * AllocVec's own MEMF_CLEAR already zeroes it. v3d_mem_alloc only reads
	 * device->sysbase and read-modify-writes device->BytesAllocated, so a
	 * throwaway, zeroed local device works fine for this one early call --
	 * the real context->device isn't usable yet (this allocation IS what
	 * makes it exist). context->selfMem (below) remembers the full v3d_mem
	 * record so MGLDeleteContext's own cleanup can free it the normal way,
	 * via v3d_mem_free(device, &selfMem), same as every other allocation. */
	memset(&tempDevice, 0, sizeof(tempDevice));
	tempDevice.sysbase = *(struct ExecBase**)4L;

	if (v3d_mem_alloc(&tempDevice, &selfMem, sizeof(struct GLcontext_t)) < 0)
	{
		/* OF: this failure message (MiniGL source). */
		printf("Error: Can't get %lu bytes of memory for context\n", (unsigned long)sizeof(struct GLcontext_t));
		return NULL;
	}
	context = (GLcontext)selfMem.hostptr;
	context->selfMem = selfMem;

	context->device.sysbase = tempDevice.sysbase;
	context->device.dosbase = (struct DOSBase*)DOSBase;

	D(("MGLCreateContext: enter, w=%ld h=%ld windowMode=%ld\n", (LONG)w, (LONG)h, (LONG)newWindowMode));

	if (v3d_init(&context->device) != 0)
	{
		E(("MGLCreateContext: v3d_init failed\n"));
		printf("Error: v3d_init failed\n");
		v3d_mem_free(&context->device, &context->selfMem);
		return NULL;
	}
	D(("MGLCreateContext: v3d_init OK\n"));

	if (newWindowMode == GL_FALSE)
	{
		if (GL_FALSE == vid_OpenDisplay(context, w,h, GL_FALSE))
		{
			v3d_free(&context->device);
			v3d_mem_free(&context->device, &context->selfMem);
			E(("MGLCreateContext: vid_OpenDisplay failed\n"));
			printf("Error: opening of display failed\n");
			return NULL;
		}
		D(("MGLCreateContext: vid_OpenDisplay OK\n"));
	}
	else
	{
		if (GL_FALSE == vid_OpenWindow(context, w,h))
		{
			v3d_free(&context->device);
			v3d_mem_free(&context->device, &context->selfMem);
			E(("MGLCreateContext: vid_OpenWindow failed\n"));
			printf("Error: opening of display failed\n");
			return NULL;
		}
		D(("MGLCreateContext: vid_OpenWindow OK\n"));
	}

	if (GL_FALSE == MGLInitContext(context))
	{
		E(("MGLCreateContext: MGLInitContext failed\n"));
		printf("Error: initalisation of context failed\n");
		MGLDeleteContext(context);
		return NULL;
	}
	D(("MGLCreateContext: MGLInitContext OK\n"));

	GLDepthRange(context, 0.0, 1.0);
	GLViewport(context, offx, offy, w, h);
	GLClearColor(context, 1.0, 1.0, 1.0, 1.0);

	D(("MGLCreateContext: exit OK\n"));

	return context;
}

/*
 * Render into a window the application already opened. Deliberately
 * a near-copy of MGLCreateContext above rather than a shared helper: the two
 * differ only in which vid_* call they make, and keeping them side by side makes
 * that one difference obvious instead of hiding it behind a mode flag.
 *
 * mglChooseWindowMode is IGNORED here, and that is correct -- the caller has
 * settled the question by handing us a window. newWindowMode still governs
 * MGLCreateContext, which is the only place it ever meant anything.
 */
void *MGLCreateContextFromWindow(struct Window *window)
{
	GLcontext context;
	v3d_mem selfMem;
	V3DDevice tempDevice;
	int w, h;

	if (!window)
	{
		E(("MGLCreateContextFromWindow: NULL window\n"));
		return NULL;
	}

	/* INNER dimensions: the borders belong to Intuition, and rendering into
	 * them would put geometry under the title bar and the sizing gadget. */
	w = (int)window->Width  - (int)window->BorderLeft - (int)window->BorderRight;
	h = (int)window->Height - (int)window->BorderTop  - (int)window->BorderBottom;

	if (w <= 0 || h <= 0)
	{
		E(("MGLCreateContextFromWindow: window inner area is %ldx%ld\n", (LONG)w, (LONG)h));
		return NULL;
	}

	memset(&tempDevice, 0, sizeof(tempDevice));
	tempDevice.sysbase = *(struct ExecBase**)4L;

	if (v3d_mem_alloc(&tempDevice, &selfMem, sizeof(struct GLcontext_t)) < 0)
	{
		printf("Error: Can't get %lu bytes of memory for context\n", (unsigned long)sizeof(struct GLcontext_t));
		return NULL;
	}
	context = (GLcontext)selfMem.hostptr;
	context->selfMem = selfMem;

	context->device.sysbase = tempDevice.sysbase;
	context->device.dosbase = (struct DOSBase*)DOSBase;

	D(("MGLCreateContextFromWindow: enter, inner %ldx%ld\n", (LONG)w, (LONG)h));

	if (v3d_init(&context->device) != 0)
	{
		E(("MGLCreateContextFromWindow: v3d_init failed\n"));
		printf("Error: v3d_init failed\n");
		v3d_mem_free(&context->device, &context->selfMem);
		return NULL;
	}

	if (GL_FALSE == vid_AdoptWindow(context, window, w, h))
	{
		v3d_free(&context->device);
		v3d_mem_free(&context->device, &context->selfMem);
		E(("MGLCreateContextFromWindow: vid_AdoptWindow failed\n"));
		printf("Error: could not render into the supplied window\n");
		return NULL;
	}

	if (GL_FALSE == MGLInitContext(context))
	{
		E(("MGLCreateContextFromWindow: MGLInitContext failed\n"));
		printf("Error: initalisation of context failed\n");
		MGLDeleteContext(context);
		return NULL;
	}

	GLDepthRange(context, 0.0, 1.0);
	GLViewport(context, 0, 0, w, h);
	GLClearColor(context, 1.0, 1.0, 1.0, 1.0);

	D(("MGLCreateContextFromWindow: exit OK\n"));

	return context;
}

/*
 * Render into a bitmap the application owns; it presents, we do not. See the
 * header comment on vid_AdoptBitMap and the prototype in gl.h for why this
 * exists separately from the window form.
 */
void *MGLCreateContextFromBitMap(struct BitMap *bitmap)
{
	GLcontext context;
	v3d_mem selfMem;
	V3DDevice tempDevice;
	int w, h;

	if (!bitmap)
	{
		E(("MGLCreateContextFromBitMap: NULL bitmap\n"));
		return NULL;
	}

	w = (int)GetBitMapAttr(bitmap, BMA_WIDTH);
	h = (int)GetBitMapAttr(bitmap, BMA_HEIGHT);

	if (w <= 0 || h <= 0)
	{
		E(("MGLCreateContextFromBitMap: bitmap is %ldx%ld\n", (LONG)w, (LONG)h));
		return NULL;
	}

	/* The 4-bytes-per-pixel requirement is enforced in vid_AdoptBitMap, off the
	 * bytes-per-row the probe lock returns -- NOT off BMA_DEPTH, which reports
	 * 24 for a truecolour bitmap because alpha is not depth. See that check. */

	memset(&tempDevice, 0, sizeof(tempDevice));
	tempDevice.sysbase = *(struct ExecBase**)4L;

	if (v3d_mem_alloc(&tempDevice, &selfMem, sizeof(struct GLcontext_t)) < 0)
	{
		printf("Error: Can't get %lu bytes of memory for context\n", (unsigned long)sizeof(struct GLcontext_t));
		return NULL;
	}
	context = (GLcontext)selfMem.hostptr;
	context->selfMem = selfMem;

	context->device.sysbase = tempDevice.sysbase;
	context->device.dosbase = (struct DOSBase*)DOSBase;

	D(("MGLCreateContextFromBitMap: enter, %ldx%ld depth %ld\n", (LONG)w, (LONG)h, (LONG)d));

	if (v3d_init(&context->device) != 0)
	{
		E(("MGLCreateContextFromBitMap: v3d_init failed\n"));
		printf("Error: v3d_init failed\n");
		v3d_mem_free(&context->device, &context->selfMem);
		return NULL;
	}

	if (GL_FALSE == vid_AdoptBitMap(context, bitmap, w, h))
	{
		v3d_free(&context->device);
		v3d_mem_free(&context->device, &context->selfMem);
		E(("MGLCreateContextFromBitMap: vid_AdoptBitMap failed\n"));
		printf("Error: could not render into the supplied bitmap\n");
		return NULL;
	}

	if (GL_FALSE == MGLInitContext(context))
	{
		E(("MGLCreateContextFromBitMap: MGLInitContext failed\n"));
		printf("Error: initalisation of context failed\n");
		MGLDeleteContext(context);
		return NULL;
	}

	GLDepthRange(context, 0.0, 1.0);
	GLViewport(context, 0, 0, w, h);
	GLClearColor(context, 1.0, 1.0, 1.0, 1.0);

	D(("MGLCreateContextFromBitMap: exit OK\n"));

	return context;
}


void MGLDeleteContext(GLcontext context)
{
	GLint current,peak;

	/* Olivier Fabre: the NULL-context guard (MiniGL source). */
	if( !context ) return;

	if (context->v3dBitMap) vid_CloseWindow(context);
	else                    vid_CloseDisplay(context, GL_FALSE);

	/* Back to the single-buffer default for whoever creates the next
	 * context. In the resident minigl.library this static outlives the
	 * program that set it, so without the reset a later program that never
	 * calls mglChooseNumberOfBuffers would open double-buffered with vsync in
	 * a build with MGLV3D_DOUBLE_BUFFER_ENABLED=1 (0 by default, which pins
	 * NumBuffers to 1 whatever this holds).
	 * MGLResizeContext keeps the context and its setting. */
	newNumberOfBuffers = 1;

	v3d_free(&context->device);

	FreeMtex();

	if (context->NormalBuffer) free(context->NormalBuffer);

	if (context->WBuffer) free(context->WBuffer);

	if (context->ElementIndex) free(context->ElementIndex);

	if (context->ElementTexS) free(context->ElementTexS);

	if (context->ElementTexT) free(context->ElementTexT);

	if (context->VertexBuffer) free(context->VertexBuffer);

	{
		extern void d_FreeSeq(void);

		d_FreeSeq();
	}

	if (context->textureObjects) free(context->textureObjects);

	if (context->GeneratedTextures) free (context->GeneratedTextures);

	MGLTexMemStat(context, &current, &peak);

	v3d_mem_free(&context->device, &context->selfMem);
}

#define ED (flag == GL_TRUE?GL_TRUE:GL_FALSE)

extern void tex_SetEnv(GLcontext context, GLenum env);

/* Surgeon: the const parameters (MiniGL source). */
void MGLSetState(GLcontext context, const GLenum cap, const GLboolean flag)
{
	int active = context->ActiveTexture;

	switch(cap)
	{
		case GL_ALPHA_TEST:
			context->AlphaTest_State = flag;
			context->backend.alpha_test_enable = flag;
		break;
		case GL_BLEND:
			context->Blend_State = flag;
		break;
		case GL_TEXTURE_2D:
			context->Texture2D_State[active] = flag;
		break;
		case GL_TEXTURE_GEN_S:
			context->TextureGenS_State = flag;
			break;
		case GL_TEXTURE_GEN_T:
			context->TextureGenT_State = flag;
			break;
		case GL_FOG:
			/* backend.fog_enable is fog_Set's job (fog.c), not mirrored
			 * here -- see this file's header comment. */
			context->FogDirty = GL_TRUE;
			context->Fog_State = flag;
			break;
		case GL_SCISSOR_TEST:
			context->Scissor_State = flag;
			context->backend.scissor_enable = flag;
			break;
		case GL_CULL_FACE:
			context->CullFace_State = flag;
			break;
		case GL_DEPTH_WRITEMASK:
			context->CurWriteMask = flag ;
		break ;
		case GL_DEPTH_TEST:
			context->CurDepthTest = flag ;
			context->DepthTest_State = flag;
			v3d_update_depth_ez_dir(context);
			break;
		case GL_DITHER:
			context->Dither_State = flag;
			break;
		case GL_POINT_SMOOTH:
			context->PointSmooth_State = flag;
			break;
		case MGL_PERSPECTIVE_MAPPING:
			/* No stored state even in the original, and no hardware
			 * toggle exists here -- true no-op, see header comment. */
			break;
		case MGL_Z_OFFSET:
			/* A flag of its own, separate from GL_POLYGON_OFFSET_FILL
			 * below: either one enables the hardware depth offset, but the
			 * two entry points carry different quantities -- see draw.c's
			 * `edo` block, which converts this one's mglSetZOffset delta and
			 * passes glPolygonOffset's units straight through. */
			context->ZOffset_State = flag;
			break;
		case GL_POLYGON_OFFSET_FILL:
			context->PolygonOffsetFill_State = flag;
			break;
		case MGL_ARRAY_TRANSFORMATIONS:
			context->VertexArrayPipeline = flag;
			break;
		/* State only -- see the comment on these fields in context.h for why
		 * each has nothing to drive. Named here so enabling them is not an
		 * error: applications enable the palette as a matter of course, and
		 * a pending error can end the run (see the default arm below). */
		case GL_POLYGON_OFFSET_LINE:
			context->PolygonOffsetLine_State = flag;
			break;
		case GL_POLYGON_OFFSET_POINT:
			context->PolygonOffsetPoint_State = flag;
			break;
		case GL_SHARED_TEXTURE_PALETTE_EXT:
			context->SharedTexturePalette_State = flag;
			break;
		default:
			/* A capability this driver does not implement, e.g.
			 * glEnable(GL_LIGHTING). glGetError reports it as
			 * GL_INVALID_ENUM; GLFlagError (gl.h) keeps an earlier error the
			 * application has not read yet, as GL specifies.
			 *
			 * No state changes and nothing is logged -- a game could make
			 * this call every frame.
			 *
			 * The switch above is meant to be exhaustive for the capabilities
			 * real applications enable: an application that checks glGetError
			 * may treat a pending error as fatal, so a capability falling
			 * through to here is not merely ignored, it can end the run. */
			GLFlagError(context, 1, GL_INVALID_ENUM);
			break;
	}
}
#undef ED

GLint mglGetSupportedScreenModes(MGLScreenModeCallback CallbackFn)
{
	/* No V3D equivalent of Warp3D's screen-mode enumeration -- see this
	 * file's header comment. */
	return MGL_SM_BESTMODE;
}

void *MGLCreateContextFromID(GLint id, GLint *width, GLint *height)
{
	/* No V3D equivalent of Warp3D's ID-based context creation -- see this
	 * file's header comment. MGLCreateContext (explicit w/h) is the real,
	 * ported path. */
	return NULL;
}

void MGLMinTriArea(GLcontext context, GLfloat area)
{
	context->MinTriArea = area;
}

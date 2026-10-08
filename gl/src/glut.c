/*
 * (C) 2025-2026 Dennis van der Boon
 *
 * GLUT lifecycle over MiniGLV3D.
 *
 * The mapping is close to one-to-one, with four places where it is not:
 *
 *  - GLUTMainLoop has no direct equivalent. MGLMainLoop drives an idle callback
 *    and a key callback; GLUT drives an idle callback and a SEPARATE display
 *    callback, the latter only when a redisplay has been posted. So MGLIdleFunc
 *    gets a trampoline here that runs the idle function every iteration and the
 *    display function only on a pending post -- see that function's own comment
 *    for why calling both unconditionally is wrong. GLUT's contract is that a
 *    display callback ends with GLUTSwapBuffers, which is mglSwitchDisplay, so
 *    the frame is submitted from inside the application's own code exactly as it
 *    would be under real GLUT.
 *
 *  - printf format strings use %ld with (long) casts, never %d. Under libnix %d
 *    consumes 16 bits, so a 32-bit int printed with %d reads half of the wrong
 *    argument -- "%dx%d:%d@%d" with 640,480,32,60 came out as "0x640:0@480".
 *    clib2's printf does not have this behaviour, and these files are compiled
 *    under both CRTs, so %ld is the spelling that is correct in both.
 *
 *  - GLUTKeyboardFunc's callback takes (key, x, y); the driver's KeyHandlerFn
 *    takes only the key. MiniGLV3D delivers no pointer position alongside a
 *    keystroke, so the trampoline passes x = y = 0.
 *
 *  - exit() from a keyboard callback is normal GLUT style and it never returns,
 *    so it would bypass MGLDeleteContext and MGLTerm. vid_CloseDisplay must run
 *    to close the backdrop input window before the screen closes, or the screen
 *    is left open after the program is gone. GLUTInit therefore registers
 *    mglglut_Cleanup with atexit, and the same function is called on the normal
 *    path, guarded so it runs exactly once either way.
 *
 *  - GLUT_DOUBLE is recorded, read, and then deliberately DOWNGRADED rather
 *    than refused. Double buffering is compiled out in this driver
 *    (MGLV3D_DOUBLE_BUFFER_ENABLED), so claiming to honour the flag by asking
 *    for two buffers would look like support that is not there, and
 *    GLUTSwapBuffers is a single-buffer frame submit. GLUT would fatal on an
 *    unservable mode; this is the one bit where refusing is worse than
 *    substituting, because every GLUT program asks for it. The window creation
 *    path says so once per run and glutGet(GLUT_WINDOW_DOUBLEBUFFER) answers 0.
 *    Every OTHER unservable bit is refused -- see MGLGLUT_MODE_REFUSED_BITS.
 *
 * No custom Intuition event loop: MGLMainLoop already calls ModifyIDCMP on the
 * input window itself, and already calls MGLFlushPendingRender before every
 * GetMsg so no bitmap lock is ever held across a library call. Reimplementing
 * that reopens a closed and expensive investigation.
 */

#include "sysinc.h"
#include <mgl/glut.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <graphics/displayinfo.h>
#include <cybergraphx/cybergraphics.h>

/* GLUTGet(GLUT_ELAPSED_TIME). ReadEClock resolves through TimerBase, which is
 * why it is a file-scope object rather than a local. */
#include <devices/timer.h>
#include <proto/timer.h>

/* E() and D(): the driver's diagnostic channel, kprintf-backed. */
#include "../../backend/hw/v3d_debug.h"

/* Registered callbacks. */
static void (*g_displayFunc)(void)                                 = NULL;
static void (*g_idleFunc)(void)                                    = NULL;
static void (*g_keyFunc)(unsigned char key, int x, int y)          = NULL;

/*
 * The pointer position GLUT reports alongside a keystroke. GLUT takes it from
 * the key event itself (glut_event.c:538 passes event.xkey.x/y straight to the
 * callback); AmigaOS keystrokes carry no position, so it is tracked from mouse
 * events instead and the last one seen is reported -- which is the same answer
 * whenever the pointer has not moved since, and the closest available otherwise.
 *
 * TOP-ORIGIN, client-relative, because that is what GLUT's callbacks use: those
 * X11 event coordinates count y DOWNWARD. MGLMouseFunc hands over GL window
 * coordinates, which count y UP, so mglglut_MouseTrampoline flips them. Zero
 * until the first mouse event, which is the honest unknown.
 */
static int g_mouseX = 0;
static int g_mouseY = 0;
static void (*g_reshapeFunc)(int width, int height)                = NULL;

/* Requested window/screen geometry. 640x480 is GLUT's own default. */
static int  g_initWidth   = 640;
static int  g_initHeight  = 480;
/* GLUT's own defaults for an unset position, so GLUTGet reports "the caller
 * never asked" rather than "the caller asked for the top-left corner". */
static int  g_initX       = -1;
static int  g_initY       = -1;
static unsigned int g_displayMode = GLUT_RGB;

/*
 * GLUTGameModeString results. -1 means the field was not specified, which is
 * GLUT's own value: GLUTGameModeGet returns -1 for WIDTH, HEIGHT, PIXEL_DEPTH and
 * REFRESH_RATE whenever no mode has been selected, and -1 from its default case
 * too. Zero would be indistinguishable from a genuine 0 and from "unknown query".
 * Nothing downstream needs to special-case it: mglglut_ModeExists already rejects
 * a non-positive size, and GLUTEnterGameMode's guard is already `> 0`.
 */
static int  g_gmWidth     = -1;
static int  g_gmHeight    = -1;
static int  g_gmBpp       = -1;
static int  g_gmRefresh   = -1;

static int  g_gameModeActive = 0;
static int  g_haveContext    = 0;
static int  g_cleanupDone    = 0;
static int  g_redisplayPosted = 0;

/*
 * Once-per-run notices. FILE SCOPE rather than function-local statics, and that
 * is the point of them: a function-local `static int said` is not per-process in
 * a library build either, so it would latch for the lifetime of the resident
 * library and the second program to run would be told nothing. GLUTInit resets
 * these with everything else, for the reason its own comment gives.
 */
static int  g_saidBadQuery    = 0;
static int  g_saidModeRefused = 0;
static int  g_saidModeDropped = 0;
static int  g_saidBadGameMode = 0;

/* GLUTLeaveGameMode has asked the main loop to finish. Separate from
 * g_gameModeActive because the two answer different questions: that one is
 * false the instant the caller leaves, while this stays true until the loop has
 * actually returned and the display is gone. GLUTMainLoop reads it to refuse a
 * loop that was entered after the leave rather than before it. */
static int  g_leavePending    = 0;

/*
 * Work owed to the application before its first display callback, deferred out of
 * window creation and into the main loop.
 *
 * GLUT does not call the reshape callback from inside GLUTCreateWindow -- it only
 * marks the window (`window->forceReshape = True;` in __glutCreateWindow) and
 * delivers the reshape from GLUTMainLoop's work pass, which is the only ordering
 * that works for a canonical GLUT program: GLUTCreateWindow comes FIRST and
 * GLUTReshapeFunc after it, so a callback invoked during creation is always the
 * one the application has not registered yet.
 *
 * GLUT also sets the redisplay bit as a consequence of that first forced reshape,
 * which is why the same flag covers the implicit first display. An application
 * that animates only from its display callback and never posts a redisplay itself
 * therefore still draws.
 */
static int  g_initWorkPending = 0;

/*
 * The clock behind GLUTGet(GLUT_ELAPSED_TIME).
 *
 * timer.device at UNIT_MICROHZ with ReadEClock is the fine-grained source; the
 * EClock frequency is read once so the tick difference can be scaled without a
 * division per call. ReadEClock costs roughly 26 us on PiStorm, so one read per
 * frame is negligible against a frame of tens of milliseconds -- but it is not
 * free, which is why nothing here reads it speculatively.
 *
 * DateStamp is the fallback and not merely a nicety: an application pacing
 * itself on this value would FREEZE if the clock never advanced, so a coarse
 * 20 ms tick that keeps moving is much better than a perfect one that is absent.
 */
struct Device *TimerBase = NULL;           /* ReadEClock resolves through this */
static struct MsgPort     *g_timerPort = NULL;
static struct timerequest *g_timerIO   = NULL;
static ULONG               g_eclockFreq = 0;
static struct EClockVal    g_eclockBase;
static ULONG               g_dsBaseMs   = 0;
static int                 g_clockReady = 0;
/*
 * g_clockShut latches once mglglut_ClockClose has run, so a query after teardown
 * cannot reopen the device and re-latch the time origin. g_lastElapsedMs holds the
 * final real reading to answer with instead of a restarted ~0.
 */
static int                 g_clockShut  = 0;
static int                 g_lastElapsedMs = 0;

/* Milliseconds since midnight from DateStamp, for the fallback path. */
static ULONG mglglut_DateStampMs(void)
{
	struct DateStamp ds;

	DateStamp(&ds);
	/* ds_Minute is minutes since midnight, ds_Tick is 1/50 s within the
	 * minute. Wrapping at midnight is acceptable: only DIFFERENCES are used,
	 * and a single wrapped frame costs one bad delta, not a bad clock. */
	return (ULONG)ds.ds_Minute * 60000UL + (ULONG)ds.ds_Tick * 20UL;
}

static void mglglut_ClockOpen(void)
{
	if (g_clockReady)
		return;

	if ((g_timerPort = CreateMsgPort()) != NULL)
	{
		g_timerIO = (struct timerequest *)CreateIORequest(g_timerPort, sizeof(struct timerequest));
		if (g_timerIO != NULL)
		{
			if (OpenDevice(TIMERNAME, UNIT_MICROHZ, (struct IORequest *)g_timerIO, 0) == 0)
				TimerBase = (struct Device *)g_timerIO->tr_node.io_Device;
		}
	}

	if (TimerBase != NULL)
		g_eclockFreq = ReadEClock(&g_eclockBase);

	if (g_eclockFreq == 0)
		g_dsBaseMs = mglglut_DateStampMs();    /* fallback, see above */

	g_clockReady = 1;
}

static void mglglut_ClockClose(void)
{
	if (TimerBase != NULL)
	{
		CloseDevice((struct IORequest *)g_timerIO);
		TimerBase = NULL;
	}
	if (g_timerIO != NULL)  { DeleteIORequest((struct IORequest *)g_timerIO); g_timerIO = NULL; }
	if (g_timerPort != NULL) { DeleteMsgPort(g_timerPort); g_timerPort = NULL; }
	g_eclockFreq = 0;
	g_clockReady = 0;
	g_clockShut  = 1;
}

/* GLUT_ELAPSED_TIME's answer -- see this function's own comments. */
static int mglglut_ElapsedMs(void)
{
	/*
	 * ONCE THE CLOCK HAS BEEN SHUT DOWN, ANSWER FROM THE CACHE AND DO NOT REOPEN.
	 *
	 * mglglut_ClockClose clears g_clockReady, so without this the `if
	 * (!g_clockReady)` below would reopen a MsgPort, an IORequest and timer.device
	 * on any query after teardown -- and mglglut_Cleanup's own g_cleanupDone guard
	 * means nothing would ever close them again. Worse than the leak, reopening
	 * re-latches the time origin, so a post-teardown reading restarts from about
	 * zero and an atexit handler that reports elapsed time would print nonsense.
	 *
	 * GLUT cannot have this problem: __glutInitTime latches its origin once behind
	 * a `static int beenhere` and GLUTGet computes now - beginning from the OS
	 * clock, with nothing to open or close.
	 */
	if (g_clockShut)
		return g_lastElapsedMs;

	if (!g_clockReady)
		mglglut_ClockOpen();

	if (g_eclockFreq != 0)
	{
		struct EClockVal now;
		double           secs;

		ReadEClock(&now);

		/*
		 * The EClock is 64 bits as a hi/lo pair. BOTH halves are differenced,
		 * so the result stays correct once the low word wraps -- at PAL's
		 * 709379 Hz that happens about every 100 minutes, which a long demo
		 * run can reach.
		 *
		 * The arithmetic goes through a double rather than integers because
		 * ticks*1000 overflows 32 bits after roughly 6 seconds at that rate,
		 * while dividing first would throw away all the sub-second precision
		 * this clock exists to provide. Each half is cast BEFORE subtracting,
		 * so an unsigned borrow cannot wrap the intermediate. One FPU divide
		 * per frame is the price.
		 */
		secs = ((double)now.ev_hi - (double)g_eclockBase.ev_hi) * 4294967296.0
		     + ((double)now.ev_lo - (double)g_eclockBase.ev_lo);
		secs /= (double)g_eclockFreq;

		g_lastElapsedMs = (int)(secs * 1000.0);
		return g_lastElapsedMs;
	}

	g_lastElapsedMs = (int)(mglglut_DateStampMs() - g_dsBaseMs);
	return g_lastElapsedMs;
}

/*
 * glutGet. Every query GLUT 3.7 defines.
 *
 * THE TOKENS ARE NAMED, from <mgl/glut.h>. Every value is
 * glut-master/include/GL/glut.h's, and GLUT_ELAPSED_TIME being
 * 0x02BC == 700 here is what shows the numbering is GLUT's own.
 *
 * AN UNRECOGNISED QUERY RETURNS -1, which is both GLUT's answer
 * (glut_get.c:239, "invalid glutGet parameter") and this file's own convention:
 * GLUTGameModeGet already returns -1 where no value has been selected,
 * precisely because 0 cannot be told apart from a real reading. The warning is
 * once per run rather than GLUT's once per call, because a query inside a
 * render loop would otherwise flood the serial line.
 *
 * A ZERO HERE IS A FACT, NOT A PLACEHOLDER. No stencil, no accumulation buffer,
 * no stereo, no multisampling, no menus and no subwindows exist in this driver,
 * so those queries answer 0 because that is the truth about the drawable.
 */



/*
 * The display-mode bits, by mgl/glut.h's own values, split by what becomes of
 * each one. GLUT reads the mode word when it creates the window.
 *
 * REFUSED -- INDEX 0x0001, ACCUM 0x0004, STENCIL 0x0020, STEREO 0x0100,
 * LUMINANCE 0x0200. No surface here can serve them and GLUT has no fallback
 * either: getVisualInfoRGB asks GLX for the capability outright, and
 * __glutGetVisualInfo refuses LUMINANCE before it even looks
 * (glut_win.c:368, "GLUT_LUMINANCE not implemented"). GLUT's answer is
 * __glutFatalError("visual with necessary capabilities not found")
 * at glut_win.c:520; window creation here returns 0 instead, which is gentler
 * than exit and is already this file's answer for a second window.
 *
 * DROPPED -- DOUBLE 0x0002 and MULTISAMPLE 0x0080. Accepted, dropped, and
 * discoverable afterwards through glutGet. MULTISAMPLE is GLUT'S OWN fallback:
 * __glutDetermineVisual clears the bit and retries, leaving the application to
 * query GLUT_WINDOW_NUM_SAMPLES (glut_win.c:406-415), which answers 0 here.
 * DOUBLE is this driver's one deliberate divergence -- GLUT would fatal, but
 * double buffering is compiled out of the whole driver and essentially every
 * GLUT program asks for it, so refusing would leave the shim unusable.
 * GLUT_WINDOW_DOUBLEBUFFER answers 0 to say what the drawable really is.
 *
 * SERVED -- RGB, RGBA and SINGLE are all 0x0000; ALPHA because the surface is
 * PIXFMT_BGRA32; DEPTH because a z-buffer is always allocated. The ABSENCE of a
 * bit is never an error: GLX asks for a MINIMUM of each capability and a visual
 * carrying more satisfies it, so a drawable with depth and alpha nobody asked
 * for is conformant. Unknown bits are ignored, as GLUT ignores them -- it only
 * ever tests the ones it knows.
 */
#define MGLGLUT_MODE_REFUSED_BITS (GLUT_INDEX | GLUT_ACCUM | GLUT_STENCIL | \
                                   GLUT_STEREO | GLUT_LUMINANCE)
#define MGLGLUT_MODE_DROPPED_BITS (GLUT_DOUBLE | GLUT_MULTISAMPLE)

/* What glutGet(GLUT_DISPLAY_MODE_POSSIBLE) tests: everything GLUT has no
 * fallback for, which is the refused set plus GLUT_DOUBLE. MULTISAMPLE is
 * excluded DELIBERATELY -- GLUT answers 1 for it, because that query runs the
 * same __glutDetermineVisual that drops the bit. Deriving this mask from the
 * refused one is what keeps the query and the refusal from drifting apart. */
#define MGLGLUT_UNSERVABLE_MODE_BITS (MGLGLUT_MODE_REFUSED_BITS | \
                                      GLUT_DOUBLE)

static const struct { unsigned int bit; const char *name; } g_modeBitNames[] = {
	{ GLUT_INDEX,       "GLUT_INDEX"       },
	{ GLUT_DOUBLE,      "GLUT_DOUBLE"      },
	{ GLUT_ACCUM,       "GLUT_ACCUM"       },
	{ GLUT_STENCIL,     "GLUT_STENCIL"     },
	{ GLUT_MULTISAMPLE, "GLUT_MULTISAMPLE" },
	{ GLUT_STEREO,      "GLUT_STEREO"      },
	{ GLUT_LUMINANCE,   "GLUT_LUMINANCE"   }
};

/*
 * Spell a bit set, so a refusal says GLUT_STENCIL rather than 0x20.
 *
 * Copied by hand rather than with strncat: this file is compiled into the
 * SHARED LIBRARY as well as into the archive, and the library links no string
 * functions at all. The archive build takes strlen and strncat happily,
 * because a demo links a full CRT and the library does not.
 */
static void mglglut_NameModeBits(unsigned int bits, char *buf, int size)
{
	int n = 0, i;

	for (i = 0; i < (int)(sizeof(g_modeBitNames) / sizeof(g_modeBitNames[0])); i++)
	{
		const char *s;

		if (!(bits & g_modeBitNames[i].bit))
			continue;
		if (n && n < size - 1)
			buf[n++] = ' ';
		for (s = g_modeBitNames[i].name; *s && n < size - 1; s++)
			buf[n++] = *s;
	}
	if (!n)
	{
		const char *s = "none";

		for (; *s && n < size - 1; s++)
			buf[n++] = *s;
	}
	buf[n] = '\0';
}

/*
 * Read the display mode, at the moment GLUT reads it. Returns 0 to refuse the
 * window.
 *
 * Both creation paths call this BEFORE either touches mglChooseWindowMode: a
 * refused call must not leave the window-mode flag flipped, which is the same
 * ordering requirement the one-window check has.
 */
static int mglglut_ApplyDisplayMode(const char *who)
{
	char names[128];
	unsigned int refused = g_displayMode & MGLGLUT_MODE_REFUSED_BITS;
	unsigned int dropped = g_displayMode & MGLGLUT_MODE_DROPPED_BITS;

	if (refused)
	{
		mglglut_NameModeBits(refused, names, sizeof(names));
		E(("%s: the display mode asks for %s, which this driver cannot "
		       "serve -- no window created, and glutGet(GLUT_DISPLAY_MODE_"
		       "POSSIBLE) already answers 0 for this mode\n", who, names));
		return 0;
	}

	if (dropped && !g_saidModeDropped)
	{
		g_saidModeDropped = 1;
		mglglut_NameModeBits(dropped, names, sizeof(names));
		D(("%s: the display mode asks for %s, which the drawable does not "
		       "provide -- the request is dropped and glutGet reports 0 for it "
		       "(said once per run)\n", who, names));
	}
	return 1;
}

/* The client area's top-left in screen coordinates, or 0 where there is no
 * window to ask. A fullscreen context draws into a BACKDROP window whose
 * borders are zero, so the arithmetic reduces to the window's own origin
 * there -- the same reduction MGLMouseFunc's one formula relies on. */
static void mglglut_ClientOrigin(int *x, int *y)
{
	struct Window *w;

	*x = 0;
	*y = 0;
	if (!mini_CurrentContext)
		return;
	w = (struct Window *)MGLGetWindowHandle(mini_CurrentContext);
	if (!w)
		return;
	*x = (int)w->LeftEdge + (int)w->BorderLeft;
	*y = (int)w->TopEdge  + (int)w->BorderTop;
}

int GLUTGet(GLenum state)
{
	int x, y;

	switch ((int)state)
	{
		/* ---- the init state, answered whether or not a window exists ---- */
		case GLUT_INIT_WINDOW_X:      return g_initX;
		case GLUT_INIT_WINDOW_Y:      return g_initY;
		case GLUT_INIT_WINDOW_WIDTH:  return g_initWidth;
		case GLUT_INIT_WINDOW_HEIGHT: return g_initHeight;
		case GLUT_INIT_DISPLAY_MODE:  return (int)g_displayMode;

		/* ---- the drawable ---- */
		case GLUT_WINDOW_WIDTH:
			return mini_CurrentContext
			       ? (int)mini_CurrentContext->backend.width : 0;
		case GLUT_WINDOW_HEIGHT:
			return mini_CurrentContext
			       ? (int)mini_CurrentContext->backend.height : 0;

		case GLUT_WINDOW_X:
			mglglut_ClientOrigin(&x, &y);
			return x;
		case GLUT_WINDOW_Y:
			mglglut_ClientOrigin(&x, &y);
			return y;

		/* PIXFMT_BGRA32 throughout: 8 bits each and 32 to the pixel. The
		 * driver never opens any other surface format -- vid_OpenDisplay's
		 * depth ladder varies the SCREEN depth it asks for, not this. */
		case GLUT_WINDOW_BUFFER_SIZE: return 32;
		case GLUT_WINDOW_RED_SIZE:    return 8;
		case GLUT_WINDOW_GREEN_SIZE:  return 8;
		case GLUT_WINDOW_BLUE_SIZE:   return 8;
		case GLUT_WINDOW_ALPHA_SIZE:  return 8;
		case GLUT_WINDOW_RGBA:        return 1;

		/* Set by mglChooseZBufferDepth before context creation and fixed for
		 * the context's life, so the backend's value is the drawable's. */
		case GLUT_WINDOW_DEPTH_SIZE:
			return mini_CurrentContext
			       ? mini_CurrentContext->backend.zbuffer_bits : 0;

		/* Facts about this driver, not unimplemented queries. */
		case GLUT_WINDOW_STENCIL_SIZE:  return 0;
		case GLUT_WINDOW_ACCUM_RED_SIZE:     return 0;
		case GLUT_WINDOW_ACCUM_GREEN_SIZE:   return 0;
		case GLUT_WINDOW_ACCUM_BLUE_SIZE:    return 0;
		case GLUT_WINDOW_ACCUM_ALPHA_SIZE:   return 0;
		case GLUT_WINDOW_NUM_SAMPLES:   return 0;
		case GLUT_WINDOW_STEREO:        return 0;
		/* OUR CHOICE, not GLUT's answer: GLUT dereferences
		 * __glutCurrentMenu unconditionally (glut_get.c:176), so it has no
		 * defined behaviour with no menu -- it faults. There are no menus
		 * here at all, so 0 items is the truth and cannot be mistaken for
		 * one. */
		case GLUT_MENU_NUM_ITEMS:       return 0;

		/* One window, no subwindows: GLUT returns the parent's number + 1, so
		 * 0 means "top level" there too. */
		case GLUT_WINDOW_PARENT:        return 0;
		case GLUT_WINDOW_NUM_CHILDREN:  return 0;

		/* GLUT returns 0 for an RGBA visual, which this always is. */
		case GLUT_WINDOW_COLORMAP_SIZE: return 0;

		case GLUT_WINDOW_CURSOR:        return GLUT_CURSOR_INHERIT;

		/* NOT Win32-only: GLUT returns the X
		 * visual's visualid outside Win32 and GetPixelFormat inside it
		 * (glut_get.c:229-237), so it is an opaque per-format identifier
		 * either way. The AmigaOS counterpart is the screen's DISPLAY MODE
		 * ID, which is exactly that -- an opaque id naming the pixel format
		 * and geometry, and the same currency MGLCreateContextFromID takes. */
		case GLUT_WINDOW_FORMAT_ID:
		{
			struct Window *w = mini_CurrentContext
			    ? (struct Window *)MGLGetWindowHandle(mini_CurrentContext)
			    : NULL;

			if (!w || !w->WScreen)
				return 0;
			return (int)GetVPModeID(&w->WScreen->ViewPort);
		}

		/* ---- the screen the window is on ---- */
		case GLUT_SCREEN_WIDTH:
		case GLUT_SCREEN_HEIGHT:
		{
			struct Window *w = mini_CurrentContext
			    ? (struct Window *)MGLGetWindowHandle(mini_CurrentContext)
			    : NULL;

			if (!w || !w->WScreen)
				return 0;
			return ((int)state == GLUT_SCREEN_WIDTH)
			       ? (int)w->WScreen->Width : (int)w->WScreen->Height;
		}

		/* GLUT takes these from DisplayWidthMM/DisplayHeightMM, the X
		 * server's claim about the physical panel. AmigaOS publishes no such
		 * figure, so there is nothing to convert and 0 is the answer. Stated
		 * as OUR answer: I do not know what X returns on a server that has no
		 * real value. */
		case GLUT_SCREEN_WIDTH_MM:  return 0;
		case GLUT_SCREEN_HEIGHT_MM: return 0;

		/* Whether a visual matching the CURRENT glutInitDisplayMode exists.
		 * GLUT asks glXChooseVisual; here it is a bit test, because the set of
		 * servable modes is fixed. Window creation refuses on a mask
		 * DERIVED from this one, so the query and the refusal cannot disagree.
		 *
		 * The one place they read differently is GLUT_DOUBLE: this answers 0,
		 * which is both true and GLUT's answer, yet glutCreateWindow still
		 * succeeds and downgrades. That is the documented divergence, not an
		 * inconsistency -- the mode asked for is genuinely unavailable, and the
		 * caller is told what it got instead. */
		case GLUT_DISPLAY_MODE_POSSIBLE:
			return (g_displayMode & MGLGLUT_UNSERVABLE_MODE_BITS) ? 0 : 1;

		/* Double buffering is compiled out of the shipped library, so a
		 * request for it is accepted and downgraded. This reports what the
		 * drawable IS, which is what GLUT's glXGetConfig(GLX_DOUBLEBUFFER)
		 * reports -- deliberately not what was asked for. glDrawBuffer and
		 * glReadBuffer answer the other way round, honouring the REQUEST,
		 * because a name that was asked for has to keep meaning something. */
		case GLUT_WINDOW_DOUBLEBUFFER:  return 0;

		case GLUT_ELAPSED_TIME:
			return mglglut_ElapsedMs();

		default:
			break;
	}

	/* GLUT warns and returns -1 per call (glut_get.c:239). Once per run here:
	 * a query in a render loop would flood the serial line, and the first one
	 * names the parameter, which is all the caller needs to find it. */
	if (!g_saidBadQuery)
	{
		g_saidBadQuery = 1;
		E(("GLUTGet: unrecognised query %ld, returning -1 "
		       "(said once per run)\n", (long)state));
	}
	return -1;
}

/*
 * Teardown, and the only place MGLDeleteContext and MGLTerm are called. Runs
 * either from atexit (the application called exit) or from GLUTMainLoop's
 * return (the application stopped the loop cleanly). The guard makes the second
 * call a no-op rather than a double free.
 */
static void mglglut_Cleanup(void)
{
	if (g_cleanupDone)
		return;
	g_cleanupDone = 1;

	if (g_haveContext)
	{
		MGLDeleteContext(mini_CurrentContext);
		/*
		 * MGLDeleteContext takes the context as a PARAMETER and never touches the
		 * global, so without this the pointer that every gl* macro in gl.h
		 * dereferences would still address freed memory. GLUT keeps the same
		 * discipline -- destroyWindow unbinds and NULLs __glutCurrentWindow before
		 * destroying the context -- and so does this driver elsewhere, MGLTerm
		 * nulling each library base as it closes it.
		 */
		mini_CurrentContext = NULL;
		g_haveContext = 0;
	}
	MGLTerm();

	/* After MGLTerm: this is the last thing the shim owns, and closing a device
	 * cannot fail in a way that would leave the display open. */
	mglglut_ClockClose();
}

void GLUTInit(int *argcp, char **argv)
{
	(void)argcp;
	(void)argv;

	/*
	 * RESET EVERY STATIC IN THIS FILE FIRST, and this is not defensive tidiness --
	 * it is required, because these statics are NOT per-process.
	 *
	 * This file lives in minigl.library, and a library stays resident after its
	 * last client closes it. So a second program, or a second run of the same one
	 * in the same shell, starts with whatever the previous one left here. In the
	 * static-archive build they die with the process and none of this matters.
	 *
	 * A client that exits through exit() runs the APPLICATION's atexit handlers --
	 * the client-side one that releases the display -- while mglglut_Cleanup below
	 * is registered with the library's own atexit shim and never runs at all. So
	 * without this reset g_haveContext is still 1 for the next client, and a
	 * fullscreen run after a windowed one refuses with "GLUTEnterGameMode: a
	 * window already exists" -- correct about stale state rather than wrong about
	 * live state.
	 *
	 * Resetting here rather than trusting any teardown is what makes it robust:
	 * however the last client left, this one starts clean.
	 */
	g_displayFunc = NULL;
	g_idleFunc    = NULL;
	g_keyFunc     = NULL;
	g_mouseX      = 0;
	g_mouseY      = 0;
	g_reshapeFunc = NULL;

	g_initWidth   = 640;
	g_initHeight  = 480;
	g_displayMode = GLUT_RGB;

	g_gmWidth   = -1;
	g_gmHeight  = -1;
	g_gmBpp     = -1;
	g_gmRefresh = -1;

	g_gameModeActive  = 0;
	g_haveContext     = 0;
	g_cleanupDone     = 0;
	g_redisplayPosted = 0;
	g_initWorkPending = 0;
	g_lastElapsedMs   = 0;

	/* The once-per-run notices, for the same reason as everything above: a
	 * latched flag in a resident library would silence the second program. */
	g_saidBadQuery    = 0;
	g_saidModeRefused = 0;
	g_saidModeDropped = 0;
	g_saidBadGameMode = 0;
	g_leavePending    = 0;

	/*
	 * g_clockShut MUST be reset:
	 * ClockClose latches it, and GLUTGet tests it BEFORE it tests g_clockReady --
	 * so a second run would return the previous run's last elapsed time forever
	 * and the whole timeline would sit frozen. ClockOpen reopens happily, because
	 * ClockClose also cleared g_clockReady.
	 */
	g_clockShut = 0;

	/* g_timerPort, g_timerIO and g_clockReady are NOT reset: they are OS resources
	 * and their liveness flag, so zeroing them here would leak a device and a port
	 * if a previous client still had them open. ClockOpen no-ops when ready. */

	/* Registered BEFORE MGLInit, so an exit() between here and
	 * GLUTCreateWindow still tears down whatever was opened. In a library build
	 * this reaches the library's atexit shim rather than the application's, which
	 * is why the reset above cannot depend on it having run. */
	atexit(mglglut_Cleanup);

	/*
	 * THE VERTEX BUFFER MUST BE SIZED BEFORE THE CONTEXT IS CREATED, and for a
	 * GLUT application the default is not enough.
	 *
	 * The default is 256 entries of 92 bytes, i.e. 23,552 bytes, and
	 * immediate-mode GLUT code routinely emits far more in one glBegin/glEnd
	 * block -- a 64x64 quad mesh is 63*63*4 = 15,876 vertices, which needs
	 * 1,460,592 bytes.
	 *
	 * GLVertex4f bound-checks and flags GL_OUT_OF_MEMORY, so exceeding the
	 * buffer does not write past the allocation. Sizing it properly is required
	 * rather than optional -- the guard stops the corruption, it does not draw
	 * the vertices, so an undersized buffer silently loses geometry instead.
	 *
	 * 65,536 entries costs roughly 11.5 MB across everything this call sizes:
	 * VertexBuffer 92*65,536 = 6,029,312, WBuffer 64*65,536 = 4,194,304,
	 * NormalBuffer 16*65,536 = 1,048,576, plus the three element arrays and
	 * s_seq. Stated because it is a large number; PiStorm is not memory
	 * restricted, and spending RAM here to avoid a heap overrun is the right
	 * trade.
	 *
	 * It must precede MGLCreateContext, which is where the buffers are
	 * allocated, so GLUTInit is the earliest and safest place for it.
	 */
	mglChooseVertexBufferSize(65536);

	/* Before MGLInit, so GLUTGet(GLUT_ELAPSED_TIME) has a base the moment any
	 * application code can ask for it, and so the zero point is GLUTInit rather
	 * than whenever the first query happened to land. */
	mglglut_ClockOpen();

	if (MGLInit() == GL_FALSE)
	{
		E(("GLUTInit: MGLInit failed\n"));
		exit(20);
	}
}

void GLUTInitDisplayMode(unsigned int mode)
{
	/*
	 * STORED VERBATIM, which is all GLUT does here too -- glutInitDisplayMode
	 * is one assignment to __glutDisplayMode (glut_init.c:546-549) and the mode
	 * is not examined until a window is created. Storing it unchanged is also
	 * what lets glutGet(GLUT_INIT_DISPLAY_MODE) report what was ASKED FOR
	 * rather than what will be served, as GLUT's does.
	 *
	 * The notice is early on purpose. GLUT says nothing at this call and fatals
	 * at the next one; this message carries the caller's own argument, so it
	 * points at the line that chose the mode rather than at the line that
	 * discovered the consequence.
	 */
	g_displayMode = mode;

	if ((mode & MGLGLUT_MODE_REFUSED_BITS) && !g_saidModeRefused)
	{
		char names[128];

		g_saidModeRefused = 1;
		mglglut_NameModeBits(mode & MGLGLUT_MODE_REFUSED_BITS,
		                     names, sizeof(names));
		D(("GLUTInitDisplayMode: %s cannot be served by this driver -- "
		       "window creation will refuse this mode (said once per run)\n",
		       names));
	}
}

void GLUTInitWindowSize(int width, int height)
{
	if (width  > 0) g_initWidth  = width;
	if (height > 0) g_initHeight = height;
}

void GLUTInitWindowPosition(int x, int y)
{
	/* MiniGLV3D opens a fullscreen screen or a backdrop window; neither takes
	 * a caller-chosen position, so this still cannot MOVE anything. The value
	 * is RECORDED rather than discarded, because GLUTGet reports it back
	 * (GLUT_INIT_WINDOW_X/Y) and GLUT's own glutGet answers with whatever was
	 * set, not with where the window landed. */
	g_initX = x;
	g_initY = y;
}

int GLUTCreateWindow(const char *title)
{
	(void)title;

	/*
	 * ONE WINDOW, AND A SECOND CALL IS REFUSED -- the mirror of
	 * GLUTEnterGameMode's refusal, for the same reason and with the same answer.
	 * Without the refusal a second call runs straight through: it reassigns
	 * mini_CurrentContext and LEAKS the first context along with its window or
	 * screen, while returning 1 both times, so the caller believes it holds two
	 * windows that both have id 1. Real GLUT hands out increasing ids and keeps
	 * every window; this shim has one context and no glutDestroyWindow or
	 * glutSetWindow to manage others with, so 0 -- the failure value both
	 * references use -- is the honest answer.
	 *
	 * g_haveContext covers BOTH origins, so a window after game mode is refused
	 * as well as a window after a window.
	 *
	 * The check comes before mglChooseWindowMode on purpose, exactly as game
	 * mode's does: the refused call must not flip the window-mode flag under a
	 * live context, which for a game-mode context would be the fullscreen one.
	 */
	if (g_haveContext)
	{
		E(("GLUTCreateWindow: only one window is supported -- the existing "
		       "one is kept and this call does nothing\n"));
		return 0;
	}

	/* The display mode is read HERE, which is where GLUT reads it, and a mode
	 * this driver cannot serve is refused rather than silently substituted. */
	if (!mglglut_ApplyDisplayMode("GLUTCreateWindow"))
		return 0;

	/*
	 * A GLUT window is a WINDOW. MiniGLV3D opens its own fullscreen screen
	 * unless told otherwise, so without this the windowed path silently gave a
	 * fullscreen display -- which also destroys windowed-versus-fullscreen as a
	 * way to tell the two driver paths apart, since they share almost nothing:
	 * vid_OpenWindow does LockPubScreen plus AllocBitMap on the existing
	 * screen, while vid_OpenDisplay opens a screen of its own.
	 */
	mglChooseWindowMode(GL_TRUE);

	mini_CurrentContext = MGLCreateContext(0, 0, g_initWidth, g_initHeight);
	if (!mini_CurrentContext)
	{
		E(("GLUTCreateWindow: MGLCreateContext(%ld,%ld) failed\n",
		       (long)g_initWidth, (long)g_initHeight));
		return 0;
	}
	g_haveContext = 1;

	/*
	 * The reshape is OWED, not delivered here. Calling g_reshapeFunc from inside
	 * window creation can only ever reach a callback registered BEFORE the window
	 * existed, and the canonical GLUT order is the opposite -- GLUTCreateWindow
	 * first, GLUTReshapeFunc after it -- so the callback is reliably NULL at this
	 * point and the reshape is silently dropped. GLUT itself only marks the
	 * window here and delivers from GLUTMainLoop's work pass; see the trampoline.
	 */
	g_initWorkPending = 1;

	/* GLUT window identifiers start at 1 and 0 means failure. One window. */
	return 1;
}

void GLUTDisplayFunc(void (*func)(void))                        { g_displayFunc = func; }
void GLUTIdleFunc(void (*func)(void))                           { g_idleFunc    = func; }
void GLUTKeyboardFunc(void (*func)(unsigned char, int, int))    { g_keyFunc     = func; }
void GLUTReshapeFunc(void (*func)(int, int))                    { g_reshapeFunc = func; }

void GLUTPostRedisplay(void)
{
	g_redisplayPosted = 1;
}

void GLUTSwapBuffers(void)
{
	mglSwitchDisplay();
}

/*
 * MGLIdleFunc trampoline, and the one place GLUT's two-callback model is mapped
 * onto MGLMainLoop's single one.
 *
 * GLUT's contract: the idle callback runs whenever there is nothing else to do,
 * and the display callback runs ONLY when a redisplay has been posted -- by
 * GLUTPostRedisplay, or once when the window first appears. It is NOT called
 * every iteration.
 *
 * Honouring that exactly matters, because an application may legitimately
 * register the SAME function as both. A client application does: it registers
 * scenemanagement as idle and as display, posts one redisplay before entering
 * the loop, and never posts another. Calling both unconditionally would run that
 * function twice per iteration, advancing its timeline at double speed and
 * drawing every frame twice -- two GLUTSwapBuffers per iteration.
 *
 * So: idle every iteration, display only on a pending post, and the post is
 * consumed. An application that animates from idle alone therefore works, and
 * one that animates by posting from idle also works.
 */
static void mglglut_IdleTrampoline(void)
{
	/*
	 * The work owed from window creation, delivered on the first iteration and
	 * BEFORE the idle callback -- GLUT's window work pass likewise precedes its
	 * idle call. By now the application has returned from GLUTCreateWindow and
	 * registered its callbacks, so a reshape handler actually receives this.
	 *
	 * Setting the redisplay flag here is GLUT's own consequence of that first
	 * forced reshape, not an extra: it is what makes the display callback run once
	 * without the application posting anything. An application that DOES post
	 * before entering the loop, as a client application does, collapses into the same boolean
	 * and still gets exactly one display -- the trampoline consumes the flag.
	 */
	if (g_initWorkPending)
	{
		g_initWorkPending = 0;
		if (g_reshapeFunc)
			g_reshapeFunc(g_initWidth, g_initHeight);
		g_redisplayPosted = 1;
	}

	if (g_idleFunc)
		g_idleFunc();

	if (g_redisplayPosted)
	{
		g_redisplayPosted = 0;
		if (g_displayFunc)
			g_displayFunc();
	}
}

/*
 * Records the pointer so the keyboard callback can report it. The flip is the
 * whole substance: MGLMouseFunc delivers GL window coordinates, y up from the
 * bottom of the client area, and GLUT's callbacks want y down from the top.
 *
 * This is also where a glutMouseFunc or glutMotionFunc would hook in if either
 * is ever added; neither exists yet, so the position is recorded and nothing
 * else is dispatched.
 */
static void mglglut_MouseTrampoline(GLint x, GLint y, GLbitfield buttons)
{
	(void)buttons;

	g_mouseX = (int)x;
	if (mini_CurrentContext != NULL)
		g_mouseY = (int)mini_CurrentContext->backend.height - 1 - (int)y;
	else
		g_mouseY = (int)y;
}

static void mglglut_KeyTrampoline(char key)
{
	if (g_keyFunc)
		g_keyFunc((unsigned char)key, g_mouseX, g_mouseY);
}

void GLUTMainLoop(void)
{
	if (!g_haveContext)
	{
		E(("GLUTMainLoop: no window -- call GLUTCreateWindow first\n"));
		return;
	}

	/*
	 * Game mode was left BEFORE the loop started, so there is nothing to run.
	 * GLUT makes this a fatal usage error -- glutLeaveGameMode destroys the only
	 * window and glutMainLoop then reports "main loop entered with no windows
	 * created" (glut_event.c:1406). Here the context outlives the request until
	 * the loop returns, so g_haveContext is still set and the pending flag is
	 * what has to be tested. Without this, MGLMainLoop would set Running TRUE
	 * again on entry and spin on a display the caller was told is gone.
	 */
	if (g_leavePending)
	{
		E(("GLUTMainLoop: game mode was left before the loop started -- "
		       "there is no window to run\n"));
		mglglut_Cleanup();
		return;
	}

	MGLIdleFunc(mini_CurrentContext, mglglut_IdleTrampoline);
	MGLKeyFunc(mini_CurrentContext, mglglut_KeyTrampoline);
	/* Not for a GLUT mouse callback -- there is none -- but so the keyboard
	 * callback has a pointer position to report. */
	MGLMouseFunc(mini_CurrentContext, mglglut_MouseTrampoline);

	MGLMainLoop(mini_CurrentContext);

	/* Reached when the application stopped the loop rather than calling exit():
	 * mglExit, or GLUTLeaveGameMode, which is the one that needs the cleanup to
	 * be HERE. Real GLUT's GLUTMainLoop never returns; this one does, and
	 * closing the display at this point is what lets game mode be left from
	 * inside a callback without freeing the context under the loop. */
	mglglut_Cleanup();
}

/*
 * "WIDTHxHEIGHT:BPP@REFRESH", every field optional, which is GLUT's own format.
 * Parsed with strtol rather than sscanf so a missing field leaves its variable
 * at -1 instead of consuming the following separator.
 *
 * -1 rather than 0 for an unspecified field, matching GLUT: it is what
 * GLUTGameModeGet answers when no mode has been selected, and unlike 0 it cannot
 * be confused with a real value or with an unknown query.
 */
void GLUTGameModeString(const char *string)
{
	const char *p = string;
	char *end;
	long v;

	g_gmWidth = g_gmHeight = g_gmBpp = g_gmRefresh = -1;
	if (!p)
		return;

	v = strtol(p, &end, 10);
	if (end != p) { g_gmWidth = (int)v; p = end; }

	if (*p == 'x' || *p == 'X')
	{
		p++;
		v = strtol(p, &end, 10);
		if (end != p) { g_gmHeight = (int)v; p = end; }
	}
	if (*p == ':')
	{
		p++;
		v = strtol(p, &end, 10);
		if (end != p) { g_gmBpp = (int)v; p = end; }
	}
	if (*p == '@')
	{
		p++;
		v = strtol(p, &end, 10);
		if (end != p) { g_gmRefresh = (int)v; p = end; }
	}

	/* GLUT 3.7 warns per unrecognised word (glut_gamemode.c). Once per run
	 * here, and only when NOTHING was recognised: the width is the one field
	 * with no prefix, so a -1 there means the string did not begin with a
	 * number and no field after it can have been read either.
	 *
	 * TRAILING TEXT IS DELIBERATELY NOT REPORTED. GLUT rejects a whole word on
	 * it, but "640x480junk" still selects 640x480 here, so a notice would flag
	 * a string that works. The widths GLUT also accepts -- its "width=640
	 * height=480" criteria form -- parse to nothing here and are caught by the
	 * test above, which is the honest answer: this driver does not understand
	 * them.
	 *
	 * NO FORMAT ARGUMENTS. Inside minigl.library printf is the freestanding
	 * shim, which writes the format string and discards its arguments, so an
	 * echoed string would print as a literal %s. */
	if (g_gmWidth == -1 && !g_saidBadGameMode)
	{
		g_saidBadGameMode = 1;
		D(("GLUTGameModeString: no mode recognised in the string -- it has "
		       "to start with a width, as in 640x480:16@60 (said once per run)\n"));
	}
}

/*
 * Is there a screen mode this driver can actually open at the requested size?
 *
 * The test mirrors vid_OpenDisplay's own requirement rather than guessing: walk
 * the whole display database and look for a CyberGraphX mode that is genuinely
 * PIXFMT_BGRA32 at exactly this width and height. A mode of the right size in
 * the wrong pixel format is not usable by the driver, so reporting it as
 * possible would turn a truthful "no" into a failed OpenScreenTags later.
 *
 * Returns 1 if such a mode exists, 0 otherwise. Answering 0 is a real answer,
 * not an error.
 */
static int mglglut_ModeExists(int w, int h)
{
	ULONG modeID;

	if (w <= 0 || h <= 0)
		return 0;

	modeID = NextDisplayInfo(INVALID_ID);
	while (modeID != INVALID_ID)
	{
		if (IsCyberModeID(modeID))
		{
			ULONG pixfmt = GetCyberIDAttr(CYBRIDATTR_PIXFMT, modeID);
			ULONG mw     = GetCyberIDAttr(CYBRIDATTR_WIDTH,  modeID);
			ULONG mh     = GetCyberIDAttr(CYBRIDATTR_HEIGHT, modeID);

			if (pixfmt == PIXFMT_BGRA32 && mw == (ULONG)w && mh == (ULONG)h)
				return 1;
		}
		modeID = NextDisplayInfo(modeID);
	}
	return 0;
}

int GLUTGameModeGet(GLenum query)
{
	switch ((int)query)
	{
		case GLUT_GAME_MODE_ACTIVE:
			return g_gameModeActive;

		case GLUT_GAME_MODE_POSSIBLE:
			return mglglut_ModeExists(g_gmWidth, g_gmHeight);

		case GLUT_GAME_MODE_WIDTH:
			return g_gmWidth;

		case GLUT_GAME_MODE_HEIGHT:
			return g_gmHeight;

		case GLUT_GAME_MODE_PIXEL_DEPTH:
			/* The driver renders into a 4-bytes-per-pixel surface and asks
			 * OpenScreenTags for 24 bits of colour plus 8 of alpha, so this
			 * is 32 whatever the mode database reports for the mode. */
			return 32;

		case GLUT_GAME_MODE_REFRESH_RATE:
			return g_gmRefresh;

		case GLUT_GAME_MODE_DISPLAY_CHANGED:
			return g_gameModeActive;

		/* -1, not 0, for an unrecognised query -- GLUT's own default case, and the
		 * same convention GLUTGet uses for an unknown state. */
		default:
			return -1;
	}
}

int GLUTEnterGameMode(void)
{
	/*
	 * A LIVE WINDOW CANNOT BE PROMOTED TO FULLSCREEN, so refuse instead of
	 * pretending. Without the refusal, GLUTCreateWindow followed by
	 * GLUTEnterGameMode leaves a WINDOWED display while
	 * GLUTGameModeGet(GLUT_GAME_MODE_ACTIVE) reports game mode -- the one answer
	 * an application cannot recover from.
	 *
	 * GLUT creates a fresh fullscreen window at this point and returns its id.
	 * MiniGLV3D cannot move a live context from vid_OpenWindow's bitmap onto
	 * vid_OpenDisplay's own screen, and tearing the context down here would
	 * discard all of the caller's GL state with no way to reinstate it. So the
	 * honest answer is 0, the failure value both references use.
	 *
	 * The check comes before the g_initWidth/g_initHeight assignment on purpose:
	 * the game-mode string must not overwrite a live window's geometry either.
	 */
	if (g_haveContext)
	{
		E(("GLUTEnterGameMode: a window already exists -- cannot switch a live "
		       "context to fullscreen\n"));
		return 0;
	}

	/* Game mode reads the display mode as well: GLUT's glutEnterGameMode goes
	 * through the same __glutDetermineWindowVisual, so a stencil request is no
	 * more servable on a fullscreen screen than on a window. */
	if (!mglglut_ApplyDisplayMode("GLUTEnterGameMode"))
		return 0;

	if (g_gmWidth > 0 && g_gmHeight > 0)
	{
		g_initWidth  = g_gmWidth;
		g_initHeight = g_gmHeight;
	}

	/* Game mode is fullscreen, which is MiniGLV3D's default -- but say so
	 * explicitly rather than relying on the default, because GLUTCreateWindow
	 * sets the window mode and the two must not depend on call order. */
	mglChooseWindowMode(GL_FALSE);

	mini_CurrentContext = MGLCreateContext(0, 0, g_initWidth, g_initHeight);
	if (!mini_CurrentContext)
	{
		E(("GLUTEnterGameMode: MGLCreateContext(%ld,%ld) failed\n",
		       (long)g_initWidth, (long)g_initHeight));
		return 0;
	}
	g_haveContext = 1;

	/* Owed, not delivered -- the same reasoning as GLUTCreateWindow. */
	g_initWorkPending = 1;

	g_gameModeActive = 1;
	return 1;
}

void GLUTLeaveGameMode(void)
{
	if (!g_gameModeActive)
	{
		/* GLUT warns and returns rather than silently doing nothing. */
		D(("GLUTLeaveGameMode: not in game mode\n"));
		return;
	}

	/*
	 * THE DISPLAY IS CLOSED, but not from here, and the two-step is the whole
	 * substance of this function.
	 *
	 * It cannot close here. The game-mode screen's backdrop window IS
	 * context->inputWindow, and MGLMainLoop caches that pointer in a local and
	 * calls GetMsg on its UserPort every iteration -- and this runs from inside
	 * a callback with that loop on the stack. Deleting the context here would
	 * leave the loop polling freed memory.
	 *
	 * So it asks the loop to finish instead. MGLExit sets context->Running
	 * FALSE, MGLMainLoop's while test fails after the current iteration, and
	 * GLUTMainLoop's return runs mglglut_Cleanup -- which deletes the context
	 * and closes the screen from a point where MGLMainLoop is no longer on the
	 * stack. The caller sees game mode inactive immediately and the display go
	 * away a frame later, which is the ordering that makes it safe.
	 *
	 * ENDING THE LOOP IS A DELIBERATE DIVERGENCE. GLUT's glutMainLoop is a bare
	 * for(;;) that never returns (glut_event.c:1409); glutLeaveGameMode destroys
	 * the game-mode window and the loop carries on, because under GLUT it has
	 * the application's OTHER windows to service. This shim has exactly one
	 * context and no glutCreateWindow to go back to, so after leaving game mode
	 * there is no drawable at all and a loop that continued would be spinning on
	 * a display the caller has been told is gone. GLUT reaches the same place by
	 * a different route: entering its main loop with no windows is a fatal usage
	 * error (glut_event.c:1406), which is the case this leaves behind.
	 */
	MGLExit(mini_CurrentContext);
	g_leavePending   = 1;
	g_gameModeActive = 0;
}

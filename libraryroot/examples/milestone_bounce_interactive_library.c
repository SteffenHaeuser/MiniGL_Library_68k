/*
 * Phase I follow-up: real interactive version of milestone_bounce.c,
 * closing the "no ESC key" gap that file's own header comment has
 * tracked since Stage 18 -- fullscreen keyboard input was a confirmed
 * hang until the backdrop-window-hang investigation root-caused and
 * fixed it (see gl/src/context.c's own header comment, FIX ATTEMPT 3 /
 * MGLV3D_UNIFIED_PERFRAME_LOCK, and Claude memory
 * project_minigl_v3d_backdrop_window_hang). REQUIRES building context.c
 * (and this file) with -DMGLV3D_TEST_BACKDROP_WINDOW -- without it,
 * vid_OpenDisplay never opens the backdrop window, mglGetWindowHandle()
 * returns NULL, and MGLMainLoop's own ModifyIDCMP(window, ...) call would
 * be handed a NULL window.
 *
 * Everything else is identical to milestone_bounce.c -- same physics,
 * same ball/cage geometry and GL state setup, same drawBall/drawCage/draw
 * functions verbatim. The only real differences:
 *  - idle() no longer calls draw() directly in a fixed-count for loop;
 *    it's registered via MGLIdleFunc and MGLMainLoop drives it instead,
 *    running until ESC is pressed (or the window is otherwise closed).
 *  - A real KeyHandlerFn (keyHandler) checks for ASCII ESC (27, the code
 *    IDCMP_VANILLAKEY delivers) and calls MGLExit() to stop MGLMainLoop's
 *    own while(context->Running) loop cleanly.
 *  - No DateStamp()/E() timing block -- frame count is unbounded now (run
 *    until the user quits), so "ms/frame over N frames" isn't a
 *    meaningful measurement here the way it is in milestone_bounce.c's
 *    own fixed-BOUNCE_FRAMES timing runs. This file is for demonstrating
 *    real interactivity, not for the timing-comparison lineage.
 *  - WaitTOF() is called unconditionally every frame (not gated behind
 *    -DDEBUG/MGLV3D_FORCE_WAITTOF like milestone_bounce.c's own draw())
 *    since this is always meant to be watched/interacted with, never a
 *    flat-out nodebug throughput test.
 */

#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>
#include <math.h>
#include <stdio.h>
#include <proto/graphics.h>
#include <proto/exec.h>

#ifndef D
#ifdef DEBUG
#define D(x) do { printf x; } while (0)
#else
#define D(x) do { } while (0)
#endif
#endif

#define MILESTONE_WIDTH 640
#define MILESTONE_HEIGHT 480

#define COS(X) (float)cos((double)(X) * 3.14159265358979 / 180.0)
#define SIN(X) (float)sin((double)(X) * 3.14159265358979 / 180.0)

#define NUMSLICE 6.0f

static GLfloat Zrot = 0.0f, Zstep = 3.0f;
static GLfloat Xpos = 0.0f, Ypos = 1.0f;
static GLfloat Xvel = 0.2f, Yvel = 0.0f;
static GLfloat Xmin = -4.0f, Xmax = 4.0f;
static GLfloat Ymin = -2.8f, Ymax = 4.0f;
static GLfloat G = -0.05f;
static GLfloat rot = 0.0f;
static GLboolean do_cage = GL_TRUE;

//#ifndef __GNUC__
struct GfxBase *GfxBase = NULL;
//#endif

/* stricmp is not provided by every GCC/libnix configuration. */
static int local_stricmp(const char *a, const char *b)
{
    while (*a && *b)
    {
        char ca = (*a >= 'A' && *a <= 'Z') ? *a + ('a' - 'A') : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? *b + ('a' - 'A') : *b;

        if (ca != cb)
            return (int)ca - (int)cb;

        ++a;
        ++b;
    }

    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static void drawBall(void)
{
	float a, b;
	float da = 180.0f / NUMSLICE, db = 180.0f / NUMSLICE;
	float radius = 1.0f;
	int color = 0;
	float x, y, z;

	for (a = -90.0f; a + da <= 90.0f; a += da)
	{
		glBegin(GL_QUAD_STRIP);
		for (b = 0.0f; b <= 360.0f; b += db)
		{
			if (color)
				glColor3f(1.0f, 0.0f, 0.0f); /* red */
			else
				glColor3f(1.0f, 1.0f, 1.0f); /* white */

			x = COS(b) * COS(a);
			y = SIN(b) * COS(a);
			z = SIN(a);
			glVertex3f(x, y, z);

			x = radius * COS(b) * COS(a + da);
			y = radius * SIN(b) * COS(a + da);
			z = radius * SIN(a + da);
			glVertex3f(x, y, z);

			color = 1 - color;
		}
		glEnd();
	}
}

static void drawCage(void)
{
	GLfloat xmin = Xmin - 2.0f,
	        xmax = Xmax + 2.0f,
	        ymin = Ymin - 2.0f,
	        ymax = Ymax + 2.0f;

	glEnable(GL_BLEND);
	glColor4f(0.0f, 0.5f, 0.5f, 0.5f);

	glBegin(GL_QUADS);
		glVertex2f(xmax, ymax);
		glVertex2f(xmax, ymin);
		glVertex2f(xmin, ymin);
		glVertex2f(xmin, ymax);
	glEnd();
	glDisable(GL_BLEND);
}

static void draw(void)
{
	/* Always paced to vblank -- this demo is only ever meant to be
	 * watched/interacted with, never a flat-out timing run (see this
	 * file's own header comment). */
	WaitTOF();

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glRotatef(rot, 0.0f, 1.0f, 0.0f);

	if (do_cage == GL_TRUE) drawCage();

	glPushMatrix();
		glTranslatef(Xpos, Ypos, 0.0f);
		glScalef(2.0f, 2.0f, 2.0f);
		glRotatef(8.0f, 0.0f, 0.0f, 1.0f);
		glRotatef(90.0f, 1.0f, 0.0f, 0.0f);
		glRotatef(Zrot, 0.0f, 0.0f, 1.0f);

		drawBall();
	glPopMatrix();

	glFrontFace(GL_CCW);
	if (do_cage == GL_TRUE) drawCage();
	glFrontFace(GL_CW);

	mglSwitchDisplay();
}

static void idle(void)
{
	static float vel0 = -100.0f;

	Zrot += (Zstep < 0.0f) ? -Zstep : Zstep;

	Xpos += Xvel;
	if (Xpos >= Xmax)
	{
		Xpos = Xmax;
		Xvel = -Xvel;
		Zstep = -Zstep;
	}
	if (Xpos <= Xmin)
	{
		Xpos = Xmin;
		Xvel = -Xvel;
		Zstep = -Zstep;
	}
	Ypos += Yvel;
	Yvel += G;
	if (Ypos < Ymin)
	{
		Ypos = Ymin;
		if (vel0 == -100.0f)
		{
			vel0 = (Yvel < 0.0f) ? -Yvel : Yvel;
		}
		Yvel = vel0;
	}
	draw();
}

/* IDCMP_VANILLAKEY delivers the plain ASCII code -- 27 is ESC. */
static void keyHandler(char key)
{
	if (key == 27)
	{
		D(("milestone_bounce_interactive: ESC pressed, exiting\n"));
		mglExit();
	}
}

int main(int argc, char *argv[])
{
	float aspect;
	int depth;
	
GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 0);
if (!GfxBase)
{
    printf("Can't open graphics.library\n");
    return 20;
}		
	
	depth = 24;
	for (int i = 1; i < argc; i++)
	{
		if (0 == local_stricmp(argv[i], "-depth"))
		{
			i++;
			depth = atoi(argv[i]);
			if ((depth!=15)&&(depth!=16)&&(depth!=24)) depth = 24;
		}
	}		

	if (MiniGLOpen() == GL_FALSE)
	{
		D(("milestone_bounce_interactive: could not open minigl.library\n"));
		return -1;
	}

	mglChoosePixelDepth(depth);
	mglChooseVertexBufferSize(1000);
	if (!mglCreateContext(0, 0, MILESTONE_WIDTH, MILESTONE_HEIGHT))
	{
		D(("milestone_bounce_interactive: mglCreateContext failed\n"));
		MiniGLClose();
		return -2;
	}

	D(("milestone_bounce_interactive: context created\n"));

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glCullFace(GL_BACK);
	glEnable(GL_CULL_FACE);

	/* See milestone_bounce.c's own comment on this exact line -- matches
	 * the steady-state front-face convention drawCage()'s two calls per
	 * frame rely on, so frame 0 behaves identically to every later frame. */
	glFrontFace(GL_CW);

	glDisable(GL_DEPTH_TEST);
	glShadeModel(GL_FLAT);

	aspect = (float)MILESTONE_WIDTH / (float)MILESTONE_HEIGHT;
	glViewport(0, 0, MILESTONE_WIDTH, MILESTONE_HEIGHT);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(-6.0 * aspect, 6.0 * aspect, -6.0, 6.0, -6.0, 6.0);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	D(("milestone_bounce_interactive: setup done, entering MGLMainLoop (ESC to quit)\n"));

	mglIdleFunc(idle);
	mglKeyFunc(keyHandler);
	mglMainLoop();

	D(("milestone_bounce_interactive: MGLMainLoop returned, tearing down\n"));

	mglDeleteContext();
	MiniGLClose();
	
if (GfxBase)
{
    CloseLibrary((struct Library *)GfxBase);
    GfxBase = NULL;
}	

	return 0;
}

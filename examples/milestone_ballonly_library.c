/*
 * Third isolation test for the milestone_bounce.c "missing chunks" bug --
 * the ball's real drawBall() geometry (6 GL_QUAD_STRIP latitude bands,
 * copied verbatim from milestone_bounce.c/drawBall()), continuously
 * rotating, GL_CULL_FACE enabled -- but NO cage, NO bounce physics, NO
 * GL_DEPTH_TEST toggling, NO translate (rotation only, around the
 * origin).
 *
 * Results so far:
 *  - milestone_cube.c (6 separate single-quad GL_QUADS calls/frame,
 *    same rotation+culling setup): all 6 faces render correctly, no
 *    glitches -- rules out "rotation+culling+multiple draw calls per
 *    frame" in general.
 *  - milestone_ring.c (ONE GL_QUAD_STRIP/frame, 12 quads, red/white
 *    alternating -- the exact multi-color-batch mechanism, same vertex
 *    count as one of the ball's own strips): confirmed "smooth and
 *    normal" by the project owner -- rules out d_DrawQuadStrip's
 *    per-quad multi-color batching mechanism itself.
 *  - milestone_bounce.c (the real thing): confirmed "genuinely glitchy,
 *    unlike the ring. Whole sections disappear sometimes."
 *
 * This demo isolates the one remaining untested variable both prior
 * demos deliberately avoided: SIX quad-strips (a real sphere, not a
 * single ring) submitted together in ONE frame, sharing one rotating
 * transform. If this glitches the same way bounce.c does: the bug is in
 * having multiple quad-strips per frame (or the sphere's own geometry,
 * e.g. near-degenerate polar bands), not in the cage or physics. If this
 * renders cleanly: the bug must involve the cage and/or GL_DEPTH_TEST/
 * physics-driven translation bounce.c also has.
 */

#include <proto/minigl.h>
#include <math.h>
#include <stdio.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <dos/dos.h>

#ifndef D
#ifdef DEBUG
#define D(x) do { printf x; } while (0)
#else
#define D(x) do { } while (0)
#endif
#endif

#ifndef E
#define E(x) do { printf x; } while (0)
#endif

#define MILESTONE_WIDTH 640
#define MILESTONE_HEIGHT 480

/* MGLV3D_FORCE_WAITTOF also marks "this is the showcase build" -- 3x the
 * normal frame count so it runs long enough to actually show off to
 * people, without changing the frame count every other build variant
 * (the timing-comparison ones already documented at 90 frames) uses. */
#ifdef MGLV3D_FORCE_WAITTOF
#define BALL_FRAMES 270
#else
#define BALL_FRAMES 90
#endif

/* MGLV3D_DISABLE_SW_CULL is normally a draw.c-only compile flag (see
 * d_DrawQuadStrip's own comment) -- also passed to THIS file's own
 * compile for the _nocull comparison build variant specifically so the
 * timing output below can tag which cull mode it's reporting. */
#ifdef MGLV3D_DISABLE_SW_CULL
#define CULL_TAG "NOCULL"
#else
#define CULL_TAG "CULL"
#endif

#define COS(X) (float)cos((double)(X) * 3.14159265358979 / 180.0)
#define SIN(X) (float)sin((double)(X) * 3.14159265358979 / 180.0)

#define NUMSLICE 6.0f

static GLfloat Zrot = 0.0f;

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

/* Verbatim copy of milestone_bounce.c's drawBall() -- same geometry,
 * same 6-band structure, same color alternation. */
static void drawBall(void)
{
	float a, b;
	float da = 180.0f / NUMSLICE, db = 180.0f / NUMSLICE;
	float radius = 1.0f;
	int color = 0;
	float x, y, z;

	for (a = -90.0f; a + da <= 90.0f; a += da)
	{
		D(("milestone_ballonly: band a=%ld\n", (LONG)a));
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

static void draw(void)
{
	/* Only pacing mechanism now -- paces the START of each frame to
	 * right after vertical blank. Only the -DDEBUG build calls this
	 * (see the render loop below); the nodebug performance build runs
	 * flat-out, uncapped.
	 *
	 * MGLV3D_FORCE_WAITTOF: separate opt-in for a nodebug "showcase"
	 * build (no D()/kprintf overhead, but still paced to vblank for a
	 * smooth, presentable demo) without disturbing the existing nodebug-
	 * means-flat-out-timing convention -- only this explicit flag
	 * re-enables WaitTOF() in a nodebug build. */
#if defined(DEBUG) || defined(MGLV3D_FORCE_WAITTOF)
	WaitTOF();
#endif

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glPushMatrix();
		glScalef(1.6f, 1.6f, 1.6f);
		glRotatef(8.0f, 0.0f, 0.0f, 1.0f);
		glRotatef(90.0f, 1.0f, 0.0f, 0.0f);
		glRotatef(Zrot, 0.0f, 0.0f, 1.0f);
		drawBall();
	glPopMatrix();

	mglSwitchDisplay();
}

static void idle(void)
{
	Zrot += 3.0f;
	draw();
}

int main(int argc, char *argv[])
{
	int frame;
	float aspect;
	int depth;
	
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
		D(("milestone_ballonly: could not open minigl.library\n"));
		return -1;
	}

	mglChoosePixelDepth(depth);
	mglChooseVertexBufferSize(1000);
	if (!mglCreateContext(0, 0, MILESTONE_WIDTH, MILESTONE_HEIGHT))
	{
		D(("milestone_ballonly: mglCreateContext failed\n"));
		MiniGLClose();
		return -2;
	}

	D(("milestone_ballonly: context created\n"));

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

	glCullFace(GL_BACK);
	glEnable(GL_CULL_FACE);
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

	D(("milestone_ballonly: setup done, running %ld frames\n", (LONG)BALL_FRAMES));

	/* Timing via DateStamp() (dos.library, 1/50s resolution) -- printed
	 * with the unconditional E() macro (always calls kprintf, regardless
	 * of -DDEBUG) so it shows up the same way in both the debug and
	 * nodebug builds, letting the two be compared directly. Covers just
	 * the render loop itself, not MGLInit/context setup/teardown. */
	{
		struct DateStamp start_ds, end_ds;
		LONG elapsed_ticks;

		DateStamp(&start_ds);

		for (frame = 0; frame < BALL_FRAMES; frame++)
		{
			D(("milestone_ballonly: === frame %ld ===\n", (LONG)frame));
			idle();
		}

		DateStamp(&end_ds);

		elapsed_ticks = (end_ds.ds_Days   - start_ds.ds_Days)   * 1440L * 3000L
		              + (end_ds.ds_Minute - start_ds.ds_Minute) * 3000L
		              + (end_ds.ds_Tick   - start_ds.ds_Tick);

		/* ms/frame, not ticks/frame -- a tick is 20ms, and any frame
		 * faster than that (very likely once actually uncapped) makes
		 * ticks/frame truncate to 0 under integer division. */
		E(("milestone_ballonly [%s]: %ld frames in %ld ticks (%ld.%02ld sec, %ld ms/frame)\n",
		   CULL_TAG, (LONG)BALL_FRAMES, elapsed_ticks, elapsed_ticks / 50L, (elapsed_ticks % 50L) * 2L,
		   (elapsed_ticks * 20L) / (LONG)BALL_FRAMES));
	}

	D(("milestone_ballonly: tearing down\n"));

	mglDeleteContext();
	MiniGLClose();

	return 0;
}

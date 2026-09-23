/*
 * Shared-library stats variant of milestone_gears.c -- identical scene/geometry/setup,
 * PLUS a live, on-screen FPS overlay (real 68k port of the original
 * gears.c's own bShowFPS/GetSysTimePPC mechanism, drawn directly onto the
 * screen's RastPort each time it updates -- see UpdateFPSOverlay/
 * DrawFPSOverlay below). Real keyboard interaction (1/2/3/f/e/ESC) and
 * MGLMainLoop, matching milestone_gears.c -- 'f' has a REAL effect here
 * (toggles whether the overlay draws), unlike milestone_gears.c where
 * bShowFPS is inert (no overlay code there at all).
 *
 * Real gears.c (MiniGL/demos/gears.c) hardware milestone -- the actual
 * motivation for Phase J's texgen work. Gear geometry-generation code
 * (gear/tooth_side/flat_face/draw_inside/draw_outside) is copied
 * VERBATIM from the original -- pure math + glBegin/glVertex3f/glNormal3f/
 * glEnd calls, zero MGL/backend-specific risk, already proven by every
 * earlier milestone in this project.
 *
 * Three things this project has NEVER hardware-tested before, all genuinely
 * exercised here:
 *  - GL_RGB (3-byte, no alpha) texture upload -- tex_GLFormatToSrcFmt maps
 *    it to V3D_SRCFMT_RGB8, v3d_texture_convert_row forces alpha=0xff. Real,
 *    documented, but never run on hardware until now.
 *  - glFrustum real perspective projection -- every demo so far used
 *    glOrtho. GLHint(MGL_W_ONE_HINT, GL_FASTEST) is also used, verified by
 *    reading v_Transform's own two branches: the "fast" path still computes
 *    the true perspective w via the matrix's bottom row, it only skips
 *    multiplying by an incoming w known to be 1 -- safe, not an
 *    approximation that breaks perspective.
 *  - GL_TEXTURE_ENV_MODE (GL_REPLACE for gear 1, GL_MODULATE + glColor3f for
 *    gear 2) -- confirmed real and GPU-consulted (tex_SetEnv writes
 *    V3DTexture's own texenv_mode field, v3d_texture_emit_state reads it),
 *    not yet exercised end-to-end on hardware.
 *
 * PPM texture loading (LoadPPM) is a faithful, real port of the original's
 * own function -- reads a real P6 file from disk (default
 * "data/chrome.ppm", or -envmap <name>), no procedural fallback. If the
 * file can't be found/read, this fails the SAME way the original does:
 * prints a D() error and tears down without ever entering MGLMainLoop.
 * The file itself is not shipped by this project -- deploy one alongside
 * the binary on the Amiga side before running.
 *
 * Deliberately DIFFERENT from the original's own main(), to avoid stacking
 * untested risk on top of the above three real things:
 *  - No mglChoosePixelDepth(16)/mglChooseNumberOfBuffers(3) -- untested
 *    combinations unrelated to what this milestone is actually about.
 * MGLLockMode(MGL_LOCK_MANUAL)/MGLLockDisplay() (Phase K) now called,
 * matching the original exactly -- see milestone_gears.c's own header
 * comment and oneFrame()'s own comment below for the full account of
 * why this is confirmed harmless/no-op re: hardware.
 */

#include <mgl/gl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <proto/exec.h>

#ifndef D
#define D(x) do { printf x; } while (0)
#endif


#ifndef M_PI
#define M_PI 3.14159265
#endif

#define fcos  cos
#define fsin  sin
#define fsqrt sqrt

//#ifndef __GNUC__
struct GfxBase *GfxBase = NULL;
//#endif

/* stricmp isn't linked in this toolchain (undefined symbol at link time,
 * confirmed) -- see milestone_gears.c's own comment on this exact
 * function. */
static int local_stricmp(const char *a, const char *b)
{
	while (*a && *b)
	{
		char ca = (*a >= 'A' && *a <= 'Z') ? *a + ('a' - 'A') : *a;
		char cb = (*b >= 'A' && *b <= 'Z') ? *b + ('a' - 'A') : *b;
		if (ca != cb)
			return (int)ca - (int)cb;
		a++; b++;
	}
	return (int)*a - (int)*b;
}

static int show = 1;
static int bShowFPS = 1; /* on by default in this variant -- it's the whole point */
static GLboolean bEnvMap = GL_TRUE;

#define MILESTONE_WIDTH 640
#define MILESTONE_HEIGHT 480

/* ================= Real gear geometry, verbatim from gears.c ================= */

typedef struct {
  float rad, wid;
} Profile;

int circle_subdiv;
#define MIN_SUBDIV 30

void
flat_face(float ir, float or, float wd)
{
  int i;
  float w;

  if (wd == 0.0)
	return;
  for (w = wd / 2; w > -wd; w -= wd) {
	if (w > 0.0)
	  glNormal3f(0.0, 0.0, 1.0);
	else
	  glNormal3f(0.0, 0.0, -1.0);

	if (ir == 0.0) {
	  glBegin(GL_TRIANGLE_FAN);
	  glVertex3f(0.0, 0.0, w);
	  glVertex3f(or, 0.0, w);
	  for (i = 1; i < circle_subdiv; i++) {
		glVertex3f(fcos(2.0 * M_PI * i / (float)circle_subdiv) * or,
		  fsin(2.0 * M_PI * i / (float)circle_subdiv) * or,
		  w);
	  }
	  glVertex3f(or, 0.0, w);
	  glEnd();
	} else {
	  glBegin(GL_TRIANGLE_STRIP);
	  glVertex3f(or, 0.0, w);
	  glVertex3f(ir, 0.0, w);
	  for (i = 1; i < circle_subdiv; i++) {
		glVertex3f(fcos(2.0 * M_PI * i / (float)circle_subdiv) * or,
		  fsin(2.0 * M_PI * i / (float)circle_subdiv) * or,
		  w);
		glVertex3f(fcos(2.0 * M_PI * i / (float)circle_subdiv) * ir,
		  fsin(2.0 * M_PI * i / (float)circle_subdiv) * ir,
		  w);
	  }
	  glVertex3f(or, 0.0, w);
	  glVertex3f(ir, 0.0, w);
	  glEnd();
	}
  }
}

void
draw_inside(float w1, float w2, float rad)
{
  int i, j;
  float c, s;
  if (w1 == w2)
	return;

  w1 = w1 / 2;
  w2 = w2 / 2;
  for (j = 0; j < 2; j++) {
	if (j == 1) {
	  w1 = -w1;
	  w2 = -w2;
	}
	glBegin(GL_TRIANGLE_STRIP);
	glNormal3f(-1.0, 0.0, 0.0);
	glVertex3f(rad, 0.0, w1);
	glVertex3f(rad, 0.0, w2);
	for (i = 1; i < circle_subdiv; i++) {
	  c = fcos(2.0 * M_PI * i / circle_subdiv);
	  s = fsin(2.0 * M_PI * i / circle_subdiv);
	  glNormal3f(-c, -s, 0.0);
	  glVertex3f(c * rad, s * rad, w1);
	  glVertex3f(c * rad, s * rad, w2);
	}
	glNormal3f(-1.0, 0.0, 0.0);
	glVertex3f(rad, 0.0, w1);
	glVertex3f(rad, 0.0, w2);
	glEnd();
  }
}

void
draw_outside(float w1, float w2, float rad)
{
  int i, j;
  float c, s;
  if (w1 == w2)
	return;

  w1 = w1 / 2;
  w2 = w2 / 2;
  for (j = 0; j < 2; j++) {
	if (j == 1) {
	  w1 = -w1;
	  w2 = -w2;
	}
	glBegin(GL_TRIANGLE_STRIP);
	glNormal3f(1.0, 0.0, 0.0);
	glVertex3f(rad, 0.0, w1);
	glVertex3f(rad, 0.0, w2);
	for (i = 1; i < circle_subdiv; i++) {
	  c = fcos(2.0 * M_PI * i / circle_subdiv);
	  s = fsin(2.0 * M_PI * i / circle_subdiv);
	  glNormal3f(c, s, 0.0);
	  glVertex3f(c * rad, s * rad, w1);
	  glVertex3f(c * rad, s * rad, w2);
	}
	glNormal3f(1.0, 0.0, 0.0);
	glVertex3f(rad, 0.0, w1);
	glVertex3f(rad, 0.0, w2);
	glEnd();
  }
}

void
tooth_side(int nt, float ir, float or, float tp, float tip, float wd)
{
  float i;
  float end = 2.0 * M_PI / nt;
  float x[6], y[6];
  float s[3], c[3];

  or = or * ir;
  for (i = 0; i < 2.0 * M_PI - end / 4.0; i += end) {

	c[0] = fcos(i);
	s[0] = fsin(i);
	c[1] = fcos(i + end * (0.5 - tip / 2));
	s[1] = fsin(i + end * (0.5 - tip / 2));
	c[2] = fcos(i + end * (0.5 + tp / 2));
	s[2] = fsin(i + end * (0.5 + tp / 2));

	x[0] = ir * c[0];
	y[0] = ir * s[0];
	x[5] = ir * fcos(i + end);
	y[5] = ir * fsin(i + end);
	x[1] = x[0] + (x[5] - x[0]) * (0.5 - tp / 2);
	y[1] = y[0] + (y[5] - y[0]) * (0.5 - tp / 2);
	x[4] = x[0] + (x[5] - x[0]) * (0.5 + tp / 2);
	y[4] = y[0] + (y[5] - y[0]) * (0.5 + tp / 2);
	x[2] = or * fcos(i + end * (0.5 - tip / 2));
	y[2] = or * fsin(i + end * (0.5 - tip / 2));
	x[3] = or * fcos(i + end * (0.5 + tip / 2));
	y[3] = or * fsin(i + end * (0.5 + tip / 2));

	glNormal3f(0.0, 0.0, 1.0);
	glBegin(GL_TRIANGLE_STRIP);
	glVertex3f(x[2], y[2], wd / 2);
	glVertex3f(x[1], y[1], wd / 2);
	glVertex3f(x[3], y[3], wd / 2);
	glVertex3f(x[4], y[4], wd / 2);
	glEnd();

	glNormal3f(0.0, 0.0, -1.0);
	glBegin(GL_TRIANGLE_STRIP);
	glVertex3f(x[2], y[2], -wd / 2);
	glVertex3f(x[1], y[1], -wd / 2);
	glVertex3f(x[3], y[3], -wd / 2);
	glVertex3f(x[4], y[4], -wd / 2);
	glEnd();

	glNormal3f(c[0], s[0], 0.0);
	glBegin(GL_TRIANGLE_STRIP);
	glVertex3f(x[0], y[0], -wd / 2);
	glVertex3f(x[1], y[1], -wd / 2);
	glVertex3f(x[0], y[0], wd / 2);
	glVertex3f(x[1], y[1], wd / 2);
	glEnd();

	{
	  float a, b, n;
	  a = x[2] - x[1];
	  b = y[2] - y[1];
	  n = 1.0 / fsqrt(a * a + b * b);
	  a = a * n;
	  b = b * n;
	  glNormal3f(b, -a, 0.0);
	}
	glBegin(GL_TRIANGLE_STRIP);
	glVertex3f(x[1], y[1], -wd / 2);
	glVertex3f(x[2], y[2], -wd / 2);
	glVertex3f(x[1], y[1], wd / 2);
	glVertex3f(x[2], y[2], wd / 2);
	glEnd();

	glNormal3f(c[1], s[1], 0.0);
	glBegin(GL_TRIANGLE_STRIP);
	glVertex3f(x[2], y[2], -wd / 2);
	glVertex3f(x[3], y[3], -wd / 2);
	glVertex3f(x[2], y[2], wd / 2);
	glVertex3f(x[3], y[3], wd / 2);
	glEnd();

	{
	  float a, b, cc;
	  a = x[4] - x[3];
	  b = y[4] - y[3];
	  cc = 1.0 / fsqrt(a * a + b * b);
	  a = a * cc;
	  b = b * cc;
	  glNormal3f(b, -a, 0.0);
	}
	glBegin(GL_TRIANGLE_STRIP);
	glVertex3f(x[3], y[3], -wd / 2);
	glVertex3f(x[4], y[4], -wd / 2);
	glVertex3f(x[3], y[3], wd / 2);
	glVertex3f(x[4], y[4], wd / 2);
	glEnd();

	glNormal3f(c[2], s[2], 0.0);
	glBegin(GL_TRIANGLE_STRIP);
	glVertex3f(x[4], y[4], -wd / 2);
	glVertex3f(x[5], y[5], -wd / 2);
	glVertex3f(x[4], y[4], wd / 2);
	glVertex3f(x[5], y[5], wd / 2);
	glEnd();
  }
}

void
gear(int nt, float wd, float ir, float or, float tp, float tip, int ns, Profile * ip)
{
  float prev;
  int k, t;

  if (nt <= 0)
	circle_subdiv = MIN_SUBDIV;
  else {
	circle_subdiv = nt;
	while (circle_subdiv < MIN_SUBDIV)
	  circle_subdiv += nt;
  }

  if (ns <= 0) {
	flat_face(0.0, ir, wd);
  } else {
	if (ip[0].rad > 0.0) {
	  flat_face(0.0, ip[0].rad * ir, wd);
	  prev = wd;
	  t = 0;
	} else {
	  flat_face(0.0, ip[1].rad * ir, ip[0].wid * wd);
	  prev = ip[0].wid;
	  t = 1;
	}
	for (k = t; k < ns; k++) {
	  if (prev < ip[k].wid) {
		draw_inside(prev * wd, ip[k].wid * wd, ip[k].rad * ir);
	  } else {
		draw_outside(prev * wd, ip[k].wid * wd, ip[k].rad * ir);
	  }
	  prev = ip[k].wid;
	  if (k == ns - 1) {
		flat_face(ip[k].rad * ir, ir, ip[k].wid * wd);
		if (ip[k].wid < 1.0) {
		  draw_inside(ip[k].wid * wd, wd, ir);
		} else {
		  draw_outside(ip[k].wid * wd, wd, ir);
		}
	  } else {
		flat_face(ip[k].rad * ir, ip[k + 1].rad * ir, ip[k].wid * wd);
	  }
	}
  }

  tooth_side(nt, ir, or, tp, tip, wd);
}

Profile gear_profile[] =
{0.000, 0.0,
  0.300, 7.0,
  0.340, 0.4,
  0.550, 0.64,
  0.600, 0.4,
  0.950, 1.0
};

/* ================= Demo-specific setup (not from the original) ================= */

/* Real external PPM (P6) loader -- verbatim port of the original gears.c's
 * own LoadPPM/TexInit, matching milestone_gears.c's own copy exactly. */
static GLubyte *LoadPPM(char *name, GLint *w, GLint *h)
{
	int i;
	unsigned long x, y, maxval;
	FILE *f;
	GLubyte *where;

	f = fopen(name, "r");
	if (!f)
	{
		*w = 0; *h = 0;
		return NULL;
	}

	/* Two real header-parsing bugs fixed here -- see milestone_gears.c's
	 * own copy of this function for the full writeup: (1) maxval is now
	 * read as a real number and verified in code instead of a fragile
	 * literal "255" match in the scanf format, which could silently leave
	 * the file position misaligned if a real deployed file's header ever
	 * differs even slightly from what was tested during porting; (2) the
	 * exactly-one required separator byte before binary pixel data is now
	 * consumed explicitly via fgetc(), not a greedy `\n` scanf directive
	 * that could over-consume into real (dark, low-byte-value) pixel
	 * data. */
	i = fscanf(f, "P6\n%lu %lu\n%lu", &x, &y, &maxval);
	if (i != 3 || maxval != 255)
	{
		D(("milestone_gears_stats_library: LoadPPM: error scanning PPM header in %s (i=%ld maxval=%lu)\n", name, (LONG)i, maxval));
		fclose(f);
		*w = 0; *h = 0;
		return NULL;
	}
	fgetc(f); /* the exactly-one required separator byte before binary data */

	*w = x;
	*h = y;

	where = malloc(x * y * 3);
	if (!where)
	{
		D(("milestone_gears_stats_library: LoadPPM: out of memory loading %s\n", name));
		fclose(f);
		*w = 0; *h = 0;
		return NULL;
	}

	i = fread(where, 1, x * y * 3, f);
	fclose(f);

	if ((unsigned long)i != x * y * 3)
	{
		D(("milestone_gears_stats_library: LoadPPM: short read on %s\n", name));
		free(where);
		*w = 0; *h = 0;
		return NULL;
	}

	return where;
}

static float a1 = 27.0f, a2 = 67.0f, a3 = 47.0f, a4 = 87.0f;
static const float i1 = 1.2f, i2 = 3.1f, i3 = 2.3f, i4 = 1.1f;

/* Live, on-screen FPS overlay -- the original gears.c's own bShowFPS
 * mechanism, ported to 68k instead of the PPC-only GetSysTimePPC/
 * SubTimePPC it used. DateStamp()'s own resolution is 1/50th sec (one
 * "tick") -- too coarse to measure a single frame meaningfully (a fast
 * frame would read back 0 elapsed ticks). Instead accumulate a small
 * window of frames (FPS_SAMPLE_FRAMES) and recompute/redraw only every
 * Nth frame, giving a stable reading updated periodically throughout
 * the run rather than a single end-of-run number. */
#define FPS_SAMPLE_FRAMES 20
static struct DateStamp g_fps_ds;
static int g_fps_frame_count = 0;
static char g_fps_text[40] = "fps: --";

static void UpdateFPSOverlay(void)
{
	struct DateStamp now_ds;
	LONG elapsed_ticks;

	g_fps_frame_count++;
	if (g_fps_frame_count < FPS_SAMPLE_FRAMES)
		return;

	DateStamp(&now_ds);
	elapsed_ticks = (now_ds.ds_Days   - g_fps_ds.ds_Days)   * 1440L * 3000L
	              + (now_ds.ds_Minute - g_fps_ds.ds_Minute) * 3000L
	              + (now_ds.ds_Tick   - g_fps_ds.ds_Tick);

	if (elapsed_ticks > 0)
	{
		/* fps = frames * 50 ticks/sec / elapsed_ticks -- integer math,
		 * one decimal place (fps*10 first, then split). */
		LONG fps_x10 = ((LONG)FPS_SAMPLE_FRAMES * 500L) / elapsed_ticks;
		sprintf(g_fps_text, "fps: %ld.%ld", fps_x10 / 10L, fps_x10 % 10L);
	}

	g_fps_ds = now_ds;
	g_fps_frame_count = 0;
}

static void DrawFPSOverlay(void)
{
	/*
	 * Shared-library-safe version:
	 * no access to mini_CurrentContext/v3dScreen/v3dWindow.
	 *
	 * If MiniGL exposes a Window through mglGetWindowHandle(), draw the FPS
	 * directly into that window's RastPort.  In fullscreen configurations
	 * where the public API returns no Window, keep the counter useful by
	 * printing the same value to stdout.
	 */
	struct Window *win = (struct Window *)mglGetWindowHandle();

	if (win)
	{
		struct RastPort *rp = win->RPort;
		WORD x = 10 + win->BorderLeft;
		WORD y = 15 + win->BorderTop;

		SetAPen(rp, 1);
		SetDrMd(rp, JAM1);
		Move(rp, x, y);
		Text(rp, g_fps_text, strlen(g_fps_text));
	}
	else
	{
		printf("\r%s   ", g_fps_text);
		fflush(stdout);
	}
}

static void oneFrame(void)
{
	/* Restored (Phase K) -- see milestone_gears.c's own oneFrame()
	 * comment for the full account of why this is confirmed harmless/
	 * no-op re: hardware. */
	mglLockDisplay();

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	if (show & 1)
	{
		glPushMatrix();
			glTranslatef(0.0f, 0.0f, -4.0f);
			glRotatef(a3, 1.0f, 1.0f, 1.0f);
			glRotatef(a4, 0.0f, 0.0f, -1.0f);
			glTranslatef(0.14f, 0.2f, 0.0f);
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			gear(40, 0.4f, 2.0f, 1.1f, 0.8f, 0.4f,
			     sizeof(gear_profile) / sizeof(Profile), gear_profile);
		glPopMatrix();
	}

	if (show & 2)
	{
		glPushMatrix();
			glTranslatef(0.1f, 0.2f, -3.8f);
			glRotatef(a2, -4.0f, 2.0f, -1.0f);
			glRotatef(a1, 1.0f, -3.0f, 1.0f);
			glTranslatef(0.0f, -0.2f, 0.0f);
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			glColor3f(1.0f, 0.8f, 0.0f);
			gear(36, 0.4f, 2.0f, 1.1f, 0.7f, 0.2f,
			     sizeof(gear_profile) / sizeof(Profile), gear_profile);
		glPopMatrix();
	}

	a1 += i1; if (a1 > 360.0f) a1 -= 360.0f;
	a2 += i2; if (a2 > 360.0f) a2 -= 360.0f;
	a3 += i3; if (a3 > 360.0f) a3 -= 360.0f;
	a4 += i4; if (a4 > 360.0f) a4 -= 360.0f;

	/* mglSwitchDisplay() is what actually submits this frame's binned
	 * geometry for the GPU to bin+render -- the real pixel writes happen
	 * during/at that call, not before it. Drawing the text overlay
	 * BEFORE this would just get painted over by the GPU's own tile
	 * render right after. Draw it AFTER, directly onto the now-rendered,
	 * currently-visible buffer (single-buffered mode -- no separate
	 * buffer-swap step needed after this). */
	mglSwitchDisplay();

	if (bShowFPS)
	{
		UpdateFPSOverlay();
		DrawFPSOverlay();
	}
}

/* Real keyboard handler -- see milestone_gears.c's own comment on the
 * identical function. 'f' has a real effect HERE (gates whether oneFrame
 * calls Update/DrawFPSOverlay above), unlike milestone_gears.c where
 * there's no overlay code to gate at all. */
static void keys(char c)
{
	if (c == 27)
	{
		D(("milestone_gears_stats_library: ESC pressed, exiting\n"));
		mglExit();
	}

	if (c == '1') show = 1;
	if (c == '2') show = 2;
	if (c == '3') show = 3;

	if (c == 'f') bShowFPS = !bShowFPS;

	if (c == 'e')
	{
		if (bEnvMap == GL_TRUE)
		{
			bEnvMap = GL_FALSE;
			glDisable(GL_TEXTURE_GEN_S);
			glDisable(GL_TEXTURE_GEN_T);
		}
		else
		{
			bEnvMap = GL_TRUE;
			glEnable(GL_TEXTURE_GEN_S);
			glEnable(GL_TEXTURE_GEN_T);
		}
	}
}

int main(int argc, char *argv[])
{
	GLint width = MILESTONE_WIDTH, height = MILESTONE_HEIGHT;
	int i;
	char *name = 0;
	GLuint texname;
	double d_near = 1.0, d_far = 2000.0;
	GLboolean windowed = GL_FALSE;
	
GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 0);
if (!GfxBase)
{
    printf("Can't open graphics.library\n");
    return 20;
}	

	/* Same arg set as milestone_gears.c -- see its own comment on -envmap
	 * (real PPM path now) and -window's real limitations here. */
	for (i = 1; i < argc; i++)
	{
		if (0 == local_stricmp(argv[i], "-width"))
		{
			i++;
			width = atoi(argv[i]);
		}
		if (0 == local_stricmp(argv[i], "-height"))
		{
			i++;
			height = atoi(argv[i]);
		}
		if (0 == local_stricmp(argv[i], "-envmap"))
		{
			i++;
			name = argv[i];
		}
		if (0 == local_stricmp(argv[i], "-window"))
		{
			windowed = GL_TRUE;
		}
	}

	if (MGLInit() == GL_FALSE)
	{
		D(("milestone_gears_stats_library: could not open minigl.library\n"));
		return -1;
	}

	if (windowed)
		mglChooseWindowMode(GL_TRUE);
	mglChooseVertexBufferSize(1000);

	if (!mglCreateContext(0, 0, width, height))
	{
		D(("milestone_gears_stats_library: mglCreateContext failed\n"));
		MGLTerm();
		return -2;
	}



	mglEnableSync(GL_FALSE);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);

	{
		GLint tw, th;
		GLubyte *tmap = LoadPPM(name ? name : "data/chrome.ppm", &tw, &th);

		if (!tmap)
		{
			/* Fails the same way the original does: no fallback texture,
			 * no MGLMainLoop entry, just tear down cleanly. */
			D(("milestone_gears_stats_library: can't find texture %s\n", name ? name : "data/chrome.ppm"));
		}
		else
		{
			glGenTextures(1, &texname);
			glBindTexture(GL_TEXTURE_2D, texname);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, tw, th,
			             0, GL_RGB, GL_UNSIGNED_BYTE, tmap);
			free(tmap);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

			glEnable(GL_TEXTURE_2D);
			glEnable(GL_TEXTURE_GEN_S);
			glEnable(GL_TEXTURE_GEN_T);
			glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
			glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);

			glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			glColor3f(1.0f, 0.0f, 0.0f);

			glDisable(GL_CULL_FACE);
			glHint(MGL_W_ONE_HINT, GL_FASTEST);
			glEnable(GL_DEPTH_TEST);

			glViewport(0, 0, width, height);
			glMatrixMode(GL_PROJECTION);
			glLoadIdentity();
			glFrustum(-1.0, 1.0, -1.0, 1.0, d_near, d_far);
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();

			DateStamp(&g_fps_ds);
			g_fps_frame_count = 0;

			D(("milestone_gears_stats_library: setup done, entering MGLMainLoop (ESC to quit, 1/2/3 to pick gear(s), f toggles FPS overlay, e toggles envmap)\n"));

			/* Restored (Phase K) -- matches the original's own main()
			 * exactly; see milestone_gears.c's own comment. */
			mglLockMode(MGL_LOCK_MANUAL);


			mglIdleFunc(oneFrame);
			mglKeyFunc(keys);
			mglMainLoop();

			D(("milestone_gears_stats_library: MGLMainLoop returned, tearing down\n"));
		}
	}

	mglDeleteContext();
	MGLTerm();
	
if (GfxBase)
{
    CloseLibrary((struct Library *)GfxBase);
    GfxBase = NULL;
}	

	return 0;
}

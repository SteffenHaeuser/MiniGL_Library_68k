/*
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
 * PPM texture loading (LoadPPM) is now a faithful, real port of the
 * original's own function -- reads a real P6 file from disk (default
 * "PROGDIR:data/chrome.ppm", or -envmap <name>), no procedural fallback. If the
 * file can't be found/read, this fails the SAME way the original does:
 * prints a D() error and tears down without ever entering mglMainLoop.
 * The file itself is not shipped by this project -- deploy one alongside
 * the binary on the Amiga side before running.
 *
 * Deliberately DIFFERENT from the original's own main(), to avoid stacking
 * untested risk on top of the above three real things:
 *  - No mglChoosePixelDepth(16)/mglChooseNumberOfBuffers(3) -- untested
 *    combinations unrelated to what this milestone is actually about.
 *
 * Real keyboard interaction (1/2/3/f/e/ESC) and command-line arg parsing
 * ADDED, matching the original's own main()/keys() -- via MGLIdleFunc/
 * MGLKeyFunc/mglMainLoop, the SAME already-hardware-proven mechanism
 * milestone_bounce_interactive.c uses (the public minigl.library mgl* entry points). REQUIRES building with
 * -DMGLV3D_TEST_BACKDROP_WINDOW -- without it, vid_OpenDisplay never
 * opens the backdrop window, mglGetWindowHandle() returns NULL, and
 * mglMainLoop's internal ModifyIDCMP(window, ...) call would be handed a NULL
 * window (see milestone_bounce_interactive.c's own header comment).
 * mglLockMode(MGL_LOCK_MANUAL)/mglLockDisplay() (Phase K) now called,
 * matching the original exactly -- confirmed harmless/no-op re: hardware
 * (context->v3dLocked, what these set, is never read anywhere in this
 * codebase to gate behavior; mglSwitchDisplay already unconditionally
 * unlocks regardless). See oneFrame()'s own comment for the full account.
 *
 * bShowFPS's toggle ('f' key) has no visible effect in THIS file (matches
 * the original's own behavior on a non-PPC build, where the FPS-drawing
 * code was `#ifdef __PPC__`-gated and equally inert) -- milestone_gears_
 * stats.c is where the real on-screen FPS overlay lives.
 */

#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef D
#define D(x) do { printf x; } while (0)
#endif

#ifndef M_PI
#define M_PI 3.14159265
#endif

#define fcos  cos
#define fsin  sin
#define fsqrt sqrt

/* stricmp isn't linked in this toolchain (undefined symbol at link time,
 * confirmed) -- the original relied on a libc that provided it; a small
 * local case-insensitive compare avoids the dependency entirely. */
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
static int bShowFPS = 0;
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
 * own LoadPPM/TexInit. Fails the same way the original does: no fallback
 * texture, name defaults to "PROGDIR:data/chrome.ppm" when -envmap wasn't given,
 * and a missing/unreadable file means the demo tears down without ever
 * entering mglMainLoop (see main()). */
static GLubyte *LoadPPM(char *name, GLint *w, GLint *h)
{
	int i;
	unsigned long x, y, maxval;
	FILE *f;
	GLubyte *where;

	f = fopen(name, "rb");
	if (!f)
	{
		*w = 0; *h = 0;
		return NULL;
	}

	/* Two real header-parsing bugs fixed here (gears.c color-corruption
	 * investigation), both classic scanf-vs-binary-PPM gotchas:
	 * 1) The original format string hardcoded a literal "255" for maxval.
	 *    Both real chrome.ppm copies tested on the dev machine happen to
	 *    write maxval as exactly that text, so this never surfaced during
	 *    porting -- but if any real deployed file's maxval differs even
	 *    slightly (whitespace, a comment line), the literal match fails
	 *    while %lu's 2 successful conversions (width/height) still made
	 *    this function think the header parsed fine, leaving the file
	 *    position misaligned from the true start of pixel data --
	 *    corrupting every subsequent pixel's channel alignment. Now reads
	 *    maxval as a real number and verifies it in code instead.
	 * 2) A trailing `\n` in a scanf format is a WHITESPACE directive --
	 *    it greedily skips ANY AMOUNT of whitespace, not exactly the one
	 *    separator byte the PPM spec actually requires between maxval and
	 *    the binary data. If the very first real pixel byte happens to
	 *    also be a whitespace-classified value (0x09-0x0D or 0x20 --
	 *    plausible for a DARK region of an image, low byte values), the
	 *    old code would silently consume that pixel byte as if it were
	 *    header whitespace, shifting every subsequent read by one byte.
	 *    Fixed by consuming exactly one separator byte via fgetc(),
	 *    never more. */
	i = fscanf(f, "P6\n%lu %lu\n%lu", &x, &y, &maxval);
	if (i != 3 || maxval != 255)
	{
		D(("milestone_gears: LoadPPM: error scanning PPM header in %s (i=%ld maxval=%lu)\n", name, (LONG)i, maxval));
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
		D(("milestone_gears: LoadPPM: out of memory loading %s\n", name));
		fclose(f);
		*w = 0; *h = 0;
		return NULL;
	}

	i = fread(where, 1, x * y * 3, f);
	fclose(f);

	if ((unsigned long)i != x * y * 3)
	{
		D(("milestone_gears: LoadPPM: short read on %s\n", name));
		free(where);
		*w = 0; *h = 0;
		return NULL;
	}

	return where;
}

static float a1 = 27.0f, a2 = 67.0f, a3 = 47.0f, a4 = 87.0f;
static const float i1 = 1.2f, i2 = 3.1f, i3 = 2.3f, i4 = 1.1f;

static void oneFrame(void)
{
	/* Restored (Phase K): matches the original's own oneFrame() exactly.
	 * Confirmed harmless/no-op re: hardware -- context->v3dLocked (what
	 * this sets) is never read anywhere in this codebase to gate any
	 * behavior; mglSwitchDisplay already unconditionally calls
	 * MGLUnlockDisplay regardless of whether this was ever called. See
	 * STATUS.md's LockMode section for the full account of why this was
	 * previously omitted on an incorrect assumption. */
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

	mglSwitchDisplay();
}

/* Real keyboard handler, matching the original's own keys(char c) exactly
 * (same key set, same effect) -- registered via mglKeyFunc, invoked by
 * mglMainLoop with the plain ASCII code IDCMP_VANILLAKEY delivers (27 for
 * ESC, matching milestone_bounce_interactive.c's own keyHandler). */
static void keys(char c)
{
	if (c == 27)
	{
		D(("milestone_gears: ESC pressed, exiting\n"));
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
	int depth = 24;
	GLboolean windowed = GL_FALSE;

	/* Same arg set as the original's own main(argc, argv) -- -envmap
	 * <name> selects the PPM file to load, defaulting to "PROGDIR:data/chrome.ppm"
	 * (relative to the demo's current directory) when not given, matching
	 * the original's own TexInit() exactly. -window is parsed too, but
	 * note it changes mglCreateContext to open a WINDOW instead of a
	 * fullscreen screen -- context->v3dScreen would then be NULL; this
	 * demo doesn't touch v3dScreen directly (milestone_gears_stats.c's
	 * FPS overlay does), so it's safe here, but untested in that mode. */
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
		depth = 24;
		if (0 == local_stricmp(argv[i], "-depth"))
		{
			i++;
			depth = atoi(argv[i]);
			if ((depth!=15)&&(depth!=16)&&(depth!=24)) depth = 24;
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

	if (MiniGLOpen() == GL_FALSE)
	{
		D(("milestone_gears: could not open minigl.library\n"));
		return -1;
	}

	mglChoosePixelDepth(depth);
	if (windowed)
		mglChooseWindowMode(GL_TRUE);
	mglChooseVertexBufferSize(1000);
	if (!mglCreateContext(0, 0, width, height))
	{
		D(("milestone_gears: mglCreateContext failed\n"));
		MiniGLClose();
		return -2;
	}

	mglEnableSync(GL_FALSE);

    D(("milestone_gears: conventional glBegin/glEnd path\n"));

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);

	{
		GLint tw, th;
		GLubyte *tmap = LoadPPM(name ? name : "PROGDIR:data/chrome.ppm", &tw, &th);

		if (!tmap)
		{
			/* Fails the same way the original does: no fallback texture,
			 * no mglMainLoop entry, just tear down cleanly. */
			D(("milestone_gears: can't find texture %s\n", name ? name : "PROGDIR:data/chrome.ppm"));
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

			D(("milestone_gears: setup done, entering mglMainLoop (ESC to quit, 1/2/3 to pick gear(s), e toggles envmap)\n"));

			/* Restored (Phase K) -- matches the original's own main()
			 * exactly; already the context-init default here regardless,
			 * see oneFrame()'s own comment above. */
			mglLockMode(MGL_LOCK_MANUAL);

			mglIdleFunc(oneFrame);
			mglKeyFunc(keys);
			mglMainLoop();

			D(("milestone_gears: mglMainLoop returned, tearing down\n"));
		}
	}

	mglDeleteContext();
	MiniGLClose();

	return 0;
}

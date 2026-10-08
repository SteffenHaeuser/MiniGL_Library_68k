/*
 * (C) 2025-2026 Dennis van der Boon
 *
 * The glutSolid* shapes for MiniGLV3D.
 *
 * Every shape emits a per-vertex normal. Nothing consumes normals on the GPU
 * yet, but GL_SPHERE_MAP texgen consumes them on the CPU today and fixed-
 * function lighting will consume them later, so generating them now costs
 * nothing and means the shapes do not have to be revisited.
 *
 * ONE GLBEGIN BLOCK PER STACK OR RING, never one block for a whole shape. Two
 * reasons: the vertex buffer defaults to 256 entries, and a stack of a
 * 50x50 sphere is 102 vertices while the whole sphere is over 5,000; and
 * MGLNormal entries are consumed once per glNormal3f CALL within a block, so
 * small blocks keep that count small too.
 *
 * Normals are computed from the surface parameters, never by normalising a
 * position. That matters for the degenerate calls this project's own demo set
 * makes: GLUTSolidSphere(0, 10, 10) has radius zero, and dividing a position by
 * the radius there would be a divide by zero.
 *
 * All angle maths is float and there is no float-to-int conversion anywhere in
 * this file. Rounding a float to an int on this target with (int)(f + 0.5f)
 * turns every exact integer into a tie and pushes odd values up by one, so the
 * idiom is avoided rather than used carefully.
 *
 * WHICH REFERENCE IS AUTHORITATIVE, because three of them disagree and the answer
 * is not the same for every shape.
 *
 *   GLU (glu-master/src/libutil/quad.c) governs the quadric-derived shapes, and
 *   GLUT 3.7 (glut-master/lib/glut/glut_shapes.c) governs the rest -- the cube,
 *   the torus and the dodecahedron, which GLUT implements itself.
 *
 *   NOT freeglut, for a structural reason rather than a preference: its solids go
 *   through fghDrawGeometrySolid11, which binds glNormalPointer and calls
 *   glDrawElements. Vertex arrays never set a CURRENT normal, so freeglut cannot
 *   define the one piece of state an application inherits. Its immediate-mode
 *   fallback does, but sits behind an #else that only compiles on a pre-GL-1.1
 *   build. Where freeglut IS followed on purpose -- the cone keeps its base disc,
 *   which GLUT 3.7 does not draw at all -- that function says so.
 *
 * THE EMISSION ORDER IS PART OF THE CONTRACT, not an implementation detail: the
 * last glNormal3f a shape executes becomes the application's current normal, and
 * a client application's sea reads exactly that for GL_SPHERE_MAP texgen. Getting it wrong is
 * invisible in every ordinary test. Those leftover normals are asserted against
 * reference-derived values on hardware by the GLUT fidelity-test demo -- add a
 * row there rather than deriving one by hand.
 */

#include "sysinc.h"

/* The largest slices/sides a GL_QUAD_STRIP ring can carry without losing
 * vertices, read from the live context. Its own comment carries the
 * derivation; the shapes here must not recompute it. */
extern GLint glu_MaxSlices(void); /* glu.c */
#include <mgl/glut.h>

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TWOPI ((GLfloat)(2.0 * M_PI))

/*
 * ONE shared quadric, created on first use, which is GLUT 3.7's own arrangement
 * (its QUAD_OBJ_INIT / quadObj). The draw style and normal mode are set on every
 * call rather than once, because an application is free to have changed them
 * through its own quadric; GLUT does the same for the same reason.
 */
static GLUquadricObj *s_quad;

static int shapes_Quad(void)
{
	if (s_quad == NULL)
		s_quad = gluNewQuadric();
	if (s_quad == NULL)
		return 0;                       /* out of memory: draw nothing, as GLU does */

	gluQuadricDrawStyle(s_quad, GLU_FILL);
	gluQuadricNormals(s_quad, GLU_SMOOTH);
	/* Texture and orientation are left at their defaults deliberately -- nothing
	 * here changes them, which is the condition GLUT's own comment states. */
	return 1;
}
/*
 * A solid cone, and it DELEGATES: GLUT 3.7's GLUTSolidCone is nothing but
 * gluCylinder(base, 0, height), so this is that call plus the base disc.
 *
 * A tessellator here would duplicate gl/src/glu.c's, and two implementations of
 * one surface each carry their own axis convention and emission order, need the
 * reconciliation against GLU/GLUT/freeglut done TWICE, and have nothing keeping
 * them agreeing afterwards. Delegating makes the leftover CURRENT NORMAL correct
 * by construction rather than by matching it by hand.
 *
 * NEGATIVE HEIGHT works: GLUCylinder deliberately omits GLU's `height < 0`
 * reject and glu_TessCone flips both the normal and the vertex order for it.
 * scene06 drives this from a timer that starts below zero.
 *
 * THE BASE DISC IS KEPT, AND DRAWN FIRST. The references disagree on whether a
 * solid cone has a base at all -- GLUT 3.7 draws none, so a cone viewed from
 * below shows its back-facing inner wall and vanishes there under GL_CULL_FACE;
 * freeglut draws one, as its FIRST strip. Keeping it follows freeglut and closes
 * the solid; drawing it first is what leaves a SIDE normal current on return,
 * which is the value GLUT 3.7 leaves. Drawn last it leaves (0,0,-1), matching
 * neither reference.
 *
 * The disc faces -Z only while h > 0. With h < 0 the solid occupies z in [h,0],
 * so the disc at z = 0 is the TOP: both its normal and its winding flip, or it is
 * a back face pointing into the solid.
 */
void GLUTSolidCone(GLdouble base, GLdouble height, GLint slices, GLint stacks)
{
	const GLfloat r  = (GLfloat)base;
	const GLfloat h  = (GLfloat)height;
	const GLfloat cz = (h < 0.0f) ? 1.0f : -1.0f;
	GLint i;

	if (!shapes_Quad())
		return;

	/*
	 * NOTHING IS DRAWN for stacks below 1, and that is what every reference
	 * does: GLUT 3.7's glutSolidCone is a bare gluCylinder call, gluCylinder
	 * rejects stacks < 1, and freeglut's cone returns on it.
	 *
	 * It has to return HERE, before the base disc. This function emits the
	 * disc itself and only then calls gluCylinder, so returning any later
	 * would leave a lone base fan drawn -- a partial figure no reference
	 * produces -- and the disc's normal current behind it.
	 */
	if (stacks < 1)
		return;

	if (slices < 3) slices = 3;

	/*
	 * ONE CLAMP FOR BOTH HALVES, and it has to be the tighter of the two.
	 * gluCylinder clamps the wall to glu_MaxSlices() itself; the base disc below
	 * is a GL_TRIANGLE_FAN of slices+2 vertices, so on its own it could take
	 * nearly twice that. Clamping the disc to its own looser bound would leave
	 * the disc and the wall on DIFFERENT slice counts and put a seam between
	 * them, so the value is pinned here, before either is drawn, and handed to
	 * gluCylinder already clamped.
	 *
	 * Without it the disc silently loses vertices at a large slices -- dropped
	 * with GL_OUT_OF_MEMORY flagged (vertexbuffer_min.c), not written past the
	 * buffer -- while the wall stays whole, which is the worst shape for a
	 * caller to diagnose.
	 */
	if (slices > glu_MaxSlices()) slices = glu_MaxSlices();

	glBegin(GL_TRIANGLE_FAN);
	glNormal3f(0.0f, 0.0f, cz);
	glVertex3f(0.0f, 0.0f, 0.0f);
	for (i = 0; i <= slices; i++)
	{
		/* GLU's axes, x = sin and y = cos. Ascending i then runs CLOCKWISE in XY,
		 * which is counter-clockwise seen from below -- verified: the first two ring
		 * points are (0,r,0) and (r sin d, r cos d, 0), and (p0-c)x(p1-p0) is
		 * (0,0,-r*r*sin d), agreeing with the declared (0,0,-1). h < 0 reverses it. */
		const GLint   k = (h < 0.0f) ? (slices - i) : i;
		const GLfloat t = TWOPI * (GLfloat)k / (GLfloat)slices;
		glVertex3f(r * (GLfloat)sin(t), r * (GLfloat)cos(t), 0.0f);
	}
	glEnd();

	gluCylinder(s_quad, base, 0.0, height, slices, stacks);
}

/*
 * Axis-aligned cube of the given EDGE LENGTH, centred on the origin, which is
 * GLUT's definition (not a half-extent).
 */
void GLUTSolidCube(GLdouble size)
{
	GLfloat s = (GLfloat)size * 0.5f;

	/*
	 * FACE ORDER IS GLUT 3.7'S, and it is the order rather than the geometry that
	 * matters here. GLUT's drawBox walks its own tables BACKWARDS --
	 * `for (i = 5; i >= 0; i--)` at glut_shapes.c:152 -- so the last normal it
	 * emits is n[0] = (-1,0,0), and that is what an application inherits.
	 *
	 * freeglut cannot arbitrate this one. Its solids go through
	 * fghDrawGeometrySolid11 (fg_geometry.c:362), which binds glNormalPointer and
	 * calls glDrawElements -- vertex arrays never touch the current normal. Its
	 * immediate-mode fghDrawGeometrySolid10 is inside the `#else` at
	 * fg_geometry.c:393-394 and so compiles only on a pre-GL-1.1 build. (Had it
	 * been the live path, freeglut's cube_n order would have left (0,0,-1) --
	 * a third answer again.)
	 *
	 * The six faces here are GLUT's, matched by vertex set: every pair agrees on
	 * both the cyclic winding direction and the declared normal.
	 */
	static const GLfloat n[6][3] = {
		{ 0.0f,  0.0f, -1.0f}, { 0.0f,  0.0f,  1.0f},
		{ 0.0f, -1.0f,  0.0f}, { 1.0f,  0.0f,  0.0f},
		{ 0.0f,  1.0f,  0.0f}, {-1.0f,  0.0f,  0.0f}
	};
	/*
	 * One row per face, as indices into the eight corners, where a corner's
	 * index is its sign bits: bit0 = x, bit1 = y, bit2 = z (set means +s). So
	 * corner 0 is (-s,-s,-s), corner 7 is (+s,+s,+s).
	 *
	 * Every row is counter-clockwise SEEN FROM OUTSIDE, and each row's order
	 * matches n[] above. That is load-bearing rather than cosmetic: an
	 * application that enables GL_CULL_FACE culls a clockwise face away
	 * entirely, and this demo both culls and flips glFrontFace between CW and
	 * CCW around individual objects, so a cube wound the wrong way loses faces
	 * and reads as a broken shape rather than as a shading error.
	 *
	 * Each row is verified by the winding's own normal, (v1-v0) x (v2-v1),
	 * agreeing with n[] for that face -- which is the check that catches both a
	 * reversed winding and a face placed on the wrong side.
	 */
	static const int f[6][4] = {
		{0, 2, 3, 1},   /* -z, all corners at z = -s; GLUT faces[5] = {7,4,0,3} */
		{4, 5, 7, 6},   /* +z, all corners at z = +s; GLUT faces[4] = {5,6,2,1} */
		{0, 1, 5, 4},   /* -y                       ; GLUT faces[3] = {4,5,1,0} */
		{1, 3, 7, 5},   /* +x                       ; GLUT faces[2] = {7,6,5,4} */
		{2, 6, 7, 3},   /* +y                       ; GLUT faces[1] = {3,2,6,7} */
		{0, 4, 6, 2}    /* -x, emitted LAST, so (-1,0,0) is the leftover normal;
		                 * GLUT faces[0] = {0,1,2,3} */
	};
	int i, k;

	for (i = 0; i < 6; i++)
	{
		glBegin(GL_QUADS);
		glNormal3f(n[i][0], n[i][1], n[i][2]);
		for (k = 0; k < 4; k++)
		{
			int c = f[i][k];
			glVertex3f((c & 1) ? s : -s,
			           (c & 2) ? s : -s,
			           (c & 4) ? s : -s);
		}
		glEnd();
	}
}

/*
 * A solid sphere, and it DELEGATES -- GLUTSolidSphere is a thin wrapper over
 * gluSphere in GLUT 3.7, and here too.
 *
 * A client application's sea reads whatever CURRENT NORMAL the sphere leaves and feeds it to
 * GL_SPHERE_MAP texgen, so a second tessellation here would diverge from
 * gl/src/glu.c's GLUSphere in exactly that value. One implementation cannot
 * disagree with itself.
 *
 * Callers to keep in mind: scene08 calls GLUTSolidSphere(0,10,10) -- a
 * ZERO-RADIUS sphere that draws nothing and exists purely to leave a normal
 * behind. GLUSphere rejects a NEGATIVE radius, so zero still draws its
 * degenerate figure and still leaves the j = stacks-2 ring normal, which is what
 * that scene depends on.
 */
void GLUTSolidSphere(GLdouble radius, GLint slices, GLint stacks)
{
	if (!shapes_Quad())
		return;

	gluSphere(s_quad, radius, slices, stacks);
}

/*
 * Torus in the z = 0 plane. GLUT's parameter names are misleading: innerRadius
 * is the radius of the TUBE and outerRadius is the distance from the origin to
 * the centre of the tube, so a torus with innerRadius > outerRadius folds
 * through itself rather than being invalid.
 *
 * THAT FOLD IS NOT HYPOTHETICAL and it is not a bug here. glxsglut scene05 calls
 * GLUTSolidTorus(.1, .05, 4, 20), where the tube radius exceeds the centre
 * distance: at sides = 4 the phi = 180 degree column has ring radius
 * .05 - .1 = -.05, and a negative ring radius reflects that column through the
 * axis, inverting the local surface orientation. 20 of that torus's 80 quads are
 * therefore inside-out. GLUT's own traversal produces the identical 20, so this
 * is what the scene's author saw on real GLUT, not something to correct.
 *
 * The traversal below is GLUT 3.7's doughnut()/quadloop() (glut_shapes.c), and
 * three details of it are load-bearing:
 *
 *   y IS NEGATED. GLUT's vertex is (cosTheta*dist, -sinTheta*dist, r*sinPhi) and
 *   its normal (cosTheta*cosPhi, -sinTheta*cosPhi, sinPhi), so its ring angle
 *   runs clockwise seen from +z. freeglut uses +sin; the point set is the same
 *   torus either way, but the traversal direction is not.
 *
 *   THE theta1 PAIR COMES FIRST, then theta. Combined with the negated y this
 *   winds every quad counter-clockwise seen from outside, agreeing with the
 *   declared normals.
 *
 *   BOTH LOOPS CLOSE BY MODULO, not by running one past the end. Band j spans
 *   (j, (j+1) mod rings) and column i is (i mod sides), which is what GLUT's
 *   explicit first/last phi = 0 pairs and its closing quadloop(theta, 0) achieve.
 *   The consequence is the CURRENT NORMAL left on return: the last emission is
 *   the theta pair of the last band at phi = 0, i.e.
 *   (cos(2pi(rings-1)/rings), +sin(2pi(rings-1)/rings), 0) -- (0.9239, 0.3827, 0)
 *   at rings = 16. Running the ring loop to 2pi instead leaves (1, 0, 0), which
 *   matches no reference.
 */
void GLUTSolidTorus(GLdouble innerRadius, GLdouble outerRadius,
                    GLint sides, GLint rings)
{
	GLint i, j;
	GLfloat tube = (GLfloat)innerRadius;
	GLfloat dist = (GLfloat)outerRadius;

	if (sides < 3) sides = 3;
	if (rings < 3) rings = 3;

	/*
	 * SIDES is the one that can overflow a block: each ring is one
	 * GL_QUAD_STRIP of 2*(sides+1) vertices, and the excess is dropped with
	 * GL_OUT_OF_MEMORY flagged, so a large sides quietly produces a torus with
	 * holes in it.
	 *
	 * RINGS deliberately has no upper clamp. Every ring is its own glBegin
	 * block, so more rings cost more blocks and never more vertices per block.
	 * Clamping it would refuse a figure this driver can draw perfectly well.
	 */
	if (sides > glu_MaxSlices()) sides = glu_MaxSlices();

	for (j = 0; j < rings; j++)
	{
		GLfloat t0 = TWOPI * (GLfloat)j                 / (GLfloat)rings;
		GLfloat t1 = TWOPI * (GLfloat)((j + 1) % rings) / (GLfloat)rings;
		GLfloat ct0 = (GLfloat)cos(t0), st0 = (GLfloat)sin(t0);
		GLfloat ct1 = (GLfloat)cos(t1), st1 = (GLfloat)sin(t1);

		glBegin(GL_QUAD_STRIP);
		for (i = 0; i <= sides; i++)
		{
			GLfloat p  = TWOPI * (GLfloat)(i % sides) / (GLfloat)sides;
			GLfloat cp = (GLfloat)cos(p), sp = (GLfloat)sin(p);
			GLfloat rr = dist + tube * cp;

			glNormal3f(ct1 * cp, -st1 * cp, sp);
			glVertex3f(ct1 * rr, -st1 * rr, tube * sp);

			glNormal3f(ct0 * cp, -st0 * cp, sp);
			glVertex3f(ct0 * rr, -st0 * rr, tube * sp);
		}
		glEnd();
	}
}

/*
 * Regular dodecahedron: 20 vertices, 12 pentagonal faces.
 *
 * The face table below is typed by hand, so the winding of any row could be
 * wrong. Rather than trust it, each face's normal is derived from the cross
 * product of its first three vertices and then checked against the face
 * centroid, which always points away from the origin for a convex solid
 * centred there. A row wound the wrong way is emitted in reverse instead of
 * being culled or lit inside out. That makes a typing error visible as nothing
 * at all rather than as a subtly wrong solid.
 */
void GLUTSolidDodecahedron(void)
{
	/* GLUT's own alpha and beta (glut_shapes.c:277-279):
	 *   alpha = sqrt(2 / (3 + sqrt 5))                                = 0.618034
	 *   beta  = 1 + sqrt(6/(3+sqrt5) - 2 + 2*sqrt(2/(3+sqrt5)))       = 1.618034
	 * which are 1/phi and phi. Named p and q here to keep the table narrow. */
	const GLfloat p  = 1.618034f;    /* GLUT's beta  = (1 + sqrt 5) / 2 */
	const GLfloat q  = 0.618034f;    /* GLUT's alpha = 1 / p            */
	GLfloat v[20][3];
	/*
	 * Vertex numbering and face order are GLUT 3.7's, taken from
	 * glut_shapes.c:281-300 (initDodecahedron) and the twelve pentagon() calls at
	 * :309-320. The ORDER decides which face is emitted last and therefore which
	 * normal an application inherits: GLUT leaves (-0.525731, +0.850651, 0).
	 *
	 * GLUT's table has all 20 vertices at
	 * circumradius sqrt 3 (the same size as freeglut's dodecahedron_v), each used
	 * exactly 3 times, all 30 edges at 2*alpha = 1.236068, all 12 faces planar and
	 * counter-clockwise seen from outside. GLUT's normal is cross(a-b, b-c) where
	 * this computes cross(b-a, c-a); expanding both gives cross(a,b) - cross(a,c)
	 * + cross(b,c), so they are the same direction, and all 12 agree numerically.
	 */
	static const int face[12][5] = {
		{ 0,  1,  9, 16,  5}, { 1,  0,  3, 18,  7}, { 1,  7, 11, 10,  9},
		{11,  7, 18, 19,  6}, { 8, 17, 16,  9, 10}, { 2, 14, 15,  6, 19},
		{ 2, 13, 12,  4, 14}, { 2, 19, 18,  3, 13}, { 3,  0,  5, 12, 13},
		{ 6, 15,  8, 10, 11}, { 4, 17,  8, 15, 14}, { 4, 12,  5, 16, 17}
	};
	int i, k;

	/* Two on each of +-z at (+-q, 0, +-p), the eight (+-1,+-1,+-1) corners, then
	 * four on z = 0 and four on x = 0. This is GLUT's order exactly, index for
	 * index, because the face table above indexes into it. */
	v[0][0]=-q; v[0][1]= 0; v[0][2]= p;      v[1][0]= q; v[1][1]= 0; v[1][2]= p;

	v[2][0]=-1; v[2][1]=-1; v[2][2]=-1;      v[3][0]=-1; v[3][1]=-1; v[3][2]= 1;
	v[4][0]=-1; v[4][1]= 1; v[4][2]=-1;      v[5][0]=-1; v[5][1]= 1; v[5][2]= 1;
	v[6][0]= 1; v[6][1]=-1; v[6][2]=-1;      v[7][0]= 1; v[7][1]=-1; v[7][2]= 1;
	v[8][0]= 1; v[8][1]= 1; v[8][2]=-1;      v[9][0]= 1; v[9][1]= 1; v[9][2]= 1;

	v[10][0]= p; v[10][1]= q; v[10][2]=0;    v[11][0]= p; v[11][1]=-q; v[11][2]=0;
	v[12][0]=-p; v[12][1]= q; v[12][2]=0;    v[13][0]=-p; v[13][1]=-q; v[13][2]=0;

	v[14][0]=-q; v[14][1]=0; v[14][2]=-p;    v[15][0]= q; v[15][1]=0; v[15][2]=-p;

	v[16][0]=0; v[16][1]= p; v[16][2]= q;    v[17][0]=0; v[17][1]= p; v[17][2]=-q;
	v[18][0]=0; v[18][1]=-p; v[18][2]= q;    v[19][0]=0; v[19][1]=-p; v[19][2]=-q;

	for (i = 0; i < 12; i++)
	{
		const GLfloat *a = v[face[i][0]];
		const GLfloat *b = v[face[i][1]];
		const GLfloat *c = v[face[i][2]];
		GLfloat u0 = b[0]-a[0], u1 = b[1]-a[1], u2 = b[2]-a[2];
		GLfloat w0 = c[0]-a[0], w1 = c[1]-a[1], w2 = c[2]-a[2];
		GLfloat nx = u1*w2 - u2*w1;
		GLfloat ny = u2*w0 - u0*w2;
		GLfloat nz = u0*w1 - u1*w0;
		GLfloat cx = 0.0f, cy = 0.0f, cz = 0.0f;
		GLfloat len;
		int reversed;

		for (k = 0; k < 5; k++)
		{
			cx += v[face[i][k]][0];
			cy += v[face[i][k]][1];
			cz += v[face[i][k]][2];
		}

		reversed = (nx*cx + ny*cy + nz*cz) < 0.0f;
		if (reversed) { nx = -nx; ny = -ny; nz = -nz; }

		len = (GLfloat)sqrt(nx*nx + ny*ny + nz*nz);
		if (len > 0.0f) { nx /= len; ny /= len; nz /= len; }

		glBegin(GL_TRIANGLE_FAN);
		glNormal3f(nx, ny, nz);
		if (reversed)
			for (k = 4; k >= 0; k--)
				glVertex3f(v[face[i][k]][0], v[face[i][k]][1], v[face[i][k]][2]);
		else
			for (k = 0; k < 5; k++)
				glVertex3f(v[face[i][k]][0], v[face[i][k]][1], v[face[i][k]][2]);
		glEnd();
	}
}

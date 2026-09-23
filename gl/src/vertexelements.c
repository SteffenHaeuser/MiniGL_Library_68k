/*
 * This file is part of the MiniGL library project
 * See the file Licence.txt for more details
 */

/*
** glDrawElements pipeline by Christian 'Surgeon' Michael
** Thanks to Olivier Fabre for bug-hunting
**
** major revision 05-07 april 2002:
** optimized path for GL_EXT_compiled_vertex_array
**
*/

/*
 * Modified by Dennis van der Boon for use on the PiStorm with Pi4.
 * Copyright 2025-2026.
 */

/*
 * MiniGLV3D fork of MiniGL/src/vertexelements.c.
 *
 * ARCHITECTURE: as in GLDrawArrays (vertexarray.c) -- gather app array
 * data into context->VertexBuffer[] (the same staging array
 * GLVertex4f/GLDrawArrays use) and call the d_Draw* function for the
 * mode (draw.c) per chunk. Unlike GLDrawArrays, which reads a linear
 * first..first+count range, each gathered vertex's source index comes
 * from the caller's OWN index array (indices[i], widened from
 * GL_UNSIGNED_BYTE/_SHORT/_INT to a plain int as it's read) -- a
 * scatter-gather, not a contiguous walk. This is the same difference the
 * original's TransformIndex (vertexelements.c) has from A_TransformArray
 * (vertexarray.c).
 *
 * The exception is GL_TRIANGLES inside a GLLockArrays range
 * (DrawLockedTriangles): it gathers the locked range in order, once per
 * draw, and hands the caller's indices to the hardware as one indexed
 * draw (see g_mglv3d_lock_epoch below).
 *
 * GLLockArrays/GLUnlockArrays (GL_EXT_compiled_vertex_array): see their
 * own comment.
 */

#include "sysinc.h"
#include "../../backend/hw/v3d_debug.h"

static char rcsid[] = "$Id: vertexelements.c,v 1.1.1.1 2000/04/07 19:44:51 hfrieden Exp $";

extern void d_DrawTriangles(GLcontext context);
extern void d_DrawTriangleStrip(GLcontext context);
extern void d_DrawTriangleFan(GLcontext context);
extern void d_DrawPoints(GLcontext context);
extern void d_DrawLines(GLcontext context);
extern void d_DrawLineStrip(GLcontext context);
extern void d_DrawLineLoop(GLcontext context);
extern void d_DrawTrianglesLocked(GLcontext context, int nidx, GLenum type, const GLvoid *indices, int first);

/* draw.c skips its per-vertex real-w scan while this is set. Every d_Draw*
 * call below draws only vertices GatherVertexFromIndex or a specialised
 * gather (vertexarray.c) has just written, and both store w = q = 1.0f for a
 * position of fewer than 4 components. Set right before the call and cleared
 * right after it, so no other draw sees it (vertexarray.c keeps the same
 * bracket). */
extern int g_mglv3d_vb_affine;
#define DRAW_GATHERED(call) \
	do { g_mglv3d_vb_affine = (context->ArrayPointer.vertexsize < 4); call; g_mglv3d_vb_affine = 0; } while (0)

/* vertexarray.c: the byte-to-float table and its FPCR sync, and the
 * specialised gathers (see mglv3d_gather_fast there). */
extern float g_ub2f[256];
extern void mglv3d_ub2f_sync(GLcontext context);
extern int mglv3d_gather_fast(GLcontext context, int src, int n);
extern int mglv3d_gather_fast_indexed(GLcontext context, GLenum type, const GLvoid *indices,
                                      int base, int n, int elem_snapshots);


/*
 * Lock-aware GL_TRIANGLES. The ordinary path copies one vertex per INDEX and
 * packs every copy into fresh GPU buffers, so a surface drawn in several
 * passes repeats all of it per pass. Inside a lock this path copies each
 * locked vertex once per draw instead, packs the locked positions on the
 * first draw of the lock and reuses them for later draws while draw.c's
 * command-list buffer is unchanged (draw.c keeps them, s_lockpos), and draws
 * the caller's indices with IndexedPrimList.
 *
 * g_mglv3d_lock_epoch changes on every lock, unlock and position-array change,
 * so positions packed for one lock are never used for another -- a caller can
 * lock the SAME position buffer for every surface, with different contents.
 */
unsigned long g_mglv3d_lock_epoch = 0;

/*
 * Reads one index out of the caller's raw index array at position i,
 * widening GL_UNSIGNED_BYTE/GL_UNSIGNED_SHORT/GL_UNSIGNED_INT to a plain
 * int uniformly.
 */
static int ReadIndex(GLenum type, const GLvoid *indices, int i)
{
	switch (type)
	{
		case GL_UNSIGNED_BYTE:
			return (int)((const GLubyte*)indices)[i];
		case GL_UNSIGNED_SHORT:
			return (int)((const GLushort*)indices)[i];
		case GL_UNSIGNED_INT:
		default:
			return (int)((const GLuint*)indices)[i];
	}
}

/*
 * Shared gather step -- one indexed vertex (srcIndex, per
 * context->ArrayPointer) copied into context->VertexBuffer[dstIndex].
 * Identical in shape to vertexarray.c's own GatherVertexFromArray (same
 * per-field logic, same MGLAColorMode switch), apart from the elemPos
 * texcoord snapshot -- kept as its own local copy rather than shared
 * across files to avoid a new extern/header for a single small static
 * helper; if this drifts out of sync with vertexarray.c's copy during a
 * future change, fix both.
 *
 * noinline: inlined into one of GLDrawElements' loops, it would make every
 * GLDrawElements call save and restore the FPU registers it uses.
 */
static __attribute__((noinline)) void GatherVertexFromIndex(GLcontext context, int dstIndex, int srcIndex, int elemPos)
{
	MGLVertex *dst = &context->VertexBuffer[dstIndex];
	float x, y, z, w;
	GLubyte *vbytes = context->ArrayPointer.verts + srcIndex * context->ArrayPointer.vertexstride;

	if (context->ArrayPointer.state & AP_FIXPOINT)
	{
		/* GL_INT array: integer coordinates, (float) conversion -- same as
		 * vertexarray.c's GatherVertexFromArray; the original's semantics are
		 * explained in vertexarray.c's header comment. */
		const GLint *vsrc = (const GLint*)vbytes;

		x = (float)vsrc[0];
		y = (float)vsrc[1];
		z = (context->ArrayPointer.vertexsize >= 3) ? (float)vsrc[2] : 0.0f;
		w = (context->ArrayPointer.vertexsize >= 4) ? (float)vsrc[3] : 1.0f;
	}
	else
	{
		const GLfloat *vsrc = (const GLfloat*)vbytes;

		x = vsrc[0];
		y = vsrc[1];
		z = (context->ArrayPointer.vertexsize >= 3) ? vsrc[2] : 0.0f;
		w = (context->ArrayPointer.vertexsize >= 4) ? vsrc[3] : 1.0f;
	}

	dst->bx = x; dst->by = y; dst->bz = z; dst->bw = w;
	dst->v.x = x; dst->v.y = y; dst->v.z = z; dst->v.w = w;
	dst->normal = 0;
	/* Real per-pixel Q perspective correction: same as vertexarray.c's
	 * GatherVertexFromArray -- array-mode vertices never go through
	 * GLTexCoord4f, so `.q` would otherwise be stale garbage from an
	 * unrelated earlier draw reusing this same VertexBuffer slot,
	 * spuriously tripping draw.c's draw_has_real_w pre-scan (`.q != 1.0f`)
	 * for a 4-component position array. For fewer components DRAW_GATHERED
	 * skips that scan, which is only equivalent because this store makes q
	 * 1.0f. */
	dst->q = 1.0f;

	/* NULL test mirrors vertexarray.c: a refused glColorPointer fails closed
	 * (see GLVertexPointer's policy comment there), and the current colour is
	 * the right fallback rather than a stale array. */
	if ((context->ClientState & GLCS_COLOR) && context->ArrayPointer.colors != NULL)
	{
		GLubyte *csrc = context->ArrayPointer.colors + srcIndex * context->ArrayPointer.colorstride;

		/* Byte colours read g_ub2f, which holds (float)c / 255.0f under this
		 * FPCR: GLDrawElements syncs it before gathering. */
		switch (context->ArrayPointer.colormode)
		{
			case MGLA_COLOR_FLOAT_RGB:
			{
				GLfloat *c = (GLfloat*)csrc;
				dst->color.r = c[0]; dst->color.g = c[1]; dst->color.b = c[2]; dst->color.a = 1.0f;
				break;
			}
			case MGLA_COLOR_FLOAT_RGBA:
			{
				GLfloat *c = (GLfloat*)csrc;
				dst->color.r = c[0]; dst->color.g = c[1]; dst->color.b = c[2]; dst->color.a = c[3];
				break;
			}
			case MGLA_COLOR_UBYTE_RGB:
				dst->color.r = g_ub2f[csrc[0]];
				dst->color.g = g_ub2f[csrc[1]];
				dst->color.b = g_ub2f[csrc[2]];
				dst->color.a = 1.0f;
				break;
			case MGLA_COLOR_UBYTE_RGBA:
			default:
				dst->color.r = g_ub2f[csrc[0]];
				dst->color.g = g_ub2f[csrc[1]];
				dst->color.b = g_ub2f[csrc[2]];
				dst->color.a = g_ub2f[csrc[3]];
				break;
			/* MGL_UBYTE_ARGB: byte order per the original's Convert_UB_ARGB --
			 * byte0=A, byte1=R, byte2=G, byte3=B, a genuine 4-way permutation
			 * of RGBA, not a 2-byte swap. */
			case MGLA_COLOR_UBYTE_ARGB:
				dst->color.a = g_ub2f[csrc[0]];
				dst->color.r = g_ub2f[csrc[1]];
				dst->color.g = g_ub2f[csrc[2]];
				dst->color.b = g_ub2f[csrc[3]];
				break;
			/* MGL_UBYTE_BGRA: byte order per the original's
			 * Convert_UB_BGR/BGRA -- byte0=B, byte1=G, byte2=R, byte3=A. */
			case MGLA_COLOR_UBYTE_BGR:
				dst->color.b = g_ub2f[csrc[0]];
				dst->color.g = g_ub2f[csrc[1]];
				dst->color.r = g_ub2f[csrc[2]];
				dst->color.a = 1.0f;
				break;
			case MGLA_COLOR_UBYTE_BGRA:
				dst->color.b = g_ub2f[csrc[0]];
				dst->color.g = g_ub2f[csrc[1]];
				dst->color.r = g_ub2f[csrc[2]];
				dst->color.a = g_ub2f[csrc[3]];
				break;
		}
	}
	else
	{
		dst->color.r = context->CurrentColor.r;
		dst->color.g = context->CurrentColor.g;
		dst->color.b = context->CurrentColor.b;
		dst->color.a = context->CurrentColor.a;
	}

	if ((context->ClientState & GLCS_TEXTURE) && context->ArrayPointer.texcoords != NULL)
	{
		GLfloat *tsrc = (GLfloat*)(context->ArrayPointer.texcoords + srcIndex * context->ArrayPointer.texcoordstride);
		dst->v.u0 = tsrc[0];
		dst->v.v0 = (context->ArrayPointer.texcoordsize >= 2) ? tsrc[1] : 0.0f;
	}
	else if (elemPos >= 0)
	{
		/* elemPos >= 0 means this gather call is for the internal
		 * GLArrayElement/GLEnd batch (indices == context->ElementIndex,
		 * checked once in GLDrawElements below) -- use THIS SLOT's own
		 * snapshot, taken when glArrayElement was called for it, not
		 * whatever CurrentTexS/T holds NOW. Gathering only happens once
		 * per glEnd (i.e. once per whole strip/fan), by which point every
		 * earlier glTexCoord2f() call in the SAME primitive has already
		 * overwritten the live scalar -- using it here would stamp every
		 * vertex in the primitive with the LAST vertex's texcoord. See
		 * context.h's ElementTexS/ElementTexT comment. */
		dst->v.u0 = context->ElementTexS[elemPos];
		dst->v.v0 = context->ElementTexT[elemPos];
	}
	else
	{
		/* A direct glDrawElements() call (not via GLArrayElement/GLEnd) has
		 * no per-slot snapshot to use: whatever the caller's last
		 * glTexCoord2f() set, applied uniformly. Same pattern as above for
		 * color via CurrentColor. */
		dst->v.u0 = context->CurrentTexS;
		dst->v.v0 = context->CurrentTexT;
	}

	/* Texture unit 1, mirroring vertexarray.c's GatherVertexFromArray. Unit 1
	 * has no ElementTexS/T-style per-slot snapshot; explicitly zero when the
	 * array isn't enabled rather than leave stale VertexBuffer[] contents
	 * from a previous draw. */
	if ((context->ClientState & GLCS_TEXTURE1) && context->ArrayPointer.texcoords1 != NULL)
	{
		GLfloat *tsrc1 = (GLfloat*)(context->ArrayPointer.texcoords1 + srcIndex * context->ArrayPointer.texcoordstride1);
		dst->v.u1 = tsrc1[0];
		dst->v.v1 = (context->ArrayPointer.texcoordsize1 >= 2) ? tsrc1[1] : 0.0f;
	}
	else
	{
		dst->v.u1 = 0.0f;
		dst->v.v1 = 0.0f;
	}
}


/*
 * GL_TRIANGLES inside a lock, see g_mglv3d_lock_epoch above. Returns 0, having
 * drawn nothing, when the draw does not fit: an index outside the locked
 * range, a locked range larger than the vertex buffer or 16-bit indices, or a
 * 4-component position (a real w takes the gather path's real-w shaders).
 */
static int DrawLockedTriangles(GLcontext context, int count, GLenum type, const GLvoid *indices, int chunk_cap)
{
	int first  = (int)context->ArrayPointer.lockfirst;
	int nverts = (int)context->ArrayPointer.locksize;
	int n = (count / 3) * 3;
	int i;

	if (n == 0 || nverts <= 0 || nverts > chunk_cap || nverts > 65536 ||
	    context->ArrayPointer.vertexsize >= 4)
		return 0;

	/* Unsigned compare: below `first` wraps to a huge value. */
	switch (type)
	{
		case GL_UNSIGNED_BYTE:
		{
			const GLubyte *p = (const GLubyte *)indices;
			for (i = 0; i < n; i++)
				if ((GLuint)((int)p[i] - first) >= (GLuint)nverts) return 0;
			break;
		}
		case GL_UNSIGNED_SHORT:
		{
			const GLushort *p = (const GLushort *)indices;
			for (i = 0; i < n; i++)
				if ((GLuint)((int)p[i] - first) >= (GLuint)nverts) return 0;
			break;
		}
		default:
		{
			const GLuint *p = (const GLuint *)indices;
			for (i = 0; i < n; i++)
				if (p[i] - (GLuint)first >= (GLuint)nverts) return 0;
			break;
		}
	}

	if (mglv3d_gather_fast(context, first, nverts))
	{
	}
	else
	{
		for (i = 0; i < nverts; i++)
			GatherVertexFromIndex(context, i, first + i, -1);
	}

	context->VertexBufferPointer = nverts;
	DRAW_GATHERED(d_DrawTrianglesLocked(context, n, type, indices, first));
	return 1;
}

void GLDrawElements(GLcontext context, GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
	int chunk_cap;
	/* Only the internal GLEnd-driven batch (GLArrayElement populated
	 * ElementTexS/ElementTexT in lockstep with context->ElementIndex,
	 * which is exactly the array GLEnd passes as `indices` here) has
	 * valid per-slot snapshots to gather from -- a direct glDrawElements()
	 * call passes its own, different index array. See context.h's
	 * ElementTexS/ElementTexT comment. */
	int fromArrayElement = (indices == (const GLvoid *)context->ElementIndex);

	if (!(context->ClientState & GLCS_VERTEX))
	{
		D(("GLDrawElements: GL_VERTEX_ARRAY not enabled, dropping\n"));
		GLFlagError(context, 1, GL_INVALID_OPERATION);
		return;
	}

	/* Same fail-closed guard as GLDrawArrays: no position array means a
	 * glVertexPointer or glInterleavedArrays call was refused, and gathering
	 * from a pointer the application may have freed is the thing this policy
	 * exists to stop. */
	if (context->ArrayPointer.verts == NULL)
	{
		D(("GLDrawElements: no vertex array bound (a refused glVertexPointer), dropping\n"));
		return;
	}

	if (mode != GL_TRIANGLES && mode != GL_TRIANGLE_STRIP && mode != GL_TRIANGLE_FAN && mode != GL_POLYGON &&
	    mode != GL_QUADS && mode != GL_QUAD_STRIP &&
	    mode != GL_POINTS && mode != GL_LINES && mode != GL_LINE_STRIP && mode != GL_LINE_LOOP)
	{
		D(("GLDrawElements: mode %ld not supported, dropping\n", (LONG)mode));
		GLFlagError(context, 1, GL_INVALID_OPERATION);
		return;
	}

	if (type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT)
	{
		D(("GLDrawElements: unsupported index type %ld, dropping\n", (LONG)type));
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	/* Per-mode minimum -- see GLDrawArrays (vertexarray.c). */
	{
		int min_verts = 3;

		if (mode == GL_POINTS)
			min_verts = 1;
		else if (mode == GL_LINES || mode == GL_LINE_STRIP || mode == GL_LINE_LOOP)
			min_verts = 2;

		if (count < min_verts)
			return;
	}

	mglv3d_ub2f_sync(context);

	chunk_cap = (int)context->VertexBufferSize;
	if (chunk_cap > MGL_MAXVERTS)
		chunk_cap = MGL_MAXVERTS;

	/* GL_POINTS / GL_LINES / GL_LINE_STRIP / GL_LINE_LOOP: the same chunking
	 * rules as GLDrawArrays -- see its comment for why lines need an even
	 * chunk, why a line strip carries one vertex rather than a triangle
	 * strip's two, and how a loop too large for one chunk is closed. This
	 * path also carries glBegin(GL_LINES) + glArrayElement + glEnd, which
	 * GLEnd forwards here with the recorded primitive. Element positions are
	 * passed through to the per-slot snapshot the same way the triangle
	 * paths do it. */
	if (mode == GL_POINTS)
	{
		int base;

		for (base = 0; base < count; base += chunk_cap)
		{
			int n = count - base;
			int i;

			if (n > chunk_cap)
				n = chunk_cap;

			for (i = 0; i < n; i++)
				GatherVertexFromIndex(context, i, ReadIndex(type, indices, base + i),
				                      fromArrayElement ? (base + i) : -1);

			context->VertexBufferPointer = n;
			DRAW_GATHERED(d_DrawPoints(context));
		}
		return;
	}

	if (mode == GL_LINES)
	{
		int line_chunk_cap = chunk_cap & ~1;
		int base;

		if (line_chunk_cap < 2)
		{
			D(("GLDrawElements: VertexBufferSize too small to hold one line, dropping\n"));
			return;
		}

		count &= ~1;

		for (base = 0; base < count; base += line_chunk_cap)
		{
			int n = count - base;
			int i;

			if (n > line_chunk_cap)
				n = line_chunk_cap;

			for (i = 0; i < n; i++)
				GatherVertexFromIndex(context, i, ReadIndex(type, indices, base + i),
				                      fromArrayElement ? (base + i) : -1);

			context->VertexBufferPointer = n;
			DRAW_GATHERED(d_DrawLines(context));
		}
		return;
	}

	if (mode == GL_LINE_STRIP || mode == GL_LINE_LOOP)
	{
		int closed = (mode == GL_LINE_LOOP);
		int used = 0;
		int first_chunk = 1;

		if (chunk_cap < 2)
		{
			D(("GLDrawElements: VertexBufferSize too small to hold one line-strip chunk, dropping\n"));
			return;
		}

		if (closed && count <= chunk_cap)
		{
			int i;

			for (i = 0; i < count; i++)
				GatherVertexFromIndex(context, i, ReadIndex(type, indices, i),
				                      fromArrayElement ? i : -1);

			context->VertexBufferPointer = count;
			DRAW_GATHERED(d_DrawLineLoop(context));
			return;
		}

		while (used < count)
		{
			int slot = 0;
			int new_count, k;

			if (!first_chunk)
				GatherVertexFromIndex(context, slot++, ReadIndex(type, indices, used - 1),
				                      fromArrayElement ? (used - 1) : -1);

			new_count = first_chunk ? chunk_cap : (chunk_cap - 1);
			if (new_count > count - used)
				new_count = count - used;

			for (k = 0; k < new_count; k++)
				GatherVertexFromIndex(context, slot++, ReadIndex(type, indices, used + k),
				                      fromArrayElement ? (used + k) : -1);

			used += new_count;

			context->VertexBufferPointer = slot;
			DRAW_GATHERED(d_DrawLineStrip(context));

			first_chunk = 0;
		}

		if (closed)
		{
			GatherVertexFromIndex(context, 0, ReadIndex(type, indices, count - 1),
			                      fromArrayElement ? (count - 1) : -1);
			GatherVertexFromIndex(context, 1, ReadIndex(type, indices, 0),
			                      fromArrayElement ? 0 : -1);
			context->VertexBufferPointer = 2;
			DRAW_GATHERED(d_DrawLines(context));
		}
		return;
	}

	/* GL_QUADS / GL_QUAD_STRIP / GL_POLYGON: same treatment as GLDrawArrays
	 * (vertexarray.c) -- see its comment. A quad strip is a triangle strip
	 * with the same vertex order and winding; a polygon is a fan;
	 * independent quads become two triangles each, six slots per quad. The
	 * per-slot snapshot position handed to GatherVertexFromIndex is the
	 * ORIGINAL element position, so the GLEnd-driven glArrayElement path
	 * keeps its texcoord snapshots aligned. */
	if (mode == GL_QUAD_STRIP)
	{
		if (count < 4)
			return;
		count &= ~1;
		mode = GL_TRIANGLE_STRIP;
	}
	else if (mode == GL_POLYGON)
	{
		mode = GL_TRIANGLE_FAN;
	}
	else if (mode == GL_QUADS)
	{
		static const int corner[6] = { 0, 1, 2, 0, 2, 3 };
		int quads = count / 4;
		int quads_per_chunk = chunk_cap / 6;
		int q = 0;

		if (quads_per_chunk < 1)
		{
			D(("GLDrawElements: VertexBufferSize too small to hold even one quad, dropping\n"));
			return;
		}

		while (q < quads)
		{
			int n = quads - q;
			int i, slot = 0;

			if (n > quads_per_chunk)
				n = quads_per_chunk;

			for (i = 0; i < n; i++)
			{
				int k;
				for (k = 0; k < 6; k++)
				{
					int e = (q + i) * 4 + corner[k];
					GatherVertexFromIndex(context, slot++, ReadIndex(type, indices, e),
					                      fromArrayElement ? e : -1);
				}
			}

			context->VertexBufferPointer = slot;
			DRAW_GATHERED(d_DrawTriangles(context));
			q += n;
		}
		return;
	}

	if (mode == GL_TRIANGLES)
	{
		int base;
		int tri_chunk_cap = (chunk_cap / 3) * 3;

		if (tri_chunk_cap < 3)
		{
			D(("GLDrawElements: VertexBufferSize too small to hold even one triangle, dropping\n"));
			return;
		}

		if (context->ArrayPointer.locksize > 0 && !fromArrayElement &&
		    DrawLockedTriangles(context, count, type, indices, chunk_cap))
			return;

		for (base = 0; base < count; base += tri_chunk_cap)
		{
			int n = count - base;
			int i;

			if (n > tri_chunk_cap)
				n = tri_chunk_cap;
			n = (n / 3) * 3;
			if (n == 0)
				break;

			if (mglv3d_gather_fast_indexed(context, type, indices, base, n, fromArrayElement))
			{
			}
			else
			{
				for (i = 0; i < n; i++)
					GatherVertexFromIndex(context, i, ReadIndex(type, indices, base + i),
					                      fromArrayElement ? (base + i) : -1);
			}

			context->VertexBufferPointer = n;
			DRAW_GATHERED(d_DrawTriangles(context));
		}
	}
	else if (mode == GL_TRIANGLE_STRIP)
	{
		/* Multi-chunk continuity -- see GLDrawArrays' (vertexarray.c) own
		 * comment on this exact algorithm and why the chunk size must be
		 * forced EVEN (so every chunk boundary lands on an original
		 * triangle index whose parity already matches what a fresh native
		 * V3D_PRIM_TRIANGLESTRIP call's own hardware winding-alternation
		 * assumes) rather than swapping carried-over vertex order (which
		 * would corrupt later triangles' vertex sets in the SAME chunk --
		 * a native strip is a sliding window over its own local array, not
		 * independent per-triangle calls). Identical shape here, just
		 * reading through ReadIndex() instead of a direct first+i offset. */
		int chunk_cap_strip = chunk_cap;
		int used, first_chunk;

		if (chunk_cap_strip & 1)
			chunk_cap_strip--;

		if (chunk_cap_strip < 4)
		{
			D(("GLDrawElements: VertexBufferSize too small to hold even one strip chunk, dropping\n"));
			return;
		}

		used = 0;
		first_chunk = 1;

		while (used < count)
		{
			int local_count, slot;

			if (first_chunk)
			{
				local_count = (count - used < chunk_cap_strip) ? (count - used) : chunk_cap_strip;

				for (slot = 0; slot < local_count; slot++)
					GatherVertexFromIndex(context, slot, ReadIndex(type, indices, used + slot),
					                      fromArrayElement ? (used + slot) : -1);

				used += local_count;
			}
			else
			{
				int new_count = (count - used < chunk_cap_strip - 2) ? (count - used) : (chunk_cap_strip - 2);

				GatherVertexFromIndex(context, 0, ReadIndex(type, indices, used - 2),
				                      fromArrayElement ? (used - 2) : -1);
				GatherVertexFromIndex(context, 1, ReadIndex(type, indices, used - 1),
				                      fromArrayElement ? (used - 1) : -1);

				for (slot = 0; slot < new_count; slot++)
					GatherVertexFromIndex(context, 2 + slot, ReadIndex(type, indices, used + slot),
					                      fromArrayElement ? (used + slot) : -1);

				local_count = 2 + new_count;
				used += new_count;
			}

			context->VertexBufferPointer = local_count;
			DRAW_GATHERED(d_DrawTriangleStrip(context));

			first_chunk = 0;
		}
	}
	else /* GL_TRIANGLE_FAN */
	{
		/* No winding-alternation concern -- see GLDrawArrays' own comment
		 * on why the fan case is simpler than the strip case above. */
		int perim_count = count - 1;
		int perim_used = 0;
		int first_chunk = 1;
		int hub_index = ReadIndex(type, indices, 0);

		if (chunk_cap < 3)
		{
			D(("GLDrawElements: VertexBufferSize too small to hold even one fan chunk, dropping\n"));
			return;
		}

		while (perim_used < perim_count)
		{
			int slot = 0;
			int new_count;

			GatherVertexFromIndex(context, slot++, hub_index, fromArrayElement ? 0 : -1);

			if (!first_chunk)
				GatherVertexFromIndex(context, slot++, ReadIndex(type, indices, perim_used),
				                      fromArrayElement ? perim_used : -1);

			new_count = first_chunk ? (chunk_cap - 1) : (chunk_cap - 2);
			if (new_count > perim_count - perim_used)
				new_count = perim_count - perim_used;

			{
				int k;
				for (k = 0; k < new_count; k++)
					GatherVertexFromIndex(context, slot++, ReadIndex(type, indices, 1 + perim_used + k),
					                      fromArrayElement ? (1 + perim_used + k) : -1);
			}

			perim_used += new_count;

			context->VertexBufferPointer = slot;
			DRAW_GATHERED(d_DrawTriangleFan(context));

			first_chunk = 0;
		}
	}
}

/*
 * GLLockArrays/GLUnlockArrays (GL_EXT_compiled_vertex_array).
 * The original's implementation (MiniGL/src/vertexelements.c) is a
 * CPU-side pre-transform-and-project cache that writes directly into
 * Warp3D's own w3dContext->VertexPointer/VPMode fields for its
 * fixed-function vertex-array renderer -- none of which exist in this
 * port.
 *
 * GL_EXT_compiled_vertex_array lets a conformant implementation ignore
 * the lock, so DrawLockedTriangles may decline a draw and leave it to the
 * ordinary path.
 *
 * The error checks match the original's, so apps that call glGetError()
 * around these see the same success/failure behavior.
 * No pre-transform is done here. The lock range is what the lock-aware
 * GL_TRIANGLES path (DrawLockedTriangles, above) draws from, and every lock
 * and unlock starts a new epoch for the positions it packs.
 */
void GLLockArrays(GLcontext context, GLuint first, GLsizei count)
{
	if (!(context->ClientState & GLCS_VERTEX))
	{
		GLFlagError(context, 1, GL_INVALID_OPERATION);
		return;
	}

	if (context->VertexArrayPipeline == GL_FALSE)
	{
		GLFlagError(context, 1, GL_INVALID_OPERATION);
		return;
	}

	context->ArrayPointer.lockfirst = first;
	context->ArrayPointer.locksize  = count;
	g_mglv3d_lock_epoch++;
}

void GLUnlockArrays(GLcontext context)
{
	if (context->ArrayPointer.locksize == 0)
	{
		GLFlagError(context, 1, GL_INVALID_OPERATION);
		return;
	}

	context->ArrayPointer.lockfirst = 0;
	context->ArrayPointer.locksize  = 0;
	g_mglv3d_lock_epoch++;
}

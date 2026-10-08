/*
 * (C) 2025-2026 Dennis van der Boon
 *
 * Display lists, as CAPTURED VERTEX BATCHES rather than a recorded command
 * stream.
 *
 * WHAT IS CAPTURED. A glBegin/glEnd block inside glNewList..glEndList already
 * accumulates finished vertices in context->VertexBuffer -- GLVertex4f latches the
 * current colour, texture coordinates and normal INDEX into each vertex as it goes.
 * So the whole of a block's per-vertex state is already sitting in one flat array
 * by the time GLEnd is reached, and capturing means copying that array out. Replay
 * points the context's buffers at the copy and runs GLEnd's own path.
 *
 * WHY NOT A COMMAND STREAM. MESA does not record vertices as command nodes either
 * (OPCODE_VERTEX_LIST), and here the reason is measurable: glxsglut scene03's list
 * is a single glBegin(GL_QUADS) block of 127*127*4 = 64,516 vertices. A command
 * stream replays 64,516 glVertex3f calls per frame and hits the same vertex-buffer
 * ceiling on every one. This replays none of them and reuses d_DrawQuads' existing
 * 4-slot flat-colour batching untouched.
 *
 * WHY NOT BAKED GEOMETRY. Nothing is pre-transformed, and that is load-bearing, not
 * laziness. scene06's ship is sphere-mapped: v_GenTexCoords computes its texture
 * coordinates from the modelview in force during CurrentDraw, so a replay under a
 * rotated modelview must recompute them. Storing transformed vertices would freeze
 * the ship's reflection. The same applies to any texgen mode and to fog.
 *
 * THE ONE PIECE OF STATE THAT NEEDS SPECIAL HANDLING IS COLOUR, and it is subtle
 * enough to be worth stating plainly. GLVertex4f latches the CURRENT colour into
 * every vertex, so a naive capture freezes it. But GL says a vertex that set no
 * colour of its own inside the list takes the colour current at EXECUTION time --
 * and scene03 calls glColor4f every frame, outside its list. Frozen colour would
 * pin its terrain to whatever was current when the list was built.
 *
 * So capture checks whether the block's vertex colours are all identical. If they
 * are, the block said nothing about colour and the batch is marked colour-agnostic:
 * replay re-latches the colour current at that moment into the copy. If they
 * differ, the block really did set per-vertex colours and they are replayed as
 * captured. The test costs one O(n) pass at capture, once, and never at replay.
 *
 * SCOPE, stated rather than discovered later:
 *
 *   - Only glBegin/glEnd blocks are recorded. Other GL calls between glNewList and
 *     glEndList EXECUTE IMMEDIATELY and are not replayed. Real GL records most of
 *     them. This is the honest limit of a vertex-batch design and it is what
 *     both of a client application's lists need: each contains exactly one glBegin block
 *     and nothing else.
 *     A list whose effect depends on, say, a glBindTexture inside it will not
 *     reproduce that on replay.
 *
 *   - CAPACITY. A capture can only hold what the vertex buffer held, so a block
 *     longer than VertexBufferSize is truncated at capture exactly as it would be
 *     when drawn directly, with GL_OUT_OF_MEMORY already flagged by GLVertex4f.
 *     scene03's 64,516 needs mglChooseVertexBufferSize before MGLCreateContext;
 *     glutInit already asks for 65,536.
 *
 *   - One list namespace, file-static rather than per-context. GL scopes lists to a
 *     context share group; this driver runs one context at a time (MGLCreateContext
 *     hands back the single mini_CurrentContext the gl* macros dereference), so a
 *     per-context table would hold the same thing. It is kept out of GLcontext
 *     deliberately: that struct's size is part of the shared library's structSize
 *     contract, and display lists do not need to move it.
 * ========================================================================== */

#include "sysinc.h"
#include "../../backend/hw/v3d_debug.h"     /* D() */

#include <mgl/gl.h>
#include <stdlib.h>
#include <string.h>

/*
 * fog.c exports no header, so each caller declares this itself --
 * vertexbuffer_min.c:59 does the same.
 *
 * WORTH KNOWING BEFORE TRUSTING THE CALL BELOW: fog_Set has no observable effect in
 * the current architecture. Its whole body is three stores
 * (backend.fog_enable/fog_start/fog_end from Fog_State/FogStart/FogEnd), and
 * draw.c:3032-3037 performs those same three stores from the same GL state on EVERY
 * draw call -- its comment says "fog_Set's three stores, inline". So the GLEnd call
 * this replay mirrors is already dead weight.
 *
 * It is mirrored anyway, deliberately: if draw.c ever stops doing that inline sync,
 * the direct path and the replay path must not diverge silently. A guarded no-op is
 * cheaper than that class of bug.
 */
extern void fog_Set(GLcontext context);

/* One recorded glBegin/glEnd block. */
typedef struct
{
    GLenum      primitive;      /* CurrentPrimitive at capture */
    DrawFn      draw;           /* CurrentDraw; NULL for an ArrayElement batch */
    MGLVertex  *verts;
    GLuint      nverts;
    MGLNormal  *norms;          /* the whole NormalBuffer prefix, indices intact */
    GLuint      nnorms;
    GLuint     *elems;          /* non-NULL only for an ArrayElement batch */
    GLboolean   colour_agnostic;/* every captured colour identical -> re-latch */
} dl_batch;

typedef struct
{
    GLboolean   inuse;
    dl_batch   *batch;
    GLuint      count, cap;
} dl_list;

/*
 * Name 0 is reserved by GL and never handed out, so entry 0 of this table is
 * unused and a list name indexes it directly.
 */
static dl_list *s_list;
static GLuint   s_nlist;

static GLuint   s_compiling;    /* list being built, 0 when not compiling */
static GLenum   s_mode;         /* GL_COMPILE or GL_COMPILE_AND_EXECUTE */

/* ---------------------------------------------------------------- table ---- */

static GLboolean dl_Grow(GLuint need)
{
    GLuint   n = (s_nlist == 0) ? 64 : s_nlist;
    dl_list *p;

    if (need < s_nlist)
        return GL_TRUE;

    while (n <= need)
        n *= 2;

    p = (dl_list *)realloc(s_list, (size_t)n * sizeof(dl_list));
    if (p == NULL)
        return GL_FALSE;

    memset(p + s_nlist, 0, (size_t)(n - s_nlist) * sizeof(dl_list));
    s_list  = p;
    s_nlist = n;
    return GL_TRUE;
}

static void dl_FreeBatches(dl_list *L)
{
    GLuint i;

    for (i = 0; i < L->count; i++)
    {
        free(L->batch[i].verts);
        free(L->batch[i].norms);
        free(L->batch[i].elems);
    }
    free(L->batch);
    L->batch = NULL;
    L->count = L->cap = 0;
}

/*
 * Context teardown. The name table only ever grows, and glDeleteLists frees a
 * list's batches without returning the table itself, so nothing here was ever
 * given back. These are file statics, so in the resident library the next
 * program would inherit this one's table. Safe on an already-deleted entry:
 * dl_FreeBatches left count 0 and batch NULL.
 */
void dl_FreeAll(void)
{
    GLuint i;

    for (i = 0; i < s_nlist; i++)
        dl_FreeBatches(&s_list[i]);

    free(s_list);
    s_list      = NULL;
    s_nlist     = 0;
    s_compiling = 0;
}

/* ---------------------------------------------------------------- names ---- */

/*
 * GL requires `range` CONSECUTIVE unused names and returns the first, or 0 if it
 * cannot. Consecutive matters: a caller may legitimately do
 * `base = glGenLists(4)` and then address base+1 .. base+3 itself.
 */
GLuint GLGenLists(GLcontext context, GLsizei range)
{
    GLuint first, i;

    (void)context;

    if (range < 0)
    {
        GLFlagError(context, 1, GL_INVALID_VALUE);
        return 0;
    }
    if (range == 0)
        return 0;

    for (first = 1; ; first++)
    {
        if (!dl_Grow(first + (GLuint)range))
            return 0;

        for (i = 0; i < (GLuint)range; i++)
            if (s_list[first + i].inuse)
                break;

        if (i == (GLuint)range)
            break;

        first += i;             /* skip the whole occupied stretch, not one name */
    }

    for (i = 0; i < (GLuint)range; i++)
    {
        s_list[first + i].inuse = GL_TRUE;
        s_list[first + i].batch = NULL;
        s_list[first + i].count = s_list[first + i].cap = 0;
    }
    return first;
}

void GLDeleteLists(GLcontext context, GLuint list, GLsizei range)
{
    GLuint i;

    (void)context;

    if (range < 0)
    {
        GLFlagError(context, 1, GL_INVALID_VALUE);
        return;
    }

    /* GL says names that are not lists are silently ignored, and 0 always is. */
    for (i = 0; i < (GLuint)range; i++)
    {
        GLuint n = list + i;

        if (n == 0 || n >= s_nlist || !s_list[n].inuse)
            continue;

        /* Deleting the list being compiled would leave s_compiling dangling. */
        if (n == s_compiling)
            s_compiling = 0;

        dl_FreeBatches(&s_list[n]);
        s_list[n].inuse = GL_FALSE;
    }
}

GLboolean GLIsList(GLcontext context, GLuint list)
{
    (void)context;
    return (list != 0 && list < s_nlist && s_list[list].inuse) ? GL_TRUE : GL_FALSE;
}

/* -------------------------------------------------------------- compile ---- */

void GLNewList(GLcontext context, GLuint list, GLenum mode)
{
    if (mode != GL_COMPILE && mode != GL_COMPILE_AND_EXECUTE)
    {
        GLFlagError(context, 1, GL_INVALID_ENUM);
        return;
    }
    if (list == 0)
    {
        GLFlagError(context, 1, GL_INVALID_VALUE);
        return;
    }
    if (s_compiling != 0)
    {
        /* GL: glNewList inside glNewList is GL_INVALID_OPERATION. */
        GLFlagError(context, 1, GL_INVALID_OPERATION);
        return;
    }
    if (!dl_Grow(list + 1))
    {
        GLFlagError(context, 1, GL_OUT_OF_MEMORY);
        return;
    }

    /* glNewList on a name glGenLists never handed out is legal -- it defines it. */
    if (s_list[list].inuse)
        dl_FreeBatches(&s_list[list]);       /* redefining replaces */
    s_list[list].inuse = GL_TRUE;

    s_compiling = list;
    s_mode      = mode;
}

void GLEndList(GLcontext context)
{
    if (s_compiling == 0)
    {
        GLFlagError(context, 1, GL_INVALID_OPERATION);
        return;
    }
    s_compiling = 0;
}

/*
 * Called from GLEnd, with the block's vertices still in context->VertexBuffer.
 *
 * Returns GL_TRUE when GLEnd should NOT draw -- i.e. the block was captured under
 * GL_COMPILE. Under GL_COMPILE_AND_EXECUTE it captures and returns GL_FALSE so the
 * block also draws now, which is what that mode means.
 */
GLboolean dl_CaptureBatch(GLcontext context)
{
    dl_list  *L;
    dl_batch *b;
    GLuint    nv = context->VertexBufferPointer;
    GLuint    nn;

    if (s_compiling == 0)
        return GL_FALSE;

    L = &s_list[s_compiling];

    if (L->count == L->cap)
    {
        GLuint    cap = (L->cap == 0) ? 4 : L->cap * 2;
        dl_batch *p   = (dl_batch *)realloc(L->batch, (size_t)cap * sizeof(dl_batch));
        if (p == NULL)
        {
            GLFlagError(context, 1, GL_OUT_OF_MEMORY);
            return GL_FALSE;                 /* draw it at least once */
        }
        L->batch = p;
        L->cap   = cap;
    }

    b = &L->batch[L->count];
    memset(b, 0, sizeof(*b));
    b->primitive = context->CurrentPrimitive;

    /*
     * The NORMAL prefix is copied whole, not per referenced index, because
     * MGLVertex.normal is an INDEX into it. Copying [0, NormalBufferPointer]
     * keeps every index valid without renumbering anything. NormalBufferPointer
     * is the NEXT free slot, so the live prefix is that plus one: slot 0 is the
     * default normal and a vertex can legitimately reference it.
     */
    nn = context->NormalBufferPointer + 1;

    b->verts = (MGLVertex *)malloc((size_t)nv * sizeof(MGLVertex));
    b->norms = (MGLNormal *)malloc((size_t)nn * sizeof(MGLNormal));
    if (b->verts == NULL || b->norms == NULL)
    {
        free(b->verts);
        free(b->norms);
        GLFlagError(context, 1, GL_OUT_OF_MEMORY);
        return GL_FALSE;
    }
    memcpy(b->verts, context->VertexBuffer, (size_t)nv * sizeof(MGLVertex));
    memcpy(b->norms, context->NormalBuffer, (size_t)nn * sizeof(MGLNormal));
    b->nverts = nv;
    b->nnorms = nn;

    if (context->UsedArrayElement)
    {
        /* An ArrayElement block dispatches through GLDrawElements rather than
         * CurrentDraw, so its index array has to travel with it. */
        b->elems = (GLuint *)malloc((size_t)nv * sizeof(GLuint));
        if (b->elems == NULL)
        {
            free(b->verts);
            free(b->norms);
            GLFlagError(context, 1, GL_OUT_OF_MEMORY);
            return GL_FALSE;
        }
        memcpy(b->elems, context->ElementIndex, (size_t)nv * sizeof(GLuint));
        b->draw = NULL;
    }
    else
    {
        b->draw = context->CurrentDraw;
    }

    /* Colour uniformity, one pass. See the header comment. */
    {
        GLuint    i;
        MGLColor  c0 = b->verts[0].color;

        b->colour_agnostic = GL_TRUE;
        for (i = 1; i < nv; i++)
        {
            if (b->verts[i].color.r != c0.r || b->verts[i].color.g != c0.g ||
                b->verts[i].color.b != c0.b || b->verts[i].color.a != c0.a)
            {
                b->colour_agnostic = GL_FALSE;
                break;
            }
        }
    }

    L->count++;
    return (s_mode == GL_COMPILE) ? GL_TRUE : GL_FALSE;
}

/* --------------------------------------------------------------- replay ---- */

/*
 * Replay one batch. The context's buffers are pointed at the captured arrays for
 * the duration and restored afterwards -- zero copy, and the reason the design is
 * cheap. Everything the draw reads beyond the vertices (modelview, texture
 * binding, texgen mode, fog, texenv) is whatever is current NOW, which is what
 * makes a list drawn under a rotating modelview animate.
 */
static void dl_ReplayBatch(GLcontext context, dl_batch *b)
{
    MGLVertex *save_vb  = context->VertexBuffer;
    MGLNormal *save_nb  = context->NormalBuffer;
    GLuint     save_vbp = context->VertexBufferPointer;
    GLuint     save_nbp = context->NormalBufferPointer;
    GLuint     save_vbs = context->VertexBufferSize;
    GLenum     save_pr  = context->CurrentPrimitive;
    DrawFn     save_dr  = context->CurrentDraw;
    GLboolean  save_uae = context->UsedArrayElement;

    if (b->nverts == 0)
        return;

    /*
     * A colour-agnostic batch takes the colour current at replay. GL gives a
     * vertex that specified no colour of its own the colour in force at execution,
     * and scene03 depends on it: glColor4f changes every frame, outside the list.
     */
    if (b->colour_agnostic)
    {
        GLuint i;
        for (i = 0; i < b->nverts; i++)
        {
            b->verts[i].color.r = context->CurrentColor.r;
            b->verts[i].color.g = context->CurrentColor.g;
            b->verts[i].color.b = context->CurrentColor.b;
            b->verts[i].color.a = context->CurrentColor.a;
        }
    }

    context->VertexBuffer        = b->verts;
    context->NormalBuffer        = b->norms;
    context->VertexBufferPointer = b->nverts;
    context->NormalBufferPointer = (b->nnorms > 0) ? (b->nnorms - 1) : 0;
    context->VertexBufferSize    = b->nverts;
    context->CurrentPrimitive    = b->primitive;

    /*
     * GLEnd's remaining responsibilities, in its order. The empty-block early-out is
     * handled above; the rest is reproduced here rather than by calling GLEnd,
     * because GLEnd would re-enter dl_CaptureBatch on a nested glCallList inside a
     * list being compiled.
     *
     * The fog step is one of them and is a no-op today -- see the note on fog_Set
     * above. Mirrored for fidelity, not effect.
     */
    if (context->FogDirty && context->Fog_State)
    {
        fog_Set(context);
        context->FogDirty = GL_FALSE;
    }

#ifdef AUTOMATIC_LOCKING_ENABLE
    if (context->LockMode == MGL_LOCK_AUTOMATIC)
        MGLLockDisplay(context);
#endif

    if (b->elems != NULL)
    {
        context->UsedArrayElement = GL_TRUE;
        GLDrawElements(context, b->primitive, (GLsizei)b->nverts,
                       GL_UNSIGNED_INT, b->elems);
    }
    else if (b->draw != NULL)
    {
        context->CurrentDraw = b->draw;
        b->draw(context);
    }
    else
    {
        D(("GLCallList: batch has no draw function, dropping %ld vertices\n",
           (LONG)b->nverts));
    }

#ifdef AUTOMATIC_LOCKING_ENABLE
    if (context->LockMode == MGL_LOCK_AUTOMATIC)
        MGLUnlockDisplay(context);
#endif

    context->VertexBuffer        = save_vb;
    context->NormalBuffer        = save_nb;
    context->VertexBufferPointer = save_vbp;
    context->NormalBufferPointer = save_nbp;
    context->VertexBufferSize    = save_vbs;
    context->CurrentPrimitive    = save_pr;
    context->CurrentDraw         = save_dr;
    context->UsedArrayElement    = save_uae;
}

void GLCallList(GLcontext context, GLuint list)
{
    dl_list *L;
    GLuint   i;

    /* GL: an undefined name is silently ignored, not an error. */
    if (list == 0 || list >= s_nlist || !s_list[list].inuse)
        return;

    L = &s_list[list];
    for (i = 0; i < L->count; i++)
        dl_ReplayBatch(context, &L->batch[i]);
}

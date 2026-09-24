/* Exercise the exact fast-path implementation with a minimal context layout.
 * The production compile additionally verifies the real MiniGL types.
 */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint16_t UWORD;
typedef uint32_t ULONG;
enum { MGL_CLIP_NEGW=1, MGL_CLIP_TOP=2, MGL_CLIP_BOTTOM=4,
       MGL_CLIP_LEFT=8, MGL_CLIP_RIGHT=16, MGL_CLIP_FRONT=32, MGL_CLIP_BACK=64 };
typedef struct { float bx, by, bz, bw; ULONG outcode; } MGLVertex;
typedef struct {
    struct { float v[16]; } CombinedMatrix;
    struct { int vertexstride; unsigned char *verts; } ArrayPointer;
    MGLVertex *VertexBuffer;
} Context, *GLcontext;
#include "../../../mglQ3/src/transform_ortho.h"

static MGLVertex reference(const float *m, const float *p)
{
    MGLVertex v;
    float w;
    v.bx = m[0]*p[0]+m[4]*p[1]+m[8]*p[2]+m[12];
    v.by = m[1]*p[0]+m[5]*p[1]+m[9]*p[2]+m[13];
    v.bz = m[2]*p[0]+m[6]*p[1]+m[10]*p[2]+m[14];
    v.bw = m[3]*p[0]+m[7]*p[1]+m[11]*p[2]+m[15];
    w = v.bw; v.outcode = 0;
    if (w < 1e-7) v.outcode |= MGL_CLIP_NEGW;
    if (-w > v.bx) v.outcode |= MGL_CLIP_LEFT;
    else if (v.bx > w) v.outcode |= MGL_CLIP_RIGHT;
    if (-w > v.by) v.outcode |= MGL_CLIP_BOTTOM;
    else if (v.by > w) v.outcode |= MGL_CLIP_TOP;
    if (-w > v.bz) v.outcode |= MGL_CLIP_BACK;
    else if (v.bz > w) v.outcode |= MGL_CLIP_FRONT;
    return v;
}
static void compare(Context *c, const float p[][7], const UWORD *idx, int n)
{
    int i;
    assert(TransformIndexOrtho(c, n, idx));
    for (i=0; i<n; ++i) {
        MGLVertex r = reference(c->CombinedMatrix.v, p[idx[i]]);
        MGLVertex *v = &c->VertexBuffer[idx[i]];
        assert(v->bx == r.bx && v->by == r.by && v->bz == r.bz && v->bw == r.bw);
        assert(v->outcode == r.outcode);
    }
}
int main(void)
{
    Context c;
    MGLVertex vertices[32], saved[32];
    float p[32][7]; /* non-packed stride and indexed reuse */
    UWORD idx[96];
    const int widths[] = {256,320,512,640,800,1024};
    const int offdiag[] = {1,2,3,4,6,7,8,9,11,15};
    uint32_t rng=12345;
    int i, pass, k;
    memset(&c, 0, sizeof(c)); memset(vertices, 0, sizeof(vertices));
    c.ArrayPointer.verts=(unsigned char *)p;
    c.ArrayPointer.vertexstride=sizeof(p[0]); c.VertexBuffer=vertices;
    for (i=0; i<96; ++i) idx[i]=(UWORD)((i*13)%32);
    for (pass=0; pass<600; ++pass) {
        float width=widths[pass%6], height=pass%2 ? 240.0f : 600.0f;
        float *m=c.CombinedMatrix.v;
        memset(m, 0, sizeof(c.CombinedMatrix.v));
        m[0]=2.0f/width; m[5]=-2.0f/height; m[10]=-1.0f;
        m[12]=-1.0f; m[13]=1.0f; m[15]=1.0f;
        for (i=0; i<32; ++i) for (k=0; k<3; ++k) {
            rng=rng*1664525u+1013904223u;
            p[i][k]=k==2 ? (float)((int)(rng%5)-2) : (float)((int)(rng%4096)-1024);
        }
        p[0][0]=0; p[0][1]=0; p[0][2]=0;
        p[1][0]=width; p[1][1]=height; p[1][2]=0;
        p[2][0]=nextafterf(width, INFINITY); p[2][1]=-1; p[2][2]=1;
        p[3][0]=nextafterf(width, 0); p[3][1]=height+1; p[3][2]=-1;
        compare(&c, p, idx, 96);
        /* Non-orthographic matrices must fall back without touching output. */
        memcpy(saved, vertices, sizeof(saved));
        for (i=0; i<10; ++i) {
            float old=m[offdiag[i]];
            m[offdiag[i]]=i==9 ? 2.0f : 1e-20f;
            assert(!TransformIndexOrtho(&c, 96, idx));
            assert(memcmp(saved, vertices, sizeof(saved))==0);
            m[offdiag[i]]=old;
        }
        assert(TransformIndexOrtho(&c, 0, idx));
        assert(memcmp(saved, vertices, sizeof(saved))==0);
    }
    puts("PASS: 57600 indexed transforms/outcodes, viewport changes, boundary clipping, perspective/rotation fallback.");
    return 0;
}

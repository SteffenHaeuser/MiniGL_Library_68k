#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>

void draw_fast_triangle(void)
{
    MGLFastPath fp = MGL_FASTPATH_INITIALIZER;

    if (!mglBeginFastPath(GL_TRIANGLES,
                          MGL_FASTPATH_POSITION |
                          MGL_FASTPATH_COLOR |
                          MGL_FASTPATH_TEXCOORD0,
                          &fp))
        return;

#define PUT_VERTEX(px,py,pz, cr,cg,cb,ca, ts,tt) do { \
    if (mglFastPathHasRoom(&fp)) { \
        *fp.x=(px); *fp.y=(py); *fp.z=(pz); *fp.w=1.0f; \
        *fp.r=(cr); *fp.g=(cg); *fp.b=(cb); *fp.a=(ca); \
        *fp.u0=(ts); *fp.v0=(tt); \
        mglAdvanceFastVertex(&fp); \
    } \
} while (0)

    PUT_VERTEX(-1, -1, 0, 1, 0, 0, 1, 0, 0);
    PUT_VERTEX( 1, -1, 0, 0, 1, 0, 1, 1, 0);
    PUT_VERTEX( 0,  1, 0, 0, 0, 1, 1, 0.5f, 1);

#undef PUT_VERTEX
    mglEndFastPath(&fp);
}

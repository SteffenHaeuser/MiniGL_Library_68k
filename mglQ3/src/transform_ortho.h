/* Optional float-array fast path. Keep homogeneous coordinates and outcodes:
 * the existing clipper, perspective setup and screen mapping still run.
 * No epsilon classification: even a tiny rotation/perspective uses fallback.
 * Called only after CombinedMatrix is up to date and outside AP_FIXPOINT.
 */
static int TransformIndexOrtho(GLcontext context, int size, const UWORD *idx)
{
    const float *m = context->CombinedMatrix.v;
    int i, stride = context->ArrayPointer.vertexstride;
    float sx, sy, sz, tx, ty, tz;
    if (m[1] != 0.0f || m[2] != 0.0f || m[3] != 0.0f ||
        m[4] != 0.0f || m[6] != 0.0f || m[7] != 0.0f ||
        m[8] != 0.0f || m[9] != 0.0f || m[11] != 0.0f ||
        m[15] != 1.0f)
        return 0;
    sx = m[0]; sy = m[5]; sz = m[10];
    tx = m[12]; ty = m[13]; tz = m[14];
    for (i = size - 1; i >= 0; --i) {
        MGLVertex *v = &context->VertexBuffer[idx[i]];
        const float *p = (const float *)(context->ArrayPointer.verts + idx[i] * stride);
        ULONG code = 0;
        float x = sx * p[0] + tx;
        float y = sy * p[1] + ty;
        float z = sz * p[2] + tz;
        if (x < -1.0f) code |= MGL_CLIP_LEFT;
        else if (x > 1.0f) code |= MGL_CLIP_RIGHT;
        if (y < -1.0f) code |= MGL_CLIP_BOTTOM;
        else if (y > 1.0f) code |= MGL_CLIP_TOP;
        if (z < -1.0f) code |= MGL_CLIP_BACK;
        else if (z > 1.0f) code |= MGL_CLIP_FRONT;
        v->bx = x; v->by = y; v->bz = z; v->bw = 1.0f;
        v->outcode = code;
    }
    return 1;
}

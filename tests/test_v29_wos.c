/* Run on Amiga with the newly built WarpOS library. Compiled against PRIVATE
 * headers solely to supply a synthetic WarpOS context to non-rendering calls.
 * Applications must use include/, never these private structures. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MINIGL_LIBRARY_BUILD 1
#include <proto/minigl.h>

#define EXPECT(test) do { if (!(test)) { \
    printf("FAIL line %d: %s\n", __LINE__, #test); failures++; \
} } while (0)

int main(void)
{
    const MGLDispatchTable *d;
    GLcontext c;
    unsigned long offset, pointer_word;
    int failures = 0;
    struct GLcontext_t before;
    MGLNormal normals[3];
    GLfloat normal[3] = {2, 3, 4};
    GLenum mode;
    GLfloat untouched[4] = {11, 22, 33, 44};
    GLfloat unsupported[4] = {0, 0, 0, 0};
    if (!MiniGLOpen()) { puts("FAIL: cannot open ABI-5 minigl_ppc.dll V29.1"); return 1; }
    d = MiniGLDispatch;
    if (d->backendFlags != MINIGL_BACKEND_FLAG_PPC ||
        d->abiVersion != 5 || d->structSize != 840) {
        puts("FAIL: this test requires exactly the V29.1 WarpOS table");
        MiniGLClose(); return 1;
    }
    for (offset = 20; offset < 840; offset += 4) {
        memcpy(&pointer_word, (const unsigned char *)d + offset, 4);
        EXPECT(pointer_word != 0);
    }
    EXPECT(d->currentContext != 0);
    c = (GLcontext)calloc(1, sizeof(*c));
    if (!c) { MiniGLClose(); return 1; }
    c->CurrentPrimitive = GL_BASE;
    EXPECT(d->MGLResizeContext(c, 640, 480) == GL_FALSE);
    EXPECT(d->GLGenLists(c, 1) == 0);
    EXPECT(d->GLIsList(c, 1) == GL_FALSE);
    d->GLGetLightfv(c, GL_LIGHT0, GL_DIFFUSE, untouched);
    EXPECT(d->GLGetError(c) == GL_INVALID_OPERATION);
    EXPECT(untouched[0] == 11 && untouched[1] == 22 && untouched[2] == 33 && untouched[3] == 44);
    d->GLNormalPointer(c, GL_FLOAT, 0, (const GLvoid *)0);
    EXPECT(d->GLGetError(c) == GL_INVALID_OPERATION);
    d->GLColorMaterial(c, GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    EXPECT(d->GLGetError(c) == GL_INVALID_OPERATION);
    c->CurrentError = GL_INVALID_VALUE;
    d->GLLightf(c, GL_LIGHT0, GL_SPOT_EXPONENT, 1.0f);
    EXPECT(d->GLGetError(c) == GL_INVALID_VALUE); /* preserve first error */
    d->GLTexEnvfv(c, GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, unsupported);
    EXPECT(d->GLGetError(c) == GL_INVALID_ENUM);
    EXPECT(d->GLUBuild2DMipmaps(c, GL_TEXTURE_2D, GL_RGB, 1, 1,
        GL_RGB, GL_UNSIGNED_BYTE, (const GLvoid *)0) == GLU_INVALID_OPERATION);
    EXPECT(d->GLUNewQuadric() == 0);
    EXPECT(d->GLUErrorString(GLU_INVALID_OPERATION) != 0);
    EXPECT(d->GLUErrorString(0xffffffffUL) == 0);
    EXPECT(d->GLUTCreateWindow("stub test") == 0);
    EXPECT(d->GLUTEnterGameMode() == 0);
    EXPECT(d->GLUTGameModeGet(GLUT_GAME_MODE_POSSIBLE) == 0);
    c->ActiveTexture = 0;
    d->GLActiveTextureARB(c, GL_TEXTURE1_ARB);
    EXPECT(c->ActiveTexture == 1);
    d->GLActiveTextureARB(c, GL_TEXTURE2_ARB);
    EXPECT(c->ActiveTexture == 1);
    EXPECT(d->GLGetError(c) == GL_INVALID_ENUM);
    EXPECT(d->GLGetError(c) == GL_NO_ERROR);
    /* NULL vectors must neither dereference buffers nor alter context/error. */
    c->CurrentError = GL_INVALID_VALUE;
    memcpy(&before, c, sizeof(before));
    d->GLVertex2fv(c, 0);
    EXPECT(memcmp(&before, c, sizeof(before)) == 0);
    d->GLVertex3fv(c, 0);
    EXPECT(memcmp(&before, c, sizeof(before)) == 0);
    d->GLVertex4fv(c, 0);
    EXPECT(memcmp(&before, c, sizeof(before)) == 0);
    d->GLColor3fv(c, 0);
    EXPECT(memcmp(&before, c, sizeof(before)) == 0);
    d->GLColor4fv(c, 0);
    EXPECT(memcmp(&before, c, sizeof(before)) == 0);
    d->GLColor3ubv(c, 0);
    EXPECT(memcmp(&before, c, sizeof(before)) == 0);
    d->GLColor4ubv(c, 0);
    EXPECT(memcmp(&before, c, sizeof(before)) == 0);
    d->GLTexCoord2fv(c, 0);
    EXPECT(memcmp(&before, c, sizeof(before)) == 0);
    d->GLTexCoord4fv(c, 0);
    EXPECT(memcmp(&before, c, sizeof(before)) == 0);
    d->GLEdgeFlagv(c, 0);
    EXPECT(memcmp(&before, c, sizeof(before)) == 0);
    d->GLIndexiv(c, 0);
    EXPECT(memcmp(&before, c, sizeof(before)) == 0);
    d->GLNormal3fv(c, 0);
    EXPECT(memcmp(&before, c, sizeof(before)) == 0);
    d->GLMultiTexCoord2fvARB(c, GL_TEXTURE0_ARB, 0);
    EXPECT(memcmp(&before, c, sizeof(before)) == 0);
    EXPECT(d->GLGetError(c) == GL_INVALID_VALUE);
    memset(normals, 0, sizeof(normals));
    c->NormalBuffer = normals;
    d->GLNormal3fv(c, normal);
    EXPECT(c->NormalBufferPointer == 1);
    EXPECT(normals[1].x == 2 && normals[1].y == 3 && normals[1].z == 4);
    for (mode = 0x7013; mode <= 0x7014; ++mode) {
        c->VertexBufferPointer = 7;
        d->GLBegin(c, mode);
        EXPECT(d->GLGetError(c) == GL_INVALID_ENUM);
        EXPECT(c->CurrentPrimitive == GL_BASE && c->VertexBufferPointer == 7);
        d->GLDrawArrays(c, mode, 0, 3);
        EXPECT(d->GLGetError(c) == GL_INVALID_ENUM);
        d->GLDrawElements(c, mode, 3, GL_UNSIGNED_INT, 0);
        EXPECT(d->GLGetError(c) == GL_INVALID_ENUM);
    }
    free(c);
    MiniGLClose();
    if (!failures) puts("PASS: V29.1 WarpOS table, stubs, error semantics and texture-unit bounds.");
    return failures ? 1 : 0;
}

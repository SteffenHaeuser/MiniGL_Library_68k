#include <stdio.h>
#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>

int main(void)
{
    void *ctx;
    const GLubyte *renderer;

    puts("Opening minigl.library...");
    if (!MiniGLOpen()) {
        puts("Could not open minigl.library v4");
        return 20;
    }

    mglChooseVertexBufferSize(1000);
    ctx = mglCreateContext(0, 0, 640, 480);
    if (!ctx) {
        puts("Could not create MiniGL context");
        MiniGLClose();
        return 20;
    }

    renderer = glGetString(GL_RENDERER);
    printf("Renderer: %s\n", renderer ? (const char *)renderer : "<NULL>");

    glViewport(0, 0, 640, 480);
    glClearColor(0.1f, 0.2f, 0.4f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    mglSwitchDisplay();

    mglDeleteContext();
    MiniGLClose();
    puts("Done.");
    return 0;
}

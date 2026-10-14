#include <proto/minigl.h>
#include <mgl/gl.h>

int main(void)
{
    if (!MiniGLOpen())
        return 20;

    mglChoosePixelDepth(16);
    if (!mglCreateContext(0, 0, 640, 480)) {
        MiniGLClose();
        return 20;
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    mglSwitchDisplay();
    mglDeleteContext();
    MiniGLClose();
    return 0;
}

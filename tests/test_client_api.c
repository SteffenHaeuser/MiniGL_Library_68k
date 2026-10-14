/* Compile-only, using public headers in both C and C++. Do not run this
 * function: it checks declarations/macros, not renderer availability. */
#include <proto/minigl.h>
#include <mgl/mglmacros.h>
static void display_callback(void) {}
static void key_callback(unsigned char key, int x, int y) { (void)key; (void)x; (void)y; }
static void resize_callback(int w, int h) { (void)w; (void)h; }
void test_v29_public_api(struct Window *window, struct BitMap *bitmap)
{
    GLfloat v[4] = {1, 1, 1, 1};
    GLboolean resized;
    GLUquadricObj *q;
    ULONG swapped = LE32(0x12345678UL);
    UWORD swapped16 = LE16(0x1234U);
    (void)swapped; (void)swapped16; NOP();
    void *handle = MiniGLBase; /* Must have C linkage in C++ too. */
    (void)handle;
    (void)MiniGLOpen();
    resized = mglResizeContext(640, 480);
    (void)resized;
    (void)mglCreateContextFromWindow(window);
    (void)mglCreateContextFromBitMap(bitmap);
    glActiveTexture(GL_TEXTURE0_ARB);
    glClientActiveTexture(GL_TEXTURE0_ARB);
    glLightf(GL_LIGHT0, GL_SPOT_EXPONENT, 1.0f);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, v);
    glMaterialf(GL_FRONT, GL_SHININESS, 1.0f);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, v);
    glLightModelf(GL_LIGHT_MODEL_TWO_SIDE, 0.0f);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, v);
    glGetLightfv(GL_LIGHT0, GL_DIFFUSE, v);
    glGetMaterialfv(GL_FRONT, GL_DIFFUSE, v);
    glNormal3fv(v); glNormal3fv((GLfloat *)0);
    glNormalPointer(GL_FLOAT, 0, v);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, v);
    (void)glGenLists(1); glNewList(1, GL_COMPILE); glEndList();
    glCallList(1); (void)glIsList(1); glDeleteLists(1, 1);
    (void)gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGB, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, v);
    (void)gluErrorString(GL_INVALID_ENUM);
    q = gluNewQuadric();
    gluQuadricNormals(q, GLU_SMOOTH); gluQuadricTexture(q, GL_TRUE);
    gluQuadricDrawStyle(q, GLU_FILL); gluQuadricOrientation(q, GLU_OUTSIDE);
    gluQuadricCallback(q, GLU_ERROR, (MGLUfuncptr)0);
    gluCylinder(q, 1, 1, 1, 8, 8); gluSphere(q, 1, 8, 8);
    gluDisk(q, 0, 1, 8, 8); gluDeleteQuadric(q);
    glutInit((int *)0, (char **)0); glutInitDisplayMode(GLUT_RGB);
    glutInitWindowSize(640, 480); glutInitWindowPosition(0, 0);
    (void)glutCreateWindow("compile check"); glutMainLoop();
    glutDisplayFunc(display_callback); glutIdleFunc(display_callback);
    glutKeyboardFunc(key_callback); glutReshapeFunc(resize_callback);
    glutSwapBuffers(); glutPostRedisplay(); (void)glutGet(GLUT_WINDOW_WIDTH);
    glutGameModeString("640x480"); (void)glutEnterGameMode();
    glutLeaveGameMode(); (void)glutGameModeGet(GLUT_GAME_MODE_POSSIBLE);
    glutSolidCube(1); glutSolidSphere(1, 8, 8); glutSolidCone(1, 1, 8, 8);
    glutSolidTorus(1, 2, 8, 8); glutSolidDodecahedron();
    MiniGLClose();
}

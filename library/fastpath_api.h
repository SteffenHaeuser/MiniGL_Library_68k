#ifndef MINIGL_FASTPATH_API_INTERNAL_H
#define MINIGL_FASTPATH_API_INTERNAL_H
#include <mgl/gl.h>
#include <libraries/minigl_fastpath.h>
ULONG Lib_mglGetFastPathCapabilities(void);
GLboolean Lib_mglBeginFastPath(GLenum primitive, ULONG attributes, MGLFastPath *fp);
void Lib_mglEndFastPath(MGLFastPath *fp);
#endif

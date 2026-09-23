#include <exec/types.h>
#include <mgl/gl.h>
#include <libraries/minigl_fastpath.h>
#include "fastpath_api.h"
#include "debug_output.h"

static void fp_stub(const char *name)
{
    dbg_puts("[minigl.direct stub] ");
    dbg_puts(name);
    dbg_newline();
}

ULONG Lib_mglGetFastPathCapabilities(void)
{
    fp_stub("mglGetFastPathCapabilities");
    return 0;
}

GLboolean Lib_mglBeginFastPath(GLenum primitive, ULONG attributes, MGLFastPath *fp)
{
    (void)primitive; (void)attributes; (void)fp;
    fp_stub("mglBeginFastPath");
    return GL_FALSE;
}

void Lib_mglEndFastPath(MGLFastPath *fp)
{
    (void)fp;
    fp_stub("mglEndFastPath");
}

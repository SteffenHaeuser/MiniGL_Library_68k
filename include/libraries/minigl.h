#ifndef LIBRARIES_MINIGL_H
#define LIBRARIES_MINIGL_H

#include <exec/types.h>

/* V29 / ABI 5 is a breaking token change: rebuild every client and backend. */
#define MINIGL_VERSION 29
#define MINIGL_REVISION 1
#define MINIGL_MIN_VERSION 29

#ifndef MINIGL_PPC_DLLNAME
#define MINIGL_PPC_DLLNAME "minigl_ppc.dll"
#endif
#ifndef MINIGL_PPC_PORTNAME
#define MINIGL_PPC_PORTNAME "MiniGLPPC.DLL"
#endif

/* On WarpOS this is the Hyperion DLL instance handle, not struct Library *. */
#ifdef __cplusplus
extern "C" {
#endif
extern void *MiniGLBase;
#ifdef __cplusplus
}
#endif

#endif

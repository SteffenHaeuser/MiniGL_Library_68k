#include <exec/types.h>
#include <libraries/minigl.h>
#include <libraries/minigl_dispatch.h>
#include "dll.h"

typedef const MGLDispatchTable *(*MGLGetDispatchTableFn)(void);

BOOL MiniGLOpen(void)
{
    void *hinst;
    MGLGetDispatchTableFn getDispatch;
    const MGLDispatchTable *dispatch;

    if (MiniGLBase && MiniGLDispatch)
        return TRUE;

    hinst = dllLoadLibrary((char *)MINIGL_PPC_DLLNAME,
                           (char *)MINIGL_PPC_PORTNAME);
    if (!hinst)
        return FALSE;

    getDispatch = (MGLGetDispatchTableFn)
        dllGetProcAddress(hinst, (char *)"MGLGetDispatchTable");
    if (!getDispatch) {
        dllFreeLibrary(hinst);
        return FALSE;
    }

    /* Exact ABI equality rejects old token encodings in either direction
     * for clients built with our WOS headers. Size alone is insufficient. */
    dispatch = getDispatch();
    if (!dispatch ||
        dispatch->abiVersion != MINIGL_DISPATCH_ABI_VERSION ||
        dispatch->structSize < (ULONG)sizeof(MGLDispatchTable) ||
        !dispatch->currentContext) {
        dllFreeLibrary(hinst);
        return FALSE;
    }

    MiniGLBase = hinst;
    MiniGLDispatch = dispatch;
    return TRUE;
}

void MiniGLClose(void)
{
    void *hinst = MiniGLBase;

    /* Invalidate pointers before unloading the PPC DLL. */
    MiniGLDispatch = 0;
    MiniGLBase = 0;

    if (hinst)
        dllFreeLibrary(hinst);
}

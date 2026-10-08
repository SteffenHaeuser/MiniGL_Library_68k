#include <proto/exec.h>
#include <libraries/minigl.h>
#include <libraries/minigl_dispatch.h>

/* Implemented by the tiny compiler-specific import stub. */
extern const MGLDispatchTable *MiniGLGetDispatchTableLVO(void);

BOOL MiniGLOpen(void)
{
    const MGLDispatchTable *dispatch;

    if (MiniGLBase && MiniGLDispatch)
        return TRUE;

    if (!MiniGLBase) {
        MiniGLBase = OpenLibrary(MINIGLNAME, MINIGL_VERSION);
        if (!MiniGLBase)
            return FALSE;
    }

    dispatch = MiniGLGetDispatchTableLVO();
	if (!dispatch)
{
    CloseLibrary(MiniGLBase);
    MiniGLBase = NULL;
    return 0;
}
    /*
     * abiVersion is compared for EQUALITY, not ">= mine". It identifies the
     * token ABI, and 3 -> 4 changed the value of 230 GL names rather than
     * adding anything, so a newer library is exactly as wrong for this client
     * as an older one. structSize keeps its "at least mine" test: that one is
     * about entry points being present, where newer really is a superset.
     *
     * This protects a client against the wrong library. It CANNOT protect the
     * other direction -- an old binary opening a new library -- because that
     * client's own compiled-in check is `< 3`, which a 4 satisfies. Only a new
     * entry point, or publishing an identity its check rejects, can refuse it.
     */
    if (dispatch->abiVersion != MINIGL_DISPATCH_ABI_VERSION ||
        dispatch->structSize < sizeof(MGLDispatchTable))
    {
        CloseLibrary(MiniGLBase);
        MiniGLBase = 0;
        MiniGLDispatch = 0;
        return FALSE;
    }

    MiniGLDispatch = dispatch;
    return TRUE;
}

void MiniGLClose(void)
{
    MiniGLDispatch = 0;

    if (MiniGLBase) {
        CloseLibrary(MiniGLBase);
        MiniGLBase = 0;
    }
}

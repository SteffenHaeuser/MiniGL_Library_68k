/*
 * (C) 2025-2026 Dennis van der Boon
 */

/*
 * From PoC/v3d_debug.c. Provides kprintf() for the D()/E() macros in
 * v3d_debug.h. Warp3D-free, generic AmigaOS debug-print helper (RawDoFmt
 * to the debug serial output).
 */

#ifdef DEBUG

#include <stdarg.h>

#include <exec/execbase.h>
#include <proto/exec.h>

#include "lvocall_compat.h"

MGLV3D_LP1(APTR, __DRawPutChar, 516, UBYTE, d0)

#define DRawPutChar(MyChar) __DRawPutChar(SysBase, (MyChar))

void DPutChProc(MGLV3D_REG(d0, UBYTE mychar), MGLV3D_REG(a3, APTR PutChData))
{
    struct ExecBase* SysBase = (struct ExecBase*)PutChData;
    DRawPutChar(mychar);
    return;
}

void kprintf(STRPTR format, ...)
{
    if (format)
    {
        struct ExecBase* SysBase = *(struct ExecBase **)4L;
        va_list args;
        va_start(args, format);
        RawDoFmt(format, (APTR)args, (void (*)())&DPutChProc, (APTR)SysBase);
        va_end(args);
    }
    return;
}
#endif

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include <intuition/intuitionbase.h>
#include <graphics/gfxbase.h>

#include "minigl_library.h"

#define MINIGL_LIBRARY_BUILD 1
#include <libraries/minigl_dispatch.h>
#include "debug_output.h"
#include "context_access.h"

/* Compiler-neutral spelling for Amiga library lifecycle register arguments. */
#if defined(__VBCC__) || defined(MINIGL_VBCC_LIBRARY)
#define MINIGL_REGARG(reg, type, name) __reg(reg) type name
#define MINIGL_COMPILER_ID "vbcc"
#elif defined(__GNUC__)
#define MINIGL_REGARG(reg, type, name) register type name __asm(reg)
#define MINIGL_COMPILER_ID "gcc"
#else
#error Unsupported compiler: define lifecycle register argument syntax
#endif

#if !defined(MINIGL_BACKEND_STUB) && !defined(MINIGL_VBCC_LIBRARY)
#include "math_shim.h"
#endif

#define LIBNAME "minigl.library"
#define VERSION 27
#define REVISION 0
#define MINIGL_STRINGIFY_INNER(value) #value
#define MINIGL_STRINGIFY(value) MINIGL_STRINGIFY_INNER(value)
#ifdef MINIGL_CLASSIC_ORTHO
#define MINIGL_VARIANT_ID ", local ortho optimization"
#else
#define MINIGL_VARIANT_ID ""
#endif

static const char LibName[] = LIBNAME;
#define MINIGL_CONTEXT_ID "single-context direct"

#if defined(MINIGL_BACKEND_STUB)
#define MINIGL_BACKEND_ID "fprintf stub, " MINIGL_CONTEXT_ID
#elif defined(PISTORM3D)
#define MINIGL_BACKEND_ID "PiStorm3D, " MINIGL_CONTEXT_ID
#else
#define MINIGL_BACKEND_ID "classic MiniGL, " MINIGL_CONTEXT_ID
#endif

static const char LibId[] =
    "$VER: minigl.library " MINIGL_STRINGIFY(VERSION) "." MINIGL_STRINGIFY(REVISION)
    " (21.9.2026) " MINIGL_BACKEND_ID ", direct-backend dispatch ABI " MINIGL_COMPILER_ID MINIGL_VARIANT_ID "\r\n";

/* Library bases used by the AmigaOS NDK proto headers. */
struct ExecBase *SysBase = 0;
struct DosLibrary *DOSBase = 0;
#ifdef MINIGL_BACKEND_STUB
struct IntuitionBase *IntuitionBase = 0;
struct GfxBase *GfxBase = 0;
struct Library *UtilityBase = 0;
#else
extern struct IntuitionBase *IntuitionBase;
extern struct GfxBase *GfxBase;
extern struct Library *UtilityBase;
#endif

#ifndef MINIGL_BACKEND_STUB
static BOOL MiniGLInitialized = FALSE;
extern void mglChooseWindowMode(GLboolean flag);
#endif

extern const APTR LibVectors[];
extern const MGLDispatchTable MiniGLDispatchTable;

static const MGLDispatchTable *LibGetDispatchTable(void)
{
    return &MiniGLDispatchTable;
}
static BPTR LibExpunge(MINIGL_REGARG("a6", struct MiniGLLibrary *, base));

static struct MiniGLLibrary *LibInit(
    MINIGL_REGARG("d0", struct MiniGLLibrary *, base),
    MINIGL_REGARG("a0", BPTR, seglist),
    MINIGL_REGARG("a6", struct ExecBase *, sysbase))
{
    SysBase = sysbase;

    /*
     * Initialize the standard Library fields directly.
     * This avoids vbcc problems with offsetof()/OFFSET() in
     * the resident autoinit data table.
     */
    base->lib.lib_Node.ln_Type = NT_LIBRARY;
    base->lib.lib_Node.ln_Name = (char *)LibName;
    base->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    base->lib.lib_Version = VERSION;
    base->lib.lib_Revision = REVISION;
    base->lib.lib_IdString = (char *)LibId;

    base->segList = seglist;
    InitSemaphore(&base->lock);

    DOSBase = (struct DosLibrary *)OpenLibrary("dos.library", 37);
    if (!DOSBase)
        return 0;

#if !defined(MINIGL_BACKEND_STUB) && !defined(MINIGL_VBCC_LIBRARY)
    if (!MiniGL_OpenMathLibrary()) {
        struct Library *dos = (struct Library *)DOSBase;
        DOSBase = 0;
        CloseLibrary(dos);
        return 0;
    }
#endif

    dbg_puts("[minigl.library] context mode: global-direct");
    dbg_newline();

    return base;
}

static struct MiniGLLibrary *LibOpen(
    MINIGL_REGARG("a6", struct MiniGLLibrary *, base))
{
#ifndef MINIGL_BACKEND_STUB
    if (!MiniGLInitialized) {
        if (MGLInit() == GL_FALSE)
            return 0;

        MiniGLInitialized = TRUE;
    }
#else
    dbg_puts("[minigl.library stub] LibOpen");
    dbg_newline();
#endif

#ifndef MINIGL_BACKEND_STUB
    /*
     * The static MiniGL build gets fresh file-static defaults on every
     * process start. A resident shared library does not: backend globals
     * survive while the library remains resident after the last CloseLibrary().
     * Restore the documented default for a new client session so a previous
     * caller of mglChooseWindowMode(GL_TRUE) cannot leave the next application
     * stuck in windowed mode. Do this only on the 0 -> 1 open transition so an
     * additional concurrent opener cannot change an active client's setting.
     */
    if (base->lib.lib_OpenCnt == 0)
        mglChooseWindowMode(GL_FALSE);
#endif

    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= (UBYTE)~LIBF_DELEXP;

    return base;
}

static BPTR LibClose(
    MINIGL_REGARG("a6", struct MiniGLLibrary *, base))
{
    BPTR seg = 0;

    if (base->lib.lib_OpenCnt)
        base->lib.lib_OpenCnt--;

    /*
     * Single-current-context library:
     * do not retain a stale backend context.
     */
    mini_CurrentContext = (GLcontext)0;

    if (!base->lib.lib_OpenCnt &&
        (base->lib.lib_Flags & LIBF_DELEXP))
    {
        seg = LibExpunge(base);
    }

    return seg;
}

static BPTR LibExpunge(
    MINIGL_REGARG("a6", struct MiniGLLibrary *, base))
{
    BPTR seg;
    ULONG size;
    UBYTE *mem;

    if (base->lib.lib_OpenCnt) {
        base->lib.lib_Flags |= LIBF_DELEXP;
        return 0;
    }

    seg = base->segList;

    Remove((struct Node *)base);

    mini_CurrentContext = (GLcontext)0;

#ifndef MINIGL_BACKEND_STUB
    if (MiniGLInitialized) {
        MGLTerm();
        MiniGLInitialized = FALSE;
    }

#if !defined(MINIGL_VBCC_LIBRARY)
    MiniGL_CloseMathLibrary();
#endif
#endif

    if (DOSBase) {
        struct Library *dos = (struct Library *)DOSBase;
        DOSBase = 0;
        CloseLibrary(dos);
    }

    size = base->lib.lib_NegSize + base->lib.lib_PosSize;
    mem = (UBYTE *)base - base->lib.lib_NegSize;

    FreeMem(mem, size);

    return seg;
}

static ULONG LibReserved(void)
{
    return 0;
}

const APTR LibVectors[] = {
    (APTR)LibOpen,
    (APTR)LibClose,
    (APTR)LibExpunge,
    (APTR)LibReserved,

    /* -30: the only public API vector. All GL/MGL calls use this table. */
    (APTR)LibGetDispatchTable,

    (APTR)-1
};

/*
 * No autoinit data initializer table.
 * LibInit() initializes the struct Library fields itself.
 */
static const struct {
    ULONG size;
    const APTR *vectors;
    const ULONG *data;
    APTR init;
} InitTable = {
    sizeof(struct MiniGLLibrary),
    LibVectors,
    0,
    (APTR)LibInit
};

const struct Resident ROMTag = {
    RTC_MATCHWORD,
    (struct Resident *)&ROMTag,
    (APTR)(&ROMTag + 1),
    RTF_AUTOINIT,
    VERSION,
    NT_LIBRARY,
    0,
    (char *)LibName,
    (char *)LibId,
    (APTR)&InitTable
};

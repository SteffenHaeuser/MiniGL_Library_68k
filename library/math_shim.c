/* Math compatibility layer for the PiStorm3D static archive.
 *
 * libm.a's IEEE-double transcendental functions use the classic AmigaOS
 * mathieeedoubtrans.library through MathIeeeDoubTransBase.  A normal program
 * startup would normally arrange this base, but minigl.library has no program
 * startup and therefore owns/opens the base itself.
 *
 * The supplied PiStorm3D archive also references cexp().  The installed libm
 * does not provide it, so it is implemented locally using exp/sin/cos from
 * libm.  LIB_CFLAGS contains -fno-builtin, preventing GCC from recognising
 * this implementation as another cexp call.
 */
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>

#include <complex.h>
#include <math.h>

#include "math_shim.h"

/* Name and type expected by the GCC/libnix libm objects. */
//struct Library *MathIeeeDoubTransBase = 0;

BOOL MiniGL_OpenMathLibrary(void)
{
	return TRUE;
	#if 0
    if (MathIeeeDoubTransBase)
        return TRUE;

    MathIeeeDoubTransBase = OpenLibrary("mathieeedoubtrans.library", 0);
    return MathIeeeDoubTransBase != 0;
	#endif
}

void MiniGL_CloseMathLibrary(void)
{
	#if 0
    if (MathIeeeDoubTransBase) {
        struct Library *base = MathIeeeDoubTransBase;
        MathIeeeDoubTransBase = 0;
        CloseLibrary(base);
    }
	#endif
}

/* libm881.a (pure 68881 FPU instructions, no AmigaOS library of any kind --
 * a static archive, not mathieeedoubtrans.library) is the correct math
 * library for this project (see feedback_minigl_v3d_gcc_build_recipe:
 * "libm881.a default"), and is what the PiStorm3D archive's own matrix.o
 * etc. expect at final link, same as glquake/Quake2's own build scripts
 * already link -lm881 for exactly this reason. It was built as application
 * code, though, so a couple of its object files (math_sqrt.o, stdio_
 * sprintf.o -- pulled in for sqrt's domain-error path) reference a small
 * slice of clib2's runtime surface. Same reasoning as runtime_shim.c's own
 * header comment: pulling a real C runtime into a resident library is
 * incorrect, so these are minimal, hand-written stand-ins for exactly the
 * symbols referenced, not a real clib2 runtime.
 */
BOOL __check_abort_enabled = FALSE;

void __check_abort(void)
{
}

void __set_errno(int value)
{
    (void)value;
}

/* C99 complex exponential.  This symbol is needed by matrix.o in the supplied
 * PiStorm3D archive.  Avoid complex arithmetic expressions involving I here;
 * assigning the real/imaginary components explicitly keeps the generated ABI
 * straightforward on m68k GCC.
 */
double complex cexp(double complex value)
{
    double complex result;
    const double real_part = __real__ value;
    const double imag_part = __imag__ value;
    const double scale = exp(real_part);

    __real__ result = scale * cos(imag_part);
    __imag__ result = scale * sin(imag_part);
    return result;
}

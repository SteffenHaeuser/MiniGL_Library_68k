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
struct Library *MathIeeeDoubTransBase = 0;

BOOL MiniGL_OpenMathLibrary(void)
{
    if (MathIeeeDoubTransBase)
        return TRUE;

    MathIeeeDoubTransBase = OpenLibrary("mathieeedoubtrans.library", 0);
    return MathIeeeDoubTransBase != 0;
}

void MiniGL_CloseMathLibrary(void)
{
    if (MathIeeeDoubTransBase) {
        struct Library *base = MathIeeeDoubTransBase;
        MathIeeeDoubTransBase = 0;
        CloseLibrary(base);
    }
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

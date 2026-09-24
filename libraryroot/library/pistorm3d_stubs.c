/*
 * Standalone symbols required by the PiStorm3D MiniGL backend.
 *
 * GLQuake/debug builds normally supply some of these.
 * minigl.library deliberately has no stdio dependency.
 */

#include <stdarg.h>

#include <stddef.h>

void mglv3d_log(const char *msg)
{
}

int g_mglv3d_glquake_build_version = 0;


/*
 * PiStorm3D contains debug printf/vprintf paths.
 *
 * Do not pull vbcc's stdio implementation into a resident
 * AmigaOS shared library.  The output is intentionally discarded.
 *
 * vbcc prefixes C symbols with "_", therefore a C function named
 * _vprintf exports the linker symbol "__vprintf".
 */
int _vprintf(const char *format, va_list args)
{
    return 0;
}
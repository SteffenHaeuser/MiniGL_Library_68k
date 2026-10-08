/*
 * $Id: sysinc.h,v 1.5 2001/12/25 00:55:26 tfrieden Exp $
 *
 * $Date: 2001/12/25 00:55:26 $
 * $Revision: 1.5 $
 *
 * (C) 1999 by Hyperion
 * All rights reserved
 *
 * This file is part of the MiniGL library project
 * See the file Licence.txt for more details
 *
 */

/*
 * Modified by Dennis van der Boon for use on the PiStorm with Pi4.
 * Copyright 2025-2026.
 */

/*
 * MiniGLV3D fork of MiniGL/src/sysinc.h.
 */

#ifndef __MINIGL_COMPILER_H
#define __MINIGL_COMPILER_H

/* Both pairs guarded for the same reason as mgl/context.h's: VBCC needs Amiga
 * alignment told for these OS headers, GCC does it anyway and only warns
 * -Wunknown-pragmas, which every -Wall compile of gl/src then repeats four
 * times. No layout changes for either compiler. */
#ifdef __VBCC__
#pragma amiga-align
#endif

#include <exec/types.h>
#include <exec/exec.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/scale.h>
#include <utility/tagitem.h>
#include <dos/dos.h>
#include <dos/exall.h>
#include <devices/timer.h>
#include <cybergraphx/cybergraphics.h>

#ifdef __VBCC__
#pragma default-align
#endif

#define UNUSED

#ifdef __VBCC__
#pragma amiga-align
#endif
#include <proto/intuition.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <proto/cybergraphics.h>
#ifdef __VBCC__
#pragma default-align
#endif

/* Resolved via -I pointing at gl/include/. */
#include <mgl/gl.h>

/* GCC's -std=c99 gives plain `inline` (no static, no extern) strict C99
 * semantics: the compiler is NOT required to emit a real, externally-
 * callable out-of-line definition at all -- only guaranteed valid if
 * every call site actually gets inlined. At -O0 (no inlining at all)
 * this is an "undefined reference" link failure. `static inline` always
 * provides a real per-TU callable definition (so -O0 links), while
 * remaining eligible for inlining at -O2/-O3. Every INLINE-marked
 * function in this codebase (draw.c's v_Transform/PrepTexCoords,
 * matrix.c's m_Mult/m_MatCopy/etc.) is only ever called from within its
 * own defining .c file, so static linkage loses nothing. Other compilers
 * keep plain `inline`, which relies on the compiler emitting an
 * out-of-line definition regardless of optimization level. */
#ifdef __GNUC__
#define INLINE static inline
#else
#define INLINE inline
#endif

#include <mgl/config.h>

#endif

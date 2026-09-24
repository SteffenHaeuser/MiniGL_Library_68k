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

#ifndef __MINIGL_COMPILER_H
#define __MINIGL_COMPILER_H

#ifdef __PPC__
extern struct Library *Warp3DPPCBase;
#else
extern struct Library *Warp3DBase;
#endif

#pragma pack(push,2)

#include <exec/types.h>
#include <exec/exec.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/scale.h>
#include <utility/tagitem.h>
#include <dos/dos.h>
#include <dos/exall.h>
#include <devices/timer.h>
#include <Warp3D/Warp3D.h>
#include <cybergraphx/cybergraphics.h>

#pragma pack(pop)

/* Required by the 68k NDK inline/graphics.h macros.  The actual shared
 * graphics.library base is defined in init.c and opened by MGLInit(). */
extern struct GfxBase *GfxBase;

#if defined(__GNUC__)
#define UNUSED  __attribute__ ((unused))
#else
#define UNUSED
#endif

#if defined(__GNUC__)
	/* These legacy functions are used across translation units.  C99 inline
	 * would suppress their external definitions, so leave inlining to -O2. */
	#ifndef INLINE
	#define INLINE
	#endif

	#include "../include/mgl/gl.h"

	#ifdef __PPC__

	#pragma pack(push,2)
	#include <powerpc/memoryPPC.h>
	#pragma pack(pop)

	#ifndef __STORMGCC__
		#pragma pack(push,2)
		#include <Warp3D/Warp3D_protos.h>
		#pragma pack(pop)
	#else
	    	#include <Warp3D/Warp3D.h>
	    	#include <clib/Warp3D_protos.h>
	#endif

	#ifndef __STORMGCC__
		#pragma pack(push,2)
		#include <powerpc/powerpc_protos.h>
		#pragma pack(pop)
	#else
		#include <clib/powerpc_protos.h>
	#endif

	#pragma pack(push,2)
	#include <proto/intuition.h>
	#include <proto/exec.h>
	#include <proto/graphics.h>
	#include <proto/dos.h>
	#include <proto/cybergraphics.h>
	#pragma pack(pop)

	#else // 68k

	/* Use the matching Classic SDK inline header bundled with these sources. */
	#include <inline/Warp3D.h>
	#include <inline/intuition.h>
	#include <inline/exec.h>
	#include <inline/graphics.h>
	#include <inline/dos.h>
	#include <proto/timer.h>
	#include <inline/timer.h>
	#include <proto/cybergraphics.h>
	#include <inline/cybergraphics.h>
	//#include <proto/cybergraphics.h>

	#endif

#elif defined(__STORM__)

	#include "/include/mgl/gl.h"
	#include <Warp3D/Warp3D.h>
	#include <clib/Warp3D_protos.h>

    	#ifdef __PPC__

    	#include <clib/powerpc_protos.h>
   	#include <clib/cybergraphics_protos.h>

   	#else

	#include <pragma/Warp3D_lib.h>

	#endif

	#define INLINE __inline
	#define inline __inline

#elif defined(__VBCC__)

	#pragma pack(push,2)

    	#include <proto/Warp3D.h>
    	#include <proto/intuition.h>
    	#include <proto/exec.h>
    	#include <proto/graphics.h>
    	#include <proto/dos.h>
    	#include <proto/cybergraphics.h>

    	#ifdef __PPC__
	//#include <clib/powerpc_protos.h>
	#include <powerpc/powerpc.h>
	#include <proto/powerpc.h>
    	#endif

	#pragma pack(pop)

    	#include "/include/mgl/gl.h"

	#ifndef inline
	#define inline
	#endif

	#define INLINE inline
	#define __inline inline

#endif

	#include <mgl/config.h>

#endif

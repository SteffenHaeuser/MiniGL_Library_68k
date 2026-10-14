/*
 * $Id: init.c,v 1.4 2001/12/25 00:55:26 tfrieden Exp $
 *
 * $Date: 2001/12/25 00:55:26 $
 * $Revision: 1.4 $
 *
 * (C) 1999 by Hyperion
 * All rights reserved
 *
 * This file is part of the MiniGL library project
 * See the file Licence.txt for more details
 *
 */

#include "sysinc.h"
#include <stdio.h>

//static char rcsid[] UNUSED = "$Id: init.c,v 1.4 2001/12/25 00:55:26 tfrieden Exp $";

//surgeon: added 11-04-02 - initializes glDrawArrays to glDrawElements wrapper for clipping with compiled arrays

extern void Init_ArrayToElements_Warpper(void);

struct Library *UtilityBase;
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;

#ifndef __PPC__
extern struct DosLibrary *DOSBase;
extern struct ExecBase *SysBase;
#endif

void MGL_SINCOS_Init(void);

#ifdef __PPC__
struct Library *Warp3DPPCBase = NULL;
#else
struct Library *Warp3DBase = NULL;
#endif

struct Library *CyberGfxBase;// = NULL;


/* Set when MGLInit() opened a library itself. Anything it found already open
   belongs to the host application -- SDL shares these very globals with us --
   so MGLTerm() must not close it out from under the owner. */
static GLboolean mgl_owns_IntuitionBase = GL_FALSE;
static GLboolean mgl_owns_GfxBase       = GL_FALSE;
static GLboolean mgl_owns_UtilityBase   = GL_FALSE;
static GLboolean mgl_owns_CyberGfxBase  = GL_FALSE;
#ifdef __PPC__
static GLboolean mgl_owns_Warp3DPPCBase = GL_FALSE;
#else
static GLboolean mgl_owns_Warp3DBase    = GL_FALSE;
#endif

GLboolean MGLInit(void)
{
	/* Only open what is not open yet: overwriting a base that the caller
	   already opened loses its OpenLibrary() and leaves it with a pointer we
	   would later close, which is a use-after-free waiting to happen. */
	if (!IntuitionBase)
	{
		IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 0L);
		mgl_owns_IntuitionBase = (IntuitionBase != NULL);
	}

	if (!GfxBase)
	{
		GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 0L);
		mgl_owns_GfxBase = (GfxBase != NULL);
	}

	if (!UtilityBase)
	{
		UtilityBase = OpenLibrary("utility.library", 0L);
		mgl_owns_UtilityBase = (UtilityBase != NULL);
	}

#ifdef __PPC__
	if (!Warp3DPPCBase)
	{
		Warp3DPPCBase = OpenLibrary("Warp3DPPC.library", 2L);
		mgl_owns_Warp3DPPCBase = (Warp3DPPCBase != NULL);
	}
#else
	if (!Warp3DBase)
	{
		Warp3DBase = OpenLibrary("Warp3D.library", 2L);
		mgl_owns_Warp3DBase = (Warp3DBase != NULL);
	}
#endif

	if (!CyberGfxBase)
	{
		CyberGfxBase = OpenLibrary("cybergraphics.library", 0L);
		mgl_owns_CyberGfxBase = (CyberGfxBase != NULL);
	}

	if (!IntuitionBase || !GfxBase || !UtilityBase || !CyberGfxBase ||
#ifdef __PPC__
	    !Warp3DPPCBase)
#else
	    !Warp3DBase)
#endif
	{
	    printf("Library initialization failed:\n");

	    if (!IntuitionBase) printf("- intuition.library (How are you doing this ?)\n");
	    if (!GfxBase)       printf("- graphics.library (Strange!)\n");

#ifdef __PPC__
	    if (!Warp3DPPCBase) printf("- Warp3DPPC.library\n");
#else
	    if (!Warp3DBase)    printf("- Warp3D.library\n");
#endif
	    if (!CyberGfxBase)  printf("- cybergraphics.library\n");
	    
	    MGLTerm();
	    return GL_FALSE;
	}

#ifdef TRIGTABLES
	MGL_SINCOS_Init();
#endif

	Init_ArrayToElements_Warpper(); //11-04-02

	return GL_TRUE;

}

void MGLTerm(void)
{
	/* Close only what MGLInit() opened. These globals are shared with the host
	   application (SDL defines the very same symbols), so closing and NULLing a
	   base we did not open would pull it out from under the still-running
	   owner. */
	if (CyberGfxBase && mgl_owns_CyberGfxBase)
	{
		CloseLibrary(CyberGfxBase);
		CyberGfxBase = NULL;
		mgl_owns_CyberGfxBase = GL_FALSE;
	}

	#ifdef __PPC__

	if (Warp3DPPCBase && mgl_owns_Warp3DPPCBase)
	{
		CloseLibrary(Warp3DPPCBase);
		Warp3DPPCBase = NULL;
		mgl_owns_Warp3DPPCBase = GL_FALSE;
	}

	#else

	if (Warp3DBase && mgl_owns_Warp3DBase)
	{
		CloseLibrary(Warp3DBase);
		Warp3DBase = NULL;
		mgl_owns_Warp3DBase = GL_FALSE;
	}

	#endif

	if (IntuitionBase && mgl_owns_IntuitionBase)
	{
		CloseLibrary((struct Library *)IntuitionBase);
		IntuitionBase = NULL;
		mgl_owns_IntuitionBase = GL_FALSE;
	}

	if (GfxBase && mgl_owns_GfxBase)
	{
		CloseLibrary((struct Library *)GfxBase);
		GfxBase = NULL;
		mgl_owns_GfxBase = GL_FALSE;
	}

	if (UtilityBase && mgl_owns_UtilityBase)
	{
		CloseLibrary(UtilityBase);
		UtilityBase = NULL;
		mgl_owns_UtilityBase = GL_FALSE;
	}
}

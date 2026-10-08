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

/*
 * Modified by Dennis van der Boon for use on the PiStorm with Pi4.
 * Copyright 2025-2026.
 */

/*
 * MiniGLV3D fork of MiniGL/src/init.c.
 *
 * MGLInit opens no Warp3D library: V3D has no AmigaOS library to open.
 * v3d_init() (backend/hw/v3d_device.c) reaches the hardware through
 * devicetree.resource plus direct MMIO register access instead.
 * The original's __PPC__ branches are not carried over: this library
 * is built for 68k only.
 */

#include "sysinc.h"
#include <stdio.h>
#include <dos/dosextens.h>
#include "../../backend/hw/v3d_debug.h"



//surgeon: added 11-04-02 - initializes glDrawArrays to glDrawElements wrapper for clipping with compiled arrays

extern void Init_ArrayToElements_Warpper(void);


struct Library *UtilityBase;
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;

extern struct DosLibrary *DOSBase;
extern struct ExecBase *SysBase;

void MGL_SINCOS_Init(void);

struct Library *CyberGfxBase = NULL;

/* Which of the four bases above THIS driver opened, so MGLTerm closes only
 * those. A host that opened one for itself keeps it: see MGLInit. */
static GLboolean s_opened_intuition = GL_FALSE;
static GLboolean s_opened_gfx       = GL_FALSE;
static GLboolean s_opened_utility   = GL_FALSE;
static GLboolean s_opened_cybergfx  = GL_FALSE;


/* Stack-size check: AmigaOS gives each program its stack at LAUNCH time
 * (Shell's current `Stack` setting, or a Workbench icon's STACK=
 * tooltype) -- nothing in this library sets it. A too-small stack is a
 * suspected (not proven) cause of memory corruption that shows up as
 * crashes or hangs in whatever runs next, hence the warning below
 * MGLV3D_RECOMMENDED_MIN_STACK. Warning only, deliberately NOT an
 * automatic fix: raising the stack means relaunching the program via
 * CreateNewProcTags with a bigger stack, which needs to forward the
 * original command line, and MGLInit() has no access to argc/argv. */
#define MGLV3D_RECOMMENDED_MIN_STACK 128000
static void mglv3d_CheckStackSize(void)
{
	struct Process *proc = (struct Process *)FindTask(NULL);
	ULONG stackSize = (ULONG)proc->pr_StackSize;
	ULONG effectiveStack = stackSize;

	/* Under at least one Shell replacement, pr_StackSize does NOT
	 * reflect the Shell's actual current Stack setting; cli_DefaultStack
	 * (stored in LONGWORDS per the AmigaOS autodocs, hence *4 for bytes)
	 * does, so it is used whenever there is a CLI. Checking pr_CLI for
	 * non-zero first: it's a BPTR, 0 if this process has no CLI at all
	 * (e.g. launched from Workbench), in which case cli_DefaultStack
	 * doesn't apply and pr_StackSize is the only signal available. */
	if (proc->pr_CLI != (BPTR)0)
	{
		struct CommandLineInterface *cli = (struct CommandLineInterface *)BADDR(proc->pr_CLI);
		ULONG cliStack = (ULONG)(cli->cli_DefaultStack) * 4;
		effectiveStack = cliStack;
		D(("MGLInit: stack diag -- pr_StackSize=%lu cli_DefaultStack(bytes)=%lu (using cli_DefaultStack)\n",
		   stackSize, cliStack));
	}
	else
	{
		D(("MGLInit: stack diag -- pr_StackSize=%lu, no CLI (pr_CLI==0), using pr_StackSize\n", stackSize));
	}

	if (effectiveStack < MGLV3D_RECOMMENDED_MIN_STACK)
	{
		D(("MGLInit: WARNING -- current stack size is %lu bytes. This project has seen "
		       "real crashes/hangs (usually affecting whatever runs NEXT, not this program "
		       "itself) with a small stack -- recommend running 'Stack %ld' or larger before "
		       "starting this program.\n",
		       effectiveStack, (LONG)MGLV3D_RECOMMENDED_MIN_STACK));
		D(("MGLInit: WARNING -- stack size %lu below recommended minimum %ld\n",
		   effectiveStack, (LONG)MGLV3D_RECOMMENDED_MIN_STACK));
	}
	else
	{
		D(("MGLInit: stack size %lu OK (recommended minimum %ld)\n",
		   effectiveStack, (LONG)MGLV3D_RECOMMENDED_MIN_STACK));
	}
}

GLboolean MGLInit(void)
{
	extern void vid_ResetCloseDesktopRequest(void);

	mglv3d_CheckStackSize();

	/* Per-program defaults. Every program calls MGLInit before the mglChoose*
	 * setters, so this is where a request left behind by the previous program
	 * in the resident library gets cleared. */
	vid_ResetCloseDesktopRequest();

	/*
	 * OPEN ONLY WHAT IS NOT ALREADY OPEN, and remember which ones those were.
	 *
	 * These four are not the driver's private handles: in a statically linked
	 * program they are the SAME globals the host's own proto/ inlines
	 * dereference. A host that opened intuition.library for its own window had
	 * its pointer overwritten here, and MGLTerm then closed and NULLed it --
	 * so the host's next CloseWindow() went through a null base, after the
	 * driver had finished and looked blameless.
	 *
	 * Opening a library twice is harmless in itself (OpenLibrary just bumps the
	 * count and returns the same base), so the overwrite was survivable. NULLing
	 * a base the driver did not open is not.
	 */
	if (!IntuitionBase)
	{
		IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 0L);
		s_opened_intuition = (IntuitionBase != NULL);
	}
	if (!GfxBase)
	{
		GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 0L);
		s_opened_gfx = (GfxBase != NULL);
	}
	if (!UtilityBase)
	{
		UtilityBase = OpenLibrary("utility.library", 0L);
		s_opened_utility = (UtilityBase != NULL);
	}
	if (!CyberGfxBase)
	{
		CyberGfxBase = OpenLibrary("cybergraphics.library", 0L);
		s_opened_cybergfx = (CyberGfxBase != NULL);
	}

	if (!IntuitionBase || !GfxBase || !UtilityBase || !CyberGfxBase)
	{
	    E(("Library initialization failed:\n"));

	    if (!IntuitionBase) E(("- intuition.library (How are you doing this ?)\n"));
	    if (!GfxBase)       E(("- graphics.library (Strange!)\n"));
	    if (!CyberGfxBase)  E(("- cybergraphics.library\n"));

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
	/* Same reason as MGLDeleteContext's own clear (context.c): the pointer is
	 * a library-side global that outlives the process in the resident library,
	 * and after this function the bases below are closed, so whatever it points
	 * at is unusable. A program that terminates without deleting its context
	 * would otherwise leave it aimed at that memory for the next one. */
	{
		extern GLcontext mini_CurrentContext;

		mini_CurrentContext = NULL;
	}

	/*
	 * Close and NULL only the bases this driver opened. The rest belong to the
	 * host: in a static link they are the same globals its own proto/
	 * inlines read, so nulling one left its next CloseWindow() or similar
	 * dereferencing NULL -- after the driver had finished, which made it look
	 * like the host's own bug.
	 *
	 * The flags are reset too, so a program that calls MGLInit again after
	 * MGLTerm reopens whatever it needs rather than inheriting stale state.
	 */
	if (s_opened_cybergfx)
	{
		if (CyberGfxBase) CloseLibrary(CyberGfxBase);
		CyberGfxBase = NULL;
		s_opened_cybergfx = GL_FALSE;
	}
	if (s_opened_intuition)
	{
		if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
		IntuitionBase = NULL;
		s_opened_intuition = GL_FALSE;
	}
	if (s_opened_gfx)
	{
		if (GfxBase) CloseLibrary((struct Library *)GfxBase);
		GfxBase = NULL;
		s_opened_gfx = GL_FALSE;
	}
	if (s_opened_utility)
	{
		if (UtilityBase) CloseLibrary(UtilityBase);
		UtilityBase = NULL;
		s_opened_utility = GL_FALSE;
	}
}

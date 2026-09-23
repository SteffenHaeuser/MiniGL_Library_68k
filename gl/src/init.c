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


static char rcsid[] UNUSED = "$Id: init.c,v 1.4 2001/12/25 00:55:26 tfrieden Exp $";

//surgeon: added 11-04-02 - initializes glDrawArrays to glDrawElements wrapper for clipping with compiled arrays

extern void Init_ArrayToElements_Warpper(void);


struct Library *UtilityBase;
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;

extern struct DosLibrary *DOSBase;
extern struct ExecBase *SysBase;

void MGL_SINCOS_Init(void);

struct Library *CyberGfxBase = NULL;


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
		printf("MGLInit: WARNING -- current stack size is %lu bytes. This project has seen "
		       "real crashes/hangs (usually affecting whatever runs NEXT, not this program "
		       "itself) with a small stack -- recommend running 'Stack %ld' or larger before "
		       "starting this program.\n",
		       effectiveStack, (LONG)MGLV3D_RECOMMENDED_MIN_STACK);
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
	mglv3d_CheckStackSize();

	IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 0L);
	GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 0L);
	UtilityBase = OpenLibrary("utility.library", 0L);
	CyberGfxBase = OpenLibrary("cybergraphics.library", 0L);

	if (!IntuitionBase || !GfxBase || !UtilityBase || !CyberGfxBase)
	{
	    printf("Library initialization failed:\n");

	    if (!IntuitionBase) printf("- intuition.library (How are you doing this ?)\n");
	    if (!GfxBase)       printf("- graphics.library (Strange!)\n");
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
	if (CyberGfxBase)       CloseLibrary(CyberGfxBase);
	CyberGfxBase = NULL;

	if (IntuitionBase)      CloseLibrary((struct Library *)IntuitionBase);
	IntuitionBase = NULL;

	if (GfxBase)            CloseLibrary((struct Library *)GfxBase);
	GfxBase = NULL;

	if (UtilityBase)        CloseLibrary(UtilityBase);
	UtilityBase = NULL;
}

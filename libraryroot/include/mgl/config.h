
/*
 * $Id: config.h,v 1.1.1.1 2000/04/07 19:44:51 tfrieden Exp $
 *
 * $Date: 2000/04/07 19:44:51 $
 * $Revision: 1.1.1.1 $
 *
 * (C) 1999 by Hyperion
 * All rights reserved
 *
 * This file is part of the MiniGL library project
 * See the file Licence.txt for more details
 *
 */

#ifndef __GLCONFIG_H
#define __GLCONFIG_H


/* This define enables clamping glColor functions
** (only the lazy programmer needs this overhead :))
*/

#define CLAMP_COLORS 1

/* USE_MGLAPI and NO_MGLMACROS are gone (2026-09-29). They selected an inline
** alternative to gl.h's gl* macros, inherited from Hyperion's MiniGL; nothing
** in this tree ever defined the switch, so the header behind it was never
** compiled and had silently drifted out of parity with the driver.
**
** The CLIENT copy kept them until 2026-10-03, because <libraries/
** minigl_dispatch.h> did define USE_MGLAPI -- which is what made the client
** header a fork of this one. It does not any more: every gl* macro here now
** resolves to a dispatch-wrapped name, so the suppression has nothing left to
** do and the client header is a verbatim copy of this file. See gl.h.
*/


// Stack sizes of the different matrix stacks

#define MODELVIEW_STACK_SIZE    40
#define PROJECTION_STACK_SIZE  5
/* GL 1.1 requires at least 2 for the texture matrix stack. */
#define TEXTURE_STACK_SIZE     10

// Define this to make mglLockMode available
#define AUTOMATIC_LOCKING_ENABLE 1

// Left undefined, gl.h declares MGLDebugLevel and the mglSetDebugLevel() macro
#define NLOGGING 1

// Define if you don't want debugging
#define GLNDEBUG 1

// Define if you don't want glGetError functionality. Left undefined:
// every check records its error for glGetError. Defining it silences
// the reports only -- each call site stops on its own.
// #define GL_NOERRORCHECK 1


#define NCGXDEBUG 1

// Bounds a clip polygon (MGLPolygon.verts), draw.c's color-batch slots and
// the chunk size of vertex-array draws (vertexarray.c, vertexelements.c)
#define MGL_MAXVERTS 1024

#endif






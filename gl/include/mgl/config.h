
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

/* USE_MGLAPI makes gl.h include mgl/minigl.h instead of defining the
** gl* compatibility macros. minigl.h is not part of this tree.
*/

//#define USE_MGLAPI 1

#ifdef USE_MGLAPI
	#define NO_MGLMACROS 1
#else
	#undef NO_MGLMACROS
#endif


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






/*
 * $Id: minigl.h,v 1.1.1.1 2000/04/07 19:44:51 tfrieden Exp $
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

#ifndef __MINIGL_H_INLINES
#define __MINIGL_H_INLINES

#ifndef MGLAPI
	#if defined(__GNUC__)
		#define MGLAPI static inline
	#elif defined(__STORMC__)
		#define MGLAPI __inline
	#elif defined(__VBCC__)
		#define MGLAPI inline
	#endif
#endif

#define CC mini_CurrentContext

MGLAPI void   glPointSize(GLfloat s)
{
	 CC->CurrentPointSize = s;
}

/* Calls the real entry point rather than poking CurrentLineWidth the way
 * glPointSize above pokes its field: the zero/negative rejection lives in
 * GLLineWidth, and inlining a bare store here would quietly bypass it. */
MGLAPI void   glLineWidth(GLfloat w)
{
	 GLLineWidth(CC, w);
}

MGLAPI void   glTexGenfv(GLenum coord, GLenum pname, const GLfloat *params)
{
	 GLTexGenfv(CC, coord, pname, params);
}

MGLAPI void   glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{
	 GLCopyTexSubImage2D(CC, target, level, xoffset, yoffset, x, y, width, height);
}

MGLAPI void   glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border)
{
	 GLCopyTexImage2D(CC, target, level, internalformat, x, y, width, height, border);
}

/* The eleven entry points gl.h's macros named before any function existed
 * behind them (2026-09-12, GL 1.1 audit item 10). All call the real functions,
 * which hold the validation. */
MGLAPI GLboolean glAreTexturesResident(GLsizei n, const GLuint *textures, GLboolean *residences)
{
	 return GLAreTexturesResident(CC, n, textures, residences);
}

MGLAPI void   glEdgeFlag(GLboolean flag)
{
	 GLEdgeFlag(CC, flag);
}

MGLAPI void   glEdgeFlagv(const GLboolean *flag)
{
	 GLEdgeFlagv(CC, flag);
}

MGLAPI void   glEdgeFlagPointer(GLsizei stride, const GLvoid *pointer)
{
	 GLEdgeFlagPointer(CC, stride, pointer);
}

MGLAPI void   glGetDoublev(GLenum pname, GLdouble *params)
{
	 GLGetDoublev(CC, pname, params);
}

MGLAPI void   glGetPointerv(GLenum pname, GLvoid **params)
{
	 GLGetPointerv(CC, pname, params);
}

MGLAPI void   glIndexi(GLint c)
{
	 GLIndexi(CC, c);
}

MGLAPI void   glIndexiv(const GLint *c)
{
	 GLIndexiv(CC, c);
}

MGLAPI void   glIndexPointer(GLenum type, GLsizei stride, const GLvoid *pointer)
{
	 GLIndexPointer(CC, type, stride, pointer);
}

MGLAPI void   glPrioritizeTextures(GLsizei n, const GLuint *textures, const GLclampf *priorities)
{
	 GLPrioritizeTextures(CC, n, textures, priorities);
}

MGLAPI void   glReadBuffer(GLenum mode)
{
	 GLReadBuffer(CC, mode);
}

MGLAPI void   glTexEnviv(GLenum target, GLenum pname, GLint *param)
{
	 GLTexEnvi(CC, target, pname, *(param)) ;
}

MGLAPI void   glTexEnvfv(GLenum target, GLenum pname, GLfloat *param)
{
	GLTexEnvi(CC, target, pname, (GLint)(*(param))) ;
}


MGLAPI void glGetBooleanv( GLenum pname, GLboolean *params)
{
	GLGetBooleanv( CC, pname, params) ;
}

MGLAPI void glGetIntegerv( GLenum pname, GLint *params)
{
	GLGetIntegerv( CC, pname, params) ;
}

MGLAPI GLboolean  glIsEnabled(GLenum cap)
{
   return( GLIsEnabled(CC, cap) );
}

MGLAPI void glAlphaFunc(GLenum func, GLclampf ref)
{
	GLAlphaFunc(CC, func, ref);
}

MGLAPI void glBegin(GLenum mode)
{
	GLBegin(CC, mode);
}

MGLAPI void glBindTexture(GLenum target, GLuint texture)
{
	GLBindTexture(CC, target, texture);
}

MGLAPI void glBlendFunc(GLenum sfactor, GLenum dfactor)
{
	GLBlendFunc(CC, sfactor, dfactor);
}

MGLAPI void glClear(GLbitfield mask)
{
	GLClear(CC, mask);
}

MGLAPI void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
	GLClearColor(CC, red, green, blue, alpha);
}

MGLAPI void glClearDepth(GLclampd depth)
{
	GLClearDepth(CC, depth);
}

MGLAPI void glColor3f(GLfloat red, GLfloat green, GLfloat blue)
{
#ifdef CLAMP_COLORS
	if (red<0.f)		red = 0.f;
	else if (red>1.f)	red = 1.f;
	if (green<0.f)		green = 0.f;
	else if (green>1.f)	green = 1.f;
	if (blue<0.f)		blue = 0.f;
	else if (blue>1.f)	blue = 1.f;
#endif

	CC->CurrentColor.r = (GLfloat)red;
	CC->CurrentColor.g = (GLfloat)green;
	CC->CurrentColor.b = (GLfloat)blue;
	CC->CurrentColor.a = (GLfloat)1.f;
	CC->UpdateCurrentColor = GL_TRUE;
}

MGLAPI void glColor3fv(GLfloat *v)
{
	GLfloat red, green, blue;

	red	= v[0];
	green	= v[1];
	blue	= v[2];

#ifdef CLAMP_COLORS
	if (red<0.f)		red = 0.f;
	else if (red>1.f)	red = 1.f;
	if (green<0.f)		green = 0.f;
	else if (green>1.f)	green = 1.f;
	if (blue<0.f)		blue = 0.f;
	else if (blue>1.f)	blue = 1.f;
#endif
	CC->CurrentColor.r = (GLfloat)red;
	CC->CurrentColor.g = (GLfloat)green;
	CC->CurrentColor.b = (GLfloat)blue;
	CC->CurrentColor.a = (GLfloat)1.f;
	CC->UpdateCurrentColor = GL_TRUE;
}

MGLAPI void glColor3ubv(GLubyte *v)
{
	const GLfloat f = 1.f/255.f;
	CC->CurrentColor.r = (GLfloat)v[0]*f;
	CC->CurrentColor.g = (GLfloat)v[1]*f;
	CC->CurrentColor.b = (GLfloat)v[2]*f;
	CC->CurrentColor.a = (GLfloat)1.f;
	CC->UpdateCurrentColor = GL_TRUE;
}

MGLAPI void glColor3ub(GLubyte red, GLubyte green, GLubyte blue)
{
	const GLfloat f = 1.f/255.f;
	CC->CurrentColor.r = (GLfloat)red*f;
	CC->CurrentColor.g = (GLfloat)green*f;
	CC->CurrentColor.b = (GLfloat)blue*f;
	CC->CurrentColor.a = (GLfloat)1.f;
	CC->UpdateCurrentColor = GL_TRUE;
}

MGLAPI void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
#ifdef CLAMP_COLORS
	if (red<0.f)		red = 0.f;
	else if (red>1.f)	red = 1.f;
	if (green<0.f)		green = 0.f;
	else if (green>1.f)	green = 1.f;
	if (blue<0.f)		blue = 0.f;
	else if (blue>1.f)	blue = 1.f;
	if (alpha<0.f)		alpha = 0.f;
	else if (alpha>1.f)	alpha = 1.f;
#endif

	CC->CurrentColor.r = (GLfloat)red;
	CC->CurrentColor.g = (GLfloat)green;
	CC->CurrentColor.b = (GLfloat)blue;
	CC->CurrentColor.a = (GLfloat)alpha;
	CC->UpdateCurrentColor = GL_TRUE;
}

MGLAPI void glColor4fv(GLfloat *v)
{
	GLfloat red, green, blue, alpha;

	red	= v[0];
	green	= v[1];
	blue	= v[2];
	alpha	= v[3];

#ifdef CLAMP_COLORS
	if (red<0.f)		red = 0.f;
	else if (red>1.f)	red = 1.f;
	if (green<0.f)		green = 0.f;
	else if (green>1.f)	green = 1.f;
	if (blue<0.f)		blue = 0.f;
	else if (blue>1.f)	blue = 1.f;
	if (alpha<0.f)		alpha = 0.f;
	else if (alpha>1.f)	alpha = 1.f;
#endif
	CC->CurrentColor.r = (GLfloat)red;
	CC->CurrentColor.g = (GLfloat)green;
	CC->CurrentColor.b = (GLfloat)blue;
	CC->CurrentColor.a = (GLfloat)alpha;
	CC->UpdateCurrentColor = GL_TRUE;
}

MGLAPI void glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
	const GLfloat f = 1.f/255.f;
	CC->CurrentColor.r = (GLfloat)red*f;
	CC->CurrentColor.g = (GLfloat)green*f;
	CC->CurrentColor.b = (GLfloat)blue*f;
	CC->CurrentColor.a = (GLfloat)alpha*f;
	CC->UpdateCurrentColor = GL_TRUE;
}

MGLAPI void glColor4ubv(GLubyte *v)
{
	const GLfloat f = 1.f/255.f;
	CC->CurrentColor.r = (GLfloat)v[0]*f;
	CC->CurrentColor.g = (GLfloat)v[1]*f;
	CC->CurrentColor.b = (GLfloat)v[2]*f;
	CC->CurrentColor.a = (GLfloat)v[3]*f;
	CC->UpdateCurrentColor = GL_TRUE;
}

MGLAPI void glCullFace(GLenum mode)
{
	GLCullFace(CC, mode);
}

MGLAPI void glDeleteTextures(GLsizei n, const GLuint *textures)
{
	GLDeleteTextures(CC, n, textures);
}

MGLAPI void glDepthFunc(GLenum func)
{
	GLDepthFunc(CC, func);
}

MGLAPI void glDepthMask(GLboolean flag)
{
	GLDepthMask(CC, flag);
}

MGLAPI void glDepthRange(GLclampd n, GLclampd f)
{
	GLDepthRange(CC, n, f);
}

MGLAPI void glDisable(GLenum cap)
{
	MGLSetState(CC, cap, GL_FALSE);
}

MGLAPI void glDrawBuffer(GLenum mode)
{
	GLDrawBuffer(CC, mode);
}

MGLAPI void glEnable(GLenum cap)
{
	MGLSetState(CC, cap, GL_TRUE);
}

MGLAPI void glEnd(void)
{
	GLEnd(CC);
}

MGLAPI void glFinish(void)
{
	GLFinish(CC);
}

MGLAPI void glFlush(void)
{
	GLFlush(CC);
}

MGLAPI void glFogf(GLenum pname, GLfloat param)
{
	GLFogf(CC, pname, param);
}

MGLAPI void glFogi(GLenum pname, GLint param)
{
	GLFogf(CC, pname, (GLfloat)param);
}

MGLAPI void glFogfv(GLenum pname, GLfloat *param)
{
	GLFogfv(CC, pname, param);
}

MGLAPI void glFrontFace(GLenum mode)
{
	GLFrontFace(CC, mode);
}

MGLAPI void glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
	GLFrustum(CC, left, right, bottom, top, zNear, zFar);
}

MGLAPI void glGenTextures(GLsizei n, GLuint *textures)
{
	GLGenTextures(CC, n, textures);
}

MGLAPI GLenum glGetError(void)
{
	return GLGetError(CC);
}

MGLAPI void glGetFloatv(GLenum pname, GLfloat *params)
{
	GLGetFloatv(CC, pname, params);
}

MGLAPI const GLubyte * glGetString(GLenum name)
{
	return GLGetString(CC, name);
}

MGLAPI void glHint(GLenum target, GLenum mode)
{
	GLHint(CC, target, mode);
}

MGLAPI void glLoadIdentity(void)
{
	GLLoadIdentity(CC);
}

MGLAPI void glLoadMatrixd(const GLdouble *m)
{
	GLLoadMatrixd(CC, m);
}

MGLAPI void glLoadMatrixf(const GLfloat *m)
{
	GLLoadMatrixf(CC, m);
}

MGLAPI void glMatrixMode(GLenum mode)
{
	GLMatrixMode(CC, mode);
}

MGLAPI void glMultMatrixd(const GLdouble *m)
{
	GLMultMatrixd(CC, m);
}

MGLAPI void glMultMatrixf(const GLfloat *m)
{
	GLMultMatrixf(CC, m);
}

MGLAPI void glNormal3f(GLfloat x, GLfloat y, GLfloat z)
{
	GLuint nbp = ++CC->NormalBufferPointer;
	CC->NormalBuffer[nbp].x = x;
	CC->NormalBuffer[nbp].y = y;
	CC->NormalBuffer[nbp].z = z;
}

MGLAPI void glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
	GLOrtho(CC, left, right, bottom, top, zNear, zFar);
}

MGLAPI void glPixelStorei(GLenum pname, GLint param)
{
	GLPixelStorei(CC, pname, param);
}

MGLAPI void glPolygonMode(GLenum face, GLenum mode)
{
	GLPolygonMode(CC, face, mode);
}

MGLAPI void glPopMatrix(void)
{
	GLPopMatrix(CC);
}

MGLAPI void glPushMatrix(void)
{
	GLPushMatrix(CC);
}

MGLAPI void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
{
	GLReadPixels(CC, x, y, width, height, format, type, pixels);
}

MGLAPI void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
	GLRotatef(CC, angle, x, y, z);
}

MGLAPI void glRotatefEXT(GLfloat angle, const GLint xyz)
{
	GLRotatefEXT(CC, angle, xyz);
}

MGLAPI void glRotatefEXTs(GLfloat sin_an, GLfloat cos_an, const GLint xyz)
{
	GLRotatefEXTs(CC, sin_an, cos_an, xyz);
}

MGLAPI void glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z)
{
	GLRotated(CC, angle, x, y, z);
}

MGLAPI void glScalef(GLfloat x, GLfloat y, GLfloat z)
{
	GLScalef(CC, x, y, z);
}

MGLAPI void glScaled(GLdouble x, GLdouble y, GLdouble z)
{
	GLScaled(CC, x, y, z);
 }

MGLAPI void glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
	GLScissor(CC, x, y, width, height);
}

MGLAPI void glShadeModel(GLenum mode)
{
	GLShadeModel(CC, mode);
}

MGLAPI void glTexCoord2f(GLfloat s, GLfloat t)
{
	#define thisvertex CC->VertexBuffer[CC->VertexBufferPointer]

	thisvertex.v.u0 = (GLfloat)s;
	thisvertex.v.v0 = (GLfloat)t;

	#undef thisvertex
}

MGLAPI void glTexCoord2fv(GLfloat *v)
{
	#define thisvertex CC->VertexBuffer[CC->VertexBufferPointer]

	thisvertex.v.u0 = (GLfloat)v[0];
	thisvertex.v.v0 = (GLfloat)v[1];

	#undef thisvertex
}

MGLAPI void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
	#define thisvertex CC->VertexBuffer[CC->VertexBufferPointer]

	thisvertex.v.u0 = (GLfloat)(s/q);
	thisvertex.v.v0 = (GLfloat)(t/q);
	thisvertex.q = q;

	CC->CurrentTexQValid = GL_TRUE;

	#undef thisvertex
}

MGLAPI void glTexCoord4fv(GLfloat *v)
{
	#define thisvertex CC->VertexBuffer[CC->VertexBufferPointer]

	thisvertex.v.u0 = (GLfloat)(v[0]/v[3]);
	thisvertex.v.v0 = (GLfloat)(v[1]/v[3]);
	thisvertex.q = v[3];

	CC->CurrentTexQValid = GL_TRUE;

	#undef thisvertex
}

MGLAPI void glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
	GLTexEnvi(CC, target, pname, (GLint)param);
}

MGLAPI void glTexEnvi(GLenum target, GLenum pname, GLint param)
{
	GLTexEnvi(CC, target, pname, param);
}

MGLAPI void glTexGeni(GLenum coord, GLenum mode, GLenum map)
{
	GLTexGeni(CC, coord, mode, map);
}

MGLAPI void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
	GLTexImage2D(CC, target, level, internalformat, width, height, border, format, type, pixels);
}

MGLAPI void glTexParameteri(GLenum target, GLenum pname, GLint param)
{
	GLTexParameteri(CC, target, pname, param);
}

MGLAPI void glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
	GLTexParameteri(CC, target, pname, (GLint)param);
}

MGLAPI void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
	GLTexSubImage2D(CC, target, level, xoffset, yoffset, width, height, format, type, pixels);
}

MGLAPI void glTranslated(GLdouble x, GLdouble y, GLdouble z)
{
	GLTranslated(CC, x, y, z);
}

MGLAPI void glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
	GLTranslatef(CC, x, y, z);
}

MGLAPI void glVertex2f(GLfloat x, GLfloat y)
{
	#define thisvertex CC->VertexBuffer[CC->VertexBufferPointer]

	thisvertex.bx = x;
	thisvertex.by = y;
	thisvertex.bz = (GLfloat)0.f;
	thisvertex.bw = (GLfloat)1.f;

	thisvertex.normal = CC->NormalBufferPointer;

	if(CC->ShadeModel == GL_SMOOTH)
	{
	thisvertex.color.r = CC->CurrentColor.r;
	thisvertex.color.g = CC->CurrentColor.g;
	thisvertex.color.b = CC->CurrentColor.b;
	thisvertex.color.a = CC->CurrentColor.a;
	}

	CC->VertexBufferPointer++;

	#undef thisvertex
}

MGLAPI void glVertex2fv(GLfloat *v)
{
	#define thisvertex CC->VertexBuffer[CC->VertexBufferPointer]

	thisvertex.bx = v[0];
	thisvertex.by = v[1];
	thisvertex.bz = (GLfloat)0.f;
	thisvertex.bw = (GLfloat)1.f;

	thisvertex.normal = CC->NormalBufferPointer;

	if(CC->ShadeModel == GL_SMOOTH)
	{
	thisvertex.color.r = CC->CurrentColor.r;
	thisvertex.color.g = CC->CurrentColor.g;
	thisvertex.color.b = CC->CurrentColor.b;
	thisvertex.color.a = CC->CurrentColor.a;
	}

	CC->VertexBufferPointer++;

	#undef thisvertex
}

MGLAPI void glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
	#define thisvertex CC->VertexBuffer[CC->VertexBufferPointer]

	thisvertex.bx = x;
	thisvertex.by = y;
	thisvertex.bz = z;
	thisvertex.bw = (GLfloat)1.f;

	thisvertex.normal = CC->NormalBufferPointer;

	if(CC->ShadeModel == GL_SMOOTH)
	{
	thisvertex.color.r = CC->CurrentColor.r;
	thisvertex.color.g = CC->CurrentColor.g;
	thisvertex.color.b = CC->CurrentColor.b;
	thisvertex.color.a = CC->CurrentColor.a;
	}

	CC->VertexBufferPointer++;

	#undef thisvertex
}

MGLAPI void glVertex3fv(GLfloat *v)
{
	#define thisvertex CC->VertexBuffer[CC->VertexBufferPointer]

	thisvertex.bx = v[0];
	thisvertex.by = v[1];
	thisvertex.bz = v[2];
	thisvertex.bw = (GLfloat)1.f;

	thisvertex.normal = CC->NormalBufferPointer;

	if(CC->ShadeModel == GL_SMOOTH)
	{
	thisvertex.color.r = CC->CurrentColor.r;
	thisvertex.color.g = CC->CurrentColor.g;
	thisvertex.color.b = CC->CurrentColor.b;
	thisvertex.color.a = CC->CurrentColor.a;
	}

	CC->VertexBufferPointer++;

	#undef thisvertex
}

MGLAPI void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
	#define thisvertex CC->VertexBuffer[CC->VertexBufferPointer]

	thisvertex.bx = x;
	thisvertex.by = y;
	thisvertex.bz = z;
	thisvertex.bw = w;

	thisvertex.normal = CC->NormalBufferPointer;

	if(CC->ShadeModel == GL_SMOOTH)
	{
	thisvertex.color.r = CC->CurrentColor.r;
	thisvertex.color.g = CC->CurrentColor.g;
	thisvertex.color.b = CC->CurrentColor.b;
	thisvertex.color.a = CC->CurrentColor.a;
	}

	CC->VertexBufferPointer++;

	#undef thisvertex
}

MGLAPI void glVertex4fv(GLfloat *v)
{
	#define thisvertex CC->VertexBuffer[CC->VertexBufferPointer]

	thisvertex.bx = v[0];
	thisvertex.by = v[1];
	thisvertex.bz = v[2];
	thisvertex.bw = v[3];

	thisvertex.normal = CC->NormalBufferPointer;

	if(CC->ShadeModel == GL_SMOOTH)
	{
	thisvertex.color.r = CC->CurrentColor.r;
	thisvertex.color.g = CC->CurrentColor.g;
	thisvertex.color.b = CC->CurrentColor.b;
	thisvertex.color.a = CC->CurrentColor.a;
	}

	CC->VertexBufferPointer++;

	#undef thisvertex
}

MGLAPI void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
	GLViewport(CC, x, y, width, height);
}

MGLAPI void * mglCreateContext(int offx, int offy, int w, int h)
{
	CC = (GLcontext)MGLCreateContext(offx, offy, w,h);
	return (void *)CC;
}

MGLAPI void * mglCreateContextFromID(GLint id, GLint *w, GLint *h)
{
	CC = (GLcontext)MGLCreateContextFromID(id, w, h);
	return (void *)CC;
}


MGLAPI void mglDeleteContext(void)
{
	MGLDeleteContext(CC);
}

MGLAPI void mglEnableSync(GLboolean enable)
{
	MGLEnableSync(CC, enable);
}

MGLAPI void mglExit(void)
{
	MGLExit(CC);
}

MGLAPI void mglMinTriArea(GLfloat area)
{
	MGLMinTriArea(CC, area);
}

MGLAPI void * mglGetWindowHandle(void)
{
	return MGLGetWindowHandle(CC);
}

MGLAPI void mglIdleFunc(IdleFn i)
{
	MGLIdleFunc(CC, i);
}

MGLAPI void mglKeyFunc(KeyHandlerFn k)
{
	MGLKeyFunc(CC, k);
}

MGLAPI GLboolean mglLockDisplay(void)
{
	return MGLLockDisplay(CC);
}

#ifdef AUTOMATIC_LOCKING_ENABLE
MGLAPI void mglLockMode(GLenum lockMode)
{
	MGLLockMode(CC, lockMode);
}
#endif

MGLAPI void mglMainLoop(void)
{
	MGLMainLoop(CC);
}

MGLAPI void mglMouseFunc(MouseHandlerFn m)
{
	MGLMouseFunc(CC, m);
}

MGLAPI void mglResizeContext(GLsizei width, GLsizei height)
{
	MGLResizeContext(CC, width, height);
}

MGLAPI void mglPrintMatrix(GLenum mode)
{
	MGLPrintMatrix(CC, mode);
}

MGLAPI void mglPrintMatrixStack(GLenum mode)
{
	MGLPrintMatrixStack(CC,mode);
}

MGLAPI void mglSpecialFunc(SpecialHandlerFn s)
{
	MGLSpecialFunc(CC, s);
}

MGLAPI void mglSwitchDisplay(void)
{
	MGLSwitchDisplay(CC);
}

MGLAPI void mglUnlockDisplay(void)
{
	MGLUnlockDisplay(CC);
}

MGLAPI void mglWriteShotPPM(char *filename)
{
	MGLWriteShotPPM(CC, filename);
}

MGLAPI void mglTexMemStat(GLint *Current, GLint *Peak)
{
	MGLTexMemStat(CC, Current, Peak);
}

MGLAPI void mglSetZOffset(GLfloat offset)
{
	MGLSetZOffset(CC, offset);
}

MGLAPI void glColorTable(GLenum target, GLenum internalformat, GLint width, GLenum format, GLenum type, GLvoid *data)
{
	GLColorTable(CC, target, internalformat, width, format, type, data);
}

MGLAPI void glColorTableEXT(GLenum target, GLenum internalformat, GLint width, GLenum format, GLenum type, GLvoid *data)
{
	GLColorTable(CC, target, internalformat, width, format, type, data);
}


/*
** These function have no hidden context parameter, but are handled the same in case we want to make
** MiniGL a shared library some day
*/

MGLAPI void gluLookAt(GLfloat ex, GLfloat ey, GLfloat ez, GLfloat cx, GLfloat cy, GLfloat cz, GLfloat ux, GLfloat uy, GLfloat uz)
{
	GLULookAt(ex, ey, ez, cx, cy, cz, ux, uy, uz);
}

MGLAPI void gluPerspective(GLfloat fovy, GLfloat aspect, GLfloat znear, GLfloat zfar)
{
	GLUPerspective(fovy, aspect, znear, zfar);
}

/*
** Additional functions for targetted context creation
*/

MGLAPI GLboolean mglLockBack(MGLLockInfo *info)
{
	return MGLLockBack(CC, info);
}


/*
** Functions implementing vertex arras
*/

MGLAPI void glEnableClientState(GLenum cap)
{
        switch ((int)cap)
        {
                case GL_TEXTURE_COORD_ARRAY:
                CC->ClientState |= GLCS_TEXTURE;
                break;

                case GL_COLOR_ARRAY:
                CC->ClientState |= GLCS_COLOR;
                break;

                case GL_VERTEX_ARRAY:
                CC->ClientState |= GLCS_VERTEX;
                break;

                default:
                GLFlagError(CC, 1, GL_INVALID_ENUM);
                break;
        }
}

MGLAPI void glDisableClientState(GLenum cap)
{
        switch ((int)cap)
        {
                case GL_TEXTURE_COORD_ARRAY:
                CC->ClientState &= ~GLCS_TEXTURE;
                break;

                case GL_COLOR_ARRAY:
                CC->ClientState &= ~GLCS_COLOR;
                break;

                case GL_VERTEX_ARRAY:
                CC->ClientState &= ~GLCS_VERTEX;
                break;

                default:
                GLFlagError(CC, 1, GL_INVALID_ENUM);
                break;
        }
}

MGLAPI void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	GLTexCoordPointer(CC, size, type, stride, pointer);
}

MGLAPI void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	GLColorPointer(CC, size, type, stride, pointer);
}

MGLAPI void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	GLVertexPointer(CC, size, type, stride, pointer);
}

MGLAPI void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *pointer)
{
	GLDrawElements(CC, mode, count, type, pointer);
}

MGLAPI void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
	GLDrawArrays(CC, mode, first, count);
}


MGLAPI void glLockArrays(GLuint first, GLsizei count)
{
	GLLockArrays(CC, first, count);
}

MGLAPI void glUnlockArrays(void)
{
	GLUnlockArrays(CC);
}

MGLAPI void glArrayElement(GLint i)
{
	CC->ElementIndex[CC->VertexBufferPointer++] = (UWORD)i;
}


/*
** Everything related to multitexturing
*/

MGLAPI void glActiveTextureARB(GLenum unit)
{
	int u = unit - GL_TEXTURE0_ARB;

	/* `>=` (2026-09-13): `>` let GL_TEXTURE2_ARB index the two-unit state. */
	if(u<0 || u>=MAX_TEXUNIT)
	{
		GLFlagError(CC, 1, GL_INVALID_ENUM);
		return;
	}

	CC->ActiveTexture = u;
}

MGLAPI void glMultiTexCoord2fARB(GLenum unit, GLfloat s, GLfloat t)
{
	int u = unit - GL_TEXTURE0_ARB;

	/* `>=` (2026-09-13): `>` let GL_TEXTURE2_ARB index the two-unit state. */
	if(u<0 || u>=MAX_TEXUNIT)
	{
		GLFlagError(CC, 1, GL_INVALID_ENUM);
		return;
	}

	#define thisvertex CC->VertexBuffer[CC->VertexBufferPointer]

	if(u)
	{
		thisvertex.v.u1 = (GLfloat)s;
		thisvertex.v.v1 = (GLfloat)t;
	}
	else //unit 0
	{
		thisvertex.v.u0 = (GLfloat)s;
		thisvertex.v.v0 = (GLfloat)t;
	}

#undef thisvertex
}

MGLAPI void glMultiTexCoord2fvARB(GLenum unit, GLfloat *v)
{
	int u = unit - GL_TEXTURE0_ARB;

	/* `>=` (2026-09-13): `>` let GL_TEXTURE2_ARB index the two-unit state. */
	if(u<0 || u>=MAX_TEXUNIT)
	{
		GLFlagError(CC, 1, GL_INVALID_ENUM);
		return;
	}

	#define thisvertex CC->VertexBuffer[CC->VertexBufferPointer]

	if(u)
	{
		thisvertex.v.u1 = (GLfloat)v[0];
		thisvertex.v.v1 = (GLfloat)v[1];
	}
	else //unit 0
	{
		thisvertex.v.u0 = (GLfloat)v[0];
		thisvertex.v.v0 = (GLfloat)v[1];
	}

#undef thisvertex
}

MGLAPI void mglDrawMultitexBuffer(GLenum s, GLenum d, GLenum env)
{
	MGLDrawMultitexBuffer (CC, s, d, env);
}

#endif

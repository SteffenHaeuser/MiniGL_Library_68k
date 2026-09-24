#ifndef MINIGL_CONTEXT_ACCESS_H
#define MINIGL_CONTEXT_ACCESS_H

#include <mgl/gl.h>

/*
 * minigl.library deliberately uses MiniGL's historic single current-context
 * model. Real backend archives own this symbol; the backend-free stub provides
 * its own definition in context_state_stub.c.
 *
 * mgl/gl.h already includes mgl/context.h, so GLcontext must NOT be redeclared
 * here (vbcc correctly rejects duplicate typedefs).
 */
extern GLcontext mini_CurrentContext;

#define MINIGL_CONTEXT()       (mini_CurrentContext)
#define MINIGL_FAST_CONTEXT()  (mini_CurrentContext)
#define MINIGL_BEGIN_CONTEXT() (mini_CurrentContext)
#define MINIGL_END_CONTEXT()   ((void)0)

#endif

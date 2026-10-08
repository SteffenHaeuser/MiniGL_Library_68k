/*
(C) 2025-2026 Dennis van der Boon

Original MiniGL/mglq3 shipped a header of this name as an opt-in
performance shortcut: included after gl.h, it #undef'd and redefined
glBegin, glEnd, the glColor family, the glVertex family, the glTexCoord
family, glMultiTexCoordARB, glEnableClientState, glDisableClientState,
glArrayElement and glPointSize as macros that poke MGLVertex / W3D_Vertex
struct fields (mglvert->v.u, ->bx, ->v.color.r and so on) directly,
skipping the real GL call entirely to save call overhead in hot
per-vertex code. Nine glquake source files include it for exactly that.

This port defines nothing. Reimplementing the same direct-struct-poke
trick against V3DVertex's own field layout (v.u0/v0, v.u1/v1, top-level
.color) would reintroduce the bug class that miniglext.c's mglTV23fv and
its siblings had: hand-written field pokes against the OLD W3D_Vertex
shape. It is also unnecessary, because this port's own gl.h already
defines every one of those names as a macro forwarding to the real GL
function (glBegin -> GLBegin, glVertex3fv -> GLVertex3fv, glColor4f ->
GLColor4f and so on), so calling the real public API instead of poking
fields is already what happens, unconditionally.

So this header's only job is to exist -- files that include it after
gl.h must not fail with "file not found", and repeated includes must stay
idempotent via the MGL_MACROS_H guard below -- and to redefine nothing,
leaving gl.h's own definitions, already in scope by the time any of those
files includes this header, to win untouched.

Do NOT add #undef / #define pairs here to restore the original's
bypass-the-real-GL-call behaviour: that reintroduces the bug class
described above.
*/

#ifndef MGL_MACROS_H
#define MGL_MACROS_H

#endif

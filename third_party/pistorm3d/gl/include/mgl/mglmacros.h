/*
MiniGLV3D port note (Phase L): original MiniGL/mglq3 shipped this header as
an opt-in performance shortcut -- included after gl.h, it #undef's and
redefines glBegin, glEnd, the glColor family, the glVertex family, the
glTexCoord family, glMultiTexCoordARB, glEnableClientState,
glDisableClientState, glArrayElement and glPointSize as macros that poke
MGLVertex / W3D_Vertex struct fields (mglvert->v.u, ->bx, ->v.color.r, etc.)
directly, skipping the real GL call entirely to save call overhead in hot
per-vertex code (glquake's gl_rsurf.c, gl_mirror.c, gl_warp.c and 6 more
files include it for exactly this).

Reimplementing that same direct-struct-poke trick against V3DVertex's own
field layout (v.u0/v0, v.u1/v1, top-level .color) would just reintroduce
the exact bug Phase L1's miniglext.c fix removed (mglTV23fv & co. did
precisely this, by hand, against the OLD W3D_Vertex shape). It's also
unnecessary: MiniGLV3D's own gl.h (Phase G-J) already defines every one of
these as a macro forwarding to the real GL function (glBegin -> GLBegin,
glVertex3fv -> GLVertex3fv, glColor4f -> GLColor4f, etc.) -- the "call the
real public API instead of poking fields" approach this project settled on
for miniglext.c is already what gl.h's own macros do, unconditionally, for
every symbol this header used to redefine.

So this header's only real job here is to exist (files that include it
after gl.h must not fail with "file not found", and multiple includes must
stay idempotent via the MGL_MACROS_H guard below) and to NOT redefine
anything -- gl.h's own definitions, already correct and already in scope by
the time any of these 9 glquake files includes this header, are left to
win untouched. Confirmed via grep against gl.h that literally every symbol
the original redefined already has a real forwarding macro there:
glActiveTextureARB, glArrayElement, glBegin, glEnd, glEnableClientState,
glDisableClientState, glPointSize, glVertex2i, the glColor family (ub/ubv
variants included), the glVertex family (f/fv variants), the glTexCoord
family (f/fv variants), glMultiTexCoordARB and its pointer variant.

Do NOT add #undef / #define pairs here to "restore" the original's
performance-macro behavior bypassing the real GL calls -- see the header
comment above and STATUS.md's Phase L1 section for why that reintroduces a
real, already-fixed bug class.
*/

#ifndef MGL_MACROS_H
#define MGL_MACROS_H

#endif

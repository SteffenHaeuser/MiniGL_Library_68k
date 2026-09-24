# minigl.library V25 Classic - V25 dispatch ABI layout

V25 Classic is based on the existing V23.1 Classic/Q3-fix tree and updates the
public includes and positional dispatch-table layout to the supplied V25.

The original V23 tree was based on the main developer's shared-library tree and adds the
public entry points implemented by the PiStorm3D V22 backend:
`glClientActiveTextureARB`, `glInterleavedArrays`, `glMultiDrawArrays`,
`glBlendEquation`, `glBlendFuncSeparate`, `glIsTexture` and
`mglChooseZBufferDepth`. The existing `glPolygonOffset` dispatch slot now
calls the real PiStorm3D implementation instead of a library-side stub.

The new entries are appended while `MINIGL_DISPATCH_ABI_VERSION` deliberately
remains 3. No V22 dispatch entry is removed or treated as a separate ABI.
Feature availability is identified exclusively by the resident library
version, which is 23. All older field offsets remain unchanged, so existing
ABI-3 executables continue to use the unchanged table prefix. Classic and
diagnostic-stub builds provide compatibility fallbacks.
The V22 headers replace the older PiStorm3D header copies. Removed experimental
APIs which are absent from V22 (`mglCreateContextFromWindow`,
`mglEmergencyCloseScreen`, and `glMGLForceNewRenderPass`) are deliberately not
part of this mainline-based tree.

The source distribution does not contain the old V14 binaries or import
libraries. Rebuild `minigl.library` against the complete V22 backend archive
and regenerate `libminigl_dispatch.a` with the supplied makefiles. The former
experimental FastPath API and its examples/backends have been removed.

This tree uses a single dispatch-table LVO instead of one LVO per GL/MGL
function. `MiniGLOpen()` opens `minigl.library` V10, fetches the complete
`MGLDispatchTable` once and validates it. Public GL/MGL names are macros that
call the corresponding function pointer in `MiniGLDispatch`.

The only custom library vector is `MiniGLGetDispatchTableLVO` at -30.
The old stack-LVO and register-LVO client stubs have intentionally been
removed from this tree. V10 is therefore an intentional ABI break from the
experimental older libraries.

## GCC

PiStorm3D:
  make -f Makefile_dispatch.gcc
  make -f Makefile_dispatch.gcc importlib
  make -f Makefile_dispatch.gcc testprograms

Stub:
  make -f Makefile_dispatch_stub.gcc
  make -f Makefile_dispatch_stub.gcc importlib
  make -f Makefile_dispatch_stub.gcc testprograms

Classic:
  make -f Makefile_dispatch_classic.gcc

All GCC backends use the same `lib/libminigl_dispatch.a`. It only contains
MiniGLBase, MiniGLOpen/MiniGLClose and the one -30 dispatch-table LVO stub.

## vbcc

Use Makefile_dispatch.vbcc, Makefile_dispatch_stub.vbcc or
Makefile_dispatch_classic.vbcc. Their example targets link the three tiny
client objects directly.

See DISPATCH_API.md for the ABI design.

## V11 direct-backend dispatch
See `DIRECT_BACKEND_DISPATCH.md`. V11 removes the old per-function shared-library wrappers/LVOs and returns direct GL*/MGL* backend pointers through one dispatch-table LVO.

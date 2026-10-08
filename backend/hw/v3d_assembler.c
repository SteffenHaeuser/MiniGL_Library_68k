/*
 * (C) 2025-2026 Dennis van der Boon
 */

/*
 * QPU shader assembler, ported from an earlier library by the same author
 * (a single-header library -- #define V3D_ASSEMBLER_IMPLEMENTATION then
 * #include to get the implementation, matching stb-style headers).
 * v3d_assembler.h holds that header; this file is the adaptation layer
 * the earlier library's own top matter provided, rewritten for MiniGLV3D.
 *
 * v3d_assembler.h needs two REQUIRED macros defined before inclusion:
 *  - v3d_memcmp: standard memcmp, no adaptation needed.
 *  - v3d_vsnprintf: only called by the header's disassembler section
 *    ("// >>> qpu_disasm.c", a single call site inside `append()`) --
 *    MiniGLV3D only ever ASSEMBLES (mnemonics -> machine code), never
 *    disassembles, so that code path is dead for our purposes. Stubbed
 *    rather than porting the earlier library's own myvsnprintf (which used a
 *    global library-base pointer -- exactly the kind of global-state pattern
 *    MiniGLV3D's architecture avoids; V3DDevice is always passed
 *    explicitly, never assumed via a global).
 *
 * struct v3d_device_info: v3d_assembler.h only forward-declares this
 * (`struct v3d_device_info;`) and uses it via pointer (devinfo->ver
 * etc.) -- needs the FULL definition in scope before the implementation
 * block. v3d_device.h's struct v3d_device_info is field-identical to
 * the earlier library's original, so including it first is sufficient --
 * no new struct needed.
 *
 * D macro collision: v3d_assembler.h defines its own `#define D 1 //
 * Destination` internally (a QPU opcode-encoding field constant,
 * unrelated to this codebase's D()/E() debug-print macros) -- colliding
 * with v3d_debug.h's D(x). the earlier library's own assembler handles this by
 * NOT including v3d_debug.h until AFTER the implementation block (letting
 * v3d_assembler.h's own D win during that section, since it also
 * self-declares kprintf/E() -- see its own line ~90), then `#undef D` +
 * `#include "v3d_debug.h"` afterward to restore normal D()/E() for the
 * rest of this file. Same pattern here.
 */

#include <exec/execbase.h>
#include <string.h>
#include <stdarg.h>

#include "../include/v3d_device.h"

#define v3d_memcmp memcmp

/* Parameters intentionally unnamed -- never dereferenced, this is a
 * stub (see the file-level comment on v3d_vsnprintf above). Avoids
 * vbcc's "statement has no effect" warning on (void)-cast idioms for
 * silencing unused-parameter warnings, since there's no parameter name
 * to warn about in the first place. */
static size_t v3d_assembler_vsnprintf_stub(char* s, size_t n, const char* format, va_list args)
{
    /* unused params, intentionally not referenced -- see file-level comment above */
    return 0;
}
#define v3d_vsnprintf v3d_assembler_vsnprintf_stub

#define V3D_ASSEMBLER_IMPLEMENTATION
#include "v3d_assembler.h"
#undef V3D_ASSEMBLER_IMPLEMENTATION

#undef D
#include "v3d_debug.h"

/*
 * Shader mnemonic source, copied verbatim from the earlier library (the ACTIVE
 * set -- a second, disabled copy sat in a permanent #if 0 block there and
 * was not carried over). Same content as the earlier library's
 * pseudocode+mnemonic form, just without the register-allocation comments.
 */
/*
 * The RENDER-pass vertex shader must produce the FINAL, viewport-scaled
 * device-space Z itself: this shader's "z_sc" output (`fmul rf13, rf6,
 * r4`, i.e. z_s/w_s -- the perspective-divided clip-space Z) is scaled
 * and offset in the shader, a few instructions further down. The
 * CLIPPER_Z_SCALE_AND_OFFSET CL packet (`ClipperZScaleAndOffset(backend,
 * 0.5f, 0.5f)`, draw.c) is NOT a blanket post-shader transform applied to
 * whatever the vertex shader writes -- cross-checked against MESA's V3D
 * compiler (`broadcom/compiler/v3d_nir_lower_io.c`'s
 * `v3d_nir_lower_io_instr`, the "zs" output for the render-pass vertex
 * shader, `!is_coord`): `z = pos[2] * viewport_z_scale * rcp_wc +
 * viewport_z_offset`. An unscaled Z can fall outside the valid [0,1]
 * clip-plane range (`ClipperZMinMaxClippingPlanes(0.0, 1.0)`), and the
 * hardware's near-plane clipper then re-clips the primitive against an
 * out-of-range Z reference frame.
 *
 * Every other RENDER-pass vertex shader below does the same (they all copy
 * this matrix-multiply-and-z_sc core verbatim) -- NOT the coordinate
 * shaders, which per MESA's
 * own convention (`is_coord` outputs the RAW 4-component clip position
 * for the BINNING pass, never a scaled "zs") keeps writing unscaled Z.
 */
static const char* g_vertex_shader_assembly[] =
{
	// set the model w value w_m = 1.0
	//mov_addalu( assembler, w_m, immfp2(0) );

    "or rf3, 0x3f800000, 0x3f800000 ; nop",

	// get the scale factor to use with xP and yP
	//ldunifrf( assembler, scale_p );

    "nop ; nop ; ldunifrf.rf10",

    /* Separate Y-axis screen-space scale (rf14, free/untouched here): a
     * non-square screen needs its own Y scale rather than sharing scale_p
     * with X. Second
     * uniform in stream order right after scale_p (rf10), before the
     * M_00.. matrix uniforms -- draw.c's v3d_my_uniforms struct has a
     * matching scale_p_y field at this exact position. */
    "nop ; nop ; ldunifrf.rf14", // scale_p_y

	// retrieve x_m, y_m, z_m model coordinates
	// from the three VPM offsets
	///ldvpmv_in( assembler, x_m, immi(0) );
	///ldvpmv_in( assembler, y_m, immi(1) );
	///ldvpmv_in( assembler, z_m, immi(2) );

    "ldvpmv_in rf0,  0 ; nop",
    "ldvpmv_in rf1,  1 ; nop",
    "ldvpmv_in rf2,  2 ; nop",

	// retrieve (s,t) texture coordinate for the vertex
	// from the VPM
	//ldvpmv_in( assembler, s, immi(3) );
	//ldvpmv_in( assembler, t, immi(4) );

    "ldvpmv_in rf11,  3 ; nop",
    "ldvpmv_in rf12,  4 ; nop",

	// calculate x_s y_s z_s w_s
	// from x_m y_m z_m w_m using M_ms
	// this does the matrix multiply
	// it reads the matrix coefficients from the uniforms
	// r0 and r5 are overwritten as a side effect

    //
    //DO MATRIX MAGIC!!
    //

    //	  qpu_mul_v4_m44(
	//	  assembler,
	//	  x_m, y_m, z_m, w_m,   -> rf0, rf1, rf2, rf3
	//	  x_s, y_s, z_s, w_s    -> rf4, rf5, rf6, rf7

    // x_out = x_in M_00 + y_in M_10 + z_in M_20 + w_in M_30
	// r5 = M_00
	//ldunif( assembler );

    "nop ; nop ; ldunif",

	// x_out = x_in M_00
	//fmul( assembler, x_out, x_in, r(5) );
    // r5 = M_10
	//ldunif( assembler );

    "nop ; fmul rf4, rf0, r5 ; ldunif",

	// acc = y_m M_10
	//fmul( assembler, acc, y_in, r(5) );

    "nop ; fmul r0, rf1, r5",

	// x_out += y_in M_10
	// x_out  = x_in M_00 + y_in M_10
	//fadd( assembler, x_out, x_out, acc );
	// r5 = M_20
	//ldunif( assembler );

    "fadd rf4, rf4, r0 ; nop ; ldunif",

	// acc = z_in M_20
	//fmul( assembler, acc, z_in, r(5) );

    "nop ; fmul r0, rf2, r5",

	// x_out += z_in M_20
	// x_out  = x_in M_00 + y_in M_10 + z_in M_20
	//fadd( assembler, x_out, x_out, acc );
	// r5 = M_30
	//ldunif( assembler );

    "fadd rf4, rf4, r0 ; nop ; ldunif",

	// acc = w_in M_30
	//fmul( assembler, acc, w_in, r(5) );

    "nop ; fmul r0, rf3, r5",

	// x_out += w_in M_30
	// x_out  = x_in M_00 + y_in M_10 + z_in M_20 + w_in M_30
	//fadd( assembler, x_out, x_out, acc );
	// r5 = M_01
	//ldunif( assembler );

    "fadd rf4, rf4, r0 ; nop ; ldunif",

	// y_out = x_in M_01
	//fmul( assembler, y_out, x_in, r(5) );
	// r5 = M_11
	//ldunif( assembler );

    "nop ; fmul rf5, rf0, r5 ; ldunif",

	// acc = y_in M_11
	//fmul( assembler, acc, y_in, r(5) );

    "nop ; fmul r0, rf1, r5",

	// y_out += y_in M_11
	// y_out  = x_in M_01 + y_in M_11
	//fadd( assembler, y_out, y_out, acc );
	// r5 = M_21
	//ldunif( assembler );

    "fadd rf5, rf5, r0 ; nop ; ldunif",

	// acc = z_in M_21
	//fmul( assembler, acc, z_in, r(5) );

    "nop ; fmul r0, rf2, r5",

	// y_out += z_in M_21
	// y_out  = x_in M_01 + y_in M_11 + z_in M_21
	//fadd( assembler, y_out, y_out, acc );
	// r5 = M_31
	//ldunif( assembler );

    "fadd rf5, rf5, r0 ; nop ; ldunif",

	// acc = w_in M_31
	//fmul( assembler, acc, w_in, r(5) );

    "nop ; fmul r0, rf3, r5",

	// y_out += w_in M_31
	// y_out  = x_in M_01 + y_in M_11 + z_in M_21 + w_in M_31
	//fadd( assembler, y_out, y_out, acc );
	// r5 = M_02
	//ldunif( assembler );

    "fadd rf5, rf5, r0 ; nop ; ldunif",

	// z_out = x_in M_02
	//fmul( assembler, z_out, x_in, r(5) );
	// r5 = M_12
	//ldunif( assembler );

    "nop ; fmul rf6, rf0, r5 ; ldunif",

	// acc = y_in M_12
	//fmul( assembler, acc, y_in, r(5) );

    "nop ; fmul r0, rf1, r5",

	// z_out += y_in M_12
	// z_out  = z_in M_02 + y_in M_12
	//fadd( assembler, z_out, z_out, acc );
	// r5 = M_22
	//ldunif( assembler );

    "fadd rf6, rf6, r0 ; nop ; ldunif",

	// acc = z_in M_22
	//fmul( assembler, acc, z_in, r(5) );

    "nop ; fmul r0, rf2, r5",

	// z_out += z_in M_22
	// z_out  = z_in M_02 + y_in M_12 + z_in M_22
	//fadd( assembler, z_out, z_out, acc );
	// r5 = M_32
	//ldunif( assembler );

    "fadd rf6, rf6, r0 ; nop ; ldunif",

	// acc = w_in M_32
	//fmul( assembler, acc, w_in, r(5) );

    "nop ; fmul r0, rf3, r5",

	// z_out += w_in M_32
	// z_out  = z_in M_02 + y_in M_12 + z_in M_22 + w_in M_32
	//fadd( assembler, z_out, z_out, acc );
	// r5 = M_03
	//ldunif( assembler );

    "fadd rf6, rf6, r0 ; nop ; ldunif",

	// w_s = x_in M_03
	//fmul( assembler, w_out, x_in, r(5) );
	// r5 = M_13
	//ldunif( assembler );

    "nop ; fmul rf7, rf0, r5 ; ldunif",

	// acc = y_in M_13
	//fmul( assembler, acc, y_in, r(5) );

    "nop ; fmul r0, rf1, r5",

	// w_out += y_in M_13
	// w_out  = x_in M_03 + y_in M_13
	//fadd( assembler, w_out, w_out, acc );
	// r5 = M_23
	//ldunif( assembler );

    "fadd rf7, rf7, r0 ; nop ; ldunif",

	// acc = z_in M_23
	//fmul( assembler, acc, z_in, r(5) );

    "nop ; fmul r0, rf2, r5",

	// w_out += z_in M_23
	// w_out  = x_in M_03 + y_in M_13 + z_in M_23
	//fadd( assembler, w_out, w_out, acc );
	// r5 = M_33
	//ldunif( assembler );

    "fadd rf7, rf7, r0 ; nop ; ldunif",

	// acc = w_in M_33
	//fmul( assembler, acc, w_in, r(5) );

    "nop ; fmul r0, rf3, r5",

	// w_out += w_in M_33
	// w_out  = x_in M_03 + y_in M_13 + z_in M_23 + w_in M_33
	//fadd( assembler, w_out, w_out, acc );

    "fadd rf7, rf7, r0 ; nop",

    //
    //END OF MATRIX
    //

	// at this point x_s, y_s, z_s, w_s are all calculated
	// now generate the x_p and y_p pixel positions

	// calculate r4 = 1 / w_s
	//mov_addalu( assembler, rmagic(recip), w_s );

    "or recip, rf7, rf7             ; nop",

	// wait an instruction for the SFU to calculate and put result into r4

    "nop ; nop",

	// x_p = x_s / w_s * 0.5 * scale_p
	// acc_a = x_s / w_s
	//fmul( assembler, acc_a, x_s, r(4) );

    "nop ; fmul r0, rf4, r4",

    // acc_a = x_s / w_s * scale_p
	//fmul( assembler, acc_a, acc_a, scale_p );

    "nop ; fmul r0, r0, rf10",

	// x_p = x_s / w_s * scale_p * 0.5 * 256.0
	//fmul( assembler, x_p, acc_a, immfp2(-1 + 8) );

    "nop ; fmul rf8, r0, 0x43000000",

	// invert sign of y_s
	// acc_a = 0.0
	//fsub( assembler, acc_a, acc_a, acc_a );

    "fsub r0, r0, r0 ; nop",

	// acc_a = 0.0 - y_s
	//fsub( assembler, acc_a, acc_a, y_s );

    "fsub r0, r0, rf5 ; nop",

	// y_p = -y_s / w_s * scale_p * 0.5 * 256.0
	// acc_a = 1 / w_s (r4) * -y_s
	//fmul( assembler, acc_a, acc_a, r(4) );

    "nop ; fmul r0, r0, r4",

	// acc_a = -y_s / w_s * scale_p_y (separate Y scale, rf14)
	//fmul( assembler, acc_a, acc_a, scale_p_y );

    "nop ; fmul r0, r0, rf14",

	// y_p = -y_s / w_s * scale_p_y * 0.5 * 256.0
	//fmul( assembler, y_p, acc_a, immfp2(-1 + 8) );

    "nop ; fmul rf9, r0, 0x43000000",

	// convert screen values to integer
	//ftoin( assembler, x_p, x_p );
	//ftoin( assembler, y_p, y_p );

    "ftoin rf8, rf8 ; nop",
    "ftoin rf9, rf9 ; nop",

	// calculate zS in Cartesian, which normalizes it
	//fmul( assembler, z_sc, z_s, r(4) );

    "nop ; fmul rf13, rf6, r4",
    /* The Z scale and offset come from the uniform stream
     * (v3d_my_uniforms.z_scale / .z_offset, draw.c), APPENDED after the 16
     * matrix values -- so these two reads must stay the LAST ldunif* in this
     * shader. rf0/rf1 held x_m/y_m and are dead from the end of the matrix
     * multiply above to the end of the shader, so no new register is needed.
     * Two instructions of slack before first use; the matrix multiply above
     * proves one is enough. */
    "nop ; nop ; ldunifrf.rf0",          // viewport z scale  (context->sz)
    "nop ; nop ; ldunifrf.rf1",          // viewport z offset (context->az)
    "nop ; fmul rf13, rf13, rf0",
    "fadd rf13, rf13, rf1 ; nop",

	// now store everything to the VPM
	//stvpmv( assembler, immi(0), x_p );
	//stvpmv( assembler, immi(1), y_p );
	//stvpmv( assembler, immi(2), z_sc );
	// 1 / w_s
	//stvpmv( assembler, immi(3), r(4) );
	//stvpmv( assembler, immi(4), s );
	//stvpmv( assembler, immi(5), t );

    "stvpmv 0, rf8                  ; nop",
    "stvpmv 1, rf9                  ; nop",
    "stvpmv 2, rf13                 ; nop",
    "stvpmv 3, r4                   ; nop",
    "stvpmv 4, rf11                 ; nop",
    "stvpmv 5, rf12                 ; nop",

    // this is at the end of Macoy's shader but it doesn't validate
    "vpmwt -              ; nop",
    "nop                  ; nop ; thrsw",
    "nop                  ; nop",
    "nop                  ; nop",
};

static const char* g_coordinate_shader_assembly[] = {
	// set the model w value w_m = 1.0
	//mov_addalu( assembler, w_m, immfp2(0) );

    "or rf3, 0x3f800000, 0x3f800000 ; nop",

    // get the scale factor to use with xP and yP
    //ldunifrf( assembler, scale_p );

    "nop ; nop ; ldunifrf.rf10",

    /* Separate Y-axis screen-space scale -- same as
     * g_vertex_shader_assembly, see that shader's own comment. */
    "nop ; nop ; ldunifrf.rf14", // scale_p_y

	// retrieve x_m, y_m, z_m model coordinates
	// from the three VPM offsets
	//ldvpmv_in( assembler, x_m, immi(0) );
	//ldvpmv_in( assembler, y_m, immi(1) );
	//ldvpmv_in( assembler, z_m, immi(2) );

    "ldvpmv_in rf0,  0 ; nop",
    "ldvpmv_in rf1,  1 ; nop",
    "ldvpmv_in rf2,  2 ; nop",

    // calculate x_s y_s z_s w_s
	// from x_m y_m z_m w_m using M_ms
	// this does the matrix multiply
	// it reads the matrix coefficients from the uniforms
	// r0 and r5 are overwritten as a side effect
    //
    //DO MATRIX MAGIC!!
    //

    //	  qpu_mul_v4_m44(
	//	  assembler,
	//	  x_m, y_m, z_m, w_m,   -> rf0, rf1, rf2, rf3
	//	  x_s, y_s, z_s, w_s    -> rf4, rf5, rf6, rf7

    // x_out = x_in M_00 + y_in M_10 + z_in M_20 + w_in M_30
	// r5 = M_00
	//ldunif( assembler );

    "nop ; nop ; ldunif",

	// x_out = x_in M_00
	//fmul( assembler, x_out, x_in, r(5) );
    // r5 = M_10
	//ldunif( assembler );

    "nop ; fmul rf4, rf0, r5 ; ldunif",

	// acc = y_m M_10
	//fmul( assembler, acc, y_in, r(5) );

    "nop ; fmul r0, rf1, r5",

	// x_out += y_in M_10
	// x_out  = x_in M_00 + y_in M_10
	//fadd( assembler, x_out, x_out, acc );
	// r5 = M_20
	//ldunif( assembler );

    "fadd rf4, rf4, r0 ; nop ; ldunif",

	// acc = z_in M_20
	//fmul( assembler, acc, z_in, r(5) );

    "nop ; fmul r0, rf2, r5",

	// x_out += z_in M_20
	// x_out  = x_in M_00 + y_in M_10 + z_in M_20
	//fadd( assembler, x_out, x_out, acc );
	// r5 = M_30
	//ldunif( assembler );

    "fadd rf4, rf4, r0 ; nop ; ldunif",

	// acc = w_in M_30
	//fmul( assembler, acc, w_in, r(5) );

    "nop ; fmul r0, rf3, r5",

	// x_out += w_in M_30
	// x_out  = x_in M_00 + y_in M_10 + z_in M_20 + w_in M_30
	//fadd( assembler, x_out, x_out, acc );
	// r5 = M_01
	//ldunif( assembler );

    "fadd rf4, rf4, r0 ; nop ; ldunif",

	// y_out = x_in M_01
	//fmul( assembler, y_out, x_in, r(5) );
	// r5 = M_11
	//ldunif( assembler );

    "nop ; fmul rf5, rf0, r5 ; ldunif",

	// acc = y_in M_11
	//fmul( assembler, acc, y_in, r(5) );

    "nop ; fmul r0, rf1, r5",

	// y_out += y_in M_11
	// y_out  = x_in M_01 + y_in M_11
	//fadd( assembler, y_out, y_out, acc );
	// r5 = M_21
	//ldunif( assembler );

    "fadd rf5, rf5, r0 ; nop ; ldunif",

	// acc = z_in M_21
	//fmul( assembler, acc, z_in, r(5) );

    "nop ; fmul r0, rf2, r5",

	// y_out += z_in M_21
	// y_out  = x_in M_01 + y_in M_11 + z_in M_21
	//fadd( assembler, y_out, y_out, acc );
	// r5 = M_31
	//ldunif( assembler );

    "fadd rf5, rf5, r0 ; nop ; ldunif",

	// acc = w_in M_31
	//fmul( assembler, acc, w_in, r(5) );

    "nop ; fmul r0, rf3, r5",

	// y_out += w_in M_31
	// y_out  = x_in M_01 + y_in M_11 + z_in M_21 + w_in M_31
	//fadd( assembler, y_out, y_out, acc );
	// r5 = M_02
	//ldunif( assembler );

    "fadd rf5, rf5, r0 ; nop ; ldunif",

	// z_out = x_in M_02
	//fmul( assembler, z_out, x_in, r(5) );
	// r5 = M_12
	//ldunif( assembler );

    "nop ; fmul rf6, rf0, r5 ; ldunif",

	// acc = y_in M_12
	//fmul( assembler, acc, y_in, r(5) );

    "nop ; fmul r0, rf1, r5",

	// z_out += y_in M_12
	// z_out  = z_in M_02 + y_in M_12
	//fadd( assembler, z_out, z_out, acc );
	// r5 = M_22
	//ldunif( assembler );

    "fadd rf6, rf6, r0 ; nop ; ldunif",

	// acc = z_in M_22
	//fmul( assembler, acc, z_in, r(5) );

    "nop ; fmul r0, rf2, r5",

	// z_out += z_in M_22
	// z_out  = z_in M_02 + y_in M_12 + z_in M_22
	//fadd( assembler, z_out, z_out, acc );
	// r5 = M_32
	//ldunif( assembler );

    "fadd rf6, rf6, r0 ; nop ; ldunif",

	// acc = w_in M_32
	//fmul( assembler, acc, w_in, r(5) );

    "nop ; fmul r0, rf3, r5",

	// z_out += w_in M_32
	// z_out  = z_in M_02 + y_in M_12 + z_in M_22 + w_in M_32
	//fadd( assembler, z_out, z_out, acc );
	// r5 = M_03
	//ldunif( assembler );

    "fadd rf6, rf6, r0 ; nop ; ldunif",

	// w_s = x_in M_03
	//fmul( assembler, w_out, x_in, r(5) );
	// r5 = M_13
	//ldunif( assembler );

    "nop ; fmul rf7, rf0, r5 ; ldunif",

	// acc = y_in M_13
	//fmul( assembler, acc, y_in, r(5) );

    "nop ; fmul r0, rf1, r5",

	// w_out += y_in M_13
	// w_out  = x_in M_03 + y_in M_13
	//fadd( assembler, w_out, w_out, acc );
	// r5 = M_23
	//ldunif( assembler );

    "fadd rf7, rf7, r0 ; nop ; ldunif",

	// acc = z_in M_23
	//fmul( assembler, acc, z_in, r(5) );

    "nop ; fmul r0, rf2, r5",

	// w_out += z_in M_23
	// w_out  = x_in M_03 + y_in M_13 + z_in M_23
	//fadd( assembler, w_out, w_out, acc );
	// r5 = M_33
	//ldunif( assembler );

    "fadd rf7, rf7, r0 ; nop ; ldunif",

	// acc = w_in M_33
	//fmul( assembler, acc, w_in, r(5) );

    "nop ; fmul r0, rf3, r5",

	// w_out += w_in M_33
	// w_out  = x_in M_03 + y_in M_13 + z_in M_23 + w_in M_33
	//fadd( assembler, w_out, w_out, acc );

    "fadd rf7, rf7, r0 ; nop",

    //
    //END OF MATRIX
    //

    // calculate r4 = 1 / w_s - recip stores in r4
    //mov_addalu( assembler, rmagic(recip), w_s );

    "or recip, rf7, rf7             ; nop",

    // wait an instruction for the SFU to calculate and put result into r4

    "nop ; nop",

	// x_p = x_s / w_s * 0.5 * scale_p
	// acc_a = x_s / w_s
    //fmul( assembler, acc_a, x_s, r(4) );

    "nop ; fmul r0, rf4, r4",

    // acc_a = x_s / w_s * scale_p
	//fmul( assembler, acc_a, acc_a, scale_p );

    "nop ; fmul r0, r0, rf10",

    // x_p = x_s / w_s * scale_p * 0.5 * 256.0
	//fmul( assembler, x_p, acc_a, immfp2(-1 + 8) );

    "nop ; fmul rf8, r0, 0x43000000",

    // y_p = -y_s / w_s * scale_p * 0.5 * 256.0
	// acc_a = 0.0
    //fsub( assembler, acc_a, acc_a, acc_a );

    "fsub r0, r0, r0 ; nop",

	// acc_a = 0 - y_s =
	//fsub( assembler, acc_a, acc_a, y_s );

    "fsub r0, r0, rf5 ; nop",

	// acc_a = 1 / w_s (r4) * y_s
	//fmul( assembler, acc_a, acc_a, r(4) );

    "nop ; fmul r0, r0, r4",

	// acc_a = y_s / w_s * scale_p_y (separate Y scale, rf14)
	//fmul( assembler, acc_a, acc_a, scale_p_y );

    "nop ; fmul r0, r0, rf14",

	// y_p = y_s / w_s * scale_p_y * 0.5 * 256.0
	//fmul( assembler, y_p, acc_a, immfp2(-1 + 8) );

    "nop ; fmul rf9, r0, 0x43000000",

    // convert screen values to integer
	//ftoin( assembler, x_p, x_p );
	//ftoin( assembler, y_p, y_p );

    "ftoin rf8, rf8 ; nop",
    "ftoin rf9, rf9 ; nop",

    // now write everything out to the vpm
	//stvpmv( assembler, immi(0), x_s );
	//stvpmv( assembler, immi(1), y_s );
	//stvpmv( assembler, immi(2), z_s );
	//stvpmv( assembler, immi(3), w_s );
	//stvpmv( assembler, immi(4), x_p );
	//stvpmv( assembler, immi(5), y_p );

    "stvpmv 0, rf4                  ; nop",
    "stvpmv 1, rf5                  ; nop",
    "stvpmv 2, rf6                  ; nop",
    "stvpmv 3, rf7                  ; nop",
    "stvpmv 4, rf8                  ; nop",
    "stvpmv 5, rf9                  ; nop",

    // this is at the end of Macoy's shader but it doesn't validate
    "vpmwt -              ; nop",
    "nop                  ; nop ; thrsw",
    "nop                  ; nop",
    "nop                  ; nop",
};

/*
 * Clip-space coordinate shader -- a COPY of g_coordinate_shader_assembly
 * above, with exactly one change: w_m is read as a REAL 4th VPM input word
 * (offset 3) instead of being hardcoded to 1.0. draw.c selects it for every
 * needs_real_w draw. One such case is dh_DrawPoly/dh_DrawLine's clip-space
 * vertices (draw.c's use_clip_space path):
 * those vertices are created by hclip.c's Sutherland-Hodgeman clipping,
 * which linearly interpolates x,y,z,w TOGETHER in clip-space; the
 * resulting w (`.bw`) is NOT generally 1.0 once a real (non-affine)
 * gluPerspective/glFrustum projection is in play, unlike an object-space
 * vertex fed through a ModelView matrix (the "w_m = 1.0" the other
 * coordinate/vertex shaders hardcode). Feeding the identity matrix (as
 * draw.c's use_clip_space path does) means every matrix coefficient
 * multiplying x/y/z into w_out is zero and only the w_in*M33 term
 * survives -- so a REAL w_in here passes straight through to a REAL
 * w_out, correctly feeding the perspective divide (`recip = 1/w_s`) that
 * follows.
 */
static const char* g_coordinate_shader_clipspace_assembly[] = {
    "ldvpmv_in rf3, 3 ; nop", // w_m -- REAL clip-space w, not hardcoded 1.0

    "nop ; nop ; ldunifrf.rf10",

    "nop ; nop ; ldunifrf.rf14", // scale_p_y

    "ldvpmv_in rf0,  0 ; nop",
    "ldvpmv_in rf1,  1 ; nop",
    "ldvpmv_in rf2,  2 ; nop",

    "nop ; nop ; ldunif",

    "nop ; fmul rf4, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",

    "nop ; fmul rf5, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",

    "nop ; fmul rf6, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",

    "nop ; fmul rf7, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf7, rf7, r0 ; nop",

    "or recip, rf7, rf7             ; nop",

    "nop ; nop",

    "nop ; fmul r0, rf4, r4",

    "nop ; fmul r0, r0, rf10",

    "nop ; fmul rf8, r0, 0x43000000",

    "fsub r0, r0, r0 ; nop",

    "fsub r0, r0, rf5 ; nop",

    "nop ; fmul r0, r0, r4",

    "nop ; fmul r0, r0, rf14",

    "nop ; fmul rf9, r0, 0x43000000",

    "ftoin rf8, rf8 ; nop",
    "ftoin rf9, rf9 ; nop",

    "stvpmv 0, rf4                  ; nop",
    "stvpmv 1, rf5                  ; nop",
    "stvpmv 2, rf6                  ; nop",
    "stvpmv 3, rf7                  ; nop",
    "stvpmv 4, rf8                  ; nop",
    "stvpmv 5, rf9                  ; nop",

    "vpmwt -              ; nop",
    "nop                  ; nop ; thrsw",
    "nop                  ; nop",
    "nop                  ; nop",
};

/*
 * Untextured/flat-color fragment shader. Same output convention as
 * g_fragment_shader_assembly (rf7/rf8/rf9/rf10 = red/green/blue/alpha_out,
 * packed to tlb via vfpack), but with the entire texture path removed --
 * no ldvary, no wrtmuc, no TMU read/unpack. Color comes straight from 4
 * uniforms (r,g,b,a as floats, the same mechanism the MVP matrix uses)
 * loaded directly into the output registers via ldunifrf -- no
 * intermediate r5 hop needed since there's nothing to compute, just
 * move-and-pack. This is the GL equivalent of rendering with texturing
 * disabled and the current color applied flat across the primitive; a
 * per-vertex color needs a varying, which is what the smooth variants do.
 *
 * ldunifrf has the same 1-cycle signal latency as ldunif/ldvary (result
 * lands at the START of the NEXT instruction, not the one that issued
 * it) -- satisfied here since rf7..rf10 are each loaded in their own
 * instruction and not read until the vfpack several instructions after
 * the last load.
 *
 * THRSW protocol: per MESA's own compiler (broadcom/compiler/nir_to_vir.c's
 * vir_emit_last_thrsw, cross-checked against broadcom/compiler/
 * qpu_validate.c's qpu_validate_block), a threaded (4-way-threadable)
 * fragment shader's "last thread switch" must be signalled by TWO
 * CONSECUTIVE thrsw instructions, and the program must contain at least
 * one MORE thrsw afterward marking actual thread termination -- 3 total,
 * minimum, in that shape. g_fragment_shader_assembly above follows this
 * (thrsw+thrsw back-to-back at the TMU write, one more at the final tlb
 * write) as a side effect of its texture-fetch latency-hiding, which makes
 * it easy to miss in a shader with no TMU section to hang it on.
 * qpu_validate.c's check for this ("No program-end THRSW found") is
 * present in the real MESA source but commented out in this project's
 * ported v3d_assembler.h, so v3d_qpu_validate() does NOT catch a missing
 * thrsw here.
 *
 * The consecutive pair must also sit >=3 instructions before the final
 * thrsw (qpu_validate.c's in_thrsw_delay_slots check) -- the textured
 * shader satisfies this naturally (13 instructions of real TMU work in
 * between); this shader has no such work, so a single filler nop
 * instruction after the pair makes up the required gap (pair at
 * instructions 4-5, final thrsw at instruction 7: 7-4=3, satisfies ">=3").
 */
static const char* g_fragment_shader_untextured_assembly[] = {
    "nop ; nop ; ldunifrf.rf7",  // rf7  = red   (uniform 0)
    "nop ; nop ; ldunifrf.rf8",  // rf8  = green (uniform 1)
    "nop ; nop ; ldunifrf.rf9",  // rf9  = blue  (uniform 2)
    "nop ; nop ; ldunifrf.rf10", // rf10 = alpha (uniform 3)

    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2 (must be consecutive)
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- satisfies the >=3-instruction gap before the next thrsw

    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw (gap from the pair is exactly 3)
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * Untextured + fog. Builds directly on the untextured/flat-color shader
 * above (same 4 ldunifrf uniform loads for the base color, same thrsw
 * protocol) plus a fog blend.
 *
 * Fog colour and the mode coefficients are real uniforms (glFogfv/glFogf-
 * driven, via context->backend.fog_r/g/b and the fog mode/start/end/
 * density, converted CPU-side in draw.c, so the fragment shader needs no
 * second uniform stream) -- see the block below for the formula. The factor
 * is clamped to [0,1] because GL_FOG clamps an out-of-range fragment to the
 * fog colour or to unfogged rather than extrapolating past either. The blend
 * itself is
 * out = fogColor + fogFactor*(baseColor - fogColor), matching GL_FOG's
 * mix(fogColor, baseColor, fogFactor). Alpha is deliberately left
 * unblended (rf10 untouched) -- GL_FOG never fogs alpha.
 */
static const char* g_fragment_shader_untextured_fog_assembly[] = {
    "nop ; nop ; ldunifrf.rf7",  // rf7  = base red   (uniform 0)
    "nop ; nop ; ldunifrf.rf8",  // rf8  = base green (uniform 1)
    "nop ; nop ; ldunifrf.rf9",  // rf9  = base blue  (uniform 2)
    "nop ; nop ; ldunifrf.rf10", // rf10 = alpha (uniform 3) -- never fogged
    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf3/rf4/rf5 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf3",             // A
    "nop ; nop ; ldunifrf.rf4",             // B
    "nop ; fmul rf4, rf4, rf0",
    "fadd rf3, rf3, rf4 ; nop ; ldunifrf.rf4",             // linear factor
    "nop ; fmul rf4, rf4, rf0 ; ldunifrf.rf5",
    "nop ; fmul rf5, rf5, rf0",
    "nop ; fmul rf5, rf5, rf0",
    "fadd rf4, rf4, rf5 ; nop",             // C*c + D*c*c
    "or exp, rf4, rf4 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf4",             // M
    "nop ; fmul rf3, rf3, rf4",
    "or rf5, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf5, rf5, rf4 ; nop",             // 1-M
    "nop ; fmul rf5, rf5, r4",
    "fadd rf3, rf3, rf5 ; nop",             // fog factor
    "sub rf4, rf4, rf4 ; nop",
    "fmax rf3, rf3, rf4 ; nop",
    "or rf4, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf3, rf3, rf4 ; nop ; ldunifrf.rf4",             // clamped to [0,1]
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop",

    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- satisfies the >=3-instruction gap before the next thrsw

    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * Untextured + alpha test, GL_GEQUAL. Builds on the untextured shader
 * (same 4 ldunifrf base-color loads, same thrsw protocol) plus a fragment
 * discard.
 *
 * V3D has no dedicated "discard" instruction -- confirmed by tracing MESA's
 * actual compiler (broadcom/compiler/nir_to_vir.c's nir_intrinsic_terminate_if,
 * the real implementation behind GLSL's "discard"/OpenGL's alpha test, both
 * lower to the same mechanism): a comparison sets a flag via `fcmp` +
 * `.pushc`, then `setmsf` conditionally clears the per-sample coverage mask
 * (SETMSF = "set multi-sample flags") when that flag says the test failed.
 * The eventual `vfpack`/tlb write instructions still execute unconditionally
 * in the code -- discard works by making that write a no-op for cleared
 * samples, not by skipping instructions.
 *
 * Exact sequence mirrors MESA's own `ntq_emit_comparison`'s `nir_op_fge32`
 * case (`alpha >= threshold`, the standard GL_GEQUAL-style alpha test) and
 * `nir_intrinsic_terminate_if`'s conditional SETMSF verbatim, not invented
 * from scratch: `fcmp.pushc -, threshold, alpha` (operands deliberately
 * reversed vs. the source comparison -- FCMP(b,a) for "a>=b", matching
 * MESA's own vir_FCMP_dest(nop, src1, src0) call for fge32) sets the flag
 * such that IFA means "alpha >= threshold" (keep), IFNA means "alpha <
 * threshold" (discard) -- cond_invert=false for fge32 in MESA's own table,
 * so IFA is the true/keep condition, IFNA is what SETMSF conditions on here.
 * "-" is V3D_QPU_WADDR_NOP (`waddr_names[6]` in this ported assembler) --
 * the "discard the result, I only want the flag/mask side-effect"
 * destination, matching MESA's own `vir_nop_reg()` used for both FCMP's and
 * SETMSF's destinations.
 *
 * The threshold is a uniform (context->backend.alpha_ref, glAlphaFunc-
 * driven, others.c), so this shader keeps a fragment when alpha >= ref.
 * The other comparison functions have their own variants below.
 */
static const char* g_fragment_shader_untextured_alphatest_assembly[] = {
    "nop ; nop ; ldunifrf.rf7",  // rf7  = red   (uniform 0)
    "nop ; nop ; ldunifrf.rf8",  // rf8  = green (uniform 1)
    "nop ; nop ; ldunifrf.rf9",  // rf9  = blue  (uniform 2)
    "nop ; nop ; ldunifrf.rf10", // rf10 = alpha (uniform 3)
    "nop ; nop ; ldunifrf.rf15", // rf15 = alpha test threshold (uniform 4)

    "fcmp.pushc -, rf15, rf10 ; nop",        // flags = FCMP(threshold, alpha); IFA true means alpha >= threshold
    "setmsf.ifna -, 0 ; nop",                // discard (clear sample mask) when alpha < threshold

    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- satisfies the >=3-instruction gap before the next thrsw

    /* Passthrough Z write: see the untextured GL_GREATER variant for the
     * full derivation. Placed after the setmsf discard and before the
     * colour vfpacks -- the only legal slot. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop", // passthrough Z write; config from uniform stream
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * GL_GREATER alpha test, untextured. See the untextured GEQUAL variant's
 * own comment above for the full fcmp/setmsf mechanism derivation from
 * MESA's ntq_emit_comparison/nir_intrinsic_terminate_if.
 *
 * Keep condition is "alpha > threshold" (strict), not "alpha >= threshold"
 * -- FCMP only computes a >=-style comparison, so this is built from that
 * by testing the OPPOSITE direction and inverting which flag state
 * discards: `fcmp.pushc -, alpha, threshold` sets IFA true when
 * threshold >= alpha, i.e. when alpha <= threshold -- exactly the DISCARD
 * condition for GL_GREATER. So `setmsf.ifa` (not `.ifna`) clears the
 * sample mask on THAT flag state, keeping only alpha > threshold. This is
 * the GEQUAL shader's two lines with the fcmp operand order swapped
 * (rf10,rf15 instead of rf15,rf10) and .ifna flipped to .ifa.
 */
static const char* g_fragment_shader_untextured_alphatest_greater_assembly[] = {
    "nop ; nop ; ldunifrf.rf7",  // rf7  = red   (uniform 0)
    "nop ; nop ; ldunifrf.rf8",  // rf8  = green (uniform 1)
    "nop ; nop ; ldunifrf.rf9",  // rf9  = blue  (uniform 2)
    "nop ; nop ; ldunifrf.rf10", // rf10 = alpha (uniform 3)
    "nop ; nop ; ldunifrf.rf15", // rf15 = alpha test threshold (uniform 4)

    "fcmp.pushc -, rf10, rf15 ; nop", // flags = FCMP(alpha, threshold); IFA true means threshold >= alpha (i.e. alpha <= threshold)
    "setmsf.ifa -, 0 ; nop",          // discard (clear sample mask) when alpha <= threshold; keep when alpha > threshold

    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- satisfies the >=3-instruction gap before the next thrsw

    /*
     * PASSTHROUGH DEPTH WRITE -- without it, alpha-discarded fragments
     * still write depth.
     *
     * Alpha test here is a QPU-side discard (the setmsf above), but the FEP
     * commits depth before the QPU ever runs, so the discard would kill
     * only colour -- the depth stamp has already happened. Disabling early
     * Z does not help: it only moves WHEN the FEP writes, not WHO writes.
     * Per MESA's emit_frag_end, the shader must take over the Z write
     * itself, which is what makes it respect the coverage mask.
     *
     * `tlbu` (not `tlb`) is required: the TLB config is a shift register
     * preloaded with 0xffffffff, and a bare `tlb` write consumes the
     * default 0xff = "normal colour". Writing tlbu pulls a 32-bit config
     * word from the FRAGMENT UNIFORM STREAM instead -- draw.c appends
     * 0xffffff84 for exactly this instruction (TLB_TYPE_DEPTH (2<<6) |
     * TLB_V42_DEPTH_TYPE_INVARIANT (0<<3) | TLB_SAMPLE_MODE_PER_PIXEL
     * (1<<2), | 0xffffff00). INVARIANT means "take Z from the FEP", i.e.
     * a passthrough -- the shader does not compute a new depth, it just
     * routes the existing one through the QPU so the discard applies. The
     * source operand is therefore ignored; rf10 is used only because it
     * is a live register here.
     *
     * PLACEMENT IS LOAD-BEARING and the assembler CANNOT check it: the
     * SETMSF_AFTER_TLB_Z_WRITE rule exists (v3d_assembler.h:5928) but is
     * dead code in this port (first_tlb_z_write is initialised to
     * numInstructions+1 and the line that lowers it is commented out), so
     * a wrong order assembles, packs and validates clean and fails only on
     * hardware. It must sit AFTER the setmsf (a discard after the Z write
     * would re-create the very defect) and BEFORE the colour vfpacks (a TLB
     * write cannot share an instruction with another TLB op, and a
     * uniform-consuming instruction cannot sit in the thrend delay slots).
     * That leaves exactly this slot.
     *
     * The two colour vfpacks below are unchanged and still get the default
     * colour config, because the config shift register refills with 0xff
     * from the top after the word above is consumed.
     */
    "or tlbu, rf10, rf10 ; nop", // passthrough Z write; config word comes from the uniform stream

    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * ===================================================================
 * REMAINING alpha_func VARIANTS, untextured
 * ===================================================================
 *
 * The two shaders above cover GL_GEQUAL and GL_GREATER. Everything below
 * covers the other four comparison functions that need a shader
 * (GL_ALWAYS needs none -- draw.c clears the alphatest predicate for it).
 *
 * THE MECHANISM, stated once here for all 15 variants (5 funcs x 3
 * shapes; the textured and textured+smooth families repeat these same
 * discard blocks against their own texture-fetch preambles):
 *
 * FCMP computes a >=-style comparison and pushes it to the flags:
 * `fcmp.pushc -, X, Y` sets IFA true when Y >= X. Both operand orders and
 * both setmsf conditions are therefore needed to cover the four
 * inequalities, and the four combinations are exactly:
 *
 *   fcmp(rf15,rf10) IFA<=>alpha>=ref  setmsf.ifna  keep alpha>=ref  GEQUAL
 *   fcmp(rf15,rf10) IFA<=>alpha>=ref  setmsf.ifa   keep alpha< ref  LESS
 *   fcmp(rf10,rf15) IFA<=>alpha<=ref  setmsf.ifa   keep alpha> ref  GREATER
 *   fcmp(rf10,rf15) IFA<=>alpha<=ref  setmsf.ifna  keep alpha<=ref  LEQUAL
 *
 * So LESS is the GEQUAL shader with its setmsf condition inverted, and
 * LEQUAL is the GREATER shader with its setmsf condition inverted --
 * nothing else about either shader changes.
 *
 * EQUAL/NOTEQUAL cannot come from FCMP's ordering at all; they need the
 * zero flag. The ported assembler supports it -- v3d_assembler.h's
 * pf_names[] carries ".pushz"/".pushn" alongside ".pushc", and
 * v3d_qpu_flags_pack ORs the enum straight into the COND field.
 * `fcmp.pushz` is also precisely what MESA emits for nir_op_feq32/fneu32
 * (ntq_emit_comparison, nir_to_vir.c): FCMP + PUSHZ, with the not-equal
 * case inverting the resulting condition rather than emitting a subtract.
 * Operand order is irrelevant for equality, so these keep the GEQUAL
 * shader's (rf15, rf10) order for diff-ability against it.
 *
 * GL_NEVER discards unconditionally and needs no comparison -- a bare
 * `setmsf -, 0`. It deliberately still executes the `ldunifrf.rf15`
 * threshold load it does not use: draw.c writes the alpha_ref uniform for
 * every draw where alphatest/smooth_alphatest is set, and keeping the
 * uniform-consumption sequence byte-identical across the whole alphatest
 * family keeps this variant from desynchronising the fragment uniform
 * stream. One dead instruction is a cheap price for that.
 */

/* GL_LESS, untextured: GEQUAL's fcmp operand order, inverted setmsf. */
static const char* g_fragment_shader_untextured_alphatest_less_assembly[] = {
    "nop ; nop ; ldunifrf.rf7",  // rf7  = red   (uniform 0)
    "nop ; nop ; ldunifrf.rf8",  // rf8  = green (uniform 1)
    "nop ; nop ; ldunifrf.rf9",  // rf9  = blue  (uniform 2)
    "nop ; nop ; ldunifrf.rf10", // rf10 = alpha (uniform 3)
    "nop ; nop ; ldunifrf.rf15", // rf15 = alpha test threshold (uniform 4)

    "fcmp.pushc -, rf15, rf10 ; nop", // flags = FCMP(threshold, alpha); IFA true means alpha >= threshold
    "setmsf.ifa -, 0 ; nop",          // discard when alpha >= threshold; keep when alpha < threshold

    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- satisfies the >=3-instruction gap before the next thrsw

    /* Passthrough Z write: see the untextured GL_GREATER variant for the
     * full derivation. Placed after the setmsf discard and before the
     * colour vfpacks -- the only legal slot. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop", // passthrough Z write; config from uniform stream
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/* GL_LEQUAL, untextured: GREATER's fcmp operand order, inverted setmsf. */
static const char* g_fragment_shader_untextured_alphatest_lequal_assembly[] = {
    "nop ; nop ; ldunifrf.rf7",  // rf7  = red   (uniform 0)
    "nop ; nop ; ldunifrf.rf8",  // rf8  = green (uniform 1)
    "nop ; nop ; ldunifrf.rf9",  // rf9  = blue  (uniform 2)
    "nop ; nop ; ldunifrf.rf10", // rf10 = alpha (uniform 3)
    "nop ; nop ; ldunifrf.rf15", // rf15 = alpha test threshold (uniform 4)

    "fcmp.pushc -, rf10, rf15 ; nop", // flags = FCMP(alpha, threshold); IFA true means alpha <= threshold
    "setmsf.ifna -, 0 ; nop",         // discard when alpha > threshold; keep when alpha <= threshold

    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- satisfies the >=3-instruction gap before the next thrsw

    /* Passthrough Z write: see the untextured GL_GREATER variant for the
     * full derivation. Placed after the setmsf discard and before the
     * colour vfpacks -- the only legal slot. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop", // passthrough Z write; config from uniform stream
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/* GL_EQUAL, untextured: zero flag via fcmp.pushz, discard when NOT equal. */
static const char* g_fragment_shader_untextured_alphatest_equal_assembly[] = {
    "nop ; nop ; ldunifrf.rf7",  // rf7  = red   (uniform 0)
    "nop ; nop ; ldunifrf.rf8",  // rf8  = green (uniform 1)
    "nop ; nop ; ldunifrf.rf9",  // rf9  = blue  (uniform 2)
    "nop ; nop ; ldunifrf.rf10", // rf10 = alpha (uniform 3)
    "nop ; nop ; ldunifrf.rf15", // rf15 = alpha test threshold (uniform 4)

    "fcmp.pushz -, rf15, rf10 ; nop", // flags = FCMP(threshold, alpha) pushing Z; IFA true means alpha == threshold
    "setmsf.ifna -, 0 ; nop",         // discard when alpha != threshold; keep when equal

    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- satisfies the >=3-instruction gap before the next thrsw

    /* Passthrough Z write: see the untextured GL_GREATER variant for the
     * full derivation. Placed after the setmsf discard and before the
     * colour vfpacks -- the only legal slot. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop", // passthrough Z write; config from uniform stream
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/* GL_NOTEQUAL, untextured: same zero flag, opposite setmsf condition. */
static const char* g_fragment_shader_untextured_alphatest_notequal_assembly[] = {
    "nop ; nop ; ldunifrf.rf7",  // rf7  = red   (uniform 0)
    "nop ; nop ; ldunifrf.rf8",  // rf8  = green (uniform 1)
    "nop ; nop ; ldunifrf.rf9",  // rf9  = blue  (uniform 2)
    "nop ; nop ; ldunifrf.rf10", // rf10 = alpha (uniform 3)
    "nop ; nop ; ldunifrf.rf15", // rf15 = alpha test threshold (uniform 4)

    "fcmp.pushz -, rf15, rf10 ; nop", // flags = FCMP(threshold, alpha) pushing Z; IFA true means alpha == threshold
    "setmsf.ifa -, 0 ; nop",          // discard when alpha == threshold; keep when not equal

    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- satisfies the >=3-instruction gap before the next thrsw

    /* Passthrough Z write: see the untextured GL_GREATER variant for the
     * full derivation. Placed after the setmsf discard and before the
     * colour vfpacks -- the only legal slot. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop", // passthrough Z write; config from uniform stream
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/* GL_NEVER, untextured: unconditional discard, no comparison. The unused
 * ldunifrf.rf15 is deliberate -- see the family comment above. */
static const char* g_fragment_shader_untextured_alphatest_never_assembly[] = {
    "nop ; nop ; ldunifrf.rf7",  // rf7  = red   (uniform 0)
    "nop ; nop ; ldunifrf.rf8",  // rf8  = green (uniform 1)
    "nop ; nop ; ldunifrf.rf9",  // rf9  = blue  (uniform 2)
    "nop ; nop ; ldunifrf.rf10", // rf10 = alpha (uniform 3)
    "nop ; nop ; ldunifrf.rf15", // rf15 = alpha test threshold (uniform 4, deliberately unread)

    "setmsf -, 0 ; nop",         // discard every fragment, unconditionally

    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- satisfies the >=3-instruction gap before the next thrsw

    /* Passthrough Z write: see the untextured GL_GREATER variant for the
     * full derivation. Placed after the setmsf discard and before the
     * colour vfpacks -- the only legal slot. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop", // passthrough Z write; config from uniform stream
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * SOFTWARE (shader-side) blend, untextured only -- the first of a family
 * of variants that compute the blend equation in the fragment shader
 * instead of leaving it to the fixed-function blend unit. The draw path
 * does not select any of them: gl_EmitCullBlendState's fixed-function
 * blend is the single source of truth (draw.c), and these shaders are
 * assembled and uploaded but never reached.
 *
 * The shader reads the CURRENT tile buffer color itself via the QPU's
 * `ldtlb` signal (v3d_assembler.h's signal name table lists "ldtlb"/
 * "ldtlbu"; per MESA's scheduler, qpu_schedule.c's `magic_waddr_latency`,
 * it has ordinary ~1-cycle latency, NOT ldtmu's multi-cycle wait, so no
 * new thrsw-class hazard), computes the blend equation itself, and writes
 * the final combined color via the SAME plain `vfpack tlb` write every
 * other variant uses. `ldtlb` reads come back as F16-packed pairs --
 * exactly the same packing convention as vfpack's own WRITE side
 * (MESA's `vir_emit_tlb_color_write`/`vir_TLB_COLOR_READ` are mirror
 * images of each other) -- so unpacking reuses the same `sub`/`fadd`-
 * with-`.l`/`.h` trick the multitexture fragment shader uses to unpack
 * F16-packed TMU texels.
 *
 * This variant covers the classic SRC_ALPHA/ONE_MINUS_SRC_ALPHA
 * translucency mode: result = src*alpha + dst*(1-alpha), the standard
 * "alpha blend"/LERP formula. Each other factor combination needs its own
 * variant, which is what the shaders below it are.
 */
/*
 * Software-blend variant, additive (GL_ONE/GL_ONE), untextured.
 * g_fragment_shader_untextured_blend_assembly above implements the
 * SRC_ALPHA/INV_SRC_ALPHA LERP and structurally cannot express a
 * different blend equation, so each equation gets its own shader. Same
 * src-uniform-read and the same F16-unpack trick; the two ldtlb reads are
 * spaced differently here (see the ldtlb SPACING note below). The combine
 * math differs: `result = src + dst` (GL_ONE/GL_ONE is
 * literally addition, no alpha weighting), clamped to 1.0 via `fmin` --
 * hardware blend clamps the framebuffer to [0,1] automatically and this
 * software path has to do it itself, since an unclamped sum written
 * straight to `vfpack` could exceed 1.0 and produce an out-of-range F16
 * value on write-back.
 */
/*
 * SMOOTH-capable additive (GL_ONE/GL_ONE) software-blend shader. The plain
 * additive variant above reads a single FLAT color from a uniform; this
 * one reads the interpolated per-vertex color the same way
 * g_fragment_shader_untextured_smooth_assembly does (ldvary +
 * perspective-correct fmul/fadd), then runs the same additive-clamp
 * combine as that flat variant (its two ldtlb reads are issued back-to-back
 * here, not spaced as they are there). It takes the same
 * VERTEX_SMOOTH vertex shader and varying layout as the smooth-untextured
 * fragment shader -- only the FRAGMENT shader differs.
 */
/*
 * SMOOTH-capable LERP (SRC_ALPHA/ONE_MINUS_SRC_ALPHA) software-blend
 * shader: the flat/uniform-color LERP variant above cannot express a
 * per-vertex source colour. Combines the smooth-additive sibling's
 * per-vertex color read (ldvary + perspective-correct fmul/fadd) with the
 * flat LERP variant's combine math, unchanged. Takes VERTEX_SMOOTH.
 */
/*
 * Textured + flat-shaded, modulated by glColor. g_fragment_shader_assembly
 * samples the texture and packs it straight to the tile buffer with no
 * multiply against any colour, so a flat-shaded textured draw needs this
 * variant to pick up glColor the way the smooth path
 * (g_fragment_shader_textured_smooth_assembly) does per vertex. This is
 * the terminal "textured, flat-shaded, nothing else active" shader in
 * draw.c's fragment dispatch.
 *
 * A splice of two existing pieces: g_fragment_shader_textured_blend_
 * assembly's texture-fetch + 4-uniform-colour-multiply section (its first
 * ~19 instructions, through the alpha multiply) verbatim, then straight to
 * the plain vfpack/thrsw ending instead of that shader's ldtlb
 * blend-with-destination tail -- no blending here, just a lit opaque
 * texture. The four colour uniforms multiply rf7/rf8/rf9/rf10 in stream
 * order; draw.c writes fixed_color's RED bits into the word that
 * multiplies rf7 and its BLUE bits into the word that multiplies rf9,
 * matching the texture sample's own hardware channel layout rather than
 * fixed_color's logical order -- see draw.c's own comment there.
 */
static const char* g_fragment_shader_textured_colormod_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",

    "nop ; nop ; ldtmu.rf4",
    "nop ; nop ; ldtmu.rf3",


    "nop ; nop ; ldunifrf.rf5", // rf5 = colour RED multiplier (uniform 0)
    "nop ; fmul rf7, rf4.l, rf5 ; ldunifrf.rf5", // rf7 = texel.r * colour.r
    "nop ; fmul rf8, rf4.h, rf5 ; ldunifrf.rf5", // rf8 = texel.g * colour.g
    "nop ; fmul rf9, rf3.l, rf5 ; ldunifrf.rf24", // rf9 = texel.b * colour.b
    "nop ; fmul rf10, rf3.h, rf24", // rf10 = tex_alpha * color_alpha

    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * TEXTURED, FLAT, GL_BLEND. Derived from textured_colormod.
 * rf7/rf8/rf9/rf10 are red/green/blue/alpha: uniform 0 is fixed_color's byte 0,
 * which is red, and it multiplies rf7.
 */
static const char* g_fragment_shader_textured_colormod_envblend_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",

    "nop ; nop ; ldtmu.rf4",
    "nop ; nop ; ldtmu.rf3",


    /* GL_BLEND: Cv = Cp + Cs*(Cc - Cp), a lerp between the primary colour and
     * the environment colour, so the result is in [0,1] with no clamp. All
     * four Cp words are read before the three Cc words, because that is the
     * order draw.c writes them and the stream is positional. */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; nop ; ldunifrf.rf6",
    "nop ; nop ; ldunifrf.rf11",
    "nop ; nop ; ldunifrf.rf12",
    "nop ; nop ; ldunifrf.rf13",
    "nop ; nop ; ldunifrf.rf14",
    "nop ; nop ; ldunifrf.rf15",
    "fsub rf13, rf13, rf5 ; nop",
    "fsub rf14, rf14, rf6 ; nop",
    "fsub rf15, rf15, rf11 ; nop",
    "nop ; fmul r0, rf4.l, rf13",
    "nop ; fmul r1, rf4.h, rf14",
    "nop ; fmul r2, rf3.l, rf15",
    "fadd rf7, rf5, r0 ; nop",
    "fadd rf8, rf6, r1 ; nop",
    "fadd rf9, rf11, r2 ; nop",
    "nop ; fmul rf10, rf3.h, rf12",

    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};


/*
 * TEXTURED, FLAT, GL_ADD. Derived from textured_colormod.
 * rf7/rf8/rf9/rf10 are red/green/blue/alpha: uniform 0 is fixed_color's byte 0,
 * which is red, and it multiplies rf7.
 */
static const char* g_fragment_shader_textured_colormod_envadd_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",

    "nop ; nop ; ldtmu.rf4",
    "nop ; nop ; ldtmu.rf3",


    /* GL_ADD: Cv = Cp + Cs, Av = Ap * As. 1.0 is materialised FIRST so each
     * fmin sits four instructions after the fadd that fills its register. */
    "or rf11, 0x3f800000, 0x3f800000 ; nop",
    "nop ; nop ; ldunifrf.rf5",
    "fadd rf7, rf5, rf4.l ; nop ; ldunifrf.rf5",
    "fadd rf8, rf5, rf4.h ; nop ; ldunifrf.rf5",
    "fadd rf9, rf5, rf3.l ; nop ; ldunifrf.rf24",
    "nop ; fmul rf10, rf3.h, rf24",
    /* GL clamps the result, and ADD is the one mode that can exceed 1. The
     * alpha is a product of two [0,1] values and needs none. */
    "fmin rf7, rf7, rf11 ; nop",
    "fmin rf8, rf8, rf11 ; nop",
    "fmin rf9, rf9, rf11 ; nop",

    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};


/*
 * Textured + SMOOTH + blend, software (ldtlb) path -- the three-way
 * combination none of the shaders above cover. A splice of two existing
 * pieces:
 *   1. g_fragment_shader_textured_smooth_assembly's S/T reconstruction +
 *      TMU fetch + per-vertex color modulation (produces rf7/rf8/rf9/rf10
 *      = texel*vertex-color blue/green/red/alpha, vertex alpha already
 *      folded in -- no separate glColor-alpha uniform needed, unlike the
 *      flat-shaded blend shader above, which has no per-vertex color to
 *      carry it).
 *   2. g_fragment_shader_textured_blend_assembly's ldtlb dest-read +
 *      SRC_ALPHA/INV_SRC_ALPHA LERP + writeback, taken verbatim from the
 *      point where IT has rf7-rf10 = modulated color (its own
 *      texel*glColor-alpha step) -- step 1 above lands in the exact same
 *      rf7-rf10 shape, so step 2 splices on unchanged. */
/*
 * Two more src factors for this family: ONE and INVSRCALPHA. Same splice
 * technique as the rest of it -- identical front section + ldtlb
 * dest-read, only the tail math differs. Paired by REGISTER INDEX
 * throughout (rf7<->rf27, rf8<->rf28, rf9<->rf29, rf10<->rf30), the same
 * convention as the rest of the family (see v3d_shader_assembler.h's own
 * comment on these 2 enum entries for the exact formulas).
 */
/*
 * Smooth (GL_SMOOTH, per-vertex color), untextured only. A modified copy
 * of g_vertex_shader_assembly above, not an in-place edit of it, so that
 * shader stays available unchanged. Two things differ from the textured
 * vertex shader:
 *  - Input VPM offsets 3-6 read color (r,g,b,a) instead of offsets 3-4
 *    reading texcoord (s,t) -- one instruction per component instead of
 *    two, held in rf11/rf12/rf14/rf15 (free/untouched by the
 *    matrix-multiply section below, the same registers that held s/t).
 *  - Output VPM slots 4-7 write that same color straight through
 *    (unmodified passthrough, exactly like s/t's own passthrough) instead
 *    of slots 4-5 writing s/t.
 * The entire matrix-multiply core (x_s/y_s/z_s/w_s, x_p/y_p screen
 * conversion, z_sc) is IDENTICAL, byte-for-byte, to that shader --
 * copied, not re-derived, so a transcription error cannot creep into the
 * one part of this shader that is unrelated to what changes.
 *
 * The VPM segment-size fields (vertex_shader_output/input_vpm_segment_size
 * in the shader state record, draw.c) are reused unchanged from the
 * textured variant (2 and 1 respectively): this shader's raw output word
 * count (8: x_p,y_p,z_sc,1/w,r,g,b,a) still fits the capacity "2" provides
 * per MESA's own v3d compiler formula (`broadcom/compiler/vir.c`'s
 * `align(vpm_output_size, 8) / 8` and `v3d_nir_lower_io.c`'s
 * `v3d_nir_setup_vpm_layout_vs`).
 *
 * The COORDINATE shader (g_coordinate_shader_assembly above) is NOT
 * duplicated for this variant: its body only reads/writes position (VPM
 * offsets 0-2 in, slots 0-5 out: x_s/y_s/z_s/w_s/x_p/y_p), never texcoord
 * or color, and the binning pass it serves needs no per-fragment color.
 * COORDINATE_TEXTURED is reused as-is for smooth shading.
 */
static const char* g_vertex_shader_smooth_assembly[] =
{
    "or rf3, 0x3f800000, 0x3f800000 ; nop", // w_m = 1.0

    "nop ; nop ; ldunifrf.rf10", // scale_p

    /* Separate Y-axis screen-space scale -- rf16, since rf14/15 are
     * already taken by color b/a in this variant (see
     * g_vertex_shader_assembly's own comment). */
    "nop ; nop ; ldunifrf.rf16", // scale_p_y

    "ldvpmv_in rf0,  0 ; nop", // x_m
    "ldvpmv_in rf1,  1 ; nop", // y_m
    "ldvpmv_in rf2,  2 ; nop", // z_m

    /* Color (r,g,b,a), held across the matrix-multiply section exactly
     * like s/t -- these 4 registers are never touched below. */
    "ldvpmv_in rf11,  3 ; nop", // color r
    "ldvpmv_in rf12,  4 ; nop", // color g
    "ldvpmv_in rf14,  5 ; nop", // color b
    "ldvpmv_in rf15,  6 ; nop", // color a

    /* Matrix multiply -- byte-for-byte identical to g_vertex_shader_assembly. */
    "nop ; nop ; ldunif",
    "nop ; fmul rf4, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul rf5, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul rf6, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul rf7, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf7, rf7, r0 ; nop",

    "or recip, rf7, rf7 ; nop",
    "nop ; nop",
    "nop ; fmul r0, rf4, r4",
    "nop ; fmul r0, r0, rf10",
    "nop ; fmul rf8, r0, 0x43000000",
    "fsub r0, r0, r0 ; nop",
    "fsub r0, r0, rf5 ; nop",
    "nop ; fmul r0, r0, r4",
    "nop ; fmul r0, r0, rf16", // scale_p_y, not the shared scale_p
    "nop ; fmul rf9, r0, 0x43000000",
    "ftoin rf8, rf8 ; nop",
    "ftoin rf9, rf9 ; nop",
    "nop ; fmul rf13, rf6, r4",
    /* The Z scale and offset come from the uniform stream
     * (v3d_my_uniforms.z_scale / .z_offset, draw.c), APPENDED after the 16
     * matrix values -- so these two reads must stay the LAST ldunif* in this
     * shader. rf0/rf1 held x_m/y_m and are dead from the end of the matrix
     * multiply above to the end of the shader, so no new register is needed.
     * Two instructions of slack before first use; the matrix multiply above
     * proves one is enough. */
    "nop ; nop ; ldunifrf.rf0",          // viewport z scale  (context->sz)
    "nop ; nop ; ldunifrf.rf1",          // viewport z offset (context->az)
    "nop ; fmul rf13, rf13, rf0",
    "fadd rf13, rf13, rf1 ; nop",

    "stvpmv 0, rf8 ; nop",
    "stvpmv 1, rf9 ; nop",
    "stvpmv 2, rf13 ; nop",
    "stvpmv 3, r4 ; nop",
    "stvpmv 4, rf11 ; nop", // color r
    "stvpmv 5, rf12 ; nop", // color g
    "stvpmv 6, rf14 ; nop", // color b
    "stvpmv 7, rf15 ; nop", // color a

    "vpmwt -              ; nop",
    "nop                  ; nop ; thrsw",
    "nop                  ; nop",
    "nop                  ; nop",
};

/*
 * Fragment side of smooth/untextured shading. Reads 4 varyings (r,g,b,a,
 * written by g_vertex_shader_smooth_assembly above at VPM slots 4-7) using
 * the SAME per-varying perspective-correction reconstruction
 * g_fragment_shader_assembly uses for s/t (ldvary -> fmul by
 * rf0/payload_w -> fadd with r5/reconstruction constant) -- just done 4
 * times, sequentially, with no overlap/pipelining between them (unlike
 * that textured shader's s/t reconstruction, which pipelines the last
 * instruction of one varying with the first of the next). The simpler,
 * unpipelined shape is deliberate.
 *
 * No TMU access anywhere in this shader (no texture, so nothing to fetch),
 * so it carries the thrsw protocol explicitly: two consecutive thrsw
 * (last-thread signal), then a >=3-instruction gap, then one final
 * thread-end thrsw at the tlb write -- structurally identical to
 * g_fragment_shader_untextured_assembly above, just with 12 varying-
 * reconstruction instructions in place of 4 ldunifrf loads.
 */
static const char* g_fragment_shader_untextured_smooth_assembly[] = {
    "nop ; nop ; ldvary.r0",   // load r/w
    "nop ; fmul r1, r0, rf0",  // r1 = (r/w) * w
    "fadd rf7, r1, r5 ; nop", // rf7 = true red

    "nop ; nop ; ldvary.r0",   // load g/w
    "nop ; fmul r1, r0, rf0",  // r1 = (g/w) * w
    "fadd rf8, r1, r5 ; nop", // rf8 = true green

    "nop ; nop ; ldvary.r0",   // load b/w
    "nop ; fmul r1, r0, rf0",  // r1 = (b/w) * w
    "fadd rf9, r1, r5 ; nop", // rf9 = true blue

    "nop ; nop ; ldvary.r0",   // load a/w
    "nop ; fmul r1, r0, rf0",  // r1 = (a/w) * w
    "fadd rf10, r1, r5 ; nop", // rf10 = true alpha

    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- satisfies the >=3-instruction gap before the next thrsw (pair at instr 13, final at 16: 16-13=3)

    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * Combined GL_MODULATE vertex shader -- texture sampled, then multiplied
 * by per-vertex smooth color, both in one draw call.
 *
 * A modified copy of g_vertex_shader_assembly, again a COPY rather than an
 * in-place edit. Reads BOTH texcoord (s,t, VPM input offsets 3-4, held in
 * rf11/rf12 exactly like that shader) AND color (r,g,b,a, input offsets
 * 5-8, held in rf14/rf15/rf16/rf17 -- free/untouched by the
 * matrix-multiply core, same as every other variant's held-register
 * choice). Output VPM slots 4-5 write s/t, slots 6-9 write r/g/b/a --
 * straight passthrough, matching every other varying's treatment. The
 * matrix-multiply core itself is copied verbatim.
 *
 * VPM SEGMENT SIZES: this shader's raw INPUT word count is 9 (3 position +
 * 2 texcoord + 4 color), the only variant here to exceed 8 words of
 * per-vertex input; every other one (5 words for textured, 7 for smooth)
 * fits a single 8-word VPM sector, so `vertex_shader_input_vpm_segment_
 * size` can stay at 1 for them. Per MESA's sizing formula
 * (`broadcom/compiler/vir.c`: `align(word_count, 8) / 8`), 9 words needs
 * TWO sectors, so draw.c's gl_EmitPrimitiveV3D sets
 * `vertex_shader_input_vpm_segment_size = 2` for this variant (see that
 * function's own comment). The output word count (10: 4 fixed + 6 varying)
 * needs exactly 2 sectors by the same formula, which is what every other
 * variant's OUTPUT segment size already is, so only the input changes.
 * Unlike the others, this shader's segment sizes are exactly what the
 * formula requires rather than comfortably above it.
 */
static const char* g_vertex_shader_smooth_textured_assembly[] =
{
    "or rf3, 0x3f800000, 0x3f800000 ; nop", // w_m = 1.0

    "nop ; nop ; ldunifrf.rf10", // scale_p

    /* Separate Y-axis screen-space scale -- rf18, since rf14-17 are
     * already taken by color r/g/b/a in this variant. */
    "nop ; nop ; ldunifrf.rf18", // scale_p_y

    "ldvpmv_in rf0,  0 ; nop", // x_m
    "ldvpmv_in rf1,  1 ; nop", // y_m
    "ldvpmv_in rf2,  2 ; nop", // z_m

    "ldvpmv_in rf11,  3 ; nop", // s
    "ldvpmv_in rf12,  4 ; nop", // t
    "ldvpmv_in rf14,  5 ; nop", // color r
    "ldvpmv_in rf15,  6 ; nop", // color g
    "ldvpmv_in rf16,  7 ; nop", // color b
    "ldvpmv_in rf17,  8 ; nop", // color a

    /* Matrix multiply -- byte-for-byte identical to g_vertex_shader_assembly. */
    "nop ; nop ; ldunif",
    "nop ; fmul rf4, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul rf5, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul rf6, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul rf7, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf7, rf7, r0 ; nop",

    "or recip, rf7, rf7 ; nop",
    "nop ; nop",
    "nop ; fmul r0, rf4, r4",
    "nop ; fmul r0, r0, rf10",
    "nop ; fmul rf8, r0, 0x43000000",
    "fsub r0, r0, r0 ; nop",
    "fsub r0, r0, rf5 ; nop",
    "nop ; fmul r0, r0, r4",
    "nop ; fmul r0, r0, rf18", // scale_p_y, not the shared scale_p
    "nop ; fmul rf9, r0, 0x43000000",
    "ftoin rf8, rf8 ; nop",
    "ftoin rf9, rf9 ; nop",
    "nop ; fmul rf13, rf6, r4",
    /* The Z scale and offset come from the uniform stream
     * (v3d_my_uniforms.z_scale / .z_offset, draw.c), APPENDED after the 16
     * matrix values -- so these two reads must stay the LAST ldunif* in this
     * shader. rf0/rf1 held x_m/y_m and are dead from the end of the matrix
     * multiply above to the end of the shader, so no new register is needed.
     * Two instructions of slack before first use; the matrix multiply above
     * proves one is enough. */
    "nop ; nop ; ldunifrf.rf0",          // viewport z scale  (context->sz)
    "nop ; nop ; ldunifrf.rf1",          // viewport z offset (context->az)
    "nop ; fmul rf13, rf13, rf0",
    "fadd rf13, rf13, rf1 ; nop",

    "stvpmv 0, rf8 ; nop",
    "stvpmv 1, rf9 ; nop",
    "stvpmv 2, rf13 ; nop",
    "stvpmv 3, r4 ; nop",
    "stvpmv 4, rf11 ; nop", // s
    "stvpmv 5, rf12 ; nop", // t
    "stvpmv 6, rf14 ; nop", // color r
    "stvpmv 7, rf15 ; nop", // color g
    "stvpmv 8, rf16 ; nop", // color b
    "stvpmv 9, rf17 ; nop", // color a

    "vpmwt -              ; nop",
    "nop                  ; nop ; thrsw",
    "nop                  ; nop",
    "nop                  ; nop",
};

/*
 * Combined (smooth+textured, GL_MODULATE) vertex shader for draws that
 * carry a real per-vertex w (draw.c's needs_real_w: the use_clip_space
 * path, and any combined or smooth_alphatest draw whose vertices have
 * w != 1) -- a COPY of g_vertex_shader_smooth_textured_
 * assembly above. Two changes only:
 *   1. w_m read as a REAL 4th VPM input word (offset 3) instead of
 *      hardcoded 1.0 -- same fix, same reasoning, as
 *      g_coordinate_shader_clipspace_assembly above.
 *   2. Every subsequent input's VPM offset shifted by +1 (s/t: 3,4 -> 4,5;
 *      color r/g/b/a: 5,6,7,8 -> 6,7,8,9) to make room for the real w_m at
 *      offset 3 -- position now occupies offsets 0-3 (4 words), not 0-2.
 * The matrix-multiply core and every output slot are byte-for-byte
 * unchanged from the original -- with the identity matrix draw.c's
 * use_clip_space path feeds, a real w_in here survives to w_out (rf7) and
 * therefore to `recip = 1/w_s` (r4, stvpmv slot 3), so the perspective
 * divide is correct for clipped, perspective-projected primitives.
 *
 * Input word count is 10 (4 position + 2 texcoord + 4 color), one more
 * than the original's 9 -- per MESA's own sizing formula
 * (align(word_count,8)/8, see the original shader's own comment),
 * ceil(10/8)=2 sectors, the same as ceil(9/8)=2, so
 * vertex_shader_input_vpm_segment_size is unchanged from that variant.
 * Output word count/layout is unchanged too (still 4 fixed + 6 varying),
 * so the output segment size is unaffected.
 */
static const char* g_vertex_shader_smooth_textured_clipspace_assembly[] =
{
    "ldvpmv_in rf3, 3 ; nop", // w_m -- REAL clip-space w, not hardcoded 1.0

    "nop ; nop ; ldunifrf.rf10", // scale_p

    "nop ; nop ; ldunifrf.rf18", // scale_p_y

    "ldvpmv_in rf0,  0 ; nop", // x_m
    "ldvpmv_in rf1,  1 ; nop", // y_m
    "ldvpmv_in rf2,  2 ; nop", // z_m

    "ldvpmv_in rf11,  4 ; nop", // s (shifted: offset 3 in the original)
    "ldvpmv_in rf12,  5 ; nop", // t (shifted: offset 4 in the original)
    "ldvpmv_in rf14,  6 ; nop", // color r (shifted: offset 5 in the original)
    "ldvpmv_in rf15,  7 ; nop", // color g (shifted: offset 6 in the original)
    "ldvpmv_in rf16,  8 ; nop", // color b (shifted: offset 7 in the original)
    "ldvpmv_in rf17,  9 ; nop", // color a (shifted: offset 8 in the original)

    /* Matrix multiply -- byte-for-byte identical to g_vertex_shader_smooth_textured_assembly. */
    "nop ; nop ; ldunif",
    "nop ; fmul rf4, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul rf5, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul rf6, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul rf7, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf7, rf7, r0 ; nop",

    "or recip, rf7, rf7 ; nop",
    "nop ; nop",
    "nop ; fmul r0, rf4, r4",
    "nop ; fmul r0, r0, rf10",
    "nop ; fmul rf8, r0, 0x43000000",
    "fsub r0, r0, r0 ; nop",
    "fsub r0, r0, rf5 ; nop",
    "nop ; fmul r0, r0, r4",
    "nop ; fmul r0, r0, rf18",
    "nop ; fmul rf9, r0, 0x43000000",
    "ftoin rf8, rf8 ; nop",
    "ftoin rf9, rf9 ; nop",
    "nop ; fmul rf13, rf6, r4",
    /* The Z scale and offset come from the uniform stream
     * (v3d_my_uniforms.z_scale / .z_offset, draw.c), APPENDED after the 16
     * matrix values -- so these two reads must stay the LAST ldunif* in this
     * shader. rf0/rf1 held x_m/y_m and are dead from the end of the matrix
     * multiply above to the end of the shader, so no new register is needed.
     * Two instructions of slack before first use; the matrix multiply above
     * proves one is enough. */
    "nop ; nop ; ldunifrf.rf0",          // viewport z scale  (context->sz)
    "nop ; nop ; ldunifrf.rf1",          // viewport z offset (context->az)
    "nop ; fmul rf13, rf13, rf0",
    "fadd rf13, rf13, rf1 ; nop",

    "stvpmv 0, rf8 ; nop",
    "stvpmv 1, rf9 ; nop",
    "stvpmv 2, rf13 ; nop",
    "stvpmv 3, r4 ; nop",
    "stvpmv 4, rf11 ; nop", // s
    "stvpmv 5, rf12 ; nop", // t
    "stvpmv 6, rf14 ; nop", // color r
    "stvpmv 7, rf15 ; nop", // color g
    "stvpmv 8, rf16 ; nop", // color b
    "stvpmv 9, rf17 ; nop", // color a

    "vpmwt -              ; nop",
    "nop                  ; nop ; thrsw",
    "nop                  ; nop",
    "nop                  ; nop",
};

/*
 * Multitexture -- vertex side. Same shape as
 * g_vertex_shader_smooth_textured_assembly just above (the established
 * "4 extra input words, 4 extra output varyings" pattern, copied rather
 * than re-derived): reads a SECOND texcoord pair (s1,t1, VPM input offsets
 * 5-6) alongside s0,t0 (3-4), using rf14/rf15 -- free/untouched by the
 * matrix-multiply core, the same registers the smooth-textured shader's
 * color passthrough uses for a different purpose. Passes all 4 straight
 * through unmodified (no color, no w needed -- MAX_TEXUNIT == 2 gives two
 * independent texcoord pairs, vertexbuffer.h). Input word count (3
 * position + 4 texcoord = 7) and output word count (4 position/w + 4
 * texcoord = 8) both fit within one 8-word VPM segment, so
 * vertex_shader_input/output_vpm_segment_size stay at 1 and 2 -- unlike
 * the smooth-textured shader's 9-word input, which needs the input size
 * bumped to 2. The COORDINATE shader is reused unchanged
 * (COORDINATE_TEXTURED), on the same "never touches texcoord/color
 * varyings" reasoning as every other variant.
 */
static const char* g_vertex_shader_multitexture_assembly[] =
{
    "or rf3, 0x3f800000, 0x3f800000 ; nop", // w_m = 1.0

    "nop ; nop ; ldunifrf.rf10", // scale_p

    /* Separate Y-axis screen-space scale -- rf16, since rf14/15 are
     * already taken by s1/t1 in this variant. */
    "nop ; nop ; ldunifrf.rf16", // scale_p_y

    "ldvpmv_in rf0,  0 ; nop", // x_m
    "ldvpmv_in rf1,  1 ; nop", // y_m
    "ldvpmv_in rf2,  2 ; nop", // z_m

    "ldvpmv_in rf11,  3 ; nop", // s0
    "ldvpmv_in rf12,  4 ; nop", // t0
    "ldvpmv_in rf14,  5 ; nop", // s1
    "ldvpmv_in rf15,  6 ; nop", // t1

    /* Matrix multiply -- byte-for-byte identical to g_vertex_shader_assembly. */
    "nop ; nop ; ldunif",
    "nop ; fmul rf4, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul rf5, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul rf6, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul rf7, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf7, rf7, r0 ; nop",

    "or recip, rf7, rf7 ; nop",
    "nop ; nop",
    "nop ; fmul r0, rf4, r4",
    "nop ; fmul r0, r0, rf10",
    "nop ; fmul rf8, r0, 0x43000000",
    "fsub r0, r0, r0 ; nop",
    "fsub r0, r0, rf5 ; nop",
    "nop ; fmul r0, r0, r4",
    "nop ; fmul r0, r0, rf16", // scale_p_y, not the shared scale_p
    "nop ; fmul rf9, r0, 0x43000000",
    "ftoin rf8, rf8 ; nop",
    "ftoin rf9, rf9 ; nop",
    "nop ; fmul rf13, rf6, r4",
    /* The Z scale and offset come from the uniform stream
     * (v3d_my_uniforms.z_scale / .z_offset, draw.c), APPENDED after the 16
     * matrix values -- so these two reads must stay the LAST ldunif* in this
     * shader. rf0/rf1 held x_m/y_m and are dead from the end of the matrix
     * multiply above to the end of the shader, so no new register is needed.
     * Two instructions of slack before first use; the matrix multiply above
     * proves one is enough. */
    "nop ; nop ; ldunifrf.rf0",          // viewport z scale  (context->sz)
    "nop ; nop ; ldunifrf.rf1",          // viewport z offset (context->az)
    "nop ; fmul rf13, rf13, rf0",
    "fadd rf13, rf13, rf1 ; nop",

    "stvpmv 0, rf8 ; nop",
    "stvpmv 1, rf9 ; nop",
    "stvpmv 2, rf13 ; nop",
    "stvpmv 3, r4 ; nop",
    "stvpmv 4, rf11 ; nop", // s0
    "stvpmv 5, rf12 ; nop", // t0
    "stvpmv 6, rf14 ; nop", // s1
    "stvpmv 7, rf15 ; nop", // t1

    "vpmwt -              ; nop",
    "nop                  ; nop ; thrsw",
    "nop                  ; nop",
    "nop                  ; nop",
};

/*
 * Clip-space (real per-vertex w) counterpart to
 * g_vertex_shader_multitexture_assembly just above -- same change as
 * g_vertex_shader_smooth_textured_clipspace_assembly (see that array's own
 * comment for the Sutherland-Hodgeman/perspective-w rationale). Only 2
 * changes from the original: (1) w_m read as a REAL 4th VPM input word
 * (offset 3) instead of hardcoded 1.0; (2) s0/t0/s1/t1 VPM offsets shifted
 * by +1 (3,4,5,6 -> 4,5,6,7) to make room. Takes the SAME
 * g_coordinate_shader_clipspace_assembly as the smooth-textured clipspace
 * variant (position-only, generic across every variant, unchanged). Input
 * word count becomes 8 (4 position + 4 texcoord), output stays 8 (4
 * position/w + 4 texcoord) -- both still fit one 8-word VPM segment, so
 * vertex_shader_input/output_vpm_segment_size stay at 1/2, the same as the
 * original multitexture variant.
 */
static const char* g_vertex_shader_multitexture_clipspace_assembly[] =
{
    "ldvpmv_in rf3, 3 ; nop", // w_m -- REAL clip-space w, not hardcoded 1.0

    "nop ; nop ; ldunifrf.rf10", // scale_p

    "nop ; nop ; ldunifrf.rf16", // scale_p_y

    "ldvpmv_in rf0,  0 ; nop", // x_m
    "ldvpmv_in rf1,  1 ; nop", // y_m
    "ldvpmv_in rf2,  2 ; nop", // z_m

    "ldvpmv_in rf11,  4 ; nop", // s0 (shifted: offset 3 in the original)
    "ldvpmv_in rf12,  5 ; nop", // t0 (shifted: offset 4 in the original)
    "ldvpmv_in rf14,  6 ; nop", // s1 (shifted: offset 5 in the original)
    "ldvpmv_in rf15,  7 ; nop", // t1 (shifted: offset 6 in the original)

    /* Matrix multiply -- byte-for-byte identical to g_vertex_shader_multitexture_assembly. */
    "nop ; nop ; ldunif",
    "nop ; fmul rf4, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul rf5, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul rf6, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul rf7, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf7, rf7, r0 ; nop",

    "or recip, rf7, rf7 ; nop",
    "nop ; nop",
    "nop ; fmul r0, rf4, r4",
    "nop ; fmul r0, r0, rf10",
    "nop ; fmul rf8, r0, 0x43000000",
    "fsub r0, r0, r0 ; nop",
    "fsub r0, r0, rf5 ; nop",
    "nop ; fmul r0, r0, r4",
    "nop ; fmul r0, r0, rf16",
    "nop ; fmul rf9, r0, 0x43000000",
    "ftoin rf8, rf8 ; nop",
    "ftoin rf9, rf9 ; nop",
    "nop ; fmul rf13, rf6, r4",
    /* The Z scale and offset come from the uniform stream
     * (v3d_my_uniforms.z_scale / .z_offset, draw.c), APPENDED after the 16
     * matrix values -- so these two reads must stay the LAST ldunif* in this
     * shader. rf0/rf1 held x_m/y_m and are dead from the end of the matrix
     * multiply above to the end of the shader, so no new register is needed.
     * Two instructions of slack before first use; the matrix multiply above
     * proves one is enough. */
    "nop ; nop ; ldunifrf.rf0",          // viewport z scale  (context->sz)
    "nop ; nop ; ldunifrf.rf1",          // viewport z offset (context->az)
    "nop ; fmul rf13, rf13, rf0",
    "fadd rf13, rf13, rf1 ; nop",

    "stvpmv 0, rf8 ; nop",
    "stvpmv 1, rf9 ; nop",
    "stvpmv 2, rf13 ; nop",
    "stvpmv 3, r4 ; nop",
    "stvpmv 4, rf11 ; nop", // s0
    "stvpmv 5, rf12 ; nop", // t0
    "stvpmv 6, rf14 ; nop", // s1
    "stvpmv 7, rf15 ; nop", // t1

    "vpmwt -              ; nop",
    "nop                  ; nop ; thrsw",
    "nop                  ; nop",
    "nop                  ; nop",
};

/*
 * Multitexture -- fragment side. Two TMU fetches (one per texture unit) in
 * ONE fragment shader invocation; every other shader here does at most one
 * TMU round-trip.
 *
 * The sequencing follows MESA's compiler: `broadcom/compiler/nir_to_vir.c`'s
 * `vir_emit_thrsw()` (only a SINGLE, non-doubled `thrsw` per texture
 * operation -- the doubled "last thread switch" pair applies only to
 * whichever one turns out to be truly last) and `ntq_flush_tmu()` ("Emits
 * the thread switch and LDTMU/TMUWT for ALL OUTSTANDING TMU operations",
 * i.e. multiple queued texture requests are batched behind ONE thrsw wait
 * and the results read back with multiple sequential `LDTMU` calls in FIFO
 * issue order, not a wait-per-fetch pattern). Applied here: both fetches
 * are ISSUED back-to-back (unit 0's tmut/tmus trigger, then unit 1's) with
 * NO wait in between; only unit 1's trigger gets the doubled thrsw pair,
 * since it is the shader's only/last TMU-wait point; all 4 result words
 * (2 per fetch) are then read via 4 sequential `ldtmu` calls in the SAME
 * order the fetches were issued. That also satisfies this file's thrsw
 * protocol: 2 consecutive + >=3-instruction gap + one more thrsw at the
 * tlb write.
 *
 * s0/t0 reconstruction + unit-0 TMU config (2 `wrtmuc` calls, consuming
 * the first 2 fragment uniforms) is BYTE-FOR-BYTE the sequence from
 * g_fragment_shader_assembly, copied verbatim. s1/t1 reconstruction +
 * unit-1 TMU config (2 MORE `wrtmuc` calls, consuming 2 MORE fragment
 * uniforms appended after unit 0's in the same per-draw-call uniform
 * buffer) is the exact same sequence again, just with fresh scratch
 * registers (rf16/rf17 for s1/t1, unused anywhere else in this shader) and
 * a second, independent texture/sampler state -- draw.c must emit texture
 * state twice through `v3d_texture_emit_state`, once per bound unit, and
 * build two TMU config uniform pairs.
 *
 * The sub/fadd unpack is done for BOTH texels independently into separate
 * registers (rf7-10 for texel0, rf20-23 for texel1), then combined via
 * GL_MODULATE's literal definition -- all four channels multiplied
 * together (matching `V3D_TEXENV_MODULATE`, the default `texenv_mode`
 * every new texture object gets, texture.c), not just RGB.
 */
static const char* g_fragment_shader_multitexture_assembly[] = {
    /* Unit 0: s0/t0 reconstruction + TMU config, byte-for-byte the same
     * sequence -- issues the fetch but does NOT wait yet. */
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop",
    "or tmus, rf6, rf6 ; nop", /* triggers unit 0's fetch, queued */

    /* Unit 1: s1/t1 reconstruction + TMU config -- same sequence again,
     * fresh registers. The doubled thrsw pair goes on THIS fetch's
     * trigger since it's the shader's last TMU-wait point. */
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf17, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf16, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf16, rf16 ; nop ; thrsw", /* last-thrsw signal, part 1 of 2 */
    "nop ; nop ; thrsw",                  /* last-thrsw signal, part 2 of 2 */
    "or tmus, rf17, rf17 ; nop",          /* triggers unit 1's fetch, queued */

    /* Read both fetches' results, FIFO issue order: unit 0 first, then
     * unit 1. */
    "nop ; nop ; ldtmu.rf4",  // unit0 blue_green
    "nop ; nop ; ldtmu.rf3",  // unit0 red_alpha
    "nop ; nop ; ldtmu.rf19", // unit1 blue_green
    "nop ; nop ; ldtmu.rf18", // unit1 red_alpha

    /* Unpack unit 0's texel (the sub/fadd trick). */

    /* Unpack unit 1's texel. */
    "sub rf20, rf20, rf20 ; nop",
    "sub rf21, rf21, rf21 ; nop",
    "sub rf22, rf22, rf22 ; nop",
    "sub rf23, rf23, rf23 ; nop",
    "fadd rf20, rf20, rf19.l ; nop", // unit1 blue
    "fadd rf21, rf21, rf19.h ; nop", // unit1 green
    "fadd rf22, rf22, rf18.l ; nop", // unit1 red
    "fadd rf23, rf23, rf18.h ; nop", // unit1 alpha

    /* Combine: multiply all 4 channels (GL_MODULATE). */
    "nop ; fmul rf7, rf4.l, rf20",
    "nop ; fmul rf8, rf4.h, rf21",
    "nop ; fmul rf9, rf3.l, rf22",
    "nop ; fmul rf10, rf3.h, rf23",

    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // final thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * LIT, MULTITEXTURED, GL_MODULATE on unit 1. The unlit variant computes
 * texel0 x texel1 and reads no colour at all; this one reads the lit colour as
 * four varyings and multiplies it in, which is GL's rule -- unit 0 modulates
 * its texel with the PRIMARY colour, and under lighting that colour is the lit
 * one.
 *
 * GL_REPLACE needs no lit variant: texel1 replaces everything, so the primary
 * colour is correctly dropped, and that shader reads only the four texcoord
 * varyings with the colour sitting after them unread -- so it pairs with the
 * lit vertex shader as it is.
 */
static const char* g_fragment_shader_lit_multitexture_assembly[] = {
    /* Unit 0: s0/t0 reconstruction + TMU config, byte-for-byte the same
     * sequence -- issues the fetch but does NOT wait yet. */
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop",
    "or tmus, rf6, rf6 ; nop", /* triggers unit 0's fetch, queued */

    /* Unit 1: s1/t1 reconstruction + TMU config -- same sequence again,
     * fresh registers. The doubled thrsw pair goes on THIS fetch's
     * trigger since it's the shader's last TMU-wait point. */
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf17, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf16, r1, r5 ; nop",
    /* THE LIT COLOUR, four varyings. ldvary is sequential and the
     * vertex shader emits the colour after both texcoord pairs, so
     * these follow on. Read here, with the other varying reads and
     * before the last-thrsw pair. */
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf24, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf25, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf26, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf27, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf16, rf16 ; nop ; thrsw", /* last-thrsw signal, part 1 of 2 */
    "nop ; nop ; thrsw",                  /* last-thrsw signal, part 2 of 2 */
    "or tmus, rf17, rf17 ; nop",          /* triggers unit 1's fetch, queued */

    /* Read both fetches' results, FIFO issue order: unit 0 first, then
     * unit 1. */
    "nop ; nop ; ldtmu.rf4",  // unit0 blue_green
    "nop ; nop ; ldtmu.rf3",  // unit0 red_alpha
    "nop ; nop ; ldtmu.rf19", // unit1 blue_green
    "nop ; nop ; ldtmu.rf18", // unit1 red_alpha

    /* Unpack unit 0's texel (the sub/fadd trick). */

    /* Unpack unit 1's texel. */
    "sub rf20, rf20, rf20 ; nop",
    "sub rf21, rf21, rf21 ; nop",
    "sub rf22, rf22, rf22 ; nop",
    "sub rf23, rf23, rf23 ; nop",
    "fadd rf20, rf20, rf19.l ; nop", // unit1 blue
    "fadd rf21, rf21, rf19.h ; nop", // unit1 green
    "fadd rf22, rf22, rf18.l ; nop", // unit1 red
    "fadd rf23, rf23, rf18.h ; nop", // unit1 alpha

    /* Combine: multiply all 4 channels (GL_MODULATE). */
    "nop ; fmul r0, rf4.l, rf20",
    "nop ; fmul rf7, r0, rf24",
    "nop ; fmul r0, rf4.h, rf21",
    "nop ; fmul rf8, r0, rf25",
    "nop ; fmul r0, rf3.l, rf22",
    "nop ; fmul rf9, r0, rf26",
    "nop ; fmul r0, rf3.h, rf23",
    "nop ; fmul rf10, r0, rf27",

    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // final thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};


/*
 * GL_DECAL for multitexture. Byte-for-byte identical fetch/unpack section
 * to g_fragment_shader_multitexture_assembly above (copied, not
 * re-derived). Only the "Combine" step differs -- GL_DECAL semantics for
 * 2-stage texturing: stage1 (unit 1) decals onto stage0's result (unit 0
 * alone, since the multitexture vertex shader has no per-vertex color
 * input to serve as a "previous stage" seed -- see its own comment):
 * result.rgb = LERP(unit0.rgb, unit1.rgb, unit1.alpha), result.a =
 * unit0.alpha unchanged (GL_DECAL preserves the incoming alpha and only
 * replaces colour). The LERP uses the standard a+t*(b-a) identity. rf10
 * (alpha) is deliberately left untouched by this block, so the shared
 * output section still reads a correct value.
 */
static const char* g_fragment_shader_multitexture_decal_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop",
    "or tmus, rf6, rf6 ; nop",

    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf17, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf16, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf16, rf16 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf17, rf17 ; nop",

    "nop ; nop ; ldtmu.rf4",
    "nop ; nop ; ldtmu.rf3",
    "nop ; nop ; ldtmu.rf19",
    "nop ; nop ; ldtmu.rf18",

    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",
    "fadd rf8, rf8, rf4.h ; nop",
    "fadd rf9, rf9, rf3.l ; nop",
    "fadd rf10, rf10, rf3.h ; nop",

    "sub rf20, rf20, rf20 ; nop",
    "sub rf21, rf21, rf21 ; nop",
    "sub rf22, rf22, rf22 ; nop",
    "sub rf23, rf23, rf23 ; nop",
    "fadd rf20, rf20, rf19.l ; nop",
    "fadd rf21, rf21, rf19.h ; nop",
    "fadd rf22, rf22, rf18.l ; nop",
    "fadd rf23, rf23, rf18.h ; nop",

    /* Combine: GL_DECAL -- result.rgb = unit0 + unit1.alpha*(unit1-unit0), result.a = unit0.alpha (rf10, untouched). */
    "fsub r0, rf20, rf7 ; nop",  // r0 = unit1_blue - unit0_blue
    "nop ; fmul r0, r0, rf23",    // r0 *= unit1_alpha
    "fadd rf7, rf7, r0 ; nop",    // rf7 = result_blue

    "fsub r0, rf21, rf8 ; nop",
    "nop ; fmul r0, r0, rf23",
    "fadd rf8, rf8, r0 ; nop",

    "fsub r0, rf22, rf9 ; nop",
    "nop ; fmul r0, r0, rf23",
    "fadd rf9, rf9, r0 ; nop",

    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * GL_REPLACE for multitexture -- stage1 (unit 1) discards stage0's result
 * entirely and outputs unit 1's own texel unchanged (GL_REPLACE semantics:
 * "texture replaces the incoming fragment", no dependence on unit 0 at
 * all). Unit 0's fetch still happens -- draw.c configures both TMUs
 * identically regardless of env mode, which avoids per-variant CPU-side
 * branching -- but its result is never read past the unpack step: the
 * "Combine" step here is a plain register copy (`or rfX, rfY, rfY`, this
 * file's copy/passthrough idiom) from unit 1's unpacked texel (rf20-23)
 * into rf7-10, so the SAME shared output section below works unchanged
 * whichever combine mode ran.
 */
static const char* g_fragment_shader_multitexture_replace_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop",
    "or tmus, rf6, rf6 ; nop",

    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf17, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf16, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf16, rf16 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf17, rf17 ; nop",

    "nop ; nop ; ldtmu.rf4",
    "nop ; nop ; ldtmu.rf3",
    "nop ; nop ; ldtmu.rf19",
    "nop ; nop ; ldtmu.rf18",

    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",
    "fadd rf8, rf8, rf4.h ; nop",
    "fadd rf9, rf9, rf3.l ; nop",
    "fadd rf10, rf10, rf3.h ; nop",

    "sub rf20, rf20, rf20 ; nop",
    "sub rf21, rf21, rf21 ; nop",
    "sub rf22, rf22, rf22 ; nop",
    "sub rf23, rf23, rf23 ; nop",
    "fadd rf20, rf20, rf19.l ; nop",
    "fadd rf21, rf21, rf19.h ; nop",
    "fadd rf22, rf22, rf18.l ; nop",
    "fadd rf23, rf23, rf18.h ; nop",

    /* Combine: GL_REPLACE -- unit 0's texel (rf7-10) is discarded, replaced with unit 1's (rf20-23). */
    "or rf7, rf20, rf20 ; nop",
    "or rf8, rf21, rf21 ; nop",
    "or rf9, rf22, rf22 ; nop",
    "or rf10, rf23, rf23 ; nop",

    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * Blend-aware counterparts of the 3 combine variants above, for
 * mglDrawMultitexBuffer(GL_ONE, GL_SRC_COLOR|GL_SRC_ALPHA, ...) -- an
 * ADDITIVE blend, result = src*ONE + dst*dstFactor, NOT the translucency
 * LERP g_fragment_shader_untextured_blend_assembly implements (a different
 * blend equation, not a reuse). Same `ldtlb` tile-buffer-read mechanism
 * (that shader's own comment covers the hardware rationale, not re-derived
 * here), reading the CURRENT tile color as "dst" after this shader's 2-TMU
 * combine has produced "src" in rf7-10. draw.c keeps multitex_blend
 * permanently GL_FALSE, so no draw selects these three.
 *
 * dstFactor is selected PER-CHANNEL via a runtime uniform (rf24, 0.0 for
 * GL_SRC_COLOR / 1.0 for GL_SRC_ALPHA) rather than two separate hardcoded
 * shader variants per combine mode -- collapses what would otherwise be
 * 6 shader variants (3 combine modes x 2 dst factors) down to 3
 * (1 uniform-selectable dst factor x 3 combine modes); draw.c feeds the
 * flag matching backend->blend_dstmode every draw call, the same
 * uniform-rather-than-hardcoded treatment as alpha_ref/fog. For the ALPHA
 * channel specifically, GL_SRC_COLOR's and GL_SRC_ALPHA's factors are
 * IDENTICAL (both reduce to src.alpha for that one channel) -- computed
 * directly, no lerp needed, and computed LAST since it consumes the
 * ORIGINAL (pre-blend) rf10 that blue/green/red's own lerp also depend
 * on (ordering matters -- overwriting rf10 first would corrupt their
 * inputs).
 *
 * ldtlb's channel-packing convention (g_fragment_shader_untextured_blend_
 * assembly: first ldtlb=packed(r,g), second=packed(b,a))
 * is DIFFERENT from this shader's own ldtmu-derived texel convention
 * (packed blue_green then red_alpha) -- deliberately NOT conflated,
 * unpacked into separately-named dst_red/green/blue/alpha registers and
 * explicitly paired against the matching-named src channel below.
 */
/*
 * Fragment side of combined GL_MODULATE. The S/T-reconstruction-and-TMU-
 * fetch section (up through the two `ldtmu` loads) is BYTE-FOR-BYTE the
 * sequence from g_fragment_shader_assembly, copied rather than re-derived.
 * The only insertion is 4 sequential color-varying reads (r,g,b,a, in
 * g_fragment_shader_untextured_smooth_assembly's non-pipelined style,
 * registers rf20/rf21/rf22/rf23, unused by the surrounding code) placed
 * after the two `ldtmu` loads rather than before the TMU write -- see the
 * block's own note at that point for why the texel registers must already
 * be loaded. The final unpack step is a MULTIPLY of each texel
 * component by the matching vertex-color component rather than the base
 * shader's clear-then-add passthrough (blue*vB, green*vG, red*vR,
 * alpha*vA -- channel order matches that shader's texture-channel-swap
 * convention, `rf4.l`/`rf4.h` = blue/green, `rf3.l`/`rf3.h` = red/alpha).
 *
 * Thrsw gap: consecutive pair at instruction 19, final thread-end thrsw at
 * instruction 28 -- a gap of 9, well over the required >=3.
 */
static const char* g_fragment_shader_textured_smooth_assembly[] = {
    /* [unchanged from g_fragment_shader_assembly] S/T reconstruction. */
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",

    /* [unchanged] wait, write T/S into TMU, thrsw pair, ldtmu x2. */
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)

    /* Color-varying reads happen here, after ldtmu rather than before;
     * the position is inert either way.
     *
     * The 6 floats per vertex this shader consumes (s,t,r,g,b,a) cannot be
     * declared as ONE attribute record: the hardware's vec_size field is
     * only 2 bits, so 4 components is the absolute maximum a single
     * attribute record can represent. draw.c therefore splits them into
     * two records, 4 + 2 components (see its glShaderStateAttributeRecord
     * call site and v3d_vertex.h). */
    "nop ; nop ; ldvary.r0",    // load r/w
    "nop ; fmul r1, r0, rf0",   // r1 = r/w * w
    "fadd rf20, r1, r5 ; nop", // rf20 = true vertex red

    "nop ; nop ; ldvary.r0",    // load g/w
    "nop ; fmul r1, r0, rf0",   // r1 = g/w * w
    "fadd rf21, r1, r5 ; nop", // rf21 = true vertex green

    "nop ; nop ; ldvary.r0",    // load b/w
    "nop ; fmul r1, r0, rf0",   // r1 = b/w * w
    "fadd rf22, r1, r5 ; fmul rf8, rf4.h, rf21", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; fmul rf7, rf4.l, rf20", // rf23 = true vertex alpha

    /* modulate: texel component * vertex-color component. The CPU side
     * (draw.c's texbuf/texbuf2 population) swaps which value (b vs r) it
     * writes into which buffer slot, to compensate for a cross-wiring
     * between the two split attribute records -- see draw.c's own comment
     * on that swap -- so rf20/rf21/rf22/rf23 hold true vertex
     * red/green/blue/alpha here. */
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * TEXTURED, SMOOTH, GL_BLEND. Derived from textured_smooth.
 * rf7/rf8/rf9/rf10 are red/green/blue/alpha: uniform 0 is fixed_color's byte 0,
 * which is red, and it multiplies rf7.
 */
static const char* g_fragment_shader_textured_smooth_envblend_assembly[] = {
    /* [unchanged from g_fragment_shader_assembly] S/T reconstruction. */
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",

    /* [unchanged] wait, write T/S into TMU, thrsw pair, ldtmu x2. */
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)

    /* Color-varying reads happen here, after ldtmu rather than before;
     * the position is inert either way.
     *
     * The 6 floats per vertex this shader consumes (s,t,r,g,b,a) cannot be
     * declared as ONE attribute record: the hardware's vec_size field is
     * only 2 bits, so 4 components is the absolute maximum a single
     * attribute record can represent. draw.c therefore splits them into
     * two records, 4 + 2 components (see its glShaderStateAttributeRecord
     * call site and v3d_vertex.h). */
    "nop ; nop ; ldvary.r0",    // load r/w
    "nop ; fmul r1, r0, rf0",   // r1 = r/w * w
    "fadd rf20, r1, r5 ; nop", // rf20 = true vertex red

    "nop ; nop ; ldvary.r0",    // load g/w
    "nop ; fmul r1, r0, rf0",   // r1 = g/w * w
    "fadd rf21, r1, r5 ; nop", // rf21 = true vertex green

    "nop ; nop ; ldvary.r0",    // load b/w
    "nop ; fmul r1, r0, rf0",   // r1 = b/w * w
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha

    /* modulate: texel component * vertex-color component. The CPU side
     * (draw.c's texbuf/texbuf2 population) swaps which value (b vs r) it
     * writes into which buffer slot, to compensate for a cross-wiring
     * between the two split attribute records -- see draw.c's own comment
     * on that swap -- so rf20/rf21/rf22/rf23 hold true vertex
     * red/green/blue/alpha here. */
    /* GL_BLEND: Cv = Cp + Cs*(Cc - Cp). The primary colour is already in
     * rf20..rf23 from the varyings; the three environment-colour words are
     * this shader's ONLY uniforms past the two TMU config words. */
    "nop ; nop ; ldunifrf.rf13",
    "nop ; nop ; ldunifrf.rf14",
    "nop ; nop ; ldunifrf.rf15",
    "fsub rf13, rf13, rf20 ; nop",
    "fsub rf14, rf14, rf21 ; nop",
    "fsub rf15, rf15, rf22 ; nop",
    "nop ; fmul r0, rf4.l, rf13",
    "nop ; fmul r1, rf4.h, rf14",
    "nop ; fmul r2, rf3.l, rf15",
    "fadd rf7, rf20, r0 ; nop",
    "fadd rf8, rf21, r1 ; nop",
    "fadd rf9, rf22, r2 ; nop",
    "nop ; fmul rf10, rf3.h, rf23",

    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};


/*
 * TEXTURED, SMOOTH, GL_ADD. Derived from textured_smooth.
 * rf7/rf8/rf9/rf10 are red/green/blue/alpha: uniform 0 is fixed_color's byte 0,
 * which is red, and it multiplies rf7.
 */
static const char* g_fragment_shader_textured_smooth_envadd_assembly[] = {
    /* [unchanged from g_fragment_shader_assembly] S/T reconstruction. */
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",

    /* [unchanged] wait, write T/S into TMU, thrsw pair, ldtmu x2. */
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)

    /* Color-varying reads happen here, after ldtmu rather than before;
     * the position is inert either way.
     *
     * The 6 floats per vertex this shader consumes (s,t,r,g,b,a) cannot be
     * declared as ONE attribute record: the hardware's vec_size field is
     * only 2 bits, so 4 components is the absolute maximum a single
     * attribute record can represent. draw.c therefore splits them into
     * two records, 4 + 2 components (see its glShaderStateAttributeRecord
     * call site and v3d_vertex.h). */
    "nop ; nop ; ldvary.r0",    // load r/w
    "nop ; fmul r1, r0, rf0",   // r1 = r/w * w
    "fadd rf20, r1, r5 ; nop", // rf20 = true vertex red

    "nop ; nop ; ldvary.r0",    // load g/w
    "nop ; fmul r1, r0, rf0",   // r1 = g/w * w
    "fadd rf21, r1, r5 ; nop", // rf21 = true vertex green

    "nop ; nop ; ldvary.r0",    // load b/w
    "nop ; fmul r1, r0, rf0",   // r1 = b/w * w
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha

    /* modulate: texel component * vertex-color component. The CPU side
     * (draw.c's texbuf/texbuf2 population) swaps which value (b vs r) it
     * writes into which buffer slot, to compensate for a cross-wiring
     * between the two split attribute records -- see draw.c's own comment
     * on that swap -- so rf20/rf21/rf22/rf23 hold true vertex
     * red/green/blue/alpha here. */
    /* GL_ADD: Cv = Cp + Cs, Av = Ap * As. The combine stands apart from the
     * varying reads above rather than packing into them: each channel's
     * combine needs the ADD alu, which those instructions' fadds already
     * occupy. */
    "or rf11, 0x3f800000, 0x3f800000 ; nop",
    "fadd rf7, rf20, rf4.l ; nop",
    "fadd rf8, rf21, rf4.h ; nop",
    "fadd rf9, rf22, rf3.l ; nop",
    "nop ; fmul rf10, rf3.h, rf23",
    "fmin rf7, rf7, rf11 ; nop",
    "fmin rf8, rf8, rf11 ; nop",
    "fmin rf9, rf9, rf11 ; nop",

    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};


/*
 * GL_FOG, textured. Takes VERTEX_TEXTURED/COORDINATE_TEXTURED unchanged
 * (no color varying needed, same as plain FRAGMENT_TEXTURED) -- only the
 * fragment shader is new, combining the texture-fetch sequence (s/t
 * reconstruction + TMU fetch + the sub/fadd unpack, byte-for-byte copies
 * of g_fragment_shader_assembly's lines, as the combined shader above also
 * reuses them) with the same uniform-driven fog-blend math as the
 * untextured fog shader.
 *
 * No extra thrsw pair is needed: the TMU fetch's own embedded pair ("or
 * tmut,...;thrsw" then "nop;nop;thrsw") already satisfies the mandatory
 * last-thread-switch signal, exactly as in the base texture shader and the
 * combined shader -- the fog-blend instructions here just extend that gap
 * before the final thrsw at the tlb write.
 *
 * The fog uniforms are read via ldunifrf AFTER the 2 TMU-config uniforms
 * consumed via wrtmuc during the texture-fetch sequence; draw.c appends
 * them to the same per-draw-call fragment uniform buffer, in that order.
 * The colour-modulation block below sits between the two -- see its own
 * comment for the full stream layout.
 */
static const char* g_fragment_shader_textured_fog_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)

    /* COLOUR MODULATION, FUSED WITH THE UNPACK. Eight instructions, not
     * sixteen: the fmul takes the unpack modifier on its own source, so zeroing
     * rf7-rf10 and widening the texel halves into them beforehand buys nothing
     * -- g_fragment_shader_textured_smooth_assembly already modulates this way.
     * Same four uniforms in the same order, read into rf20-rf23, which are dead
     * in this shader.
     *
     * Placed BEFORE the fog block for two independent reasons: draw.c writes
     * the colour words ahead of the fog words, so the read order must match;
     * and GL applies fog to the post-texture-environment colour, not to the
     * raw texel. Fog rewrites rf7-rf9 only, so the alpha modulated here
     * reaches the blend unit unchanged.
     *
     * The stream for this family is
     *   [TMU][TMU][r][g][b][a][fog A][fog B][fog C][fog D][fog M][fog r][fog g]
     *   [fog b]  (+ [alpha_ref][TLB cfg] for the alphatest variants) */
    "nop ; nop ; ldunifrf.rf20",
    "nop ; nop ; ldunifrf.rf21",
    "nop ; nop ; ldunifrf.rf22",
    "nop ; nop ; ldunifrf.rf23",
    "nop ; fmul rf7, rf4.l, rf20",  // rf7  = blue  * glColor
    "nop ; fmul rf8, rf4.h, rf21",  // rf8  = green * glColor
    "nop ; fmul rf9, rf3.l, rf22",  // rf9  = red   * glColor
    "nop ; fmul rf10, rf3.h, rf23 ; ldunifrf.rf11", // rf10 = alpha * glColor

    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf11/rf12/rf13 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // linear factor
    "nop ; fmul rf12, rf12, rf0 ; ldunifrf.rf13",
    "nop ; fmul rf13, rf13, rf0",
    "nop ; fmul rf13, rf13, rf0",
    "fadd rf12, rf12, rf13 ; nop",             // C*c + D*c*c
    "or exp, rf12, rf12 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf12",             // M
    "nop ; fmul rf11, rf11, rf12",
    "or rf13, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf13, rf13, rf12 ; nop",             // 1-M
    "nop ; fmul rf13, rf13, r4",
    "fadd rf11, rf11, rf13 ; nop",             // fog factor
    "sub rf12, rf12, rf12 ; nop",
    "fmax rf11, rf11, rf12 ; nop",
    "or rf12, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // clamped to [0,1]
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",

    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // final thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * GL_ALPHA_TEST (GL_GEQUAL), textured. Same reuse pattern as the textured
 * fog shader above: texture-fetch sequence + unpack, verbatim, then the
 * same uniform-driven alpha-discard mechanism as the untextured alphatest
 * shader. No extra thrsw pair needed, same reasoning as the fog variant.
 */
static const char* g_fragment_shader_textured_alphatest_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)


    /* COLOUR MODULATION. Without this block the shader would emit the texel
     * UNMODULATED and ignore glColor entirely -- and, with GL_MODULATE, hand
     * the hardware blend unit the texel's own alpha instead of glColor's, so
     * a translucent alpha-tested draw would render opaque (SRC_ALPHA
     * blending with 1.0 is a no-op).
     *
     * Every sibling shape does the same: the SMOOTH alphatest variants via
     * interpolated varyings (fmul rf10, rf3.h, rf23), the UNTEXTURED alphatest
     * variants via these same 4 uniforms, and FRAGMENT_TEXTURED_COLORMOD --
     * the plain textured+flat fallback -- via these same 4 uniforms in this
     * same order.
     *
     * UNIFORM STREAM for these shaders:
     *   [TMU][TMU][red][green][blue][alpha][alpha_ref][TLB cfg]
     * The two wrtmuc take words 0-1, these four ldunifrf take 2-5, rf15 takes 6,
     * and the `or tlbu` below implicitly takes word 7 -- exactly where draw.c
     * appends 0xffffff84. All seven variants of this family must carry this
     * block or they would read alpha_ref from the wrong word, desynchronising
     * the stream.
     *
     * rf5 is dead after the TMU request section above -- the same register
     * g_fragment_shader_textured_blend_assembly reuses for this purpose.
     * Channel order follows draw.c: its red word multiplies rf7 and its blue
     * word multiplies rf9, matching the texture sample's hardware channel
     * layout rather than fixed_color's logical order, which is why rf7 is
     * labelled blue above. */
    "nop ; nop ; ldunifrf.rf5",   // colour word 0 -> rf7
    "nop ; fmul rf7, rf4.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf8, rf4.h, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf9, rf3.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf10, rf3.h, rf5 ; ldunifrf.rf15",

    "fcmp.pushc -, rf15, rf10 ; nop",
    "setmsf.ifna -, 0 ; nop",

    /* Passthrough Z write: see the untextured GL_GREATER variant for the
     * full derivation. Placed after the setmsf discard and before the
     * colour vfpacks -- the only legal slot. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop", // passthrough Z write; config from uniform stream
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // final thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * GL_GREATER alpha test, textured. Same reuse pattern as the textured
 * GEQUAL shader above: texture-fetch sequence + unpack, verbatim, then the
 * untextured GREATER variant's fcmp-operand-swap/setmsf-condition-flip
 * discard mechanism (see that shader's own comment for the derivation)
 * instead of the GEQUAL pair's.
 */
static const char* g_fragment_shader_textured_alphatest_greater_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)


    /* colour modulation: see g_fragment_shader_textured_alphatest_assembly */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf4.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf8, rf4.h, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf9, rf3.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf10, rf3.h, rf5 ; ldunifrf.rf15",

    "fcmp.pushc -, rf10, rf15 ; nop", // IFA true means threshold >= alpha (alpha <= threshold)
    "setmsf.ifa -, 0 ; nop",          // discard when alpha <= threshold; keep when alpha > threshold

    /* Passthrough Z write -- textured shape. Same mechanism, placement and
     * rationale as the untextured GREATER variant; see that shader for the
     * full derivation. The uniform stream here is the one
     * g_fragment_shader_textured_alphatest_assembly documents, so the tlbu
     * read lands on the TLB config word draw.c appends last. */
    "or tlbu, rf10, rf10 ; nop", // passthrough Z write; config word from the uniform stream
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // final thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * REMAINING alpha_func VARIANTS, textured. Same five discard blocks as the
 * untextured family above -- see that family's header comment for the full
 * fcmp/setmsf and fcmp.pushz derivation -- spliced onto this shape's own
 * texture-fetch + unpack preamble, copied verbatim from
 * g_fragment_shader_textured_alphatest_greater_assembly. Only the two/one
 * discard instructions differ between these five.
 */

/* GL_LESS, textured. */
static const char* g_fragment_shader_textured_alphatest_less_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)


    /* colour modulation: see g_fragment_shader_textured_alphatest_assembly */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf4.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf8, rf4.h, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf9, rf3.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf10, rf3.h, rf5 ; ldunifrf.rf15",

    "fcmp.pushc -, rf15, rf10 ; nop", // IFA true means alpha >= threshold
    "setmsf.ifa -, 0 ; nop",          // discard when alpha >= threshold; keep when alpha < threshold

    /* Passthrough Z write: see the untextured GL_GREATER variant for the
     * full derivation. Placed after the setmsf discard and before the
     * colour vfpacks -- the only legal slot. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop", // passthrough Z write; config from uniform stream
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // final thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/* GL_LEQUAL, textured. */
static const char* g_fragment_shader_textured_alphatest_lequal_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)


    /* colour modulation: see g_fragment_shader_textured_alphatest_assembly */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf4.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf8, rf4.h, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf9, rf3.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf10, rf3.h, rf5 ; ldunifrf.rf15",

    "fcmp.pushc -, rf10, rf15 ; nop", // IFA true means alpha <= threshold
    "setmsf.ifna -, 0 ; nop",         // discard when alpha > threshold; keep when alpha <= threshold

    /* Passthrough Z write: see the untextured GL_GREATER variant for the
     * full derivation. Placed after the setmsf discard and before the
     * colour vfpacks -- the only legal slot. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop", // passthrough Z write; config from uniform stream
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // final thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/* GL_EQUAL, textured. */
static const char* g_fragment_shader_textured_alphatest_equal_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)


    /* colour modulation: see g_fragment_shader_textured_alphatest_assembly */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf4.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf8, rf4.h, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf9, rf3.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf10, rf3.h, rf5 ; ldunifrf.rf15",

    "fcmp.pushz -, rf15, rf10 ; nop", // Z flag: IFA true means alpha == threshold
    "setmsf.ifna -, 0 ; nop",         // discard when alpha != threshold

    /* Passthrough Z write: see the untextured GL_GREATER variant for the
     * full derivation. Placed after the setmsf discard and before the
     * colour vfpacks -- the only legal slot. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop", // passthrough Z write; config from uniform stream
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // final thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/* GL_NOTEQUAL, textured. */
static const char* g_fragment_shader_textured_alphatest_notequal_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)


    /* colour modulation: see g_fragment_shader_textured_alphatest_assembly */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf4.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf8, rf4.h, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf9, rf3.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf10, rf3.h, rf5 ; ldunifrf.rf15",

    "fcmp.pushz -, rf15, rf10 ; nop", // Z flag: IFA true means alpha == threshold
    "setmsf.ifa -, 0 ; nop",          // discard when alpha == threshold

    /* Passthrough Z write: see the untextured GL_GREATER variant for the
     * full derivation. Placed after the setmsf discard and before the
     * colour vfpacks -- the only legal slot. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop", // passthrough Z write; config from uniform stream
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // final thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/* GL_NEVER, textured. Unconditional discard; the ldunifrf.rf15 is kept
 * unread on purpose -- see the untextured family's header comment. */
static const char* g_fragment_shader_textured_alphatest_never_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)


    /* colour modulation: see g_fragment_shader_textured_alphatest_assembly.
     * Kept even here, where every fragment is discarded, because the uniform
     * stream layout must be identical across all seven variants of this family
     * -- draw.c writes these four words for the whole class. */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf4.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf8, rf4.h, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf9, rf3.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf10, rf3.h, rf5 ; ldunifrf.rf15",

    "setmsf -, 0 ; nop",         // discard every fragment, unconditionally

    /* Passthrough Z write: see the untextured GL_GREATER variant for the
     * full derivation. Placed after the setmsf discard and before the
     * colour vfpacks -- the only legal slot. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop", // passthrough Z write; config from uniform stream
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // final thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * GL_ALPHA_TEST (GL_GREATER), textured + smooth.
 *
 * g_fragment_shader_textured_smooth_assembly's GL_MODULATE sequence
 * (texture fetch + per-vertex-color reads + final multiply, producing
 * rf7/rf8/rf9/rf10 = blue/green/red/alpha) verbatim, up through the
 * modulate step, with g_fragment_shader_textured_alphatest_greater_
 * assembly's own GL_GREATER discard block (ldunifrf threshold, fcmp/
 * setmsf) spliced in on the FINAL (post-modulation) alpha, before the
 * vfpack/thrsw -- matching GL semantics, where alpha test applies to the
 * fragment's final alpha and not the raw texel alpha.
 */
static const char* g_fragment_shader_textured_smooth_alphatest_greater_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",

    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)

    "nop ; nop ; ldvary.r0",    // load r/w
    "nop ; fmul r1, r0, rf0",   // r1 = r/w * w
    "fadd rf20, r1, r5 ; nop", // rf20 = true vertex red

    "nop ; nop ; ldvary.r0",    // load g/w
    "nop ; fmul r1, r0, rf0",   // r1 = g/w * w
    "fadd rf21, r1, r5 ; nop", // rf21 = true vertex green

    "nop ; nop ; ldvary.r0",    // load b/w
    "nop ; fmul r1, r0, rf0",   // r1 = b/w * w
    "fadd rf22, r1, r5 ; fmul rf8, rf4.h, rf21", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; fmul rf7, rf4.l, rf20", // rf23 = true vertex alpha

    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23 ; ldunifrf.rf15", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "fcmp.pushc -, rf10, rf15 ; nop", // IFA true means threshold >= alpha (alpha <= threshold)
    "setmsf.ifa -, 0 ; nop",          // discard when alpha <= threshold; keep when alpha > threshold

    /* Passthrough Z write: see the untextured GL_GREATER variant for the
     * full derivation. Placed after the setmsf discard and before the
     * colour vfpacks -- the only legal slot. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop", // passthrough Z write; config from uniform stream
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // final thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * GL_ALPHA_TEST (GL_GEQUAL), textured + smooth. Identical to
 * g_fragment_shader_textured_smooth_alphatest_greater_assembly except for
 * the discard block, which is g_fragment_shader_textured_alphatest_
 * assembly's own GEQUAL mechanism (operand order rf15,rf10 and
 * setmsf.ifna, instead of the GREATER pair's rf10,rf15 and setmsf.ifa) --
 * see that shader's own comment for the derivation.
 */
static const char* g_fragment_shader_textured_smooth_alphatest_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",

    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)

    "nop ; nop ; ldvary.r0",    // load r/w
    "nop ; fmul r1, r0, rf0",   // r1 = r/w * w
    "fadd rf20, r1, r5 ; nop", // rf20 = true vertex red

    "nop ; nop ; ldvary.r0",    // load g/w
    "nop ; fmul r1, r0, rf0",   // r1 = g/w * w
    "fadd rf21, r1, r5 ; nop", // rf21 = true vertex green

    "nop ; nop ; ldvary.r0",    // load b/w
    "nop ; fmul r1, r0, rf0",   // r1 = b/w * w
    "fadd rf22, r1, r5 ; fmul rf8, rf4.h, rf21", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; fmul rf7, rf4.l, rf20", // rf23 = true vertex alpha

    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23 ; ldunifrf.rf15", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "fcmp.pushc -, rf15, rf10 ; nop", // IFA true means alpha >= threshold
    "setmsf.ifna -, 0 ; nop",         // discard when alpha < threshold; keep when alpha >= threshold

    /* Passthrough Z write: see the untextured GL_GREATER variant for the
     * full derivation. Placed after the setmsf discard and before the
     * colour vfpacks -- the only legal slot. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop", // passthrough Z write; config from uniform stream
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // final thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * REMAINING alpha_func VARIANTS, textured + smooth. Last of the three
 * shapes. The preamble is g_fragment_shader_textured_smooth_
 * alphatest_greater_assembly's GL_MODULATE sequence verbatim
 * (texture fetch, four per-vertex color varyings, final modulate into
 * rf7/rf8/rf9/rf10), with each of the five discard blocks spliced in
 * on the FINAL post-modulation alpha -- matching GL semantics, where
 * alpha test applies to the fragment's final alpha and not the raw texel
 * alpha. See the untextured family's header comment for the derivation of
 * every discard block.
 */

/* GL_LESS, textured + smooth. */
static const char* g_fragment_shader_textured_smooth_alphatest_less_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",

    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)

    "nop ; nop ; ldvary.r0",   // load r/w
    "nop ; fmul r1, r0, rf0",  // r1 = r/w * w
    "fadd rf20, r1, r5 ; nop", // rf20 = true vertex red

    "nop ; nop ; ldvary.r0",   // load g/w
    "nop ; fmul r1, r0, rf0",  // r1 = g/w * w
    "fadd rf21, r1, r5 ; nop", // rf21 = true vertex green

    "nop ; nop ; ldvary.r0",   // load b/w
    "nop ; fmul r1, r0, rf0",  // r1 = b/w * w
    "fadd rf22, r1, r5 ; fmul rf8, rf4.h, rf21", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",   // load a/w
    "nop ; fmul r1, r0, rf0",  // r1 = a/w * w
    "fadd rf23, r1, r5 ; fmul rf7, rf4.l, rf20", // rf23 = true vertex alpha

    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23 ; ldunifrf.rf15", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "fcmp.pushc -, rf15, rf10 ; nop", // IFA true means alpha >= threshold
    "setmsf.ifa -, 0 ; nop",          // discard when alpha >= threshold; keep when alpha < threshold

    /* Passthrough Z write: see the untextured GL_GREATER variant for the
     * full derivation. Placed after the setmsf discard and before the
     * colour vfpacks -- the only legal slot. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop", // passthrough Z write; config from uniform stream
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // final thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/* GL_LEQUAL, textured + smooth. */
static const char* g_fragment_shader_textured_smooth_alphatest_lequal_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",

    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)

    "nop ; nop ; ldvary.r0",   // load r/w
    "nop ; fmul r1, r0, rf0",  // r1 = r/w * w
    "fadd rf20, r1, r5 ; nop", // rf20 = true vertex red

    "nop ; nop ; ldvary.r0",   // load g/w
    "nop ; fmul r1, r0, rf0",  // r1 = g/w * w
    "fadd rf21, r1, r5 ; nop", // rf21 = true vertex green

    "nop ; nop ; ldvary.r0",   // load b/w
    "nop ; fmul r1, r0, rf0",  // r1 = b/w * w
    "fadd rf22, r1, r5 ; fmul rf8, rf4.h, rf21", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",   // load a/w
    "nop ; fmul r1, r0, rf0",  // r1 = a/w * w
    "fadd rf23, r1, r5 ; fmul rf7, rf4.l, rf20", // rf23 = true vertex alpha

    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23 ; ldunifrf.rf15", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "fcmp.pushc -, rf10, rf15 ; nop", // IFA true means alpha <= threshold
    "setmsf.ifna -, 0 ; nop",         // discard when alpha > threshold; keep when alpha <= threshold

    /* Passthrough Z write: see the untextured GL_GREATER variant for the
     * full derivation. Placed after the setmsf discard and before the
     * colour vfpacks -- the only legal slot. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop", // passthrough Z write; config from uniform stream
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // final thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/* GL_EQUAL, textured + smooth. */
static const char* g_fragment_shader_textured_smooth_alphatest_equal_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",

    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)

    "nop ; nop ; ldvary.r0",   // load r/w
    "nop ; fmul r1, r0, rf0",  // r1 = r/w * w
    "fadd rf20, r1, r5 ; nop", // rf20 = true vertex red

    "nop ; nop ; ldvary.r0",   // load g/w
    "nop ; fmul r1, r0, rf0",  // r1 = g/w * w
    "fadd rf21, r1, r5 ; nop", // rf21 = true vertex green

    "nop ; nop ; ldvary.r0",   // load b/w
    "nop ; fmul r1, r0, rf0",  // r1 = b/w * w
    "fadd rf22, r1, r5 ; fmul rf8, rf4.h, rf21", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",   // load a/w
    "nop ; fmul r1, r0, rf0",  // r1 = a/w * w
    "fadd rf23, r1, r5 ; fmul rf7, rf4.l, rf20", // rf23 = true vertex alpha

    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23 ; ldunifrf.rf15", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "fcmp.pushz -, rf15, rf10 ; nop", // Z flag: IFA true means alpha == threshold
    "setmsf.ifna -, 0 ; nop",         // discard when alpha != threshold

    /* Passthrough Z write: see the untextured GL_GREATER variant for the
     * full derivation. Placed after the setmsf discard and before the
     * colour vfpacks -- the only legal slot. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop", // passthrough Z write; config from uniform stream
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // final thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/* GL_NOTEQUAL, textured + smooth. */
static const char* g_fragment_shader_textured_smooth_alphatest_notequal_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",

    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)

    "nop ; nop ; ldvary.r0",   // load r/w
    "nop ; fmul r1, r0, rf0",  // r1 = r/w * w
    "fadd rf20, r1, r5 ; nop", // rf20 = true vertex red

    "nop ; nop ; ldvary.r0",   // load g/w
    "nop ; fmul r1, r0, rf0",  // r1 = g/w * w
    "fadd rf21, r1, r5 ; nop", // rf21 = true vertex green

    "nop ; nop ; ldvary.r0",   // load b/w
    "nop ; fmul r1, r0, rf0",  // r1 = b/w * w
    "fadd rf22, r1, r5 ; fmul rf8, rf4.h, rf21", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",   // load a/w
    "nop ; fmul r1, r0, rf0",  // r1 = a/w * w
    "fadd rf23, r1, r5 ; fmul rf7, rf4.l, rf20", // rf23 = true vertex alpha

    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23 ; ldunifrf.rf15", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "fcmp.pushz -, rf15, rf10 ; nop", // Z flag: IFA true means alpha == threshold
    "setmsf.ifa -, 0 ; nop",          // discard when alpha == threshold

    /* Passthrough Z write: see the untextured GL_GREATER variant for the
     * full derivation. Placed after the setmsf discard and before the
     * colour vfpacks -- the only legal slot. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop", // passthrough Z write; config from uniform stream
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // final thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/* GL_NEVER, textured + smooth. Unconditional discard; ldunifrf.rf15 kept
 * unread on purpose -- see the untextured family's header comment. */
static const char* g_fragment_shader_textured_smooth_alphatest_never_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",

    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)

    "nop ; nop ; ldvary.r0",   // load r/w
    "nop ; fmul r1, r0, rf0",  // r1 = r/w * w
    "fadd rf20, r1, r5 ; nop", // rf20 = true vertex red

    "nop ; nop ; ldvary.r0",   // load g/w
    "nop ; fmul r1, r0, rf0",  // r1 = g/w * w
    "fadd rf21, r1, r5 ; nop", // rf21 = true vertex green

    "nop ; nop ; ldvary.r0",   // load b/w
    "nop ; fmul r1, r0, rf0",  // r1 = b/w * w
    "fadd rf22, r1, r5 ; fmul rf8, rf4.h, rf21", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",   // load a/w
    "nop ; fmul r1, r0, rf0",  // r1 = a/w * w
    "fadd rf23, r1, r5 ; fmul rf7, rf4.l, rf20", // rf23 = true vertex alpha

    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23 ; ldunifrf.rf15", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "setmsf -, 0 ; nop",         // discard every fragment, unconditionally

    /* Passthrough Z write: see the untextured GL_GREATER variant for the
     * full derivation. Placed after the setmsf discard and before the
     * colour vfpacks -- the only legal slot. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop", // passthrough Z write; config from uniform stream
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // final thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/* ==================================================================
 * COMBINED FOG + ALPHA TEST
 *
 * These 14 variants serve a draw that asks for both: 7 compare functions
 * x 2 shapes, untextured and textured.
 * ================================================================== */

/*
 * Combined fog + alpha test (GL_GEQUAL), untextured. Spliced from the
 * untextured fog shader: the fog math, the fog/base register pairing
 * and the thrsw layout are copied VERBATIM so a combined draw fogs
 * identically to a fog-only draw. Added on top: the alpha threshold
 * uniform (rf15), the compare + setmsf discard, and the passthrough Z
 * write.
 *
 * draw.c writes the colour, fog and alpha_ref words in that order and the
 * TLB config word last, so tlbu picks up the config and not a colour value.
 */
static const char* g_fragment_shader_untextured_fog_alphatest_assembly[] = {
    "nop ; nop ; ldunifrf.rf7",  // rf7  = base red   (uniform 0)
    "nop ; nop ; ldunifrf.rf8",  // rf8  = base green (uniform 1)
    "nop ; nop ; ldunifrf.rf9",  // rf9  = base blue  (uniform 2)
    "nop ; nop ; ldunifrf.rf10", // rf10 = alpha (uniform 3) -- never fogged
    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf3/rf4/rf5 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf3",             // A
    "nop ; nop ; ldunifrf.rf4",             // B
    "nop ; fmul rf4, rf4, rf0",
    "fadd rf3, rf3, rf4 ; nop ; ldunifrf.rf4",             // linear factor
    "nop ; fmul rf4, rf4, rf0 ; ldunifrf.rf5",
    "nop ; fmul rf5, rf5, rf0",
    "nop ; fmul rf5, rf5, rf0",
    "fadd rf4, rf4, rf5 ; nop",             // C*c + D*c*c
    "or exp, rf4, rf4 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf4",             // M
    "nop ; fmul rf3, rf3, rf4",
    "or rf5, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf5, rf5, rf4 ; nop",             // 1-M
    "nop ; fmul rf5, rf5, r4",
    "fadd rf3, rf3, rf5 ; nop",             // fog factor
    "sub rf4, rf4, rf4 ; nop",
    "fmax rf3, rf3, rf4 ; nop",
    "or rf4, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf3, rf3, rf4 ; nop ; ldunifrf.rf4",             // clamped to [0,1]
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop ; ldunifrf.rf15",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "fcmp.pushc -, rf15, rf10 ; nop",
    "setmsf.ifna -, 0 ; nop",
    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- satisfies the >=3-instruction gap before the next thrsw
    /* Passthrough Z write: the FEP must stop writing depth because the
     * QPU does it here instead, so a discarded fragment leaves the depth
     * buffer alone. Placed after the setmsf discard and before the first
     * vfpack tlb -- the only legal slot -- and at least 3 ticks after the
     * first thrsw of the last-thrsw pair. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * Combined fog + alpha test (GL_GEQUAL), textured. Spliced from the
 * textured fog shader: the fog math, the fog/base register pairing
 * and the thrsw layout are copied VERBATIM so a combined draw fogs
 * identically to a fog-only draw. Added on top: the alpha threshold
 * uniform (rf15), the compare + setmsf discard, and the passthrough Z
 * write.
 *
 * draw.c writes the colour, fog and alpha_ref words in that order and the
 * TLB config word last, so tlbu picks up the config and not a colour value.
 */
static const char* g_fragment_shader_textured_fog_alphatest_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)

    /* COLOUR MODULATION -- see
     * g_fragment_shader_textured_alphatest_assembly for the derivation and
     * g_fragment_shader_textured_fog_assembly for why it sits before the fog
     * block. Fog rewrites rf7-rf9 only, so the alpha modulated here survives to
     * the blend unit. rf5 is dead after the TMU requests above. */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf4.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf8, rf4.h, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf9, rf3.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf10, rf3.h, rf5 ; ldunifrf.rf11",

    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf11/rf12/rf13 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // linear factor
    "nop ; fmul rf12, rf12, rf0 ; ldunifrf.rf13",
    "nop ; fmul rf13, rf13, rf0",
    "nop ; fmul rf13, rf13, rf0",
    "fadd rf12, rf12, rf13 ; nop",             // C*c + D*c*c
    "or exp, rf12, rf12 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf12",             // M
    "nop ; fmul rf11, rf11, rf12",
    "or rf13, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf13, rf13, rf12 ; nop",             // 1-M
    "nop ; fmul rf13, rf13, r4",
    "fadd rf11, rf11, rf13 ; nop",             // fog factor
    "sub rf12, rf12, rf12 ; nop",
    "fmax rf11, rf11, rf12 ; nop",
    "or rf12, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // clamped to [0,1]
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop ; ldunifrf.rf15",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "fcmp.pushc -, rf15, rf10 ; nop",
    "setmsf.ifna -, 0 ; nop",
    /* Passthrough Z write: the FEP must stop writing depth because the
     * QPU does it here instead, so a discarded fragment leaves the depth
     * buffer alone. Placed after the setmsf discard and before the first
     * vfpack tlb -- the only legal slot -- and at least 3 ticks after the
     * first thrsw of the last-thrsw pair. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // final thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * Combined fog + alpha test (GL_GREATER), untextured. Spliced from the
 * untextured fog shader: the fog math, the fog/base register pairing
 * and the thrsw layout are copied VERBATIM so a combined draw fogs
 * identically to a fog-only draw. Added on top: the alpha threshold
 * uniform (rf15), the compare + setmsf discard, and the passthrough Z
 * write.
 *
 * draw.c writes the colour, fog and alpha_ref words in that order and the
 * TLB config word last, so tlbu picks up the config and not a colour value.
 */
static const char* g_fragment_shader_untextured_fog_alphatest_greater_assembly[] = {
    "nop ; nop ; ldunifrf.rf7",  // rf7  = base red   (uniform 0)
    "nop ; nop ; ldunifrf.rf8",  // rf8  = base green (uniform 1)
    "nop ; nop ; ldunifrf.rf9",  // rf9  = base blue  (uniform 2)
    "nop ; nop ; ldunifrf.rf10", // rf10 = alpha (uniform 3) -- never fogged
    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf3/rf4/rf5 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf3",             // A
    "nop ; nop ; ldunifrf.rf4",             // B
    "nop ; fmul rf4, rf4, rf0",
    "fadd rf3, rf3, rf4 ; nop ; ldunifrf.rf4",             // linear factor
    "nop ; fmul rf4, rf4, rf0 ; ldunifrf.rf5",
    "nop ; fmul rf5, rf5, rf0",
    "nop ; fmul rf5, rf5, rf0",
    "fadd rf4, rf4, rf5 ; nop",             // C*c + D*c*c
    "or exp, rf4, rf4 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf4",             // M
    "nop ; fmul rf3, rf3, rf4",
    "or rf5, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf5, rf5, rf4 ; nop",             // 1-M
    "nop ; fmul rf5, rf5, r4",
    "fadd rf3, rf3, rf5 ; nop",             // fog factor
    "sub rf4, rf4, rf4 ; nop",
    "fmax rf3, rf3, rf4 ; nop",
    "or rf4, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf3, rf3, rf4 ; nop ; ldunifrf.rf4",             // clamped to [0,1]
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop ; ldunifrf.rf15",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "fcmp.pushc -, rf10, rf15 ; nop",
    "setmsf.ifa -, 0 ; nop",
    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- satisfies the >=3-instruction gap before the next thrsw
    /* Passthrough Z write: the FEP must stop writing depth because the
     * QPU does it here instead, so a discarded fragment leaves the depth
     * buffer alone. Placed after the setmsf discard and before the first
     * vfpack tlb -- the only legal slot -- and at least 3 ticks after the
     * first thrsw of the last-thrsw pair. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * Combined fog + alpha test (GL_GREATER), textured. Spliced from the
 * textured fog shader: the fog math, the fog/base register pairing
 * and the thrsw layout are copied VERBATIM so a combined draw fogs
 * identically to a fog-only draw. Added on top: the alpha threshold
 * uniform (rf15), the compare + setmsf discard, and the passthrough Z
 * write.
 *
 * draw.c writes the colour, fog and alpha_ref words in that order and the
 * TLB config word last, so tlbu picks up the config and not a colour value.
 */
static const char* g_fragment_shader_textured_fog_alphatest_greater_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)

    /* COLOUR MODULATION -- see
     * g_fragment_shader_textured_alphatest_assembly for the derivation and
     * g_fragment_shader_textured_fog_assembly for why it sits before the fog
     * block. Fog rewrites rf7-rf9 only, so the alpha modulated here survives to
     * the blend unit. rf5 is dead after the TMU requests above. */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf4.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf8, rf4.h, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf9, rf3.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf10, rf3.h, rf5 ; ldunifrf.rf11",

    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf11/rf12/rf13 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // linear factor
    "nop ; fmul rf12, rf12, rf0 ; ldunifrf.rf13",
    "nop ; fmul rf13, rf13, rf0",
    "nop ; fmul rf13, rf13, rf0",
    "fadd rf12, rf12, rf13 ; nop",             // C*c + D*c*c
    "or exp, rf12, rf12 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf12",             // M
    "nop ; fmul rf11, rf11, rf12",
    "or rf13, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf13, rf13, rf12 ; nop",             // 1-M
    "nop ; fmul rf13, rf13, r4",
    "fadd rf11, rf11, rf13 ; nop",             // fog factor
    "sub rf12, rf12, rf12 ; nop",
    "fmax rf11, rf11, rf12 ; nop",
    "or rf12, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // clamped to [0,1]
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop ; ldunifrf.rf15",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "fcmp.pushc -, rf10, rf15 ; nop",
    "setmsf.ifa -, 0 ; nop",
    /* Passthrough Z write: the FEP must stop writing depth because the
     * QPU does it here instead, so a discarded fragment leaves the depth
     * buffer alone. Placed after the setmsf discard and before the first
     * vfpack tlb -- the only legal slot -- and at least 3 ticks after the
     * first thrsw of the last-thrsw pair. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // final thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * Combined fog + alpha test (GL_LESS), untextured. Spliced from the
 * untextured fog shader: the fog math, the fog/base register pairing
 * and the thrsw layout are copied VERBATIM so a combined draw fogs
 * identically to a fog-only draw. Added on top: the alpha threshold
 * uniform (rf15), the compare + setmsf discard, and the passthrough Z
 * write.
 *
 * draw.c writes the colour, fog and alpha_ref words in that order and the
 * TLB config word last, so tlbu picks up the config and not a colour value.
 */
static const char* g_fragment_shader_untextured_fog_alphatest_less_assembly[] = {
    "nop ; nop ; ldunifrf.rf7",  // rf7  = base red   (uniform 0)
    "nop ; nop ; ldunifrf.rf8",  // rf8  = base green (uniform 1)
    "nop ; nop ; ldunifrf.rf9",  // rf9  = base blue  (uniform 2)
    "nop ; nop ; ldunifrf.rf10", // rf10 = alpha (uniform 3) -- never fogged
    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf3/rf4/rf5 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf3",             // A
    "nop ; nop ; ldunifrf.rf4",             // B
    "nop ; fmul rf4, rf4, rf0",
    "fadd rf3, rf3, rf4 ; nop ; ldunifrf.rf4",             // linear factor
    "nop ; fmul rf4, rf4, rf0 ; ldunifrf.rf5",
    "nop ; fmul rf5, rf5, rf0",
    "nop ; fmul rf5, rf5, rf0",
    "fadd rf4, rf4, rf5 ; nop",             // C*c + D*c*c
    "or exp, rf4, rf4 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf4",             // M
    "nop ; fmul rf3, rf3, rf4",
    "or rf5, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf5, rf5, rf4 ; nop",             // 1-M
    "nop ; fmul rf5, rf5, r4",
    "fadd rf3, rf3, rf5 ; nop",             // fog factor
    "sub rf4, rf4, rf4 ; nop",
    "fmax rf3, rf3, rf4 ; nop",
    "or rf4, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf3, rf3, rf4 ; nop ; ldunifrf.rf4",             // clamped to [0,1]
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop ; ldunifrf.rf15",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "fcmp.pushc -, rf15, rf10 ; nop",
    "setmsf.ifa -, 0 ; nop",
    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- satisfies the >=3-instruction gap before the next thrsw
    /* Passthrough Z write: the FEP must stop writing depth because the
     * QPU does it here instead, so a discarded fragment leaves the depth
     * buffer alone. Placed after the setmsf discard and before the first
     * vfpack tlb -- the only legal slot -- and at least 3 ticks after the
     * first thrsw of the last-thrsw pair. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * Combined fog + alpha test (GL_LESS), textured. Spliced from the
 * textured fog shader: the fog math, the fog/base register pairing
 * and the thrsw layout are copied VERBATIM so a combined draw fogs
 * identically to a fog-only draw. Added on top: the alpha threshold
 * uniform (rf15), the compare + setmsf discard, and the passthrough Z
 * write.
 *
 * draw.c writes the colour, fog and alpha_ref words in that order and the
 * TLB config word last, so tlbu picks up the config and not a colour value.
 */
static const char* g_fragment_shader_textured_fog_alphatest_less_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)

    /* COLOUR MODULATION -- see
     * g_fragment_shader_textured_alphatest_assembly for the derivation and
     * g_fragment_shader_textured_fog_assembly for why it sits before the fog
     * block. Fog rewrites rf7-rf9 only, so the alpha modulated here survives to
     * the blend unit. rf5 is dead after the TMU requests above. */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf4.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf8, rf4.h, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf9, rf3.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf10, rf3.h, rf5 ; ldunifrf.rf11",

    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf11/rf12/rf13 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // linear factor
    "nop ; fmul rf12, rf12, rf0 ; ldunifrf.rf13",
    "nop ; fmul rf13, rf13, rf0",
    "nop ; fmul rf13, rf13, rf0",
    "fadd rf12, rf12, rf13 ; nop",             // C*c + D*c*c
    "or exp, rf12, rf12 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf12",             // M
    "nop ; fmul rf11, rf11, rf12",
    "or rf13, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf13, rf13, rf12 ; nop",             // 1-M
    "nop ; fmul rf13, rf13, r4",
    "fadd rf11, rf11, rf13 ; nop",             // fog factor
    "sub rf12, rf12, rf12 ; nop",
    "fmax rf11, rf11, rf12 ; nop",
    "or rf12, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // clamped to [0,1]
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop ; ldunifrf.rf15",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "fcmp.pushc -, rf15, rf10 ; nop",
    "setmsf.ifa -, 0 ; nop",
    /* Passthrough Z write: the FEP must stop writing depth because the
     * QPU does it here instead, so a discarded fragment leaves the depth
     * buffer alone. Placed after the setmsf discard and before the first
     * vfpack tlb -- the only legal slot -- and at least 3 ticks after the
     * first thrsw of the last-thrsw pair. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // final thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * Combined fog + alpha test (GL_EQUAL), untextured. Spliced from the
 * untextured fog shader: the fog math, the fog/base register pairing
 * and the thrsw layout are copied VERBATIM so a combined draw fogs
 * identically to a fog-only draw. Added on top: the alpha threshold
 * uniform (rf15), the compare + setmsf discard, and the passthrough Z
 * write.
 *
 * draw.c writes the colour, fog and alpha_ref words in that order and the
 * TLB config word last, so tlbu picks up the config and not a colour value.
 */
static const char* g_fragment_shader_untextured_fog_alphatest_equal_assembly[] = {
    "nop ; nop ; ldunifrf.rf7",  // rf7  = base red   (uniform 0)
    "nop ; nop ; ldunifrf.rf8",  // rf8  = base green (uniform 1)
    "nop ; nop ; ldunifrf.rf9",  // rf9  = base blue  (uniform 2)
    "nop ; nop ; ldunifrf.rf10", // rf10 = alpha (uniform 3) -- never fogged
    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf3/rf4/rf5 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf3",             // A
    "nop ; nop ; ldunifrf.rf4",             // B
    "nop ; fmul rf4, rf4, rf0",
    "fadd rf3, rf3, rf4 ; nop ; ldunifrf.rf4",             // linear factor
    "nop ; fmul rf4, rf4, rf0 ; ldunifrf.rf5",
    "nop ; fmul rf5, rf5, rf0",
    "nop ; fmul rf5, rf5, rf0",
    "fadd rf4, rf4, rf5 ; nop",             // C*c + D*c*c
    "or exp, rf4, rf4 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf4",             // M
    "nop ; fmul rf3, rf3, rf4",
    "or rf5, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf5, rf5, rf4 ; nop",             // 1-M
    "nop ; fmul rf5, rf5, r4",
    "fadd rf3, rf3, rf5 ; nop",             // fog factor
    "sub rf4, rf4, rf4 ; nop",
    "fmax rf3, rf3, rf4 ; nop",
    "or rf4, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf3, rf3, rf4 ; nop ; ldunifrf.rf4",             // clamped to [0,1]
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop ; ldunifrf.rf15",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "fcmp.pushz -, rf15, rf10 ; nop",
    "setmsf.ifna -, 0 ; nop",
    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- satisfies the >=3-instruction gap before the next thrsw
    /* Passthrough Z write: the FEP must stop writing depth because the
     * QPU does it here instead, so a discarded fragment leaves the depth
     * buffer alone. Placed after the setmsf discard and before the first
     * vfpack tlb -- the only legal slot -- and at least 3 ticks after the
     * first thrsw of the last-thrsw pair. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * Combined fog + alpha test (GL_EQUAL), textured. Spliced from the
 * textured fog shader: the fog math, the fog/base register pairing
 * and the thrsw layout are copied VERBATIM so a combined draw fogs
 * identically to a fog-only draw. Added on top: the alpha threshold
 * uniform (rf15), the compare + setmsf discard, and the passthrough Z
 * write.
 *
 * draw.c writes the colour, fog and alpha_ref words in that order and the
 * TLB config word last, so tlbu picks up the config and not a colour value.
 */
static const char* g_fragment_shader_textured_fog_alphatest_equal_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)

    /* COLOUR MODULATION -- see
     * g_fragment_shader_textured_alphatest_assembly for the derivation and
     * g_fragment_shader_textured_fog_assembly for why it sits before the fog
     * block. Fog rewrites rf7-rf9 only, so the alpha modulated here survives to
     * the blend unit. rf5 is dead after the TMU requests above. */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf4.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf8, rf4.h, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf9, rf3.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf10, rf3.h, rf5 ; ldunifrf.rf11",

    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf11/rf12/rf13 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // linear factor
    "nop ; fmul rf12, rf12, rf0 ; ldunifrf.rf13",
    "nop ; fmul rf13, rf13, rf0",
    "nop ; fmul rf13, rf13, rf0",
    "fadd rf12, rf12, rf13 ; nop",             // C*c + D*c*c
    "or exp, rf12, rf12 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf12",             // M
    "nop ; fmul rf11, rf11, rf12",
    "or rf13, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf13, rf13, rf12 ; nop",             // 1-M
    "nop ; fmul rf13, rf13, r4",
    "fadd rf11, rf11, rf13 ; nop",             // fog factor
    "sub rf12, rf12, rf12 ; nop",
    "fmax rf11, rf11, rf12 ; nop",
    "or rf12, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // clamped to [0,1]
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop ; ldunifrf.rf15",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "fcmp.pushz -, rf15, rf10 ; nop",
    "setmsf.ifna -, 0 ; nop",
    /* Passthrough Z write: the FEP must stop writing depth because the
     * QPU does it here instead, so a discarded fragment leaves the depth
     * buffer alone. Placed after the setmsf discard and before the first
     * vfpack tlb -- the only legal slot -- and at least 3 ticks after the
     * first thrsw of the last-thrsw pair. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // final thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * Combined fog + alpha test (GL_LEQUAL), untextured. Spliced from the
 * untextured fog shader: the fog math, the fog/base register pairing
 * and the thrsw layout are copied VERBATIM so a combined draw fogs
 * identically to a fog-only draw. Added on top: the alpha threshold
 * uniform (rf15), the compare + setmsf discard, and the passthrough Z
 * write.
 *
 * draw.c writes the colour, fog and alpha_ref words in that order and the
 * TLB config word last, so tlbu picks up the config and not a colour value.
 */
static const char* g_fragment_shader_untextured_fog_alphatest_lequal_assembly[] = {
    "nop ; nop ; ldunifrf.rf7",  // rf7  = base red   (uniform 0)
    "nop ; nop ; ldunifrf.rf8",  // rf8  = base green (uniform 1)
    "nop ; nop ; ldunifrf.rf9",  // rf9  = base blue  (uniform 2)
    "nop ; nop ; ldunifrf.rf10", // rf10 = alpha (uniform 3) -- never fogged
    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf3/rf4/rf5 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf3",             // A
    "nop ; nop ; ldunifrf.rf4",             // B
    "nop ; fmul rf4, rf4, rf0",
    "fadd rf3, rf3, rf4 ; nop ; ldunifrf.rf4",             // linear factor
    "nop ; fmul rf4, rf4, rf0 ; ldunifrf.rf5",
    "nop ; fmul rf5, rf5, rf0",
    "nop ; fmul rf5, rf5, rf0",
    "fadd rf4, rf4, rf5 ; nop",             // C*c + D*c*c
    "or exp, rf4, rf4 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf4",             // M
    "nop ; fmul rf3, rf3, rf4",
    "or rf5, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf5, rf5, rf4 ; nop",             // 1-M
    "nop ; fmul rf5, rf5, r4",
    "fadd rf3, rf3, rf5 ; nop",             // fog factor
    "sub rf4, rf4, rf4 ; nop",
    "fmax rf3, rf3, rf4 ; nop",
    "or rf4, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf3, rf3, rf4 ; nop ; ldunifrf.rf4",             // clamped to [0,1]
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop ; ldunifrf.rf15",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "fcmp.pushc -, rf10, rf15 ; nop",
    "setmsf.ifna -, 0 ; nop",
    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- satisfies the >=3-instruction gap before the next thrsw
    /* Passthrough Z write: the FEP must stop writing depth because the
     * QPU does it here instead, so a discarded fragment leaves the depth
     * buffer alone. Placed after the setmsf discard and before the first
     * vfpack tlb -- the only legal slot -- and at least 3 ticks after the
     * first thrsw of the last-thrsw pair. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * Combined fog + alpha test (GL_LEQUAL), textured. Spliced from the
 * textured fog shader: the fog math, the fog/base register pairing
 * and the thrsw layout are copied VERBATIM so a combined draw fogs
 * identically to a fog-only draw. Added on top: the alpha threshold
 * uniform (rf15), the compare + setmsf discard, and the passthrough Z
 * write.
 *
 * draw.c writes the colour, fog and alpha_ref words in that order and the
 * TLB config word last, so tlbu picks up the config and not a colour value.
 */
static const char* g_fragment_shader_textured_fog_alphatest_lequal_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)

    /* COLOUR MODULATION -- see
     * g_fragment_shader_textured_alphatest_assembly for the derivation and
     * g_fragment_shader_textured_fog_assembly for why it sits before the fog
     * block. Fog rewrites rf7-rf9 only, so the alpha modulated here survives to
     * the blend unit. rf5 is dead after the TMU requests above. */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf4.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf8, rf4.h, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf9, rf3.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf10, rf3.h, rf5 ; ldunifrf.rf11",

    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf11/rf12/rf13 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // linear factor
    "nop ; fmul rf12, rf12, rf0 ; ldunifrf.rf13",
    "nop ; fmul rf13, rf13, rf0",
    "nop ; fmul rf13, rf13, rf0",
    "fadd rf12, rf12, rf13 ; nop",             // C*c + D*c*c
    "or exp, rf12, rf12 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf12",             // M
    "nop ; fmul rf11, rf11, rf12",
    "or rf13, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf13, rf13, rf12 ; nop",             // 1-M
    "nop ; fmul rf13, rf13, r4",
    "fadd rf11, rf11, rf13 ; nop",             // fog factor
    "sub rf12, rf12, rf12 ; nop",
    "fmax rf11, rf11, rf12 ; nop",
    "or rf12, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // clamped to [0,1]
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop ; ldunifrf.rf15",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "fcmp.pushc -, rf10, rf15 ; nop",
    "setmsf.ifna -, 0 ; nop",
    /* Passthrough Z write: the FEP must stop writing depth because the
     * QPU does it here instead, so a discarded fragment leaves the depth
     * buffer alone. Placed after the setmsf discard and before the first
     * vfpack tlb -- the only legal slot -- and at least 3 ticks after the
     * first thrsw of the last-thrsw pair. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // final thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * Combined fog + alpha test (GL_NOTEQUAL), untextured. Spliced from the
 * untextured fog shader: the fog math, the fog/base register pairing
 * and the thrsw layout are copied VERBATIM so a combined draw fogs
 * identically to a fog-only draw. Added on top: the alpha threshold
 * uniform (rf15), the compare + setmsf discard, and the passthrough Z
 * write.
 *
 * draw.c writes the colour, fog and alpha_ref words in that order and the
 * TLB config word last, so tlbu picks up the config and not a colour value.
 */
static const char* g_fragment_shader_untextured_fog_alphatest_notequal_assembly[] = {
    "nop ; nop ; ldunifrf.rf7",  // rf7  = base red   (uniform 0)
    "nop ; nop ; ldunifrf.rf8",  // rf8  = base green (uniform 1)
    "nop ; nop ; ldunifrf.rf9",  // rf9  = base blue  (uniform 2)
    "nop ; nop ; ldunifrf.rf10", // rf10 = alpha (uniform 3) -- never fogged
    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf3/rf4/rf5 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf3",             // A
    "nop ; nop ; ldunifrf.rf4",             // B
    "nop ; fmul rf4, rf4, rf0",
    "fadd rf3, rf3, rf4 ; nop ; ldunifrf.rf4",             // linear factor
    "nop ; fmul rf4, rf4, rf0 ; ldunifrf.rf5",
    "nop ; fmul rf5, rf5, rf0",
    "nop ; fmul rf5, rf5, rf0",
    "fadd rf4, rf4, rf5 ; nop",             // C*c + D*c*c
    "or exp, rf4, rf4 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf4",             // M
    "nop ; fmul rf3, rf3, rf4",
    "or rf5, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf5, rf5, rf4 ; nop",             // 1-M
    "nop ; fmul rf5, rf5, r4",
    "fadd rf3, rf3, rf5 ; nop",             // fog factor
    "sub rf4, rf4, rf4 ; nop",
    "fmax rf3, rf3, rf4 ; nop",
    "or rf4, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf3, rf3, rf4 ; nop ; ldunifrf.rf4",             // clamped to [0,1]
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop ; ldunifrf.rf15",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "fcmp.pushz -, rf15, rf10 ; nop",
    "setmsf.ifa -, 0 ; nop",
    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- satisfies the >=3-instruction gap before the next thrsw
    /* Passthrough Z write: the FEP must stop writing depth because the
     * QPU does it here instead, so a discarded fragment leaves the depth
     * buffer alone. Placed after the setmsf discard and before the first
     * vfpack tlb -- the only legal slot -- and at least 3 ticks after the
     * first thrsw of the last-thrsw pair. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * Combined fog + alpha test (GL_NOTEQUAL), textured. Spliced from the
 * textured fog shader: the fog math, the fog/base register pairing
 * and the thrsw layout are copied VERBATIM so a combined draw fogs
 * identically to a fog-only draw. Added on top: the alpha threshold
 * uniform (rf15), the compare + setmsf discard, and the passthrough Z
 * write.
 *
 * draw.c writes the colour, fog and alpha_ref words in that order and the
 * TLB config word last, so tlbu picks up the config and not a colour value.
 */
static const char* g_fragment_shader_textured_fog_alphatest_notequal_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)

    /* COLOUR MODULATION -- see
     * g_fragment_shader_textured_alphatest_assembly for the derivation and
     * g_fragment_shader_textured_fog_assembly for why it sits before the fog
     * block. Fog rewrites rf7-rf9 only, so the alpha modulated here survives to
     * the blend unit. rf5 is dead after the TMU requests above. */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf4.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf8, rf4.h, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf9, rf3.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf10, rf3.h, rf5 ; ldunifrf.rf11",

    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf11/rf12/rf13 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // linear factor
    "nop ; fmul rf12, rf12, rf0 ; ldunifrf.rf13",
    "nop ; fmul rf13, rf13, rf0",
    "nop ; fmul rf13, rf13, rf0",
    "fadd rf12, rf12, rf13 ; nop",             // C*c + D*c*c
    "or exp, rf12, rf12 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf12",             // M
    "nop ; fmul rf11, rf11, rf12",
    "or rf13, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf13, rf13, rf12 ; nop",             // 1-M
    "nop ; fmul rf13, rf13, r4",
    "fadd rf11, rf11, rf13 ; nop",             // fog factor
    "sub rf12, rf12, rf12 ; nop",
    "fmax rf11, rf11, rf12 ; nop",
    "or rf12, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // clamped to [0,1]
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop ; ldunifrf.rf15",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "fcmp.pushz -, rf15, rf10 ; nop",
    "setmsf.ifa -, 0 ; nop",
    /* Passthrough Z write: the FEP must stop writing depth because the
     * QPU does it here instead, so a discarded fragment leaves the depth
     * buffer alone. Placed after the setmsf discard and before the first
     * vfpack tlb -- the only legal slot -- and at least 3 ticks after the
     * first thrsw of the last-thrsw pair. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // final thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * Combined fog + alpha test (GL_NEVER), untextured. Spliced from the
 * untextured fog shader: the fog math, the fog/base register pairing
 * and the thrsw layout are copied VERBATIM so a combined draw fogs
 * identically to a fog-only draw. Added on top: the alpha threshold
 * uniform (rf15), the compare + setmsf discard, and the passthrough Z
 * write.
 *
 * draw.c writes the colour, fog and alpha_ref words in that order and the
 * TLB config word last, so tlbu picks up the config and not a colour value.
 */
static const char* g_fragment_shader_untextured_fog_alphatest_never_assembly[] = {
    "nop ; nop ; ldunifrf.rf7",  // rf7  = base red   (uniform 0)
    "nop ; nop ; ldunifrf.rf8",  // rf8  = base green (uniform 1)
    "nop ; nop ; ldunifrf.rf9",  // rf9  = base blue  (uniform 2)
    "nop ; nop ; ldunifrf.rf10", // rf10 = alpha (uniform 3) -- never fogged
    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf3/rf4/rf5 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf3",             // A
    "nop ; nop ; ldunifrf.rf4",             // B
    "nop ; fmul rf4, rf4, rf0",
    "fadd rf3, rf3, rf4 ; nop ; ldunifrf.rf4",             // linear factor
    "nop ; fmul rf4, rf4, rf0 ; ldunifrf.rf5",
    "nop ; fmul rf5, rf5, rf0",
    "nop ; fmul rf5, rf5, rf0",
    "fadd rf4, rf4, rf5 ; nop",             // C*c + D*c*c
    "or exp, rf4, rf4 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf4",             // M
    "nop ; fmul rf3, rf3, rf4",
    "or rf5, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf5, rf5, rf4 ; nop",             // 1-M
    "nop ; fmul rf5, rf5, r4",
    "fadd rf3, rf3, rf5 ; nop",             // fog factor
    "sub rf4, rf4, rf4 ; nop",
    "fmax rf3, rf3, rf4 ; nop",
    "or rf4, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf3, rf3, rf4 ; nop ; ldunifrf.rf4",             // clamped to [0,1]
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop ; ldunifrf.rf15",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "setmsf -, 0 ; nop",
    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- satisfies the >=3-instruction gap before the next thrsw
    /* Passthrough Z write: the FEP must stop writing depth because the
     * QPU does it here instead, so a discarded fragment leaves the depth
     * buffer alone. Placed after the setmsf discard and before the first
     * vfpack tlb -- the only legal slot -- and at least 3 ticks after the
     * first thrsw of the last-thrsw pair. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * Combined fog + alpha test (GL_NEVER), textured. Spliced from the
 * textured fog shader: the fog math, the fog/base register pairing
 * and the thrsw layout are copied VERBATIM so a combined draw fogs
 * identically to a fog-only draw. Added on top: the alpha threshold
 * uniform (rf15), the compare + setmsf discard, and the passthrough Z
 * write.
 *
 * draw.c writes the colour, fog and alpha_ref words in that order and the
 * TLB config word last, so tlbu picks up the config and not a colour value.
 */
static const char* g_fragment_shader_textured_fog_alphatest_never_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)

    /* COLOUR MODULATION -- see
     * g_fragment_shader_textured_alphatest_assembly for the derivation and
     * g_fragment_shader_textured_fog_assembly for why it sits before the fog
     * block. Fog rewrites rf7-rf9 only, so the alpha modulated here survives to
     * the blend unit. rf5 is dead after the TMU requests above. */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf4.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf8, rf4.h, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf9, rf3.l, rf5 ; ldunifrf.rf5",
    "nop ; fmul rf10, rf3.h, rf5 ; ldunifrf.rf11",

    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf11/rf12/rf13 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // linear factor
    "nop ; fmul rf12, rf12, rf0 ; ldunifrf.rf13",
    "nop ; fmul rf13, rf13, rf0",
    "nop ; fmul rf13, rf13, rf0",
    "fadd rf12, rf12, rf13 ; nop",             // C*c + D*c*c
    "or exp, rf12, rf12 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf12",             // M
    "nop ; fmul rf11, rf11, rf12",
    "or rf13, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf13, rf13, rf12 ; nop",             // 1-M
    "nop ; fmul rf13, rf13, r4",
    "fadd rf11, rf11, rf13 ; nop",             // fog factor
    "sub rf12, rf12, rf12 ; nop",
    "fmax rf11, rf11, rf12 ; nop",
    "or rf12, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // clamped to [0,1]
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop ; ldunifrf.rf15",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "setmsf -, 0 ; nop",
    /* Passthrough Z write: the FEP must stop writing depth because the
     * QPU does it here instead, so a discarded fragment leaves the depth
     * buffer alone. Placed after the setmsf discard and before the first
     * vfpack tlb -- the only legal slot -- and at least 3 ticks after the
     * first thrsw of the last-thrsw pair. Consumes the 0xffffff84 TLB
     * depth-config word draw.c appends to the fragment uniform stream. */
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // final thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/* ==================================================================
 * SMOOTH FOG
 *
 * These 9 variants cover the smooth shape: untextured, textured, and
 * textured with each of the 7 alpha compare functions. There is no
 * untextured smooth alphatest shader to pair with -- alpha test on an
 * untextured smooth draw matches no predicate at all, a separate gap.
 * ================================================================== */

/*
 * Untextured + smooth + fog. Colour comes from the varyings (rf7..rf10) as in
 * the plain smooth shader; the fog lerp is copied verbatim from the
 * untextured fog shader and operates on the same rf7/rf8/rf9.
 */
static const char* g_fragment_shader_untextured_smooth_fog_assembly[] = {
    "nop ; nop ; ldvary.r0",   // load r/w
    "nop ; fmul r1, r0, rf0",  // r1 = (r/w) * w
    "fadd rf7, r1, r5 ; nop", // rf7 = true red
    "nop ; nop ; ldvary.r0",   // load g/w
    "nop ; fmul r1, r0, rf0",  // r1 = (g/w) * w
    "fadd rf8, r1, r5 ; nop", // rf8 = true green
    "nop ; nop ; ldvary.r0",   // load b/w
    "nop ; fmul r1, r0, rf0",  // r1 = (b/w) * w
    "fadd rf9, r1, r5 ; nop", // rf9 = true blue
    "nop ; nop ; ldvary.r0",   // load a/w
    "nop ; fmul r1, r0, rf0",  // r1 = (a/w) * w
    "fadd rf10, r1, r5 ; nop ; ldunifrf.rf24", // rf10 = true alpha
    /* draw.c writes 4 fixed-colour words for EVERY untextured draw,
     * including smooth ones that take their colour from varyings. They
     * are unused here, but ldunifrf is sequential: without consuming them
     * the fog loads below would read col[0..3] instead of the fog values.
     * rf24 is scratch -- nothing in this shader reads it. */
    "nop ; nop ; ldunifrf.rf24", // consume col[1], unused
    "nop ; nop ; ldunifrf.rf24", // consume col[2], unused
    "nop ; nop ; ldunifrf.rf24", // consume col[3], unused
    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf3/rf4/rf5 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf3",             // A
    "nop ; nop ; ldunifrf.rf4",             // B
    "nop ; fmul rf4, rf4, rf0",
    "fadd rf3, rf3, rf4 ; nop ; ldunifrf.rf4",             // linear factor
    "nop ; fmul rf4, rf4, rf0 ; ldunifrf.rf5",
    "nop ; fmul rf5, rf5, rf0",
    "nop ; fmul rf5, rf5, rf0",
    "fadd rf4, rf4, rf5 ; nop",             // C*c + D*c*c
    "or exp, rf4, rf4 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf4",             // M
    "nop ; fmul rf3, rf3, rf4",
    "or rf5, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf5, rf5, rf4 ; nop",             // 1-M
    "nop ; fmul rf5, rf5, r4",
    "fadd rf3, rf3, rf5 ; nop",             // fog factor
    "sub rf4, rf4, rf4 ; nop",
    "fmax rf3, rf3, rf4 ; nop",
    "or rf4, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf3, rf3, rf4 ; nop ; ldunifrf.rf4",             // clamped to [0,1]
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop",
    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- satisfies the >=3-instruction gap before the next thrsw
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * UNTEXTURED SMOOTH + ALPHA TEST, seven compare functions, without and with fog.
 * Each is the untextured smooth (or smooth fog) shader with the threshold read
 * and that function's discard block spliced in, lifted from the FLAT untextured
 * family. The four ldunifrf.rf24 reads step over the flat-colour words the
 * untextured uniform branch writes whatever the shade model is, exactly as
 * untextured_smooth_point_smooth does.
 */
static const char* g_fragment_shader_untextured_smooth_alphatest_assembly[] = {
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf7, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf8, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf9, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf10, r1, r5 ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf15",
    "fcmp.pushc -, rf15, rf10 ; nop",
    "setmsf.ifna -, 0 ; nop",
    "nop ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "nop ; nop",
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

static const char* g_fragment_shader_untextured_smooth_alphatest_greater_assembly[] = {
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf7, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf8, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf9, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf10, r1, r5 ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf15",
    "fcmp.pushc -, rf10, rf15 ; nop",
    "setmsf.ifa -, 0 ; nop",
    "nop ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "nop ; nop",
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

static const char* g_fragment_shader_untextured_smooth_alphatest_less_assembly[] = {
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf7, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf8, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf9, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf10, r1, r5 ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf15",
    "fcmp.pushc -, rf15, rf10 ; nop",
    "setmsf.ifa -, 0 ; nop",
    "nop ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "nop ; nop",
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

static const char* g_fragment_shader_untextured_smooth_alphatest_equal_assembly[] = {
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf7, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf8, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf9, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf10, r1, r5 ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf15",
    "fcmp.pushz -, rf15, rf10 ; nop",
    "setmsf.ifna -, 0 ; nop",
    "nop ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "nop ; nop",
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

static const char* g_fragment_shader_untextured_smooth_alphatest_lequal_assembly[] = {
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf7, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf8, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf9, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf10, r1, r5 ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf15",
    "fcmp.pushc -, rf10, rf15 ; nop",
    "setmsf.ifna -, 0 ; nop",
    "nop ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "nop ; nop",
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

static const char* g_fragment_shader_untextured_smooth_alphatest_notequal_assembly[] = {
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf7, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf8, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf9, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf10, r1, r5 ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf15",
    "fcmp.pushz -, rf15, rf10 ; nop",
    "setmsf.ifa -, 0 ; nop",
    "nop ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "nop ; nop",
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

static const char* g_fragment_shader_untextured_smooth_alphatest_never_assembly[] = {
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf7, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf8, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf9, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf10, r1, r5 ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf15",
    "setmsf -, 0 ; nop",
    "nop ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "nop ; nop",
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

static const char* g_fragment_shader_untextured_smooth_fog_alphatest_assembly[] = {
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf7, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf8, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf9, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf10, r1, r5 ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf3",
    "nop ; nop ; ldunifrf.rf4",
    "nop ; fmul rf4, rf4, rf0",
    "fadd rf3, rf3, rf4 ; nop ; ldunifrf.rf4",
    "nop ; fmul rf4, rf4, rf0 ; ldunifrf.rf5",
    "nop ; fmul rf5, rf5, rf0",
    "nop ; fmul rf5, rf5, rf0",
    "fadd rf4, rf4, rf5 ; nop",
    "or exp, rf4, rf4 ; nop",
    "nop ; nop",
    "nop ; nop ; ldunifrf.rf4",
    "nop ; fmul rf3, rf3, rf4",
    "or rf5, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf5, rf5, rf4 ; nop",
    "nop ; fmul rf5, rf5, r4",
    "fadd rf3, rf3, rf5 ; nop",
    "sub rf4, rf4, rf4 ; nop",
    "fmax rf3, rf3, rf4 ; nop",
    "or rf4, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf3, rf3, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop ; ldunifrf.rf15",
    "fcmp.pushc -, rf15, rf10 ; nop",
    "setmsf.ifna -, 0 ; nop",
    "nop ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "nop ; nop",
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

static const char* g_fragment_shader_untextured_smooth_fog_alphatest_greater_assembly[] = {
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf7, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf8, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf9, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf10, r1, r5 ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf3",
    "nop ; nop ; ldunifrf.rf4",
    "nop ; fmul rf4, rf4, rf0",
    "fadd rf3, rf3, rf4 ; nop ; ldunifrf.rf4",
    "nop ; fmul rf4, rf4, rf0 ; ldunifrf.rf5",
    "nop ; fmul rf5, rf5, rf0",
    "nop ; fmul rf5, rf5, rf0",
    "fadd rf4, rf4, rf5 ; nop",
    "or exp, rf4, rf4 ; nop",
    "nop ; nop",
    "nop ; nop ; ldunifrf.rf4",
    "nop ; fmul rf3, rf3, rf4",
    "or rf5, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf5, rf5, rf4 ; nop",
    "nop ; fmul rf5, rf5, r4",
    "fadd rf3, rf3, rf5 ; nop",
    "sub rf4, rf4, rf4 ; nop",
    "fmax rf3, rf3, rf4 ; nop",
    "or rf4, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf3, rf3, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop ; ldunifrf.rf15",
    "fcmp.pushc -, rf10, rf15 ; nop",
    "setmsf.ifa -, 0 ; nop",
    "nop ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "nop ; nop",
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

static const char* g_fragment_shader_untextured_smooth_fog_alphatest_less_assembly[] = {
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf7, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf8, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf9, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf10, r1, r5 ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf3",
    "nop ; nop ; ldunifrf.rf4",
    "nop ; fmul rf4, rf4, rf0",
    "fadd rf3, rf3, rf4 ; nop ; ldunifrf.rf4",
    "nop ; fmul rf4, rf4, rf0 ; ldunifrf.rf5",
    "nop ; fmul rf5, rf5, rf0",
    "nop ; fmul rf5, rf5, rf0",
    "fadd rf4, rf4, rf5 ; nop",
    "or exp, rf4, rf4 ; nop",
    "nop ; nop",
    "nop ; nop ; ldunifrf.rf4",
    "nop ; fmul rf3, rf3, rf4",
    "or rf5, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf5, rf5, rf4 ; nop",
    "nop ; fmul rf5, rf5, r4",
    "fadd rf3, rf3, rf5 ; nop",
    "sub rf4, rf4, rf4 ; nop",
    "fmax rf3, rf3, rf4 ; nop",
    "or rf4, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf3, rf3, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop ; ldunifrf.rf15",
    "fcmp.pushc -, rf15, rf10 ; nop",
    "setmsf.ifa -, 0 ; nop",
    "nop ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "nop ; nop",
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

static const char* g_fragment_shader_untextured_smooth_fog_alphatest_equal_assembly[] = {
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf7, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf8, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf9, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf10, r1, r5 ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf3",
    "nop ; nop ; ldunifrf.rf4",
    "nop ; fmul rf4, rf4, rf0",
    "fadd rf3, rf3, rf4 ; nop ; ldunifrf.rf4",
    "nop ; fmul rf4, rf4, rf0 ; ldunifrf.rf5",
    "nop ; fmul rf5, rf5, rf0",
    "nop ; fmul rf5, rf5, rf0",
    "fadd rf4, rf4, rf5 ; nop",
    "or exp, rf4, rf4 ; nop",
    "nop ; nop",
    "nop ; nop ; ldunifrf.rf4",
    "nop ; fmul rf3, rf3, rf4",
    "or rf5, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf5, rf5, rf4 ; nop",
    "nop ; fmul rf5, rf5, r4",
    "fadd rf3, rf3, rf5 ; nop",
    "sub rf4, rf4, rf4 ; nop",
    "fmax rf3, rf3, rf4 ; nop",
    "or rf4, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf3, rf3, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop ; ldunifrf.rf15",
    "fcmp.pushz -, rf15, rf10 ; nop",
    "setmsf.ifna -, 0 ; nop",
    "nop ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "nop ; nop",
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

static const char* g_fragment_shader_untextured_smooth_fog_alphatest_lequal_assembly[] = {
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf7, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf8, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf9, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf10, r1, r5 ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf3",
    "nop ; nop ; ldunifrf.rf4",
    "nop ; fmul rf4, rf4, rf0",
    "fadd rf3, rf3, rf4 ; nop ; ldunifrf.rf4",
    "nop ; fmul rf4, rf4, rf0 ; ldunifrf.rf5",
    "nop ; fmul rf5, rf5, rf0",
    "nop ; fmul rf5, rf5, rf0",
    "fadd rf4, rf4, rf5 ; nop",
    "or exp, rf4, rf4 ; nop",
    "nop ; nop",
    "nop ; nop ; ldunifrf.rf4",
    "nop ; fmul rf3, rf3, rf4",
    "or rf5, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf5, rf5, rf4 ; nop",
    "nop ; fmul rf5, rf5, r4",
    "fadd rf3, rf3, rf5 ; nop",
    "sub rf4, rf4, rf4 ; nop",
    "fmax rf3, rf3, rf4 ; nop",
    "or rf4, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf3, rf3, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop ; ldunifrf.rf15",
    "fcmp.pushc -, rf10, rf15 ; nop",
    "setmsf.ifna -, 0 ; nop",
    "nop ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "nop ; nop",
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

static const char* g_fragment_shader_untextured_smooth_fog_alphatest_notequal_assembly[] = {
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf7, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf8, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf9, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf10, r1, r5 ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf3",
    "nop ; nop ; ldunifrf.rf4",
    "nop ; fmul rf4, rf4, rf0",
    "fadd rf3, rf3, rf4 ; nop ; ldunifrf.rf4",
    "nop ; fmul rf4, rf4, rf0 ; ldunifrf.rf5",
    "nop ; fmul rf5, rf5, rf0",
    "nop ; fmul rf5, rf5, rf0",
    "fadd rf4, rf4, rf5 ; nop",
    "or exp, rf4, rf4 ; nop",
    "nop ; nop",
    "nop ; nop ; ldunifrf.rf4",
    "nop ; fmul rf3, rf3, rf4",
    "or rf5, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf5, rf5, rf4 ; nop",
    "nop ; fmul rf5, rf5, r4",
    "fadd rf3, rf3, rf5 ; nop",
    "sub rf4, rf4, rf4 ; nop",
    "fmax rf3, rf3, rf4 ; nop",
    "or rf4, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf3, rf3, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop ; ldunifrf.rf15",
    "fcmp.pushz -, rf15, rf10 ; nop",
    "setmsf.ifa -, 0 ; nop",
    "nop ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "nop ; nop",
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

static const char* g_fragment_shader_untextured_smooth_fog_alphatest_never_assembly[] = {
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf7, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf8, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf9, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf10, r1, r5 ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf24",
    "nop ; nop ; ldunifrf.rf3",
    "nop ; nop ; ldunifrf.rf4",
    "nop ; fmul rf4, rf4, rf0",
    "fadd rf3, rf3, rf4 ; nop ; ldunifrf.rf4",
    "nop ; fmul rf4, rf4, rf0 ; ldunifrf.rf5",
    "nop ; fmul rf5, rf5, rf0",
    "nop ; fmul rf5, rf5, rf0",
    "fadd rf4, rf4, rf5 ; nop",
    "or exp, rf4, rf4 ; nop",
    "nop ; nop",
    "nop ; nop ; ldunifrf.rf4",
    "nop ; fmul rf3, rf3, rf4",
    "or rf5, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf5, rf5, rf4 ; nop",
    "nop ; fmul rf5, rf5, r4",
    "fadd rf3, rf3, rf5 ; nop",
    "sub rf4, rf4, rf4 ; nop",
    "fmax rf3, rf3, rf4 ; nop",
    "or rf4, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf3, rf3, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop ; ldunifrf.rf4",
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop ; ldunifrf.rf15",
    "setmsf -, 0 ; nop",
    "nop ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "nop ; nop",
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * Textured + smooth + fog. The texel x vertex-colour modulate is untouched;
 * fog is applied to its result, so it composes exactly as the flat textured
 * fog shader does.
 */
static const char* g_fragment_shader_textured_smooth_fog_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)
    "nop ; nop ; ldvary.r0",    // load r/w
    "nop ; fmul r1, r0, rf0",   // r1 = r/w * w
    "fadd rf20, r1, r5 ; nop", // rf20 = true vertex red
    "nop ; nop ; ldvary.r0",    // load g/w
    "nop ; fmul r1, r0, rf0",   // r1 = g/w * w
    "fadd rf21, r1, r5 ; nop", // rf21 = true vertex green
    "nop ; nop ; ldvary.r0",    // load b/w
    "nop ; fmul r1, r0, rf0",   // r1 = b/w * w
    "fadd rf22, r1, r5 ; fmul rf8, rf4.h, rf21", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; fmul rf7, rf4.l, rf20", // rf23 = true vertex alpha
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23 ; ldunifrf.rf11", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf11/rf12/rf13 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // linear factor
    "nop ; fmul rf12, rf12, rf0 ; ldunifrf.rf13",
    "nop ; fmul rf13, rf13, rf0",
    "nop ; fmul rf13, rf13, rf0",
    "fadd rf12, rf12, rf13 ; nop",             // C*c + D*c*c
    "or exp, rf12, rf12 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf12",             // M
    "nop ; fmul rf11, rf11, rf12",
    "or rf13, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf13, rf13, rf12 ; nop",             // 1-M
    "nop ; fmul rf13, rf13, r4",
    "fadd rf11, rf11, rf13 ; nop",             // fog factor
    "sub rf12, rf12, rf12 ; nop",
    "fmax rf11, rf11, rf12 ; nop",
    "or rf12, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // clamped to [0,1]
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * Textured + smooth + fog + alpha test (GL_GEQUAL).
 * Fog never touches rf10, so the alpha operand stays valid where the
 * discard reads it.
 */
static const char* g_fragment_shader_textured_smooth_fog_alphatest_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)
    "nop ; nop ; ldvary.r0",    // load r/w
    "nop ; fmul r1, r0, rf0",   // r1 = r/w * w
    "fadd rf20, r1, r5 ; nop", // rf20 = true vertex red
    "nop ; nop ; ldvary.r0",    // load g/w
    "nop ; fmul r1, r0, rf0",   // r1 = g/w * w
    "fadd rf21, r1, r5 ; nop", // rf21 = true vertex green
    "nop ; nop ; ldvary.r0",    // load b/w
    "nop ; fmul r1, r0, rf0",   // r1 = b/w * w
    "fadd rf22, r1, r5 ; fmul rf8, rf4.h, rf21", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; fmul rf7, rf4.l, rf20", // rf23 = true vertex alpha
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23 ; ldunifrf.rf11", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf11/rf12/rf13 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // linear factor
    "nop ; fmul rf12, rf12, rf0 ; ldunifrf.rf13",
    "nop ; fmul rf13, rf13, rf0",
    "nop ; fmul rf13, rf13, rf0",
    "fadd rf12, rf12, rf13 ; nop",             // C*c + D*c*c
    "or exp, rf12, rf12 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf12",             // M
    "nop ; fmul rf11, rf11, rf12",
    "or rf13, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf13, rf13, rf12 ; nop",             // 1-M
    "nop ; fmul rf13, rf13, r4",
    "fadd rf11, rf11, rf13 ; nop",             // fog factor
    "sub rf12, rf12, rf12 ; nop",
    "fmax rf11, rf11, rf12 ; nop",
    "or rf12, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // clamped to [0,1]
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop ; ldunifrf.rf15",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "fcmp.pushc -, rf15, rf10 ; nop",
    "setmsf.ifna -, 0 ; nop",
    /* Passthrough Z write: the QPU takes over the depth write so a
     * discarded fragment leaves the depth buffer alone. After the
     * setmsf, before the first vfpack tlb, and >= 3 ticks past the
     * first thrsw of the last-thrsw pair. Consumes the 0xffffff84
     * TLB depth-config word draw.c appends to the uniform stream. */
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * Textured + smooth + fog + alpha test (GL_GREATER).
 * Fog never touches rf10, so the alpha operand stays valid where the
 * discard reads it.
 */
static const char* g_fragment_shader_textured_smooth_fog_alphatest_greater_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)
    "nop ; nop ; ldvary.r0",    // load r/w
    "nop ; fmul r1, r0, rf0",   // r1 = r/w * w
    "fadd rf20, r1, r5 ; nop", // rf20 = true vertex red
    "nop ; nop ; ldvary.r0",    // load g/w
    "nop ; fmul r1, r0, rf0",   // r1 = g/w * w
    "fadd rf21, r1, r5 ; nop", // rf21 = true vertex green
    "nop ; nop ; ldvary.r0",    // load b/w
    "nop ; fmul r1, r0, rf0",   // r1 = b/w * w
    "fadd rf22, r1, r5 ; fmul rf8, rf4.h, rf21", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; fmul rf7, rf4.l, rf20", // rf23 = true vertex alpha
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23 ; ldunifrf.rf11", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf11/rf12/rf13 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // linear factor
    "nop ; fmul rf12, rf12, rf0 ; ldunifrf.rf13",
    "nop ; fmul rf13, rf13, rf0",
    "nop ; fmul rf13, rf13, rf0",
    "fadd rf12, rf12, rf13 ; nop",             // C*c + D*c*c
    "or exp, rf12, rf12 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf12",             // M
    "nop ; fmul rf11, rf11, rf12",
    "or rf13, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf13, rf13, rf12 ; nop",             // 1-M
    "nop ; fmul rf13, rf13, r4",
    "fadd rf11, rf11, rf13 ; nop",             // fog factor
    "sub rf12, rf12, rf12 ; nop",
    "fmax rf11, rf11, rf12 ; nop",
    "or rf12, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // clamped to [0,1]
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop ; ldunifrf.rf15",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "fcmp.pushc -, rf10, rf15 ; nop",
    "setmsf.ifa -, 0 ; nop",
    /* Passthrough Z write: the QPU takes over the depth write so a
     * discarded fragment leaves the depth buffer alone. After the
     * setmsf, before the first vfpack tlb, and >= 3 ticks past the
     * first thrsw of the last-thrsw pair. Consumes the 0xffffff84
     * TLB depth-config word draw.c appends to the uniform stream. */
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * Textured + smooth + fog + alpha test (GL_LESS).
 * Fog never touches rf10, so the alpha operand stays valid where the
 * discard reads it.
 */
static const char* g_fragment_shader_textured_smooth_fog_alphatest_less_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)
    "nop ; nop ; ldvary.r0",    // load r/w
    "nop ; fmul r1, r0, rf0",   // r1 = r/w * w
    "fadd rf20, r1, r5 ; nop", // rf20 = true vertex red
    "nop ; nop ; ldvary.r0",    // load g/w
    "nop ; fmul r1, r0, rf0",   // r1 = g/w * w
    "fadd rf21, r1, r5 ; nop", // rf21 = true vertex green
    "nop ; nop ; ldvary.r0",    // load b/w
    "nop ; fmul r1, r0, rf0",   // r1 = b/w * w
    "fadd rf22, r1, r5 ; fmul rf8, rf4.h, rf21", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; fmul rf7, rf4.l, rf20", // rf23 = true vertex alpha
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23 ; ldunifrf.rf11", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf11/rf12/rf13 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // linear factor
    "nop ; fmul rf12, rf12, rf0 ; ldunifrf.rf13",
    "nop ; fmul rf13, rf13, rf0",
    "nop ; fmul rf13, rf13, rf0",
    "fadd rf12, rf12, rf13 ; nop",             // C*c + D*c*c
    "or exp, rf12, rf12 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf12",             // M
    "nop ; fmul rf11, rf11, rf12",
    "or rf13, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf13, rf13, rf12 ; nop",             // 1-M
    "nop ; fmul rf13, rf13, r4",
    "fadd rf11, rf11, rf13 ; nop",             // fog factor
    "sub rf12, rf12, rf12 ; nop",
    "fmax rf11, rf11, rf12 ; nop",
    "or rf12, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // clamped to [0,1]
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop ; ldunifrf.rf15",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "fcmp.pushc -, rf15, rf10 ; nop",
    "setmsf.ifa -, 0 ; nop",
    /* Passthrough Z write: the QPU takes over the depth write so a
     * discarded fragment leaves the depth buffer alone. After the
     * setmsf, before the first vfpack tlb, and >= 3 ticks past the
     * first thrsw of the last-thrsw pair. Consumes the 0xffffff84
     * TLB depth-config word draw.c appends to the uniform stream. */
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * Textured + smooth + fog + alpha test (GL_EQUAL).
 * Fog never touches rf10, so the alpha operand stays valid where the
 * discard reads it.
 */
static const char* g_fragment_shader_textured_smooth_fog_alphatest_equal_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)
    "nop ; nop ; ldvary.r0",    // load r/w
    "nop ; fmul r1, r0, rf0",   // r1 = r/w * w
    "fadd rf20, r1, r5 ; nop", // rf20 = true vertex red
    "nop ; nop ; ldvary.r0",    // load g/w
    "nop ; fmul r1, r0, rf0",   // r1 = g/w * w
    "fadd rf21, r1, r5 ; nop", // rf21 = true vertex green
    "nop ; nop ; ldvary.r0",    // load b/w
    "nop ; fmul r1, r0, rf0",   // r1 = b/w * w
    "fadd rf22, r1, r5 ; fmul rf8, rf4.h, rf21", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; fmul rf7, rf4.l, rf20", // rf23 = true vertex alpha
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23 ; ldunifrf.rf11", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf11/rf12/rf13 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // linear factor
    "nop ; fmul rf12, rf12, rf0 ; ldunifrf.rf13",
    "nop ; fmul rf13, rf13, rf0",
    "nop ; fmul rf13, rf13, rf0",
    "fadd rf12, rf12, rf13 ; nop",             // C*c + D*c*c
    "or exp, rf12, rf12 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf12",             // M
    "nop ; fmul rf11, rf11, rf12",
    "or rf13, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf13, rf13, rf12 ; nop",             // 1-M
    "nop ; fmul rf13, rf13, r4",
    "fadd rf11, rf11, rf13 ; nop",             // fog factor
    "sub rf12, rf12, rf12 ; nop",
    "fmax rf11, rf11, rf12 ; nop",
    "or rf12, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // clamped to [0,1]
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop ; ldunifrf.rf15",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "fcmp.pushz -, rf15, rf10 ; nop",
    "setmsf.ifna -, 0 ; nop",
    /* Passthrough Z write: the QPU takes over the depth write so a
     * discarded fragment leaves the depth buffer alone. After the
     * setmsf, before the first vfpack tlb, and >= 3 ticks past the
     * first thrsw of the last-thrsw pair. Consumes the 0xffffff84
     * TLB depth-config word draw.c appends to the uniform stream. */
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * Textured + smooth + fog + alpha test (GL_LEQUAL).
 * Fog never touches rf10, so the alpha operand stays valid where the
 * discard reads it.
 */
static const char* g_fragment_shader_textured_smooth_fog_alphatest_lequal_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)
    "nop ; nop ; ldvary.r0",    // load r/w
    "nop ; fmul r1, r0, rf0",   // r1 = r/w * w
    "fadd rf20, r1, r5 ; nop", // rf20 = true vertex red
    "nop ; nop ; ldvary.r0",    // load g/w
    "nop ; fmul r1, r0, rf0",   // r1 = g/w * w
    "fadd rf21, r1, r5 ; nop", // rf21 = true vertex green
    "nop ; nop ; ldvary.r0",    // load b/w
    "nop ; fmul r1, r0, rf0",   // r1 = b/w * w
    "fadd rf22, r1, r5 ; fmul rf8, rf4.h, rf21", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; fmul rf7, rf4.l, rf20", // rf23 = true vertex alpha
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23 ; ldunifrf.rf11", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf11/rf12/rf13 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // linear factor
    "nop ; fmul rf12, rf12, rf0 ; ldunifrf.rf13",
    "nop ; fmul rf13, rf13, rf0",
    "nop ; fmul rf13, rf13, rf0",
    "fadd rf12, rf12, rf13 ; nop",             // C*c + D*c*c
    "or exp, rf12, rf12 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf12",             // M
    "nop ; fmul rf11, rf11, rf12",
    "or rf13, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf13, rf13, rf12 ; nop",             // 1-M
    "nop ; fmul rf13, rf13, r4",
    "fadd rf11, rf11, rf13 ; nop",             // fog factor
    "sub rf12, rf12, rf12 ; nop",
    "fmax rf11, rf11, rf12 ; nop",
    "or rf12, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // clamped to [0,1]
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop ; ldunifrf.rf15",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "fcmp.pushc -, rf10, rf15 ; nop",
    "setmsf.ifna -, 0 ; nop",
    /* Passthrough Z write: the QPU takes over the depth write so a
     * discarded fragment leaves the depth buffer alone. After the
     * setmsf, before the first vfpack tlb, and >= 3 ticks past the
     * first thrsw of the last-thrsw pair. Consumes the 0xffffff84
     * TLB depth-config word draw.c appends to the uniform stream. */
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * Textured + smooth + fog + alpha test (GL_NOTEQUAL).
 * Fog never touches rf10, so the alpha operand stays valid where the
 * discard reads it.
 */
static const char* g_fragment_shader_textured_smooth_fog_alphatest_notequal_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)
    "nop ; nop ; ldvary.r0",    // load r/w
    "nop ; fmul r1, r0, rf0",   // r1 = r/w * w
    "fadd rf20, r1, r5 ; nop", // rf20 = true vertex red
    "nop ; nop ; ldvary.r0",    // load g/w
    "nop ; fmul r1, r0, rf0",   // r1 = g/w * w
    "fadd rf21, r1, r5 ; nop", // rf21 = true vertex green
    "nop ; nop ; ldvary.r0",    // load b/w
    "nop ; fmul r1, r0, rf0",   // r1 = b/w * w
    "fadd rf22, r1, r5 ; fmul rf8, rf4.h, rf21", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; fmul rf7, rf4.l, rf20", // rf23 = true vertex alpha
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23 ; ldunifrf.rf11", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf11/rf12/rf13 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // linear factor
    "nop ; fmul rf12, rf12, rf0 ; ldunifrf.rf13",
    "nop ; fmul rf13, rf13, rf0",
    "nop ; fmul rf13, rf13, rf0",
    "fadd rf12, rf12, rf13 ; nop",             // C*c + D*c*c
    "or exp, rf12, rf12 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf12",             // M
    "nop ; fmul rf11, rf11, rf12",
    "or rf13, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf13, rf13, rf12 ; nop",             // 1-M
    "nop ; fmul rf13, rf13, r4",
    "fadd rf11, rf11, rf13 ; nop",             // fog factor
    "sub rf12, rf12, rf12 ; nop",
    "fmax rf11, rf11, rf12 ; nop",
    "or rf12, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // clamped to [0,1]
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop ; ldunifrf.rf15",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "fcmp.pushz -, rf15, rf10 ; nop",
    "setmsf.ifa -, 0 ; nop",
    /* Passthrough Z write: the QPU takes over the depth write so a
     * discarded fragment leaves the depth buffer alone. After the
     * setmsf, before the first vfpack tlb, and >= 3 ticks past the
     * first thrsw of the last-thrsw pair. Consumes the 0xffffff84
     * TLB depth-config word draw.c appends to the uniform stream. */
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * Textured + smooth + fog + alpha test (GL_NEVER).
 * Fog never touches rf10, so the alpha operand stays valid where the
 * discard reads it.
 */
static const char* g_fragment_shader_textured_smooth_fog_alphatest_never_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)
    "nop ; nop ; ldvary.r0",    // load r/w
    "nop ; fmul r1, r0, rf0",   // r1 = r/w * w
    "fadd rf20, r1, r5 ; nop", // rf20 = true vertex red
    "nop ; nop ; ldvary.r0",    // load g/w
    "nop ; fmul r1, r0, rf0",   // r1 = g/w * w
    "fadd rf21, r1, r5 ; nop", // rf21 = true vertex green
    "nop ; nop ; ldvary.r0",    // load b/w
    "nop ; fmul r1, r0, rf0",   // r1 = b/w * w
    "fadd rf22, r1, r5 ; fmul rf8, rf4.h, rf21", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; fmul rf7, rf4.l, rf20", // rf23 = true vertex alpha
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23 ; ldunifrf.rf11", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf11/rf12/rf13 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // linear factor
    "nop ; fmul rf12, rf12, rf0 ; ldunifrf.rf13",
    "nop ; fmul rf13, rf13, rf0",
    "nop ; fmul rf13, rf13, rf0",
    "fadd rf12, rf12, rf13 ; nop",             // C*c + D*c*c
    "or exp, rf12, rf12 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf12",             // M
    "nop ; fmul rf11, rf11, rf12",
    "or rf13, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf13, rf13, rf12 ; nop",             // 1-M
    "nop ; fmul rf13, rf13, r4",
    "fadd rf11, rf11, rf13 ; nop",             // fog factor
    "sub rf12, rf12, rf12 ; nop",
    "fmax rf11, rf11, rf12 ; nop",
    "or rf12, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // clamped to [0,1]
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop ; ldunifrf.rf15",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "setmsf -, 0 ; nop",
    /* Passthrough Z write: the QPU takes over the depth write so a
     * discarded fragment leaves the depth buffer alone. After the
     * setmsf, before the first vfpack tlb, and >= 3 ticks past the
     * first thrsw of the last-thrsw pair. Consumes the 0xffffff84
     * TLB depth-config word draw.c appends to the uniform stream. */
    "or tlbu, rf10, rf10 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/* ==================================================================
 * MULTITEXTURE FOG
 *
 * These three cover the multitexture env modes that do NOT
 * software-blend. The blending multitexture variants (modulate_blend,
 * decal_blend, replace_blend, translucent) are deliberately NOT here: fog
 * must be applied to the SOURCE colour before a blend reads the
 * destination, so those need the fog block placed mid-shader rather than
 * before the vfpacks, and are handled separately.
 * ================================================================== */

/*
 * Multitexture (GL_MODULATE) + fog. Spliced from the
 * multitexture shader: both texture units are combined exactly as
 * before, then the fog lerp -- copied verbatim from the other fog
 * shaders -- is applied to the combined rf7/rf8/rf9 immediately before
 * the colour vfpacks.
 *
 * Safe to splice there: only rf7..rf10 are live at that point, so the
 * fog block's rf11/rf12/rf13 scratch cannot clash with the unit-1 texel
 * registers (rf18..rf23) this shader uses earlier and has finished with.
 */
static const char* g_fragment_shader_multitexture_fog_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop",
    "or tmus, rf6, rf6 ; nop", /* triggers unit 0's fetch, queued */
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf17, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf16, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf16, rf16 ; nop ; thrsw", /* last-thrsw signal, part 1 of 2 */
    "nop ; nop ; thrsw",                  /* last-thrsw signal, part 2 of 2 */
    "or tmus, rf17, rf17 ; nop",          /* triggers unit 1's fetch, queued */
    "nop ; nop ; ldtmu.rf4",  // unit0 blue_green
    "nop ; nop ; ldtmu.rf3",  // unit0 red_alpha
    "nop ; nop ; ldtmu.rf19", // unit1 blue_green
    "nop ; nop ; ldtmu.rf18", // unit1 red_alpha
    "sub rf20, rf20, rf20 ; nop",
    "sub rf21, rf21, rf21 ; nop",
    "sub rf22, rf22, rf22 ; nop",
    "sub rf23, rf23, rf23 ; nop",
    "fadd rf20, rf20, rf19.l ; nop", // unit1 blue
    "fadd rf21, rf21, rf19.h ; nop", // unit1 green
    "fadd rf22, rf22, rf18.l ; nop", // unit1 red
    "fadd rf23, rf23, rf18.h ; nop", // unit1 alpha
    "nop ; fmul rf7, rf4.l, rf20",
    "nop ; fmul rf8, rf4.h, rf21",
    "nop ; fmul rf9, rf3.l, rf22",
    "nop ; fmul rf10, rf3.h, rf23 ; ldunifrf.rf11",
    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf11/rf12/rf13 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // linear factor
    "nop ; fmul rf12, rf12, rf0 ; ldunifrf.rf13",
    "nop ; fmul rf13, rf13, rf0",
    "nop ; fmul rf13, rf13, rf0",
    "fadd rf12, rf12, rf13 ; nop",             // C*c + D*c*c
    "or exp, rf12, rf12 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf12",             // M
    "nop ; fmul rf11, rf11, rf12",
    "or rf13, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf13, rf13, rf12 ; nop",             // 1-M
    "nop ; fmul rf13, rf13, r4",
    "fadd rf11, rf11, rf13 ; nop",             // fog factor
    "sub rf12, rf12, rf12 ; nop",
    "fmax rf11, rf11, rf12 ; nop",
    "or rf12, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // clamped to [0,1]
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // final thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * Multitexture (GL_DECAL) + fog. Spliced from the
 * multitexture_decal shader: both texture units are combined exactly as
 * before, then the fog lerp -- copied verbatim from the other fog
 * shaders -- is applied to the combined rf7/rf8/rf9 immediately before
 * the colour vfpacks.
 *
 * Safe to splice there: only rf7..rf10 are live at that point, so the
 * fog block's rf11/rf12/rf13 scratch cannot clash with the unit-1 texel
 * registers (rf18..rf23) this shader uses earlier and has finished with.
 */
static const char* g_fragment_shader_multitexture_decal_fog_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf17, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf16, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf16, rf16 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf17, rf17 ; nop",
    "nop ; nop ; ldtmu.rf4",
    "nop ; nop ; ldtmu.rf3",
    "nop ; nop ; ldtmu.rf19",
    "nop ; nop ; ldtmu.rf18",
    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",
    "fadd rf8, rf8, rf4.h ; nop",
    "fadd rf9, rf9, rf3.l ; nop",
    "fadd rf10, rf10, rf3.h ; nop",
    "sub rf20, rf20, rf20 ; nop",
    "sub rf21, rf21, rf21 ; nop",
    "sub rf22, rf22, rf22 ; nop",
    "sub rf23, rf23, rf23 ; nop",
    "fadd rf20, rf20, rf19.l ; nop",
    "fadd rf21, rf21, rf19.h ; nop",
    "fadd rf22, rf22, rf18.l ; nop",
    "fadd rf23, rf23, rf18.h ; nop",
    "fsub r0, rf20, rf7 ; nop",  // r0 = unit1_blue - unit0_blue
    "nop ; fmul r0, r0, rf23",    // r0 *= unit1_alpha
    "fadd rf7, rf7, r0 ; nop",    // rf7 = result_blue
    "fsub r0, rf21, rf8 ; nop",
    "nop ; fmul r0, r0, rf23",
    "fadd rf8, rf8, r0 ; nop",
    "fsub r0, rf22, rf9 ; nop",
    "nop ; fmul r0, r0, rf23",
    "fadd rf9, rf9, r0 ; nop ; ldunifrf.rf11",
    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf11/rf12/rf13 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // linear factor
    "nop ; fmul rf12, rf12, rf0 ; ldunifrf.rf13",
    "nop ; fmul rf13, rf13, rf0",
    "nop ; fmul rf13, rf13, rf0",
    "fadd rf12, rf12, rf13 ; nop",             // C*c + D*c*c
    "or exp, rf12, rf12 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf12",             // M
    "nop ; fmul rf11, rf11, rf12",
    "or rf13, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf13, rf13, rf12 ; nop",             // 1-M
    "nop ; fmul rf13, rf13, r4",
    "fadd rf11, rf11, rf13 ; nop",             // fog factor
    "sub rf12, rf12, rf12 ; nop",
    "fmax rf11, rf11, rf12 ; nop",
    "or rf12, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // clamped to [0,1]
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * Multitexture (GL_REPLACE) + fog. Spliced from the
 * multitexture_replace shader: both texture units are combined exactly as
 * before, then the fog lerp -- copied verbatim from the other fog
 * shaders -- is applied to the combined rf7/rf8/rf9 immediately before
 * the colour vfpacks.
 *
 * Safe to splice there: only rf7..rf10 are live at that point, so the
 * fog block's rf11/rf12/rf13 scratch cannot clash with the unit-1 texel
 * registers (rf18..rf23) this shader uses earlier and has finished with.
 */
static const char* g_fragment_shader_multitexture_replace_fog_assembly[] = {
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf17, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf16, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf16, rf16 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf17, rf17 ; nop",
    "nop ; nop ; ldtmu.rf4",
    "nop ; nop ; ldtmu.rf3",
    "nop ; nop ; ldtmu.rf19",
    "nop ; nop ; ldtmu.rf18",
    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",
    "fadd rf8, rf8, rf4.h ; nop",
    "fadd rf9, rf9, rf3.l ; nop",
    "fadd rf10, rf10, rf3.h ; nop",
    "sub rf20, rf20, rf20 ; nop",
    "sub rf21, rf21, rf21 ; nop",
    "sub rf22, rf22, rf22 ; nop",
    "sub rf23, rf23, rf23 ; nop",
    "fadd rf20, rf20, rf19.l ; nop",
    "fadd rf21, rf21, rf19.h ; nop",
    "fadd rf22, rf22, rf18.l ; nop",
    "fadd rf23, rf23, rf18.h ; nop",
    "or rf7, rf20, rf20 ; nop",
    "or rf8, rf21, rf21 ; nop",
    "or rf9, rf22, rf22 ; nop",
    "or rf10, rf23, rf23 ; nop ; ldunifrf.rf11",
    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf11/rf12/rf13 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // linear factor
    "nop ; fmul rf12, rf12, rf0 ; ldunifrf.rf13",
    "nop ; fmul rf13, rf13, rf0",
    "nop ; fmul rf13, rf13, rf0",
    "fadd rf12, rf12, rf13 ; nop",             // C*c + D*c*c
    "or exp, rf12, rf12 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf12",             // M
    "nop ; fmul rf11, rf11, rf12",
    "or rf13, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf13, rf13, rf12 ; nop",             // 1-M
    "nop ; fmul rf13, rf13, r4",
    "fadd rf11, rf11, rf13 ; nop",             // fog factor
    "sub rf12, rf12, rf12 ; nop",
    "fmax rf11, rf11, rf12 ; nop",
    "or rf12, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf11, rf11, rf12 ; nop ; ldunifrf.rf12",             // clamped to [0,1]
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop ; ldunifrf.rf12",
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/* ==================================================================
 * LIT VERTEX SHADERS  --  code slots 64 and 65
 *
 * These are VERTEX shaders sitting in the middle of the fragment-shader
 * region of this file, and that is deliberate rather than untidy: a code
 * slot is TYPE-AGNOSTIC. draw.c sets
 * fragment_shader_code_address_rshift_3 and
 * vertex_shader_code_address_rshift_3 as two INDEPENDENT addresses into
 * the same shader_code_mem, and the layout already interleaves the two
 * kinds (slot 0 vertex, 1 coordinate, 2 fragment, 3 fragment, 4 vertex).
 * So a reclaimed fragment slot can host a vertex shader, and these two
 * sit in reclaimed slots rather than appended ones precisely so that nothing
 * renumbers: V3D_MAX_SHADER_VARIANTS stays 114 and every offset
 * literal keeps its value. The allocation does not enter into it -- it is
 * sized from the packed shaders rather than from a 114 * 1024 literal.
 *
 * Slots 90 and 91 are dead. Their ADJACENCY buys nothing: the shaders are
 * packed end to end with a 256-instruction buffer, so a long variant simply
 * takes more bytes.
 *
 * DERIVATION. Each of these is g_vertex_shader_smooth_assembly (or its
 * textured twin) with the per-vertex COLOUR input replaced by the
 * per-vertex NORMAL. The whole matrix-multiply core, the screen-space
 * conversion and the Z scale/offset block are byte-for-byte the
 * originals, copied rather than re-derived, for the reason that shader's
 * own comment gives: a transcription error must not be able to creep into
 * the part of the shader that is unrelated to what changes.
 *
 * Dropping the colour input is what pays for the normal. GL fixed-function
 * lighting ignores the per-vertex colour entirely, so a lit draw carries
 * no colour attribute record -- which means the normal costs no extra
 * record, no extra varying, and FEWER VPM input words than the unlit
 * shader it came from (6 against 7 untextured, 8 against 9 textured).
 *
 * THESE COMPUTE THE LIGHTING. The bodies below normalize and evaluate the GL
 * terms, and milestone_l76_lighting_no_lights measures the resulting colours
 * against the equation on hardware.
 * ================================================================== */

static const char* g_vertex_shader_lit_assembly[] =
{
    "or rf3, 0x3f800000, 0x3f800000 ; nop", // w_m = 1.0

    "nop ; nop ; ldunifrf.rf10", // scale_p

    /* Separate Y-axis screen-space scale -- rf16, since rf14/15 are
     * already taken by color b/a in this variant (see
     * g_vertex_shader_assembly's own comment). */
    "nop ; nop ; ldunifrf.rf16", // scale_p_y

    "ldvpmv_in rf0,  0 ; nop", // x_m
    "ldvpmv_in rf1,  1 ; nop", // y_m
    "ldvpmv_in rf2,  2 ; nop", // z_m

    /* THE NORMAL, object space, in the three registers that hold colour
     * r/g/b -- chosen for the same reason: the matrix-multiply section
     * below never touches rf11/rf12/rf14, so a value parked here survives
     * it without a save/restore.
     *
     * VPM input words are 0-2 position + 3-5 normal = SIX, one word FEWER
     * than the smooth shader this is derived from, because the lit path
     * drops the per-vertex colour record (GL lighting ignores vertex
     * colour). So six words still fit one 8-word VPM sector and
     * vertex_shader_input_vpm_segment_size stays 1. */
    "ldvpmv_in rf11,  3 ; nop", // normal x
    "ldvpmv_in rf12,  4 ; nop", // normal y
    "ldvpmv_in rf14,  5 ; nop", // normal z

    /* Alpha, constant 1.0 in the register that holds colour a. */
    "or rf15, 0x3f800000, 0x3f800000 ; nop", // a = 1.0
    /* Matrix multiply -- byte-for-byte identical to g_vertex_shader_assembly. */
    "nop ; nop ; ldunif",
    "nop ; fmul rf4, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul rf5, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul rf6, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul rf7, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf7, rf7, r0 ; nop",

    "or recip, rf7, rf7 ; nop",
    "nop ; nop",
    "nop ; fmul r0, rf4, r4",
    "nop ; fmul r0, r0, rf10",
    "nop ; fmul rf8, r0, 0x43000000",
    "fsub r0, r0, r0 ; nop",
    "fsub r0, r0, rf5 ; nop",
    "nop ; fmul r0, r0, r4",
    "nop ; fmul r0, r0, rf16", // scale_p_y, not the shared scale_p
    "nop ; fmul rf9, r0, 0x43000000",
    "ftoin rf8, rf8 ; nop",
    "ftoin rf9, rf9 ; nop",
    "nop ; fmul rf13, rf6, r4",
    /* The Z scale and offset come from the uniform stream
     * (v3d_my_uniforms.z_scale / .z_offset, draw.c), APPENDED after the 16
     * matrix values -- so these two reads must stay the LAST ldunif* in this
     * shader. rf0/rf1 held x_m/y_m and are dead from the end of the matrix
     * multiply above to the end of the shader, so no new register is needed.
     * Two instructions of slack before first use; the matrix multiply above
     * proves one is enough. */
    "nop ; nop ; ldunifrf.rf23",          // viewport z scale  (context->sz)   // moved off rf0: rf0-rf2 must keep the object position
    "nop ; nop ; ldunifrf.rf24",          // viewport z offset (context->az)
    "nop ; fmul rf13, rf13, rf23",
    "fadd rf13, rf13, rf24 ; nop",

    "stvpmv 0, rf8 ; nop",
    "stvpmv 1, rf9 ; nop",
    "stvpmv 2, rf13 ; nop",
    "stvpmv 3, r4 ; nop",

    /* ---- EYE-SPACE LIGHTING. rf4..rf9 are all dead from here: rf4-rf7
     * held the clip position and rf8/rf9 the screen x/y, both already
     * written to the VPM above. ---- */
    "nop ; nop ; ldunif",                   // mv11
    "nop ; fmul rf4, rf0, r5 ; ldunif",      // mv12
    "nop ; fmul r0, rf1, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",      // mv13
    "nop ; fmul r0, rf2, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",      // mv14
    "nop ; fmul r0, rf3, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",      // mv21
    "nop ; fmul rf5, rf0, r5 ; ldunif",      // mv22
    "nop ; fmul r0, rf1, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",      // mv23
    "nop ; fmul r0, rf2, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",      // mv24
    "nop ; fmul r0, rf3, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",      // mv31
    "nop ; fmul rf6, rf0, r5 ; ldunif",      // mv32
    "nop ; fmul r0, rf1, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",      // mv33
    "nop ; fmul r0, rf2, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",      // mv34
    "nop ; fmul r0, rf3, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",      // it11
    "nop ; fmul rf7, rf11, r5 ; ldunif",      // it12
    "nop ; fmul r0, rf12, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",      // it13
    "nop ; fmul r0, rf14, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",      // it21
    "nop ; fmul rf8, rf11, r5 ; ldunif",      // it22
    "nop ; fmul r0, rf12, r5",
    "fadd rf8, rf8, r0 ; nop ; ldunif",      // it23
    "nop ; fmul r0, rf14, r5",
    "fadd rf8, rf8, r0 ; nop ; ldunif",      // it31
    "nop ; fmul rf9, rf11, r5 ; ldunif",      // it32
    "nop ; fmul r0, rf12, r5",
    "fadd rf9, rf9, r0 ; nop ; ldunif",      // it33
    "nop ; fmul r0, rf14, r5",
    "fadd rf9, rf9, r0 ; nop",
    /* |N_eye| is what object space got wrong: it divided by |n|. */
    "nop ; fmul r0, rf7, rf7",
    "nop ; fmul r1, rf8, rf8",
    "fadd r0, r0, r1 ; fmul r2, rf9, rf9",
    "fadd r0, r0, r2 ; nop",
    "or rsqrt, r0, r0 ; nop",
    "nop ; nop",
    "nop ; fmul rf7, rf7, r4",
    "nop ; fmul rf8, rf8, r4",
    "nop ; fmul rf9, rf9, r4",

    /* ---------------------------------------------------------------- LIGHTING
     * Object space, one light, ambient + diffuse + specular.
     *
     * r4 IS FREE HERE. It carried 1/w from "or recip, rf7, rf7" up to the
     * "stvpmv 3, r4" just above, and nothing reads it again, so both SFU
     * lookups below can use it without disturbing the matrix core. The block
     * sits between the position stores and the colour stores for exactly that
     * reason.
     *
     * THE UNIFORM TAIL IS POSITIONAL -- ldunif carries no address. It is read
     * in this order and the CPU writes it in this order: light position (3),
     * view direction (3), shininess (1), diffuse product (3), specular product (3), base
     * colour (3), shininess (1), diffuse product (3), specular product (3), base
     * colour (3), alpha (1) -- SEVENTEEN words, and seventeen instructions
     * minimum: one signal field per instruction is a hardware floor.
     *
     * The two trailing z_scale/z_offset ldunifrf above MUST stay the last reads
     * before the first read here, or the tail's first word arrives as z_scale.
     *
     * Every operand pair keeps to at most TWO register-file reads, which is the
     * port limit; partial products live in the accumulators r0-r2.
     */

    /* Zero, made by subtracting rf3 from itself -- the idiom the screen-space
     * conversion above already uses. rf3 holds w_m = 1.0 and is dead from the end
     * of the matrix multiply, so 1.0 needs no register of its own. */
    "fsub rf18, rf3, rf3 ; nop",

    /* L = light_position_object - vertex_position_object. The light arrives
     * already carried into object space by the CPU (draw.c), once per draw,
     * which is why no inverse matrix appears in this shader. */
    "nop ; nop ; ldunif",
    "fsub rf19, r5, rf4 ; nop ; ldunif",
    "fsub rf20, r5, rf5 ; nop ; ldunif",
    "fsub rf21, r5, rf6 ; nop",

    /* |L|^2, then r4 = 1/|L|. THE NORMALIZE IS REQUIRED, not an option: the
     * light is positional (w != 0), so GL defines the direction per vertex as
     * normalize(P_light - P_vertex). A constant direction would flatten the
     * specular highlight to one value across a whole surface. */
    "nop ; fmul r0, rf19, rf19",
    "nop ; fmul r1, rf20, rf20",
    "fadd r0, r0, r1 ; fmul r2, rf21, rf21",
    "fadd r0, r0, r2 ; nop",
    /* Magic-waddr SFU, never the ALU-op rsqrt spelling: the validator's
     * sfu_writes counter only inspects magic writes, so the ALU-op form's r4
     * latency is unchecked and a violation would assemble clean and fail on
     * hardware. This form ships in four fragment shaders. */
    "or rsqrt, r0, r0 ; nop",
    /* Exactly ONE instruction between the SFU write and the r4 read; it earns
     * its keep by starting the view-direction reads. */
    /* V is eye-space (0,0,1), a constant now, so H = L + z needs no
     * uniform and the three view-direction words leave the tail. The
     * instruction STAYS as a nop: it also fills the mandatory gap after
     * the rsqrt above, and r4 cannot be read in the next instruction. */
    "nop ; nop",
    "nop ; fmul rf19, rf19, r4",
    "nop ; fmul rf20, rf20, r4",
    "nop ; fmul rf21, rf21, r4",

    /* H = Lhat + Vobj, the half vector. V is the eye-space view direction
     * (0,0,1) carried into object space and normalised by the CPU;
     * GL_LIGHT_MODEL_LOCAL_VIEWER is false, so it is constant for the draw. */
    "or rf25, rf19, rf19 ; nop",
    "or rf26, rf20, rf20 ; nop",
    "fadd rf27, rf21, rf3 ; nop",

    "nop ; fmul r0, rf25, rf25",
    "nop ; fmul r1, rf26, rf26",
    "fadd r0, r0, r1 ; fmul r2, rf27, rf27",
    "fadd r0, r0, r2 ; nop",
    "or rsqrt, r0, r0 ; nop",
    "nop ; nop ; ldunif",                  // latency slot; r5 = shininess
    "nop ; fmul rf25, rf25, r4",
    "nop ; fmul rf26, rf26, r4",
    "nop ; fmul rf27, rf27, r4",

    /* N . Lhat, clamped at zero. The normal is used UNNORMALISED, which is GL:
     * with normalisation disabled -- and this GL has no GL_NORMALIZE token at
     * all -- the transformed normal is fed to the lighting equation as it is. */
    "nop ; fmul r0, rf7, rf19",
    "nop ; fmul r1, rf8, rf20",
    "fadd r0, r0, r1 ; fmul r2, rf9, rf21",
    "fadd r0, r0, r2 ; nop",
    "fmax rf28, r0, rf18 ; nop",

    /* N . Hhat, clamped at zero. fmax and not a predicated write: the only
     * conditional register writes in this whole tree are a fragment-stage
     * setmsf, and there are zero examples of predication to copy. */
    "nop ; fmul r0, rf7, rf25",
    "nop ; fmul r1, rf8, rf26",
    "fadd r0, r0, r1 ; fmul r2, rf9, rf27",
    "fadd r0, r0, r2 ; nop",
    /* Clamped to 2^-8 and NOT to zero, which the log below requires: log2(0) is
     * -inf, and shininess 0 would then give 0 * -inf = NaN where GL wants 1. At
     * 2^-8 the smallest representable result is 2^-8 raised to the shininess,
     * which is zero in eight bits for any exponent above 3, so nothing visible
     * changes and the NaN cannot arise. */
    "fmax rf29, r0, 0x3b800000 ; nop",

    /* (N.Hhat) ^ shininess = exp2(shininess * log2(N.Hhat)), for ANY shininess in
     * GL's [0,128] rather than the fixed exponent repeated squaring would give.
     * Both SFU forms are the magic-waddr spelling; `exp` ships in 50 fog shaders,
     * `log` is the first use of that waddr in this tree.
     *
     * The result STAYS IN r4 and is used from there by the specular multiplies
     * below -- no SFU lookup follows, so nothing disturbs it, and skipping the
     * move back to a register is what pays for the log and exp. */
    "or log, rf29, rf29 ; nop",
    "nop ; nop",
    "nop ; fmul r0, r4, r5",
    "or exp, r0, r0 ; nop",
    "nop ; nop",

    /* colour = base + diffuse_product * (N.L) + specular_product * (N.H)^10.
     * The products are folded on the CPU (light.c), so no light x material
     * multiply happens per vertex. */
    "nop ; nop ; ldunif",
    "nop ; fmul rf30, r5, rf28 ; ldunif",
    "nop ; fmul rf31, r5, rf28 ; ldunif",
    "nop ; fmul rf22, r5, rf28 ; ldunif",
    "nop ; fmul r0, r5, r4 ; ldunif",
    "fadd rf30, rf30, r0 ; fmul r1, r5, r4 ; ldunif",
    "fadd rf31, rf31, r1 ; fmul r2, r5, r4 ; ldunif",
    "fadd rf22, rf22, r2 ; nop",
    "fadd rf30, rf30, r5 ; nop ; ldunif",
    "fadd rf31, rf31, r5 ; nop ; ldunif",
    "fadd rf22, rf22, r5 ; nop ; ldunif",
    "or rf15, r5, r5 ; nop",

    /* GL clamps the final vertex colour to [0,1]. Only the upper clamp is
     * needed: every term above is non-negative, the two dot products having
     * been clamped at zero already. */
    "fmin rf11, rf30, rf3 ; nop",
    "fmin rf12, rf31, rf3 ; nop",
    "fmin rf14, rf22, rf3 ; nop",

    "stvpmv 4, rf11 ; nop", // lit colour r
    "stvpmv 5, rf12 ; nop", // lit colour g
    "stvpmv 6, rf14 ; nop", // lit colour b
    "stvpmv 7, rf15 ; nop", // a = 1.0

    "vpmwt -              ; nop",
    "nop                  ; nop ; thrsw",
    "nop                  ; nop",
    "nop                  ; nop",
};

/*
 * LIT, UNTEXTURED, with GL_COLOR_MATERIAL. lit
 * plus the K1 half of light.c's fold: the vertex colour arrives as a fourth
 * attribute at VPM inputs 6..9 and each channel gains
 * C * (K1base + K1diff * N.L + K1spec * spec). With a mode that tracks nothing
 * every K1 is zero and this computes exactly what the host shader does.
 */
static const char* g_vertex_shader_lit_colormaterial_assembly[] = {
    "or rf3, 0x3f800000, 0x3f800000 ; nop", // w_m = 1.0

    "nop ; nop ; ldunifrf.rf10", // scale_p

    /* Separate Y-axis screen-space scale -- rf16, since rf14/15 are
     * already taken by color b/a in this variant (see
     * g_vertex_shader_assembly's own comment). */
    "nop ; nop ; ldunifrf.rf16", // scale_p_y

    "ldvpmv_in rf0,  0 ; nop", // x_m
    "ldvpmv_in rf1,  1 ; nop", // y_m
    "ldvpmv_in rf2,  2 ; nop", // z_m

    /* THE NORMAL, object space, in the three registers that hold colour
     * r/g/b -- chosen for the same reason: the matrix-multiply section
     * below never touches rf11/rf12/rf14, so a value parked here survives
     * it without a save/restore.
     *
     * VPM input words are 0-2 position + 3-5 normal = SIX, one word FEWER
     * than the smooth shader this is derived from, because the lit path
     * drops the per-vertex colour record (GL lighting ignores vertex
     * colour). So six words still fit one 8-word VPM sector and
     * vertex_shader_input_vpm_segment_size stays 1. */
    "ldvpmv_in rf11,  3 ; nop", // normal x
    "ldvpmv_in rf12,  4 ; nop", // normal y
    "ldvpmv_in rf14,  5 ; nop", // normal z

    /* Alpha, constant 1.0 in the register that holds colour a. */
    "or rf15, 0x3f800000, 0x3f800000 ; nop", // a = 1.0
    /* Matrix multiply -- byte-for-byte identical to g_vertex_shader_assembly. */
    "nop ; nop ; ldunif",
    "nop ; fmul rf4, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul rf5, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul rf6, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul rf7, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf7, rf7, r0 ; nop",

    "or recip, rf7, rf7 ; nop",
    "nop ; nop",
    "nop ; fmul r0, rf4, r4",
    "nop ; fmul r0, r0, rf10",
    "nop ; fmul rf8, r0, 0x43000000",
    "fsub r0, r0, r0 ; nop",
    "fsub r0, r0, rf5 ; nop",
    "nop ; fmul r0, r0, r4",
    "nop ; fmul r0, r0, rf16", // scale_p_y, not the shared scale_p
    "nop ; fmul rf9, r0, 0x43000000",
    "ftoin rf8, rf8 ; nop",
    "ftoin rf9, rf9 ; nop",
    "nop ; fmul rf13, rf6, r4",
    /* The Z scale and offset come from the uniform stream
     * (v3d_my_uniforms.z_scale / .z_offset, draw.c), APPENDED after the 16
     * matrix values -- so these two reads must stay the LAST ldunif* in this
     * shader. rf0/rf1 held x_m/y_m and are dead from the end of the matrix
     * multiply above to the end of the shader, so no new register is needed.
     * Two instructions of slack before first use; the matrix multiply above
     * proves one is enough. */
    "nop ; nop ; ldunifrf.rf23",          // viewport z scale  (context->sz)   // moved off rf0: rf0-rf2 must keep the object position
    "nop ; nop ; ldunifrf.rf24",          // viewport z offset (context->az)
    "nop ; fmul rf13, rf13, rf23",
    "fadd rf13, rf13, rf24 ; nop",

    "stvpmv 0, rf8 ; nop",
    "stvpmv 1, rf9 ; nop",
    "stvpmv 2, rf13 ; nop",
    "stvpmv 3, r4 ; nop",

    /* ---- EYE-SPACE LIGHTING. rf4..rf9 are all dead from here: rf4-rf7
     * held the clip position and rf8/rf9 the screen x/y, both already
     * written to the VPM above. ---- */
    "nop ; nop ; ldunif",                   // mv11
    "nop ; fmul rf4, rf0, r5 ; ldunif",      // mv12
    "nop ; fmul r0, rf1, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",      // mv13
    "nop ; fmul r0, rf2, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",      // mv14
    "nop ; fmul r0, rf3, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",      // mv21
    "nop ; fmul rf5, rf0, r5 ; ldunif",      // mv22
    "nop ; fmul r0, rf1, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",      // mv23
    "nop ; fmul r0, rf2, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",      // mv24
    "nop ; fmul r0, rf3, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",      // mv31
    "nop ; fmul rf6, rf0, r5 ; ldunif",      // mv32
    "nop ; fmul r0, rf1, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",      // mv33
    "nop ; fmul r0, rf2, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",      // mv34
    "nop ; fmul r0, rf3, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",      // it11
    "nop ; fmul rf7, rf11, r5 ; ldunif",      // it12
    "nop ; fmul r0, rf12, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",      // it13
    "nop ; fmul r0, rf14, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",      // it21
    "nop ; fmul rf8, rf11, r5 ; ldunif",      // it22
    "nop ; fmul r0, rf12, r5",
    "fadd rf8, rf8, r0 ; nop ; ldunif",      // it23
    "nop ; fmul r0, rf14, r5",
    "fadd rf8, rf8, r0 ; nop ; ldunif",      // it31
    "nop ; fmul rf9, rf11, r5 ; ldunif",      // it32
    "nop ; fmul r0, rf12, r5",
    "fadd rf9, rf9, r0 ; nop ; ldunif",      // it33
    "nop ; fmul r0, rf14, r5",
    "fadd rf9, rf9, r0 ; nop",
    /* |N_eye| is what object space got wrong: it divided by |n|. */
    "nop ; fmul r0, rf7, rf7",
    "nop ; fmul r1, rf8, rf8",
    "fadd r0, r0, r1 ; fmul r2, rf9, rf9",
    "fadd r0, r0, r2 ; nop",
    "or rsqrt, r0, r0 ; nop",
    "nop ; nop",
    "nop ; fmul rf7, rf7, r4",
    "nop ; fmul rf8, rf8, r4",
    "nop ; fmul rf9, rf9, r4",

    /* ---------------------------------------------------------------- LIGHTING
     * Object space, one light, ambient + diffuse + specular.
     *
     * r4 IS FREE HERE. It carried 1/w from "or recip, rf7, rf7" up to the
     * "stvpmv 3, r4" just above, and nothing reads it again, so both SFU
     * lookups below can use it without disturbing the matrix core. The block
     * sits between the position stores and the colour stores for exactly that
     * reason.
     *
     * THE UNIFORM TAIL IS POSITIONAL -- ldunif carries no address. It is read
     * in this order and the CPU writes it in this order: light position (3),
     * view direction (3), shininess (1), diffuse product (3), specular product (3), base
     * colour (3), shininess (1), diffuse product (3), specular product (3), base
     * colour (3), alpha (1) -- SEVENTEEN words, and seventeen instructions
     * minimum: one signal field per instruction is a hardware floor.
     *
     * The two trailing z_scale/z_offset ldunifrf above MUST stay the last reads
     * before the first read here, or the tail's first word arrives as z_scale.
     *
     * Every operand pair keeps to at most TWO register-file reads, which is the
     * port limit; partial products live in the accumulators r0-r2.
     */

    /* Zero, made by subtracting rf3 from itself -- the idiom the screen-space
     * conversion above already uses. rf3 holds w_m = 1.0 and is dead from the end
     * of the matrix multiply, so 1.0 needs no register of its own. */
    "fsub rf18, rf3, rf3 ; nop",

    /* L = light_position_object - vertex_position_object. The light arrives
     * already carried into object space by the CPU (draw.c), once per draw,
     * which is why no inverse matrix appears in this shader. */
    "nop ; nop ; ldunif",
    "fsub rf19, r5, rf4 ; nop ; ldunif",
    "fsub rf20, r5, rf5 ; nop ; ldunif",
    "fsub rf21, r5, rf6 ; nop",

    /* |L|^2, then r4 = 1/|L|. THE NORMALIZE IS REQUIRED, not an option: the
     * light is positional (w != 0), so GL defines the direction per vertex as
     * normalize(P_light - P_vertex). A constant direction would flatten the
     * specular highlight to one value across a whole surface. */
    "nop ; fmul r0, rf19, rf19",
    "nop ; fmul r1, rf20, rf20",
    "fadd r0, r0, r1 ; fmul r2, rf21, rf21",
    "fadd r0, r0, r2 ; nop",
    /* Magic-waddr SFU, never the ALU-op rsqrt spelling: the validator's
     * sfu_writes counter only inspects magic writes, so the ALU-op form's r4
     * latency is unchecked and a violation would assemble clean and fail on
     * hardware. This form ships in four fragment shaders. */
    "or rsqrt, r0, r0 ; nop",
    /* Exactly ONE instruction between the SFU write and the r4 read; it earns
     * its keep by starting the view-direction reads. */
    /* V is eye-space (0,0,1), a constant now, so H = L + z needs no
     * uniform and the three view-direction words leave the tail. The
     * instruction STAYS as a nop: it also fills the mandatory gap after
     * the rsqrt above, and r4 cannot be read in the next instruction. */
    "nop ; nop",
    "nop ; fmul rf19, rf19, r4",
    "nop ; fmul rf20, rf20, r4",
    "nop ; fmul rf21, rf21, r4",

    /* H = Lhat + Vobj, the half vector. V is the eye-space view direction
     * (0,0,1) carried into object space and normalised by the CPU;
     * GL_LIGHT_MODEL_LOCAL_VIEWER is false, so it is constant for the draw. */
    "or rf25, rf19, rf19 ; nop",
    "or rf26, rf20, rf20 ; nop",
    "fadd rf27, rf21, rf3 ; nop",

    "nop ; fmul r0, rf25, rf25",
    "nop ; fmul r1, rf26, rf26",
    "fadd r0, r0, r1 ; fmul r2, rf27, rf27",
    "fadd r0, r0, r2 ; nop",
    "or rsqrt, r0, r0 ; nop",
    "nop ; nop ; ldunif",                  // latency slot; r5 = shininess
    "nop ; fmul rf25, rf25, r4",
    "nop ; fmul rf26, rf26, r4",
    "nop ; fmul rf27, rf27, r4",

    /* N . Lhat, clamped at zero. The normal is used UNNORMALISED, which is GL:
     * with normalisation disabled -- and this GL has no GL_NORMALIZE token at
     * all -- the transformed normal is fed to the lighting equation as it is. */
    "nop ; fmul r0, rf7, rf19",
    "nop ; fmul r1, rf8, rf20",
    "fadd r0, r0, r1 ; fmul r2, rf9, rf21",
    "fadd r0, r0, r2 ; nop",
    "fmax rf28, r0, rf18 ; nop",

    /* N . Hhat, clamped at zero. fmax and not a predicated write: the only
     * conditional register writes in this whole tree are a fragment-stage
     * setmsf, and there are zero examples of predication to copy. */
    "nop ; fmul r0, rf7, rf25",
    "nop ; fmul r1, rf8, rf26",
    "fadd r0, r0, r1 ; fmul r2, rf9, rf27",
    "fadd r0, r0, r2 ; nop",
    /* Clamped to 2^-8 and NOT to zero, which the log below requires: log2(0) is
     * -inf, and shininess 0 would then give 0 * -inf = NaN where GL wants 1. At
     * 2^-8 the smallest representable result is 2^-8 raised to the shininess,
     * which is zero in eight bits for any exponent above 3, so nothing visible
     * changes and the NaN cannot arise. */
    "fmax rf29, r0, 0x3b800000 ; nop",

    /* (N.Hhat) ^ shininess = exp2(shininess * log2(N.Hhat)), for ANY shininess in
     * GL's [0,128] rather than the fixed exponent repeated squaring would give.
     * Both SFU forms are the magic-waddr spelling; `exp` ships in 50 fog shaders,
     * `log` is the first use of that waddr in this tree.
     *
     * The result STAYS IN r4 and is used from there by the specular multiplies
     * below -- no SFU lookup follows, so nothing disturbs it, and skipping the
     * move back to a register is what pays for the log and exp. */
    "or log, rf29, rf29 ; nop",
    "nop ; nop",
    "nop ; fmul r0, r4, r5",
    "or exp, r0, r0 ; nop",
    "nop ; nop",

    /* colour = base + diffuse_product * (N.L) + specular_product * (N.H)^10.
     * The products are folded on the CPU (light.c), so no light x material
     * multiply happens per vertex. */
    "nop ; nop ; ldunif",
    "nop ; fmul rf30, r5, rf28 ; ldunif",
    "nop ; fmul rf31, r5, rf28 ; ldunif",
    "nop ; fmul rf22, r5, rf28 ; ldunif",
    "nop ; fmul r0, r5, r4 ; ldunif",
    "fadd rf30, rf30, r0 ; fmul r1, r5, r4 ; ldunif",
    "fadd rf31, rf31, r1 ; fmul r2, r5, r4 ; ldunif",
    "fadd rf22, rf22, r2 ; nop",
    "fadd rf30, rf30, r5 ; nop ; ldunif",
    "fadd rf31, rf31, r5 ; nop ; ldunif",
    "fadd rf22, rf22, r5 ; nop ; ldunif",
    "or rf15, r5, r5 ; nop",
    /* ---- GL_COLOR_MATERIAL: the K1 half of the folded terms ---- */
    "ldvpmv_in rf5, 6 ; nop",
    "ldvpmv_in rf6, 7 ; nop",
    "ldvpmv_in rf7, 8 ; nop",
    "ldvpmv_in rf8, 9 ; nop",
    /* k per channel: base + diffuse*N.L + specular*spec. The tail is
     * grouped by channel, so each group is three consecutive words. */
    "nop ; nop ; ldunif",
    "or rf0, r5, r5 ; nop ; ldunif",
    "nop ; fmul r0, r5, rf28 ; ldunif",
    "nop ; fmul r1, r5, r4 ; ldunif",
    "fadd r0, r0, r1 ; nop",
    "fadd rf0, rf0, r0 ; nop",
    "or rf1, r5, r5 ; nop ; ldunif",
    "nop ; fmul r0, r5, rf28 ; ldunif",
    "nop ; fmul r1, r5, r4 ; ldunif",
    "fadd r0, r0, r1 ; nop",
    "fadd rf1, rf1, r0 ; nop",
    "or rf2, r5, r5 ; nop ; ldunif",
    "nop ; fmul r0, r5, rf28 ; ldunif",
    "nop ; fmul r1, r5, r4 ; ldunif",
    "fadd r0, r0, r1 ; nop",
    "fadd rf2, rf2, r0 ; nop",
    /* accumulate C * k, and the alpha scale still sitting in r5 */
    "nop ; fmul r0, rf0, rf5",
    "nop ; fmul r1, rf1, rf6",
    "nop ; fmul r2, rf2, rf7",
    "fadd rf30, rf30, r0 ; nop",
    "fadd rf31, rf31, r1 ; fmul r0, r5, rf8",
    "fadd rf22, rf22, r2 ; nop",
    "fadd rf15, rf15, r0 ; nop",

    /* GL clamps the final vertex colour to [0,1]. Only the upper clamp is
     * needed: every term above is non-negative, the two dot products having
     * been clamped at zero already. */
    "fmin rf11, rf30, rf3 ; nop",
    "fmin rf12, rf31, rf3 ; nop",
    "fmin rf14, rf22, rf3 ; nop",

    "stvpmv 4, rf11 ; nop", // lit colour r
    "stvpmv 5, rf12 ; nop", // lit colour g
    "stvpmv 6, rf14 ; nop", // lit colour b
    "stvpmv 7, rf15 ; nop", // a = 1.0

    "vpmwt -              ; nop",
    "nop                  ; nop ; thrsw",
    "nop                  ; nop",
    "nop                  ; nop",
};


static const char* g_vertex_shader_lit_textured_assembly[] =
{
    "or rf3, 0x3f800000, 0x3f800000 ; nop", // w_m = 1.0

    "nop ; nop ; ldunifrf.rf10", // scale_p

    /* Separate Y-axis screen-space scale -- rf18, since rf14-17 are
     * already taken by color r/g/b/a in this variant. */
    "nop ; nop ; ldunifrf.rf18", // scale_p_y

    "ldvpmv_in rf0,  0 ; nop", // x_m
    "ldvpmv_in rf1,  1 ; nop", // y_m
    "ldvpmv_in rf2,  2 ; nop", // z_m

    "ldvpmv_in rf11,  3 ; nop", // s
    "ldvpmv_in rf12,  4 ; nop", // t

    /* THE NORMAL, object space, in the registers that hold colour r/g/b,
     * for the same reason as in the untextured variant above: the
     * matrix-multiply section never touches them.
     *
     * VPM input words are 0-2 position + 3-4 s,t + 5-7 normal = EIGHT, one
     * word fewer than the smooth textured shader, the colour record being
     * dropped for a lit draw. Eight is exactly one sector; the field stays
     * at the 2 the combined path already sets, which is harmless. */
    "ldvpmv_in rf14,  5 ; nop", // normal x
    "ldvpmv_in rf15,  6 ; nop", // normal y
    "ldvpmv_in rf16,  7 ; nop", // normal z

    /* Alpha, constant 1.0. */
    "or rf17, 0x3f800000, 0x3f800000 ; nop", // a = 1.0
    /* Matrix multiply -- byte-for-byte identical to g_vertex_shader_assembly. */
    "nop ; nop ; ldunif",
    "nop ; fmul rf4, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul rf5, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul rf6, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul rf7, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf7, rf7, r0 ; nop",

    "or recip, rf7, rf7 ; nop",
    "nop ; nop",
    "nop ; fmul r0, rf4, r4",
    "nop ; fmul r0, r0, rf10",
    "nop ; fmul rf8, r0, 0x43000000",
    "fsub r0, r0, r0 ; nop",
    "fsub r0, r0, rf5 ; nop",
    "nop ; fmul r0, r0, r4",
    "nop ; fmul r0, r0, rf18", // scale_p_y, not the shared scale_p
    "nop ; fmul rf9, r0, 0x43000000",
    "ftoin rf8, rf8 ; nop",
    "ftoin rf9, rf9 ; nop",
    "nop ; fmul rf13, rf6, r4",
    /* The Z scale and offset come from the uniform stream
     * (v3d_my_uniforms.z_scale / .z_offset, draw.c), APPENDED after the 16
     * matrix values -- so these two reads must stay the LAST ldunif* in this
     * shader. rf0/rf1 held x_m/y_m and are dead from the end of the matrix
     * multiply above to the end of the shader, so no new register is needed.
     * Two instructions of slack before first use; the matrix multiply above
     * proves one is enough. */
    "nop ; nop ; ldunifrf.rf19",          // viewport z scale  (context->sz)   // moved off rf0: rf0-rf2 must keep the object position
    "nop ; nop ; ldunifrf.rf20",          // viewport z offset (context->az)
    "nop ; fmul rf13, rf13, rf19",
    "fadd rf13, rf13, rf20 ; nop",

    "stvpmv 0, rf8 ; nop",
    "stvpmv 1, rf9 ; nop",
    "stvpmv 2, rf13 ; nop",
    "stvpmv 3, r4 ; nop",

    /* ---- EYE-SPACE LIGHTING. rf4..rf9 are all dead from here: rf4-rf7
     * held the clip position and rf8/rf9 the screen x/y, both already
     * written to the VPM above. ---- */
    "nop ; nop ; ldunif",                   // mv11
    "nop ; fmul rf4, rf0, r5 ; ldunif",      // mv12
    "nop ; fmul r0, rf1, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",      // mv13
    "nop ; fmul r0, rf2, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",      // mv14
    "nop ; fmul r0, rf3, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",      // mv21
    "nop ; fmul rf5, rf0, r5 ; ldunif",      // mv22
    "nop ; fmul r0, rf1, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",      // mv23
    "nop ; fmul r0, rf2, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",      // mv24
    "nop ; fmul r0, rf3, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",      // mv31
    "nop ; fmul rf6, rf0, r5 ; ldunif",      // mv32
    "nop ; fmul r0, rf1, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",      // mv33
    "nop ; fmul r0, rf2, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",      // mv34
    "nop ; fmul r0, rf3, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",      // it11
    "nop ; fmul rf7, rf14, r5 ; ldunif",      // it12
    "nop ; fmul r0, rf15, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",      // it13
    "nop ; fmul r0, rf16, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",      // it21
    "nop ; fmul rf8, rf14, r5 ; ldunif",      // it22
    "nop ; fmul r0, rf15, r5",
    "fadd rf8, rf8, r0 ; nop ; ldunif",      // it23
    "nop ; fmul r0, rf16, r5",
    "fadd rf8, rf8, r0 ; nop ; ldunif",      // it31
    "nop ; fmul rf9, rf14, r5 ; ldunif",      // it32
    "nop ; fmul r0, rf15, r5",
    "fadd rf9, rf9, r0 ; nop ; ldunif",      // it33
    "nop ; fmul r0, rf16, r5",
    "fadd rf9, rf9, r0 ; nop",
    /* |N_eye| is what object space got wrong: it divided by |n|. */
    "nop ; fmul r0, rf7, rf7",
    "nop ; fmul r1, rf8, rf8",
    "fadd r0, r0, r1 ; fmul r2, rf9, rf9",
    "fadd r0, r0, r2 ; nop",
    "or rsqrt, r0, r0 ; nop",
    "nop ; nop",
    "nop ; fmul rf7, rf7, r4",
    "nop ; fmul rf8, rf8, r4",
    "nop ; fmul rf9, rf9, r4",

    /* ---------------------------------------------------------------- LIGHTING
     * Object space, one light, ambient + diffuse + specular.
     *
     * r4 IS FREE HERE. It carried 1/w from "or recip, rf7, rf7" up to the
     * "stvpmv 3, r4" just above, and nothing reads it again, so both SFU
     * lookups below can use it without disturbing the matrix core. The block
     * sits between the position stores and the colour stores for exactly that
     * reason.
     *
     * THE UNIFORM TAIL IS POSITIONAL -- ldunif carries no address. It is read
     * in this order and the CPU writes it in this order: light position (3),
     * view direction (3), shininess (1), diffuse product (3), specular product (3), base
     * colour (3), shininess (1), diffuse product (3), specular product (3), base
     * colour (3), alpha (1) -- SEVENTEEN words, and seventeen instructions
     * minimum: one signal field per instruction is a hardware floor.
     *
     * The two trailing z_scale/z_offset ldunifrf above MUST stay the last reads
     * before the first read here, or the tail's first word arrives as z_scale.
     *
     * Every operand pair keeps to at most TWO register-file reads, which is the
     * port limit; partial products live in the accumulators r0-r2.
     */

    /* Zero, made by subtracting rf3 from itself -- the idiom the screen-space
     * conversion above already uses. rf3 holds w_m = 1.0 and is dead from the end
     * of the matrix multiply, so 1.0 needs no register of its own. */
    "fsub rf22, rf3, rf3 ; nop",

    /* L = light_position_object - vertex_position_object. The light arrives
     * already carried into object space by the CPU (draw.c), once per draw,
     * which is why no inverse matrix appears in this shader. */
    "nop ; nop ; ldunif",
    "fsub rf23, r5, rf4 ; nop ; ldunif",
    "fsub rf24, r5, rf5 ; nop ; ldunif",
    "fsub rf25, r5, rf6 ; nop",

    /* |L|^2, then r4 = 1/|L|. THE NORMALIZE IS REQUIRED, not an option: the
     * light is positional (w != 0), so GL defines the direction per vertex as
     * normalize(P_light - P_vertex). A constant direction would flatten the
     * specular highlight to one value across a whole surface. */
    "nop ; fmul r0, rf23, rf23",
    "nop ; fmul r1, rf24, rf24",
    "fadd r0, r0, r1 ; fmul r2, rf25, rf25",
    "fadd r0, r0, r2 ; nop",
    /* Magic-waddr SFU, never the ALU-op rsqrt spelling: the validator's
     * sfu_writes counter only inspects magic writes, so the ALU-op form's r4
     * latency is unchecked and a violation would assemble clean and fail on
     * hardware. This form ships in four fragment shaders. */
    "or rsqrt, r0, r0 ; nop",
    /* Exactly ONE instruction between the SFU write and the r4 read; it earns
     * its keep by starting the view-direction reads. */
    /* V is eye-space (0,0,1), a constant now, so H = L + z needs no
     * uniform and the three view-direction words leave the tail. The
     * instruction STAYS as a nop: it also fills the mandatory gap after
     * the rsqrt above, and r4 cannot be read in the next instruction. */
    "nop ; nop",
    "nop ; fmul rf23, rf23, r4",
    "nop ; fmul rf24, rf24, r4",
    "nop ; fmul rf25, rf25, r4",

    /* H = Lhat + Vobj, the half vector. V is the eye-space view direction
     * (0,0,1) carried into object space and normalised by the CPU;
     * GL_LIGHT_MODEL_LOCAL_VIEWER is false, so it is constant for the draw. */
    "or rf26, rf23, rf23 ; nop",
    "or rf27, rf24, rf24 ; nop",
    "fadd rf28, rf25, rf3 ; nop",

    "nop ; fmul r0, rf26, rf26",
    "nop ; fmul r1, rf27, rf27",
    "fadd r0, r0, r1 ; fmul r2, rf28, rf28",
    "fadd r0, r0, r2 ; nop",
    "or rsqrt, r0, r0 ; nop",
    "nop ; nop ; ldunif",                  // latency slot; r5 = shininess
    "nop ; fmul rf26, rf26, r4",
    "nop ; fmul rf27, rf27, r4",
    "nop ; fmul rf28, rf28, r4",

    /* N . Lhat, clamped at zero. The normal is used UNNORMALISED, which is GL:
     * with normalisation disabled -- and this GL has no GL_NORMALIZE token at
     * all -- the transformed normal is fed to the lighting equation as it is. */
    "nop ; fmul r0, rf7, rf23",
    "nop ; fmul r1, rf8, rf24",
    "fadd r0, r0, r1 ; fmul r2, rf9, rf25",
    "fadd r0, r0, r2 ; nop",
    "fmax rf29, r0, rf22 ; nop",

    /* N . Hhat, clamped at zero. fmax and not a predicated write: the only
     * conditional register writes in this whole tree are a fragment-stage
     * setmsf, and there are zero examples of predication to copy. */
    "nop ; fmul r0, rf7, rf26",
    "nop ; fmul r1, rf8, rf27",
    "fadd r0, r0, r1 ; fmul r2, rf9, rf28",
    "fadd r0, r0, r2 ; nop",
    /* Clamped to 2^-8 and NOT to zero, which the log below requires: log2(0) is
     * -inf, and shininess 0 would then give 0 * -inf = NaN where GL wants 1. At
     * 2^-8 the smallest representable result is 2^-8 raised to the shininess,
     * which is zero in eight bits for any exponent above 3, so nothing visible
     * changes and the NaN cannot arise. */
    "fmax rf30, r0, 0x3b800000 ; nop",

    /* (N.Hhat) ^ shininess = exp2(shininess * log2(N.Hhat)), for ANY shininess in
     * GL's [0,128] rather than the fixed exponent repeated squaring would give.
     * Both SFU forms are the magic-waddr spelling; `exp` ships in 50 fog shaders,
     * `log` is the first use of that waddr in this tree.
     *
     * The result STAYS IN r4 and is used from there by the specular multiplies
     * below -- no SFU lookup follows, so nothing disturbs it, and skipping the
     * move back to a register is what pays for the log and exp. */
    "or log, rf30, rf30 ; nop",
    "nop ; nop",
    "nop ; fmul r0, r4, r5",
    "or exp, r0, r0 ; nop",
    "nop ; nop",

    /* colour = base + diffuse_product * (N.L) + specular_product * (N.H)^10.
     * The products are folded on the CPU (light.c), so no light x material
     * multiply happens per vertex. */
    "nop ; nop ; ldunif",
    "nop ; fmul rf31, r5, rf29 ; ldunif",
    "nop ; fmul rf3, r5, rf29 ; ldunif",
    "nop ; fmul rf4, r5, rf29 ; ldunif",
    "nop ; fmul r0, r5, r4 ; ldunif",
    "fadd rf31, rf31, r0 ; fmul r1, r5, r4 ; ldunif",
    "fadd rf3, rf3, r1 ; fmul r2, r5, r4 ; ldunif",
    "fadd rf4, rf4, r2 ; nop",
    "fadd rf31, rf31, r5 ; nop ; ldunif",
    "fadd rf3, rf3, r5 ; nop ; ldunif",
    "fadd rf4, rf4, r5 ; nop ; ldunif",
    "or rf17, r5, r5 ; nop",

    /* GL clamps the final vertex colour to [0,1]. Only the upper clamp is
     * needed: every term above is non-negative, the two dot products having
     * been clamped at zero already.
     *
     * 1.0 IS REMATERIALISED HERE, and not taken from rf3 the way the untextured
     * twin does it. This variant reuses rf3 as its GREEN accumulator above --
     * legal, w_m being dead by then -- so clamping against rf3 clamped every
     * channel against green: emission 0.8/0.2/0.2 came out 0.2/0.2/0.2. rf30 is
     * dead after the log above. */
    "or rf30, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf14, rf31, rf30 ; nop",
    "fmin rf15, rf3,  rf30 ; nop",
    "fmin rf16, rf4,  rf30 ; nop",

    "stvpmv 4, rf11 ; nop", // s
    "stvpmv 5, rf12 ; nop", // t
    "stvpmv 6, rf14 ; nop", // lit colour r
    "stvpmv 7, rf15 ; nop", // lit colour g
    "stvpmv 8, rf16 ; nop", // lit colour b
    "stvpmv 9, rf17 ; nop", // a = 1.0

    "vpmwt -              ; nop",
    "nop                  ; nop ; thrsw",
    "nop                  ; nop",
    "nop                  ; nop",
};

/*
 * LIT, TEXTURED, with GL_COLOR_MATERIAL. lit_textured
 * plus the K1 half of light.c's fold: the vertex colour arrives as a fourth
 * attribute at VPM inputs 8..11 and each channel gains
 * C * (K1base + K1diff * N.L + K1spec * spec). With a mode that tracks nothing
 * every K1 is zero and this computes exactly what the host shader does.
 */
static const char* g_vertex_shader_lit_textured_colormaterial_assembly[] = {
    "or rf3, 0x3f800000, 0x3f800000 ; nop", // w_m = 1.0

    "nop ; nop ; ldunifrf.rf10", // scale_p

    /* Separate Y-axis screen-space scale -- rf18, since rf14-17 are
     * already taken by color r/g/b/a in this variant. */
    "nop ; nop ; ldunifrf.rf18", // scale_p_y

    "ldvpmv_in rf0,  0 ; nop", // x_m
    "ldvpmv_in rf1,  1 ; nop", // y_m
    "ldvpmv_in rf2,  2 ; nop", // z_m

    "ldvpmv_in rf11,  3 ; nop", // s
    "ldvpmv_in rf12,  4 ; nop", // t

    /* THE NORMAL, object space, in the registers that hold colour r/g/b,
     * for the same reason as in the untextured variant above: the
     * matrix-multiply section never touches them.
     *
     * VPM input words are 0-2 position + 3-4 s,t + 5-7 normal = EIGHT, one
     * word fewer than the smooth textured shader, the colour record being
     * dropped for a lit draw. Eight is exactly one sector; the field stays
     * at the 2 the combined path already sets, which is harmless. */
    "ldvpmv_in rf14,  5 ; nop", // normal x
    "ldvpmv_in rf15,  6 ; nop", // normal y
    "ldvpmv_in rf16,  7 ; nop", // normal z

    /* Alpha, constant 1.0. */
    "or rf17, 0x3f800000, 0x3f800000 ; nop", // a = 1.0
    /* Matrix multiply -- byte-for-byte identical to g_vertex_shader_assembly. */
    "nop ; nop ; ldunif",
    "nop ; fmul rf4, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul rf5, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul rf6, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul rf7, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf7, rf7, r0 ; nop",

    "or recip, rf7, rf7 ; nop",
    "nop ; nop",
    "nop ; fmul r0, rf4, r4",
    "nop ; fmul r0, r0, rf10",
    "nop ; fmul rf8, r0, 0x43000000",
    "fsub r0, r0, r0 ; nop",
    "fsub r0, r0, rf5 ; nop",
    "nop ; fmul r0, r0, r4",
    "nop ; fmul r0, r0, rf18", // scale_p_y, not the shared scale_p
    "nop ; fmul rf9, r0, 0x43000000",
    "ftoin rf8, rf8 ; nop",
    "ftoin rf9, rf9 ; nop",
    "nop ; fmul rf13, rf6, r4",
    /* The Z scale and offset come from the uniform stream
     * (v3d_my_uniforms.z_scale / .z_offset, draw.c), APPENDED after the 16
     * matrix values -- so these two reads must stay the LAST ldunif* in this
     * shader. rf0/rf1 held x_m/y_m and are dead from the end of the matrix
     * multiply above to the end of the shader, so no new register is needed.
     * Two instructions of slack before first use; the matrix multiply above
     * proves one is enough. */
    "nop ; nop ; ldunifrf.rf19",          // viewport z scale  (context->sz)   // moved off rf0: rf0-rf2 must keep the object position
    "nop ; nop ; ldunifrf.rf20",          // viewport z offset (context->az)
    "nop ; fmul rf13, rf13, rf19",
    "fadd rf13, rf13, rf20 ; nop",

    "stvpmv 0, rf8 ; nop",
    "stvpmv 1, rf9 ; nop",
    "stvpmv 2, rf13 ; nop",
    "stvpmv 3, r4 ; nop",

    /* ---- EYE-SPACE LIGHTING. rf4..rf9 are all dead from here: rf4-rf7
     * held the clip position and rf8/rf9 the screen x/y, both already
     * written to the VPM above. ---- */
    "nop ; nop ; ldunif",                   // mv11
    "nop ; fmul rf4, rf0, r5 ; ldunif",      // mv12
    "nop ; fmul r0, rf1, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",      // mv13
    "nop ; fmul r0, rf2, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",      // mv14
    "nop ; fmul r0, rf3, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",      // mv21
    "nop ; fmul rf5, rf0, r5 ; ldunif",      // mv22
    "nop ; fmul r0, rf1, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",      // mv23
    "nop ; fmul r0, rf2, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",      // mv24
    "nop ; fmul r0, rf3, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",      // mv31
    "nop ; fmul rf6, rf0, r5 ; ldunif",      // mv32
    "nop ; fmul r0, rf1, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",      // mv33
    "nop ; fmul r0, rf2, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",      // mv34
    "nop ; fmul r0, rf3, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",      // it11
    "nop ; fmul rf7, rf14, r5 ; ldunif",      // it12
    "nop ; fmul r0, rf15, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",      // it13
    "nop ; fmul r0, rf16, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",      // it21
    "nop ; fmul rf8, rf14, r5 ; ldunif",      // it22
    "nop ; fmul r0, rf15, r5",
    "fadd rf8, rf8, r0 ; nop ; ldunif",      // it23
    "nop ; fmul r0, rf16, r5",
    "fadd rf8, rf8, r0 ; nop ; ldunif",      // it31
    "nop ; fmul rf9, rf14, r5 ; ldunif",      // it32
    "nop ; fmul r0, rf15, r5",
    "fadd rf9, rf9, r0 ; nop ; ldunif",      // it33
    "nop ; fmul r0, rf16, r5",
    "fadd rf9, rf9, r0 ; nop",
    /* |N_eye| is what object space got wrong: it divided by |n|. */
    "nop ; fmul r0, rf7, rf7",
    "nop ; fmul r1, rf8, rf8",
    "fadd r0, r0, r1 ; fmul r2, rf9, rf9",
    "fadd r0, r0, r2 ; nop",
    "or rsqrt, r0, r0 ; nop",
    "nop ; nop",
    "nop ; fmul rf7, rf7, r4",
    "nop ; fmul rf8, rf8, r4",
    "nop ; fmul rf9, rf9, r4",

    /* ---------------------------------------------------------------- LIGHTING
     * Object space, one light, ambient + diffuse + specular.
     *
     * r4 IS FREE HERE. It carried 1/w from "or recip, rf7, rf7" up to the
     * "stvpmv 3, r4" just above, and nothing reads it again, so both SFU
     * lookups below can use it without disturbing the matrix core. The block
     * sits between the position stores and the colour stores for exactly that
     * reason.
     *
     * THE UNIFORM TAIL IS POSITIONAL -- ldunif carries no address. It is read
     * in this order and the CPU writes it in this order: light position (3),
     * view direction (3), shininess (1), diffuse product (3), specular product (3), base
     * colour (3), shininess (1), diffuse product (3), specular product (3), base
     * colour (3), alpha (1) -- SEVENTEEN words, and seventeen instructions
     * minimum: one signal field per instruction is a hardware floor.
     *
     * The two trailing z_scale/z_offset ldunifrf above MUST stay the last reads
     * before the first read here, or the tail's first word arrives as z_scale.
     *
     * Every operand pair keeps to at most TWO register-file reads, which is the
     * port limit; partial products live in the accumulators r0-r2.
     */

    /* Zero, made by subtracting rf3 from itself -- the idiom the screen-space
     * conversion above already uses. rf3 holds w_m = 1.0 and is dead from the end
     * of the matrix multiply, so 1.0 needs no register of its own. */
    "fsub rf22, rf3, rf3 ; nop",

    /* L = light_position_object - vertex_position_object. The light arrives
     * already carried into object space by the CPU (draw.c), once per draw,
     * which is why no inverse matrix appears in this shader. */
    "nop ; nop ; ldunif",
    "fsub rf23, r5, rf4 ; nop ; ldunif",
    "fsub rf24, r5, rf5 ; nop ; ldunif",
    "fsub rf25, r5, rf6 ; nop",

    /* |L|^2, then r4 = 1/|L|. THE NORMALIZE IS REQUIRED, not an option: the
     * light is positional (w != 0), so GL defines the direction per vertex as
     * normalize(P_light - P_vertex). A constant direction would flatten the
     * specular highlight to one value across a whole surface. */
    "nop ; fmul r0, rf23, rf23",
    "nop ; fmul r1, rf24, rf24",
    "fadd r0, r0, r1 ; fmul r2, rf25, rf25",
    "fadd r0, r0, r2 ; nop",
    /* Magic-waddr SFU, never the ALU-op rsqrt spelling: the validator's
     * sfu_writes counter only inspects magic writes, so the ALU-op form's r4
     * latency is unchecked and a violation would assemble clean and fail on
     * hardware. This form ships in four fragment shaders. */
    "or rsqrt, r0, r0 ; nop",
    /* Exactly ONE instruction between the SFU write and the r4 read; it earns
     * its keep by starting the view-direction reads. */
    /* V is eye-space (0,0,1), a constant now, so H = L + z needs no
     * uniform and the three view-direction words leave the tail. The
     * instruction STAYS as a nop: it also fills the mandatory gap after
     * the rsqrt above, and r4 cannot be read in the next instruction. */
    "nop ; nop",
    "nop ; fmul rf23, rf23, r4",
    "nop ; fmul rf24, rf24, r4",
    "nop ; fmul rf25, rf25, r4",

    /* H = Lhat + Vobj, the half vector. V is the eye-space view direction
     * (0,0,1) carried into object space and normalised by the CPU;
     * GL_LIGHT_MODEL_LOCAL_VIEWER is false, so it is constant for the draw. */
    "or rf26, rf23, rf23 ; nop",
    "or rf27, rf24, rf24 ; nop",
    "fadd rf28, rf25, rf3 ; nop",

    "nop ; fmul r0, rf26, rf26",
    "nop ; fmul r1, rf27, rf27",
    "fadd r0, r0, r1 ; fmul r2, rf28, rf28",
    "fadd r0, r0, r2 ; nop",
    "or rsqrt, r0, r0 ; nop",
    "nop ; nop ; ldunif",                  // latency slot; r5 = shininess
    "nop ; fmul rf26, rf26, r4",
    "nop ; fmul rf27, rf27, r4",
    "nop ; fmul rf28, rf28, r4",

    /* N . Lhat, clamped at zero. The normal is used UNNORMALISED, which is GL:
     * with normalisation disabled -- and this GL has no GL_NORMALIZE token at
     * all -- the transformed normal is fed to the lighting equation as it is. */
    "nop ; fmul r0, rf7, rf23",
    "nop ; fmul r1, rf8, rf24",
    "fadd r0, r0, r1 ; fmul r2, rf9, rf25",
    "fadd r0, r0, r2 ; nop",
    "fmax rf29, r0, rf22 ; nop",

    /* N . Hhat, clamped at zero. fmax and not a predicated write: the only
     * conditional register writes in this whole tree are a fragment-stage
     * setmsf, and there are zero examples of predication to copy. */
    "nop ; fmul r0, rf7, rf26",
    "nop ; fmul r1, rf8, rf27",
    "fadd r0, r0, r1 ; fmul r2, rf9, rf28",
    "fadd r0, r0, r2 ; nop",
    /* Clamped to 2^-8 and NOT to zero, which the log below requires: log2(0) is
     * -inf, and shininess 0 would then give 0 * -inf = NaN where GL wants 1. At
     * 2^-8 the smallest representable result is 2^-8 raised to the shininess,
     * which is zero in eight bits for any exponent above 3, so nothing visible
     * changes and the NaN cannot arise. */
    "fmax rf30, r0, 0x3b800000 ; nop",

    /* (N.Hhat) ^ shininess = exp2(shininess * log2(N.Hhat)), for ANY shininess in
     * GL's [0,128] rather than the fixed exponent repeated squaring would give.
     * Both SFU forms are the magic-waddr spelling; `exp` ships in 50 fog shaders,
     * `log` is the first use of that waddr in this tree.
     *
     * The result STAYS IN r4 and is used from there by the specular multiplies
     * below -- no SFU lookup follows, so nothing disturbs it, and skipping the
     * move back to a register is what pays for the log and exp. */
    "or log, rf30, rf30 ; nop",
    "nop ; nop",
    "nop ; fmul r0, r4, r5",
    "or exp, r0, r0 ; nop",
    "nop ; nop",

    /* colour = base + diffuse_product * (N.L) + specular_product * (N.H)^10.
     * The products are folded on the CPU (light.c), so no light x material
     * multiply happens per vertex. */
    "nop ; nop ; ldunif",
    "nop ; fmul rf31, r5, rf29 ; ldunif",
    "nop ; fmul rf3, r5, rf29 ; ldunif",
    "nop ; fmul rf4, r5, rf29 ; ldunif",
    "nop ; fmul r0, r5, r4 ; ldunif",
    "fadd rf31, rf31, r0 ; fmul r1, r5, r4 ; ldunif",
    "fadd rf3, rf3, r1 ; fmul r2, r5, r4 ; ldunif",
    "fadd rf4, rf4, r2 ; nop",
    "fadd rf31, rf31, r5 ; nop ; ldunif",
    "fadd rf3, rf3, r5 ; nop ; ldunif",
    "fadd rf4, rf4, r5 ; nop ; ldunif",
    "or rf17, r5, r5 ; nop",
    /* ---- GL_COLOR_MATERIAL: the K1 half of the folded terms ---- */
    "ldvpmv_in rf5, 8 ; nop",
    "ldvpmv_in rf6, 9 ; nop",
    "ldvpmv_in rf7, 10 ; nop",
    "ldvpmv_in rf8, 11 ; nop",
    /* k per channel: base + diffuse*N.L + specular*spec. The tail is
     * grouped by channel, so each group is three consecutive words. */
    "nop ; nop ; ldunif",
    "or rf0, r5, r5 ; nop ; ldunif",
    "nop ; fmul r0, r5, rf29 ; ldunif",
    "nop ; fmul r1, r5, r4 ; ldunif",
    "fadd r0, r0, r1 ; nop",
    "fadd rf0, rf0, r0 ; nop",
    "or rf1, r5, r5 ; nop ; ldunif",
    "nop ; fmul r0, r5, rf29 ; ldunif",
    "nop ; fmul r1, r5, r4 ; ldunif",
    "fadd r0, r0, r1 ; nop",
    "fadd rf1, rf1, r0 ; nop",
    "or rf2, r5, r5 ; nop ; ldunif",
    "nop ; fmul r0, r5, rf29 ; ldunif",
    "nop ; fmul r1, r5, r4 ; ldunif",
    "fadd r0, r0, r1 ; nop",
    "fadd rf2, rf2, r0 ; nop",
    /* accumulate C * k, and the alpha scale still sitting in r5 */
    "nop ; fmul r0, rf0, rf5",
    "nop ; fmul r1, rf1, rf6",
    "nop ; fmul r2, rf2, rf7",
    "fadd rf31, rf31, r0 ; nop",
    "fadd rf3, rf3, r1 ; fmul r0, r5, rf8",
    "fadd rf4, rf4, r2 ; nop",
    "fadd rf17, rf17, r0 ; nop",

    /* GL clamps the final vertex colour to [0,1]. Only the upper clamp is
     * needed: every term above is non-negative, the two dot products having
     * been clamped at zero already.
     *
     * 1.0 IS REMATERIALISED HERE, and not taken from rf3 the way the untextured
     * twin does it. This variant reuses rf3 as its GREEN accumulator above --
     * legal, w_m being dead by then -- so clamping against rf3 clamped every
     * channel against green: emission 0.8/0.2/0.2 came out 0.2/0.2/0.2. rf30 is
     * dead after the log above. */
    "or rf30, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf14, rf31, rf30 ; nop",
    "fmin rf15, rf3,  rf30 ; nop",
    "fmin rf16, rf4,  rf30 ; nop",

    "stvpmv 4, rf11 ; nop", // s
    "stvpmv 5, rf12 ; nop", // t
    "stvpmv 6, rf14 ; nop", // lit colour r
    "stvpmv 7, rf15 ; nop", // lit colour g
    "stvpmv 8, rf16 ; nop", // lit colour b
    "stvpmv 9, rf17 ; nop", // a = 1.0

    "vpmwt -              ; nop",
    "nop                  ; nop ; thrsw",
    "nop                  ; nop",
    "nop                  ; nop",
};


/*
 * LIT, MULTITEXTURED. g_vertex_shader_lit_textured_assembly plus unit 1's
 * texcoord pair, which is why it is derived from that rather than from
 * g_vertex_shader_multitexture_assembly: the lighting block is 106 of these
 * instructions and copying four is cheaper than splicing it.
 *
 * INPUTS  0,1,2 position | 3,4 unit 0 s,t | 5,6 unit 1 s,t | 7,8,9 normal
 * OUTPUTS 0..3 position  | 4,5 unit 0 s,t | 6,7 unit 1 s,t | 8..11 lit colour
 *
 * Ten input words, so draw.c gives this shape TWO VPM input sectors.
 *
 * Unit 1's pair is read into rf21 and written out immediately, twice, rather
 * than held: rf21 is the only register this shader leaves unused, and every
 * register that falls dead after the position output is written before it.
 */
static const char* g_vertex_shader_lit_multitexture_assembly[] = {
    "or rf3, 0x3f800000, 0x3f800000 ; nop", // w_m = 1.0

    "nop ; nop ; ldunifrf.rf10", // scale_p

    /* Separate Y-axis screen-space scale -- rf18, since rf14-17 are
     * already taken by color r/g/b/a in this variant. */
    "nop ; nop ; ldunifrf.rf18", // scale_p_y

    "ldvpmv_in rf0,  0 ; nop", // x_m
    "ldvpmv_in rf1,  1 ; nop", // y_m
    "ldvpmv_in rf2,  2 ; nop", // z_m

    "ldvpmv_in rf11,  3 ; nop", // s
    "ldvpmv_in rf12,  4 ; nop",
    /* UNIT 1's texcoord pair, read and written straight out. rf21 is
     * the only register unused by this shader and every other free one
     * is written before the position output, so there is nothing to
     * hold these in; stvpmv addresses its slot, so writing 6 and 7 here
     * and the rest below is committed together by the vpmwt. */
    "ldvpmv_in rf21,  5 ; nop",
    "stvpmv 6, rf21 ; nop",
    "ldvpmv_in rf21,  6 ; nop",
    "stvpmv 7, rf21 ; nop", // t

    /* THE NORMAL, object space, in the registers that hold colour r/g/b,
     * for the same reason as in the untextured variant above: the
     * matrix-multiply section never touches them.
     *
     * VPM input words are 0-2 position + 3-4 s,t + 5-7 normal = EIGHT, one
     * word fewer than the smooth textured shader, the colour record being
     * dropped for a lit draw. Eight is exactly one sector; the field stays
     * at the 2 the combined path already sets, which is harmless. */
    "ldvpmv_in rf14,  7 ; nop", // normal x
    "ldvpmv_in rf15,  8 ; nop", // normal y
    "ldvpmv_in rf16,  9 ; nop", // normal z

    /* Alpha, constant 1.0. */
    "or rf17, 0x3f800000, 0x3f800000 ; nop", // a = 1.0
    /* Matrix multiply -- byte-for-byte identical to g_vertex_shader_assembly. */
    "nop ; nop ; ldunif",
    "nop ; fmul rf4, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul rf5, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul rf6, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul rf7, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf7, rf7, r0 ; nop",

    "or recip, rf7, rf7 ; nop",
    "nop ; nop",
    "nop ; fmul r0, rf4, r4",
    "nop ; fmul r0, r0, rf10",
    "nop ; fmul rf8, r0, 0x43000000",
    "fsub r0, r0, r0 ; nop",
    "fsub r0, r0, rf5 ; nop",
    "nop ; fmul r0, r0, r4",
    "nop ; fmul r0, r0, rf18", // scale_p_y, not the shared scale_p
    "nop ; fmul rf9, r0, 0x43000000",
    "ftoin rf8, rf8 ; nop",
    "ftoin rf9, rf9 ; nop",
    "nop ; fmul rf13, rf6, r4",
    /* The Z scale and offset come from the uniform stream
     * (v3d_my_uniforms.z_scale / .z_offset, draw.c), APPENDED after the 16
     * matrix values -- so these two reads must stay the LAST ldunif* in this
     * shader. rf0/rf1 held x_m/y_m and are dead from the end of the matrix
     * multiply above to the end of the shader, so no new register is needed.
     * Two instructions of slack before first use; the matrix multiply above
     * proves one is enough. */
    "nop ; nop ; ldunifrf.rf19",          // viewport z scale  (context->sz)   // moved off rf0: rf0-rf2 must keep the object position
    "nop ; nop ; ldunifrf.rf20",          // viewport z offset (context->az)
    "nop ; fmul rf13, rf13, rf19",
    "fadd rf13, rf13, rf20 ; nop",

    "stvpmv 0, rf8 ; nop",
    "stvpmv 1, rf9 ; nop",
    "stvpmv 2, rf13 ; nop",
    "stvpmv 3, r4 ; nop",

    /* ---- EYE-SPACE LIGHTING. rf4..rf9 are all dead from here: rf4-rf7
     * held the clip position and rf8/rf9 the screen x/y, both already
     * written to the VPM above. ---- */
    "nop ; nop ; ldunif",                   // mv11
    "nop ; fmul rf4, rf0, r5 ; ldunif",      // mv12
    "nop ; fmul r0, rf1, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",      // mv13
    "nop ; fmul r0, rf2, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",      // mv14
    "nop ; fmul r0, rf3, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",      // mv21
    "nop ; fmul rf5, rf0, r5 ; ldunif",      // mv22
    "nop ; fmul r0, rf1, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",      // mv23
    "nop ; fmul r0, rf2, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",      // mv24
    "nop ; fmul r0, rf3, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",      // mv31
    "nop ; fmul rf6, rf0, r5 ; ldunif",      // mv32
    "nop ; fmul r0, rf1, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",      // mv33
    "nop ; fmul r0, rf2, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",      // mv34
    "nop ; fmul r0, rf3, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",      // it11
    "nop ; fmul rf7, rf14, r5 ; ldunif",      // it12
    "nop ; fmul r0, rf15, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",      // it13
    "nop ; fmul r0, rf16, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",      // it21
    "nop ; fmul rf8, rf14, r5 ; ldunif",      // it22
    "nop ; fmul r0, rf15, r5",
    "fadd rf8, rf8, r0 ; nop ; ldunif",      // it23
    "nop ; fmul r0, rf16, r5",
    "fadd rf8, rf8, r0 ; nop ; ldunif",      // it31
    "nop ; fmul rf9, rf14, r5 ; ldunif",      // it32
    "nop ; fmul r0, rf15, r5",
    "fadd rf9, rf9, r0 ; nop ; ldunif",      // it33
    "nop ; fmul r0, rf16, r5",
    "fadd rf9, rf9, r0 ; nop",
    /* |N_eye| is what object space got wrong: it divided by |n|. */
    "nop ; fmul r0, rf7, rf7",
    "nop ; fmul r1, rf8, rf8",
    "fadd r0, r0, r1 ; fmul r2, rf9, rf9",
    "fadd r0, r0, r2 ; nop",
    "or rsqrt, r0, r0 ; nop",
    "nop ; nop",
    "nop ; fmul rf7, rf7, r4",
    "nop ; fmul rf8, rf8, r4",
    "nop ; fmul rf9, rf9, r4",

    /* ---------------------------------------------------------------- LIGHTING
     * Object space, one light, ambient + diffuse + specular.
     *
     * r4 IS FREE HERE. It carried 1/w from "or recip, rf7, rf7" up to the
     * "stvpmv 3, r4" just above, and nothing reads it again, so both SFU
     * lookups below can use it without disturbing the matrix core. The block
     * sits between the position stores and the colour stores for exactly that
     * reason.
     *
     * THE UNIFORM TAIL IS POSITIONAL -- ldunif carries no address. It is read
     * in this order and the CPU writes it in this order: light position (3),
     * view direction (3), shininess (1), diffuse product (3), specular product (3), base
     * colour (3), shininess (1), diffuse product (3), specular product (3), base
     * colour (3), alpha (1) -- SEVENTEEN words, and seventeen instructions
     * minimum: one signal field per instruction is a hardware floor.
     *
     * The two trailing z_scale/z_offset ldunifrf above MUST stay the last reads
     * before the first read here, or the tail's first word arrives as z_scale.
     *
     * Every operand pair keeps to at most TWO register-file reads, which is the
     * port limit; partial products live in the accumulators r0-r2.
     */

    /* Zero, made by subtracting rf3 from itself -- the idiom the screen-space
     * conversion above already uses. rf3 holds w_m = 1.0 and is dead from the end
     * of the matrix multiply, so 1.0 needs no register of its own. */
    "fsub rf22, rf3, rf3 ; nop",

    /* L = light_position_object - vertex_position_object. The light arrives
     * already carried into object space by the CPU (draw.c), once per draw,
     * which is why no inverse matrix appears in this shader. */
    "nop ; nop ; ldunif",
    "fsub rf23, r5, rf4 ; nop ; ldunif",
    "fsub rf24, r5, rf5 ; nop ; ldunif",
    "fsub rf25, r5, rf6 ; nop",

    /* |L|^2, then r4 = 1/|L|. THE NORMALIZE IS REQUIRED, not an option: the
     * light is positional (w != 0), so GL defines the direction per vertex as
     * normalize(P_light - P_vertex). A constant direction would flatten the
     * specular highlight to one value across a whole surface. */
    "nop ; fmul r0, rf23, rf23",
    "nop ; fmul r1, rf24, rf24",
    "fadd r0, r0, r1 ; fmul r2, rf25, rf25",
    "fadd r0, r0, r2 ; nop",
    /* Magic-waddr SFU, never the ALU-op rsqrt spelling: the validator's
     * sfu_writes counter only inspects magic writes, so the ALU-op form's r4
     * latency is unchecked and a violation would assemble clean and fail on
     * hardware. This form ships in four fragment shaders. */
    "or rsqrt, r0, r0 ; nop",
    /* Exactly ONE instruction between the SFU write and the r4 read; it earns
     * its keep by starting the view-direction reads. */
    /* V is eye-space (0,0,1), a constant now, so H = L + z needs no
     * uniform and the three view-direction words leave the tail. The
     * instruction STAYS as a nop: it also fills the mandatory gap after
     * the rsqrt above, and r4 cannot be read in the next instruction. */
    "nop ; nop",
    "nop ; fmul rf23, rf23, r4",
    "nop ; fmul rf24, rf24, r4",
    "nop ; fmul rf25, rf25, r4",

    /* H = Lhat + Vobj, the half vector. V is the eye-space view direction
     * (0,0,1) carried into object space and normalised by the CPU;
     * GL_LIGHT_MODEL_LOCAL_VIEWER is false, so it is constant for the draw. */
    "or rf26, rf23, rf23 ; nop",
    "or rf27, rf24, rf24 ; nop",
    "fadd rf28, rf25, rf3 ; nop",

    "nop ; fmul r0, rf26, rf26",
    "nop ; fmul r1, rf27, rf27",
    "fadd r0, r0, r1 ; fmul r2, rf28, rf28",
    "fadd r0, r0, r2 ; nop",
    "or rsqrt, r0, r0 ; nop",
    "nop ; nop ; ldunif",                  // latency slot; r5 = shininess
    "nop ; fmul rf26, rf26, r4",
    "nop ; fmul rf27, rf27, r4",
    "nop ; fmul rf28, rf28, r4",

    /* N . Lhat, clamped at zero. The normal is used UNNORMALISED, which is GL:
     * with normalisation disabled -- and this GL has no GL_NORMALIZE token at
     * all -- the transformed normal is fed to the lighting equation as it is. */
    "nop ; fmul r0, rf7, rf23",
    "nop ; fmul r1, rf8, rf24",
    "fadd r0, r0, r1 ; fmul r2, rf9, rf25",
    "fadd r0, r0, r2 ; nop",
    "fmax rf29, r0, rf22 ; nop",

    /* N . Hhat, clamped at zero. fmax and not a predicated write: the only
     * conditional register writes in this whole tree are a fragment-stage
     * setmsf, and there are zero examples of predication to copy. */
    "nop ; fmul r0, rf7, rf26",
    "nop ; fmul r1, rf8, rf27",
    "fadd r0, r0, r1 ; fmul r2, rf9, rf28",
    "fadd r0, r0, r2 ; nop",
    /* Clamped to 2^-8 and NOT to zero, which the log below requires: log2(0) is
     * -inf, and shininess 0 would then give 0 * -inf = NaN where GL wants 1. At
     * 2^-8 the smallest representable result is 2^-8 raised to the shininess,
     * which is zero in eight bits for any exponent above 3, so nothing visible
     * changes and the NaN cannot arise. */
    "fmax rf30, r0, 0x3b800000 ; nop",

    /* (N.Hhat) ^ shininess = exp2(shininess * log2(N.Hhat)), for ANY shininess in
     * GL's [0,128] rather than the fixed exponent repeated squaring would give.
     * Both SFU forms are the magic-waddr spelling; `exp` ships in 50 fog shaders,
     * `log` is the first use of that waddr in this tree.
     *
     * The result STAYS IN r4 and is used from there by the specular multiplies
     * below -- no SFU lookup follows, so nothing disturbs it, and skipping the
     * move back to a register is what pays for the log and exp. */
    "or log, rf30, rf30 ; nop",
    "nop ; nop",
    "nop ; fmul r0, r4, r5",
    "or exp, r0, r0 ; nop",
    "nop ; nop",

    /* colour = base + diffuse_product * (N.L) + specular_product * (N.H)^10.
     * The products are folded on the CPU (light.c), so no light x material
     * multiply happens per vertex. */
    "nop ; nop ; ldunif",
    "nop ; fmul rf31, r5, rf29 ; ldunif",
    "nop ; fmul rf3, r5, rf29 ; ldunif",
    "nop ; fmul rf4, r5, rf29 ; ldunif",
    "nop ; fmul r0, r5, r4 ; ldunif",
    "fadd rf31, rf31, r0 ; fmul r1, r5, r4 ; ldunif",
    "fadd rf3, rf3, r1 ; fmul r2, r5, r4 ; ldunif",
    "fadd rf4, rf4, r2 ; nop",
    "fadd rf31, rf31, r5 ; nop ; ldunif",
    "fadd rf3, rf3, r5 ; nop ; ldunif",
    "fadd rf4, rf4, r5 ; nop ; ldunif",
    "or rf17, r5, r5 ; nop",

    /* GL clamps the final vertex colour to [0,1]. Only the upper clamp is
     * needed: every term above is non-negative, the two dot products having
     * been clamped at zero already.
     *
     * 1.0 IS REMATERIALISED HERE, and not taken from rf3 the way the untextured
     * twin does it. This variant reuses rf3 as its GREEN accumulator above --
     * legal, w_m being dead by then -- so clamping against rf3 clamped every
     * channel against green: emission 0.8/0.2/0.2 came out 0.2/0.2/0.2. rf30 is
     * dead after the log above. */
    "or rf30, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf14, rf31, rf30 ; nop",
    "fmin rf15, rf3,  rf30 ; nop",
    "fmin rf16, rf4,  rf30 ; nop",

    "stvpmv 4, rf11 ; nop", // s
    "stvpmv 5, rf12 ; nop", // t
    "stvpmv 8, rf14 ; nop", // lit colour r
    "stvpmv 9, rf15 ; nop", // lit colour g
    "stvpmv 10, rf16 ; nop", // lit colour b
    "stvpmv 11, rf17 ; nop", // a = 1.0

    "vpmwt -              ; nop",
    "nop                  ; nop ; thrsw",
    "nop                  ; nop",
    "nop                  ; nop",
};

/*
 * LIT, MULTITEXTURED, with GL_COLOR_MATERIAL. lit_multitexture
 * plus the K1 half of light.c's fold: the vertex colour arrives as a fourth
 * attribute at VPM inputs 10..13 and each channel gains
 * C * (K1base + K1diff * N.L + K1spec * spec). With a mode that tracks nothing
 * every K1 is zero and this computes exactly what the host shader does.
 */
static const char* g_vertex_shader_lit_multitexture_colormaterial_assembly[] = {
    "or rf3, 0x3f800000, 0x3f800000 ; nop", // w_m = 1.0

    "nop ; nop ; ldunifrf.rf10", // scale_p

    /* Separate Y-axis screen-space scale -- rf18, since rf14-17 are
     * already taken by color r/g/b/a in this variant. */
    "nop ; nop ; ldunifrf.rf18", // scale_p_y

    "ldvpmv_in rf0,  0 ; nop", // x_m
    "ldvpmv_in rf1,  1 ; nop", // y_m
    "ldvpmv_in rf2,  2 ; nop", // z_m

    "ldvpmv_in rf11,  3 ; nop", // s
    "ldvpmv_in rf12,  4 ; nop",
    /* UNIT 1's texcoord pair, read and written straight out. rf21 is
     * the only register unused by this shader and every other free one
     * is written before the position output, so there is nothing to
     * hold these in; stvpmv addresses its slot, so writing 6 and 7 here
     * and the rest below is committed together by the vpmwt. */
    "ldvpmv_in rf21,  5 ; nop",
    "stvpmv 6, rf21 ; nop",
    "ldvpmv_in rf21,  6 ; nop",
    "stvpmv 7, rf21 ; nop", // t

    /* THE NORMAL, object space, in the registers that hold colour r/g/b,
     * for the same reason as in the untextured variant above: the
     * matrix-multiply section never touches them.
     *
     * VPM input words are 0-2 position + 3-4 s,t + 5-7 normal = EIGHT, one
     * word fewer than the smooth textured shader, the colour record being
     * dropped for a lit draw. Eight is exactly one sector; the field stays
     * at the 2 the combined path already sets, which is harmless. */
    "ldvpmv_in rf14,  7 ; nop", // normal x
    "ldvpmv_in rf15,  8 ; nop", // normal y
    "ldvpmv_in rf16,  9 ; nop", // normal z

    /* Alpha, constant 1.0. */
    "or rf17, 0x3f800000, 0x3f800000 ; nop", // a = 1.0
    /* Matrix multiply -- byte-for-byte identical to g_vertex_shader_assembly. */
    "nop ; nop ; ldunif",
    "nop ; fmul rf4, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul rf5, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul rf6, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul rf7, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf7, rf7, r0 ; nop",

    "or recip, rf7, rf7 ; nop",
    "nop ; nop",
    "nop ; fmul r0, rf4, r4",
    "nop ; fmul r0, r0, rf10",
    "nop ; fmul rf8, r0, 0x43000000",
    "fsub r0, r0, r0 ; nop",
    "fsub r0, r0, rf5 ; nop",
    "nop ; fmul r0, r0, r4",
    "nop ; fmul r0, r0, rf18", // scale_p_y, not the shared scale_p
    "nop ; fmul rf9, r0, 0x43000000",
    "ftoin rf8, rf8 ; nop",
    "ftoin rf9, rf9 ; nop",
    "nop ; fmul rf13, rf6, r4",
    /* The Z scale and offset come from the uniform stream
     * (v3d_my_uniforms.z_scale / .z_offset, draw.c), APPENDED after the 16
     * matrix values -- so these two reads must stay the LAST ldunif* in this
     * shader. rf0/rf1 held x_m/y_m and are dead from the end of the matrix
     * multiply above to the end of the shader, so no new register is needed.
     * Two instructions of slack before first use; the matrix multiply above
     * proves one is enough. */
    "nop ; nop ; ldunifrf.rf19",          // viewport z scale  (context->sz)   // moved off rf0: rf0-rf2 must keep the object position
    "nop ; nop ; ldunifrf.rf20",          // viewport z offset (context->az)
    "nop ; fmul rf13, rf13, rf19",
    "fadd rf13, rf13, rf20 ; nop",

    "stvpmv 0, rf8 ; nop",
    "stvpmv 1, rf9 ; nop",
    "stvpmv 2, rf13 ; nop",
    "stvpmv 3, r4 ; nop",

    /* ---- EYE-SPACE LIGHTING. rf4..rf9 are all dead from here: rf4-rf7
     * held the clip position and rf8/rf9 the screen x/y, both already
     * written to the VPM above. ---- */
    "nop ; nop ; ldunif",                   // mv11
    "nop ; fmul rf4, rf0, r5 ; ldunif",      // mv12
    "nop ; fmul r0, rf1, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",      // mv13
    "nop ; fmul r0, rf2, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",      // mv14
    "nop ; fmul r0, rf3, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",      // mv21
    "nop ; fmul rf5, rf0, r5 ; ldunif",      // mv22
    "nop ; fmul r0, rf1, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",      // mv23
    "nop ; fmul r0, rf2, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",      // mv24
    "nop ; fmul r0, rf3, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",      // mv31
    "nop ; fmul rf6, rf0, r5 ; ldunif",      // mv32
    "nop ; fmul r0, rf1, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",      // mv33
    "nop ; fmul r0, rf2, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",      // mv34
    "nop ; fmul r0, rf3, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",      // it11
    "nop ; fmul rf7, rf14, r5 ; ldunif",      // it12
    "nop ; fmul r0, rf15, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",      // it13
    "nop ; fmul r0, rf16, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",      // it21
    "nop ; fmul rf8, rf14, r5 ; ldunif",      // it22
    "nop ; fmul r0, rf15, r5",
    "fadd rf8, rf8, r0 ; nop ; ldunif",      // it23
    "nop ; fmul r0, rf16, r5",
    "fadd rf8, rf8, r0 ; nop ; ldunif",      // it31
    "nop ; fmul rf9, rf14, r5 ; ldunif",      // it32
    "nop ; fmul r0, rf15, r5",
    "fadd rf9, rf9, r0 ; nop ; ldunif",      // it33
    "nop ; fmul r0, rf16, r5",
    "fadd rf9, rf9, r0 ; nop",
    /* |N_eye| is what object space got wrong: it divided by |n|. */
    "nop ; fmul r0, rf7, rf7",
    "nop ; fmul r1, rf8, rf8",
    "fadd r0, r0, r1 ; fmul r2, rf9, rf9",
    "fadd r0, r0, r2 ; nop",
    "or rsqrt, r0, r0 ; nop",
    "nop ; nop",
    "nop ; fmul rf7, rf7, r4",
    "nop ; fmul rf8, rf8, r4",
    "nop ; fmul rf9, rf9, r4",

    /* ---------------------------------------------------------------- LIGHTING
     * Object space, one light, ambient + diffuse + specular.
     *
     * r4 IS FREE HERE. It carried 1/w from "or recip, rf7, rf7" up to the
     * "stvpmv 3, r4" just above, and nothing reads it again, so both SFU
     * lookups below can use it without disturbing the matrix core. The block
     * sits between the position stores and the colour stores for exactly that
     * reason.
     *
     * THE UNIFORM TAIL IS POSITIONAL -- ldunif carries no address. It is read
     * in this order and the CPU writes it in this order: light position (3),
     * view direction (3), shininess (1), diffuse product (3), specular product (3), base
     * colour (3), shininess (1), diffuse product (3), specular product (3), base
     * colour (3), alpha (1) -- SEVENTEEN words, and seventeen instructions
     * minimum: one signal field per instruction is a hardware floor.
     *
     * The two trailing z_scale/z_offset ldunifrf above MUST stay the last reads
     * before the first read here, or the tail's first word arrives as z_scale.
     *
     * Every operand pair keeps to at most TWO register-file reads, which is the
     * port limit; partial products live in the accumulators r0-r2.
     */

    /* Zero, made by subtracting rf3 from itself -- the idiom the screen-space
     * conversion above already uses. rf3 holds w_m = 1.0 and is dead from the end
     * of the matrix multiply, so 1.0 needs no register of its own. */
    "fsub rf22, rf3, rf3 ; nop",

    /* L = light_position_object - vertex_position_object. The light arrives
     * already carried into object space by the CPU (draw.c), once per draw,
     * which is why no inverse matrix appears in this shader. */
    "nop ; nop ; ldunif",
    "fsub rf23, r5, rf4 ; nop ; ldunif",
    "fsub rf24, r5, rf5 ; nop ; ldunif",
    "fsub rf25, r5, rf6 ; nop",

    /* |L|^2, then r4 = 1/|L|. THE NORMALIZE IS REQUIRED, not an option: the
     * light is positional (w != 0), so GL defines the direction per vertex as
     * normalize(P_light - P_vertex). A constant direction would flatten the
     * specular highlight to one value across a whole surface. */
    "nop ; fmul r0, rf23, rf23",
    "nop ; fmul r1, rf24, rf24",
    "fadd r0, r0, r1 ; fmul r2, rf25, rf25",
    "fadd r0, r0, r2 ; nop",
    /* Magic-waddr SFU, never the ALU-op rsqrt spelling: the validator's
     * sfu_writes counter only inspects magic writes, so the ALU-op form's r4
     * latency is unchecked and a violation would assemble clean and fail on
     * hardware. This form ships in four fragment shaders. */
    "or rsqrt, r0, r0 ; nop",
    /* Exactly ONE instruction between the SFU write and the r4 read; it earns
     * its keep by starting the view-direction reads. */
    /* V is eye-space (0,0,1), a constant now, so H = L + z needs no
     * uniform and the three view-direction words leave the tail. The
     * instruction STAYS as a nop: it also fills the mandatory gap after
     * the rsqrt above, and r4 cannot be read in the next instruction. */
    "nop ; nop",
    "nop ; fmul rf23, rf23, r4",
    "nop ; fmul rf24, rf24, r4",
    "nop ; fmul rf25, rf25, r4",

    /* H = Lhat + Vobj, the half vector. V is the eye-space view direction
     * (0,0,1) carried into object space and normalised by the CPU;
     * GL_LIGHT_MODEL_LOCAL_VIEWER is false, so it is constant for the draw. */
    "or rf26, rf23, rf23 ; nop",
    "or rf27, rf24, rf24 ; nop",
    "fadd rf28, rf25, rf3 ; nop",

    "nop ; fmul r0, rf26, rf26",
    "nop ; fmul r1, rf27, rf27",
    "fadd r0, r0, r1 ; fmul r2, rf28, rf28",
    "fadd r0, r0, r2 ; nop",
    "or rsqrt, r0, r0 ; nop",
    "nop ; nop ; ldunif",                  // latency slot; r5 = shininess
    "nop ; fmul rf26, rf26, r4",
    "nop ; fmul rf27, rf27, r4",
    "nop ; fmul rf28, rf28, r4",

    /* N . Lhat, clamped at zero. The normal is used UNNORMALISED, which is GL:
     * with normalisation disabled -- and this GL has no GL_NORMALIZE token at
     * all -- the transformed normal is fed to the lighting equation as it is. */
    "nop ; fmul r0, rf7, rf23",
    "nop ; fmul r1, rf8, rf24",
    "fadd r0, r0, r1 ; fmul r2, rf9, rf25",
    "fadd r0, r0, r2 ; nop",
    "fmax rf29, r0, rf22 ; nop",

    /* N . Hhat, clamped at zero. fmax and not a predicated write: the only
     * conditional register writes in this whole tree are a fragment-stage
     * setmsf, and there are zero examples of predication to copy. */
    "nop ; fmul r0, rf7, rf26",
    "nop ; fmul r1, rf8, rf27",
    "fadd r0, r0, r1 ; fmul r2, rf9, rf28",
    "fadd r0, r0, r2 ; nop",
    /* Clamped to 2^-8 and NOT to zero, which the log below requires: log2(0) is
     * -inf, and shininess 0 would then give 0 * -inf = NaN where GL wants 1. At
     * 2^-8 the smallest representable result is 2^-8 raised to the shininess,
     * which is zero in eight bits for any exponent above 3, so nothing visible
     * changes and the NaN cannot arise. */
    "fmax rf30, r0, 0x3b800000 ; nop",

    /* (N.Hhat) ^ shininess = exp2(shininess * log2(N.Hhat)), for ANY shininess in
     * GL's [0,128] rather than the fixed exponent repeated squaring would give.
     * Both SFU forms are the magic-waddr spelling; `exp` ships in 50 fog shaders,
     * `log` is the first use of that waddr in this tree.
     *
     * The result STAYS IN r4 and is used from there by the specular multiplies
     * below -- no SFU lookup follows, so nothing disturbs it, and skipping the
     * move back to a register is what pays for the log and exp. */
    "or log, rf30, rf30 ; nop",
    "nop ; nop",
    "nop ; fmul r0, r4, r5",
    "or exp, r0, r0 ; nop",
    "nop ; nop",

    /* colour = base + diffuse_product * (N.L) + specular_product * (N.H)^10.
     * The products are folded on the CPU (light.c), so no light x material
     * multiply happens per vertex. */
    "nop ; nop ; ldunif",
    "nop ; fmul rf31, r5, rf29 ; ldunif",
    "nop ; fmul rf3, r5, rf29 ; ldunif",
    "nop ; fmul rf4, r5, rf29 ; ldunif",
    "nop ; fmul r0, r5, r4 ; ldunif",
    "fadd rf31, rf31, r0 ; fmul r1, r5, r4 ; ldunif",
    "fadd rf3, rf3, r1 ; fmul r2, r5, r4 ; ldunif",
    "fadd rf4, rf4, r2 ; nop",
    "fadd rf31, rf31, r5 ; nop ; ldunif",
    "fadd rf3, rf3, r5 ; nop ; ldunif",
    "fadd rf4, rf4, r5 ; nop ; ldunif",
    "or rf17, r5, r5 ; nop",
    /* ---- GL_COLOR_MATERIAL: the K1 half of the folded terms ---- */
    "ldvpmv_in rf5, 10 ; nop",
    "ldvpmv_in rf6, 11 ; nop",
    "ldvpmv_in rf7, 12 ; nop",
    "ldvpmv_in rf8, 13 ; nop",
    /* k per channel: base + diffuse*N.L + specular*spec. The tail is
     * grouped by channel, so each group is three consecutive words. */
    "nop ; nop ; ldunif",
    "or rf0, r5, r5 ; nop ; ldunif",
    "nop ; fmul r0, r5, rf29 ; ldunif",
    "nop ; fmul r1, r5, r4 ; ldunif",
    "fadd r0, r0, r1 ; nop",
    "fadd rf0, rf0, r0 ; nop",
    "or rf1, r5, r5 ; nop ; ldunif",
    "nop ; fmul r0, r5, rf29 ; ldunif",
    "nop ; fmul r1, r5, r4 ; ldunif",
    "fadd r0, r0, r1 ; nop",
    "fadd rf1, rf1, r0 ; nop",
    "or rf2, r5, r5 ; nop ; ldunif",
    "nop ; fmul r0, r5, rf29 ; ldunif",
    "nop ; fmul r1, r5, r4 ; ldunif",
    "fadd r0, r0, r1 ; nop",
    "fadd rf2, rf2, r0 ; nop",
    /* accumulate C * k, and the alpha scale still sitting in r5 */
    "nop ; fmul r0, rf0, rf5",
    "nop ; fmul r1, rf1, rf6",
    "nop ; fmul r2, rf2, rf7",
    "fadd rf31, rf31, r0 ; nop",
    "fadd rf3, rf3, r1 ; fmul r0, r5, rf8",
    "fadd rf4, rf4, r2 ; nop",
    "fadd rf17, rf17, r0 ; nop",

    /* GL clamps the final vertex colour to [0,1]. Only the upper clamp is
     * needed: every term above is non-negative, the two dot products having
     * been clamped at zero already.
     *
     * 1.0 IS REMATERIALISED HERE, and not taken from rf3 the way the untextured
     * twin does it. This variant reuses rf3 as its GREEN accumulator above --
     * legal, w_m being dead by then -- so clamping against rf3 clamped every
     * channel against green: emission 0.8/0.2/0.2 came out 0.2/0.2/0.2. rf30 is
     * dead after the log above. */
    "or rf30, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf14, rf31, rf30 ; nop",
    "fmin rf15, rf3,  rf30 ; nop",
    "fmin rf16, rf4,  rf30 ; nop",

    "stvpmv 4, rf11 ; nop", // s
    "stvpmv 5, rf12 ; nop", // t
    "stvpmv 8, rf14 ; nop", // lit colour r
    "stvpmv 9, rf15 ; nop", // lit colour g
    "stvpmv 10, rf16 ; nop", // lit colour b
    "stvpmv 11, rf17 ; nop", // a = 1.0

    "vpmwt -              ; nop",
    "nop                  ; nop ; thrsw",
    "nop                  ; nop",
    "nop                  ; nop",
};



/*
 * LIT, TEXTURED, REAL PER-VERTEX W. g_vertex_shader_lit_textured_assembly with
 * three changes and nothing else: w is read from VPM index 3 rather than set to
 * 1.0, every later input index shifts by one because draw.c widens the position
 * record to four components for needs_real_w, and the half-vector's +1.0 is a
 * small immediate because rf3 now holds that w.
 *
 * The shape it serves is the only one it can: real_w_combined requires
 * `combined` or `smooth_alphatest`, and BOTH of those require textured and
 * exclude multitextured -- so one shader covers every lit real-w draw, and the
 * vex selector testing multitextured first costs nothing. No fragment shader is
 * needed; that stage only sees interpolated varyings.
 */
static const char* g_vertex_shader_lit_realw_assembly[] = {
    "ldvpmv_in rf3, 3 ; nop",   // the REAL per-vertex w, where lit_textured had 1.0

    "nop ; nop ; ldunifrf.rf10", // scale_p

    /* Separate Y-axis screen-space scale -- rf18, since rf14-17 are
     * already taken by color r/g/b/a in this variant. */
    "nop ; nop ; ldunifrf.rf18", // scale_p_y

    "ldvpmv_in rf0,  0 ; nop", // x_m
    "ldvpmv_in rf1,  1 ; nop", // y_m
    "ldvpmv_in rf2,  2 ; nop", // z_m

    "ldvpmv_in rf11,  4 ; nop", // s
    "ldvpmv_in rf12,  5 ; nop", // t

    /* THE NORMAL, object space, in the registers that hold colour r/g/b,
     * for the same reason as in the untextured variant above: the
     * matrix-multiply section never touches them.
     *
     * VPM input words are 0-2 position + 3-4 s,t + 5-7 normal = EIGHT, one
     * word fewer than the smooth textured shader, the colour record being
     * dropped for a lit draw. Eight is exactly one sector; the field stays
     * at the 2 the combined path already sets, which is harmless. */
    "ldvpmv_in rf14,  6 ; nop", // normal x
    "ldvpmv_in rf15,  7 ; nop", // normal y
    "ldvpmv_in rf16,  8 ; nop", // normal z

    /* Alpha, constant 1.0. */
    "or rf17, 0x3f800000, 0x3f800000 ; nop", // a = 1.0
    /* Matrix multiply -- byte-for-byte identical to g_vertex_shader_assembly. */
    "nop ; nop ; ldunif",
    "nop ; fmul rf4, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul rf5, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul rf6, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul rf7, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf7, rf7, r0 ; nop",

    "or recip, rf7, rf7 ; nop",
    "nop ; nop",
    "nop ; fmul r0, rf4, r4",
    "nop ; fmul r0, r0, rf10",
    "nop ; fmul rf8, r0, 0x43000000",
    "fsub r0, r0, r0 ; nop",
    "fsub r0, r0, rf5 ; nop",
    "nop ; fmul r0, r0, r4",
    "nop ; fmul r0, r0, rf18", // scale_p_y, not the shared scale_p
    "nop ; fmul rf9, r0, 0x43000000",
    "ftoin rf8, rf8 ; nop",
    "ftoin rf9, rf9 ; nop",
    "nop ; fmul rf13, rf6, r4",
    /* The Z scale and offset come from the uniform stream
     * (v3d_my_uniforms.z_scale / .z_offset, draw.c), APPENDED after the 16
     * matrix values -- so these two reads must stay the LAST ldunif* in this
     * shader. rf0/rf1 held x_m/y_m and are dead from the end of the matrix
     * multiply above to the end of the shader, so no new register is needed.
     * Two instructions of slack before first use; the matrix multiply above
     * proves one is enough. */
    "nop ; nop ; ldunifrf.rf19",          // viewport z scale  (context->sz)   // moved off rf0: rf0-rf2 must keep the object position
    "nop ; nop ; ldunifrf.rf20",          // viewport z offset (context->az)
    "nop ; fmul rf13, rf13, rf19",
    "fadd rf13, rf13, rf20 ; nop",

    "stvpmv 0, rf8 ; nop",
    "stvpmv 1, rf9 ; nop",
    "stvpmv 2, rf13 ; nop",
    "stvpmv 3, r4 ; nop",

    /* ---- EYE-SPACE LIGHTING. rf4..rf9 are all dead from here: rf4-rf7
     * held the clip position and rf8/rf9 the screen x/y, both already
     * written to the VPM above. ---- */
    "nop ; nop ; ldunif",                   // mv11
    "nop ; fmul rf4, rf0, r5 ; ldunif",      // mv12
    "nop ; fmul r0, rf1, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",      // mv13
    "nop ; fmul r0, rf2, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",      // mv14
    "nop ; fmul r0, rf3, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",      // mv21
    "nop ; fmul rf5, rf0, r5 ; ldunif",      // mv22
    "nop ; fmul r0, rf1, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",      // mv23
    "nop ; fmul r0, rf2, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",      // mv24
    "nop ; fmul r0, rf3, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",      // mv31
    "nop ; fmul rf6, rf0, r5 ; ldunif",      // mv32
    "nop ; fmul r0, rf1, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",      // mv33
    "nop ; fmul r0, rf2, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",      // mv34
    "nop ; fmul r0, rf3, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",      // it11
    "nop ; fmul rf7, rf14, r5 ; ldunif",      // it12
    "nop ; fmul r0, rf15, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",      // it13
    "nop ; fmul r0, rf16, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",      // it21
    "nop ; fmul rf8, rf14, r5 ; ldunif",      // it22
    "nop ; fmul r0, rf15, r5",
    "fadd rf8, rf8, r0 ; nop ; ldunif",      // it23
    "nop ; fmul r0, rf16, r5",
    "fadd rf8, rf8, r0 ; nop ; ldunif",      // it31
    "nop ; fmul rf9, rf14, r5 ; ldunif",      // it32
    "nop ; fmul r0, rf15, r5",
    "fadd rf9, rf9, r0 ; nop ; ldunif",      // it33
    "nop ; fmul r0, rf16, r5",
    "fadd rf9, rf9, r0 ; nop",
    /* |N_eye| is what object space got wrong: it divided by |n|. */
    "nop ; fmul r0, rf7, rf7",
    "nop ; fmul r1, rf8, rf8",
    "fadd r0, r0, r1 ; fmul r2, rf9, rf9",
    "fadd r0, r0, r2 ; nop",
    "or rsqrt, r0, r0 ; nop",
    "nop ; nop",
    "nop ; fmul rf7, rf7, r4",
    "nop ; fmul rf8, rf8, r4",
    "nop ; fmul rf9, rf9, r4",

    /* ---------------------------------------------------------------- LIGHTING
     * Object space, one light, ambient + diffuse + specular.
     *
     * r4 IS FREE HERE. It carried 1/w from "or recip, rf7, rf7" up to the
     * "stvpmv 3, r4" just above, and nothing reads it again, so both SFU
     * lookups below can use it without disturbing the matrix core. The block
     * sits between the position stores and the colour stores for exactly that
     * reason.
     *
     * THE UNIFORM TAIL IS POSITIONAL -- ldunif carries no address. It is read
     * in this order and the CPU writes it in this order: light position (3),
     * view direction (3), shininess (1), diffuse product (3), specular product (3), base
     * colour (3), shininess (1), diffuse product (3), specular product (3), base
     * colour (3), alpha (1) -- SEVENTEEN words, and seventeen instructions
     * minimum: one signal field per instruction is a hardware floor.
     *
     * The two trailing z_scale/z_offset ldunifrf above MUST stay the last reads
     * before the first read here, or the tail's first word arrives as z_scale.
     *
     * Every operand pair keeps to at most TWO register-file reads, which is the
     * port limit; partial products live in the accumulators r0-r2.
     */

    /* Zero, made by subtracting rf3 from itself -- the idiom the screen-space
     * conversion above already uses. rf3 holds w_m = 1.0 and is dead from the end
     * of the matrix multiply, so 1.0 needs no register of its own. */
    "fsub rf22, rf3, rf3 ; nop",

    /* L = light_position_object - vertex_position_object. The light arrives
     * already carried into object space by the CPU (draw.c), once per draw,
     * which is why no inverse matrix appears in this shader. */
    "nop ; nop ; ldunif",
    "fsub rf23, r5, rf4 ; nop ; ldunif",
    "fsub rf24, r5, rf5 ; nop ; ldunif",
    "fsub rf25, r5, rf6 ; nop",

    /* |L|^2, then r4 = 1/|L|. THE NORMALIZE IS REQUIRED, not an option: the
     * light is positional (w != 0), so GL defines the direction per vertex as
     * normalize(P_light - P_vertex). A constant direction would flatten the
     * specular highlight to one value across a whole surface. */
    "nop ; fmul r0, rf23, rf23",
    "nop ; fmul r1, rf24, rf24",
    "fadd r0, r0, r1 ; fmul r2, rf25, rf25",
    "fadd r0, r0, r2 ; nop",
    /* Magic-waddr SFU, never the ALU-op rsqrt spelling: the validator's
     * sfu_writes counter only inspects magic writes, so the ALU-op form's r4
     * latency is unchecked and a violation would assemble clean and fail on
     * hardware. This form ships in four fragment shaders. */
    "or rsqrt, r0, r0 ; nop",
    /* Exactly ONE instruction between the SFU write and the r4 read; it earns
     * its keep by starting the view-direction reads. */
    /* V is eye-space (0,0,1), a constant now, so H = L + z needs no
     * uniform and the three view-direction words leave the tail. The
     * instruction STAYS as a nop: it also fills the mandatory gap after
     * the rsqrt above, and r4 cannot be read in the next instruction. */
    "nop ; nop",
    "nop ; fmul rf23, rf23, r4",
    "nop ; fmul rf24, rf24, r4",
    "nop ; fmul rf25, rf25, r4",

    /* H = Lhat + Vobj, the half vector. V is the eye-space view direction
     * (0,0,1) carried into object space and normalised by the CPU;
     * GL_LIGHT_MODEL_LOCAL_VIEWER is false, so it is constant for the draw. */
    "or rf26, rf23, rf23 ; nop",
    "or rf27, rf24, rf24 ; nop",
    "fadd rf28, rf25, 0x3f800000 ; nop",

    "nop ; fmul r0, rf26, rf26",
    "nop ; fmul r1, rf27, rf27",
    "fadd r0, r0, r1 ; fmul r2, rf28, rf28",
    "fadd r0, r0, r2 ; nop",
    "or rsqrt, r0, r0 ; nop",
    "nop ; nop ; ldunif",                  // latency slot; r5 = shininess
    "nop ; fmul rf26, rf26, r4",
    "nop ; fmul rf27, rf27, r4",
    "nop ; fmul rf28, rf28, r4",

    /* N . Lhat, clamped at zero. The normal is used UNNORMALISED, which is GL:
     * with normalisation disabled -- and this GL has no GL_NORMALIZE token at
     * all -- the transformed normal is fed to the lighting equation as it is. */
    "nop ; fmul r0, rf7, rf23",
    "nop ; fmul r1, rf8, rf24",
    "fadd r0, r0, r1 ; fmul r2, rf9, rf25",
    "fadd r0, r0, r2 ; nop",
    "fmax rf29, r0, rf22 ; nop",

    /* N . Hhat, clamped at zero. fmax and not a predicated write: the only
     * conditional register writes in this whole tree are a fragment-stage
     * setmsf, and there are zero examples of predication to copy. */
    "nop ; fmul r0, rf7, rf26",
    "nop ; fmul r1, rf8, rf27",
    "fadd r0, r0, r1 ; fmul r2, rf9, rf28",
    "fadd r0, r0, r2 ; nop",
    /* Clamped to 2^-8 and NOT to zero, which the log below requires: log2(0) is
     * -inf, and shininess 0 would then give 0 * -inf = NaN where GL wants 1. At
     * 2^-8 the smallest representable result is 2^-8 raised to the shininess,
     * which is zero in eight bits for any exponent above 3, so nothing visible
     * changes and the NaN cannot arise. */
    "fmax rf30, r0, 0x3b800000 ; nop",

    /* (N.Hhat) ^ shininess = exp2(shininess * log2(N.Hhat)), for ANY shininess in
     * GL's [0,128] rather than the fixed exponent repeated squaring would give.
     * Both SFU forms are the magic-waddr spelling; `exp` ships in 50 fog shaders,
     * `log` is the first use of that waddr in this tree.
     *
     * The result STAYS IN r4 and is used from there by the specular multiplies
     * below -- no SFU lookup follows, so nothing disturbs it, and skipping the
     * move back to a register is what pays for the log and exp. */
    "or log, rf30, rf30 ; nop",
    "nop ; nop",
    "nop ; fmul r0, r4, r5",
    "or exp, r0, r0 ; nop",
    "nop ; nop",

    /* colour = base + diffuse_product * (N.L) + specular_product * (N.H)^10.
     * The products are folded on the CPU (light.c), so no light x material
     * multiply happens per vertex. */
    "nop ; nop ; ldunif",
    "nop ; fmul rf31, r5, rf29 ; ldunif",
    "nop ; fmul rf3, r5, rf29 ; ldunif",
    "nop ; fmul rf4, r5, rf29 ; ldunif",
    "nop ; fmul r0, r5, r4 ; ldunif",
    "fadd rf31, rf31, r0 ; fmul r1, r5, r4 ; ldunif",
    "fadd rf3, rf3, r1 ; fmul r2, r5, r4 ; ldunif",
    "fadd rf4, rf4, r2 ; nop",
    "fadd rf31, rf31, r5 ; nop ; ldunif",
    "fadd rf3, rf3, r5 ; nop ; ldunif",
    "fadd rf4, rf4, r5 ; nop ; ldunif",
    "or rf17, r5, r5 ; nop",

    /* GL clamps the final vertex colour to [0,1]. Only the upper clamp is
     * needed: every term above is non-negative, the two dot products having
     * been clamped at zero already.
     *
     * 1.0 IS REMATERIALISED HERE, and not taken from rf3 the way the untextured
     * twin does it. This variant reuses rf3 as its GREEN accumulator above --
     * legal, w_m being dead by then -- so clamping against rf3 clamped every
     * channel against green: emission 0.8/0.2/0.2 came out 0.2/0.2/0.2. rf30 is
     * dead after the log above. */
    "or rf30, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf14, rf31, rf30 ; nop",
    "fmin rf15, rf3,  rf30 ; nop",
    "fmin rf16, rf4,  rf30 ; nop",

    "stvpmv 4, rf11 ; nop", // s
    "stvpmv 5, rf12 ; nop", // t
    "stvpmv 6, rf14 ; nop", // lit colour r
    "stvpmv 7, rf15 ; nop", // lit colour g
    "stvpmv 8, rf16 ; nop", // lit colour b
    "stvpmv 9, rf17 ; nop", // a = 1.0

    "vpmwt -              ; nop",
    "nop                  ; nop ; thrsw",
    "nop                  ; nop",
    "nop                  ; nop",
};

/*
 * LIT, TEXTURED, REAL PER-VERTEX W, with GL_COLOR_MATERIAL. lit_realw
 * plus the K1 half of light.c's fold: the vertex colour arrives as a fourth
 * attribute at VPM inputs 9..12 and each channel gains
 * C * (K1base + K1diff * N.L + K1spec * spec). With a mode that tracks nothing
 * every K1 is zero and this computes exactly what the host shader does.
 */
static const char* g_vertex_shader_lit_realw_colormaterial_assembly[] = {
    "ldvpmv_in rf3, 3 ; nop",   // the REAL per-vertex w, where lit_textured had 1.0

    "nop ; nop ; ldunifrf.rf10", // scale_p

    /* Separate Y-axis screen-space scale -- rf18, since rf14-17 are
     * already taken by color r/g/b/a in this variant. */
    "nop ; nop ; ldunifrf.rf18", // scale_p_y

    "ldvpmv_in rf0,  0 ; nop", // x_m
    "ldvpmv_in rf1,  1 ; nop", // y_m
    "ldvpmv_in rf2,  2 ; nop", // z_m

    "ldvpmv_in rf11,  4 ; nop", // s
    "ldvpmv_in rf12,  5 ; nop", // t

    /* THE NORMAL, object space, in the registers that hold colour r/g/b,
     * for the same reason as in the untextured variant above: the
     * matrix-multiply section never touches them.
     *
     * VPM input words are 0-2 position + 3-4 s,t + 5-7 normal = EIGHT, one
     * word fewer than the smooth textured shader, the colour record being
     * dropped for a lit draw. Eight is exactly one sector; the field stays
     * at the 2 the combined path already sets, which is harmless. */
    "ldvpmv_in rf14,  6 ; nop", // normal x
    "ldvpmv_in rf15,  7 ; nop", // normal y
    "ldvpmv_in rf16,  8 ; nop", // normal z

    /* Alpha, constant 1.0. */
    "or rf17, 0x3f800000, 0x3f800000 ; nop", // a = 1.0
    /* Matrix multiply -- byte-for-byte identical to g_vertex_shader_assembly. */
    "nop ; nop ; ldunif",
    "nop ; fmul rf4, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",
    "nop ; fmul rf5, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",
    "nop ; fmul rf6, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",
    "nop ; fmul rf7, rf0, r5 ; ldunif",
    "nop ; fmul r0, rf1, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf2, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",
    "nop ; fmul r0, rf3, r5",
    "fadd rf7, rf7, r0 ; nop",

    "or recip, rf7, rf7 ; nop",
    "nop ; nop",
    "nop ; fmul r0, rf4, r4",
    "nop ; fmul r0, r0, rf10",
    "nop ; fmul rf8, r0, 0x43000000",
    "fsub r0, r0, r0 ; nop",
    "fsub r0, r0, rf5 ; nop",
    "nop ; fmul r0, r0, r4",
    "nop ; fmul r0, r0, rf18", // scale_p_y, not the shared scale_p
    "nop ; fmul rf9, r0, 0x43000000",
    "ftoin rf8, rf8 ; nop",
    "ftoin rf9, rf9 ; nop",
    "nop ; fmul rf13, rf6, r4",
    /* The Z scale and offset come from the uniform stream
     * (v3d_my_uniforms.z_scale / .z_offset, draw.c), APPENDED after the 16
     * matrix values -- so these two reads must stay the LAST ldunif* in this
     * shader. rf0/rf1 held x_m/y_m and are dead from the end of the matrix
     * multiply above to the end of the shader, so no new register is needed.
     * Two instructions of slack before first use; the matrix multiply above
     * proves one is enough. */
    "nop ; nop ; ldunifrf.rf19",          // viewport z scale  (context->sz)   // moved off rf0: rf0-rf2 must keep the object position
    "nop ; nop ; ldunifrf.rf20",          // viewport z offset (context->az)
    "nop ; fmul rf13, rf13, rf19",
    "fadd rf13, rf13, rf20 ; nop",

    "stvpmv 0, rf8 ; nop",
    "stvpmv 1, rf9 ; nop",
    "stvpmv 2, rf13 ; nop",
    "stvpmv 3, r4 ; nop",

    /* ---- EYE-SPACE LIGHTING. rf4..rf9 are all dead from here: rf4-rf7
     * held the clip position and rf8/rf9 the screen x/y, both already
     * written to the VPM above. ---- */
    "nop ; nop ; ldunif",                   // mv11
    "nop ; fmul rf4, rf0, r5 ; ldunif",      // mv12
    "nop ; fmul r0, rf1, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",      // mv13
    "nop ; fmul r0, rf2, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",      // mv14
    "nop ; fmul r0, rf3, r5",
    "fadd rf4, rf4, r0 ; nop ; ldunif",      // mv21
    "nop ; fmul rf5, rf0, r5 ; ldunif",      // mv22
    "nop ; fmul r0, rf1, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",      // mv23
    "nop ; fmul r0, rf2, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",      // mv24
    "nop ; fmul r0, rf3, r5",
    "fadd rf5, rf5, r0 ; nop ; ldunif",      // mv31
    "nop ; fmul rf6, rf0, r5 ; ldunif",      // mv32
    "nop ; fmul r0, rf1, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",      // mv33
    "nop ; fmul r0, rf2, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",      // mv34
    "nop ; fmul r0, rf3, r5",
    "fadd rf6, rf6, r0 ; nop ; ldunif",      // it11
    "nop ; fmul rf7, rf14, r5 ; ldunif",      // it12
    "nop ; fmul r0, rf15, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",      // it13
    "nop ; fmul r0, rf16, r5",
    "fadd rf7, rf7, r0 ; nop ; ldunif",      // it21
    "nop ; fmul rf8, rf14, r5 ; ldunif",      // it22
    "nop ; fmul r0, rf15, r5",
    "fadd rf8, rf8, r0 ; nop ; ldunif",      // it23
    "nop ; fmul r0, rf16, r5",
    "fadd rf8, rf8, r0 ; nop ; ldunif",      // it31
    "nop ; fmul rf9, rf14, r5 ; ldunif",      // it32
    "nop ; fmul r0, rf15, r5",
    "fadd rf9, rf9, r0 ; nop ; ldunif",      // it33
    "nop ; fmul r0, rf16, r5",
    "fadd rf9, rf9, r0 ; nop",
    /* |N_eye| is what object space got wrong: it divided by |n|. */
    "nop ; fmul r0, rf7, rf7",
    "nop ; fmul r1, rf8, rf8",
    "fadd r0, r0, r1 ; fmul r2, rf9, rf9",
    "fadd r0, r0, r2 ; nop",
    "or rsqrt, r0, r0 ; nop",
    "nop ; nop",
    "nop ; fmul rf7, rf7, r4",
    "nop ; fmul rf8, rf8, r4",
    "nop ; fmul rf9, rf9, r4",

    /* ---------------------------------------------------------------- LIGHTING
     * Object space, one light, ambient + diffuse + specular.
     *
     * r4 IS FREE HERE. It carried 1/w from "or recip, rf7, rf7" up to the
     * "stvpmv 3, r4" just above, and nothing reads it again, so both SFU
     * lookups below can use it without disturbing the matrix core. The block
     * sits between the position stores and the colour stores for exactly that
     * reason.
     *
     * THE UNIFORM TAIL IS POSITIONAL -- ldunif carries no address. It is read
     * in this order and the CPU writes it in this order: light position (3),
     * view direction (3), shininess (1), diffuse product (3), specular product (3), base
     * colour (3), shininess (1), diffuse product (3), specular product (3), base
     * colour (3), alpha (1) -- SEVENTEEN words, and seventeen instructions
     * minimum: one signal field per instruction is a hardware floor.
     *
     * The two trailing z_scale/z_offset ldunifrf above MUST stay the last reads
     * before the first read here, or the tail's first word arrives as z_scale.
     *
     * Every operand pair keeps to at most TWO register-file reads, which is the
     * port limit; partial products live in the accumulators r0-r2.
     */

    /* Zero, made by subtracting rf3 from itself -- the idiom the screen-space
     * conversion above already uses. rf3 holds w_m = 1.0 and is dead from the end
     * of the matrix multiply, so 1.0 needs no register of its own. */
    "fsub rf22, rf3, rf3 ; nop",

    /* L = light_position_object - vertex_position_object. The light arrives
     * already carried into object space by the CPU (draw.c), once per draw,
     * which is why no inverse matrix appears in this shader. */
    "nop ; nop ; ldunif",
    "fsub rf23, r5, rf4 ; nop ; ldunif",
    "fsub rf24, r5, rf5 ; nop ; ldunif",
    "fsub rf25, r5, rf6 ; nop",

    /* |L|^2, then r4 = 1/|L|. THE NORMALIZE IS REQUIRED, not an option: the
     * light is positional (w != 0), so GL defines the direction per vertex as
     * normalize(P_light - P_vertex). A constant direction would flatten the
     * specular highlight to one value across a whole surface. */
    "nop ; fmul r0, rf23, rf23",
    "nop ; fmul r1, rf24, rf24",
    "fadd r0, r0, r1 ; fmul r2, rf25, rf25",
    "fadd r0, r0, r2 ; nop",
    /* Magic-waddr SFU, never the ALU-op rsqrt spelling: the validator's
     * sfu_writes counter only inspects magic writes, so the ALU-op form's r4
     * latency is unchecked and a violation would assemble clean and fail on
     * hardware. This form ships in four fragment shaders. */
    "or rsqrt, r0, r0 ; nop",
    /* Exactly ONE instruction between the SFU write and the r4 read; it earns
     * its keep by starting the view-direction reads. */
    /* V is eye-space (0,0,1), a constant now, so H = L + z needs no
     * uniform and the three view-direction words leave the tail. The
     * instruction STAYS as a nop: it also fills the mandatory gap after
     * the rsqrt above, and r4 cannot be read in the next instruction. */
    "nop ; nop",
    "nop ; fmul rf23, rf23, r4",
    "nop ; fmul rf24, rf24, r4",
    "nop ; fmul rf25, rf25, r4",

    /* H = Lhat + Vobj, the half vector. V is the eye-space view direction
     * (0,0,1) carried into object space and normalised by the CPU;
     * GL_LIGHT_MODEL_LOCAL_VIEWER is false, so it is constant for the draw. */
    "or rf26, rf23, rf23 ; nop",
    "or rf27, rf24, rf24 ; nop",
    "fadd rf28, rf25, 0x3f800000 ; nop",

    "nop ; fmul r0, rf26, rf26",
    "nop ; fmul r1, rf27, rf27",
    "fadd r0, r0, r1 ; fmul r2, rf28, rf28",
    "fadd r0, r0, r2 ; nop",
    "or rsqrt, r0, r0 ; nop",
    "nop ; nop ; ldunif",                  // latency slot; r5 = shininess
    "nop ; fmul rf26, rf26, r4",
    "nop ; fmul rf27, rf27, r4",
    "nop ; fmul rf28, rf28, r4",

    /* N . Lhat, clamped at zero. The normal is used UNNORMALISED, which is GL:
     * with normalisation disabled -- and this GL has no GL_NORMALIZE token at
     * all -- the transformed normal is fed to the lighting equation as it is. */
    "nop ; fmul r0, rf7, rf23",
    "nop ; fmul r1, rf8, rf24",
    "fadd r0, r0, r1 ; fmul r2, rf9, rf25",
    "fadd r0, r0, r2 ; nop",
    "fmax rf29, r0, rf22 ; nop",

    /* N . Hhat, clamped at zero. fmax and not a predicated write: the only
     * conditional register writes in this whole tree are a fragment-stage
     * setmsf, and there are zero examples of predication to copy. */
    "nop ; fmul r0, rf7, rf26",
    "nop ; fmul r1, rf8, rf27",
    "fadd r0, r0, r1 ; fmul r2, rf9, rf28",
    "fadd r0, r0, r2 ; nop",
    /* Clamped to 2^-8 and NOT to zero, which the log below requires: log2(0) is
     * -inf, and shininess 0 would then give 0 * -inf = NaN where GL wants 1. At
     * 2^-8 the smallest representable result is 2^-8 raised to the shininess,
     * which is zero in eight bits for any exponent above 3, so nothing visible
     * changes and the NaN cannot arise. */
    "fmax rf30, r0, 0x3b800000 ; nop",

    /* (N.Hhat) ^ shininess = exp2(shininess * log2(N.Hhat)), for ANY shininess in
     * GL's [0,128] rather than the fixed exponent repeated squaring would give.
     * Both SFU forms are the magic-waddr spelling; `exp` ships in 50 fog shaders,
     * `log` is the first use of that waddr in this tree.
     *
     * The result STAYS IN r4 and is used from there by the specular multiplies
     * below -- no SFU lookup follows, so nothing disturbs it, and skipping the
     * move back to a register is what pays for the log and exp. */
    "or log, rf30, rf30 ; nop",
    "nop ; nop",
    "nop ; fmul r0, r4, r5",
    "or exp, r0, r0 ; nop",
    "nop ; nop",

    /* colour = base + diffuse_product * (N.L) + specular_product * (N.H)^10.
     * The products are folded on the CPU (light.c), so no light x material
     * multiply happens per vertex. */
    "nop ; nop ; ldunif",
    "nop ; fmul rf31, r5, rf29 ; ldunif",
    "nop ; fmul rf3, r5, rf29 ; ldunif",
    "nop ; fmul rf4, r5, rf29 ; ldunif",
    "nop ; fmul r0, r5, r4 ; ldunif",
    "fadd rf31, rf31, r0 ; fmul r1, r5, r4 ; ldunif",
    "fadd rf3, rf3, r1 ; fmul r2, r5, r4 ; ldunif",
    "fadd rf4, rf4, r2 ; nop",
    "fadd rf31, rf31, r5 ; nop ; ldunif",
    "fadd rf3, rf3, r5 ; nop ; ldunif",
    "fadd rf4, rf4, r5 ; nop ; ldunif",
    "or rf17, r5, r5 ; nop",
    /* ---- GL_COLOR_MATERIAL: the K1 half of the folded terms ---- */
    "ldvpmv_in rf5, 9 ; nop",
    "ldvpmv_in rf6, 10 ; nop",
    "ldvpmv_in rf7, 11 ; nop",
    "ldvpmv_in rf8, 12 ; nop",
    /* k per channel: base + diffuse*N.L + specular*spec. The tail is
     * grouped by channel, so each group is three consecutive words. */
    "nop ; nop ; ldunif",
    "or rf0, r5, r5 ; nop ; ldunif",
    "nop ; fmul r0, r5, rf29 ; ldunif",
    "nop ; fmul r1, r5, r4 ; ldunif",
    "fadd r0, r0, r1 ; nop",
    "fadd rf0, rf0, r0 ; nop",
    "or rf1, r5, r5 ; nop ; ldunif",
    "nop ; fmul r0, r5, rf29 ; ldunif",
    "nop ; fmul r1, r5, r4 ; ldunif",
    "fadd r0, r0, r1 ; nop",
    "fadd rf1, rf1, r0 ; nop",
    "or rf2, r5, r5 ; nop ; ldunif",
    "nop ; fmul r0, r5, rf29 ; ldunif",
    "nop ; fmul r1, r5, r4 ; ldunif",
    "fadd r0, r0, r1 ; nop",
    "fadd rf2, rf2, r0 ; nop",
    /* accumulate C * k, and the alpha scale still sitting in r5 */
    "nop ; fmul r0, rf0, rf5",
    "nop ; fmul r1, rf1, rf6",
    "nop ; fmul r2, rf2, rf7",
    "fadd rf31, rf31, r0 ; nop",
    "fadd rf3, rf3, r1 ; fmul r0, r5, rf8",
    "fadd rf4, rf4, r2 ; nop",
    "fadd rf17, rf17, r0 ; nop",

    /* GL clamps the final vertex colour to [0,1]. Only the upper clamp is
     * needed: every term above is non-negative, the two dot products having
     * been clamped at zero already.
     *
     * 1.0 IS REMATERIALISED HERE, and not taken from rf3 the way the untextured
     * twin does it. This variant reuses rf3 as its GREEN accumulator above --
     * legal, w_m being dead by then -- so clamping against rf3 clamped every
     * channel against green: emission 0.8/0.2/0.2 came out 0.2/0.2/0.2. rf30 is
     * dead after the log above. */
    "or rf30, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf14, rf31, rf30 ; nop",
    "fmin rf15, rf3,  rf30 ; nop",
    "fmin rf16, rf4,  rf30 ; nop",

    "stvpmv 4, rf11 ; nop", // s
    "stvpmv 5, rf12 ; nop", // t
    "stvpmv 6, rf14 ; nop", // lit colour r
    "stvpmv 7, rf15 ; nop", // lit colour g
    "stvpmv 8, rf16 ; nop", // lit colour b
    "stvpmv 9, rf17 ; nop", // a = 1.0

    "vpmwt -              ; nop",
    "nop                  ; nop ; thrsw",
    "nop                  ; nop",
    "nop                  ; nop",
};


/*
 * textured_smooth_dstcolor_zero + fog.
 *
 * The fog lerp is placed BEFORE the first ldtlb, not before the colour
 * vfpacks as in the non-blending fog variants. That is the whole point:
 * GL applies fog to the FRAGMENT, and blending happens afterwards against
 * the destination, so fogging the already-blended result would fog the
 * destination's contribution too. At this point rf7/rf8/rf9 hold the final
 * source colour and the destination has not been read yet.
 *
 * This shader consumes NO uniform words after the insertion point, so the
 * fog loads stay in stream order, and its blend math does not touch
 * the fog block's rf11/rf12/rf13, so the two cannot collide.
 */
/*
 * textured_smooth_dstcolor_one + fog.
 *
 * The fog lerp is placed BEFORE the first ldtlb, not before the colour
 * vfpacks as in the non-blending fog variants. That is the whole point:
 * GL applies fog to the FRAGMENT, and blending happens afterwards against
 * the destination, so fogging the already-blended result would fog the
 * destination's contribution too. At this point rf7/rf8/rf9 hold the final
 * source colour and the destination has not been read yet.
 *
 * This shader consumes NO uniform words after the insertion point, so the
 * fog loads stay in stream order, and its blend math does not touch
 * the fog block's rf11/rf12/rf13, so the two cannot collide.
 */
/*
 * textured_smooth_dstcolor_srccolor + fog.
 *
 * The fog lerp is placed BEFORE the first ldtlb, not before the colour
 * vfpacks as in the non-blending fog variants. That is the whole point:
 * GL applies fog to the FRAGMENT, and blending happens afterwards against
 * the destination, so fogging the already-blended result would fog the
 * destination's contribution too. At this point rf7/rf8/rf9 hold the final
 * source colour and the destination has not been read yet.
 *
 * This shader consumes NO uniform words after the insertion point, so the
 * fog loads stay in stream order, and its blend math does not touch
 * the fog block's rf11/rf12/rf13, so the two cannot collide.
 */
/*
 * textured_smooth_dstcolor_invdstalpha + fog.
 *
 * The fog lerp is placed BEFORE the first ldtlb, not before the colour
 * vfpacks as in the non-blending fog variants. That is the whole point:
 * GL applies fog to the FRAGMENT, and blending happens afterwards against
 * the destination, so fogging the already-blended result would fog the
 * destination's contribution too. At this point rf7/rf8/rf9 hold the final
 * source colour and the destination has not been read yet.
 *
 * This shader consumes NO uniform words after the insertion point, so the
 * fog loads stay in stream order, and its blend math does not touch
 * the fog block's rf11/rf12/rf13, so the two cannot collide.
 */
/*
 * textured_smooth_zero_invsrccolor + fog.
 *
 * The fog lerp is placed BEFORE the first ldtlb, not before the colour
 * vfpacks as in the non-blending fog variants. That is the whole point:
 * GL applies fog to the FRAGMENT, and blending happens afterwards against
 * the destination, so fogging the already-blended result would fog the
 * destination's contribution too. At this point rf7/rf8/rf9 hold the final
 * source colour and the destination has not been read yet.
 *
 * This shader consumes NO uniform words after the insertion point, so the
 * fog loads stay in stream order, and its blend math does not touch
 * the fog block's rf11/rf12/rf13, so the two cannot collide.
 */
/*
 * textured_smooth_dstcolor_srcalpha + fog.
 *
 * The fog lerp is placed BEFORE the first ldtlb, not before the colour
 * vfpacks as in the non-blending fog variants. That is the whole point:
 * GL applies fog to the FRAGMENT, and blending happens afterwards against
 * the destination, so fogging the already-blended result would fog the
 * destination's contribution too. At this point rf7/rf8/rf9 hold the final
 * source colour and the destination has not been read yet.
 *
 * This shader consumes NO uniform words after the insertion point, so the
 * fog loads stay in stream order, and its blend math does not touch
 * the fog block's rf11/rf12/rf13, so the two cannot collide.
 */
/*
 * textured_smooth_one_invsrcalpha + fog.
 *
 * The fog lerp is placed BEFORE the first ldtlb, not before the colour
 * vfpacks as in the non-blending fog variants. That is the whole point:
 * GL applies fog to the FRAGMENT, and blending happens afterwards against
 * the destination, so fogging the already-blended result would fog the
 * destination's contribution too. At this point rf7/rf8/rf9 hold the final
 * source colour and the destination has not been read yet.
 *
 * This shader consumes NO uniform words after the insertion point, so the
 * fog loads stay in stream order, and its blend math does not touch
 * the fog block's rf11/rf12/rf13, so the two cannot collide.
 */
/*
 * textured_smooth_invsrcalpha_srcalpha + fog.
 *
 * The fog lerp is placed BEFORE the first ldtlb, not before the colour
 * vfpacks as in the non-blending fog variants. That is the whole point:
 * GL applies fog to the FRAGMENT, and blending happens afterwards against
 * the destination, so fogging the already-blended result would fog the
 * destination's contribution too. At this point rf7/rf8/rf9 hold the final
 * source colour and the destination has not been read yet.
 *
 * This shader consumes NO uniform words after the insertion point, so the
 * fog loads stay in stream order, and its blend math does not touch
 * the fog block's rf11/rf12/rf13, so the two cannot collide.
 */
/*
 * multitexture_modulate_translucent + fog.
 *
 * The fog lerp is placed BEFORE the first ldtlb, not before the colour
 * vfpacks as in the non-blending fog variants. That is the whole point:
 * GL applies fog to the FRAGMENT, and blending happens afterwards against
 * the destination, so fogging the already-blended result would fog the
 * destination's contribution too. At this point rf7/rf8/rf9 hold the final
 * source colour and the destination has not been read yet.
 *
 * This shader consumes NO uniform words after the insertion point, so the
 * fog loads stay in stream order, and its blend math does not touch
 * the fog block's rf11/rf12/rf13, so the two cannot collide.
 */
/*
 * multitexture_modulate_blend + fog.
 *
 * The fog lerp is placed BEFORE the first ldtlb, not before the colour
 * vfpacks as in the non-blending fog variants. That is the whole point:
 * GL applies fog to the FRAGMENT, and blending happens afterwards against
 * the destination, so fogging the already-blended result would fog the
 * destination's contribution too. At this point rf7/rf8/rf9 hold the final
 * source colour and the destination has not been read yet.
 *
 * This shader consumes NO uniform words after the insertion point, so the
 * fog loads stay in stream order, and its blend math does not touch
 * the fog block's rf11/rf12/rf13, so the two cannot collide.
 */
/*
 * multitexture_decal_blend + fog.
 *
 * The fog lerp is placed BEFORE the first ldtlb, not before the colour
 * vfpacks as in the non-blending fog variants. That is the whole point:
 * GL applies fog to the FRAGMENT, and blending happens afterwards against
 * the destination, so fogging the already-blended result would fog the
 * destination's contribution too. At this point rf7/rf8/rf9 hold the final
 * source colour and the destination has not been read yet.
 *
 * This shader consumes NO uniform words after the insertion point, so the
 * fog loads stay in stream order, and its blend math does not touch
 * the fog block's rf11/rf12/rf13, so the two cannot collide.
 */
/*
 * multitexture_replace_blend + fog.
 *
 * The fog lerp is placed BEFORE the first ldtlb, not before the colour
 * vfpacks as in the non-blending fog variants. That is the whole point:
 * GL applies fog to the FRAGMENT, and blending happens afterwards against
 * the destination, so fogging the already-blended result would fog the
 * destination's contribution too. At this point rf7/rf8/rf9 hold the final
 * source colour and the destination has not been read yet.
 *
 * This shader consumes NO uniform words after the insertion point, so the
 * fog loads stay in stream order, and its blend math does not touch
 * the fog block's rf11/rf12/rf13, so the two cannot collide.
 */
/* ==================================================================
 * SOFTWARE-BLEND FOG, REGISTER-CONSTRAINED GROUP
 *
 * These five blend shaders use the shared fog block's rf11/rf12/rf13 for
 * their own blend math, so that block cannot be dropped in unchanged.
 * Each gets a fog block on registers allocated from its own unused set
 * instead.
 * ================================================================== */

/* ==================================================================
 * UNTEXTURED FLAT BLEND FOG
 * ================================================================== */

/*
 * untextured_blend + fog.
 *
 * Untextured and FLAT: the source colour arrives as four ldunifrf loads at
 * the very top, so it is final before the ldtlb and the fog block goes
 * straight after it.
 *
 * Uniform words: the 4 source-colour words, then the fog words, matching
 * what draw.c writes for a fogged untextured draw.
 */
/*
 * untextured_blend_srcalpha_one + fog.
 *
 * Untextured and FLAT: the source colour arrives as four ldunifrf loads at
 * the very top, so it is final before the ldtlb and the fog block goes
 * straight after it.
 *
 * Uniform words: the 4 source-colour words, then the fog words, matching
 * what draw.c writes for a fogged untextured draw.
 */
/* ==================================================================
 * SMOOTH POINTS: GL_POINT_SMOOTH
 *
 * These four shaders draw a point as a disc: GL 1.1 antialiasing in RGBA
 * mode multiplies the fragment's alpha by its coverage, and a pixel the
 * point does not cover produces no fragment. Coverage follows MESA's
 * nir_lower_point_smooth (clamp(radius - distance, 0, 1), distance in
 * pixels, discard at 0), with the rasterized point size arriving as a
 * uniform instead of being derived from dFdx.
 *
 * One shader per base shape a point can take: untextured or textured, flat
 * or smooth colour. A point that also needs fog or alpha test keeps the
 * square shader (draw.c selects these only without both).
 *
 * THE POINT COORDINATE. V3D supplies gl_PointCoord as two IMPLICIT
 * varyings, read before every other varying, for a points primitive whose
 * shader record clears disable_implicit_point_line_varyings. MESA reads them
 * first (nir_to_vir.c: point_x/point_y are emitted before
 * ntq_setup_fs_inputs) and does not count them in
 * number_of_varyings_in_fragment_shader. draw.c clears the flag for these
 * four shaders only -- on any other shader the extra two varyings would
 * shift every read.
 *
 * Each shader is its base shader with three insertions:
 *   1. the two implicit reads and the centre distance, at the very start
 *      (registers rf11/rf12, which none of the four bases touch; the
 *      distance ends in rf11, which a thread switch preserves);
 *   2. the coverage block after the base's last uniform read, reading the
 *      point size draw.c appends to the uniform stream, discarding at zero
 *      coverage and scaling alpha;
 *   3. the alphatest variants' passthrough Z write before the colour
 *      packs, so a discarded corner leaves depth alone. It consumes the
 *      0xffffff84 TLB config word draw.c appends after the point size, and
 *      draw.c sets fragment_shader_does_z_writes for these shaders.
 * ================================================================== */

/* Untextured, flat colour (base: g_fragment_shader_untextured_assembly).
 * Uniform words: 4 colour, point size, TLB config. */
static const char* g_fragment_shader_untextured_point_smooth_assembly[] = {
    "nop ; nop ; ldvary.r0",                 // point s/w (implicit, first varying)
    "nop ; fmul r1, r0, rf0",
    "fadd rf11, r1, r5 ; nop",               // rf11 = s
    "nop ; nop ; ldvary.r0",                 // point t/w (implicit)
    "nop ; fmul r1, r0, rf0",
    "fadd rf12, r1, r5 ; nop",               // rf12 = t
    "fsub rf11, rf11, 0x3f000000 ; nop",     // s - 0.5
    "fsub rf12, rf12, 0x3f000000 ; nop",     // t - 0.5
    "nop ; fmul rf11, rf11, rf11",
    "nop ; fmul rf12, rf12, rf12",
    "fadd rf11, rf11, rf12 ; nop",           // d^2, point-coordinate units
    "or rf12, 0x3b800000, 0x3b800000 ; nop", // 2^-8
    "nop ; fmul rf12, rf12, 0x3b800000",     // 2^-16: keeps rsqrt finite at the exact centre
    "fadd rf11, rf11, rf12 ; nop",
    "or rsqrt, rf11, rf11 ; nop",            // SFU: r4 = 1/sqrt(d^2)
    "nop ; nop",                             // SFU latency
    "nop ; fmul rf11, rf11, r4 ; ldunifrf.rf7",             // rf11 = d

    "nop ; nop ; ldunifrf.rf8",  // rf8  = green (uniform 1)
    "nop ; nop ; ldunifrf.rf9",  // rf9  = blue  (uniform 2)
    "nop ; nop ; ldunifrf.rf10", // rf10 = alpha (uniform 3)

    "nop ; nop ; ldunifrf.rf12",             // rf12 = rasterized point size (uniform 4)
    "or rf13, 0x3f000000, 0x3f000000 ; nop", // 0.5
    "fsub rf13, rf13, rf11 ; nop",           // 0.5 - d
    "nop ; fmul rf13, rf13, rf12",           // pixels from this fragment to the rim
    "sub rf12, rf12, rf12 ; nop",            // 0.0
    "fmax rf13, rf13, rf12 ; nop",
    "or rf14, 0x3f800000, 0x3f800000 ; nop", // 1.0
    "fmin rf13, rf13, rf14 ; nop",           // rf13 = coverage
    "fcmp.pushc -, rf13, rf12 ; nop",        // IFA true means 0 >= coverage
    "setmsf.ifa -, 0 ; nop",                 // not covered: no fragment
    "nop ; fmul rf10, rf10, rf13",           // alpha *= coverage

    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- the >=3-instruction gap before the next thrsw
    "or tlbu, rf10, rf10 ; nop", // passthrough Z write; config word from the uniform stream (uniform 5)
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/* Untextured, smooth colour (base: g_fragment_shader_untextured_smooth_assembly).
 * Uniform words: 4 unused colour (draw.c writes them for every untextured
 * draw -- see g_fragment_shader_untextured_smooth_fog_assembly), point size,
 * TLB config. */
static const char* g_fragment_shader_untextured_smooth_point_smooth_assembly[] = {
    "nop ; nop ; ldvary.r0",                 // point s/w (implicit, first varying)
    "nop ; fmul r1, r0, rf0",
    "fadd rf11, r1, r5 ; nop",               // rf11 = s
    "nop ; nop ; ldvary.r0",                 // point t/w (implicit)
    "nop ; fmul r1, r0, rf0",
    "fadd rf12, r1, r5 ; nop",               // rf12 = t
    "fsub rf11, rf11, 0x3f000000 ; nop",     // s - 0.5
    "fsub rf12, rf12, 0x3f000000 ; nop",     // t - 0.5
    "nop ; fmul rf11, rf11, rf11",
    "nop ; fmul rf12, rf12, rf12",
    "fadd rf11, rf11, rf12 ; nop",           // d^2, point-coordinate units
    "or rf12, 0x3b800000, 0x3b800000 ; nop", // 2^-8
    "nop ; fmul rf12, rf12, 0x3b800000",     // 2^-16: keeps rsqrt finite at the exact centre
    "fadd rf11, rf11, rf12 ; nop",
    "or rsqrt, rf11, rf11 ; nop",            // SFU: r4 = 1/sqrt(d^2)
    "nop ; nop",                             // SFU latency
    "nop ; fmul rf11, rf11, r4",             // rf11 = d

    "nop ; nop ; ldvary.r0",   // load r/w
    "nop ; fmul r1, r0, rf0",  // r1 = (r/w) * w
    "fadd rf7, r1, r5 ; nop",  // rf7 = true red
    "nop ; nop ; ldvary.r0",   // load g/w
    "nop ; fmul r1, r0, rf0",  // r1 = (g/w) * w
    "fadd rf8, r1, r5 ; nop",  // rf8 = true green
    "nop ; nop ; ldvary.r0",   // load b/w
    "nop ; fmul r1, r0, rf0",  // r1 = (b/w) * w
    "fadd rf9, r1, r5 ; nop",  // rf9 = true blue
    "nop ; nop ; ldvary.r0",   // load a/w
    "nop ; fmul r1, r0, rf0",  // r1 = (a/w) * w
    "fadd rf10, r1, r5 ; nop ; ldunifrf.rf24", // rf10 = true alpha

    "nop ; nop ; ldunifrf.rf24", // consume col[1], unused
    "nop ; nop ; ldunifrf.rf24", // consume col[2], unused
    "nop ; nop ; ldunifrf.rf24", // consume col[3], unused

    "nop ; nop ; ldunifrf.rf12",             // rf12 = rasterized point size (uniform 4)
    "or rf13, 0x3f000000, 0x3f000000 ; nop", // 0.5
    "fsub rf13, rf13, rf11 ; nop",           // 0.5 - d
    "nop ; fmul rf13, rf13, rf12",           // pixels from this fragment to the rim
    "sub rf12, rf12, rf12 ; nop",            // 0.0
    "fmax rf13, rf13, rf12 ; nop",
    "or rf14, 0x3f800000, 0x3f800000 ; nop", // 1.0
    "fmin rf13, rf13, rf14 ; nop",           // rf13 = coverage
    "fcmp.pushc -, rf13, rf12 ; nop",        // IFA true means 0 >= coverage
    "setmsf.ifa -, 0 ; nop",                 // not covered: no fragment
    "nop ; fmul rf10, rf10, rf13",           // alpha *= coverage

    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- the >=3-instruction gap before the next thrsw
    "or tlbu, rf10, rf10 ; nop", // passthrough Z write; config word from the uniform stream (uniform 5)
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/* Textured, flat colour (base: g_fragment_shader_textured_colormod_assembly).
 * Uniform words: 2 TMU config (wrtmuc), 4 colour, point size, TLB config.
 * The TMU fetch's own consecutive thrsw pair serves as the last-thread
 * signal, as in the base. */
static const char* g_fragment_shader_textured_point_smooth_assembly[] = {
    "nop ; nop ; ldvary.r0",                 // point s/w (implicit, first varying)
    "nop ; fmul r1, r0, rf0",
    "fadd rf11, r1, r5 ; nop",               // rf11 = s
    "nop ; nop ; ldvary.r0",                 // point t/w (implicit)
    "nop ; fmul r1, r0, rf0",
    "fadd rf12, r1, r5 ; nop",               // rf12 = t
    "fsub rf11, rf11, 0x3f000000 ; nop",     // s - 0.5
    "fsub rf12, rf12, 0x3f000000 ; nop",     // t - 0.5
    "nop ; fmul rf11, rf11, rf11",
    "nop ; fmul rf12, rf12, rf12",
    "fadd rf11, rf11, rf12 ; nop",           // d^2, point-coordinate units
    "or rf12, 0x3b800000, 0x3b800000 ; nop", // 2^-8
    "nop ; fmul rf12, rf12, 0x3b800000",     // 2^-16: keeps rsqrt finite at the exact centre
    "fadd rf11, rf11, rf12 ; nop",
    "or rsqrt, rf11, rf11 ; nop",            // SFU: r4 = 1/sqrt(d^2)
    "nop ; nop",                             // SFU latency
    "nop ; fmul rf11, rf11, r4",             // rf11 = d

    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",
    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",

    "nop ; nop ; ldtmu.rf4",
    "nop ; nop ; ldtmu.rf3",


    "nop ; nop ; ldunifrf.rf5", // rf5 = colour RED multiplier (uniform 2)
    "nop ; fmul rf7, rf4.l, rf5 ; ldunifrf.rf5", // rf7 = texel.r * colour.r
    "nop ; fmul rf8, rf4.h, rf5 ; ldunifrf.rf5", // rf8 = texel.g * colour.g
    "nop ; fmul rf9, rf3.l, rf5 ; ldunifrf.rf24", // rf9 = texel.b * colour.b
    "nop ; fmul rf10, rf3.h, rf24 ; ldunifrf.rf12", // rf10 = tex_alpha * color_alpha

    "or rf13, 0x3f000000, 0x3f000000 ; nop", // 0.5
    "fsub rf13, rf13, rf11 ; nop",           // 0.5 - d
    "nop ; fmul rf13, rf13, rf12",           // pixels from this fragment to the rim
    "sub rf12, rf12, rf12 ; nop",            // 0.0
    "fmax rf13, rf13, rf12 ; nop",
    "or rf14, 0x3f800000, 0x3f800000 ; nop", // 1.0
    "fmin rf13, rf13, rf14 ; nop",           // rf13 = coverage
    "fcmp.pushc -, rf13, rf12 ; nop",        // IFA true means 0 >= coverage
    "setmsf.ifa -, 0 ; nop",                 // not covered: no fragment
    "nop ; fmul rf10, rf10, rf13",           // alpha *= coverage

    "or tlbu, rf10, rf10 ; nop", // passthrough Z write; config word from the uniform stream (uniform 7)
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/* Textured, smooth colour (base: g_fragment_shader_textured_smooth_assembly,
 * the `combined` GL_MODULATE shader). Uniform words: 2 TMU config (wrtmuc),
 * point size, TLB config. */
static const char* g_fragment_shader_textured_smooth_point_smooth_assembly[] = {
    "nop ; nop ; ldvary.r0",                 // point s/w (implicit, first varying)
    "nop ; fmul r1, r0, rf0",
    "fadd rf11, r1, r5 ; nop",               // rf11 = s
    "nop ; nop ; ldvary.r0",                 // point t/w (implicit)
    "nop ; fmul r1, r0, rf0",
    "fadd rf12, r1, r5 ; nop",               // rf12 = t
    "fsub rf11, rf11, 0x3f000000 ; nop",     // s - 0.5
    "fsub rf12, rf12, 0x3f000000 ; nop",     // t - 0.5
    "nop ; fmul rf11, rf11, rf11",
    "nop ; fmul rf12, rf12, rf12",
    "fadd rf11, rf11, rf12 ; nop",           // d^2, point-coordinate units
    "or rf12, 0x3b800000, 0x3b800000 ; nop", // 2^-8
    "nop ; fmul rf12, rf12, 0x3b800000",     // 2^-16: keeps rsqrt finite at the exact centre
    "fadd rf11, rf11, rf12 ; nop",
    "or rsqrt, rf11, rf11 ; nop",            // SFU: r4 = 1/sqrt(d^2)
    "nop ; nop",                             // SFU latency
    "nop ; fmul rf11, rf11, r4",             // rf11 = d

    "nop ; nop ; ldvary.r0 ; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; wrtmuc",
    "fadd rf6, r1, r5 ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf5, r1, r5 ; nop",

    "nop ; nop",
    "or tmut, rf5, rf5 ; nop ; thrsw",
    "nop ; nop ; thrsw",
    "or tmus, rf6, rf6 ; nop",
    "nop ; nop ; ldtmu.rf4", // texel channel pair 0,1 (.l,.h)
    "nop ; nop ; ldtmu.rf3", // texel channel pair 2,3 (.l,.h)

    "nop ; nop ; ldvary.r0",    // load r/w
    "nop ; fmul r1, r0, rf0",   // r1 = r/w * w
    "fadd rf20, r1, r5 ; nop",  // rf20 = true vertex red
    "nop ; nop ; ldvary.r0",    // load g/w
    "nop ; fmul r1, r0, rf0",   // r1 = g/w * w
    "fadd rf21, r1, r5 ; nop",  // rf21 = true vertex green
    "nop ; nop ; ldvary.r0",    // load b/w
    "nop ; fmul r1, r0, rf0",   // r1 = b/w * w
    "fadd rf22, r1, r5 ; fmul rf8, rf4.h, rf21",  // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; fmul rf7, rf4.l, rf20",  // rf23 = true vertex alpha

    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23 ; ldunifrf.rf12", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "or rf13, 0x3f000000, 0x3f000000 ; nop", // 0.5
    "fsub rf13, rf13, rf11 ; nop",           // 0.5 - d
    "nop ; fmul rf13, rf13, rf12",           // pixels from this fragment to the rim
    "sub rf12, rf12, rf12 ; nop",            // 0.0
    "fmax rf13, rf13, rf12 ; nop",
    "or rf14, 0x3f800000, 0x3f800000 ; nop", // 1.0
    "fmin rf13, rf13, rf14 ; nop",           // rf13 = coverage
    "fcmp.pushc -, rf13, rf12 ; nop",        // IFA true means 0 >= coverage
    "setmsf.ifa -, 0 ; nop",                 // not covered: no fragment
    "nop ; fmul rf10, rf10, rf13",           // alpha *= coverage

    "or tlbu, rf10, rf10 ; nop", // passthrough Z write; config word from the uniform stream (uniform 3)
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * MiniGLV3D orchestrator, replacing the earlier library's v3d_assemble():
 * same per-instruction assemble/pack loop then whole-sequence validate,
 * adapted to write into a caller-provided V3DAssembledShader* instead of
 * static file-scope buffers.
 */
#include "../include/v3d_shader_assembler.h"

/*
 * static, not a stack-local -- struct v3d_qpu_instr is 88 bytes, so a
 * V3D_SHADER_MAX_INSTRUCTIONS-entry stack array here would be 11264 bytes,
 * plus another ~126 bytes for the
 * per-iteration struct v3d_qpu_assemble_arguments below (static for the
 * same reason). That much stack overflows and corrupts memory, which is
 * why the earlier library's own v3d_assemble() used static file-scope buffers
 * too.
 *
 * Making them static is safe here because this is purely internal scratch
 * space for one function call, never observed outside it, with no
 * re-entrancy concern: v3d_assemble_one_shader is only ever called
 * sequentially -- fragment, then vertex, then coordinate -- never nested
 * or concurrent. The "object, not global" rule this deviates from is about
 * STATE other code observes across calls (V3DTexture, V3DContext).
 */
static struct v3d_qpu_instr unpackedInstructions[V3D_SHADER_MAX_INSTRUCTIONS];
static struct v3d_qpu_assemble_arguments assembleArguments;

/*
 * WHAT FAILED, and the entry point that assembles one shader for a caller --
 * BOTH EXIST ONLY FOR shader_assembler_test, so the driver does not carry them.
 *
 * The test links the no-logging archive, where D() is gone, so a failure there
 * has nothing to name; and it assembles the textured fragment shader itself,
 * that shader not being one the driver carries. In the library the recording
 * alone would cost 2.4KB of text -- v3d_assemble_shader is exported, so it is
 * emitted in full and inlines the whole assemble path.
 *
 * The demo build script's --assembler-report option compiles THIS FILE again with
 * MGLV3D_ASSEMBLE_REPORT and links that object ahead of the archive, so the test
 * gets both and the library gets neither.
 */
#ifdef MGLV3D_ASSEMBLE_REPORT

const char* v3d_assemble_error_shader;
const char* v3d_assemble_error_what;
const char* v3d_assemble_error_text;
int         v3d_assemble_error_index = -1;

int         v3d_assemble_report_all;
int         v3d_assemble_failures;
const char* v3d_assemble_failed_name[V3D_ASSEMBLE_MAX_FAILURES];
const char* v3d_assemble_failed_what[V3D_ASSEMBLE_MAX_FAILURES];
const char* v3d_assemble_failed_text[V3D_ASSEMBLE_MAX_FAILURES];
int         v3d_assemble_failed_index[V3D_ASSEMBLE_MAX_FAILURES];

/* Returns TRUE to carry on to the next shader, FALSE to stop here. */
static int v3d_assemble_note_failure(void)
{
    if (v3d_assemble_failures < V3D_ASSEMBLE_MAX_FAILURES)
    {
        v3d_assemble_failed_name[v3d_assemble_failures]  = v3d_assemble_error_shader;
        v3d_assemble_failed_what[v3d_assemble_failures]  = v3d_assemble_error_what;
        v3d_assemble_failed_text[v3d_assemble_failures]  = v3d_assemble_error_text;
        v3d_assemble_failed_index[v3d_assemble_failures] = v3d_assemble_error_index;
    }
    v3d_assemble_failures++;
    return v3d_assemble_report_all;
}

#define NOTE_FAILURE(n, what, text, idx) \
    do { v3d_assemble_error_shader = (n);    v3d_assemble_error_what  = (what); \
         v3d_assemble_error_text   = (text); v3d_assemble_error_index = (idx); } while (0)

#else
#define NOTE_FAILURE(n, what, text, idx) do { } while (0)
#endif

static int v3d_assemble_into(struct v3d_device_info* devinfo, const char* name,
                             const char** assemblyLines, int numAssemblyLines,
                             v3d_qpu_instruction* outInstructions, int capacity,
                             int* outNumInstructions)
{
    struct v3d_qpu_validate_result validateResults;
    int assemblyLine;
    int numInstructions = 0;

    *outNumInstructions = 0;

    for (assemblyLine = 0; assemblyLine < numAssemblyLines; ++assemblyLine)
    {
        v3d_uint32 numCharactersRead;

        memset(&assembleArguments, 0, sizeof(assembleArguments));
        assembleArguments.devinfo = *devinfo;
        assembleArguments.assembly = assemblyLines[assemblyLine];

        numCharactersRead = v3d_qpu_assemble(&assembleArguments);
        if (!numCharactersRead)
        {
            D(("v3d_assemble_one_shader: failed to assemble %s instruction [%ld] column %ld: %s\n'%s'\n",
               name, (LONG)assemblyLine, (LONG)assembleArguments.errorAtOffset,
               assembleArguments.errorMessage, assembleArguments.assembly));
            NOTE_FAILURE(name, assembleArguments.errorMessage,
                         assembleArguments.assembly, assemblyLine);
            return FALSE;
        }

        if (numInstructions >= capacity)
        {
            D(("v3d_assemble_into: %s ran out of space (max %ld instructions)\n", name, (LONG)capacity));
            NOTE_FAILURE(name, "ran out of space", assemblyLines[assemblyLine], assemblyLine);
            return FALSE;
        }

        unpackedInstructions[numInstructions] = assembleArguments.instruction;

        if (!v3d_qpu_instr_pack(devinfo, &assembleArguments.instruction, &outInstructions[numInstructions]))
        {
            D(("v3d_assemble_one_shader: failed to pack %s instruction [%ld]\n'%s'\n",
               name, (LONG)assemblyLine, assembleArguments.assembly));
            NOTE_FAILURE(name, "failed to pack -- the encoding refused it",
                         assembleArguments.assembly, assemblyLine);
            return FALSE;
        }

        ++numInstructions;
    }

    *outNumInstructions = numInstructions;

    memset(&validateResults, 0, sizeof(validateResults));
    if (!v3d_qpu_validate(devinfo, unpackedInstructions, numInstructions, &validateResults))
    {
        D(("v3d_assemble_one_shader: validation error in %s at instruction [%ld]: %s\n'%s'\n",
           name, (LONG)validateResults.errorInstructionIndex, validateResults.errorMessage,
           assemblyLines[validateResults.errorInstructionIndex]));
        NOTE_FAILURE(name, validateResults.errorMessage,
                     assemblyLines[validateResults.errorInstructionIndex],
                     validateResults.errorInstructionIndex);
        return FALSE;
    }

    return TRUE;
}

/*
 * The 113 ordinary shaders. The cap is V3D_SHADER_MAX_INSTRUCTIONS, 256, and
 * it is the ASSEMBLER's buffer rather than a GPU slot size: the shaders are
 * packed end to end in one allocation, so a longer one costs bytes, not a
 * neighbour. Overrunning it fails loudly at init, caught by
 * shader_assembler_test.
 */
static int v3d_assemble_one_shader(struct v3d_device_info* devinfo, const char* name,
                                    const char** assemblyLines, int numAssemblyLines,
                                    V3DAssembledShader* out)
{
    if (v3d_assemble_into(devinfo, name, assemblyLines, numAssemblyLines,
                          out->instructions, V3D_SHADER_MAX_INSTRUCTIONS,
                          &out->numInstructions))
        return TRUE;

#ifdef MGLV3D_ASSEMBLE_REPORT
    /* Record it, and say whether to carry on: the test wants every failure. */
    return v3d_assemble_note_failure();
#else
    return FALSE;
#endif
}

/*
 * The same thing for a shader the driver does not carry. shader_assembler_test
 * assembles the textured fragment shader this way: its machine code is the only
 * checked-in ground truth we have (straight from the earlier library), but no
 * selector reaches it, so it lives with the test instead of in the driver.
 */
#ifdef MGLV3D_ASSEMBLE_REPORT
int v3d_assemble_shader(V3DDevice* device, const char* name,
                        const char** assemblyLines, int numAssemblyLines,
                        V3DAssembledShader* out)
{
    return v3d_assemble_one_shader(&device->deviceInfo, name,
                                   assemblyLines, numAssemblyLines, out);
}
#endif


/* Shared storage for every assembled shader variant -- see
 * v3d_shader_assembler.h's own comment on why this is static/persistent
 * rather than caller-provided, unlike the transient scratch buffers
 * above. */
V3DAssembledShader v3d_shader_variants[V3D_MAX_SHADER_VARIANTS];

int v3d_assemble_builtin_shaders(V3DDevice* device)
{
    if (!v3d_assemble_one_shader(&device->deviceInfo, "vertex",
                                  g_vertex_shader_assembly, V3D_ARRAY_SIZE(g_vertex_shader_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_VERTEX_TEXTURED]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "coordinate",
                                  g_coordinate_shader_assembly, V3D_ARRAY_SIZE(g_coordinate_shader_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_COORDINATE_TEXTURED]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured",
                                  g_fragment_shader_untextured_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_fog",
                                  g_fragment_shader_untextured_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_alphatest",
                                  g_fragment_shader_untextured_alphatest_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_alphatest_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "vertex_smooth",
                                  g_vertex_shader_smooth_assembly, V3D_ARRAY_SIZE(g_vertex_shader_smooth_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_VERTEX_SMOOTH]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_smooth",
                                  g_fragment_shader_untextured_smooth_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_smooth_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_smooth_alphatest",
                                  g_fragment_shader_untextured_smooth_alphatest_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_smooth_alphatest_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_ALPHATEST]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_smooth_alphatest_greater",
                                  g_fragment_shader_untextured_smooth_alphatest_greater_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_smooth_alphatest_greater_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_ALPHATEST_GREATER]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_smooth_alphatest_less",
                                  g_fragment_shader_untextured_smooth_alphatest_less_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_smooth_alphatest_less_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_ALPHATEST_LESS]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_smooth_alphatest_equal",
                                  g_fragment_shader_untextured_smooth_alphatest_equal_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_smooth_alphatest_equal_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_ALPHATEST_EQUAL]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_smooth_alphatest_lequal",
                                  g_fragment_shader_untextured_smooth_alphatest_lequal_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_smooth_alphatest_lequal_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_ALPHATEST_LEQUAL]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_smooth_alphatest_notequal",
                                  g_fragment_shader_untextured_smooth_alphatest_notequal_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_smooth_alphatest_notequal_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_ALPHATEST_NOTEQUAL]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_smooth_alphatest_never",
                                  g_fragment_shader_untextured_smooth_alphatest_never_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_smooth_alphatest_never_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_ALPHATEST_NEVER]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_smooth_fog_alphatest",
                                  g_fragment_shader_untextured_smooth_fog_alphatest_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_smooth_fog_alphatest_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_FOG_ALPHATEST]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_smooth_fog_alphatest_greater",
                                  g_fragment_shader_untextured_smooth_fog_alphatest_greater_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_smooth_fog_alphatest_greater_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_FOG_ALPHATEST_GREATER]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_smooth_fog_alphatest_less",
                                  g_fragment_shader_untextured_smooth_fog_alphatest_less_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_smooth_fog_alphatest_less_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_FOG_ALPHATEST_LESS]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_smooth_fog_alphatest_equal",
                                  g_fragment_shader_untextured_smooth_fog_alphatest_equal_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_smooth_fog_alphatest_equal_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_FOG_ALPHATEST_EQUAL]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_smooth_fog_alphatest_lequal",
                                  g_fragment_shader_untextured_smooth_fog_alphatest_lequal_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_smooth_fog_alphatest_lequal_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_FOG_ALPHATEST_LEQUAL]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_smooth_fog_alphatest_notequal",
                                  g_fragment_shader_untextured_smooth_fog_alphatest_notequal_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_smooth_fog_alphatest_notequal_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_FOG_ALPHATEST_NOTEQUAL]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_smooth_fog_alphatest_never",
                                  g_fragment_shader_untextured_smooth_fog_alphatest_never_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_smooth_fog_alphatest_never_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_FOG_ALPHATEST_NEVER]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "vertex_smooth_textured",
                                  g_vertex_shader_smooth_textured_assembly, V3D_ARRAY_SIZE(g_vertex_shader_smooth_textured_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_VERTEX_SMOOTH_TEXTURED]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth",
                                  g_fragment_shader_textured_smooth_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_shader_textured_smooth_envblend",
                                  g_fragment_shader_textured_smooth_envblend_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_envblend_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ENVBLEND]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_shader_textured_smooth_envadd",
                                  g_fragment_shader_textured_smooth_envadd_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_envadd_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ENVADD]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_fog",
                                  g_fragment_shader_textured_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_alphatest_greater",
                                  g_fragment_shader_textured_smooth_alphatest_greater_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_alphatest_greater_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST_GREATER]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_alphatest",
                                  g_fragment_shader_textured_smooth_alphatest_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_alphatest_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_alphatest",
                                  g_fragment_shader_textured_alphatest_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_alphatest_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "vertex_multitexture",
                                  g_vertex_shader_multitexture_assembly, V3D_ARRAY_SIZE(g_vertex_shader_multitexture_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_VERTEX_MULTITEXTURE]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_multitexture",
                                  g_fragment_shader_multitexture_assembly, V3D_ARRAY_SIZE(g_fragment_shader_multitexture_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "coordinate_clipspace",
                                  g_coordinate_shader_clipspace_assembly, V3D_ARRAY_SIZE(g_coordinate_shader_clipspace_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_COORDINATE_CLIPSPACE]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "vertex_smooth_textured_clipspace",
                                  g_vertex_shader_smooth_textured_clipspace_assembly, V3D_ARRAY_SIZE(g_vertex_shader_smooth_textured_clipspace_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_VERTEX_SMOOTH_TEXTURED_CLIPSPACE]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "vertex_multitexture_clipspace",
                                  g_vertex_shader_multitexture_clipspace_assembly, V3D_ARRAY_SIZE(g_vertex_shader_multitexture_clipspace_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_VERTEX_MULTITEXTURE_CLIPSPACE]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_multitexture_decal",
                                  g_fragment_shader_multitexture_decal_assembly, V3D_ARRAY_SIZE(g_fragment_shader_multitexture_decal_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_DECAL]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_multitexture_replace",
                                  g_fragment_shader_multitexture_replace_assembly, V3D_ARRAY_SIZE(g_fragment_shader_multitexture_replace_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_REPLACE]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_alphatest_greater",
                                  g_fragment_shader_untextured_alphatest_greater_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_alphatest_greater_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST_GREATER]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_alphatest_greater",
                                  g_fragment_shader_textured_alphatest_greater_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_alphatest_greater_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST_GREATER]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_colormod",
                                  g_fragment_shader_textured_colormod_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_colormod_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_COLORMOD]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_shader_textured_colormod_envblend",
                                  g_fragment_shader_textured_colormod_envblend_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_colormod_envblend_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_COLORMOD_ENVBLEND]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_shader_textured_colormod_envadd",
                                  g_fragment_shader_textured_colormod_envadd_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_colormod_envadd_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_COLORMOD_ENVADD]))
        return FALSE;

    /* Remaining alpha_func variants: 5 comparison functions x
     * the same 3 shapes the GEQUAL/GREATER families already use. Every
     * enum entry must be registered here or v3d_shader_variants[] stays
     * all-zero for it and the GPU executes a slot of zeroed instructions. */
    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_alphatest_less",
                                  g_fragment_shader_untextured_alphatest_less_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_alphatest_less_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST_LESS]))
        return FALSE;
    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_alphatest_less",
                                  g_fragment_shader_textured_alphatest_less_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_alphatest_less_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST_LESS]))
        return FALSE;
    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_alphatest_less",
                                  g_fragment_shader_textured_smooth_alphatest_less_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_alphatest_less_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST_LESS]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_alphatest_lequal",
                                  g_fragment_shader_untextured_alphatest_lequal_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_alphatest_lequal_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST_LEQUAL]))
        return FALSE;
    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_alphatest_lequal",
                                  g_fragment_shader_textured_alphatest_lequal_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_alphatest_lequal_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST_LEQUAL]))
        return FALSE;
    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_alphatest_lequal",
                                  g_fragment_shader_textured_smooth_alphatest_lequal_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_alphatest_lequal_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST_LEQUAL]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_alphatest_equal",
                                  g_fragment_shader_untextured_alphatest_equal_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_alphatest_equal_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST_EQUAL]))
        return FALSE;
    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_alphatest_equal",
                                  g_fragment_shader_textured_alphatest_equal_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_alphatest_equal_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST_EQUAL]))
        return FALSE;
    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_alphatest_equal",
                                  g_fragment_shader_textured_smooth_alphatest_equal_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_alphatest_equal_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST_EQUAL]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_alphatest_notequal",
                                  g_fragment_shader_untextured_alphatest_notequal_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_alphatest_notequal_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST_NOTEQUAL]))
        return FALSE;
    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_alphatest_notequal",
                                  g_fragment_shader_textured_alphatest_notequal_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_alphatest_notequal_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST_NOTEQUAL]))
        return FALSE;
    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_alphatest_notequal",
                                  g_fragment_shader_textured_smooth_alphatest_notequal_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_alphatest_notequal_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST_NOTEQUAL]))
        return FALSE;

    /* Combined fog + alpha test, 14 variants. */
    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_fog_alphatest",
                                  g_fragment_shader_untextured_fog_alphatest_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_fog_alphatest_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_fog_alphatest",
                                  g_fragment_shader_textured_fog_alphatest_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_fog_alphatest_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_fog_alphatest_greater",
                                  g_fragment_shader_untextured_fog_alphatest_greater_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_fog_alphatest_greater_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST_GREATER]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_fog_alphatest_greater",
                                  g_fragment_shader_textured_fog_alphatest_greater_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_fog_alphatest_greater_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST_GREATER]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_fog_alphatest_less",
                                  g_fragment_shader_untextured_fog_alphatest_less_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_fog_alphatest_less_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST_LESS]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_fog_alphatest_less",
                                  g_fragment_shader_textured_fog_alphatest_less_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_fog_alphatest_less_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST_LESS]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_fog_alphatest_equal",
                                  g_fragment_shader_untextured_fog_alphatest_equal_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_fog_alphatest_equal_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST_EQUAL]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_fog_alphatest_equal",
                                  g_fragment_shader_textured_fog_alphatest_equal_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_fog_alphatest_equal_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST_EQUAL]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_fog_alphatest_lequal",
                                  g_fragment_shader_untextured_fog_alphatest_lequal_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_fog_alphatest_lequal_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST_LEQUAL]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_fog_alphatest_lequal",
                                  g_fragment_shader_textured_fog_alphatest_lequal_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_fog_alphatest_lequal_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST_LEQUAL]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_fog_alphatest_notequal",
                                  g_fragment_shader_untextured_fog_alphatest_notequal_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_fog_alphatest_notequal_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST_NOTEQUAL]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_fog_alphatest_notequal",
                                  g_fragment_shader_textured_fog_alphatest_notequal_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_fog_alphatest_notequal_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST_NOTEQUAL]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_fog_alphatest_never",
                                  g_fragment_shader_untextured_fog_alphatest_never_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_fog_alphatest_never_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST_NEVER]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_fog_alphatest_never",
                                  g_fragment_shader_textured_fog_alphatest_never_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_fog_alphatest_never_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST_NEVER]))
        return FALSE;

    /* Smooth fog, 9 variants. */
    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_smooth_fog",
                                  g_fragment_shader_untextured_smooth_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_smooth_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_FOG]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_fog",
                                  g_fragment_shader_textured_smooth_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_fog_alphatest",
                                  g_fragment_shader_textured_smooth_fog_alphatest_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_fog_alphatest_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_fog_alphatest_greater",
                                  g_fragment_shader_textured_smooth_fog_alphatest_greater_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_fog_alphatest_greater_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST_GREATER]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_fog_alphatest_less",
                                  g_fragment_shader_textured_smooth_fog_alphatest_less_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_fog_alphatest_less_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST_LESS]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_fog_alphatest_equal",
                                  g_fragment_shader_textured_smooth_fog_alphatest_equal_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_fog_alphatest_equal_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST_EQUAL]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_fog_alphatest_lequal",
                                  g_fragment_shader_textured_smooth_fog_alphatest_lequal_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_fog_alphatest_lequal_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST_LEQUAL]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_fog_alphatest_notequal",
                                  g_fragment_shader_textured_smooth_fog_alphatest_notequal_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_fog_alphatest_notequal_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST_NOTEQUAL]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_fog_alphatest_never",
                                  g_fragment_shader_textured_smooth_fog_alphatest_never_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_fog_alphatest_never_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST_NEVER]))
        return FALSE;

    /* Multitexture fog, 3 variants. */
    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_multitexture_fog",
                                  g_fragment_shader_multitexture_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_multitexture_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_FOG]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_multitexture_decal_fog",
                                  g_fragment_shader_multitexture_decal_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_multitexture_decal_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_DECAL_FOG]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_multitexture_replace_fog",
                                  g_fragment_shader_multitexture_replace_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_multitexture_replace_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_REPLACE_FOG]))
        return FALSE;

    /* The two LIT VERTEX shaders, code slots 64 and 65. They are registered
     * among the fragment shaders, which is placement and nothing more: a code
     * slot is type-agnostic, so draw.c's two independent code addresses decide
     * what a slot holds. */
    if (!v3d_assemble_one_shader(&device->deviceInfo, "vertex_lit",
                                  g_vertex_shader_lit_assembly, V3D_ARRAY_SIZE(g_vertex_shader_lit_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_VERTEX_LIT]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "vertex_shader_lit_colormaterial",
                                  g_vertex_shader_lit_colormaterial_assembly, V3D_ARRAY_SIZE(g_vertex_shader_lit_colormaterial_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_VERTEX_LIT_COLORMATERIAL]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "vertex_lit_textured",
                                  g_vertex_shader_lit_textured_assembly, V3D_ARRAY_SIZE(g_vertex_shader_lit_textured_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_VERTEX_LIT_TEXTURED]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "vertex_shader_lit_textured_colormaterial",
                                  g_vertex_shader_lit_textured_colormaterial_assembly, V3D_ARRAY_SIZE(g_vertex_shader_lit_textured_colormaterial_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_VERTEX_LIT_TEXTURED_COLORMATERIAL]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "vertex_lit_realw",
                                  g_vertex_shader_lit_realw_assembly, V3D_ARRAY_SIZE(g_vertex_shader_lit_realw_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_VERTEX_LIT_REALW]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "vertex_shader_lit_realw_colormaterial",
                                  g_vertex_shader_lit_realw_colormaterial_assembly, V3D_ARRAY_SIZE(g_vertex_shader_lit_realw_colormaterial_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_VERTEX_LIT_REALW_COLORMATERIAL]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "vertex_lit_multitexture",
                                  g_vertex_shader_lit_multitexture_assembly, V3D_ARRAY_SIZE(g_vertex_shader_lit_multitexture_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_VERTEX_LIT_MULTITEXTURE]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "vertex_shader_lit_multitexture_colormaterial",
                                  g_vertex_shader_lit_multitexture_colormaterial_assembly, V3D_ARRAY_SIZE(g_vertex_shader_lit_multitexture_colormaterial_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_VERTEX_LIT_MULTITEXTURE_COLORMATERIAL]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_lit_multitexture",
                                  g_fragment_shader_lit_multitexture_assembly, V3D_ARRAY_SIZE(g_fragment_shader_lit_multitexture_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_LIT_MULTITEXTURE]))
        return FALSE;

    /* Software-blend fog, the remaining 12 variants. */

    /* Register-constrained blend fog, 5 variants. */
    /* Untextured flat blend fog, 3 variants. */
    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_alphatest_never",
                                  g_fragment_shader_untextured_alphatest_never_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_alphatest_never_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST_NEVER]))
        return FALSE;
    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_alphatest_never",
                                  g_fragment_shader_textured_alphatest_never_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_alphatest_never_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST_NEVER]))
        return FALSE;
    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_alphatest_never",
                                  g_fragment_shader_textured_smooth_alphatest_never_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_alphatest_never_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST_NEVER]))
        return FALSE;

    /* Smooth points, slots 110..113. */
    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_point_smooth",
                                  g_fragment_shader_untextured_point_smooth_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_point_smooth_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_POINT_SMOOTH]))
        return FALSE;
    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_smooth_point_smooth",
                                  g_fragment_shader_untextured_smooth_point_smooth_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_smooth_point_smooth_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_POINT_SMOOTH]))
        return FALSE;
    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_point_smooth",
                                  g_fragment_shader_textured_point_smooth_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_point_smooth_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_POINT_SMOOTH]))
        return FALSE;
    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_point_smooth",
                                  g_fragment_shader_textured_smooth_point_smooth_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_point_smooth_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_POINT_SMOOTH]))
        return FALSE;

    return TRUE;
}


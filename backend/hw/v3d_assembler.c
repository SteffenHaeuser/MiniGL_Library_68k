/*
 * (C) 2025-2026 Dennis van der Boon
 */

/*
 * QPU shader assembler, ported from PoC/v3d_assembler.h (a single-header
 * library -- #define V3D_ASSEMBLER_IMPLEMENTATION then #include to get the
 * implementation, matching stb-style headers). v3d_assembler.h holds that
 * header; this file is the adaptation layer PoC/v3d_assembler.c's own top
 * matter provided, rewritten for MiniGLV3D instead of RPIV3D.
 *
 * v3d_assembler.h needs two REQUIRED macros defined before inclusion:
 *  - v3d_memcmp: standard memcmp, no adaptation needed.
 *  - v3d_vsnprintf: only called by the header's disassembler section
 *    ("// >>> qpu_disasm.c", a single call site inside `append()`) --
 *    MiniGLV3D only ever ASSEMBLES (mnemonics -> machine code), never
 *    disassembles, so that code path is dead for our purposes. Stubbed
 *    rather than porting PoC's own myvsnprintf (which used a global
 *    `RPIV3D* constbase` -- exactly the kind of global-state pattern
 *    MiniGLV3D's architecture avoids; V3DDevice is always passed
 *    explicitly, never assumed via a global).
 *
 * struct v3d_device_info: v3d_assembler.h only forward-declares this
 * (`struct v3d_device_info;`) and uses it via pointer (devinfo->ver
 * etc.) -- needs the FULL definition in scope before the implementation
 * block. v3d_device.h's struct v3d_device_info is field-identical to
 * PoC/v3d_structs.h's original, so including it first is sufficient --
 * no new struct needed.
 *
 * D macro collision: v3d_assembler.h defines its own `#define D 1 //
 * Destination` internally (a QPU opcode-encoding field constant,
 * unrelated to this codebase's D()/E() debug-print macros) -- colliding
 * with v3d_debug.h's D(x). PoC's own v3d_assembler.c handles this by NOT
 * including v3d_debug.h until AFTER the implementation block (letting
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

/* static -- avoids colliding with a host application that defines its
 * own (non-static) byteswap64 and links into the same executable. */
static v3d_u64 byteswap64(v3d_u64 x)
{
    v3d_u64 hi_lo_swapped = (x << 32) | (x >> 32);
    v3d_u32 hi = (v3d_u32)(hi_lo_swapped >> 32);
    v3d_u32 lo = (v3d_u32)hi_lo_swapped;

    return ((v3d_u64)LE32(hi) << 32) | LE32(lo);
}

/*
 * Shader mnemonic source, copied verbatim from PoC/v3d_assembler.c
 * (lines 51-952, the ACTIVE set -- a second, disabled copy sits in a
 * permanent #if 0 block later in that file and was not carried over).
 * Same content as PoC/v3d_shaders.c's pseudocode+mnemonic form, just
 * without the register-allocation comments -- see that file for the
 * human-readable version if modifying these.
 */
static const char* g_fragment_shader_assembly[] = {
#if 1
	// get the first varying, which is s/w
	//ldvary( assembler, varying_div_w );
	// write first texture memory unit configuration value from uniforms
	//wrtmuc( assembler );

    "nop ; nop ; ldvary.r0 ; wrtmuc",

	// generate s value
	// acc = varying_div_w[s] * w
	//fmul( assembler, acc, varying_div_w, w );
	// write second texture memory unit configuration value from uniforms
	//wrtmuc( assembler );

    "nop ; fmul r1, r0, rf0 ; wrtmuc",

	// generate output value for s
	// s = varying_div_w[s] * w + varying_c[s]
	//fadd( assembler, s, acc, varying_c );
	// get next varying [t]
	//ldvary( assembler, varying_div_w );

    "fadd rf6, r1, r5 ; nop ; ldvary.r0",

	// generate first part of t output value
	// acc = varying_div_w[t] * w
	//fmul( assembler, acc, varying_div_w, w );

    "nop ; fmul r1, r0, rf0",

	// generate output value for t
	// t = varying_div_w[t] * w + varying_c[t]
	//fadd( assembler, t, acc, varying_c );

    "fadd rf5, r1, r5 ; nop",

	// wait another instruction for value in t to take
    "nop ; nop",

	// write t value into TMU
	//mov_addalu( assembler, rmagic(tmut), t );
	//thrsw( assembler );

    "or tmut, rf5, rf5 ; nop ; thrsw",

	// macoy version does an alu operation here and another thrsw...
	//thrsw( assembler );

    "nop ; nop ; thrsw",

	// write s value into TMU
	// this will trigger the TMU read operation
	//mov_addalu( assembler, rmagic(tmus), s );
	// put another thrsw in here?
	//thrsw( assembler ); //NO?

    "or tmus, rf6, rf6 ; nop", // ; thrsw",

	// load the TMU results for blue and green
	//ldtmu( assembler, blue_green );

    "nop ; nop ; ldtmu.rf4",

	// load tmu results for red and alpha
	//ldtmu( assembler, red_alpha );

    "nop ; nop ; ldtmu.rf3",

	// unpack the read texture pixel value into components for testing
	// then repack for tlb

	// this is confirmed to perform the exact same in terms of output
	// as just the reading from the TMU and stashing it right back
	// into the TLB

	// clear out the output values for now
	//sub_addalu( assembler, red_out, red_out, red_out );
	//sub_addalu( assembler, green_out, green_out, green_out );
    //sub_addalu( assembler, blue_out, blue_out, blue_out );
	//sub_addalu( assembler, alpha_out, alpha_out, alpha_out );
#if 1
    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",

	// load in the pixel colors
	//fadd( assembler, blue_out, blue_out, unpack_l(blue_green) );
	//fadd( assembler, green_out, green_out, unpack_h(blue_green) );
	//fadd( assembler, red_out, red_out, unpack_l(red_alpha) );
	//fadd( assembler, alpha_out, alpha_out, unpack_h(red_alpha) );

    "fadd rf7, rf7, rf4.l ; nop",  //corrected debugdebug
    "fadd rf8, rf8, rf4.h ; nop",
    "fadd rf9, rf9, rf3.l ; nop",
    "fadd rf10, rf10, rf3.h ; nop",

	// When an image looks good in the frame buffer, the colors are
	// different when shown as a texture.
	// red [frame buffer]   --> blue [texture]
	// green [frame buffer] --> green [texture]
	// blue [frame buffer]  --> red [texture]

	// repack values into output pixel
	//vfpack( assembler, rmagic(tlb), red_out, green_out );
	//thrsw( assembler );
	//vfpack( assembler, rmagic(tlb), blue_out, alpha_out );
#else
    "or rf7, 0x3f800000, 0x3f800000 ; nop",
    "or rf8, 0x3f800000, 0x3f800000 ; nop",
    "or rf9, 0x3f800000, 0x3f800000 ; nop",
    "or rf10, 0x3f800000, 0x3f800000 ; nop",
#endif
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",

#else
    // payload_w : rf0
    // payload_w_centroid : rf1
    // payload_z : rf2

	// Load S ; write uniform texture p0
    "nop                   ; nop                         ; ldvary.r0; wrtmuc", // (tex[0].p0 | 0x3)",
	// S * W ; load T ; write uniform texture p1
    "nop                   ; fmul r1, r0, rf0            ; ldvary.r3; wrtmuc",
	// S + r5 (from varying?) ; T * W ; load R
    "fadd r2, r1, r5       ; fmul r4, r3, rf0            ; ldvary.r1",
	// T + r5 (from varying?) ; R * W ; load G
    "fadd r0, r4, r5       ; fmul r3, r1, rf0            ; ldvary.r4",
	// R + r5 (from varying?) ; G * W ; load B
    "fadd rf3, r3, r5      ; fmul r1, r4, rf0            ; ldvary.r3",
	// G + r5 (from varying?) ; B * W ; load A
    "fadd rf4, r1, r5      ; fmul r4, r3, rf0            ; ldvary.r1",
	// B + r5 (from varying?) ; set T
    "fadd rf5, r4, r5      ; mov tmut, r0                ; thrsw",
	// A * W
    "nop                   ; fmul r3, r1, rf0            ; thrsw",
	// A + r5 (from varying?) ; Set S
    "fadd rf6, r3, r5      ; mov tmus, r2",
	// Load RG
    "nop                   ; nop                         ; ldtmu.r4",
	// R * sample R ; Load BA
    "nop                   ; fmul rf7, r4.l, rf3         ; ldtmu.r0",
	// G * sample G
	"nop                   ; fmul rf8, r4.h, rf4",
	// B * sample B
	"nop                   ; fmul rf9, r0.l, rf5",
	// A * sample A
	"nop                   ; fmul rf10, r0.h, rf6",
	// RG
    "vfpack tlb, rf7, rf8  ; nop                         ; thrsw",
	// BA
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
#endif
    // out[0] = vary[0] * payload_w + r5
    // out[1] = vary[1] * payload_w + r5
    // out[2] = vary[2] * payload_w + r5
    // out[3] = vary[3] * payload_w + r5
};

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
    "fadd rf3, rf3, rf4 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf4",             // C
    "nop ; fmul rf4, rf4, rf0",
    "nop ; nop ; ldunifrf.rf5",             // D
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
    "fmin rf3, rf3, rf4 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf4",             // fog red
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop",
    "nop ; nop ; ldunifrf.rf4",             // fog green
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop",
    "nop ; nop ; ldunifrf.rf4",             // fog blue
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
static const char* g_fragment_shader_untextured_blend_assembly[] = {
    "nop ; nop ; ldunifrf.rf7",  // rf7  = src red   (uniform 0)
    "nop ; nop ; ldunifrf.rf8",  // rf8  = src green (uniform 1)
    "nop ; nop ; ldunifrf.rf9",  // rf9  = src blue  (uniform 2)
    "nop ; nop ; ldunifrf.rf10", // rf10 = src alpha (uniform 3)

    /* SCOREBOARD LOCK. A TLB read must not happen before the scoreboard
     * lock is taken -- MESA nir_to_vir.c, vir_emit_tlb_color_read: "We need
     * to emit our TLB reads after we have acquired the scoreboard lock, or
     * the GPU will hang... we make sure we always emit a thread switch
     * before the first tlb color read." MESA's vir_emit_thrsw (same file)
     * emits exactly ONE thrsw there, not the doubled pair this file uses to
     * mark the program's LAST thread switch (two CONSECUTIVE thrsw, which
     * is how v3d_assembler.h's own validator identifies it) -- a different
     * convention for a different purpose.
     *
     * The gap matters too: MESA's scheduler (qpu_schedule.c,
     * scoreboard_is_locked) counts the scoreboard as locked only once
     * `tick - last_thrsw_tick >= 3`, i.e. at least 2 full instructions must
     * separate the thrsw from the first TLB access. The ported validator
     * (v3d_assembler.h, in_thrsw_delay_slots) checks only SFU/LDVARY-during-
     * delay-slots, THRSW-too-close-to-THRSW and THREND RF-write timing --
     * never TLB-read timing -- so it validating clean says nothing about
     * this requirement. Hence the two filler instructions below. */
    "nop ; nop ; thrsw", // single thread switch before the first tlb access
    "nop ; nop",         // delay slot 1 of 2 (tick+1)
    "nop ; nop",         // delay slot 2 of 2 (tick+2) -- ldtlb below lands at tick+3

    "nop ; nop ; ldtlb.rf11", // rf11 = packed dst (r,g)
    "nop ; nop ; ldtlb.rf12", // rf12 = packed dst (b,a)

    /* Unpack dst r,g,b,a -- same sub/fadd F16-unpack trick as the
     * multitexture fragment shader's own texel unpack. */
    "sub rf13, rf13, rf13 ; nop", "sub rf14, rf14, rf14 ; nop",
    "sub rf15, rf15, rf15 ; nop", "sub rf16, rf16, rf16 ; nop",
    "fadd rf13, rf13, rf11.l ; nop", // dst_r
    "fadd rf14, rf14, rf11.h ; nop", // dst_g
    "fadd rf15, rf15, rf12.l ; nop", // dst_b
    "fadd rf16, rf16, rf12.h ; nop", // dst_a

    "or rf17, 0x3f800000, 0x3f800000 ; nop", // rf17 = 1.0
    "fsub rf18, rf17, rf10 ; nop",           // rf18 = invAlpha = 1.0 - alpha

    /* result = src*alpha + dst*invAlpha, per channel (alpha channel uses
     * the same formula -- glBlendFunc, not glBlendFuncSeparate, applies
     * identical factors to color and alpha). */
    "nop ; fmul rf19, rf7, rf10",
    "nop ; fmul rf20, rf13, rf18",
    "fadd rf7, rf19, rf20 ; nop", // result_r

    "nop ; fmul rf19, rf8, rf10",
    "nop ; fmul rf20, rf14, rf18",
    "fadd rf8, rf19, rf20 ; nop", // result_g

    "nop ; fmul rf19, rf9, rf10",
    "nop ; fmul rf20, rf15, rf18",
    "fadd rf9, rf19, rf20 ; nop", // result_b

    "nop ; fmul rf19, rf10, rf10",
    "nop ; fmul rf20, rf16, rf18",
    "fadd rf10, rf19, rf20 ; nop", // result_a

    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- satisfies the >=3-instruction gap before the next thrsw

    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

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
static const char* g_fragment_shader_untextured_blend_add_assembly[] = {
    "nop ; nop ; ldunifrf.rf7",  // rf7  = src red   (uniform 0)
    "nop ; nop ; ldunifrf.rf8",  // rf8  = src green (uniform 1)
    "nop ; nop ; ldunifrf.rf9",  // rf9  = src blue  (uniform 2)
    "nop ; nop ; ldunifrf.rf10", // rf10 = src alpha (uniform 3)

    /* ldtlb SPACING: MESA's vir_emit_tlb_color_read (nir_to_vir.c) never
     * issues a second ldtlb immediately after the first for this packed-RG/
     * packed-BA shape -- it always unpacks the first read's two components
     * (2 FMOV instructions) BEFORE issuing the second ldtlb. This shader
     * does the same: rf11's unpack (dst_r/dst_g) happens before ldtlb.rf12
     * is issued, rather than both reads issuing first and all unpacking
     * happening after. */

    /* SCOREBOARD LOCK. A TLB read must not happen before the scoreboard
     * lock is taken -- MESA nir_to_vir.c vir_emit_tlb_color_read: "We need
     * to emit our TLB reads after we have acquired the scoreboard lock, or
     * the GPU will hang." The lock is taken on a thread switch, so one is
     * emitted here, and MESA's scheduler only counts the scoreboard as
     * locked once tick - last_thrsw_tick >= 3, hence two filler slots.
     * Which thread switch takes the lock is chosen by
     * do_scoreboard_wait_on_first_thread_switch in the shader record
     * (draw.c). */
    "nop ; nop ; thrsw",
    "nop ; nop",
    "nop ; nop",

    "nop ; nop ; ldtlb.rf11", // rf11 = packed dst (r,g)

    "sub rf13, rf13, rf13 ; nop",
    "sub rf14, rf14, rf14 ; nop",
    "fadd rf13, rf13, rf11.l ; nop", // dst_r
    "fadd rf14, rf14, rf11.h ; nop", // dst_g

    "nop ; nop ; ldtlb.rf12", // rf12 = packed dst (b,a) -- issued only after rf11's own unpack, matching MESA's spacing

    "sub rf15, rf15, rf15 ; nop",
    "sub rf16, rf16, rf16 ; nop",
    "fadd rf15, rf15, rf12.l ; nop", // dst_b
    "fadd rf16, rf16, rf12.h ; nop", // dst_a

    "or rf17, 0x3f800000, 0x3f800000 ; nop", // rf17 = 1.0

    /* result = src + dst, per channel, clamped to 1.0. */
    "fadd rf7, rf7, rf13 ; nop",
    "fmin rf7, rf7, rf17 ; nop", // result_r

    "fadd rf8, rf8, rf14 ; nop",
    "fmin rf8, rf8, rf17 ; nop", // result_g

    "fadd rf9, rf9, rf15 ; nop",
    "fmin rf9, rf9, rf17 ; nop", // result_b

    "fadd rf10, rf10, rf16 ; nop",
    "fmin rf10, rf10, rf17 ; nop", // result_a

    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- satisfies the >=3-instruction gap before the next thrsw

    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

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
static const char* g_fragment_shader_untextured_smooth_blend_add_assembly[] = {
    /* Per-vertex color, perspective-correct interpolation -- byte-for-byte
     * the same pattern as g_fragment_shader_untextured_smooth_assembly
     * above, just landing in rf7-rf10 (matching this shader's own
     * src-color register convention) instead of that shader's. */
    "nop ; nop ; ldvary.r0",  // load r/w
    "nop ; fmul r1, r0, rf0", // r1 = (r/w) * w
    "fadd rf7, r1, r5 ; nop", // rf7 = true red

    "nop ; nop ; ldvary.r0",  // load g/w
    "nop ; fmul r1, r0, rf0", // r1 = (g/w) * w
    "fadd rf8, r1, r5 ; nop", // rf8 = true green

    "nop ; nop ; ldvary.r0",  // load b/w
    "nop ; fmul r1, r0, rf0", // r1 = (b/w) * w
    "fadd rf9, r1, r5 ; nop", // rf9 = true blue

    "nop ; nop ; ldvary.r0",  // load a/w
    "nop ; fmul r1, r0, rf0", // r1 = (a/w) * w
    "fadd rf10, r1, r5 ; nop", // rf10 = true alpha


    /* SCOREBOARD LOCK. A TLB read must not happen before the scoreboard
     * lock is taken -- MESA nir_to_vir.c vir_emit_tlb_color_read: "We need
     * to emit our TLB reads after we have acquired the scoreboard lock, or
     * the GPU will hang." The lock is taken on a thread switch, so one is
     * emitted here, and MESA's scheduler only counts the scoreboard as
     * locked once tick - last_thrsw_tick >= 3, hence two filler slots.
     * Which thread switch takes the lock is chosen by
     * do_scoreboard_wait_on_first_thread_switch in the shader record
     * (draw.c). */
    "nop ; nop ; thrsw",
    "nop ; nop",
    "nop ; nop",

    "nop ; nop ; ldtlb.rf11", // rf11 = packed dst (r,g)
    "nop ; nop ; ldtlb.rf12", // rf12 = packed dst (b,a)

    "sub rf13, rf13, rf13 ; nop", "sub rf14, rf14, rf14 ; nop",
    "sub rf15, rf15, rf15 ; nop", "sub rf16, rf16, rf16 ; nop",
    "fadd rf13, rf13, rf11.l ; nop", // dst_r
    "fadd rf14, rf14, rf11.h ; nop", // dst_g
    "fadd rf15, rf15, rf12.l ; nop", // dst_b
    "fadd rf16, rf16, rf12.h ; nop", // dst_a

    "or rf17, 0x3f800000, 0x3f800000 ; nop", // rf17 = 1.0

    /* result = src + dst, per channel, clamped to 1.0. */
    "fadd rf7, rf7, rf13 ; nop",
    "fmin rf7, rf7, rf17 ; nop", // result_r

    "fadd rf8, rf8, rf14 ; nop",
    "fmin rf8, rf8, rf17 ; nop", // result_g

    "fadd rf9, rf9, rf15 ; nop",
    "fmin rf9, rf9, rf17 ; nop", // result_b

    "fadd rf10, rf10, rf16 ; nop",
    "fmin rf10, rf10, rf17 ; nop", // result_a

    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler

    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * GL_SRC_ALPHA/GL_ONE (alpha-weighted additive) software blend,
 * untextured, flat -- every other SRCALPHA-source variant in this family
 * pairs with INVSRCALPHA, so this combination needs its own shader. Same
 * splice technique as the rest of the family: the LERP shader's src*alpha
 * multiply, combined with the additive shader's dst-add-and-clamp tail
 * (the dst factor is ONE, not 1-alpha, so no invAlpha term is needed).
 * All 4 src*alpha products are computed into their OWN fresh registers
 * (rf19-rf22) before any of them is consumed -- mirroring the LERP
 * shader's two-fmul-before-fadd shape (rf19/rf20 computed, then both
 * consumed several instructions later) rather than an fmul-then-fadd on
 * the very next instruction, which no shader in this file does.
 */
static const char* g_fragment_shader_untextured_blend_srcalpha_one_assembly[] = {
    "nop ; nop ; ldunifrf.rf7",  // rf7  = src red   (uniform 0)
    "nop ; nop ; ldunifrf.rf8",  // rf8  = src green (uniform 1)
    "nop ; nop ; ldunifrf.rf9",  // rf9  = src blue  (uniform 2)
    "nop ; nop ; ldunifrf.rf10", // rf10 = src alpha (uniform 3)


    /* SCOREBOARD LOCK. A TLB read must not happen before the scoreboard
     * lock is taken -- MESA nir_to_vir.c vir_emit_tlb_color_read: "We need
     * to emit our TLB reads after we have acquired the scoreboard lock, or
     * the GPU will hang." The lock is taken on a thread switch, so one is
     * emitted here, and MESA's scheduler only counts the scoreboard as
     * locked once tick - last_thrsw_tick >= 3, hence two filler slots.
     * Which thread switch takes the lock is chosen by
     * do_scoreboard_wait_on_first_thread_switch in the shader record
     * (draw.c). */
    "nop ; nop ; thrsw",
    "nop ; nop",
    "nop ; nop",

    "nop ; nop ; ldtlb.rf11", // rf11 = packed dst (r,g)
    "nop ; nop ; ldtlb.rf12", // rf12 = packed dst (b,a)

    "sub rf13, rf13, rf13 ; nop", "sub rf14, rf14, rf14 ; nop",
    "sub rf15, rf15, rf15 ; nop", "sub rf16, rf16, rf16 ; nop",
    "fadd rf13, rf13, rf11.l ; nop", // dst_r
    "fadd rf14, rf14, rf11.h ; nop", // dst_g
    "fadd rf15, rf15, rf12.l ; nop", // dst_b
    "fadd rf16, rf16, rf12.h ; nop", // dst_a

    "or rf17, 0x3f800000, 0x3f800000 ; nop", // rf17 = 1.0

    /* src*alpha, all 4 channels, into fresh registers before any is
     * consumed. */
    "nop ; fmul rf19, rf7, rf10",  // rf19 = src_r * alpha
    "nop ; fmul rf20, rf8, rf10",  // rf20 = src_g * alpha
    "nop ; fmul rf21, rf9, rf10",  // rf21 = src_b * alpha
    "nop ; fmul rf22, rf10, rf10", // rf22 = alpha * alpha (before rf10 is overwritten)

    /* result = src*alpha + dst, per channel, clamped to 1.0
     * (SRC_ALPHA/ONE). */
    "fadd rf7, rf19, rf13 ; nop",
    "fmin rf7, rf7, rf17 ; nop", // result_r

    "fadd rf8, rf20, rf14 ; nop",
    "fmin rf8, rf8, rf17 ; nop", // result_g

    "fadd rf9, rf21, rf15 ; nop",
    "fmin rf9, rf9, rf17 ; nop", // result_b

    "fadd rf10, rf22, rf16 ; nop",
    "fmin rf10, rf10, rf17 ; nop", // result_a

    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- satisfies the >=3-instruction gap before the next thrsw

    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * Smooth-capable sibling of the GL_SRC_ALPHA/GL_ONE flat variant above --
 * same relationship as the additive family's own flat/smooth pair. The
 * per-vertex color read is byte-for-byte the same pattern as
 * g_fragment_shader_untextured_smooth_blend_add_assembly; only the combine
 * tail differs (src*alpha + dst instead of src + dst).
 */
static const char* g_fragment_shader_untextured_smooth_blend_srcalpha_one_assembly[] = {
    "nop ; nop ; ldvary.r0",  // load r/w
    "nop ; fmul r1, r0, rf0", // r1 = (r/w) * w
    "fadd rf7, r1, r5 ; nop", // rf7 = true red

    "nop ; nop ; ldvary.r0",  // load g/w
    "nop ; fmul r1, r0, rf0", // r1 = (g/w) * w
    "fadd rf8, r1, r5 ; nop", // rf8 = true green

    "nop ; nop ; ldvary.r0",  // load b/w
    "nop ; fmul r1, r0, rf0", // r1 = (b/w) * w
    "fadd rf9, r1, r5 ; nop", // rf9 = true blue

    "nop ; nop ; ldvary.r0",  // load a/w
    "nop ; fmul r1, r0, rf0", // r1 = (a/w) * w
    "fadd rf10, r1, r5 ; nop", // rf10 = true alpha


    /* SCOREBOARD LOCK. A TLB read must not happen before the scoreboard
     * lock is taken -- MESA nir_to_vir.c vir_emit_tlb_color_read: "We need
     * to emit our TLB reads after we have acquired the scoreboard lock, or
     * the GPU will hang." The lock is taken on a thread switch, so one is
     * emitted here, and MESA's scheduler only counts the scoreboard as
     * locked once tick - last_thrsw_tick >= 3, hence two filler slots.
     * Which thread switch takes the lock is chosen by
     * do_scoreboard_wait_on_first_thread_switch in the shader record
     * (draw.c). */
    "nop ; nop ; thrsw",
    "nop ; nop",
    "nop ; nop",

    "nop ; nop ; ldtlb.rf11", // rf11 = packed dst (r,g)
    "nop ; nop ; ldtlb.rf12", // rf12 = packed dst (b,a)

    "sub rf13, rf13, rf13 ; nop", "sub rf14, rf14, rf14 ; nop",
    "sub rf15, rf15, rf15 ; nop", "sub rf16, rf16, rf16 ; nop",
    "fadd rf13, rf13, rf11.l ; nop", // dst_r
    "fadd rf14, rf14, rf11.h ; nop", // dst_g
    "fadd rf15, rf15, rf12.l ; nop", // dst_b
    "fadd rf16, rf16, rf12.h ; nop", // dst_a

    "or rf17, 0x3f800000, 0x3f800000 ; nop", // rf17 = 1.0

    "nop ; fmul rf19, rf7, rf10",
    "nop ; fmul rf20, rf8, rf10",
    "nop ; fmul rf21, rf9, rf10",
    "nop ; fmul rf22, rf10, rf10",

    "fadd rf7, rf19, rf13 ; nop",
    "fmin rf7, rf7, rf17 ; nop", // result_r

    "fadd rf8, rf20, rf14 ; nop",
    "fmin rf8, rf8, rf17 ; nop", // result_g

    "fadd rf9, rf21, rf15 ; nop",
    "fmin rf9, rf9, rf17 ; nop", // result_b

    "fadd rf10, rf22, rf16 ; nop",
    "fmin rf10, rf10, rf17 ; nop", // result_a

    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler

    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * SMOOTH-capable LERP (SRC_ALPHA/ONE_MINUS_SRC_ALPHA) software-blend
 * shader: the flat/uniform-color LERP variant above cannot express a
 * per-vertex source colour. Combines the smooth-additive sibling's
 * per-vertex color read (ldvary + perspective-correct fmul/fadd) with the
 * flat LERP variant's combine math, unchanged. Takes VERTEX_SMOOTH.
 */
static const char* g_fragment_shader_untextured_smooth_blend_assembly[] = {
    "nop ; nop ; ldvary.r0",  // load r/w
    "nop ; fmul r1, r0, rf0", // r1 = (r/w) * w
    "fadd rf7, r1, r5 ; nop", // rf7 = true red

    "nop ; nop ; ldvary.r0",  // load g/w
    "nop ; fmul r1, r0, rf0", // r1 = (g/w) * w
    "fadd rf8, r1, r5 ; nop", // rf8 = true green

    "nop ; nop ; ldvary.r0",  // load b/w
    "nop ; fmul r1, r0, rf0", // r1 = (b/w) * w
    "fadd rf9, r1, r5 ; nop", // rf9 = true blue

    "nop ; nop ; ldvary.r0",  // load a/w
    "nop ; fmul r1, r0, rf0", // r1 = (a/w) * w
    "fadd rf10, r1, r5 ; nop", // rf10 = true alpha


    /* SCOREBOARD LOCK. A TLB read must not happen before the scoreboard
     * lock is taken -- MESA nir_to_vir.c vir_emit_tlb_color_read: "We need
     * to emit our TLB reads after we have acquired the scoreboard lock, or
     * the GPU will hang." The lock is taken on a thread switch, so one is
     * emitted here, and MESA's scheduler only counts the scoreboard as
     * locked once tick - last_thrsw_tick >= 3, hence two filler slots.
     * Which thread switch takes the lock is chosen by
     * do_scoreboard_wait_on_first_thread_switch in the shader record
     * (draw.c). */
    "nop ; nop ; thrsw",
    "nop ; nop",
    "nop ; nop",

    "nop ; nop ; ldtlb.rf11", // rf11 = packed dst (r,g)
    "nop ; nop ; ldtlb.rf12", // rf12 = packed dst (b,a)

    "sub rf13, rf13, rf13 ; nop", "sub rf14, rf14, rf14 ; nop",
    "sub rf15, rf15, rf15 ; nop", "sub rf16, rf16, rf16 ; nop",
    "fadd rf13, rf13, rf11.l ; nop", // dst_r
    "fadd rf14, rf14, rf11.h ; nop", // dst_g
    "fadd rf15, rf15, rf12.l ; nop", // dst_b
    "fadd rf16, rf16, rf12.h ; nop", // dst_a

    "or rf17, 0x3f800000, 0x3f800000 ; nop", // rf17 = 1.0
    "fsub rf18, rf17, rf10 ; nop",           // rf18 = invAlpha = 1.0 - alpha

    /* result = src*alpha + dst*invAlpha, per channel -- identical
     * formula to the flat LERP variant above, just fed real per-vertex
     * src color instead of a uniform. */
    "nop ; fmul rf19, rf7, rf10",
    "nop ; fmul rf20, rf13, rf18",
    "fadd rf7, rf19, rf20 ; nop", // result_r

    "nop ; fmul rf19, rf8, rf10",
    "nop ; fmul rf20, rf14, rf18",
    "fadd rf8, rf19, rf20 ; nop", // result_g

    "nop ; fmul rf19, rf9, rf10",
    "nop ; fmul rf20, rf15, rf18",
    "fadd rf9, rf19, rf20 ; nop", // result_b

    "nop ; fmul rf19, rf10, rf10",
    "nop ; fmul rf20, rf16, rf18",
    "fadd rf10, rf19, rf20 ; nop", // result_a

    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler

    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * TEXTURED software blend: the software-blend shaders above all emit a
 * flat/uniform or per-vertex-varying color with no texture sampling at
 * all, so a textured draw needs its own variant.
 *
 * Combines g_fragment_shader_assembly's texture-sample sequence (ldvary +
 * w-divide s/t computation, TMU write/trigger, ldtmu read, F16 unpack --
 * the texture-read channel convention is blue,green,red,alpha in
 * rf7/rf8/rf9/rf10, NOT straight r,g,b,a, matching that shader's own
 * "colors are different when shown as a texture" comment) with the
 * SRC_ALPHA/ONE_MINUS_SRC_ALPHA LERP-against-tile-buffer math from
 * g_fragment_shader_untextured_blend_assembly above, and the
 * texture+ldtlb-in-one-shader structure/thrsw layout from
 * g_fragment_shader_multitexture_modulate_blend_assembly (single thrsw
 * pair for the one TMU trigger, single final thrsw at the last vfpack --
 * NOT the doubled-thrsw-pair ending the flat/smooth software-blend
 * variants use).
 *
 * GL_MODULATE means the final alpha is tex_alpha * color_alpha, not
 * tex_alpha alone -- a uniform carries that color-alpha multiplier in,
 * since it is not part of the texture sample itself.
 */
static const char* g_fragment_shader_textured_blend_assembly[] = {
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

    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",  // tex_blue
    "fadd rf8, rf8, rf4.h ; nop",  // tex_green
    "fadd rf9, rf9, rf3.l ; nop",  // tex_red
    "fadd rf10, rf10, rf3.h ; nop", // tex_alpha

    /* RGB modulation by glColor, one channel at a time: a texture that
     * carries no colour of its own (flat white with only its ALPHA shaped)
     * takes its colour entirely from glColor's RGB, so the RGB channels
     * must be modulated as well as the alpha. rf5 is dead after the
     * tmu-request thrsw section above, so no new register is needed and
     * all three channels need not be held at once. */
    "nop ; nop ; ldunifrf.rf5", // rf5 = color blue multiplier (uniform 0)
    "nop ; fmul rf7, rf7, rf5", // rf7 = tex_blue * color_blue
    "nop ; nop ; ldunifrf.rf5", // rf5 = color green multiplier (uniform 1)
    "nop ; fmul rf8, rf8, rf5", // rf8 = tex_green * color_green
    "nop ; nop ; ldunifrf.rf5", // rf5 = color red multiplier (uniform 2)
    "nop ; fmul rf9, rf9, rf5", // rf9 = tex_red * color_red

    "nop ; nop ; ldunifrf.rf24", // rf24 = glColor alpha multiplier (uniform 3)
    "nop ; fmul rf10, rf10, rf24", // rf10 = final alpha = tex_alpha * color_alpha

    "nop ; nop ; ldtlb.rf25", // dst (r,g)
    "nop ; nop ; ldtlb.rf26", // dst (b,a)

    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop", // dst_red
    "fadd rf28, rf28, rf25.h ; nop", // dst_green
    "fadd rf29, rf29, rf26.l ; nop", // dst_blue
    "fadd rf30, rf30, rf26.h ; nop", // dst_alpha

    "or rf24, 0x3f800000, 0x3f800000 ; nop", // reuse rf24 (color-alpha no longer needed) for 1.0
    "fsub rf24, rf24, rf10 ; nop", // rf24 = invAlpha = 1.0 - final_alpha

    /* The destination term is paired by REGISTER INDEX: rf7 (vfpack's RED
     * slot) takes dst_red (rf27) and rf9 (the BLUE slot) takes dst_blue
     * (rf29). rf7/rf9 already hold the cross-labelled texel data (this
     * shader family's established convention), so pairing by the colour
     * NAME in the labels instead would cross the background term and tint
     * it at low alpha. Same pairing as
     * g_fragment_shader_textured_smooth_blend_assembly. */
    "nop ; fmul r0, rf7, rf10",   // tex_blue * alpha
    "nop ; fmul r1, rf27, rf24",  // dst_red * invAlpha
    "fadd rf7, r0, r1 ; nop",     // result_red -> rf7

    "nop ; fmul r0, rf8, rf10",   // tex_green * alpha
    "nop ; fmul r1, rf28, rf24",  // dst_green * invAlpha
    "fadd rf8, r0, r1 ; nop",     // result_green -> rf8

    "nop ; fmul r0, rf9, rf10",   // tex_red * alpha
    "nop ; fmul r1, rf29, rf24",  // dst_blue * invAlpha
    "fadd rf9, r0, r1 ; nop",     // result_blue -> rf9

    "nop ; fmul r0, rf10, rf10",  // alpha * alpha
    "nop ; fmul r1, rf30, rf24",  // dst_alpha * invAlpha
    "fadd rf10, r0, r1 ; nop",    // result_alpha -> rf10

    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

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

    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",  // tex_blue
    "fadd rf8, rf8, rf4.h ; nop",  // tex_green
    "fadd rf9, rf9, rf3.l ; nop",  // tex_red
    "fadd rf10, rf10, rf3.h ; nop", // tex_alpha

    "nop ; nop ; ldunifrf.rf5", // rf5 = color blue multiplier (uniform 0)
    "nop ; fmul rf7, rf7, rf5", // rf7 = tex_blue * color_blue
    "nop ; nop ; ldunifrf.rf5", // rf5 = color green multiplier (uniform 1)
    "nop ; fmul rf8, rf8, rf5", // rf8 = tex_green * color_green
    "nop ; nop ; ldunifrf.rf5", // rf5 = color red multiplier (uniform 2)
    "nop ; fmul rf9, rf9, rf5", // rf9 = tex_red * color_red
    "nop ; nop ; ldunifrf.rf24", // rf24 = color alpha multiplier (uniform 3)
    "nop ; fmul rf10, rf10, rf24", // rf10 = tex_alpha * color_alpha

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
static const char* g_fragment_shader_textured_smooth_blend_assembly[] = {
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha

    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    /* From here down: verbatim from g_fragment_shader_textured_blend_
     * assembly's own ldtlb-LERP section, unmodified -- rf7-rf10 already
     * hold modulated color, matching what THAT shader has at this same
     * point in its own sequence. */
    "nop ; nop ; ldtlb.rf25", // dst (r,g)
    "nop ; nop ; ldtlb.rf26", // dst (b,a)

    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop", // dst_red
    "fadd rf28, rf28, rf25.h ; nop", // dst_green
    "fadd rf29, rf29, rf26.l ; nop", // dst_blue
    "fadd rf30, rf30, rf26.h ; nop", // dst_alpha

    "or rf24, 0x3f800000, 0x3f800000 ; nop", // rf24 = 1.0
    "fsub rf24, rf24, rf10 ; nop", // rf24 = invAlpha = 1.0 - final_alpha

    /* The destination term is paired by REGISTER INDEX: rf7 (which ends up
     * in vfpack's RED slot) takes dst_red (rf27), rf9 (the BLUE slot)
     * takes dst_blue (rf29). rf7/rf9 already hold the correct red/blue by
     * this point -- the vertex-color double-crossing upstream (draw.c's
     * CPU-side swap feeding this shader's rf20/rf22 crossing above)
     * cancels out -- so pairing the background term by the colour NAME in
     * the labels instead would cross it independently. That is invisible
     * at high alpha, where the source colour dominates, and tints the
     * background at low alpha. */
    "nop ; fmul r0, rf7, rf10",   // true red * alpha
    "nop ; fmul r1, rf27, rf24",  // dst_red * invAlpha
    "fadd rf7, r0, r1 ; nop",     // result_red -> rf7

    "nop ; fmul r0, rf8, rf10",   // tex_green * alpha
    "nop ; fmul r1, rf28, rf24",  // dst_green * invAlpha
    "fadd rf8, r0, r1 ; nop",     // result_green -> rf8

    "nop ; fmul r0, rf9, rf10",   // true blue * alpha
    "nop ; fmul r1, rf29, rf24",  // dst_blue * invAlpha
    "fadd rf9, r0, r1 ; nop",     // result_blue -> rf9

    "nop ; fmul r0, rf10, rf10",  // alpha * alpha
    "nop ; fmul r1, rf30, rf24",  // dst_alpha * invAlpha
    "fadd rf10, r0, r1 ; nop",    // result_alpha -> rf10

    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * Four textured+smooth+!multitextured software-blend variants, one per dst
 * factor with src=GL_DST_COLOR: SRCCOLOR, INVDSTALPHA, ZERO and ONE.
 *
 * All 4 are the exact same splice technique as
 * g_fragment_shader_textured_smooth_blend_assembly directly above:
 *   1. Front section (TMU fetch + per-vertex-color modulation), copied
 *      verbatim -- produces rf7/rf8/rf9/rf10 = modulated (blue,green,red,
 *      alpha), same as every other textured+smooth variant in this file.
 *   2. ldtlb dest-read + unpack, also copied verbatim -- produces
 *      rf27/rf28/rf29/rf30 = dst (red,green,blue,alpha), matching that
 *      shader's own comment on the convention.
 *   3. NEW tail: real blend math for src=DST_COLOR (i.e. Sf=Cd, using the
 *      just-read destination color as the source factor) against each
 *      shader's own fixed dst factor Df, per the standard blend equation
 *      result = Cs*Sf + Cd*Df = Cs*Cd + Cd*Df:
 *        - ZERO (Df=0):      result = Cs*Cd            (pure multiply)
 *        - ONE (Df=1):       result = Cs*Cd + Cd
 *        - SRCCOLOR (Df=Cs): result = Cs*Cd + Cd*Cs = 2*Cs*Cd
 *        - INVDSTALPHA (Df=1-Cd.a): result = Cs*Cd + Cd*(1-Cd.a)
 *      Every instruction is single-op-per-slot (nop in the unused slot),
 *      matching this file's own established convention throughout rather
 *      than attempting dual-issue -- no new QPU idiom introduced, same
 *      register file (r0/r1 scratch, rf24 as the reusable 1.0 constant,
 *      same as the LERP tail above), same thrsw/vfpack ending shape.
 */
static const char* g_fragment_shader_textured_smooth_dstcolor_zero_assembly[] = {
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha

    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "nop ; nop ; ldtlb.rf25", // dst (r,g)
    "nop ; nop ; ldtlb.rf26", // dst (b,a)

    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop", // dst_red
    "fadd rf28, rf28, rf25.h ; nop", // dst_green
    "fadd rf29, rf29, rf26.l ; nop", // dst_blue
    "fadd rf30, rf30, rf26.h ; nop", // dst_alpha

    /* result = Cs*Cd (Df=ZERO, no dst term). Paired by REGISTER INDEX
     * (rf7<->rf27, rf8<->rf28, rf9<->rf29, rf10<->rf30), matching the LERP
     * tail above -- NOT by the front section's own "blue/green/red/alpha"
     * slot labels, which disagree with the downstream "true red/true blue"
     * labels that same tail uses at this exact point (a red/blue crossing
     * baked into this whole shader family and compensated for on the C
     * side). Pairing by colour NAME instead of by index produces a visible
     * red/blue swap. */
    "nop ; fmul rf7, rf7, rf27",   // true_red = red * dst_red
    "nop ; fmul rf8, rf8, rf28",   // green = green * dst_green
    "nop ; fmul rf9, rf9, rf29",   // true_blue = blue * dst_blue
    "nop ; fmul rf10, rf10, rf30", // alpha = alpha * dst_alpha

    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

static const char* g_fragment_shader_textured_smooth_dstcolor_one_assembly[] = {
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha

    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "nop ; nop ; ldtlb.rf25", // dst (r,g)
    "nop ; nop ; ldtlb.rf26", // dst (b,a)

    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop", // dst_red
    "fadd rf28, rf28, rf25.h ; nop", // dst_green
    "fadd rf29, rf29, rf26.l ; nop", // dst_blue
    "fadd rf30, rf30, rf26.h ; nop", // dst_alpha

    /* result = Cs*Cd + Cd (Df=ONE). Paired by REGISTER INDEX -- see the
     * ZERO variant's own comment above for why (rf7<->rf27, rf9<->rf29,
     * not by color name). */
    "nop ; fmul r0, rf7, rf27",  // r0 = true_red * dst_red
    "fadd rf7, r0, rf27 ; nop",  // true_red result

    "nop ; fmul r0, rf8, rf28",  // green
    "fadd rf8, r0, rf28 ; nop",

    "nop ; fmul r0, rf9, rf29",  // true_blue
    "fadd rf9, r0, rf29 ; nop",

    "nop ; fmul r0, rf10, rf30", // alpha
    "fadd rf10, r0, rf30 ; nop",

    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

static const char* g_fragment_shader_textured_smooth_dstcolor_srccolor_assembly[] = {
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha

    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "nop ; nop ; ldtlb.rf25", // dst (r,g)
    "nop ; nop ; ldtlb.rf26", // dst (b,a)

    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop", // dst_red
    "fadd rf28, rf28, rf25.h ; nop", // dst_green
    "fadd rf29, rf29, rf26.l ; nop", // dst_blue
    "fadd rf30, rf30, rf26.h ; nop", // dst_alpha

    /* result = Cs*Cd + Cd*Cs = 2*Cs*Cd (Df=SRCCOLOR=Cs, same term twice).
     * Paired by REGISTER INDEX -- see the ZERO variant's own comment
     * above for why (rf7<->rf27, rf9<->rf29, not by color name). */
    "nop ; fmul r0, rf7, rf27",  // r0 = true_red * dst_red
    "fadd rf7, r0, r0 ; nop",    // true_red result = 2*r0

    "nop ; fmul r0, rf8, rf28",
    "fadd rf8, r0, r0 ; nop",

    "nop ; fmul r0, rf9, rf29",
    "fadd rf9, r0, r0 ; nop",

    "nop ; fmul r0, rf10, rf30",
    "fadd rf10, r0, r0 ; nop",

    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

static const char* g_fragment_shader_textured_smooth_dstcolor_invdstalpha_assembly[] = {
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha

    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "nop ; nop ; ldtlb.rf25", // dst (r,g)
    "nop ; nop ; ldtlb.rf26", // dst (b,a)

    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop", // dst_red
    "fadd rf28, rf28, rf25.h ; nop", // dst_green
    "fadd rf29, rf29, rf26.l ; nop", // dst_blue
    "fadd rf30, rf30, rf26.h ; nop", // dst_alpha

    /* result = Cs*Cd + Cd*(1-Cd.a) (Df=INVDSTALPHA). Paired by REGISTER
     * INDEX -- see the ZERO variant's own comment above for why
     * (rf7<->rf27, rf9<->rf29, not by color name). */
    "or rf24, 0x3f800000, 0x3f800000 ; nop", // rf24 = 1.0
    "fsub rf24, rf24, rf30 ; nop", // rf24 = invDstAlpha = 1.0 - dst_alpha

    "nop ; fmul r0, rf7, rf27",   // r0 = true_red * dst_red
    "nop ; fmul r1, rf27, rf24",  // r1 = dst_red * invDstAlpha
    "fadd rf7, r0, r1 ; nop",     // true_red result

    "nop ; fmul r0, rf8, rf28",
    "nop ; fmul r1, rf28, rf24",
    "fadd rf8, r0, r1 ; nop",

    "nop ; fmul r0, rf9, rf29",
    "nop ; fmul r1, rf29, rf24",
    "fadd rf9, r0, r1 ; nop",

    "nop ; fmul r0, rf10, rf30",
    "nop ; fmul r1, rf30, rf24",
    "fadd rf10, r0, r1 ; nop",

    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * Textured+smooth+!multitextured software blend, src=ZERO,
 * dst=INVSRCCOLOR. Same splice technique as the 4 DSTCOLOR variants
 * directly above -- identical front section + ldtlb dest-read, only the
 * tail math differs:
 *   result = Cs*Sf + Cd*Df = Cs*0 + Cd*(1-Cs) = Cd*(1-Cs)
 * Paired by REGISTER INDEX throughout (rf7<->rf27, rf8<->rf28,
 * rf9<->rf29, rf10<->rf30), the same convention as the DSTCOLOR family.
 */
static const char* g_fragment_shader_textured_smooth_zero_invsrccolor_assembly[] = {
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha

    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "nop ; nop ; ldtlb.rf25", // dst (r,g)
    "nop ; nop ; ldtlb.rf26", // dst (b,a)

    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop", // dst_red
    "fadd rf28, rf28, rf25.h ; nop", // dst_green
    "fadd rf29, rf29, rf26.l ; nop", // dst_blue
    "fadd rf30, rf30, rf26.h ; nop", // dst_alpha

    /* result = Cd*(1-Cs) (Sf=ZERO drops the Cs*Sf term entirely, Df=
     * INVSRCCOLOR=1-Cs). Paired by REGISTER INDEX -- see the DSTCOLOR
     * family's own comment for why (rf7<->rf27, rf9<->rf29, not by
     * color name). */
    "or rf24, 0x3f800000, 0x3f800000 ; nop", // rf24 = 1.0

    "fsub r0, rf24, rf7 ; nop",   // r0 = 1 - true_red (src)
    "nop ; fmul rf7, rf27, r0",   // true_red result = dst_red * (1-src)

    "fsub r0, rf24, rf8 ; nop",   // r0 = 1 - green
    "nop ; fmul rf8, rf28, r0",   // green result = dst_green * (1-src)

    "fsub r0, rf24, rf9 ; nop",   // r0 = 1 - true_blue
    "nop ; fmul rf9, rf29, r0",   // true_blue result

    "fsub r0, rf24, rf10 ; nop",  // r0 = 1 - alpha
    "nop ; fmul rf10, rf30, r0",  // alpha result

    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * Back in the DSTCOLOR family, with a dst factor (SRCALPHA) the original
 * four do not cover. Same splice technique, same front section + ldtlb
 * dest-read as every other variant in this family. Tail math:
 *   result = Cs*Sf + Cd*Df = Cs*Cd + Cd*Cs.a = Cd*(Cs+Cs.a)
 * SRCALPHA (Cs.a, rf10) is a single SCALAR applied uniformly to every
 * channel's dst term -- the same role rf10 plays in the LERP shader's own
 * tail (g_fragment_shader_textured_smooth_blend_assembly above). Paired by
 * REGISTER INDEX throughout (rf7<->rf27, rf8<->rf28, rf9<->rf29,
 * rf10<->rf30), the same convention as the rest of the DSTCOLOR family.
 */
static const char* g_fragment_shader_textured_smooth_dstcolor_srcalpha_assembly[] = {
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha

    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "nop ; nop ; ldtlb.rf25", // dst (r,g)
    "nop ; nop ; ldtlb.rf26", // dst (b,a)

    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop", // dst_red
    "fadd rf28, rf28, rf25.h ; nop", // dst_green
    "fadd rf29, rf29, rf26.l ; nop", // dst_blue
    "fadd rf30, rf30, rf26.h ; nop", // dst_alpha

    /* result = Cd*(Cs+Cs.a) (Sf=DSTCOLOR=Cd, Df=SRCALPHA=Cs.a applied
     * uniformly to every channel). Paired by REGISTER INDEX -- see the
     * DSTCOLOR family's own comment for why (rf7<->rf27, rf9<->rf29,
     * not by color name). */
    "fadd r0, rf7, rf10 ; nop",   // r0 = true_red + alpha
    "nop ; fmul rf7, rf27, r0",   // true_red result = dst_red * (red+alpha)

    "fadd r0, rf8, rf10 ; nop",   // r0 = green + alpha
    "nop ; fmul rf8, rf28, r0",   // green result

    "fadd r0, rf9, rf10 ; nop",   // r0 = true_blue + alpha
    "nop ; fmul rf9, rf29, r0",   // true_blue result

    "fadd r0, rf10, rf10 ; nop",  // r0 = alpha + alpha
    "nop ; fmul rf10, rf30, r0",  // alpha result

    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * Two more src factors for this family: ONE and INVSRCALPHA. Same splice
 * technique as the rest of it -- identical front section + ldtlb
 * dest-read, only the tail math differs. Paired by REGISTER INDEX
 * throughout (rf7<->rf27, rf8<->rf28, rf9<->rf29, rf10<->rf30), the same
 * convention as the rest of the family (see v3d_shader_assembler.h's own
 * comment on these 2 enum entries for the exact formulas).
 */
static const char* g_fragment_shader_textured_smooth_one_invsrcalpha_assembly[] = {
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha

    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "nop ; nop ; ldtlb.rf25", // dst (r,g)
    "nop ; nop ; ldtlb.rf26", // dst (b,a)

    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop", // dst_red
    "fadd rf28, rf28, rf25.h ; nop", // dst_green
    "fadd rf29, rf29, rf26.l ; nop", // dst_blue
    "fadd rf30, rf30, rf26.h ; nop", // dst_alpha

    /* result = Cs + Cd*(1-Cs.a) (Sf=ONE, Df=INVSRCALPHA=1-Cs.a applied
     * uniformly to every channel). rf24=invAlpha snapshotted from the
     * ORIGINAL rf10 (alpha) BEFORE rf10 itself is overwritten last --
     * every other channel's own read of rf24 (not rf10 directly) stays
     * correct regardless of ordering. */
    "or rf24, 0x3f800000, 0x3f800000 ; nop", // rf24 = 1.0
    "fsub rf24, rf24, rf10 ; nop", // rf24 = invAlpha = 1 - alpha

    "nop ; fmul r0, rf27, rf24",   // r0 = dst_red * invAlpha
    "fadd rf7, rf7, r0 ; nop",     // true_red result = red + r0

    "nop ; fmul r0, rf28, rf24",
    "fadd rf8, rf8, r0 ; nop",

    "nop ; fmul r0, rf29, rf24",
    "fadd rf9, rf9, r0 ; nop",

    "nop ; fmul r0, rf30, rf24",
    "fadd rf10, rf10, r0 ; nop",   // last write to rf10 -- safe, rf24 already snapshotted it

    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

static const char* g_fragment_shader_textured_smooth_invsrcalpha_srcalpha_assembly[] = {
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha

    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "nop ; nop ; ldtlb.rf25", // dst (r,g)
    "nop ; nop ; ldtlb.rf26", // dst (b,a)

    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop", // dst_red
    "fadd rf28, rf28, rf25.h ; nop", // dst_green
    "fadd rf29, rf29, rf26.l ; nop", // dst_blue
    "fadd rf30, rf30, rf26.h ; nop", // dst_alpha

    /* result = Cs*(1-Cs.a) + Cd*Cs.a (Sf=INVSRCALPHA, Df=SRCALPHA). Every
     * channel's r0/r1 pair reads rf10 (alpha) directly BEFORE rf10 itself
     * is overwritten last, so the ordering here is safe the same way as
     * the ONE_INVSRCALPHA variant above. */
    "or rf24, 0x3f800000, 0x3f800000 ; nop", // rf24 = 1.0
    "fsub rf24, rf24, rf10 ; nop", // rf24 = invAlpha = 1 - alpha

    "nop ; fmul r0, rf7, rf24",    // r0 = true_red * invAlpha
    "nop ; fmul r1, rf27, rf10",   // r1 = dst_red * alpha
    "fadd rf7, r0, r1 ; nop",      // true_red result

    "nop ; fmul r0, rf8, rf24",
    "nop ; fmul r1, rf28, rf10",
    "fadd rf8, r0, r1 ; nop",

    "nop ; fmul r0, rf9, rf24",
    "nop ; fmul r1, rf29, rf10",
    "fadd rf9, r0, r1 ; nop",

    "nop ; fmul r0, rf10, rf24",   // reads rf10 (still original alpha)
    "nop ; fmul r1, rf30, rf10",   // reads rf10 (still original alpha)
    "fadd rf10, r0, r1 ; nop",     // last write to rf10

    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * Textured + smooth + ONE/ONE additive software blend. The two untextured
 * additive variants (g_fragment_shader_untextured_blend_add_assembly and
 * its smooth sibling) structurally require `!textured`; the two textured
 * variants above cover only the SRC_ALPHA/INV_SRC_ALPHA LERP pair.
 *
 * A splice of two existing pieces, the same technique as
 * g_fragment_shader_textured_smooth_blend_assembly (see that shader's
 * comment above):
 *   1. that shader's TMU fetch + per-vertex-color modulate front section,
 *      verbatim, up to where it has rf7-rf10 = modulated
 *      (blue,green,red,alpha) in its own vfpack-slot convention.
 *   2. g_fragment_shader_untextured_smooth_blend_add_assembly's ldtlb
 *      dest-read + additive-clamp combine tail, verbatim from its ldtlb
 *      reads onward -- its own "2 consecutive thrsw" pair near the end is
 *      dropped, because piece 1's TMU-fetch thrsw pair already satisfies
 *      the "2 consecutive" half of the thrsw protocol and only the one
 *      embedded in the final vfpack is still needed, leaving the same
 *      3-line ending g_fragment_shader_textured_smooth_blend_assembly has.
 *
 * The dst term is paired with piece 1's rf7/rf8/rf9/rf10 BY POSITION
 * (1st-ldtlb-read-low, 1st-read-high, 2nd-read-low, 2nd-read-high), not by
 * channel name -- the R/B crossing baked into this family means the
 * "red"/"blue" labels do not track physical channel identity past the
 * modulate step.
 */
static const char* g_fragment_shader_textured_smooth_blend_add_assembly[] = {
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha

    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "nop ; nop ; ldtlb.rf11", // rf11 = packed dst (r,g)
    "nop ; nop ; ldtlb.rf12", // rf12 = packed dst (b,a)

    "sub rf13, rf13, rf13 ; nop", "sub rf14, rf14, rf14 ; nop",
    "sub rf15, rf15, rf15 ; nop", "sub rf16, rf16, rf16 ; nop",
    "fadd rf13, rf13, rf11.l ; nop", // dst (1st read, low)
    "fadd rf14, rf14, rf11.h ; nop", // dst (1st read, high)
    "fadd rf15, rf15, rf12.l ; nop", // dst (2nd read, low)
    "fadd rf16, rf16, rf12.h ; nop", // dst (2nd read, high)

    "or rf17, 0x3f800000, 0x3f800000 ; nop", // rf17 = 1.0

    /* result = src + dst, per channel, clamped to 1.0. */
    "fadd rf7, rf7, rf13 ; nop",
    "fmin rf7, rf7, rf17 ; nop",

    "fadd rf8, rf8, rf14 ; nop",
    "fmin rf8, rf8, rf17 ; nop",

    "fadd rf9, rf9, rf15 ; nop",
    "fmin rf9, rf9, rf17 ; nop",

    "fadd rf10, rf10, rf16 ; nop",
    "fmin rf10, rf10, rf17 ; nop",

    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * Textured+smooth sibling of the two untextured SRC_ALPHA/ONE variants
 * above, the same relationship as this shader's additive sibling just
 * above it. The front section (texture fetch + per-vertex modulate + dst
 * read/unpack) is byte-for-byte identical to
 * g_fragment_shader_textured_smooth_blend_add_assembly -- only the combine
 * tail differs. rf7-10 hold texel*vertex-color per channel (in whatever
 * channel order this shader's own vfpack established, not necessarily
 * r,g,b,a -- paired by INDEX with rf13-16 exactly as that shader does, not
 * by colour name, per this family's register-pairing rule). src*alpha
 * products go into fresh rf24-rf27 (not rf19-22, already spent on the
 * vertex-color computation earlier in this shader) before any is consumed,
 * the same latency-safe shape as the untextured variants.
 */
static const char* g_fragment_shader_textured_smooth_blend_srcalpha_one_assembly[] = {
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

    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf20, r1, r5 ; nop",

    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf21, r1, r5 ; nop",

    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf22, r1, r5 ; nop",

    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf23, r1, r5 ; nop",

    "nop ; fmul rf7, rf4.l, rf20",
    "nop ; fmul rf8, rf4.h, rf21",
    "nop ; fmul rf9, rf3.l, rf22",
    "nop ; fmul rf10, rf3.h, rf23",

    "nop ; nop ; ldtlb.rf11", // rf11 = packed dst (r,g)
    "nop ; nop ; ldtlb.rf12", // rf12 = packed dst (b,a)

    "sub rf13, rf13, rf13 ; nop", "sub rf14, rf14, rf14 ; nop",
    "sub rf15, rf15, rf15 ; nop", "sub rf16, rf16, rf16 ; nop",
    "fadd rf13, rf13, rf11.l ; nop",
    "fadd rf14, rf14, rf11.h ; nop",
    "fadd rf15, rf15, rf12.l ; nop",
    "fadd rf16, rf16, rf12.h ; nop",

    "or rf17, 0x3f800000, 0x3f800000 ; nop", // rf17 = 1.0

    /* src*alpha, all 4 channels, into fresh registers (rf24-27, not
     * rf19-22 -- already spent on vertex-color computation above) before
     * any is consumed. */
    "nop ; fmul rf24, rf7, rf10",
    "nop ; fmul rf25, rf8, rf10",
    "nop ; fmul rf26, rf9, rf10",
    "nop ; fmul rf27, rf10, rf10",

    "fadd rf7, rf24, rf13 ; nop",
    "fmin rf7, rf7, rf17 ; nop",

    "fadd rf8, rf25, rf14 ; nop",
    "fmin rf8, rf8, rf17 ; nop",

    "fadd rf9, rf26, rf15 ; nop",
    "fmin rf9, rf9, rf17 ; nop",

    "fadd rf10, rf27, rf16 ; nop",
    "fmin rf10, rf10, rf17 ; nop",

    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/*
 * DIAGNOSTIC variant, no draw shape selects it: it is assembled and
 * uploaded like the rest, but draw.c's fragment dispatch never reaches it.
 * Identical to g_fragment_shader_assembly except for 8 extra harmless
 * "nop ; nop" instructions inserted at the same relative position
 * g_fragment_shader_textured_smooth_blend_assembly's 4 extra varying-read
 * triplets sit (right after the TMU-fetch thrsw pair, before the
 * unpack/modulate step) -- so total instruction count (30) and thrsw
 * timing match the smooth shader exactly, with zero varying reads. It
 * separates "instruction count and thrsw timing" from "the varying reads
 * themselves" when a smooth-shaded draw is made to run flat shader code.
 */
static const char* g_fragment_shader_padded_flat_test_assembly[] = {
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
    /* 8 harmless filler instructions -- same COUNT and POSITION as the
     * smooth+blend shader's 4 extra ldvary/fmul/fadd triplets (which
     * are 12 instructions, not 8 -- but the smooth+TEXTURED shader
     * without blend, g_fragment_shader_textured_smooth_assembly, saves
     * 4 elsewhere via its fmul-modulate replacing the flat shader's
     * separate sub+fadd unpack, netting +8 overall against this shader's
     * 22). Plain ALU no-ops, no signals, no register writes -- pure
     * timing filler. */
    "nop ; nop",
    "nop ; nop",
    "nop ; nop",
    "nop ; nop",
    "nop ; nop",
    "nop ; nop",
    "nop ; nop",
    "nop ; nop",
    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",
    "fadd rf8, rf8, rf4.h ; nop",
    "fadd rf9, rf9, rf3.l ; nop",
    "fadd rf10, rf10, rf3.h ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};







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
     * like s/t were -- these 4 registers are never touched below. */
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

    "ldvpmv_in rf11,  4 ; nop", // s (shifted: was offset 3)
    "ldvpmv_in rf12,  5 ; nop", // t (shifted: was offset 4)
    "ldvpmv_in rf14,  6 ; nop", // color r (shifted: was offset 5)
    "ldvpmv_in rf15,  7 ; nop", // color g (shifted: was offset 6)
    "ldvpmv_in rf16,  8 ; nop", // color b (shifted: was offset 7)
    "ldvpmv_in rf17,  9 ; nop", // color a (shifted: was offset 8)

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

    "ldvpmv_in rf11,  4 ; nop", // s0 (shifted: was offset 3)
    "ldvpmv_in rf12,  5 ; nop", // t0 (shifted: was offset 4)
    "ldvpmv_in rf14,  6 ; nop", // s1 (shifted: was offset 5)
    "ldvpmv_in rf15,  7 ; nop", // t1 (shifted: was offset 6)

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
    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",   // unit0 blue
    "fadd rf8, rf8, rf4.h ; nop",   // unit0 green
    "fadd rf9, rf9, rf3.l ; nop",   // unit0 red
    "fadd rf10, rf10, rf3.h ; nop", // unit0 alpha

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
    "nop ; fmul rf7, rf7, rf20",
    "nop ; fmul rf8, rf8, rf21",
    "nop ; fmul rf9, rf9, rf22",
    "nop ; fmul rf10, rf10, rf23",

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
 * Multitextured + SRC_ALPHA/INV_SRC_ALPHA translucency, GL_MODULATE
 * combine only -- see v3d_shader_assembler.h's own comment on
 * V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_MODULATE_TRANSLUCENT. A splice:
 * lines through the MODULATE combine below are BYTE-IDENTICAL to
 * g_fragment_shader_multitexture_modulate_blend_assembly's prefix (same
 * 2-TMU fetch, same channel unpack, same combine) up to the point where IT
 * has rf7-10 = combined blue/green/red/alpha; from there on the tail is
 * g_fragment_shader_textured_blend_assembly's, verbatim (glColor-alpha
 * uniform read, ldtlb dest-read, SRC_ALPHA/INV_SRC_ALPHA LERP,
 * writeback) -- that shader reaches the exact same rf7-10 shape via a
 * different (single-TMU) prefix, so its tail splices on unchanged, the
 * same reasoning as g_fragment_shader_textured_smooth_blend_assembly
 * above.
 */
static const char* g_fragment_shader_multitexture_modulate_translucent_assembly[] = {
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

    /* Combine: GL_MODULATE (same as g_fragment_shader_multitexture_assembly). */
    "nop ; fmul rf7, rf7, rf20",
    "nop ; fmul rf8, rf8, rf21",
    "nop ; fmul rf9, rf9, rf22",
    "nop ; fmul rf10, rf10, rf23",

    /* Blend tail from here on: g_fragment_shader_textured_blend_assembly's
     * own SRC_ALPHA/INV_SRC_ALPHA LERP, verbatim (see that shader for the
     * per-line rationale -- not re-derived here). */
    "nop ; nop ; ldunifrf.rf24", // rf24 = glColor alpha multiplier (uniform 0)
    "nop ; fmul rf10, rf10, rf24", // rf10 = final alpha = combined_tex_alpha * color_alpha

    "nop ; nop ; ldtlb.rf25", // dst (r,g)
    "nop ; nop ; ldtlb.rf26", // dst (b,a)

    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop", // dst_red
    "fadd rf28, rf28, rf25.h ; nop", // dst_green
    "fadd rf29, rf29, rf26.l ; nop", // dst_blue
    "fadd rf30, rf30, rf26.h ; nop", // dst_alpha

    "or rf24, 0x3f800000, 0x3f800000 ; nop", // reuse rf24 (color-alpha no longer needed) for 1.0
    "fsub rf24, rf24, rf10 ; nop", // rf24 = invAlpha = 1.0 - final_alpha

    /* Destination term paired by REGISTER INDEX, exactly as in
     * g_fragment_shader_textured_blend_assembly's own blend tail (which
     * this shader copies verbatim, see its comment above): same register
     * usage, rf7/rf9/rf27/rf29/rf24. */
    "nop ; fmul r0, rf7, rf10",   // tex_blue * alpha
    "nop ; fmul r1, rf27, rf24",  // dst_red * invAlpha
    "fadd rf7, r0, r1 ; nop",     // result_red -> rf7

    "nop ; fmul r0, rf8, rf10",   // tex_green * alpha
    "nop ; fmul r1, rf28, rf24",  // dst_green * invAlpha
    "fadd rf8, r0, r1 ; nop",     // result_green -> rf8

    "nop ; fmul r0, rf9, rf10",   // tex_red * alpha
    "nop ; fmul r1, rf29, rf24",  // dst_blue * invAlpha
    "fadd rf9, r0, r1 ; nop",     // result_blue -> rf9

    "nop ; fmul r0, rf10, rf10",  // alpha * alpha
    "nop ; fmul r1, rf30, rf24",  // dst_alpha * invAlpha
    "fadd rf10, r0, r1 ; nop",    // result_alpha -> rf10

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
static const char* g_fragment_shader_multitexture_modulate_blend_assembly[] = {
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

    /* Combine: GL_MODULATE (same as g_fragment_shader_multitexture_assembly). */
    "nop ; fmul rf7, rf7, rf20",
    "nop ; fmul rf8, rf8, rf21",
    "nop ; fmul rf9, rf9, rf22",
    "nop ; fmul rf10, rf10, rf23",

    /* Blend: read dst from the tile buffer, additive-blend against src (rf7-10). */
    "nop ; nop ; ldunifrf.rf24", // rf24 = blend dst-factor flag (0.0=SRC_COLOR, 1.0=SRC_ALPHA)

    "nop ; nop ; ldtlb.rf25", // rf25 = packed dst (r,g)
    "nop ; nop ; ldtlb.rf26", // rf26 = packed dst (b,a)

    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop", // dst_red
    "fadd rf28, rf28, rf25.h ; nop", // dst_green
    "fadd rf29, rf29, rf26.l ; nop", // dst_blue
    "fadd rf30, rf30, rf26.h ; nop", // dst_alpha

    /* result_blue = src_blue + dst_blue * lerp(src_blue, src_alpha, flag) */
    "fsub r0, rf10, rf7 ; nop",
    "nop ; fmul r0, r0, rf24",
    "fadd r0, rf7, r0 ; nop",
    "nop ; fmul r0, r0, rf29",
    "fadd rf7, rf7, r0 ; nop",

    "fsub r0, rf10, rf8 ; nop",
    "nop ; fmul r0, r0, rf24",
    "fadd r0, rf8, r0 ; nop",
    "nop ; fmul r0, r0, rf28",
    "fadd rf8, rf8, r0 ; nop",

    "fsub r0, rf10, rf9 ; nop",
    "nop ; fmul r0, r0, rf24",
    "fadd r0, rf9, r0 ; nop",
    "nop ; fmul r0, r0, rf27",
    "fadd rf9, rf9, r0 ; nop",

    /* result_alpha = src_alpha + dst_alpha * src_alpha -- computed LAST, consumes original rf10. */
    "nop ; fmul r0, rf30, rf10",
    "fadd rf10, rf10, r0 ; nop",

    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

static const char* g_fragment_shader_multitexture_decal_blend_assembly[] = {
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

    /* Combine: GL_DECAL (same as g_fragment_shader_multitexture_decal_assembly). */
    "fsub r0, rf20, rf7 ; nop",
    "nop ; fmul r0, r0, rf23",
    "fadd rf7, rf7, r0 ; nop",

    "fsub r0, rf21, rf8 ; nop",
    "nop ; fmul r0, r0, rf23",
    "fadd rf8, rf8, r0 ; nop",

    "fsub r0, rf22, rf9 ; nop",
    "nop ; fmul r0, r0, rf23",
    "fadd rf9, rf9, r0 ; nop",

    /* Blend -- identical shape to g_fragment_shader_multitexture_modulate_blend_assembly's own block. */
    "nop ; nop ; ldunifrf.rf24",

    "nop ; nop ; ldtlb.rf25",
    "nop ; nop ; ldtlb.rf26",

    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop",
    "fadd rf28, rf28, rf25.h ; nop",
    "fadd rf29, rf29, rf26.l ; nop",
    "fadd rf30, rf30, rf26.h ; nop",

    "fsub r0, rf10, rf7 ; nop",
    "nop ; fmul r0, r0, rf24",
    "fadd r0, rf7, r0 ; nop",
    "nop ; fmul r0, r0, rf29",
    "fadd rf7, rf7, r0 ; nop",

    "fsub r0, rf10, rf8 ; nop",
    "nop ; fmul r0, r0, rf24",
    "fadd r0, rf8, r0 ; nop",
    "nop ; fmul r0, r0, rf28",
    "fadd rf8, rf8, r0 ; nop",

    "fsub r0, rf10, rf9 ; nop",
    "nop ; fmul r0, r0, rf24",
    "fadd r0, rf9, r0 ; nop",
    "nop ; fmul r0, r0, rf27",
    "fadd rf9, rf9, r0 ; nop",

    "nop ; fmul r0, rf30, rf10",
    "fadd rf10, rf10, r0 ; nop",

    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

static const char* g_fragment_shader_multitexture_replace_blend_assembly[] = {
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

    /* Combine: GL_REPLACE (same as g_fragment_shader_multitexture_replace_assembly). */
    "or rf7, rf20, rf20 ; nop",
    "or rf8, rf21, rf21 ; nop",
    "or rf9, rf22, rf22 ; nop",
    "or rf10, rf23, rf23 ; nop",

    /* Blend -- identical shape to the other two blend variants' own block. */
    "nop ; nop ; ldunifrf.rf24",

    "nop ; nop ; ldtlb.rf25",
    "nop ; nop ; ldtlb.rf26",

    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop",
    "fadd rf28, rf28, rf25.h ; nop",
    "fadd rf29, rf29, rf26.l ; nop",
    "fadd rf30, rf30, rf26.h ; nop",

    "fsub r0, rf10, rf7 ; nop",
    "nop ; fmul r0, r0, rf24",
    "fadd r0, rf7, r0 ; nop",
    "nop ; fmul r0, r0, rf29",
    "fadd rf7, rf7, r0 ; nop",

    "fsub r0, rf10, rf8 ; nop",
    "nop ; fmul r0, r0, rf24",
    "fadd r0, rf8, r0 ; nop",
    "nop ; fmul r0, r0, rf28",
    "fadd rf8, rf8, r0 ; nop",

    "fsub r0, rf10, rf9 ; nop",
    "nop ; fmul r0, r0, rf24",
    "fadd r0, rf9, r0 ; nop",
    "nop ; fmul r0, r0, rf27",
    "fadd rf9, rf9, r0 ; nop",

    "nop ; fmul r0, rf30, rf10",
    "fadd rf10, rf10, r0 ; nop",

    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

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
    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

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

    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",  // rf7 = blue
    "fadd rf8, rf8, rf4.h ; nop",  // rf8 = green
    "fadd rf9, rf9, rf3.l ; nop",  // rf9 = red
    "fadd rf10, rf10, rf3.h ; nop", // rf10 = alpha

    /* COLOUR MODULATION -- multiplies the texel by glColor, as in the
     * non-fog family; see g_fragment_shader_textured_alphatest_assembly for
     * the full derivation. Without it a FLAT textured draw with fog would
     * discard glColor, RGB and alpha alike.
     *
     * Placed BEFORE the fog block for two independent reasons: draw.c writes
     * the colour words ahead of the fog words, so the read order must match;
     * and GL applies fog to the post-texture-environment colour, not to the
     * raw texel. Fog rewrites rf7-rf9 only, so the alpha modulated here
     * reaches the blend unit unchanged.
     *
     * The stream for this family is
     *   [TMU][TMU][r][g][b][a][fog A][fog B][fog C][fog D][fog M][fog r][fog g]
     *   [fog b]  (+ [alpha_ref][TLB cfg] for the alphatest variants)
     * rf5 is dead after the TMU requests above. */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf7, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf8, rf8, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf9, rf9, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf10, rf10, rf5",

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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
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

    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",  // rf7 = blue
    "fadd rf8, rf8, rf4.h ; nop",  // rf8 = green
    "fadd rf9, rf9, rf3.l ; nop",  // rf9 = red
    "fadd rf10, rf10, rf3.h ; nop", // rf10 = alpha

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
    "nop ; fmul rf7, rf7, rf5",
    "nop ; nop ; ldunifrf.rf5",   // colour word 1 -> rf8
    "nop ; fmul rf8, rf8, rf5",
    "nop ; nop ; ldunifrf.rf5",   // colour word 2 -> rf9
    "nop ; fmul rf9, rf9, rf5",
    "nop ; nop ; ldunifrf.rf5",   // colour word 3 -> rf10, the alpha the blend unit reads
    "nop ; fmul rf10, rf10, rf5",

    "nop ; nop ; ldunifrf.rf15", // rf15 = alpha test threshold
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

    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",  // rf7 = blue
    "fadd rf8, rf8, rf4.h ; nop",  // rf8 = green
    "fadd rf9, rf9, rf3.l ; nop",  // rf9 = red
    "fadd rf10, rf10, rf3.h ; nop", // rf10 = alpha

    /* colour modulation: see g_fragment_shader_textured_alphatest_assembly */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf7, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf8, rf8, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf9, rf9, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf10, rf10, rf5",

    "nop ; nop ; ldunifrf.rf15", // rf15 = alpha test threshold
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

    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",  // rf7 = blue
    "fadd rf8, rf8, rf4.h ; nop",  // rf8 = green
    "fadd rf9, rf9, rf3.l ; nop",  // rf9 = red
    "fadd rf10, rf10, rf3.h ; nop", // rf10 = alpha

    /* colour modulation: see g_fragment_shader_textured_alphatest_assembly */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf7, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf8, rf8, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf9, rf9, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf10, rf10, rf5",

    "nop ; nop ; ldunifrf.rf15", // rf15 = alpha test threshold
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

    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",  // rf7 = blue
    "fadd rf8, rf8, rf4.h ; nop",  // rf8 = green
    "fadd rf9, rf9, rf3.l ; nop",  // rf9 = red
    "fadd rf10, rf10, rf3.h ; nop", // rf10 = alpha

    /* colour modulation: see g_fragment_shader_textured_alphatest_assembly */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf7, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf8, rf8, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf9, rf9, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf10, rf10, rf5",

    "nop ; nop ; ldunifrf.rf15", // rf15 = alpha test threshold
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

    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",  // rf7 = blue
    "fadd rf8, rf8, rf4.h ; nop",  // rf8 = green
    "fadd rf9, rf9, rf3.l ; nop",  // rf9 = red
    "fadd rf10, rf10, rf3.h ; nop", // rf10 = alpha

    /* colour modulation: see g_fragment_shader_textured_alphatest_assembly */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf7, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf8, rf8, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf9, rf9, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf10, rf10, rf5",

    "nop ; nop ; ldunifrf.rf15", // rf15 = alpha test threshold
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

    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",  // rf7 = blue
    "fadd rf8, rf8, rf4.h ; nop",  // rf8 = green
    "fadd rf9, rf9, rf3.l ; nop",  // rf9 = red
    "fadd rf10, rf10, rf3.h ; nop", // rf10 = alpha

    /* colour modulation: see g_fragment_shader_textured_alphatest_assembly */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf7, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf8, rf8, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf9, rf9, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf10, rf10, rf5",

    "nop ; nop ; ldunifrf.rf15", // rf15 = alpha test threshold
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

    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",  // rf7 = blue
    "fadd rf8, rf8, rf4.h ; nop",  // rf8 = green
    "fadd rf9, rf9, rf3.l ; nop",  // rf9 = red
    "fadd rf10, rf10, rf3.h ; nop", // rf10 = alpha

    /* colour modulation: see g_fragment_shader_textured_alphatest_assembly.
     * Kept even here, where every fragment is discarded, because the uniform
     * stream layout must be identical across all seven variants of this family
     * -- draw.c writes these four words for the whole class. */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf7, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf8, rf8, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf9, rf9, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf10, rf10, rf5",

    "nop ; nop ; ldunifrf.rf15", // rf15 = alpha test threshold (deliberately unread)
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha

    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "nop ; nop ; ldunifrf.rf15", // rf15 = alpha test threshold
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha

    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "nop ; nop ; ldunifrf.rf15", // rf15 = alpha test threshold
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",   // load a/w
    "nop ; fmul r1, r0, rf0",  // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha

    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "nop ; nop ; ldunifrf.rf15", // rf15 = alpha test threshold
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",   // load a/w
    "nop ; fmul r1, r0, rf0",  // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha

    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "nop ; nop ; ldunifrf.rf15", // rf15 = alpha test threshold
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",   // load a/w
    "nop ; fmul r1, r0, rf0",  // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha

    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "nop ; nop ; ldunifrf.rf15", // rf15 = alpha test threshold
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",   // load a/w
    "nop ; fmul r1, r0, rf0",  // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha

    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "nop ; nop ; ldunifrf.rf15", // rf15 = alpha test threshold
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue

    "nop ; nop ; ldvary.r0",   // load a/w
    "nop ; fmul r1, r0, rf0",  // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha

    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "nop ; nop ; ldunifrf.rf15", // rf15 = alpha test threshold (deliberately unread)
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
    "fadd rf3, rf3, rf4 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf4",             // C
    "nop ; fmul rf4, rf4, rf0",
    "nop ; nop ; ldunifrf.rf5",             // D
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
    "fmin rf3, rf3, rf4 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf4",             // fog red
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop",
    "nop ; nop ; ldunifrf.rf4",             // fog green
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop",
    "nop ; nop ; ldunifrf.rf4",             // fog blue
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "nop ; nop ; ldunifrf.rf15",
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
    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",  // rf7 = blue
    "fadd rf8, rf8, rf4.h ; nop",  // rf8 = green
    "fadd rf9, rf9, rf3.l ; nop",  // rf9 = red
    "fadd rf10, rf10, rf3.h ; nop", // rf10 = alpha

    /* COLOUR MODULATION -- see
     * g_fragment_shader_textured_alphatest_assembly for the derivation and
     * g_fragment_shader_textured_fog_assembly for why it sits before the fog
     * block. Fog rewrites rf7-rf9 only, so the alpha modulated here survives to
     * the blend unit. rf5 is dead after the TMU requests above. */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf7, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf8, rf8, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf9, rf9, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf10, rf10, rf5",

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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "nop ; nop ; ldunifrf.rf15",
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
    "fadd rf3, rf3, rf4 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf4",             // C
    "nop ; fmul rf4, rf4, rf0",
    "nop ; nop ; ldunifrf.rf5",             // D
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
    "fmin rf3, rf3, rf4 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf4",             // fog red
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop",
    "nop ; nop ; ldunifrf.rf4",             // fog green
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop",
    "nop ; nop ; ldunifrf.rf4",             // fog blue
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "nop ; nop ; ldunifrf.rf15",
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
    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",  // rf7 = blue
    "fadd rf8, rf8, rf4.h ; nop",  // rf8 = green
    "fadd rf9, rf9, rf3.l ; nop",  // rf9 = red
    "fadd rf10, rf10, rf3.h ; nop", // rf10 = alpha

    /* COLOUR MODULATION -- see
     * g_fragment_shader_textured_alphatest_assembly for the derivation and
     * g_fragment_shader_textured_fog_assembly for why it sits before the fog
     * block. Fog rewrites rf7-rf9 only, so the alpha modulated here survives to
     * the blend unit. rf5 is dead after the TMU requests above. */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf7, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf8, rf8, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf9, rf9, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf10, rf10, rf5",

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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "nop ; nop ; ldunifrf.rf15",
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
    "fadd rf3, rf3, rf4 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf4",             // C
    "nop ; fmul rf4, rf4, rf0",
    "nop ; nop ; ldunifrf.rf5",             // D
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
    "fmin rf3, rf3, rf4 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf4",             // fog red
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop",
    "nop ; nop ; ldunifrf.rf4",             // fog green
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop",
    "nop ; nop ; ldunifrf.rf4",             // fog blue
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "nop ; nop ; ldunifrf.rf15",
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
    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",  // rf7 = blue
    "fadd rf8, rf8, rf4.h ; nop",  // rf8 = green
    "fadd rf9, rf9, rf3.l ; nop",  // rf9 = red
    "fadd rf10, rf10, rf3.h ; nop", // rf10 = alpha

    /* COLOUR MODULATION -- see
     * g_fragment_shader_textured_alphatest_assembly for the derivation and
     * g_fragment_shader_textured_fog_assembly for why it sits before the fog
     * block. Fog rewrites rf7-rf9 only, so the alpha modulated here survives to
     * the blend unit. rf5 is dead after the TMU requests above. */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf7, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf8, rf8, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf9, rf9, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf10, rf10, rf5",

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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "nop ; nop ; ldunifrf.rf15",
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
    "fadd rf3, rf3, rf4 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf4",             // C
    "nop ; fmul rf4, rf4, rf0",
    "nop ; nop ; ldunifrf.rf5",             // D
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
    "fmin rf3, rf3, rf4 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf4",             // fog red
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop",
    "nop ; nop ; ldunifrf.rf4",             // fog green
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop",
    "nop ; nop ; ldunifrf.rf4",             // fog blue
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "nop ; nop ; ldunifrf.rf15",
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
    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",  // rf7 = blue
    "fadd rf8, rf8, rf4.h ; nop",  // rf8 = green
    "fadd rf9, rf9, rf3.l ; nop",  // rf9 = red
    "fadd rf10, rf10, rf3.h ; nop", // rf10 = alpha

    /* COLOUR MODULATION -- see
     * g_fragment_shader_textured_alphatest_assembly for the derivation and
     * g_fragment_shader_textured_fog_assembly for why it sits before the fog
     * block. Fog rewrites rf7-rf9 only, so the alpha modulated here survives to
     * the blend unit. rf5 is dead after the TMU requests above. */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf7, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf8, rf8, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf9, rf9, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf10, rf10, rf5",

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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "nop ; nop ; ldunifrf.rf15",
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
    "fadd rf3, rf3, rf4 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf4",             // C
    "nop ; fmul rf4, rf4, rf0",
    "nop ; nop ; ldunifrf.rf5",             // D
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
    "fmin rf3, rf3, rf4 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf4",             // fog red
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop",
    "nop ; nop ; ldunifrf.rf4",             // fog green
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop",
    "nop ; nop ; ldunifrf.rf4",             // fog blue
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "nop ; nop ; ldunifrf.rf15",
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
    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",  // rf7 = blue
    "fadd rf8, rf8, rf4.h ; nop",  // rf8 = green
    "fadd rf9, rf9, rf3.l ; nop",  // rf9 = red
    "fadd rf10, rf10, rf3.h ; nop", // rf10 = alpha

    /* COLOUR MODULATION -- see
     * g_fragment_shader_textured_alphatest_assembly for the derivation and
     * g_fragment_shader_textured_fog_assembly for why it sits before the fog
     * block. Fog rewrites rf7-rf9 only, so the alpha modulated here survives to
     * the blend unit. rf5 is dead after the TMU requests above. */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf7, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf8, rf8, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf9, rf9, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf10, rf10, rf5",

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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "nop ; nop ; ldunifrf.rf15",
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
    "fadd rf3, rf3, rf4 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf4",             // C
    "nop ; fmul rf4, rf4, rf0",
    "nop ; nop ; ldunifrf.rf5",             // D
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
    "fmin rf3, rf3, rf4 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf4",             // fog red
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop",
    "nop ; nop ; ldunifrf.rf4",             // fog green
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop",
    "nop ; nop ; ldunifrf.rf4",             // fog blue
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "nop ; nop ; ldunifrf.rf15",
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
    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",  // rf7 = blue
    "fadd rf8, rf8, rf4.h ; nop",  // rf8 = green
    "fadd rf9, rf9, rf3.l ; nop",  // rf9 = red
    "fadd rf10, rf10, rf3.h ; nop", // rf10 = alpha

    /* COLOUR MODULATION -- see
     * g_fragment_shader_textured_alphatest_assembly for the derivation and
     * g_fragment_shader_textured_fog_assembly for why it sits before the fog
     * block. Fog rewrites rf7-rf9 only, so the alpha modulated here survives to
     * the blend unit. rf5 is dead after the TMU requests above. */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf7, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf8, rf8, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf9, rf9, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf10, rf10, rf5",

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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "nop ; nop ; ldunifrf.rf15",
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
    "fadd rf3, rf3, rf4 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf4",             // C
    "nop ; fmul rf4, rf4, rf0",
    "nop ; nop ; ldunifrf.rf5",             // D
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
    "fmin rf3, rf3, rf4 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf4",             // fog red
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop",
    "nop ; nop ; ldunifrf.rf4",             // fog green
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop",
    "nop ; nop ; ldunifrf.rf4",             // fog blue
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "nop ; nop ; ldunifrf.rf15",
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
    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",  // rf7 = blue
    "fadd rf8, rf8, rf4.h ; nop",  // rf8 = green
    "fadd rf9, rf9, rf3.l ; nop",  // rf9 = red
    "fadd rf10, rf10, rf3.h ; nop", // rf10 = alpha

    /* COLOUR MODULATION -- see
     * g_fragment_shader_textured_alphatest_assembly for the derivation and
     * g_fragment_shader_textured_fog_assembly for why it sits before the fog
     * block. Fog rewrites rf7-rf9 only, so the alpha modulated here survives to
     * the blend unit. rf5 is dead after the TMU requests above. */
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf7, rf7, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf8, rf8, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf9, rf9, rf5",
    "nop ; nop ; ldunifrf.rf5",
    "nop ; fmul rf10, rf10, rf5",

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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "nop ; nop ; ldunifrf.rf15",
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
    "fadd rf10, r1, r5 ; nop", // rf10 = true alpha
    /* draw.c writes 4 fixed-colour words for EVERY untextured draw,
     * including smooth ones that take their colour from varyings. They
     * are unused here, but ldunifrf is sequential: without consuming them
     * the fog loads below would read col[0..3] instead of the fog values.
     * rf24 is scratch -- nothing in this shader reads it. */
    "nop ; nop ; ldunifrf.rf24", // consume col[0], unused
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
    "fadd rf3, rf3, rf4 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf4",             // C
    "nop ; fmul rf4, rf4, rf0",
    "nop ; nop ; ldunifrf.rf5",             // D
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
    "fmin rf3, rf3, rf4 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf4",             // fog red
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop",
    "nop ; nop ; ldunifrf.rf4",             // fog green
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop",
    "nop ; nop ; ldunifrf.rf4",             // fog blue
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha
    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha
    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "nop ; nop ; ldunifrf.rf15",
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha
    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "nop ; nop ; ldunifrf.rf15",
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha
    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "nop ; nop ; ldunifrf.rf15",
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha
    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "nop ; nop ; ldunifrf.rf15",
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha
    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "nop ; nop ; ldunifrf.rf15",
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha
    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "nop ; nop ; ldunifrf.rf15",
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha
    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    /* The alpha threshold load sits BELOW the fog block: draw.c writes the
     * eight fog words before alpha_ref, so this load must follow them or the
     * whole uniform stream shifts and tlbu takes the wrong TLB config word. */
    "nop ; nop ; ldunifrf.rf15",
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
    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",   // unit0 blue
    "fadd rf8, rf8, rf4.h ; nop",   // unit0 green
    "fadd rf9, rf9, rf3.l ; nop",   // unit0 red
    "fadd rf10, rf10, rf3.h ; nop", // unit0 alpha
    "sub rf20, rf20, rf20 ; nop",
    "sub rf21, rf21, rf21 ; nop",
    "sub rf22, rf22, rf22 ; nop",
    "sub rf23, rf23, rf23 ; nop",
    "fadd rf20, rf20, rf19.l ; nop", // unit1 blue
    "fadd rf21, rf21, rf19.h ; nop", // unit1 green
    "fadd rf22, rf22, rf18.l ; nop", // unit1 red
    "fadd rf23, rf23, rf18.h ; nop", // unit1 alpha
    "nop ; fmul rf7, rf7, rf20",
    "nop ; fmul rf8, rf8, rf21",
    "nop ; fmul rf9, rf9, rf22",
    "nop ; fmul rf10, rf10, rf23",
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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
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
    "fadd rf9, rf9, r0 ; nop",
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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
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
    "or rf10, rf23, rf23 ; nop",
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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/* ==================================================================
 * SOFTWARE-BLEND FOG
 *
 * Fog for the blend families whose fog block can go straight in before
 * the first ldtlb: source colour final in rf7-rf9, no uniforms consumed
 * after that point, and no clash between the fog block's scratch and the
 * blend math.
 *
 * Like the software-blend shaders they extend, no draw reaches these:
 * they are assembled and uploaded, but the draw path never selects them.
 *
 * The eight remaining blend variants (untextured_blend*,
 * untextured_smooth_blend*, textured_smooth_blend_add and
 * textured_smooth_blend_srcalpha_one) are NOT here: their blend math uses
 * those scratch registers itself, so fog needs different ones there.
 * ================================================================== */

/*
 * textured_blend + fog.
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
static const char* g_fragment_shader_textured_blend_fog_assembly[] = {
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
    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",  // tex_blue
    "fadd rf8, rf8, rf4.h ; nop",  // tex_green
    "fadd rf9, rf9, rf3.l ; nop",  // tex_red
    "fadd rf10, rf10, rf3.h ; nop", // tex_alpha
    "nop ; nop ; ldunifrf.rf5", // rf5 = color blue multiplier (uniform 0)
    "nop ; fmul rf7, rf7, rf5", // rf7 = tex_blue * color_blue
    "nop ; nop ; ldunifrf.rf5", // rf5 = color green multiplier (uniform 1)
    "nop ; fmul rf8, rf8, rf5", // rf8 = tex_green * color_green
    "nop ; nop ; ldunifrf.rf5", // rf5 = color red multiplier (uniform 2)
    "nop ; fmul rf9, rf9, rf5", // rf9 = tex_red * color_red
    "nop ; nop ; ldunifrf.rf24", // rf24 = glColor alpha multiplier (uniform 3)
    "nop ; fmul rf10, rf10, rf24", // rf10 = final alpha = tex_alpha * color_alpha
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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    "nop ; nop ; ldtlb.rf25", // dst (r,g)
    "nop ; nop ; ldtlb.rf26", // dst (b,a)
    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop", // dst_red
    "fadd rf28, rf28, rf25.h ; nop", // dst_green
    "fadd rf29, rf29, rf26.l ; nop", // dst_blue
    "fadd rf30, rf30, rf26.h ; nop", // dst_alpha
    "or rf24, 0x3f800000, 0x3f800000 ; nop", // reuse rf24 (color-alpha no longer needed) for 1.0
    "fsub rf24, rf24, rf10 ; nop", // rf24 = invAlpha = 1.0 - final_alpha
    "nop ; fmul r0, rf7, rf10",   // tex_blue * alpha
    "nop ; fmul r1, rf27, rf24",  // dst_red * invAlpha
    "fadd rf7, r0, r1 ; nop",     // result_red -> rf7
    "nop ; fmul r0, rf8, rf10",   // tex_green * alpha
    "nop ; fmul r1, rf28, rf24",  // dst_green * invAlpha
    "fadd rf8, r0, r1 ; nop",     // result_green -> rf8
    "nop ; fmul r0, rf9, rf10",   // tex_red * alpha
    "nop ; fmul r1, rf29, rf24",  // dst_blue * invAlpha
    "fadd rf9, r0, r1 ; nop",     // result_blue -> rf9
    "nop ; fmul r0, rf10, rf10",  // alpha * alpha
    "nop ; fmul r1, rf30, rf24",  // dst_alpha * invAlpha
    "fadd rf10, r0, r1 ; nop",    // result_alpha -> rf10
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * textured_smooth_blend + fog.
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
static const char* g_fragment_shader_textured_smooth_blend_fog_assembly[] = {
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha
    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    "nop ; nop ; ldtlb.rf25", // dst (r,g)
    "nop ; nop ; ldtlb.rf26", // dst (b,a)
    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop", // dst_red
    "fadd rf28, rf28, rf25.h ; nop", // dst_green
    "fadd rf29, rf29, rf26.l ; nop", // dst_blue
    "fadd rf30, rf30, rf26.h ; nop", // dst_alpha
    "or rf24, 0x3f800000, 0x3f800000 ; nop", // rf24 = 1.0
    "fsub rf24, rf24, rf10 ; nop", // rf24 = invAlpha = 1.0 - final_alpha
    "nop ; fmul r0, rf7, rf10",   // true red * alpha
    "nop ; fmul r1, rf27, rf24",  // dst_red * invAlpha
    "fadd rf7, r0, r1 ; nop",     // result_red -> rf7
    "nop ; fmul r0, rf8, rf10",   // tex_green * alpha
    "nop ; fmul r1, rf28, rf24",  // dst_green * invAlpha
    "fadd rf8, r0, r1 ; nop",     // result_green -> rf8
    "nop ; fmul r0, rf9, rf10",   // true blue * alpha
    "nop ; fmul r1, rf29, rf24",  // dst_blue * invAlpha
    "fadd rf9, r0, r1 ; nop",     // result_blue -> rf9
    "nop ; fmul r0, rf10, rf10",  // alpha * alpha
    "nop ; fmul r1, rf30, rf24",  // dst_alpha * invAlpha
    "fadd rf10, r0, r1 ; nop",    // result_alpha -> rf10
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
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
static const char* g_fragment_shader_textured_smooth_dstcolor_zero_fog_assembly[] = {
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha
    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    "nop ; nop ; ldtlb.rf25", // dst (r,g)
    "nop ; nop ; ldtlb.rf26", // dst (b,a)
    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop", // dst_red
    "fadd rf28, rf28, rf25.h ; nop", // dst_green
    "fadd rf29, rf29, rf26.l ; nop", // dst_blue
    "fadd rf30, rf30, rf26.h ; nop", // dst_alpha
    "nop ; fmul rf7, rf7, rf27",   // true_red = red * dst_red
    "nop ; fmul rf8, rf8, rf28",   // green = green * dst_green
    "nop ; fmul rf9, rf9, rf29",   // true_blue = blue * dst_blue
    "nop ; fmul rf10, rf10, rf30", // alpha = alpha * dst_alpha
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
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
static const char* g_fragment_shader_textured_smooth_dstcolor_one_fog_assembly[] = {
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha
    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    "nop ; nop ; ldtlb.rf25", // dst (r,g)
    "nop ; nop ; ldtlb.rf26", // dst (b,a)
    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop", // dst_red
    "fadd rf28, rf28, rf25.h ; nop", // dst_green
    "fadd rf29, rf29, rf26.l ; nop", // dst_blue
    "fadd rf30, rf30, rf26.h ; nop", // dst_alpha
    "nop ; fmul r0, rf7, rf27",  // r0 = true_red * dst_red
    "fadd rf7, r0, rf27 ; nop",  // true_red result
    "nop ; fmul r0, rf8, rf28",  // green
    "fadd rf8, r0, rf28 ; nop",
    "nop ; fmul r0, rf9, rf29",  // true_blue
    "fadd rf9, r0, rf29 ; nop",
    "nop ; fmul r0, rf10, rf30", // alpha
    "fadd rf10, r0, rf30 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
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
static const char* g_fragment_shader_textured_smooth_dstcolor_srccolor_fog_assembly[] = {
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha
    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    "nop ; nop ; ldtlb.rf25", // dst (r,g)
    "nop ; nop ; ldtlb.rf26", // dst (b,a)
    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop", // dst_red
    "fadd rf28, rf28, rf25.h ; nop", // dst_green
    "fadd rf29, rf29, rf26.l ; nop", // dst_blue
    "fadd rf30, rf30, rf26.h ; nop", // dst_alpha
    "nop ; fmul r0, rf7, rf27",  // r0 = true_red * dst_red
    "fadd rf7, r0, r0 ; nop",    // true_red result = 2*r0
    "nop ; fmul r0, rf8, rf28",
    "fadd rf8, r0, r0 ; nop",
    "nop ; fmul r0, rf9, rf29",
    "fadd rf9, r0, r0 ; nop",
    "nop ; fmul r0, rf10, rf30",
    "fadd rf10, r0, r0 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
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
static const char* g_fragment_shader_textured_smooth_dstcolor_invdstalpha_fog_assembly[] = {
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha
    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    "nop ; nop ; ldtlb.rf25", // dst (r,g)
    "nop ; nop ; ldtlb.rf26", // dst (b,a)
    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop", // dst_red
    "fadd rf28, rf28, rf25.h ; nop", // dst_green
    "fadd rf29, rf29, rf26.l ; nop", // dst_blue
    "fadd rf30, rf30, rf26.h ; nop", // dst_alpha
    "or rf24, 0x3f800000, 0x3f800000 ; nop", // rf24 = 1.0
    "fsub rf24, rf24, rf30 ; nop", // rf24 = invDstAlpha = 1.0 - dst_alpha
    "nop ; fmul r0, rf7, rf27",   // r0 = true_red * dst_red
    "nop ; fmul r1, rf27, rf24",  // r1 = dst_red * invDstAlpha
    "fadd rf7, r0, r1 ; nop",     // true_red result
    "nop ; fmul r0, rf8, rf28",
    "nop ; fmul r1, rf28, rf24",
    "fadd rf8, r0, r1 ; nop",
    "nop ; fmul r0, rf9, rf29",
    "nop ; fmul r1, rf29, rf24",
    "fadd rf9, r0, r1 ; nop",
    "nop ; fmul r0, rf10, rf30",
    "nop ; fmul r1, rf30, rf24",
    "fadd rf10, r0, r1 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
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
static const char* g_fragment_shader_textured_smooth_zero_invsrccolor_fog_assembly[] = {
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha
    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    "nop ; nop ; ldtlb.rf25", // dst (r,g)
    "nop ; nop ; ldtlb.rf26", // dst (b,a)
    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop", // dst_red
    "fadd rf28, rf28, rf25.h ; nop", // dst_green
    "fadd rf29, rf29, rf26.l ; nop", // dst_blue
    "fadd rf30, rf30, rf26.h ; nop", // dst_alpha
    "or rf24, 0x3f800000, 0x3f800000 ; nop", // rf24 = 1.0
    "fsub r0, rf24, rf7 ; nop",   // r0 = 1 - true_red (src)
    "nop ; fmul rf7, rf27, r0",   // true_red result = dst_red * (1-src)
    "fsub r0, rf24, rf8 ; nop",   // r0 = 1 - green
    "nop ; fmul rf8, rf28, r0",   // green result = dst_green * (1-src)
    "fsub r0, rf24, rf9 ; nop",   // r0 = 1 - true_blue
    "nop ; fmul rf9, rf29, r0",   // true_blue result
    "fsub r0, rf24, rf10 ; nop",  // r0 = 1 - alpha
    "nop ; fmul rf10, rf30, r0",  // alpha result
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
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
static const char* g_fragment_shader_textured_smooth_dstcolor_srcalpha_fog_assembly[] = {
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha
    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    "nop ; nop ; ldtlb.rf25", // dst (r,g)
    "nop ; nop ; ldtlb.rf26", // dst (b,a)
    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop", // dst_red
    "fadd rf28, rf28, rf25.h ; nop", // dst_green
    "fadd rf29, rf29, rf26.l ; nop", // dst_blue
    "fadd rf30, rf30, rf26.h ; nop", // dst_alpha
    "fadd r0, rf7, rf10 ; nop",   // r0 = true_red + alpha
    "nop ; fmul rf7, rf27, r0",   // true_red result = dst_red * (red+alpha)
    "fadd r0, rf8, rf10 ; nop",   // r0 = green + alpha
    "nop ; fmul rf8, rf28, r0",   // green result
    "fadd r0, rf9, rf10 ; nop",   // r0 = true_blue + alpha
    "nop ; fmul rf9, rf29, r0",   // true_blue result
    "fadd r0, rf10, rf10 ; nop",  // r0 = alpha + alpha
    "nop ; fmul rf10, rf30, r0",  // alpha result
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
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
static const char* g_fragment_shader_textured_smooth_one_invsrcalpha_fog_assembly[] = {
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha
    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    "nop ; nop ; ldtlb.rf25", // dst (r,g)
    "nop ; nop ; ldtlb.rf26", // dst (b,a)
    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop", // dst_red
    "fadd rf28, rf28, rf25.h ; nop", // dst_green
    "fadd rf29, rf29, rf26.l ; nop", // dst_blue
    "fadd rf30, rf30, rf26.h ; nop", // dst_alpha
    "or rf24, 0x3f800000, 0x3f800000 ; nop", // rf24 = 1.0
    "fsub rf24, rf24, rf10 ; nop", // rf24 = invAlpha = 1 - alpha
    "nop ; fmul r0, rf27, rf24",   // r0 = dst_red * invAlpha
    "fadd rf7, rf7, r0 ; nop",     // true_red result = red + r0
    "nop ; fmul r0, rf28, rf24",
    "fadd rf8, rf8, r0 ; nop",
    "nop ; fmul r0, rf29, rf24",
    "fadd rf9, rf9, r0 ; nop",
    "nop ; fmul r0, rf30, rf24",
    "fadd rf10, rf10, r0 ; nop",   // last write to rf10 -- safe, rf24 already snapshotted it
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
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
static const char* g_fragment_shader_textured_smooth_invsrcalpha_srcalpha_fog_assembly[] = {
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha
    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    "nop ; nop ; ldtlb.rf25", // dst (r,g)
    "nop ; nop ; ldtlb.rf26", // dst (b,a)
    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop", // dst_red
    "fadd rf28, rf28, rf25.h ; nop", // dst_green
    "fadd rf29, rf29, rf26.l ; nop", // dst_blue
    "fadd rf30, rf30, rf26.h ; nop", // dst_alpha
    "or rf24, 0x3f800000, 0x3f800000 ; nop", // rf24 = 1.0
    "fsub rf24, rf24, rf10 ; nop", // rf24 = invAlpha = 1 - alpha
    "nop ; fmul r0, rf7, rf24",    // r0 = true_red * invAlpha
    "nop ; fmul r1, rf27, rf10",   // r1 = dst_red * alpha
    "fadd rf7, r0, r1 ; nop",      // true_red result
    "nop ; fmul r0, rf8, rf24",
    "nop ; fmul r1, rf28, rf10",
    "fadd rf8, r0, r1 ; nop",
    "nop ; fmul r0, rf9, rf24",
    "nop ; fmul r1, rf29, rf10",
    "fadd rf9, r0, r1 ; nop",
    "nop ; fmul r0, rf10, rf24",   // reads rf10 (still original alpha)
    "nop ; fmul r1, rf30, rf10",   // reads rf10 (still original alpha)
    "fadd rf10, r0, r1 ; nop",     // last write to rf10
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
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
static const char* g_fragment_shader_multitexture_modulate_translucent_fog_assembly[] = {
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
    "nop ; fmul rf7, rf7, rf20",
    "nop ; fmul rf8, rf8, rf21",
    "nop ; fmul rf9, rf9, rf22",
    "nop ; fmul rf10, rf10, rf23",
    "nop ; nop ; ldunifrf.rf24", // rf24 = glColor alpha multiplier (uniform 0)
    "nop ; fmul rf10, rf10, rf24", // rf10 = final alpha = combined_tex_alpha * color_alpha
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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    "nop ; nop ; ldtlb.rf25", // dst (r,g)
    "nop ; nop ; ldtlb.rf26", // dst (b,a)
    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop", // dst_red
    "fadd rf28, rf28, rf25.h ; nop", // dst_green
    "fadd rf29, rf29, rf26.l ; nop", // dst_blue
    "fadd rf30, rf30, rf26.h ; nop", // dst_alpha
    "or rf24, 0x3f800000, 0x3f800000 ; nop", // reuse rf24 (color-alpha no longer needed) for 1.0
    "fsub rf24, rf24, rf10 ; nop", // rf24 = invAlpha = 1.0 - final_alpha
    "nop ; fmul r0, rf7, rf10",   // tex_blue * alpha
    "nop ; fmul r1, rf27, rf24",  // dst_red * invAlpha
    "fadd rf7, r0, r1 ; nop",     // result_red -> rf7
    "nop ; fmul r0, rf8, rf10",   // tex_green * alpha
    "nop ; fmul r1, rf28, rf24",  // dst_green * invAlpha
    "fadd rf8, r0, r1 ; nop",     // result_green -> rf8
    "nop ; fmul r0, rf9, rf10",   // tex_red * alpha
    "nop ; fmul r1, rf29, rf24",  // dst_blue * invAlpha
    "fadd rf9, r0, r1 ; nop",     // result_blue -> rf9
    "nop ; fmul r0, rf10, rf10",  // alpha * alpha
    "nop ; fmul r1, rf30, rf24",  // dst_alpha * invAlpha
    "fadd rf10, r0, r1 ; nop",    // result_alpha -> rf10
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
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
static const char* g_fragment_shader_multitexture_modulate_blend_fog_assembly[] = {
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
    "nop ; fmul rf7, rf7, rf20",
    "nop ; fmul rf8, rf8, rf21",
    "nop ; fmul rf9, rf9, rf22",
    "nop ; fmul rf10, rf10, rf23",
    "nop ; nop ; ldunifrf.rf24", // rf24 = blend dst-factor flag (0.0=SRC_COLOR, 1.0=SRC_ALPHA)
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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    "nop ; nop ; ldtlb.rf25", // rf25 = packed dst (r,g)
    "nop ; nop ; ldtlb.rf26", // rf26 = packed dst (b,a)
    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop", // dst_red
    "fadd rf28, rf28, rf25.h ; nop", // dst_green
    "fadd rf29, rf29, rf26.l ; nop", // dst_blue
    "fadd rf30, rf30, rf26.h ; nop", // dst_alpha
    "fsub r0, rf10, rf7 ; nop",
    "nop ; fmul r0, r0, rf24",
    "fadd r0, rf7, r0 ; nop",
    "nop ; fmul r0, r0, rf29",
    "fadd rf7, rf7, r0 ; nop",
    "fsub r0, rf10, rf8 ; nop",
    "nop ; fmul r0, r0, rf24",
    "fadd r0, rf8, r0 ; nop",
    "nop ; fmul r0, r0, rf28",
    "fadd rf8, rf8, r0 ; nop",
    "fsub r0, rf10, rf9 ; nop",
    "nop ; fmul r0, r0, rf24",
    "fadd r0, rf9, r0 ; nop",
    "nop ; fmul r0, r0, rf27",
    "fadd rf9, rf9, r0 ; nop",
    "nop ; fmul r0, rf30, rf10",
    "fadd rf10, rf10, r0 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
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
static const char* g_fragment_shader_multitexture_decal_blend_fog_assembly[] = {
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
    "fsub r0, rf20, rf7 ; nop",
    "nop ; fmul r0, r0, rf23",
    "fadd rf7, rf7, r0 ; nop",
    "fsub r0, rf21, rf8 ; nop",
    "nop ; fmul r0, r0, rf23",
    "fadd rf8, rf8, r0 ; nop",
    "fsub r0, rf22, rf9 ; nop",
    "nop ; fmul r0, r0, rf23",
    "fadd rf9, rf9, r0 ; nop",
    "nop ; nop ; ldunifrf.rf24",
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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    "nop ; nop ; ldtlb.rf25",
    "nop ; nop ; ldtlb.rf26",
    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop",
    "fadd rf28, rf28, rf25.h ; nop",
    "fadd rf29, rf29, rf26.l ; nop",
    "fadd rf30, rf30, rf26.h ; nop",
    "fsub r0, rf10, rf7 ; nop",
    "nop ; fmul r0, r0, rf24",
    "fadd r0, rf7, r0 ; nop",
    "nop ; fmul r0, r0, rf29",
    "fadd rf7, rf7, r0 ; nop",
    "fsub r0, rf10, rf8 ; nop",
    "nop ; fmul r0, r0, rf24",
    "fadd r0, rf8, r0 ; nop",
    "nop ; fmul r0, r0, rf28",
    "fadd rf8, rf8, r0 ; nop",
    "fsub r0, rf10, rf9 ; nop",
    "nop ; fmul r0, r0, rf24",
    "fadd r0, rf9, r0 ; nop",
    "nop ; fmul r0, r0, rf27",
    "fadd rf9, rf9, r0 ; nop",
    "nop ; fmul r0, rf30, rf10",
    "fadd rf10, rf10, r0 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
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
static const char* g_fragment_shader_multitexture_replace_blend_fog_assembly[] = {
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
    "or rf10, rf23, rf23 ; nop",
    "nop ; nop ; ldunifrf.rf24",
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
    "nop ; nop ; ldunifrf.rf11",             // A
    "nop ; nop ; ldunifrf.rf12",             // B
    "nop ; fmul rf12, rf12, rf0",
    "fadd rf11, rf11, rf12 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf12",             // C
    "nop ; fmul rf12, rf12, rf0",
    "nop ; nop ; ldunifrf.rf13",             // D
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
    "fmin rf11, rf11, rf12 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf12",             // fog red
    "fsub rf7, rf7, rf12 ; nop",
    "nop ; fmul rf7, rf7, rf11",
    "fadd rf7, rf7, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog green
    "fsub rf8, rf8, rf12 ; nop",
    "nop ; fmul rf8, rf8, rf11",
    "fadd rf8, rf8, rf12 ; nop",
    "nop ; nop ; ldunifrf.rf12",             // fog blue
    "fsub rf9, rf9, rf12 ; nop",
    "nop ; fmul rf9, rf9, rf11",
    "fadd rf9, rf9, rf12 ; nop",
    "nop ; nop ; ldtlb.rf25",
    "nop ; nop ; ldtlb.rf26",
    "sub rf27, rf27, rf27 ; nop",
    "sub rf28, rf28, rf28 ; nop",
    "sub rf29, rf29, rf29 ; nop",
    "sub rf30, rf30, rf30 ; nop",
    "fadd rf27, rf27, rf25.l ; nop",
    "fadd rf28, rf28, rf25.h ; nop",
    "fadd rf29, rf29, rf26.l ; nop",
    "fadd rf30, rf30, rf26.h ; nop",
    "fsub r0, rf10, rf7 ; nop",
    "nop ; fmul r0, r0, rf24",
    "fadd r0, rf7, r0 ; nop",
    "nop ; fmul r0, r0, rf29",
    "fadd rf7, rf7, r0 ; nop",
    "fsub r0, rf10, rf8 ; nop",
    "nop ; fmul r0, r0, rf24",
    "fadd r0, rf8, r0 ; nop",
    "nop ; fmul r0, r0, rf28",
    "fadd rf8, rf8, r0 ; nop",
    "fsub r0, rf10, rf9 ; nop",
    "nop ; fmul r0, r0, rf24",
    "fadd r0, rf9, r0 ; nop",
    "nop ; fmul r0, r0, rf27",
    "fadd rf9, rf9, r0 ; nop",
    "nop ; fmul r0, rf30, rf10",
    "fadd rf10, rf10, r0 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

/* ==================================================================
 * SOFTWARE-BLEND FOG, REGISTER-CONSTRAINED GROUP
 *
 * These five blend shaders use the shared fog block's rf11/rf12/rf13 for
 * their own blend math, so that block cannot be dropped in unchanged.
 * Each gets a fog block on registers allocated from its own unused set
 * instead.
 * ================================================================== */

/*
 * untextured_smooth_blend + fog.
 *
 * Fog before the first ldtlb, so it reaches the SOURCE colour and not the
 * blended result -- same rule as the other software-blend fog variants.
 *
 * This shader's blend math occupies the rf11/rf12/rf13 the shared fog
 * block uses. Fog runs on rf4/rf5/rf6 instead, picked from registers
 * this shader never touches. rf0/rf1/rf2 are not available for it: the
 * fragment payload map gives them to payload_w, payload_w_centroid and
 * payload_z.
 */
static const char* g_fragment_shader_untextured_smooth_blend_fog_assembly[] = {
    "nop ; nop ; ldvary.r0",  // load r/w
    "nop ; fmul r1, r0, rf0", // r1 = (r/w) * w
    "fadd rf7, r1, r5 ; nop", // rf7 = true red
    "nop ; nop ; ldvary.r0",  // load g/w
    "nop ; fmul r1, r0, rf0", // r1 = (g/w) * w
    "fadd rf8, r1, r5 ; nop", // rf8 = true green
    "nop ; nop ; ldvary.r0",  // load b/w
    "nop ; fmul r1, r0, rf0", // r1 = (b/w) * w
    "fadd rf9, r1, r5 ; nop", // rf9 = true blue
    "nop ; nop ; ldvary.r0",  // load a/w
    "nop ; fmul r1, r0, rf0", // r1 = (a/w) * w
    "fadd rf10, r1, r5 ; nop", // rf10 = true alpha
    /* Fog on rf4/rf5/rf6 -- allocated from THIS shader's own unused
     * registers. The usual rf11/rf12/rf13 fog block cannot be used here:
     * this shader's blend math occupies those. */
    /* CONSUME the four fixed-colour words draw.c writes for EVERY untextured
     * draw. This shader takes its colour from varyings, but ldunifrf is
     * sequential: without these the fog loads below would read col[0..3]
     * instead of the fog values. rf3 is scratch -- nothing reads it. */
    "nop ; nop ; ldunifrf.rf3", // consume col[0], unused
    "nop ; nop ; ldunifrf.rf3", // consume col[1], unused
    "nop ; nop ; ldunifrf.rf3", // consume col[2], unused
    "nop ; nop ; ldunifrf.rf3", // consume col[3], unused
    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf4/rf5/rf6 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf4",             // A
    "nop ; nop ; ldunifrf.rf5",             // B
    "nop ; fmul rf5, rf5, rf0",
    "fadd rf4, rf4, rf5 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf5",             // C
    "nop ; fmul rf5, rf5, rf0",
    "nop ; nop ; ldunifrf.rf6",             // D
    "nop ; fmul rf6, rf6, rf0",
    "nop ; fmul rf6, rf6, rf0",
    "fadd rf5, rf5, rf6 ; nop",             // C*c + D*c*c
    "or exp, rf5, rf5 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf5",             // M
    "nop ; fmul rf4, rf4, rf5",
    "or rf6, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf6, rf6, rf5 ; nop",             // 1-M
    "nop ; fmul rf6, rf6, r4",
    "fadd rf4, rf4, rf6 ; nop",             // fog factor
    "sub rf5, rf5, rf5 ; nop",
    "fmax rf4, rf4, rf5 ; nop",
    "or rf5, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf4, rf4, rf5 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf5",             // fog red
    "fsub rf7, rf7, rf5 ; nop",
    "nop ; fmul rf7, rf7, rf4",
    "fadd rf7, rf7, rf5 ; nop",
    "nop ; nop ; ldunifrf.rf5",             // fog green
    "fsub rf8, rf8, rf5 ; nop",
    "nop ; fmul rf8, rf8, rf4",
    "fadd rf8, rf8, rf5 ; nop",
    "nop ; nop ; ldunifrf.rf5",             // fog blue
    "fsub rf9, rf9, rf5 ; nop",
    "nop ; fmul rf9, rf9, rf4",
    "fadd rf9, rf9, rf5 ; nop",

    /* SCOREBOARD LOCK. A TLB read must not happen before the scoreboard
     * lock is taken -- MESA nir_to_vir.c vir_emit_tlb_color_read: "We need
     * to emit our TLB reads after we have acquired the scoreboard lock, or
     * the GPU will hang." The lock is taken on a thread switch, so one is
     * emitted here, and MESA's scheduler only counts the scoreboard as
     * locked once tick - last_thrsw_tick >= 3, hence two filler slots.
     * Which thread switch takes the lock is chosen by
     * do_scoreboard_wait_on_first_thread_switch in the shader record
     * (draw.c). */
    "nop ; nop ; thrsw",
    "nop ; nop",
    "nop ; nop",

    "nop ; nop ; ldtlb.rf11", // rf11 = packed dst (r,g)
    "nop ; nop ; ldtlb.rf12", // rf12 = packed dst (b,a)
    "sub rf13, rf13, rf13 ; nop", "sub rf14, rf14, rf14 ; nop",
    "sub rf15, rf15, rf15 ; nop", "sub rf16, rf16, rf16 ; nop",
    "fadd rf13, rf13, rf11.l ; nop", // dst_r
    "fadd rf14, rf14, rf11.h ; nop", // dst_g
    "fadd rf15, rf15, rf12.l ; nop", // dst_b
    "fadd rf16, rf16, rf12.h ; nop", // dst_a
    "or rf17, 0x3f800000, 0x3f800000 ; nop", // rf17 = 1.0
    "fsub rf18, rf17, rf10 ; nop",           // rf18 = invAlpha = 1.0 - alpha
    "nop ; fmul rf19, rf7, rf10",
    "nop ; fmul rf20, rf13, rf18",
    "fadd rf7, rf19, rf20 ; nop", // result_r
    "nop ; fmul rf19, rf8, rf10",
    "nop ; fmul rf20, rf14, rf18",
    "fadd rf8, rf19, rf20 ; nop", // result_g
    "nop ; fmul rf19, rf9, rf10",
    "nop ; fmul rf20, rf15, rf18",
    "fadd rf9, rf19, rf20 ; nop", // result_b
    "nop ; fmul rf19, rf10, rf10",
    "nop ; fmul rf20, rf16, rf18",
    "fadd rf10, rf19, rf20 ; nop", // result_a
    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * untextured_smooth_blend_add + fog.
 *
 * Fog before the first ldtlb, so it reaches the SOURCE colour and not the
 * blended result -- same rule as the other software-blend fog variants.
 *
 * This shader's blend math occupies the rf11/rf12/rf13 the shared fog
 * block uses. Fog runs on rf4/rf5/rf6 instead, picked from registers
 * this shader never touches. rf0/rf1/rf2 are not available for it: the
 * fragment payload map gives them to payload_w, payload_w_centroid and
 * payload_z.
 */
static const char* g_fragment_shader_untextured_smooth_blend_add_fog_assembly[] = {
    "nop ; nop ; ldvary.r0",  // load r/w
    "nop ; fmul r1, r0, rf0", // r1 = (r/w) * w
    "fadd rf7, r1, r5 ; nop", // rf7 = true red
    "nop ; nop ; ldvary.r0",  // load g/w
    "nop ; fmul r1, r0, rf0", // r1 = (g/w) * w
    "fadd rf8, r1, r5 ; nop", // rf8 = true green
    "nop ; nop ; ldvary.r0",  // load b/w
    "nop ; fmul r1, r0, rf0", // r1 = (b/w) * w
    "fadd rf9, r1, r5 ; nop", // rf9 = true blue
    "nop ; nop ; ldvary.r0",  // load a/w
    "nop ; fmul r1, r0, rf0", // r1 = (a/w) * w
    "fadd rf10, r1, r5 ; nop", // rf10 = true alpha
    /* Fog on rf4/rf5/rf6 -- allocated from THIS shader's own unused
     * registers. The usual rf11/rf12/rf13 fog block cannot be used here:
     * this shader's blend math occupies those. */
    /* CONSUME the four fixed-colour words draw.c writes for EVERY untextured
     * draw. This shader takes its colour from varyings, but ldunifrf is
     * sequential: without these the fog loads below would read col[0..3]
     * instead of the fog values. rf3 is scratch -- nothing reads it. */
    "nop ; nop ; ldunifrf.rf3", // consume col[0], unused
    "nop ; nop ; ldunifrf.rf3", // consume col[1], unused
    "nop ; nop ; ldunifrf.rf3", // consume col[2], unused
    "nop ; nop ; ldunifrf.rf3", // consume col[3], unused
    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf4/rf5/rf6 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf4",             // A
    "nop ; nop ; ldunifrf.rf5",             // B
    "nop ; fmul rf5, rf5, rf0",
    "fadd rf4, rf4, rf5 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf5",             // C
    "nop ; fmul rf5, rf5, rf0",
    "nop ; nop ; ldunifrf.rf6",             // D
    "nop ; fmul rf6, rf6, rf0",
    "nop ; fmul rf6, rf6, rf0",
    "fadd rf5, rf5, rf6 ; nop",             // C*c + D*c*c
    "or exp, rf5, rf5 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf5",             // M
    "nop ; fmul rf4, rf4, rf5",
    "or rf6, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf6, rf6, rf5 ; nop",             // 1-M
    "nop ; fmul rf6, rf6, r4",
    "fadd rf4, rf4, rf6 ; nop",             // fog factor
    "sub rf5, rf5, rf5 ; nop",
    "fmax rf4, rf4, rf5 ; nop",
    "or rf5, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf4, rf4, rf5 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf5",             // fog red
    "fsub rf7, rf7, rf5 ; nop",
    "nop ; fmul rf7, rf7, rf4",
    "fadd rf7, rf7, rf5 ; nop",
    "nop ; nop ; ldunifrf.rf5",             // fog green
    "fsub rf8, rf8, rf5 ; nop",
    "nop ; fmul rf8, rf8, rf4",
    "fadd rf8, rf8, rf5 ; nop",
    "nop ; nop ; ldunifrf.rf5",             // fog blue
    "fsub rf9, rf9, rf5 ; nop",
    "nop ; fmul rf9, rf9, rf4",
    "fadd rf9, rf9, rf5 ; nop",

    /* SCOREBOARD LOCK. A TLB read must not happen before the scoreboard
     * lock is taken -- MESA nir_to_vir.c vir_emit_tlb_color_read: "We need
     * to emit our TLB reads after we have acquired the scoreboard lock, or
     * the GPU will hang." The lock is taken on a thread switch, so one is
     * emitted here, and MESA's scheduler only counts the scoreboard as
     * locked once tick - last_thrsw_tick >= 3, hence two filler slots.
     * Which thread switch takes the lock is chosen by
     * do_scoreboard_wait_on_first_thread_switch in the shader record
     * (draw.c). */
    "nop ; nop ; thrsw",
    "nop ; nop",
    "nop ; nop",

    "nop ; nop ; ldtlb.rf11", // rf11 = packed dst (r,g)
    "nop ; nop ; ldtlb.rf12", // rf12 = packed dst (b,a)
    "sub rf13, rf13, rf13 ; nop", "sub rf14, rf14, rf14 ; nop",
    "sub rf15, rf15, rf15 ; nop", "sub rf16, rf16, rf16 ; nop",
    "fadd rf13, rf13, rf11.l ; nop", // dst_r
    "fadd rf14, rf14, rf11.h ; nop", // dst_g
    "fadd rf15, rf15, rf12.l ; nop", // dst_b
    "fadd rf16, rf16, rf12.h ; nop", // dst_a
    "or rf17, 0x3f800000, 0x3f800000 ; nop", // rf17 = 1.0
    "fadd rf7, rf7, rf13 ; nop",
    "fmin rf7, rf7, rf17 ; nop", // result_r
    "fadd rf8, rf8, rf14 ; nop",
    "fmin rf8, rf8, rf17 ; nop", // result_g
    "fadd rf9, rf9, rf15 ; nop",
    "fmin rf9, rf9, rf17 ; nop", // result_b
    "fadd rf10, rf10, rf16 ; nop",
    "fmin rf10, rf10, rf17 ; nop", // result_a
    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * untextured_smooth_blend_srcalpha_one + fog.
 *
 * Fog before the first ldtlb, so it reaches the SOURCE colour and not the
 * blended result -- same rule as the other software-blend fog variants.
 *
 * This shader's blend math occupies the rf11/rf12/rf13 the shared fog
 * block uses. Fog runs on rf4/rf5/rf6 instead, picked from registers
 * this shader never touches. rf0/rf1/rf2 are not available for it: the
 * fragment payload map gives them to payload_w, payload_w_centroid and
 * payload_z.
 */
static const char* g_fragment_shader_untextured_smooth_blend_srcalpha_one_fog_assembly[] = {
    "nop ; nop ; ldvary.r0",  // load r/w
    "nop ; fmul r1, r0, rf0", // r1 = (r/w) * w
    "fadd rf7, r1, r5 ; nop", // rf7 = true red
    "nop ; nop ; ldvary.r0",  // load g/w
    "nop ; fmul r1, r0, rf0", // r1 = (g/w) * w
    "fadd rf8, r1, r5 ; nop", // rf8 = true green
    "nop ; nop ; ldvary.r0",  // load b/w
    "nop ; fmul r1, r0, rf0", // r1 = (b/w) * w
    "fadd rf9, r1, r5 ; nop", // rf9 = true blue
    "nop ; nop ; ldvary.r0",  // load a/w
    "nop ; fmul r1, r0, rf0", // r1 = (a/w) * w
    "fadd rf10, r1, r5 ; nop", // rf10 = true alpha
    /* Fog on rf4/rf5/rf6 -- allocated from THIS shader's own unused
     * registers. The usual rf11/rf12/rf13 fog block cannot be used here:
     * this shader's blend math occupies those. */
    /* CONSUME the four fixed-colour words draw.c writes for EVERY untextured
     * draw. This shader takes its colour from varyings, but ldunifrf is
     * sequential: without these the fog loads below would read col[0..3]
     * instead of the fog values. rf3 is scratch -- nothing reads it. */
    "nop ; nop ; ldunifrf.rf3", // consume col[0], unused
    "nop ; nop ; ldunifrf.rf3", // consume col[1], unused
    "nop ; nop ; ldunifrf.rf3", // consume col[2], unused
    "nop ; nop ; ldunifrf.rf3", // consume col[3], unused
    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf4/rf5/rf6 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf4",             // A
    "nop ; nop ; ldunifrf.rf5",             // B
    "nop ; fmul rf5, rf5, rf0",
    "fadd rf4, rf4, rf5 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf5",             // C
    "nop ; fmul rf5, rf5, rf0",
    "nop ; nop ; ldunifrf.rf6",             // D
    "nop ; fmul rf6, rf6, rf0",
    "nop ; fmul rf6, rf6, rf0",
    "fadd rf5, rf5, rf6 ; nop",             // C*c + D*c*c
    "or exp, rf5, rf5 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf5",             // M
    "nop ; fmul rf4, rf4, rf5",
    "or rf6, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf6, rf6, rf5 ; nop",             // 1-M
    "nop ; fmul rf6, rf6, r4",
    "fadd rf4, rf4, rf6 ; nop",             // fog factor
    "sub rf5, rf5, rf5 ; nop",
    "fmax rf4, rf4, rf5 ; nop",
    "or rf5, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf4, rf4, rf5 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf5",             // fog red
    "fsub rf7, rf7, rf5 ; nop",
    "nop ; fmul rf7, rf7, rf4",
    "fadd rf7, rf7, rf5 ; nop",
    "nop ; nop ; ldunifrf.rf5",             // fog green
    "fsub rf8, rf8, rf5 ; nop",
    "nop ; fmul rf8, rf8, rf4",
    "fadd rf8, rf8, rf5 ; nop",
    "nop ; nop ; ldunifrf.rf5",             // fog blue
    "fsub rf9, rf9, rf5 ; nop",
    "nop ; fmul rf9, rf9, rf4",
    "fadd rf9, rf9, rf5 ; nop",

    /* SCOREBOARD LOCK. A TLB read must not happen before the scoreboard
     * lock is taken -- MESA nir_to_vir.c vir_emit_tlb_color_read: "We need
     * to emit our TLB reads after we have acquired the scoreboard lock, or
     * the GPU will hang." The lock is taken on a thread switch, so one is
     * emitted here, and MESA's scheduler only counts the scoreboard as
     * locked once tick - last_thrsw_tick >= 3, hence two filler slots.
     * Which thread switch takes the lock is chosen by
     * do_scoreboard_wait_on_first_thread_switch in the shader record
     * (draw.c). */
    "nop ; nop ; thrsw",
    "nop ; nop",
    "nop ; nop",

    "nop ; nop ; ldtlb.rf11", // rf11 = packed dst (r,g)
    "nop ; nop ; ldtlb.rf12", // rf12 = packed dst (b,a)
    "sub rf13, rf13, rf13 ; nop", "sub rf14, rf14, rf14 ; nop",
    "sub rf15, rf15, rf15 ; nop", "sub rf16, rf16, rf16 ; nop",
    "fadd rf13, rf13, rf11.l ; nop", // dst_r
    "fadd rf14, rf14, rf11.h ; nop", // dst_g
    "fadd rf15, rf15, rf12.l ; nop", // dst_b
    "fadd rf16, rf16, rf12.h ; nop", // dst_a
    "or rf17, 0x3f800000, 0x3f800000 ; nop", // rf17 = 1.0
    "nop ; fmul rf19, rf7, rf10",
    "nop ; fmul rf20, rf8, rf10",
    "nop ; fmul rf21, rf9, rf10",
    "nop ; fmul rf22, rf10, rf10",
    "fadd rf7, rf19, rf13 ; nop",
    "fmin rf7, rf7, rf17 ; nop", // result_r
    "fadd rf8, rf20, rf14 ; nop",
    "fmin rf8, rf8, rf17 ; nop", // result_g
    "fadd rf9, rf21, rf15 ; nop",
    "fmin rf9, rf9, rf17 ; nop", // result_b
    "fadd rf10, rf22, rf16 ; nop",
    "fmin rf10, rf10, rf17 ; nop", // result_a
    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * textured_smooth_blend_add + fog.
 *
 * Fog before the first ldtlb, so it reaches the SOURCE colour and not the
 * blended result -- same rule as the other software-blend fog variants.
 *
 * This shader's blend math occupies the rf11/rf12/rf13 the shared fog
 * block uses. Fog runs on rf18/rf19/rf24 instead, picked from registers
 * this shader never touches.
 */
static const char* g_fragment_shader_textured_smooth_blend_add_fog_assembly[] = {
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
    "fadd rf22, r1, r5 ; nop", // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop", // rf23 = true vertex alpha
    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)
    /* Fog on rf18/rf19/rf24 -- allocated from THIS shader's own unused
     * registers. The usual rf11/rf12/rf13 fog block cannot be used here:
     * this shader's blend math occupies those. */
    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf18/rf19/rf24 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf18",             // A
    "nop ; nop ; ldunifrf.rf19",             // B
    "nop ; fmul rf19, rf19, rf0",
    "fadd rf18, rf18, rf19 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf19",             // C
    "nop ; fmul rf19, rf19, rf0",
    "nop ; nop ; ldunifrf.rf24",             // D
    "nop ; fmul rf24, rf24, rf0",
    "nop ; fmul rf24, rf24, rf0",
    "fadd rf19, rf19, rf24 ; nop",             // C*c + D*c*c
    "or exp, rf19, rf19 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf19",             // M
    "nop ; fmul rf18, rf18, rf19",
    "or rf24, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf24, rf24, rf19 ; nop",             // 1-M
    "nop ; fmul rf24, rf24, r4",
    "fadd rf18, rf18, rf24 ; nop",             // fog factor
    "sub rf19, rf19, rf19 ; nop",
    "fmax rf18, rf18, rf19 ; nop",
    "or rf19, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf18, rf18, rf19 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf19",             // fog red
    "fsub rf7, rf7, rf19 ; nop",
    "nop ; fmul rf7, rf7, rf18",
    "fadd rf7, rf7, rf19 ; nop",
    "nop ; nop ; ldunifrf.rf19",             // fog green
    "fsub rf8, rf8, rf19 ; nop",
    "nop ; fmul rf8, rf8, rf18",
    "fadd rf8, rf8, rf19 ; nop",
    "nop ; nop ; ldunifrf.rf19",             // fog blue
    "fsub rf9, rf9, rf19 ; nop",
    "nop ; fmul rf9, rf9, rf18",
    "fadd rf9, rf9, rf19 ; nop",
    "nop ; nop ; ldtlb.rf11", // rf11 = packed dst (r,g)
    "nop ; nop ; ldtlb.rf12", // rf12 = packed dst (b,a)
    "sub rf13, rf13, rf13 ; nop", "sub rf14, rf14, rf14 ; nop",
    "sub rf15, rf15, rf15 ; nop", "sub rf16, rf16, rf16 ; nop",
    "fadd rf13, rf13, rf11.l ; nop", // dst (1st read, low)
    "fadd rf14, rf14, rf11.h ; nop", // dst (1st read, high)
    "fadd rf15, rf15, rf12.l ; nop", // dst (2nd read, low)
    "fadd rf16, rf16, rf12.h ; nop", // dst (2nd read, high)
    "or rf17, 0x3f800000, 0x3f800000 ; nop", // rf17 = 1.0
    "fadd rf7, rf7, rf13 ; nop",
    "fmin rf7, rf7, rf17 ; nop",
    "fadd rf8, rf8, rf14 ; nop",
    "fmin rf8, rf8, rf17 ; nop",
    "fadd rf9, rf9, rf15 ; nop",
    "fmin rf9, rf9, rf17 ; nop",
    "fadd rf10, rf10, rf16 ; nop",
    "fmin rf10, rf10, rf17 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * textured_smooth_blend_srcalpha_one + fog.
 *
 * Fog before the first ldtlb, so it reaches the SOURCE colour and not the
 * blended result -- same rule as the other software-blend fog variants.
 *
 * This shader's blend math occupies the rf11/rf12/rf13 the shared fog
 * block uses. Fog runs on rf18/rf19/rf28 instead, picked from registers
 * this shader never touches.
 */
static const char* g_fragment_shader_textured_smooth_blend_srcalpha_one_fog_assembly[] = {
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
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf20, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf21, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf22, r1, r5 ; nop",
    "nop ; nop ; ldvary.r0",
    "nop ; fmul r1, r0, rf0",
    "fadd rf23, r1, r5 ; nop",
    "nop ; fmul rf7, rf4.l, rf20",
    "nop ; fmul rf8, rf4.h, rf21",
    "nop ; fmul rf9, rf3.l, rf22",
    "nop ; fmul rf10, rf3.h, rf23",
    /* Fog on rf18/rf19/rf28 -- allocated from THIS shader's own unused
     * registers. The usual rf11/rf12/rf13 fog block cannot be used here:
     * this shader's blend math occupies those. */
    /* Unified fog factor: all three GL modes from uniforms.
     *     f = M*(A + B*c) + (1-M) * 2^(C*c + D*c*c),   c = rf0 = eye distance
     * LINEAR sets M=1 with A,B from start/end; EXP sets C=-d*log2(e); EXP2
     * sets D=-d*d*log2(e). Same constants MESA precomputes in
     * st_nir_lower_fog.c. One sequence serves every mode, so a mode change
     * needs no new shader variant. 2^x is the QPU SFU, where MESA lowers
     * nir_fexp2 on this hardware.
     *
     * Only rf18/rf19/rf28 are needed: each uniform is consumed as it arrives and
     * the colour lerp runs one channel at a time. */
    "nop ; nop ; ldunifrf.rf18",             // A
    "nop ; nop ; ldunifrf.rf19",             // B
    "nop ; fmul rf19, rf19, rf0",
    "fadd rf18, rf18, rf19 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf19",             // C
    "nop ; fmul rf19, rf19, rf0",
    "nop ; nop ; ldunifrf.rf28",             // D
    "nop ; fmul rf28, rf28, rf0",
    "nop ; fmul rf28, rf28, rf0",
    "fadd rf19, rf19, rf28 ; nop",             // C*c + D*c*c
    "or exp, rf19, rf19 ; nop",               // SFU: r4 = 2^x
    "nop ; nop",                            // SFU latency
    "nop ; nop ; ldunifrf.rf19",             // M
    "nop ; fmul rf18, rf18, rf19",
    "or rf28, 0x3f800000, 0x3f800000 ; nop",
    "fsub rf28, rf28, rf19 ; nop",             // 1-M
    "nop ; fmul rf28, rf28, r4",
    "fadd rf18, rf18, rf28 ; nop",             // fog factor
    "sub rf19, rf19, rf19 ; nop",
    "fmax rf18, rf18, rf19 ; nop",
    "or rf19, 0x3f800000, 0x3f800000 ; nop",
    "fmin rf18, rf18, rf19 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf19",             // fog red
    "fsub rf7, rf7, rf19 ; nop",
    "nop ; fmul rf7, rf7, rf18",
    "fadd rf7, rf7, rf19 ; nop",
    "nop ; nop ; ldunifrf.rf19",             // fog green
    "fsub rf8, rf8, rf19 ; nop",
    "nop ; fmul rf8, rf8, rf18",
    "fadd rf8, rf8, rf19 ; nop",
    "nop ; nop ; ldunifrf.rf19",             // fog blue
    "fsub rf9, rf9, rf19 ; nop",
    "nop ; fmul rf9, rf9, rf18",
    "fadd rf9, rf9, rf19 ; nop",
    "nop ; nop ; ldtlb.rf11", // rf11 = packed dst (r,g)
    "nop ; nop ; ldtlb.rf12", // rf12 = packed dst (b,a)
    "sub rf13, rf13, rf13 ; nop", "sub rf14, rf14, rf14 ; nop",
    "sub rf15, rf15, rf15 ; nop", "sub rf16, rf16, rf16 ; nop",
    "fadd rf13, rf13, rf11.l ; nop",
    "fadd rf14, rf14, rf11.h ; nop",
    "fadd rf15, rf15, rf12.l ; nop",
    "fadd rf16, rf16, rf12.h ; nop",
    "or rf17, 0x3f800000, 0x3f800000 ; nop", // rf17 = 1.0
    "nop ; fmul rf24, rf7, rf10",
    "nop ; fmul rf25, rf8, rf10",
    "nop ; fmul rf26, rf9, rf10",
    "nop ; fmul rf27, rf10, rf10",
    "fadd rf7, rf24, rf13 ; nop",
    "fmin rf7, rf7, rf17 ; nop",
    "fadd rf8, rf25, rf14 ; nop",
    "fmin rf8, rf8, rf17 ; nop",
    "fadd rf9, rf26, rf15 ; nop",
    "fmin rf9, rf9, rf17 ; nop",
    "fadd rf10, rf27, rf16 ; nop",
    "fmin rf10, rf10, rf17 ; nop",
    "vfpack tlb, rf7, rf8  ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

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
static const char* g_fragment_shader_untextured_blend_fog_assembly[] = {
    "nop ; nop ; ldunifrf.rf7",  // rf7  = src red   (uniform 0)
    "nop ; nop ; ldunifrf.rf8",  // rf8  = src green (uniform 1)
    "nop ; nop ; ldunifrf.rf9",  // rf9  = src blue  (uniform 2)
    "nop ; nop ; ldunifrf.rf10", // rf10 = src alpha (uniform 3)
    /* Fog on rf3/rf4/rf5, allocated from this shader's own unused
     * registers -- its blend math occupies rf11 upwards.
     *
     * Placed here, straight after the four source-colour loads and BEFORE the
     * ldtlb, so fog reaches the source and not the blended result. No
     * skip-reads are needed: unlike the smooth untextured variants, this
     * shader already consumes draw.c's four fixed-colour words above as its
     * own source colour, so the fog words follow immediately in the stream. */
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
    "fadd rf3, rf3, rf4 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf4",             // C
    "nop ; fmul rf4, rf4, rf0",
    "nop ; nop ; ldunifrf.rf5",             // D
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
    "fmin rf3, rf3, rf4 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf4",             // fog red
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop",
    "nop ; nop ; ldunifrf.rf4",             // fog green
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop",
    "nop ; nop ; ldunifrf.rf4",             // fog blue
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop",
    "nop ; nop ; thrsw", // single thread switch before the first tlb access
    "nop ; nop",         // delay slot 1 of 2 (tick+1)
    "nop ; nop",         // delay slot 2 of 2 (tick+2) -- ldtlb below lands at tick+3
    "nop ; nop ; ldtlb.rf11", // rf11 = packed dst (r,g)
    "nop ; nop ; ldtlb.rf12", // rf12 = packed dst (b,a)
    "sub rf13, rf13, rf13 ; nop", "sub rf14, rf14, rf14 ; nop",
    "sub rf15, rf15, rf15 ; nop", "sub rf16, rf16, rf16 ; nop",
    "fadd rf13, rf13, rf11.l ; nop", // dst_r
    "fadd rf14, rf14, rf11.h ; nop", // dst_g
    "fadd rf15, rf15, rf12.l ; nop", // dst_b
    "fadd rf16, rf16, rf12.h ; nop", // dst_a
    "sub rf7, rf7, rf7 ; nop", "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop", "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf13 ; nop",
    "fadd rf8, rf8, rf14 ; nop",
    "fadd rf9, rf9, rf15 ; nop",
    "fadd rf10, rf10, rf16 ; nop",
    "or rf17, 0x3f800000, 0x3f800000 ; nop", // rf17 = 1.0
    "fsub rf18, rf17, rf10 ; nop",           // rf18 = invAlpha = 1.0 - alpha
    "nop ; fmul rf19, rf7, rf10",
    "nop ; fmul rf20, rf13, rf18",
    "fadd rf7, rf19, rf20 ; nop", // result_r
    "nop ; fmul rf19, rf8, rf10",
    "nop ; fmul rf20, rf14, rf18",
    "fadd rf8, rf19, rf20 ; nop", // result_g
    "nop ; fmul rf19, rf9, rf10",
    "nop ; fmul rf20, rf15, rf18",
    "fadd rf9, rf19, rf20 ; nop", // result_b
    "nop ; fmul rf19, rf10, rf10",
    "nop ; fmul rf20, rf16, rf18",
    "fadd rf10, rf19, rf20 ; nop", // result_a
    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- satisfies the >=3-instruction gap before the next thrsw
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
/*
 * untextured_blend_add + fog.
 *
 * Untextured and FLAT: the source colour arrives as four ldunifrf loads at
 * the very top, so it is final before the ldtlb and the fog block goes
 * straight after it.
 *
 * Uniform words: the 4 source-colour words, then the fog words, matching
 * what draw.c writes for a fogged untextured draw.
 */
static const char* g_fragment_shader_untextured_blend_add_fog_assembly[] = {
    "nop ; nop ; ldunifrf.rf7",  // rf7  = src red   (uniform 0)
    "nop ; nop ; ldunifrf.rf8",  // rf8  = src green (uniform 1)
    "nop ; nop ; ldunifrf.rf9",  // rf9  = src blue  (uniform 2)
    "nop ; nop ; ldunifrf.rf10", // rf10 = src alpha (uniform 3)
    /* Fog on rf3/rf4/rf5, allocated from this shader's own unused
     * registers -- its blend math occupies rf11 upwards.
     *
     * Placed here, straight after the four source-colour loads and BEFORE the
     * ldtlb, so fog reaches the source and not the blended result. No
     * skip-reads are needed: unlike the smooth untextured variants, this
     * shader already consumes draw.c's four fixed-colour words above as its
     * own source colour, so the fog words follow immediately in the stream. */
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
    "fadd rf3, rf3, rf4 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf4",             // C
    "nop ; fmul rf4, rf4, rf0",
    "nop ; nop ; ldunifrf.rf5",             // D
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
    "fmin rf3, rf3, rf4 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf4",             // fog red
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop",
    "nop ; nop ; ldunifrf.rf4",             // fog green
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop",
    "nop ; nop ; ldunifrf.rf4",             // fog blue
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop",

    /* SCOREBOARD LOCK. A TLB read must not happen before the scoreboard
     * lock is taken -- MESA nir_to_vir.c vir_emit_tlb_color_read: "We need
     * to emit our TLB reads after we have acquired the scoreboard lock, or
     * the GPU will hang." The lock is taken on a thread switch, so one is
     * emitted here, and MESA's scheduler only counts the scoreboard as
     * locked once tick - last_thrsw_tick >= 3, hence two filler slots.
     * Which thread switch takes the lock is chosen by
     * do_scoreboard_wait_on_first_thread_switch in the shader record
     * (draw.c). */
    "nop ; nop ; thrsw",
    "nop ; nop",
    "nop ; nop",

    "nop ; nop ; ldtlb.rf11", // rf11 = packed dst (r,g)
    "sub rf13, rf13, rf13 ; nop",
    "sub rf14, rf14, rf14 ; nop",
    "fadd rf13, rf13, rf11.l ; nop", // dst_r
    "fadd rf14, rf14, rf11.h ; nop", // dst_g
    "nop ; nop ; ldtlb.rf12", // rf12 = packed dst (b,a) -- issued only after rf11's own unpack, matching MESA's spacing
    "sub rf15, rf15, rf15 ; nop",
    "sub rf16, rf16, rf16 ; nop",
    "fadd rf15, rf15, rf12.l ; nop", // dst_b
    "fadd rf16, rf16, rf12.h ; nop", // dst_a
    "or rf17, 0x3f800000, 0x3f800000 ; nop", // rf17 = 1.0
    "fadd rf7, rf7, rf13 ; nop",
    "fmin rf7, rf7, rf17 ; nop", // result_r
    "fadd rf8, rf8, rf14 ; nop",
    "fmin rf8, rf8, rf17 ; nop", // result_g
    "fadd rf9, rf9, rf15 ; nop",
    "fmin rf9, rf9, rf17 ; nop", // result_b
    "fadd rf10, rf10, rf16 ; nop",
    "fmin rf10, rf10, rf17 ; nop", // result_a
    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- satisfies the >=3-instruction gap before the next thrsw
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};
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
static const char* g_fragment_shader_untextured_blend_srcalpha_one_fog_assembly[] = {
    "nop ; nop ; ldunifrf.rf7",  // rf7  = src red   (uniform 0)
    "nop ; nop ; ldunifrf.rf8",  // rf8  = src green (uniform 1)
    "nop ; nop ; ldunifrf.rf9",  // rf9  = src blue  (uniform 2)
    "nop ; nop ; ldunifrf.rf10", // rf10 = src alpha (uniform 3)
    /* Fog on rf3/rf4/rf5, allocated from this shader's own unused
     * registers -- its blend math occupies rf11 upwards.
     *
     * Placed here, straight after the four source-colour loads and BEFORE the
     * ldtlb, so fog reaches the source and not the blended result. No
     * skip-reads are needed: unlike the smooth untextured variants, this
     * shader already consumes draw.c's four fixed-colour words above as its
     * own source colour, so the fog words follow immediately in the stream. */
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
    "fadd rf3, rf3, rf4 ; nop",             // linear factor
    "nop ; nop ; ldunifrf.rf4",             // C
    "nop ; fmul rf4, rf4, rf0",
    "nop ; nop ; ldunifrf.rf5",             // D
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
    "fmin rf3, rf3, rf4 ; nop",             // clamped to [0,1]
    "nop ; nop ; ldunifrf.rf4",             // fog red
    "fsub rf7, rf7, rf4 ; nop",
    "nop ; fmul rf7, rf7, rf3",
    "fadd rf7, rf7, rf4 ; nop",
    "nop ; nop ; ldunifrf.rf4",             // fog green
    "fsub rf8, rf8, rf4 ; nop",
    "nop ; fmul rf8, rf8, rf3",
    "fadd rf8, rf8, rf4 ; nop",
    "nop ; nop ; ldunifrf.rf4",             // fog blue
    "fsub rf9, rf9, rf4 ; nop",
    "nop ; fmul rf9, rf9, rf3",
    "fadd rf9, rf9, rf4 ; nop",

    /* SCOREBOARD LOCK. A TLB read must not happen before the scoreboard
     * lock is taken -- MESA nir_to_vir.c vir_emit_tlb_color_read: "We need
     * to emit our TLB reads after we have acquired the scoreboard lock, or
     * the GPU will hang." The lock is taken on a thread switch, so one is
     * emitted here, and MESA's scheduler only counts the scoreboard as
     * locked once tick - last_thrsw_tick >= 3, hence two filler slots.
     * Which thread switch takes the lock is chosen by
     * do_scoreboard_wait_on_first_thread_switch in the shader record
     * (draw.c). */
    "nop ; nop ; thrsw",
    "nop ; nop",
    "nop ; nop",

    "nop ; nop ; ldtlb.rf11", // rf11 = packed dst (r,g)
    "nop ; nop ; ldtlb.rf12", // rf12 = packed dst (b,a)
    "sub rf13, rf13, rf13 ; nop", "sub rf14, rf14, rf14 ; nop",
    "sub rf15, rf15, rf15 ; nop", "sub rf16, rf16, rf16 ; nop",
    "fadd rf13, rf13, rf11.l ; nop", // dst_r
    "fadd rf14, rf14, rf11.h ; nop", // dst_g
    "fadd rf15, rf15, rf12.l ; nop", // dst_b
    "fadd rf16, rf16, rf12.h ; nop", // dst_a
    "or rf17, 0x3f800000, 0x3f800000 ; nop", // rf17 = 1.0
    "nop ; fmul rf19, rf7, rf10",  // rf19 = src_r * alpha
    "nop ; fmul rf20, rf8, rf10",  // rf20 = src_g * alpha
    "nop ; fmul rf21, rf9, rf10",  // rf21 = src_b * alpha
    "nop ; fmul rf22, rf10, rf10", // rf22 = alpha * alpha (before rf10 is overwritten)
    "fadd rf7, rf19, rf13 ; nop",
    "fmin rf7, rf7, rf17 ; nop", // result_r
    "fadd rf8, rf20, rf14 ; nop",
    "fmin rf8, rf8, rf17 ; nop", // result_g
    "fadd rf9, rf21, rf15 ; nop",
    "fmin rf9, rf9, rf17 ; nop", // result_b
    "fadd rf10, rf22, rf16 ; nop",
    "fmin rf10, rf10, rf17 ; nop", // result_a
    "nop ; nop ; thrsw", // last-thrsw signal, part 1 of 2
    "nop ; nop ; thrsw", // last-thrsw signal, part 2 of 2
    "nop ; nop",         // filler -- satisfies the >=3-instruction gap before the next thrsw
    "vfpack tlb, rf7, rf8  ; nop ; thrsw", // thread-end thrsw
    "vfpack tlb, rf9, rf10 ; nop",
    "nop                   ; nop",
};

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
    "nop ; fmul rf11, rf11, r4",             // rf11 = d

    "nop ; nop ; ldunifrf.rf7",  // rf7  = red   (uniform 0)
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
    "fadd rf10, r1, r5 ; nop", // rf10 = true alpha

    "nop ; nop ; ldunifrf.rf24", // consume col[0], unused
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

    "sub rf7, rf7, rf7 ; nop",
    "sub rf8, rf8, rf8 ; nop",
    "sub rf9, rf9, rf9 ; nop",
    "sub rf10, rf10, rf10 ; nop",
    "fadd rf7, rf7, rf4.l ; nop",  // tex_blue
    "fadd rf8, rf8, rf4.h ; nop",  // tex_green
    "fadd rf9, rf9, rf3.l ; nop",  // tex_red
    "fadd rf10, rf10, rf3.h ; nop", // tex_alpha

    "nop ; nop ; ldunifrf.rf5", // rf5 = color blue multiplier (uniform 2)
    "nop ; fmul rf7, rf7, rf5", // rf7 = tex_blue * color_blue
    "nop ; nop ; ldunifrf.rf5", // rf5 = color green multiplier (uniform 3)
    "nop ; fmul rf8, rf8, rf5", // rf8 = tex_green * color_green
    "nop ; nop ; ldunifrf.rf5", // rf5 = color red multiplier (uniform 4)
    "nop ; fmul rf9, rf9, rf5", // rf9 = tex_red * color_red
    "nop ; nop ; ldunifrf.rf24", // rf24 = color alpha multiplier (uniform 5)
    "nop ; fmul rf10, rf10, rf24", // rf10 = tex_alpha * color_alpha

    "nop ; nop ; ldunifrf.rf12",             // rf12 = rasterized point size (uniform 6)
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
    "fadd rf22, r1, r5 ; nop",  // rf22 = true vertex blue
    "nop ; nop ; ldvary.r0",    // load a/w
    "nop ; fmul r1, r0, rf0",   // r1 = a/w * w
    "fadd rf23, r1, r5 ; nop",  // rf23 = true vertex alpha

    "nop ; fmul rf7, rf4.l, rf20",  // ch0 = texel ch0 * vertex red   (TLB slot 0)
    "nop ; fmul rf8, rf4.h, rf21",  // ch1 = texel ch1 * vertex green (TLB slot 1)
    "nop ; fmul rf9, rf3.l, rf22",  // ch2 = texel ch2 * vertex blue  (TLB slot 2)
    "nop ; fmul rf10, rf3.h, rf23", // ch3 = texel ch3 * vertex alpha (TLB slot 3)

    "nop ; nop ; ldunifrf.rf12",             // rf12 = rasterized point size (uniform 2)
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
 * MiniGLV3D orchestrator, replacing PoC's v3d_assemble() (v3d_assembler.c:1077):
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
 * why PoC's own v3d_assemble() (v3d_assembler.c, pre-port) used static
 * file-scope buffers too.
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

static int v3d_assemble_one_shader(struct v3d_device_info* devinfo, const char* name,
                                    const char** assemblyLines, int numAssemblyLines,
                                    V3DAssembledShader* out)
{
    struct v3d_qpu_validate_result validateResults;
    int assemblyLine;

    out->numInstructions = 0;

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
            return FALSE;
        }

        if (out->numInstructions >= V3D_SHADER_MAX_INSTRUCTIONS)
        {
            D(("v3d_assemble_one_shader: %s ran out of space (max %ld instructions)\n", name, (LONG)V3D_SHADER_MAX_INSTRUCTIONS));
            return FALSE;
        }

        unpackedInstructions[out->numInstructions] = assembleArguments.instruction;

        if (!v3d_qpu_instr_pack(devinfo, &assembleArguments.instruction, &out->instructions[out->numInstructions]))
        {
            D(("v3d_assemble_one_shader: failed to pack %s instruction [%ld]\n'%s'\n",
               name, (LONG)assemblyLine, assembleArguments.assembly));
            return FALSE;
        }

        ++out->numInstructions;
    }

    memset(&validateResults, 0, sizeof(validateResults));
    if (!v3d_qpu_validate(devinfo, unpackedInstructions, out->numInstructions, &validateResults))
    {
        D(("v3d_assemble_one_shader: validation error in %s at instruction [%ld]: %s\n'%s'\n",
           name, (LONG)validateResults.errorInstructionIndex, validateResults.errorMessage,
           assemblyLines[validateResults.errorInstructionIndex]));
        return FALSE;
    }

    return TRUE;
}

/* Shared storage for every assembled shader variant -- see
 * v3d_shader_assembler.h's own comment on why this is static/persistent
 * rather than caller-provided, unlike the transient scratch buffers
 * above. */
V3DAssembledShader v3d_shader_variants[V3D_MAX_SHADER_VARIANTS];

int v3d_assemble_builtin_shaders(V3DDevice* device)
{
    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment",
                                  g_fragment_shader_assembly, V3D_ARRAY_SIZE(g_fragment_shader_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED]))
        return FALSE;

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

    if (!v3d_assemble_one_shader(&device->deviceInfo, "vertex_smooth_textured",
                                  g_vertex_shader_smooth_textured_assembly, V3D_ARRAY_SIZE(g_vertex_shader_smooth_textured_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_VERTEX_SMOOTH_TEXTURED]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth",
                                  g_fragment_shader_textured_smooth_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH]))
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

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_blend",
                                  g_fragment_shader_untextured_blend_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_blend_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_BLEND]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_blend_add",
                                  g_fragment_shader_untextured_blend_add_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_blend_add_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_BLEND_ADD]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_smooth_blend_add",
                                  g_fragment_shader_untextured_smooth_blend_add_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_smooth_blend_add_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_BLEND_ADD]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_blend_srcalpha_one",
                                  g_fragment_shader_untextured_blend_srcalpha_one_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_blend_srcalpha_one_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_BLEND_SRCALPHA_ONE]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_smooth_blend_srcalpha_one",
                                  g_fragment_shader_untextured_smooth_blend_srcalpha_one_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_smooth_blend_srcalpha_one_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_BLEND_SRCALPHA_ONE]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_blend_srcalpha_one",
                                  g_fragment_shader_textured_smooth_blend_srcalpha_one_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_blend_srcalpha_one_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_BLEND_SRCALPHA_ONE]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_smooth_blend",
                                  g_fragment_shader_untextured_smooth_blend_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_smooth_blend_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_BLEND]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_blend",
                                  g_fragment_shader_textured_blend_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_blend_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_BLEND]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_blend",
                                  g_fragment_shader_textured_smooth_blend_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_blend_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_BLEND]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_dstcolor_zero",
                                  g_fragment_shader_textured_smooth_dstcolor_zero_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_dstcolor_zero_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_ZERO]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_dstcolor_one",
                                  g_fragment_shader_textured_smooth_dstcolor_one_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_dstcolor_one_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_ONE]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_dstcolor_srccolor",
                                  g_fragment_shader_textured_smooth_dstcolor_srccolor_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_dstcolor_srccolor_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_SRCCOLOR]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_dstcolor_invdstalpha",
                                  g_fragment_shader_textured_smooth_dstcolor_invdstalpha_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_dstcolor_invdstalpha_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_INVDSTALPHA]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_zero_invsrccolor",
                                  g_fragment_shader_textured_smooth_zero_invsrccolor_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_zero_invsrccolor_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ZERO_INVSRCCOLOR]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_dstcolor_srcalpha",
                                  g_fragment_shader_textured_smooth_dstcolor_srcalpha_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_dstcolor_srcalpha_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_SRCALPHA]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_one_invsrcalpha",
                                  g_fragment_shader_textured_smooth_one_invsrcalpha_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_one_invsrcalpha_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ONE_INVSRCALPHA]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_invsrcalpha_srcalpha",
                                  g_fragment_shader_textured_smooth_invsrcalpha_srcalpha_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_invsrcalpha_srcalpha_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_INVSRCALPHA_SRCALPHA]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_padded_flat_test",
                                  g_fragment_shader_padded_flat_test_assembly, V3D_ARRAY_SIZE(g_fragment_shader_padded_flat_test_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_PADDED_FLAT_TEST]))
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

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_multitexture_modulate_blend",
                                  g_fragment_shader_multitexture_modulate_blend_assembly, V3D_ARRAY_SIZE(g_fragment_shader_multitexture_modulate_blend_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_MODULATE_BLEND]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_multitexture_decal_blend",
                                  g_fragment_shader_multitexture_decal_blend_assembly, V3D_ARRAY_SIZE(g_fragment_shader_multitexture_decal_blend_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_DECAL_BLEND]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_multitexture_replace_blend",
                                  g_fragment_shader_multitexture_replace_blend_assembly, V3D_ARRAY_SIZE(g_fragment_shader_multitexture_replace_blend_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_REPLACE_BLEND]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_multitexture_modulate_translucent",
                                  g_fragment_shader_multitexture_modulate_translucent_assembly, V3D_ARRAY_SIZE(g_fragment_shader_multitexture_modulate_translucent_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_MODULATE_TRANSLUCENT]))
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

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_blend_add",
                                  g_fragment_shader_textured_smooth_blend_add_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_blend_add_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_BLEND_ADD]))
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

    /* Software-blend fog, 14 variants. */
    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_blend_fog",
                                  g_fragment_shader_textured_blend_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_blend_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_BLEND_FOG]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_blend_fog",
                                  g_fragment_shader_textured_smooth_blend_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_blend_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_BLEND_FOG]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_dstcolor_zero_fog",
                                  g_fragment_shader_textured_smooth_dstcolor_zero_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_dstcolor_zero_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_ZERO_FOG]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_dstcolor_one_fog",
                                  g_fragment_shader_textured_smooth_dstcolor_one_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_dstcolor_one_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_ONE_FOG]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_dstcolor_srccolor_fog",
                                  g_fragment_shader_textured_smooth_dstcolor_srccolor_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_dstcolor_srccolor_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_SRCCOLOR_FOG]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_dstcolor_invdstalpha_fog",
                                  g_fragment_shader_textured_smooth_dstcolor_invdstalpha_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_dstcolor_invdstalpha_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_INVDSTALPHA_FOG]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_zero_invsrccolor_fog",
                                  g_fragment_shader_textured_smooth_zero_invsrccolor_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_zero_invsrccolor_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ZERO_INVSRCCOLOR_FOG]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_dstcolor_srcalpha_fog",
                                  g_fragment_shader_textured_smooth_dstcolor_srcalpha_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_dstcolor_srcalpha_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_SRCALPHA_FOG]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_one_invsrcalpha_fog",
                                  g_fragment_shader_textured_smooth_one_invsrcalpha_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_one_invsrcalpha_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ONE_INVSRCALPHA_FOG]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_invsrcalpha_srcalpha_fog",
                                  g_fragment_shader_textured_smooth_invsrcalpha_srcalpha_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_invsrcalpha_srcalpha_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_INVSRCALPHA_SRCALPHA_FOG]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_multitexture_modulate_translucent_fog",
                                  g_fragment_shader_multitexture_modulate_translucent_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_multitexture_modulate_translucent_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_MODULATE_TRANSLUCENT_FOG]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_multitexture_modulate_blend_fog",
                                  g_fragment_shader_multitexture_modulate_blend_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_multitexture_modulate_blend_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_MODULATE_BLEND_FOG]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_multitexture_decal_blend_fog",
                                  g_fragment_shader_multitexture_decal_blend_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_multitexture_decal_blend_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_DECAL_BLEND_FOG]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_multitexture_replace_blend_fog",
                                  g_fragment_shader_multitexture_replace_blend_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_multitexture_replace_blend_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_REPLACE_BLEND_FOG]))
        return FALSE;

    /* Register-constrained blend fog, 5 variants. */
    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_smooth_blend_fog",
                                  g_fragment_shader_untextured_smooth_blend_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_smooth_blend_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_BLEND_FOG]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_smooth_blend_add_fog",
                                  g_fragment_shader_untextured_smooth_blend_add_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_smooth_blend_add_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_BLEND_ADD_FOG]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_smooth_blend_srcalpha_one_fog",
                                  g_fragment_shader_untextured_smooth_blend_srcalpha_one_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_smooth_blend_srcalpha_one_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_BLEND_SRCALPHA_ONE_FOG]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_blend_add_fog",
                                  g_fragment_shader_textured_smooth_blend_add_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_blend_add_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_BLEND_ADD_FOG]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_textured_smooth_blend_srcalpha_one_fog",
                                  g_fragment_shader_textured_smooth_blend_srcalpha_one_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_textured_smooth_blend_srcalpha_one_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_BLEND_SRCALPHA_ONE_FOG]))
        return FALSE;

    /* Untextured flat blend fog, 3 variants. */
    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_blend_fog",
                                  g_fragment_shader_untextured_blend_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_blend_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_BLEND_FOG]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_blend_add_fog",
                                  g_fragment_shader_untextured_blend_add_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_blend_add_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_BLEND_ADD_FOG]))
        return FALSE;

    if (!v3d_assemble_one_shader(&device->deviceInfo, "fragment_untextured_blend_srcalpha_one_fog",
                                  g_fragment_shader_untextured_blend_srcalpha_one_fog_assembly, V3D_ARRAY_SIZE(g_fragment_shader_untextured_blend_srcalpha_one_fog_assembly),
                                  &v3d_shader_variants[V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_BLEND_SRCALPHA_ONE_FOG]))
        return FALSE;







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


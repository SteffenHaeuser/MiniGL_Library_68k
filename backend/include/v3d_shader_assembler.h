/*
 * (C) 2025-2026 Dennis van der Boon
 */

#ifndef V3D_SHADER_ASSEMBLER_H
#define V3D_SHADER_ASSEMBLER_H

#include "v3d_types.h"
#include "v3d_device.h"

/*
 * Public API for backend/hw/v3d_assembler.c (the ported QPU assembler,
 * backend/hw/v3d_assembler.h). This header is what the rest of
 * MiniGLV3D includes -- v3d_assembler.h itself (the ported library) is
 * an internal implementation detail, included only by v3d_assembler.c.
 */

/*
 * Not a hardware ceiling and not a layout constraint -- purely the host buffer
 * each variant assembles into. The GPU-side limit is gone: gl_EnsureShaders
 * (draw.c) lays the shaders end to end and records each offset in
 * g_shader_offset, so a longer shader simply takes more bytes and the next one
 * starts after it. A shader past this cap fails to assemble ("ran out of
 * space") and v3d_assemble_builtin_shaders returns FALSE -- loudly, at init,
 * caught by shader_assembler_test.
 *
 * One capacity for every variant, 2026-09-29. It was 128 with a separate
 * 256-entry buffer for the one shader that needed more, back when 128 was
 * welded to the 1024-byte slot stride. With the stride gone that split bought
 * nothing but a special case at every use, so it costs 70 * 256 * 8 = 143,360
 * bytes here instead of 71,680, and the lit shaders have room to unroll per
 * light.
 */
#define V3D_SHADER_MAX_INSTRUCTIONS 256

typedef struct V3DAssembledShader {
    v3d_qpu_instruction instructions[V3D_SHADER_MAX_INSTRUCTIONS]; /* packed 64-bit QPU machine code, ready for byteswap64 + upload */
    int numInstructions;
} V3DAssembledShader;

/*
 * Named slots into v3d_shader_variants[]. The 3 base shaders
 * (fragment/vertex/coordinate, textured; derived from the mnemonics in
 * PoC/v3d_shaders.c) come first.
 *
 * V3D_MAX_SHADER_VARIANTS IS A PRECISE COUNT, NOT HEADROOM -- the enum is
 * exactly full.
 *
 * ADDING A VARIANT REQUIRES THESE EDITS IN LOCKSTEP:
 *   1. the new enum entry.
 *   2. V3D_MAX_SHADER_VARIANTS below (it sizes v3d_shader_variants[]).
 *   3. nothing, for the allocation. gl_EnsureShaders sums the variants'
 *      real sizes and allocates exactly that, so the buffer grows with the
 *      new variant on its own. No literal total needs bumping.
 *   4. the parallel lists in gl_EnsureShaders (pointer declarations,
 *      shader_vex + OFFSET assignment, &v3d_shader_variants[] binding, and
 *      the byteswap64 upload loop).
 *   5. the v3d_assemble_one_shader call in v3d_assemble_builtin_shaders
 *      (v3d_assembler.c) that fills the entry -- without it the entry
 *      stays all-zero.
 * Nothing enforces any of this at compile time; it is maintained by hand.
 *
 * THERE IS NO SLOT STRIDE. The shaders are packed end to end and each one's
 * byte offset is recorded in draw.c's g_shader_offset, so a long shader cannot
 * reach the next variant's code. This said "stride is 1024 bytes =
 * V3D_SHADER_MAX_INSTRUCTIONS (128) * 8" until 2026-09-29 and was left behind
 * by that change, contradicting the 256 above it four lines up.
 */
enum {
    V3D_SHADER_VARIANT_VERTEX_TEXTURED = 0,
    V3D_SHADER_VARIANT_COORDINATE_TEXTURED,
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED,
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG,
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST,

    /* Smooth (GL_SMOOTH, per-vertex color) shading, untextured only --
     * see v3d_assembler.c's comment on g_vertex_shader_smooth_assembly/
     * g_fragment_shader_untextured_smooth_assembly for the design (a
     * vertex-stage variant carrying color instead of texcoord as its
     * varying payload; the COORDINATE stage is reused unchanged -- it
     * never touches texcoord/color varyings at all). */
    V3D_SHADER_VARIANT_VERTEX_SMOOTH,
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH,

    /* Combined GL_MODULATE -- texture sampled, then multiplied by
     * per-vertex smooth color, in one draw call. Not just the textured
     * and smooth variants side by side: the vertex shader reads 9 input
     * words (3 position + 2 texcoord + 4 color), more than one 8-word VPM
     * sector, so draw.c sets vertex_shader_input_vpm_segment_size = 2 for
     * it. See v3d_assembler.c's comment on
     * g_vertex_shader_smooth_textured_assembly/
     * g_fragment_shader_textured_smooth_assembly for the design. */
    V3D_SHADER_VARIANT_VERTEX_SMOOTH_TEXTURED,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH,

    /* GL_FOG and GL_ALPHA_TEST, textured. Reuse VERTEX_TEXTURED/
     * COORDINATE_TEXTURED unchanged (neither needs a color varying, same
     * as flat/plain-textured) -- only new FRAGMENT shaders: the same
     * texture fetch and flat glColor multiply as FRAGMENT_TEXTURED_COLORMOD
     * below, followed by the fog-blend/alpha-discard math of the
     * untextured FOG/ALPHATEST variants above.
     * The untextured and textured variants alike read their fog
     * parameters, fog colour and alpha_ref as uniforms (glFogf/glFogfv/
     * glAlphaFunc-driven) -- see v3d_assembler.c's comments on
     * g_fragment_shader_textured_fog_assembly/textured_alphatest_assembly
     * for the full design. */
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST,

    /* Multitexture -- two independent bound textures (MAX_TEXUNIT==2)
     * sampled in one draw call and combined via GL_MODULATE (multiply).
     * Needs its OWN vertex shader (4 texcoord varyings, s0/t0/s1/t1,
     * instead of 2), without needing a color varying. COORDINATE_TEXTURED
     * is reused unchanged. See v3d_assembler.c's comments on
     * g_vertex_shader_multitexture_assembly/
     * g_fragment_shader_multitexture_assembly for the full design,
     * including the two-TMU-fetch thrsw pattern, which follows MESA's
     * compiler. */
    V3D_SHADER_VARIANT_VERTEX_MULTITEXTURE,
    V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE,

    /* GL_BLEND via SOFTWARE (shader-side) blend, untextured only. See
     * v3d_assembler.c's comment on
     * g_fragment_shader_untextured_blend_assembly for the full design
     * (reads the destination tile-buffer pixel itself via the QPU's
     * ldtlb signal, computes the classic SRC_ALPHA/ONE_MINUS_SRC_ALPHA
     * blend equation in-shader). Reuses VERTEX_TEXTURED/COORDINATE_
     * TEXTURED unchanged, same as the untextured fog/alphatest variants.
     * draw.c selects none of the software-blend (ldtlb) variants in this
     * enum, their _FOG forms included: a blended draw uses the plain
     * shader for its shape plus the hardware blend. */

    /* Variants that read a REAL per-vertex w instead of a hardcoded
     * w=1.0: selected for clip-space (Sutherland-Hodgeman-generated)
     * vertices, and for ordinary smooth+textured draws whose vertices
     * carry a real w or q (glVertex4f/glTexCoord4f) -- see
     * v3d_assembler.c's comments on g_coordinate_shader_clipspace_
     * assembly/g_vertex_shader_smooth_textured_clipspace_assembly for
     * the full design. Only the "combined" (smooth+textured) vertex
     * shape is covered here (the multitextured one follows below);
     * draw.c falls back to the w=1.0 shaders for any state these don't
     * cover. */
    V3D_SHADER_VARIANT_COORDINATE_CLIPSPACE,
    V3D_SHADER_VARIANT_VERTEX_SMOOTH_TEXTURED_CLIPSPACE,

    /* Same real-w handling, for multitextured+clip-space (see
     * v3d_assembler.c's comment on
     * g_vertex_shader_multitexture_clipspace_assembly). Reuses
     * COORDINATE_CLIPSPACE unchanged -- position-only, generic across
     * every variant. */
    V3D_SHADER_VARIANT_VERTEX_MULTITEXTURE_CLIPSPACE,

    /* GL_DECAL/GL_REPLACE combine modes for multitexture, plus
     * blend-aware counterparts of all 3 combine modes -- see
     * v3d_assembler.c's comments on g_fragment_shader_multitexture_
     * decal_assembly/g_fragment_shader_multitexture_replace_assembly/
     * g_fragment_shader_multitexture_{modulate,decal,replace}_blend_
     * assembly for the full design. Fragment-only --
     * VERTEX_MULTITEXTURE(_CLIPSPACE) covers every combine/blend
     * combination, since only the fragment stage differs between them. */
    V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_DECAL,
    V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_REPLACE,

    /* GL_GREATER alpha test. Same discard mechanism as the GEQUAL pair,
     * just the fcmp operand order swapped and the setmsf condition
     * flipped -- see v3d_assembler.c's comment on
     * g_fragment_shader_untextured_alphatest_greater_assembly for the
     * derivation. Fragment-only, same as the GEQUAL pair -- reuses
     * VERTEX_TEXTURED/COORDINATE_TEXTURED unchanged. */
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST_GREATER,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST_GREATER,

    /* Diagnostic variant, not selected by draw.c: byte-for-byte
     * g_fragment_shader_assembly (the plain flat FRAGMENT_TEXTURED
     * shader) plus 8 harmless no-op instructions
     * inserted at the same relative point the smooth+textured shader's 4
     * extra varying-read triplets sit -- a shader that matches the smooth
     * shader's instruction COUNT/timing exactly but not its CONTENT. See
     * v3d_assembler.c's comment on
     * g_fragment_shader_padded_flat_test_assembly. */

    /* Multitextured + SRC_ALPHA/INV_SRC_ALPHA translucency blend,
     * GL_MODULATE combine only. A direct splice of two existing pieces,
     * not new QPU design, same technique as
     * FRAGMENT_TEXTURED_SMOOTH_BLEND's own construction:
     *   1. g_fragment_shader_multitexture_modulate_blend_assembly's own
     *      2-TMU fetch + GL_MODULATE combine (produces rf7-10 = combined
     *      blue/green/red/alpha, its own additive blend tail stripped
     *      off after this point).
     *   2. g_fragment_shader_textured_blend_assembly's own glColor-alpha-
     *      uniform read + ldtlb dest-read + SRC_ALPHA/INV_SRC_ALPHA LERP +
     *      writeback, taken verbatim from the point where IT has rf7-rf10
     *      in the exact same blue/green/red/alpha shape. */

    /* The plain textured+flat case (not smooth, not multitextured, no
     * fog/alphatest): multiplies the texture sample by glColor's flat
     * color, the way the smooth path does per-vertex.
     * g_fragment_shader_assembly (FRAGMENT_TEXTURED) is a pure
     * texture-sample passthrough with NO color modulation at all. This
     * variant splices g_fragment_shader_textured_blend_assembly's own
     * texture-fetch + 4-uniform-color-multiply front section onto a plain
     * vfpack/thrsw ending (no ldtlb blend tail) -- see v3d_assembler.c's
     * comment on g_fragment_shader_textured_colormod_assembly. */
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_COLORMOD,

    /* src=ZERO, dst=INVSRCCOLOR (result = Cd*(1-Cs)),
     * textured+smooth+!multitextured. Same splice technique, same
     * VERTEX_SMOOTH_TEXTURED reuse as the DSTCOLOR family above. */

    /* src=DSTCOLOR, dst=SRCALPHA (result = Cd*(Cs+Cs.a)) -- the DSTCOLOR
     * family, with a dst factor not covered by the 4 above,
     * textured+smooth+!multitextured. Same splice technique, same
     * VERTEX_SMOOTH_TEXTURED reuse. */

    /* Two more factor pairs, src=ONE and src=INVSRCALPHA:
     *   ONE_INVSRCALPHA:      src=ONE,         dst=INVSRCALPHA
     *                         result = Cs + Cd*(1-Cs.a)
     *   INVSRCALPHA_SRCALPHA: src=INVSRCALPHA, dst=SRCALPHA
     *                         result = Cs*(1-Cs.a) + Cd*Cs.a
     * Same splice technique, same VERTEX_SMOOTH_TEXTURED reuse as the
     * rest of this family. */

    /* GL_ALPHA_TEST (GL_GREATER) combined with GL_SMOOTH shading,
     * textured -- see v3d_assembler.c's comment on
     * g_fragment_shader_textured_smooth_alphatest_greater_assembly.
     * draw.c's flat `alphatest` predicate requires !smooth, so smooth
     * draws need shaders of their own. */
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST_GREATER,

    /* Same shape, GL_GEQUAL. */
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST,

    /*
     * REMAINING alpha_func VARIANTS: GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL
     * and GL_NOTEQUAL (GL_GEQUAL and GL_GREATER are above).
     *
     * Derivation (see each shader's own comment in v3d_assembler.c):
     * FCMP only computes a >=-style comparison -- `fcmp.pushc -, X, Y` sets
     * IFA true when Y >= X. That gives all four inequalities from two
     * operand orders x two setmsf conditions:
     *
     *   fcmp(rf15,rf10) IFA<=>alpha>=ref  + setmsf.ifna -> keep alpha>=ref  GEQUAL
     *   fcmp(rf15,rf10)                   + setmsf.ifa  -> keep alpha< ref  LESS
     *   fcmp(rf10,rf15) IFA<=>alpha<=ref  + setmsf.ifa  -> keep alpha> ref  GREATER
     *   fcmp(rf10,rf15)                   + setmsf.ifna -> keep alpha<=ref  LEQUAL
     *
     * EQUAL/NOTEQUAL need an equality flag instead, which the ported
     * assembler supports (pf_names[] in v3d_assembler.h lists
     * .pushz/.pushn/.pushc). `fcmp.pushz` matches MESA's own lowering for
     * nir_op_feq32/fneu32 (nir_to_vir.c ntq_emit_comparison), which
     * likewise uses FCMP+PUSHZ and inverts the cond for not-equal rather
     * than emitting a subtract.
     *
     * GL_NEVER discards unconditionally, so it needs no comparison at all --
     * just a bare `setmsf -, 0`. It is a real shader rather than a
     * draw-call skip because it is correct and uniform with the rest of the
     * family, not because it is observably different: every alphatest
     * variant carries a `tlbu` passthrough Z write, and draw.c sets
     * turn_off_early_z_test AND fragment_shader_does_z_writes for every
     * draw where (alphatest || smooth_alphatest). Discarded fragments
     * therefore write no depth, so a discard-everything shader and a
     * skipped draw produce the same buffer.
     *
     * READ THIS BEFORE ADDING A VARIANT: any new discard-capable shader
     * reached by those predicates MUST carry the `or tlbu, rf10, rf10 ; nop`
     * write, placed after the last `setmsf` and before the first `vfpack tlb`.
     * The flag and the instruction must stay in lockstep -- setting the flag
     * for a shader without the write means NO depth is written at all. The
     * assembler CANNOT catch a mistake here: the three TLB-Z ordering rules,
     * including SETMSF_AFTER_TLB_Z_WRITE, are dead code in this port (the
     * is_tlb_z_write tracking is commented out in v3d_assembler.h).
     *
     * GL_ALWAYS gets no variant here by design: it is exactly "alpha test
     * disabled", handled in draw.c by clearing the alphatest/smooth_alphatest
     * predicates themselves so the whole sibling family of uniform-stream
     * guards stays in lockstep with frag_code_offset (changing only the
     * shader selection there would desynchronise the fragment uniform
     * stream from frag_code_offset).
     *
     * Same 3-shape scope as the GEQUAL/GREATER families (flat untextured,
     * flat textured, textured+smooth).
     */
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST_NEVER,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST_NEVER,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST_NEVER,

    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST_LESS,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST_LESS,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST_LESS,

    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST_EQUAL,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST_EQUAL,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST_EQUAL,

    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST_LEQUAL,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST_LEQUAL,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST_LEQUAL,

    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_ALPHATEST_NOTEQUAL,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_ALPHATEST_NOTEQUAL,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST_NOTEQUAL,

    /* Combined fog + alpha test, slots 62..75. Fourteen variants: 7
     * compare functions x 2 shapes (untextured, textured); the
     * textured+smooth shape is in the smooth fog group below. */
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST,

    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST_GREATER,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST_GREATER,

    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST_LESS,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST_LESS,

    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST_EQUAL,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST_EQUAL,

    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST_LEQUAL,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST_LEQUAL,

    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST_NOTEQUAL,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST_NOTEQUAL,

    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_FOG_ALPHATEST_NEVER,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_FOG_ALPHATEST_NEVER,

    /* Smooth fog, slots 76..84. Nine variants: untextured, textured, and
     * textured with each of the seven alpha compare functions. The untextured
     * smooth alphatest variants are no longer missing: they are the group at
     * the end of this enum. */
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_FOG,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG,

    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST_GREATER,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST_LESS,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST_EQUAL,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST_LEQUAL,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST_NOTEQUAL,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_FOG_ALPHATEST_NEVER,

    /* Multitexture fog, slots 85..87. Only the env modes that do NOT
     * software-blend. The blending multitexture variants are absent on
     * purpose: fog must reach the SOURCE colour before a blend reads the
     * destination, so those need the fog block mid-shader, not before the
     * vfpacks -- they are in the software-blend fog group below. */
    V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_FOG,
    V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_DECAL_FOG,
    V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_REPLACE_FOG,

    /* THE TWO LIT VERTEX SHADERS, code slots 64 and 65. They are VERTEX
     * shaders declared among the fragment ones, which is placement and nothing
     * more: a code slot is type-agnostic.
     *
     * They were RENAMED IN PLACE rather than deleted and appended, and that
     * is the whole trick: the enum count stays 114, so
     * V3D_MAX_SHADER_VARIANTS below never moves and not one surviving offset
     * literal in the 193-literal dispatch chain is renumbered. (It also kept
     * draw.c's 114 * 1024 allocation literal still, which no longer exists --
     * the allocation sizes itself from the packed shaders.) A slot is
     * type-agnostic
     * (draw.c holds the fragment and vertex code addresses as two
     * independent fields), so a reclaimed fragment slot hosts a vertex
     * shader perfectly well. See the derivation comment on the arrays
     * themselves in v3d_assembler.c. */
    V3D_SHADER_VARIANT_VERTEX_LIT,
    V3D_SHADER_VARIANT_VERTEX_LIT_TEXTURED,
    /* Software-blend fog, slots 90..101. Fog goes in BEFORE the first
     * ldtlb in these -- GL applies fog to the fragment and blends
     * afterwards, so fogging the blended result would fog the destination's
     * contribution too. Only the variants whose blend math leaves rf11-rf19
     * alone and finishes the source colour before the first ldtlb are here;
     * the other eight need different scratch registers and are in the next
     * two groups. */

    /* Register-constrained blend fog, slots 102..106. These five use
     * rf11-rf19 for their own blend math, so each carries a fog block on
     * registers allocated from its own unused set instead of the shared one. */

    /* Untextured flat blend fog, slots 107..109. */

    /* Smooth points, slots 110..113: GL_POINT_SMOOTH for the four
     * base shapes (untextured/textured x flat/smooth colour). They read the
     * point coordinate as implicit varyings, so draw.c pairs them with a
     * shader record that enables those -- see v3d_assembler.c. */
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_POINT_SMOOTH,
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_POINT_SMOOTH,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_POINT_SMOOTH,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_POINT_SMOOTH,

    /* Untextured smooth alpha test: seven compare functions without fog, then
     * seven with. They close the gap the smooth-fog comment above used to
     * record, and they are what lets `lit` stop excluding alpha test -- a lit
     * draw's fragment stage is smooth, so an untextured lit draw with the alpha
     * test on had no shader to reach. */
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_ALPHATEST,
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_ALPHATEST_GREATER,
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_ALPHATEST_LESS,
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_ALPHATEST_EQUAL,
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_ALPHATEST_LEQUAL,
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_ALPHATEST_NOTEQUAL,
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_ALPHATEST_NEVER,
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_FOG_ALPHATEST,
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_FOG_ALPHATEST_GREATER,
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_FOG_ALPHATEST_LESS,
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_FOG_ALPHATEST_EQUAL,
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_FOG_ALPHATEST_LEQUAL,
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_FOG_ALPHATEST_NOTEQUAL,
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_FOG_ALPHATEST_NEVER,

    /* Lit with a REAL per-vertex w. The lit twin of
     * VERTEX_SMOOTH_TEXTURED_CLIPSPACE, derived from VERTEX_LIT_TEXTURED by
     * reading w from the VPM and shifting every later input index by one.
     * Appended, so no existing ordinal moves. */
    V3D_SHADER_VARIANT_VERTEX_LIT_REALW,

    /* Lit MULTITEXTURE, both stages. The vertex shader is VERTEX_LIT_TEXTURED
     * plus unit 1's texcoord pair; the fragment shader is
     * FRAGMENT_MULTITEXTURE plus the lit colour as four varyings, which that
     * path had no input for at all. MODULATE only: decal and the fogged
     * combines need their own and have none. */
    V3D_SHADER_VARIANT_VERTEX_LIT_MULTITEXTURE,
    V3D_SHADER_VARIANT_FRAGMENT_LIT_MULTITEXTURE,

    /* GL_COLOR_MATERIAL: one twin per lit vertex shape. The colour
     * attribute record comes back and each term gains its K1 half.
     * No fragment twin -- the lit colour already arrives as varyings. */
    V3D_SHADER_VARIANT_VERTEX_LIT_COLORMATERIAL,
    V3D_SHADER_VARIANT_VERTEX_LIT_TEXTURED_COLORMATERIAL,
    V3D_SHADER_VARIANT_VERTEX_LIT_REALW_COLORMATERIAL,
    V3D_SHADER_VARIANT_VERTEX_LIT_MULTITEXTURE_COLORMATERIAL,

    /* GL_BLEND and GL_ADD on unit 0, for the two base textured shapes.
     * The other modes need no shader: GL_MODULATE is the combine these
     * hosts already compute, and REPLACE and DECAL are faked by feeding
     * white as the primary colour (draw.c, replace_white). */
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_COLORMOD_ENVADD,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_COLORMOD_ENVBLEND,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ENVADD,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ENVBLEND,

    V3D_MAX_SHADER_VARIANTS = 95
};

/*
 * Shared storage for every assembled shader variant -- static (not a
 * stack-local, not a function-local of any kind), since this is
 * genuinely persistent state other code needs to reach, unlike the
 * purely transient per-call scratch buffers inside v3d_assembler.c
 * (which stay private to that file -- see its own comment on
 * unpackedInstructions/assembleArguments for why those are static for a
 * different reason). Defined once in v3d_assembler.c.
 */
extern V3DAssembledShader v3d_shader_variants[V3D_MAX_SHADER_VARIANTS];

/*
 * Assembles the built-in shaders via the ported QPU assembler, into
 * v3d_shader_variants[]. Returns TRUE on success, FALSE if any shader
 * fails to assemble/pack/validate (details go to D()/kprintf, matching
 * PoC's own v3d_assemble() error reporting).
 */
int v3d_assemble_builtin_shaders(V3DDevice* device);

/*
 * TEST-ONLY, AND NOT IN THE DRIVER. Everything below exists for
 * shader_assembler_test and is compiled only when MGLV3D_ASSEMBLE_REPORT is
 * defined -- demos/build_demo_gcc.sh --assembler-report compiles
 * v3d_assembler.c a second time with that flag and links it ahead of the
 * archive. The library carries none of it.
 *
 * What it buys: the test links the no-logging archive, where D() is gone, so a
 * failure there otherwise gives a bare FALSE with nothing to name. And with
 * v3d_assemble_report_all set, assembly carries on past a failure and records
 * each one, so a run lists every broken shader instead of only the first --
 * v3d_assemble_builtin_shaders then returns TRUE even though shaders failed, so
 * check v3d_assemble_failures, not the return value. The driver always stops at
 * the first, which is the only thing it can sensibly do.
 */
#ifdef MGLV3D_ASSEMBLE_REPORT
extern const char* v3d_assemble_error_shader;   /* the most recent failure */
extern const char* v3d_assemble_error_what;
extern const char* v3d_assemble_error_text;
extern int         v3d_assemble_error_index;    /* -1 until something fails */

#define V3D_ASSEMBLE_MAX_FAILURES 16

extern int         v3d_assemble_report_all;     /* caller sets this to collect all */
extern int         v3d_assemble_failures;       /* how many failed, may exceed the array */
extern const char* v3d_assemble_failed_name[V3D_ASSEMBLE_MAX_FAILURES];
extern const char* v3d_assemble_failed_what[V3D_ASSEMBLE_MAX_FAILURES];
extern const char* v3d_assemble_failed_text[V3D_ASSEMBLE_MAX_FAILURES];
extern int         v3d_assemble_failed_index[V3D_ASSEMBLE_MAX_FAILURES];

/*
 * Assemble one shader the driver does not carry, into the caller's own
 * V3DAssembledShader. shader_assembler_test uses this for the textured fragment
 * shader: it is the only shader with checked-in known-good machine code
 * (milestone1_shaders.h, copied from the PoC, so it predates this assembler),
 * but no selector reaches it, so its assembly text lives with the test.
 */
int v3d_assemble_shader(V3DDevice* device, const char* name,
                        const char** assemblyLines, int numAssemblyLines,
                        V3DAssembledShader* out);

#endif /* MGLV3D_ASSEMBLE_REPORT */

#endif /* V3D_SHADER_ASSEMBLER_H */

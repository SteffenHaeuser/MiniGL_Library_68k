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
 * Not a hardware ceiling -- purely this project's own buffer-sizing
 * choice, matched to the 1024-byte code slot gl_EnsureShaders (draw.c)
 * gives each variant (1024 bytes / 8 bytes-per-instruction = 128). A
 * longer shader fails to assemble ("ran out of space"), so
 * v3d_assemble_builtin_shaders returns FALSE.
 */
#define V3D_SHADER_MAX_INSTRUCTIONS 128

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
 *   3. the 114 * 1024 literal passed to v3d_mem_alloc in gl/src/draw.c's
 *      gl_EnsureShaders -- the slot buffer is likewise exactly full, the
 *      last slot ending precisely at the end of the allocation, so a new
 *      slot without this bump writes past the end of shader_code_mem.
 *   4. the parallel lists in gl_EnsureShaders (pointer declarations,
 *      shader_vex + OFFSET assignment, &v3d_shader_variants[] binding, and
 *      the byteswap64 upload loop).
 *   5. the v3d_assemble_one_shader call in v3d_assemble_builtin_shaders
 *      (v3d_assembler.c) that fills the entry -- without it the entry
 *      stays all-zero.
 * Nothing enforces any of this at compile time; it is maintained by hand.
 *
 * Slot stride is 1024 bytes = V3D_SHADER_MAX_INSTRUCTIONS (128) * 8, on
 * purpose -- raising the instruction cap without raising draw.c's stride
 * would let a long shader overwrite the NEXT variant's slot silently.
 */
enum {
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED = 0,
    V3D_SHADER_VARIANT_VERTEX_TEXTURED,
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
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_BLEND,

    /* A second software-blend variant, additive (GL_ONE/GL_ONE) instead
     * of the LERP above -- see v3d_assembler.c's comment on
     * g_fragment_shader_untextured_blend_add_assembly. Fragment-only,
     * reuses VERTEX_TEXTURED/COORDINATE_TEXTURED unchanged, same as the
     * LERP variant above. */
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_BLEND_ADD,

    /* Smooth-capable additive blend -- see v3d_assembler.c's comment on
     * g_fragment_shader_untextured_smooth_blend_add_assembly. Takes a
     * different color per vertex, not the single flat color the plain
     * additive variant above handles. Fragment-only, reuses VERTEX_SMOOTH
     * unchanged (same varying layout the smooth-untextured fragment
     * shader consumes). */
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_BLEND_ADD,

    /* Smooth-capable LERP (SRC_ALPHA/ONE_MINUS_SRC_ALPHA) blend -- see
     * v3d_assembler.c's comment on
     * g_fragment_shader_untextured_smooth_blend_assembly. Takes a
     * different color per vertex, which the flat/uniform-color LERP
     * variant can't handle. Fragment-only, reuses VERTEX_SMOOTH
     * unchanged. */
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_BLEND,

    /* TEXTURED software blend -- none of the untextured swblend variants
     * above can sample a texture at all. See v3d_assembler.c's comment
     * on g_fragment_shader_textured_blend_assembly. */
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_BLEND,

    /* The one three-way combination (textured + SMOOTH + blend) none of
     * the variants above cover -- each requires either !textured or
     * !smooth. See v3d_assembler.c's comment on
     * g_fragment_shader_textured_smooth_blend_assembly -- reuses the
     * smooth+textured shader's per-vertex modulation feeding directly
     * into FRAGMENT_TEXTURED_BLEND's ldtlb LERP math. */
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_BLEND,

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
    V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_MODULATE_BLEND,
    V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_DECAL_BLEND,
    V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_REPLACE_BLEND,

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
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_PADDED_FLAT_TEST,

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
    V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_MODULATE_TRANSLUCENT,

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

    /* Textured + smooth + GL_ONE/GL_ONE additive -- see v3d_assembler.c's
     * comment on g_fragment_shader_textured_smooth_blend_add_assembly
     * for the full derivation. */
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_BLEND_ADD,

    /* 4 textured+smooth+!multitextured software-blend variants for
     * src=DSTCOLOR, one per dst factor (ZERO, ONE, SRCCOLOR,
     * INVDSTALPHA). None of the swblend-family shaders above cover any
     * DSTCOLOR-sourced blend. Each reuses VERTEX_SMOOTH_TEXTURED
     * unchanged (same front-section/varying layout as
     * FRAGMENT_TEXTURED_SMOOTH_BLEND -- see v3d_assembler.c's comments on
     * these 4 shaders for the exact splice). */
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_ZERO,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_ONE,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_SRCCOLOR,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_INVDSTALPHA,

    /* src=ZERO, dst=INVSRCCOLOR (result = Cd*(1-Cs)),
     * textured+smooth+!multitextured. Same splice technique, same
     * VERTEX_SMOOTH_TEXTURED reuse as the DSTCOLOR family above. */
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ZERO_INVSRCCOLOR,

    /* src=DSTCOLOR, dst=SRCALPHA (result = Cd*(Cs+Cs.a)) -- the DSTCOLOR
     * family, with a dst factor not covered by the 4 above,
     * textured+smooth+!multitextured. Same splice technique, same
     * VERTEX_SMOOTH_TEXTURED reuse. */
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_SRCALPHA,

    /* Two more factor pairs, src=ONE and src=INVSRCALPHA:
     *   ONE_INVSRCALPHA:      src=ONE,         dst=INVSRCALPHA
     *                         result = Cs + Cd*(1-Cs.a)
     *   INVSRCALPHA_SRCALPHA: src=INVSRCALPHA, dst=SRCALPHA
     *                         result = Cs*(1-Cs.a) + Cd*Cs.a
     * Same splice technique, same VERTEX_SMOOTH_TEXTURED reuse as the
     * rest of this family. */
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ONE_INVSRCALPHA,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_INVSRCALPHA_SRCALPHA,

    /* GL_ALPHA_TEST (GL_GREATER) combined with GL_SMOOTH shading,
     * textured -- see v3d_assembler.c's comment on
     * g_fragment_shader_textured_smooth_alphatest_greater_assembly.
     * draw.c's flat `alphatest` predicate requires !smooth, so smooth
     * draws need shaders of their own. */
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST_GREATER,

    /* Same shape, GL_GEQUAL. */
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ALPHATEST,

    /* GL_SRC_ALPHA/GL_ONE (alpha-weighted additive) software blend, which
     * none of the variants above cover (every SRCALPHA-source variant
     * pairs with INVSRCALPHA, never ONE). Same
     * splice technique as the rest of this family -- see
     * v3d_assembler.c's comments on these 3 shaders for the exact
     * derivation (LERP shader's src*alpha multiply + additive shader's
     * dst-add-and-clamp tail, no invAlpha term since dst factor is ONE
     * not 1-alpha). Mirrors the ONE/ONE "add" family's exact 3-shape
     * scope (flat, smooth, textured+smooth). */
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_BLEND_SRCALPHA_ONE,
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_BLEND_SRCALPHA_ONE,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_BLEND_SRCALPHA_ONE,

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
     * textured with each of the seven alpha compare functions. There is
     * no untextured smooth alphatest entry because no such shader exists
     * anywhere -- alpha test on an untextured smooth draw matches neither
     * the `alphatest` predicate (needs !smooth) nor `smooth_alphatest`
     * (needs textured), so alpha test is not applied to an untextured
     * smooth draw -- a known gap. */
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

    /* Software-blend fog, slots 88..101. Fog goes in BEFORE the first
     * ldtlb in these -- GL applies fog to the fragment and blends
     * afterwards, so fogging the blended result would fog the destination's
     * contribution too. Only the variants whose blend math leaves rf11-rf19
     * alone and finishes the source colour before the first ldtlb are here;
     * the other eight need different scratch registers and are in the next
     * two groups. */
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_BLEND_FOG,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_BLEND_FOG,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_ZERO_FOG,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_ONE_FOG,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_SRCCOLOR_FOG,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_INVDSTALPHA_FOG,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ZERO_INVSRCCOLOR_FOG,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_DSTCOLOR_SRCALPHA_FOG,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_ONE_INVSRCALPHA_FOG,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_INVSRCALPHA_SRCALPHA_FOG,
    V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_MODULATE_TRANSLUCENT_FOG,
    V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_MODULATE_BLEND_FOG,
    V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_DECAL_BLEND_FOG,
    V3D_SHADER_VARIANT_FRAGMENT_MULTITEXTURE_REPLACE_BLEND_FOG,

    /* Register-constrained blend fog, slots 102..106. These five use
     * rf11-rf19 for their own blend math, so each carries a fog block on
     * registers allocated from its own unused set instead of the shared one. */
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_BLEND_FOG,
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_BLEND_ADD_FOG,
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_BLEND_SRCALPHA_ONE_FOG,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_BLEND_ADD_FOG,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_BLEND_SRCALPHA_ONE_FOG,

    /* Untextured flat blend fog, slots 107..109. */
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_BLEND_FOG,
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_BLEND_ADD_FOG,
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_BLEND_SRCALPHA_ONE_FOG,

    /* Smooth points, slots 110..113: GL_POINT_SMOOTH for the four
     * base shapes (untextured/textured x flat/smooth colour). They read the
     * point coordinate as implicit varyings, so draw.c pairs them with a
     * shader record that enables those -- see v3d_assembler.c. */
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_POINT_SMOOTH,
    V3D_SHADER_VARIANT_FRAGMENT_UNTEXTURED_SMOOTH_POINT_SMOOTH,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_POINT_SMOOTH,
    V3D_SHADER_VARIANT_FRAGMENT_TEXTURED_SMOOTH_POINT_SMOOTH,

    V3D_MAX_SHADER_VARIANTS = 114
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

#endif /* V3D_SHADER_ASSEMBLER_H */

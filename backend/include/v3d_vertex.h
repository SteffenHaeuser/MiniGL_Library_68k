/*
 * (C) 2025-2026 Dennis van der Boon
 */

#ifndef V3D_VERTEX_H
#define V3D_VERTEX_H

#include "v3d_types.h"

/*
 * V3DVertex -- per-vertex position and texture coordinates. MGLVertex_t
 * (gl/include/mgl/vertexbuffer.h) embeds it as its `v` member.
 *
 * V3D's GL Shader State Attribute Record (glShaderStateAttributeRecord,
 * v3d_commands.c) describes an attribute as: address + stride + vec_size +
 * type + normalized/signed-int flags + separate "values read by vertex
 * shader" / "values read by coordinate shader" counts + instance divisor.
 * The records do not point into V3DVertex: draw.c writes each draw's
 * attribute values, byte-swapped, into separate float buffers claimed
 * from state_buf (posbuf, texbuf, and texbuf2 for some shader variants)
 * and points one record at each.
 *
 * Record formats (draw.c), all v3d_ATTRIBUTE_FLOAT:
 *   - posbuf: position, v3d_VEC_3/4 -- read by BOTH the vertex shader and
 *     the coordinate/binning shader (binning needs position to compute
 *     tile coverage).
 *   - texbuf (2 or 4 floats per vertex) and texbuf2 (2): texture
 *     coordinates and/or vertex color, zeros when a draw needs neither;
 *     read by the vertex shader only (novrbcs=0; the coordinate/binning
 *     pass doesn't need them). When multitextured, both units'
 *     coordinates (MAX_TEXUNIT in gl/include/mgl/gl.h is 2) share texbuf.
 *     Color comes from MGLVertex_t's float color; V3DVertex's r/g/b/a are
 *     not used.
 *
 * For non-position attributes, how vec_size relates to the number of
 * values read is not pinned down: draw.c declares texbuf as v3d_VEC_4
 * even when each vertex carries only 2 floats (novrbvs 2, stride 8).
 */

typedef struct V3DVertex {
    float x, y, z, w;          /* object-space position, as passed to glVertex */
    v3d_u8 r, g, b, a;          /* packed diffuse color, normalized UBYTE */
    float u0, v0;               /* texcoord, texture unit 0 */
    float u1, v1;               /* texcoord, texture unit 1 (MAX_TEXUNIT == 2) */
} V3DVertex;

/*
 * V3DAttribDesc -- a friendly (non-bit-packed) intermediate representation
 * of an attribute record, built from offsetof/sizeof against V3DVertex (see
 * V3D_ATTRIB_OFFSET/V3D_ATTRIB_STRIDE below). Nothing in this repository
 * builds one: draw.c calls glShaderStateAttributeRecord directly.
 *
 * The real record -- v3d_gl_shader_state_attribute_record, v3d_hw.h --
 * is a 16-byte PACKED bitfield struct:
 *   address:32, read_as_int_uint:1, normalized_int_type:1, signed_int_type:1,
 *   type:3 (values 1-7 -- v3d_ATTRIBUTE_HALF_FLOAT..INT2101010;
 *   0 is unused/reserved, not a valid default), vec_size:2 (v3d_VEC_1..VEC_4,
 *   VEC_4 encodes as 0), number_of_values_read_by_vertex_shader:4,
 *   number_of_values_read_by_coordinate_shader:4, instance_divisor:16,
 *   stride:32, maximum_index:32.
 * V3DAttribDesc is deliberately NOT bit-identical to that -- it's a plain
 * struct for backend code to build/inspect easily; it has to be translated
 * into the real packed form (pass its fields, with a buffer base address
 * added to offset, to glShaderStateAttributeRecord, or pack
 * v3d_gl_shader_state_attribute_record directly), not raw-memcpy'd.
 */
typedef struct V3DAttribDesc {
    v3d_u32 offset;                 /* byte offset within one vertex -- combined with a buffer base address to get the record's `address` */
    v3d_u32 stride;                  /* bytes between consecutive vertices (== sizeof(V3DVertex) for AoS) */
    v3d_u32 maximum_index;           /* bounds sentinel -- draw.c's records use 0xFFFFFF */
    v3d_u8  read_as_int_uint;        /* raiu */
    v3d_u8  normalized_int_type;     /* nit  -- e.g. TRUE for a normalized UBYTE attribute */
    v3d_u8  signed_int_type;         /* sit */
    v3d_u8  vec_size;                 /* v3d_VEC_1..v3d_VEC_4; note VEC_4 encodes as 0 (2-bit hw field) */
    v3d_u8  type;                     /* v3d_ATTRIBUTE_FLOAT / _BYTE / _SHORT / ... ; valid range 1-7, 0 is not a real type */
    v3d_u8  values_read_by_vertex_shader;      /* novrbvs -- 4-bit hw field, practical range 0-4 */
    v3d_u8  values_read_by_coordinate_shader;  /* novrbcs -- 4-bit hw field, practical range 0-4; 0 for attributes binning doesn't need */
    v3d_u16 instance_divisor;         /* matches the hw field's 16-bit width exactly */
} V3DAttribDesc;

/* Build a V3DAttribDesc's offset+stride from a field of V3DVertex; caller fills the rest. */
#define V3D_ATTRIB_OFFSET(field) ((v3d_u32)((char*)&((V3DVertex*)0)->field - (char*)0))
#define V3D_ATTRIB_STRIDE        ((v3d_u32)sizeof(V3DVertex))

#endif /* V3D_VERTEX_H */

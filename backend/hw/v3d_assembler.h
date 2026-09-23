// v3dAssembler.h
// Broadcom VideoCore VI V3D shader code assembler, disassembler, and instruction information.
//
// Ported and modified by Macoy Madson from Mesa source (https://mesa3d.org/) into standalone
// single-header.
//
// Copyright © 2024 Macoy Madson
// This port shares the same license as the original Broadcom code:
/*
 * Copyright © 2016 Broadcom
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
// ffs() and ffsll() have the following copyright and license:
/**************************************************************************
 *
 * Copyright 2008 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

//
// Usage
//
// To use, do the following in ONE source file in your project:
//
// #define V3D_ASSEMBLER_IMPLEMENTATION
// #include "v3dAssembler.h"
// #undef V3D_ASSEMBLER_IMPLEMENTATION
//
// Required defines:
// #define v3d_memcmp
// #define v3d_vsnprintf
//
// Optional defines (if unset, will do nothing):
// #define v3d_assert(condition)
// #define v3d_unreachable(message)
// #define V3D_STATIC_ASSERT(condition)
#ifndef V3DASSEMBLER_H
#define V3DASSEMBLER_H

// For v3d_vsnprintf. This should be built in to your C compiler.
/* #include <stdarg.h> */

// NOTE: Assumes 64 bit.
// (TODO macoy) Make these consistent with v3d.h
//typedef char v3d_bool;
typedef unsigned char v3d_uint8;
typedef unsigned int v3d_uint32;
typedef unsigned long long v3d_uint64;
typedef int v3d_int32;
typedef char v3d_bool;
void kprintf(STRPTR format, ...);
#define E(x) do { kprintf x; } while (0)

//
// Interface
//

// >>> qpu_instr.h

/**
 * Definitions of the unpacked form of QPU instructions.  Assembly and
 * disassembly will use this for talking about instructions, with qpu_encode
 * and qpu_decode handling the pack and unpack of the actual 64-bit QPU
 * instruction.
 */

struct v3d_device_info;

struct v3d_qpu_sig { //debugdebug need to check endian
	v3d_bool thrsw:1;
	v3d_bool ldunif:1;
	v3d_bool ldunifa:1;
	v3d_bool ldunifrf:1;
	v3d_bool ldunifarf:1;
	v3d_bool ldtmu:1;
	v3d_bool ldvary:1;
	v3d_bool ldvpm:1;
	v3d_bool ldtlb:1;
	v3d_bool ldtlbu:1;
	v3d_bool ucb:1;
	v3d_bool rotate:1;
	v3d_bool wrtmuc:1;
	v3d_bool small_imm_a:1; /* raddr_a (add a), since V3D 7.x */
	v3d_bool small_imm_b:1; /* raddr_b (add b) */
	v3d_bool small_imm_c:1; /* raddr_c (mul a), since V3D 7.x */
	v3d_bool small_imm_d:1; /* raddr_d (mul b), since V3D 7.x */
};

enum v3d_qpu_cond {
	V3D_QPU_COND_NONE,
	V3D_QPU_COND_IFA,
	V3D_QPU_COND_IFB,
	V3D_QPU_COND_IFNA,
	V3D_QPU_COND_IFNB,
};

enum v3d_qpu_pf {
	V3D_QPU_PF_NONE,
	V3D_QPU_PF_PUSHZ,
	V3D_QPU_PF_PUSHN,
	V3D_QPU_PF_PUSHC,
};

enum v3d_qpu_uf {
	V3D_QPU_UF_NONE,
	V3D_QPU_UF_ANDZ,
	V3D_QPU_UF_ANDNZ,
	V3D_QPU_UF_NORNZ,
	V3D_QPU_UF_NORZ,
	V3D_QPU_UF_ANDN,
	V3D_QPU_UF_ANDNN,
	V3D_QPU_UF_NORNN,
	V3D_QPU_UF_NORN,
	V3D_QPU_UF_ANDC,
	V3D_QPU_UF_ANDNC,
	V3D_QPU_UF_NORNC,
	V3D_QPU_UF_NORC,
};

enum v3d_qpu_waddr {
	V3D_QPU_WADDR_R0 = 0,    /* Reserved on V3D 7.x */
	V3D_QPU_WADDR_R1 = 1,    /* Reserved on V3D 7.x */
	V3D_QPU_WADDR_R2 = 2,    /* Reserved on V3D 7.x */
	V3D_QPU_WADDR_R3 = 3,    /* Reserved on V3D 7.x */
	V3D_QPU_WADDR_R4 = 4,    /* Reserved on V3D 7.x */
	V3D_QPU_WADDR_R5 = 5,    /* V3D 4.x */
	V3D_QPU_WADDR_QUAD = 5,  /* V3D 7.x */
	V3D_QPU_WADDR_NOP = 6,
	V3D_QPU_WADDR_TLB = 7,
	V3D_QPU_WADDR_TLBU = 8,
	V3D_QPU_WADDR_TMU = 9,   /* V3D 3.x */
	V3D_QPU_WADDR_UNIFA = 9, /* V3D 4.x */
	V3D_QPU_WADDR_TMUL = 10,
	V3D_QPU_WADDR_TMUD = 11,
	V3D_QPU_WADDR_TMUA = 12,
	V3D_QPU_WADDR_TMUAU = 13,
	V3D_QPU_WADDR_VPM = 14,
	V3D_QPU_WADDR_VPMU = 15,
	V3D_QPU_WADDR_SYNC = 16,
	V3D_QPU_WADDR_SYNCU = 17,
	V3D_QPU_WADDR_SYNCB = 18,
	V3D_QPU_WADDR_RECIP = 19,  /* Reserved on V3D 7.x */
	V3D_QPU_WADDR_RSQRT = 20,  /* Reserved on V3D 7.x */
	V3D_QPU_WADDR_EXP = 21,    /* Reserved on V3D 7.x */
	V3D_QPU_WADDR_LOG = 22,    /* Reserved on V3D 7.x */
	V3D_QPU_WADDR_SIN = 23,    /* Reserved on V3D 7.x */
	V3D_QPU_WADDR_RSQRT2 = 24, /* Reserved on V3D 7.x */
	V3D_QPU_WADDR_TMUC = 32,
	V3D_QPU_WADDR_TMUS = 33,
	V3D_QPU_WADDR_TMUT = 34,
	V3D_QPU_WADDR_TMUR = 35,
	V3D_QPU_WADDR_TMUI = 36,
	V3D_QPU_WADDR_TMUB = 37,
	V3D_QPU_WADDR_TMUDREF = 38,
	V3D_QPU_WADDR_TMUOFF = 39,
	V3D_QPU_WADDR_TMUSCM = 40,
	V3D_QPU_WADDR_TMUSF = 41,
	V3D_QPU_WADDR_TMUSLOD = 42,
	V3D_QPU_WADDR_TMUHS = 43,
	V3D_QPU_WADDR_TMUHSCM = 44,
	V3D_QPU_WADDR_TMUHSF = 45,
	V3D_QPU_WADDR_TMUHSLOD = 46,
	V3D_QPU_WADDR_R5REP = 55, /* V3D 4.x */
	V3D_QPU_WADDR_REP = 55,   /* V3D 7.x */
};

struct v3d_qpu_flags {
	enum v3d_qpu_cond ac, mc;
	enum v3d_qpu_pf apf, mpf;
	enum v3d_qpu_uf auf, muf;
};

enum v3d_qpu_add_op {
	V3D_QPU_A_FADD,
	V3D_QPU_A_FADDNF,
	V3D_QPU_A_VFPACK,
	V3D_QPU_A_ADD,
	V3D_QPU_A_SUB,
	V3D_QPU_A_FSUB,
	V3D_QPU_A_MIN,
	V3D_QPU_A_MAX,
	V3D_QPU_A_UMIN,
	V3D_QPU_A_UMAX,
	V3D_QPU_A_SHL,
	V3D_QPU_A_SHR,
	V3D_QPU_A_ASR,
	V3D_QPU_A_ROR,
	V3D_QPU_A_FMIN,
	V3D_QPU_A_FMAX,
	V3D_QPU_A_VFMIN,
	V3D_QPU_A_AND,
	V3D_QPU_A_OR,
	V3D_QPU_A_XOR,
	V3D_QPU_A_VADD,
	V3D_QPU_A_VSUB,
	V3D_QPU_A_NOT,
	V3D_QPU_A_NEG,
	V3D_QPU_A_FLAPUSH,
	V3D_QPU_A_FLBPUSH,
	V3D_QPU_A_FLPOP,
	V3D_QPU_A_RECIP,
	V3D_QPU_A_SETMSF,
	V3D_QPU_A_SETREVF,
	V3D_QPU_A_NOP,
	V3D_QPU_A_TIDX,
	V3D_QPU_A_EIDX,
	V3D_QPU_A_LR,
	V3D_QPU_A_VFLA,
	V3D_QPU_A_VFLNA,
	V3D_QPU_A_VFLB,
	V3D_QPU_A_VFLNB,
	V3D_QPU_A_FXCD,
	V3D_QPU_A_XCD,
	V3D_QPU_A_FYCD,
	V3D_QPU_A_YCD,
	V3D_QPU_A_MSF,
	V3D_QPU_A_REVF,
	V3D_QPU_A_VDWWT,
	V3D_QPU_A_IID,
	V3D_QPU_A_SAMPID,
	V3D_QPU_A_BARRIERID,
	V3D_QPU_A_TMUWT,
	V3D_QPU_A_VPMSETUP,
	V3D_QPU_A_VPMWT,
	V3D_QPU_A_FLAFIRST,
	V3D_QPU_A_FLNAFIRST,
	V3D_QPU_A_LDVPMV_IN,
	V3D_QPU_A_LDVPMV_OUT,
	V3D_QPU_A_LDVPMD_IN,
	V3D_QPU_A_LDVPMD_OUT,
	V3D_QPU_A_LDVPMP,
	V3D_QPU_A_RSQRT,
	V3D_QPU_A_EXP,
	V3D_QPU_A_LOG,
	V3D_QPU_A_SIN,
	V3D_QPU_A_RSQRT2,
	V3D_QPU_A_LDVPMG_IN,
	V3D_QPU_A_LDVPMG_OUT,
	V3D_QPU_A_FCMP,
	V3D_QPU_A_VFMAX,
	V3D_QPU_A_FROUND,
	V3D_QPU_A_FTOIN,
	V3D_QPU_A_FTRUNC,
	V3D_QPU_A_FTOIZ,
	V3D_QPU_A_FFLOOR,
	V3D_QPU_A_FTOUZ,
	V3D_QPU_A_FCEIL,
	V3D_QPU_A_FTOC,
	V3D_QPU_A_FDX,
	V3D_QPU_A_FDY,
	V3D_QPU_A_STVPMV,
	V3D_QPU_A_STVPMD,
	V3D_QPU_A_STVPMP,
	V3D_QPU_A_ITOF,
	V3D_QPU_A_CLZ,
	V3D_QPU_A_UTOF,

	/* V3D 7.x */
	V3D_QPU_A_FMOV,
	V3D_QPU_A_MOV,
	V3D_QPU_A_VPACK,
	V3D_QPU_A_V8PACK,
	V3D_QPU_A_V10PACK,
	V3D_QPU_A_V11FPACK,
};

enum v3d_qpu_mul_op {
	V3D_QPU_M_ADD,
	V3D_QPU_M_SUB,
	V3D_QPU_M_UMUL24,
	V3D_QPU_M_VFMUL,
	V3D_QPU_M_SMUL24,
	V3D_QPU_M_MULTOP,
	V3D_QPU_M_FMOV,
	V3D_QPU_M_MOV,
	V3D_QPU_M_NOP,
	V3D_QPU_M_FMUL,

	/* V3D 7.x */
	V3D_QPU_M_FTOUNORM16,
	V3D_QPU_M_FTOSNORM16,
	V3D_QPU_M_VFTOUNORM8,
	V3D_QPU_M_VFTOSNORM8,
	V3D_QPU_M_VFTOUNORM10LO,
	V3D_QPU_M_VFTOUNORM10HI,
};

enum v3d_qpu_output_pack {
	V3D_QPU_PACK_NONE,
	/**
	 * Convert to 16-bit float, put in low 16 bits of destination leaving
	 * high unmodified.
	 */
	V3D_QPU_PACK_L,
	/**
	 * Convert to 16-bit float, put in high 16 bits of destination leaving
	 * low unmodified.
	 */
	V3D_QPU_PACK_H,
};

enum v3d_qpu_input_unpack {
	/**
	 * No-op input unpacking.  Note that this enum's value doesn't match
	 * the packed QPU instruction value of the field (we use 0 so that the
	 * default on new instruction creation is no-op).
	 */
	V3D_QPU_UNPACK_NONE,
	/** Absolute value.  Only available for some operations. */
	V3D_QPU_UNPACK_ABS,
	/** Convert low 16 bits from 16-bit float to 32-bit float. */
	V3D_QPU_UNPACK_L,
	/** Convert high 16 bits from 16-bit float to 32-bit float. */
	V3D_QPU_UNPACK_H,

	/** Convert to 16f and replicate it to the high bits. */
	V3D_QPU_UNPACK_REPLICATE_32F_16,

	/** Replicate low 16 bits to high */
	V3D_QPU_UNPACK_REPLICATE_L_16,

	/** Replicate high 16 bits to low */
	V3D_QPU_UNPACK_REPLICATE_H_16,

	/** Swap high and low 16 bits */
	V3D_QPU_UNPACK_SWAP_16,

	/** Convert low 16 bits from 16-bit integer to unsigned 32-bit int */
	V3D_QPU_UNPACK_UL,
	/** Convert high 16 bits from 16-bit integer to unsigned 32-bit int */
	V3D_QPU_UNPACK_UH,
	/** Convert low 16 bits from 16-bit integer to signed 32-bit int */
	V3D_QPU_UNPACK_IL,
	/** Convert high 16 bits from 16-bit integer to signed 32-bit int */
	V3D_QPU_UNPACK_IH,
};

enum v3d_qpu_mux {
	V3D_QPU_MUX_R0,
	V3D_QPU_MUX_R1,
	V3D_QPU_MUX_R2,
	V3D_QPU_MUX_R3,
	V3D_QPU_MUX_R4,
	V3D_QPU_MUX_R5,
	V3D_QPU_MUX_A,
	V3D_QPU_MUX_B,
};

struct v3d_qpu_input {
	union {
		enum v3d_qpu_mux mux; /* V3D 4.x */
		v3d_uint8 raddr; /* V3D 7.x */
	} input;            /* debugdebug */
	enum v3d_qpu_input_unpack unpack;
};

struct v3d_qpu_alu_instr {
	struct {
		enum v3d_qpu_add_op op;
		struct v3d_qpu_input a, b;
		v3d_uint8 waddr;
		v3d_bool magic_write;
		enum v3d_qpu_output_pack output_pack;
	} add;

	struct {
		enum v3d_qpu_mul_op op;
		struct v3d_qpu_input a, b;
		v3d_uint8 waddr;
		v3d_bool magic_write;
		enum v3d_qpu_output_pack output_pack;
	} mul;
};

enum v3d_qpu_branch_cond {
	V3D_QPU_BRANCH_COND_ALWAYS,
	V3D_QPU_BRANCH_COND_A0,
	V3D_QPU_BRANCH_COND_NA0,
	V3D_QPU_BRANCH_COND_ALLA,
	V3D_QPU_BRANCH_COND_ANYNA,
	V3D_QPU_BRANCH_COND_ANYA,
	V3D_QPU_BRANCH_COND_ALLNA,
};

enum v3d_qpu_msfign {
	/** Ignore multisample flags when determining branch condition. */
	V3D_QPU_MSFIGN_NONE,
	/**
	 * If no multisample flags are set in the lane (a pixel in the FS, a
	 * vertex in the VS), ignore the lane's condition when computing the
	 * branch condition.
	 */
	V3D_QPU_MSFIGN_P,
	/**
	 * If no multisample flags are set in a 2x2 quad in the FS, ignore the
	 * quad's a/b conditions.
	 */
	V3D_QPU_MSFIGN_Q,
};

enum v3d_qpu_branch_dest {
	V3D_QPU_BRANCH_DEST_ABS,
	V3D_QPU_BRANCH_DEST_REL,
	V3D_QPU_BRANCH_DEST_LINK_REG,
	V3D_QPU_BRANCH_DEST_REGFILE,
};

struct v3d_qpu_branch_instr {
	enum v3d_qpu_branch_cond cond;
	enum v3d_qpu_msfign msfign;

	/** Selects how to compute the new IP if the branch is taken. */
	enum v3d_qpu_branch_dest bdi;

	/**
	 * Selects how to compute the new uniforms pointer if the branch is
	 * taken.  (ABS/REL implicitly load a uniform and use that)
	 */
	enum v3d_qpu_branch_dest bdu;

	/**
	 * If set, then udest determines how the uniform stream will branch,
	 * otherwise the uniform stream is left as is.
	 */
	v3d_bool ub;

	v3d_uint8 raddr_a;

	v3d_uint32 offset;
};

enum v3d_qpu_instr_type {
	V3D_QPU_INSTR_TYPE_ALU,
	V3D_QPU_INSTR_TYPE_BRANCH,
};

struct v3d_qpu_instr {
	enum v3d_qpu_instr_type type;

	struct v3d_qpu_sig sig;
	v3d_uint8 sig_addr;
	v3d_bool sig_magic; /* If the signal writes to a magic address */
	v3d_uint8 raddr_a; /* V3D 4.x */
	v3d_uint8 raddr_b; /* V3D 4.x (holds packed small immediate in 7.x too) */
	struct v3d_qpu_flags flags;

	union {
		struct v3d_qpu_alu_instr alu;
		struct v3d_qpu_branch_instr branch;
	}instr; /* debugdebug */
};

const char *v3d_qpu_magic_waddr_name(const struct v3d_device_info *devinfo,
                                     enum v3d_qpu_waddr waddr);
const char *v3d_qpu_add_op_name(enum v3d_qpu_add_op op);
const char *v3d_qpu_mul_op_name(enum v3d_qpu_mul_op op);
const char *v3d_qpu_cond_name(enum v3d_qpu_cond cond);
const char *v3d_qpu_pf_name(enum v3d_qpu_pf pf);
const char *v3d_qpu_uf_name(enum v3d_qpu_uf uf);
const char *v3d_qpu_pack_name(enum v3d_qpu_output_pack pack);
const char *v3d_qpu_unpack_name(enum v3d_qpu_input_unpack unpack);
const char *v3d_qpu_branch_cond_name(enum v3d_qpu_branch_cond cond);
const char *v3d_qpu_msfign_name(enum v3d_qpu_msfign msfign);

enum v3d_qpu_cond v3d_qpu_cond_invert(enum v3d_qpu_cond cond);

v3d_bool v3d_qpu_add_op_has_dst(enum v3d_qpu_add_op op);
v3d_bool v3d_qpu_mul_op_has_dst(enum v3d_qpu_mul_op op);
int v3d_qpu_add_op_num_src(enum v3d_qpu_add_op op);
int v3d_qpu_mul_op_num_src(enum v3d_qpu_mul_op op);

v3d_bool v3d_qpu_sig_pack(const struct v3d_device_info *devinfo,
						  const struct v3d_qpu_sig *sig,
						  v3d_uint32 *packed_sig);
v3d_bool v3d_qpu_sig_unpack(const struct v3d_device_info *devinfo,
							v3d_uint32 packed_sig,
							struct v3d_qpu_sig *sig);

v3d_bool
v3d_qpu_flags_pack(const struct v3d_device_info *devinfo,
                   const struct v3d_qpu_flags *cond,
                   v3d_uint32 *packed_cond);
v3d_bool
v3d_qpu_flags_unpack(const struct v3d_device_info *devinfo,
                     v3d_uint32 packed_cond,
                     struct v3d_qpu_flags *cond);

v3d_bool
v3d_qpu_small_imm_pack(const struct v3d_device_info *devinfo,
                       v3d_uint32 value,
                       v3d_uint32 *packed_small_immediate);

v3d_bool
v3d_qpu_small_imm_unpack(const struct v3d_device_info *devinfo,
                         v3d_uint32 packed_small_immediate,
                         v3d_uint32 *small_immediate);

v3d_bool
v3d_qpu_instr_pack(const struct v3d_device_info *devinfo,
                   const struct v3d_qpu_instr *instr,
                   v3d_uint64 *packed_instr);
v3d_bool
v3d_qpu_instr_unpack(const struct v3d_device_info *devinfo,
                     v3d_uint64 packed_instr,
                     struct v3d_qpu_instr *instr);

v3d_bool v3d_qpu_magic_waddr_is_sfu(enum v3d_qpu_waddr waddr);
v3d_bool v3d_qpu_magic_waddr_is_tmu(const struct v3d_device_info *devinfo,
									enum v3d_qpu_waddr waddr);
v3d_bool v3d_qpu_magic_waddr_is_tlb(enum v3d_qpu_waddr waddr);
v3d_bool v3d_qpu_magic_waddr_is_vpm(enum v3d_qpu_waddr waddr);
v3d_bool v3d_qpu_magic_waddr_is_tsy(enum v3d_qpu_waddr waddr);
v3d_bool v3d_qpu_magic_waddr_loads_unif(enum v3d_qpu_waddr waddr);
v3d_bool v3d_qpu_reads_tlb(const struct v3d_qpu_instr *inst);
v3d_bool v3d_qpu_writes_tlb(const struct v3d_qpu_instr *inst);
v3d_bool v3d_qpu_uses_tlb(const struct v3d_qpu_instr *inst);
v3d_bool v3d_qpu_instr_is_sfu(const struct v3d_qpu_instr *inst);
v3d_bool v3d_qpu_instr_is_legacy_sfu(const struct v3d_qpu_instr *inst);
v3d_bool v3d_qpu_uses_sfu(const struct v3d_qpu_instr *inst);
v3d_bool v3d_qpu_writes_tmu(const struct v3d_device_info *devinfo,
							const struct v3d_qpu_instr *inst);
v3d_bool v3d_qpu_writes_tmu_not_tmuc(const struct v3d_device_info *devinfo,
									 const struct v3d_qpu_instr *inst);
v3d_bool v3d_qpu_writes_r3(const struct v3d_device_info *devinfo,
						   const struct v3d_qpu_instr *instr);
v3d_bool v3d_qpu_writes_r4(const struct v3d_device_info *devinfo,
						   const struct v3d_qpu_instr *instr);
v3d_bool v3d_qpu_writes_r5(const struct v3d_device_info *devinfo,
						   const struct v3d_qpu_instr *instr);
v3d_bool v3d_qpu_writes_rf0_implicitly(const struct v3d_device_info *devinfo,
									   const struct v3d_qpu_instr *instr);
v3d_bool v3d_qpu_writes_accum(const struct v3d_device_info *devinfo,
							  const struct v3d_qpu_instr *inst);
v3d_bool v3d_qpu_waits_on_tmu(const struct v3d_qpu_instr *inst);
v3d_bool v3d_qpu_uses_mux(const struct v3d_qpu_instr *inst, enum v3d_qpu_mux mux);
v3d_bool v3d_qpu_uses_vpm(const struct v3d_qpu_instr *inst);
v3d_bool v3d_qpu_waits_vpm(const struct v3d_qpu_instr *inst);
v3d_bool v3d_qpu_reads_vpm(const struct v3d_qpu_instr *inst);
v3d_bool v3d_qpu_writes_vpm(const struct v3d_qpu_instr *inst);
v3d_bool v3d_qpu_reads_or_writes_vpm(const struct v3d_qpu_instr *inst);
v3d_bool v3d_qpu_reads_flags(const struct v3d_qpu_instr *inst);
v3d_bool v3d_qpu_writes_flags(const struct v3d_qpu_instr *inst);
v3d_bool v3d_qpu_writes_unifa(const struct v3d_device_info *devinfo,
							  const struct v3d_qpu_instr *inst);
v3d_bool v3d_qpu_sig_writes_address(const struct v3d_device_info *devinfo,
									const struct v3d_qpu_sig *sig);
v3d_bool v3d_qpu_unpacks_f32(const struct v3d_qpu_instr *inst);
v3d_bool v3d_qpu_unpacks_f16(const struct v3d_qpu_instr *inst);

v3d_bool v3d_qpu_is_nop(struct v3d_qpu_instr *inst);

v3d_bool v3d71_qpu_reads_raddr(const struct v3d_qpu_instr *inst, v3d_uint8 raddr);
v3d_bool v3d71_qpu_writes_waddr_explicitly(const struct v3d_device_info *devinfo,
										   const struct v3d_qpu_instr *inst,
										   v3d_uint8 waddr);

// >>> qpu_disasm.h

size_t
v3d_qpu_decode(const struct v3d_device_info *devinfo,
               const struct v3d_qpu_instr *instr,
               char* outBuffer, size_t outBufferSize);

/**
 * Formats a string to the disassembled representation of the QPU instruction.
 * Returns the string length.
 */
size_t
v3d_qpu_disasm(const struct v3d_device_info *devinfo, v3d_uint64 inst, char* outBuffer, size_t outBufferSize);

// The assembler is written by Macoy Madson (not from Mesa)
struct v3d_qpu_assemble_arguments
{
	// Inputs
	struct v3d_device_info devinfo;
	const char* assembly;

	// Outputs
	struct v3d_qpu_instr instruction;
	v3d_bool isEmptyLine;

	// So later errors can be routed directly to this instruction in the text. This is a byte
	// offset, NOT a line or column number.
	int instructionStartsAtOffset;

	int errorAtOffset;
	const char* errorMessage;
	const char** hintAvailable;
	int numHints;
};

// Assembly is expected to point to the start of a line of assembly code. This function parses
// until the end of the line or null terminator, whichever comes first. If a /**/ comment is
// detected, this function will skip over the entire comment, which could include multiple newlines.
// Therefore, do not expect this to only parse a single line and use that as a line count.
// This function parses a single 64 bit instruction expected to be in the same format as the
// disassembly, although this is whitespace insensitive.
// Comments '//' and '/* */' can be used.
// ';' is used as a delimiter between e.g. add, mul, sig operations.
// hintAvailable is set when the error can hint the user with a list of all valid strings for a
// given context, e.g. all available add operations. It's intended that e.g. an editor could say
// "did you mean X" using this list.
// Check isEmptyLine to ignore instructionOut and advance read head by the returned value.
// Returns 0 and sets error if the assembly could not be decoded.
// Otherwise, returns the number of characters absorbed by this instruction.
v3d_uint32 v3d_qpu_assemble(struct v3d_qpu_assemble_arguments* args);

// (todo documentation) It would be good to write explanations for all of these.
enum v3d_qpu_validate_error
{
	V3D_QPU_VALIDATE_ERROR_NONE,
	V3D_QPU_VALIDATE_ERROR_IMPLICIT_BRANCH_MSF_READ_AFTER_TLB_Z_WRITE,
	V3D_QPU_VALIDATE_ERROR_SETMSF_AFTER_TLB_Z_WRITE,
	V3D_QPU_VALIDATE_ERROR_MSF_READ_AFTER_TLB_Z_WRITE,
	V3D_QPU_VALIDATE_ERROR_SMALL_IMM_A_C_D_ADDED_AFTER_V3D_7_1,
	V3D_QPU_VALIDATE_ERROR_SMALL_IMM_A_B_USED_BUT_NO_ADD_INST,
	V3D_QPU_VALIDATE_ERROR_SMALL_IMM_C_D_USED_BUT_NO_MUL_INST,
	V3D_QPU_VALIDATE_ERROR_MAX_ONE_SMALL_IMMEDIATE_PER_INSTRUCTION,
	V3D_QPU_VALIDATE_ERROR_LDUNIF_AFTER_A_LDVARY,
	V3D_QPU_VALIDATE_ERROR_LDUNIF_AND_LDUNIFA_CANT_BE_NEXT_TO_EACH_OTHER,
	V3D_QPU_VALIDATE_ERROR_SFU_WRITE_STARTED_DURING_THRSW_DELAY_SLOTS,
	V3D_QPU_VALIDATE_ERROR_LDVARY_DURING_THRSW_DELAY_SLOTS,
	V3D_QPU_VALIDATE_ERROR_LDVARY_IN_2ND_THRSW_DELAY_SLOT,
	V3D_QPU_VALIDATE_ERROR_R4_READ_TOO_SOON_AFTER_SFU,
	V3D_QPU_VALIDATE_ERROR_R4_WRITE_TOO_SOON_AFTER_SFU,
	V3D_QPU_VALIDATE_ERROR_SFU_WRITE_TOO_SOON_AFTER_SFU,
	V3D_QPU_VALIDATE_ERROR_ONLY_ONE_OF_TMU_SFU_TSY_TLB_READ_VPM_ALLOWED,
	V3D_QPU_VALIDATE_ERROR_THRSW_IN_A_BRANCH_DELAY_SLOT,
	V3D_QPU_VALIDATE_ERROR_TWO_LAST_THRSW_SIGNALS,
	V3D_QPU_VALIDATE_ERROR_THRSW_TOO_CLOSE_TO_ANOTHER_THRSW,
	V3D_QPU_VALIDATE_ERROR_RF_WRITE_AFTER_THREND,
	V3D_QPU_VALIDATE_ERROR_ADD_RF_WRITE_AT_THREND,
	V3D_QPU_VALIDATE_ERROR_RF2_3_WRITE_AFTER_THREND,
	V3D_QPU_VALIDATE_ERROR_MUL_RF_WRITE_AT_THREND,
	V3D_QPU_VALIDATE_ERROR_TMUWT_IN_LAST_INSTRUCTION,
	V3D_QPU_VALIDATE_ERROR_BRANCH_IN_A_BRANCH_DELAY_SLOT,
	V3D_QPU_VALIDATE_ERROR_BRANCH_IN_A_THRSW_DELAY_SLOT,
	V3D_QPU_VALIDATE_ERROR_THREAD_SWITCH_FOUND_WITHOUT_LAST_THRSW_IN_PROGRAM,
	V3D_QPU_VALIDATE_ERROR_NO_PROGRAM_END_THRSW_FOUND,
	V3D_QPU_VALIDATE_ERROR_NO_PROGRAM_END_THRSW_DELAY_SLOTS,
};

struct v3d_qpu_validate_result
{
	int errorInstructionIndex;
	const char* errorMessage;
	enum v3d_qpu_validate_error error;
};

// Checks an instruction sequence for the instruction restrictions from page 37 ("Summary of
// Instruction Restrictions").
// Returns FALSE if the sequence has an invalid instruction.
v3d_bool v3d_qpu_validate(const struct v3d_device_info* devinfo, struct v3d_qpu_instr* instructions,
                          int numInstructions, struct v3d_qpu_validate_result* results);

//
// Implementation
//

#ifdef V3D_ASSEMBLER_IMPLEMENTATION

#ifndef v3d_memcmp
#error "Need to #define v3d_memcmp to e.g.: #define v3d_memcmp memcmp"
#endif

#ifndef v3d_vsnprintf
#error "Need to #define v3d_vsnprintf to e.g.: #define v3d_vsnprintf vsnprintf"
#endif

#ifndef V3D_ARRAY_SIZE
#define V3D_ARRAY_SIZE(array) (sizeof((array)) / sizeof((array)[0]))
#endif

#ifndef V3D_STATIC_ASSERT
#define V3D_STATIC_ASSERT(condition)
#endif

#ifndef v3d_assert
#define v3d_assert(condition)
#endif

#ifndef v3d_unreachable
#define v3d_unreachable(message) v3d_assert(0)
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef NULL
#define NULL 0
#endif

// endOfCompareOut is set only when the symbol matches, and is the character right after the match
// completes.
static v3d_bool v3d_symbol_equals(const char* symbol, const char* compare,
                                  const char** endOfCompareOut)
{
	const char* candidateChar = compare;
	for (const char* a = symbol; *a && *candidateChar && *a == *candidateChar; ++a)
	{
		++candidateChar;
		// All possible delimiters for symbols
		if (a[1] == 0 && (*candidateChar == 0 || *candidateChar == '\n' || *candidateChar == '\r' ||
		                  *candidateChar == '\t' || *candidateChar == '.' ||
		                  *candidateChar == ' ' || *candidateChar == ',' || *candidateChar == ';'))
		{
			if (endOfCompareOut)
				*endOfCompareOut = candidateChar;
			return TRUE;
		}
	}
	return FALSE;
}

// >>> qpu_instr.c
const char *
v3d_qpu_magic_waddr_name(const struct v3d_device_info *devinfo,
                         enum v3d_qpu_waddr waddr)
{
	/* V3D 4.x UNIFA aliases TMU in V3D 3.x in the table below */
	if (devinfo->ver < 40 && waddr == V3D_QPU_WADDR_TMU)
		return "tmu";

	/* V3D 7.x QUAD and REP aliases R5 and R5REPT in the table below
	 */
	if (devinfo->ver >= 71 && waddr == V3D_QPU_WADDR_QUAD)
		return "quad";

	if (devinfo->ver >= 71 && waddr == V3D_QPU_WADDR_REP)
		return "rep";

	static const char *waddr_magic[] = {
		[V3D_QPU_WADDR_R0] = "r0",
		[V3D_QPU_WADDR_R1] = "r1",
		[V3D_QPU_WADDR_R2] = "r2",
		[V3D_QPU_WADDR_R3] = "r3",
		[V3D_QPU_WADDR_R4] = "r4",
		[V3D_QPU_WADDR_R5] = "r5",
		[V3D_QPU_WADDR_NOP] = "-",
		[V3D_QPU_WADDR_TLB] = "tlb",
		[V3D_QPU_WADDR_TLBU] = "tlbu",
		[V3D_QPU_WADDR_UNIFA] = "unifa",
		[V3D_QPU_WADDR_TMUL] = "tmul",
		[V3D_QPU_WADDR_TMUD] = "tmud",
		[V3D_QPU_WADDR_TMUA] = "tmua",
		[V3D_QPU_WADDR_TMUAU] = "tmuau",
		[V3D_QPU_WADDR_VPM] = "vpm",
		[V3D_QPU_WADDR_VPMU] = "vpmu",
		[V3D_QPU_WADDR_SYNC] = "sync",
		[V3D_QPU_WADDR_SYNCU] = "syncu",
		[V3D_QPU_WADDR_SYNCB] = "syncb",
		[V3D_QPU_WADDR_RECIP] = "recip",
		[V3D_QPU_WADDR_RSQRT] = "rsqrt",
		[V3D_QPU_WADDR_EXP] = "exp",
		[V3D_QPU_WADDR_LOG] = "log",
		[V3D_QPU_WADDR_SIN] = "sin",
		[V3D_QPU_WADDR_RSQRT2] = "rsqrt2",
		[V3D_QPU_WADDR_TMUC] = "tmuc",
		[V3D_QPU_WADDR_TMUS] = "tmus",
		[V3D_QPU_WADDR_TMUT] = "tmut",
		[V3D_QPU_WADDR_TMUR] = "tmur",
		[V3D_QPU_WADDR_TMUI] = "tmui",
		[V3D_QPU_WADDR_TMUB] = "tmub",
		[V3D_QPU_WADDR_TMUDREF] = "tmudref",
		[V3D_QPU_WADDR_TMUOFF] = "tmuoff",
		[V3D_QPU_WADDR_TMUSCM] = "tmuscm",
		[V3D_QPU_WADDR_TMUSF] = "tmusf",
		[V3D_QPU_WADDR_TMUSLOD] = "tmuslod",
		[V3D_QPU_WADDR_TMUHS] = "tmuhs",
		[V3D_QPU_WADDR_TMUHSCM] = "tmuscm",
		[V3D_QPU_WADDR_TMUHSF] = "tmuhsf",
		[V3D_QPU_WADDR_TMUHSLOD] = "tmuhslod",
		[V3D_QPU_WADDR_R5REP] = "r5rep",
	};

	return waddr_magic[waddr];
}

// MUST align exactly with waddr_values
// (kept separate to make names easy to prompt in assembler errors)
static const char* waddr_names[] = {
	"r0",
	"r1",
	"r2",
	"r3",
	"r4",
	"r5",
	"-",
	"tlb",
	"tlbu",
	"unifa",
	"tmul",
	"tmud",
	"tmua",
	"tmuau",
	"vpm",
	"vpmu",
	"sync",
	"syncu",
	"syncb",
	"recip",
	"rsqrt",
	"exp",
	"log",
	"sin",
	"rsqrt2",
	"tmuc",
	"tmus",
	"tmut",
	"tmur",
	"tmui",
	"tmub",
	"tmudref",
	"tmuoff",
	"tmuscm",
	"tmusf",
	"tmuslod",
	"tmuhs",
	"tmuscm",
	"tmuhsf",
	"tmuhslod",
	"r5rep",
	/* V3D 4.x UNIFA aliases TMU in V3D 3.x */
	// devinfo->ver < 40
	"tmu",
	/* V3D 7.x QUAD and REP aliases R5 and R5REPT */
	// devinfo->ver >= 71
	"quad",
	// devinfo->ver >= 71
	"rep",
};

// MUST align exactly with waddr_names
static const enum v3d_qpu_waddr waddr_values[] = {
	V3D_QPU_WADDR_R0,
	V3D_QPU_WADDR_R1,
	V3D_QPU_WADDR_R2,
	V3D_QPU_WADDR_R3,
	V3D_QPU_WADDR_R4,
	V3D_QPU_WADDR_R5,
	V3D_QPU_WADDR_NOP,
	V3D_QPU_WADDR_TLB,
	V3D_QPU_WADDR_TLBU,
	V3D_QPU_WADDR_UNIFA,
	V3D_QPU_WADDR_TMUL,
	V3D_QPU_WADDR_TMUD,
	V3D_QPU_WADDR_TMUA,
	V3D_QPU_WADDR_TMUAU,
	V3D_QPU_WADDR_VPM,
	V3D_QPU_WADDR_VPMU,
	V3D_QPU_WADDR_SYNC,
	V3D_QPU_WADDR_SYNCU,
	V3D_QPU_WADDR_SYNCB,
	V3D_QPU_WADDR_RECIP,
	V3D_QPU_WADDR_RSQRT,
	V3D_QPU_WADDR_EXP,
	V3D_QPU_WADDR_LOG,
	V3D_QPU_WADDR_SIN,
	V3D_QPU_WADDR_RSQRT2,
	V3D_QPU_WADDR_TMUC,
	V3D_QPU_WADDR_TMUS,
	V3D_QPU_WADDR_TMUT,
	V3D_QPU_WADDR_TMUR,
	V3D_QPU_WADDR_TMUI,
	V3D_QPU_WADDR_TMUB,
	V3D_QPU_WADDR_TMUDREF,
	V3D_QPU_WADDR_TMUOFF,
	V3D_QPU_WADDR_TMUSCM,
	V3D_QPU_WADDR_TMUSF,
	V3D_QPU_WADDR_TMUSLOD,
	V3D_QPU_WADDR_TMUHS,
	V3D_QPU_WADDR_TMUHSCM,
	V3D_QPU_WADDR_TMUHSF,
	V3D_QPU_WADDR_TMUHSLOD,
	V3D_QPU_WADDR_R5REP,
	V3D_QPU_WADDR_TMU,
	V3D_QPU_WADDR_QUAD,
	V3D_QPU_WADDR_REP,
};

v3d_bool v32_qpu_magic_waddr_from_name(const char* name,
									   enum v3d_qpu_waddr* waddrOut,
									   const char** endOfNameOut)
{
    for (int index = 0; index < V3D_ARRAY_SIZE(waddr_names); ++index)
	{
		if (v3d_symbol_equals(waddr_names[index], name, endOfNameOut))
		{
            *waddrOut = waddr_values[index];
			return TRUE;
		}
	}
	return FALSE;
}

static const char *add_op_names[] = {
	[V3D_QPU_A_FADD] = "fadd",
	[V3D_QPU_A_FADDNF] = "faddnf",
	[V3D_QPU_A_VFPACK] = "vfpack",
	[V3D_QPU_A_ADD] = "add",
	[V3D_QPU_A_SUB] = "sub",
	[V3D_QPU_A_FSUB] = "fsub",
	[V3D_QPU_A_MIN] = "min",
	[V3D_QPU_A_MAX] = "max",
	[V3D_QPU_A_UMIN] = "umin",
	[V3D_QPU_A_UMAX] = "umax",
	[V3D_QPU_A_SHL] = "shl",
	[V3D_QPU_A_SHR] = "shr",
	[V3D_QPU_A_ASR] = "asr",
	[V3D_QPU_A_ROR] = "ror",
	[V3D_QPU_A_FMIN] = "fmin",
	[V3D_QPU_A_FMAX] = "fmax",
	[V3D_QPU_A_VFMIN] = "vfmin",
	[V3D_QPU_A_AND] = "and",
	[V3D_QPU_A_OR] = "or",
	[V3D_QPU_A_XOR] = "xor",
	[V3D_QPU_A_VADD] = "vadd",
	[V3D_QPU_A_VSUB] = "vsub",
	[V3D_QPU_A_NOT] = "not",
	[V3D_QPU_A_NEG] = "neg",
	[V3D_QPU_A_FLAPUSH] = "flapush",
	[V3D_QPU_A_FLBPUSH] = "flbpush",
	[V3D_QPU_A_FLPOP] = "flpop",
	[V3D_QPU_A_RECIP] = "recip",
	[V3D_QPU_A_SETMSF] = "setmsf",
	[V3D_QPU_A_SETREVF] = "setrevf",
	[V3D_QPU_A_NOP] = "nop",
	[V3D_QPU_A_TIDX] = "tidx",
	[V3D_QPU_A_EIDX] = "eidx",
	[V3D_QPU_A_LR] = "lr",
	[V3D_QPU_A_VFLA] = "vfla",
	[V3D_QPU_A_VFLNA] = "vflna",
	[V3D_QPU_A_VFLB] = "vflb",
	[V3D_QPU_A_VFLNB] = "vflnb",
	[V3D_QPU_A_FXCD] = "fxcd",
	[V3D_QPU_A_XCD] = "xcd",
	[V3D_QPU_A_FYCD] = "fycd",
	[V3D_QPU_A_YCD] = "ycd",
	[V3D_QPU_A_MSF] = "msf",
	[V3D_QPU_A_REVF] = "revf",
	[V3D_QPU_A_VDWWT] = "vdwwt",
	[V3D_QPU_A_IID] = "iid",
	[V3D_QPU_A_SAMPID] = "sampid",
	[V3D_QPU_A_BARRIERID] = "barrierid",
	[V3D_QPU_A_TMUWT] = "tmuwt",
	[V3D_QPU_A_VPMSETUP] = "vpmsetup",
	[V3D_QPU_A_VPMWT] = "vpmwt",
	[V3D_QPU_A_FLAFIRST] = "flafirst",
	[V3D_QPU_A_FLNAFIRST] = "flnafirst",
	[V3D_QPU_A_LDVPMV_IN] = "ldvpmv_in",
	[V3D_QPU_A_LDVPMV_OUT] = "ldvpmv_out",
	[V3D_QPU_A_LDVPMD_IN] = "ldvpmd_in",
	[V3D_QPU_A_LDVPMD_OUT] = "ldvpmd_out",
	[V3D_QPU_A_LDVPMP] = "ldvpmp",
	[V3D_QPU_A_RSQRT] = "rsqrt",
	[V3D_QPU_A_EXP] = "exp",
	[V3D_QPU_A_LOG] = "log",
	[V3D_QPU_A_SIN] = "sin",
	[V3D_QPU_A_RSQRT2] = "rsqrt2",
	[V3D_QPU_A_LDVPMG_IN] = "ldvpmg_in",
	[V3D_QPU_A_LDVPMG_OUT] = "ldvpmg_out",
	[V3D_QPU_A_FCMP] = "fcmp",
	[V3D_QPU_A_VFMAX] = "vfmax",
	[V3D_QPU_A_FROUND] = "fround",
	[V3D_QPU_A_FTOIN] = "ftoin",
	[V3D_QPU_A_FTRUNC] = "ftrunc",
	[V3D_QPU_A_FTOIZ] = "ftoiz",
	[V3D_QPU_A_FFLOOR] = "ffloor",
	[V3D_QPU_A_FTOUZ] = "ftouz",
	[V3D_QPU_A_FCEIL] = "fceil",
	[V3D_QPU_A_FTOC] = "ftoc",
	[V3D_QPU_A_FDX] = "fdx",
	[V3D_QPU_A_FDY] = "fdy",
	[V3D_QPU_A_STVPMV] = "stvpmv",
	[V3D_QPU_A_STVPMD] = "stvpmd",
	[V3D_QPU_A_STVPMP] = "stvpmp",
	[V3D_QPU_A_ITOF] = "itof",
	[V3D_QPU_A_CLZ] = "clz",
	[V3D_QPU_A_UTOF] = "utof",
	[V3D_QPU_A_MOV] = "mov",
	[V3D_QPU_A_FMOV] = "fmov",
	[V3D_QPU_A_VPACK] = "vpack",
	[V3D_QPU_A_V8PACK] = "v8pack",
	[V3D_QPU_A_V10PACK] = "v10pack",
	[V3D_QPU_A_V11FPACK] = "v11fpack",
};

const char *
v3d_qpu_add_op_name(enum v3d_qpu_add_op op)
{
	if (op >= V3D_ARRAY_SIZE(add_op_names))
		return NULL;

	return add_op_names[op];
}

static const char* mul_op_names[] = {
    [V3D_QPU_M_ADD] = "add",
    [V3D_QPU_M_SUB] = "sub",
    [V3D_QPU_M_UMUL24] = "umul24",
    [V3D_QPU_M_VFMUL] = "vfmul",
    [V3D_QPU_M_SMUL24] = "smul24",
    [V3D_QPU_M_MULTOP] = "multop",
    [V3D_QPU_M_FMOV] = "fmov",
    [V3D_QPU_M_MOV] = "mov",
    [V3D_QPU_M_NOP] = "nop",
    [V3D_QPU_M_FMUL] = "fmul",
    [V3D_QPU_M_FTOUNORM16] = "ftounorm16",
    [V3D_QPU_M_FTOSNORM16] = "ftosnorm16",
    [V3D_QPU_M_VFTOUNORM8] = "vftounorm8",
    [V3D_QPU_M_VFTOSNORM8] = "vftosnorm8",
    [V3D_QPU_M_VFTOUNORM10LO] = "vftounorm10lo",
    [V3D_QPU_M_VFTOUNORM10HI] = "vftounorm10hi",
};

const char* v3d_qpu_mul_op_name(enum v3d_qpu_mul_op op)
{
	if (op >= V3D_ARRAY_SIZE(mul_op_names))
		return NULL;

	return mul_op_names[op];
}

// Also update cond_pf_uf_names if changing
static const char* cond_names[] = {
	[V3D_QPU_COND_NONE] = "",
	[V3D_QPU_COND_IFA] = ".ifa",
	[V3D_QPU_COND_IFB] = ".ifb",
	[V3D_QPU_COND_IFNA] = ".ifna",
	[V3D_QPU_COND_IFNB] = ".ifnb",
};

const char *
v3d_qpu_cond_name(enum v3d_qpu_cond cond)
{
	if (cond >= V3D_ARRAY_SIZE(cond_names))
		return NULL;

	return cond_names[cond];
}

const char *
v3d_qpu_branch_cond_name(enum v3d_qpu_branch_cond cond)
{
	switch (cond) {
	case V3D_QPU_BRANCH_COND_ALWAYS:
		return "";
	case V3D_QPU_BRANCH_COND_A0:
		return ".a0";
	case V3D_QPU_BRANCH_COND_NA0:
		return ".na0";
	case V3D_QPU_BRANCH_COND_ALLA:
		return ".alla";
	case V3D_QPU_BRANCH_COND_ANYNA:
		return ".anyna";
	case V3D_QPU_BRANCH_COND_ANYA:
		return ".anya";
	case V3D_QPU_BRANCH_COND_ALLNA:
		return ".allna";
	default:
		v3d_unreachable("bad branch cond value");
	}
	return "";
}

const char *
v3d_qpu_msfign_name(enum v3d_qpu_msfign msfign)
{
	switch (msfign) {
	case V3D_QPU_MSFIGN_NONE:
		return "";
	case V3D_QPU_MSFIGN_P:
		return "p";
	case V3D_QPU_MSFIGN_Q:
		return "q";
	default:
		v3d_unreachable("bad branch cond value");
	}
	return "";
}

// Also update cond_pf_uf_names if changing
const char* pf_names[] = {
	[V3D_QPU_PF_NONE] =  "",
	[V3D_QPU_PF_PUSHZ] =  ".pushz",
	[V3D_QPU_PF_PUSHN] =  ".pushn",
	[V3D_QPU_PF_PUSHC] =  ".pushc",
};

const char *
v3d_qpu_pf_name(enum v3d_qpu_pf pf)
{
	if (pf >= V3D_ARRAY_SIZE(pf_names))
		return NULL;

	return pf_names[pf];
}

// Also update cond_pf_uf_names if changing
const char* uf_names[] = {
	[V3D_QPU_UF_NONE] = "",
	[V3D_QPU_UF_ANDZ] = ".andz",
	[V3D_QPU_UF_ANDNZ] = ".andnz",
	[V3D_QPU_UF_NORZ] = ".norz",
	[V3D_QPU_UF_NORNZ] = ".nornz",
	[V3D_QPU_UF_ANDN] = ".andn",
	[V3D_QPU_UF_ANDNN] = ".andnn",
	[V3D_QPU_UF_NORN] = ".norn",
	[V3D_QPU_UF_NORNN] = ".nornn",
	[V3D_QPU_UF_ANDC] = ".andc",
	[V3D_QPU_UF_ANDNC] = ".andnc",
	[V3D_QPU_UF_NORC] = ".norc",
	[V3D_QPU_UF_NORNC] = ".nornc",
};

const char *
v3d_qpu_uf_name(enum v3d_qpu_uf uf)
{
	if (uf >= V3D_ARRAY_SIZE(uf_names))
		return NULL;

	return uf_names[uf];
}

// Only used for listing all the options
const char* cond_pf_uf_names[] = {
    // cond_names
    ".ifa",
    ".ifb",
    ".ifna",
    ".ifnb",
    // pf_names
    ".pushz",
    ".pushn",
    ".pushc",
    // uf_names
    ".andz",
    ".andnz",
    ".norz",
    ".nornz",
    ".andn",
    ".andnn",
    ".norn",
    ".nornn",
    ".andc",
    ".andnc",
    ".norc",
    ".nornc",
};

// Looks through nameList for an exact match to name. Name doesn't need to be null terminated or
// anything because this will automatically check for symbol dividers. If dotOptional, nothing
// specified will be considered a valid entry and its index will be 0. The nameList should therefore
// have its first index be an empty string with NONE associated value.
// Returns whether the value is in the list (TRUE if unspecified and dotOptional).
v3d_bool v3d_qpu_value_from_name_list(const char* name, const char** nameList, int nameListLength,
                                      v3d_bool dotOptional, v3d_uint32* matchingIndexOut,
                                      const char** endOfNameOut)
{
	if (dotOptional && name[0] != '.')
	{
		*matchingIndexOut = 0;
		if (endOfNameOut)
			*endOfNameOut = name;
		return TRUE;
	}

	// Skip over empty string for dot-optional lists
	for (int index = dotOptional ? 1 : 0; index < nameListLength; ++index)
	{
		if (v3d_symbol_equals(nameList[index], name, endOfNameOut))
		{
			*matchingIndexOut = index;
			return TRUE;
		}
	}
	return FALSE;
}

static const char* pack_names[] = {
    [V3D_QPU_PACK_NONE] = "",
    [V3D_QPU_PACK_L] = ".l",
    [V3D_QPU_PACK_H] = ".h",
};

const char*	v3d_qpu_pack_name(enum v3d_qpu_output_pack pack)
{
	if (pack >= V3D_ARRAY_SIZE(pack_names))
		return NULL;

	return pack_names[pack];
}

static const char* unpack_names[] = {
    [V3D_QPU_UNPACK_NONE] = "",
    [V3D_QPU_UNPACK_L] = ".l",
    [V3D_QPU_UNPACK_H] = ".h",
    [V3D_QPU_UNPACK_ABS] = ".abs",
    [V3D_QPU_UNPACK_REPLICATE_32F_16] = ".ff",
    [V3D_QPU_UNPACK_REPLICATE_L_16] = ".ll",
    [V3D_QPU_UNPACK_REPLICATE_H_16] = ".hh",
    [V3D_QPU_UNPACK_SWAP_16] = ".swp",
};

const char*	v3d_qpu_unpack_name(enum v3d_qpu_input_unpack unpack)
{
	if (unpack >= V3D_ARRAY_SIZE(unpack_names))
		return NULL;

	return unpack_names[unpack];
}

#define D   1 // Destination
#define A   2 // Argument A
#define B   4 // Argument B
static const v3d_uint8 add_op_args[] = {
	[V3D_QPU_A_FADD] = D | A | B,
	[V3D_QPU_A_FADDNF] = D | A | B,
	[V3D_QPU_A_VFPACK] = D | A | B,
	[V3D_QPU_A_ADD] = D | A | B,
	[V3D_QPU_A_VFPACK] = D | A | B,
	[V3D_QPU_A_SUB] = D | A | B,
	[V3D_QPU_A_VFPACK] = D | A | B,
	[V3D_QPU_A_FSUB] = D | A | B,
	[V3D_QPU_A_MIN] = D | A | B,
	[V3D_QPU_A_MAX] = D | A | B,
	[V3D_QPU_A_UMIN] = D | A | B,
	[V3D_QPU_A_UMAX] = D | A | B,
	[V3D_QPU_A_SHL] = D | A | B,
	[V3D_QPU_A_SHR] = D | A | B,
	[V3D_QPU_A_ASR] = D | A | B,
	[V3D_QPU_A_ROR] = D | A | B,
	[V3D_QPU_A_FMIN] = D | A | B,
	[V3D_QPU_A_FMAX] = D | A | B,
	[V3D_QPU_A_VFMIN] = D | A | B,

	[V3D_QPU_A_AND] = D | A | B,
	[V3D_QPU_A_OR] = D | A | B,
	[V3D_QPU_A_XOR] = D | A | B,

	[V3D_QPU_A_VADD] = D | A | B,
	[V3D_QPU_A_VSUB] = D | A | B,
	[V3D_QPU_A_NOT] = D | A,
	[V3D_QPU_A_NEG] = D | A,
	[V3D_QPU_A_FLAPUSH] = D | A,
	[V3D_QPU_A_FLBPUSH] = D | A,
	[V3D_QPU_A_FLPOP] = D | A,
	[V3D_QPU_A_RECIP] = D | A,
	[V3D_QPU_A_SETMSF] = D | A,
	[V3D_QPU_A_SETREVF] = D | A,
	[V3D_QPU_A_NOP] = 0,
	[V3D_QPU_A_TIDX] = D,
	[V3D_QPU_A_EIDX] = D,
	[V3D_QPU_A_LR] = D,
	[V3D_QPU_A_VFLA] = D,
	[V3D_QPU_A_VFLNA] = D,
	[V3D_QPU_A_VFLB] = D,
	[V3D_QPU_A_VFLNB] = D,

	[V3D_QPU_A_FXCD] = D,
	[V3D_QPU_A_XCD] = D,
	[V3D_QPU_A_FYCD] = D,
	[V3D_QPU_A_YCD] = D,

	[V3D_QPU_A_MSF] = D,
	[V3D_QPU_A_REVF] = D,
	[V3D_QPU_A_VDWWT] = D,
	[V3D_QPU_A_IID] = D,
	[V3D_QPU_A_SAMPID] = D,
	[V3D_QPU_A_BARRIERID] = D,
	[V3D_QPU_A_TMUWT] = D,
	[V3D_QPU_A_VPMWT] = D,
	[V3D_QPU_A_FLAFIRST] = D,
	[V3D_QPU_A_FLNAFIRST] = D,

	[V3D_QPU_A_VPMSETUP] = D | A,

	[V3D_QPU_A_LDVPMV_IN] = D | A,
	[V3D_QPU_A_LDVPMV_OUT] = D | A,
	[V3D_QPU_A_LDVPMD_IN] = D | A,
	[V3D_QPU_A_LDVPMD_OUT] = D | A,
	[V3D_QPU_A_LDVPMP] = D | A,
	[V3D_QPU_A_RSQRT] = D | A,
	[V3D_QPU_A_EXP] = D | A,
	[V3D_QPU_A_LOG] = D | A,
	[V3D_QPU_A_SIN] = D | A,
	[V3D_QPU_A_RSQRT2] = D | A,
	[V3D_QPU_A_LDVPMG_IN] = D | A | B,
	[V3D_QPU_A_LDVPMG_OUT] = D | A | B,

	/* FIXME: MOVABSNEG */

	[V3D_QPU_A_FCMP] = D | A | B,
	[V3D_QPU_A_VFMAX] = D | A | B,

	[V3D_QPU_A_FROUND] = D | A,
	[V3D_QPU_A_FTOIN] = D | A,
	[V3D_QPU_A_FTRUNC] = D | A,
	[V3D_QPU_A_FTOIZ] = D | A,
	[V3D_QPU_A_FFLOOR] = D | A,
	[V3D_QPU_A_FTOUZ] = D | A,
	[V3D_QPU_A_FCEIL] = D | A,
	[V3D_QPU_A_FTOC] = D | A,

	[V3D_QPU_A_FDX] = D | A,
	[V3D_QPU_A_FDY] = D | A,

	[V3D_QPU_A_STVPMV] = A | B,
	[V3D_QPU_A_STVPMD] = A | B,
	[V3D_QPU_A_STVPMP] = A | B,

	[V3D_QPU_A_ITOF] = D | A,
	[V3D_QPU_A_CLZ] = D | A,
	[V3D_QPU_A_UTOF] = D | A,

	[V3D_QPU_A_MOV] = D | A,
	[V3D_QPU_A_FMOV] = D | A,
	[V3D_QPU_A_VPACK] = D | A | B,
	[V3D_QPU_A_V8PACK] = D | A | B,
	[V3D_QPU_A_V10PACK] = D | A | B,
	[V3D_QPU_A_V11FPACK] = D | A | B,
};

static const v3d_uint8 mul_op_args[] = {
	[V3D_QPU_M_ADD] = D | A | B,
	[V3D_QPU_M_SUB] = D | A | B,
	[V3D_QPU_M_UMUL24] = D | A | B,
	[V3D_QPU_M_VFMUL] = D | A | B,
	[V3D_QPU_M_SMUL24] = D | A | B,
	[V3D_QPU_M_MULTOP] = D | A | B,
	[V3D_QPU_M_FMOV] = D | A,
	[V3D_QPU_M_NOP] = 0,
	[V3D_QPU_M_MOV] = D | A,
	[V3D_QPU_M_FMUL] = D | A | B,
	[V3D_QPU_M_FTOUNORM16] = D | A,
	[V3D_QPU_M_FTOSNORM16] = D | A,
	[V3D_QPU_M_VFTOUNORM8] = D | A,
	[V3D_QPU_M_VFTOSNORM8] = D | A,
	[V3D_QPU_M_VFTOUNORM10LO] = D | A,
	[V3D_QPU_M_VFTOUNORM10HI] = D | A,
};

v3d_bool
v3d_qpu_add_op_has_dst(enum v3d_qpu_add_op op)
{
	v3d_assert(op < V3D_ARRAY_SIZE(add_op_args));

	return add_op_args[op] & D;
}

v3d_bool
v3d_qpu_mul_op_has_dst(enum v3d_qpu_mul_op op)
{
	v3d_assert(op < V3D_ARRAY_SIZE(mul_op_args));

	return mul_op_args[op] & D;
}

int
v3d_qpu_add_op_num_src(enum v3d_qpu_add_op op)
{
	v3d_assert(op < V3D_ARRAY_SIZE(add_op_args));

	v3d_uint8 args = add_op_args[op];
	if (args & B)
		return 2;
	else if (args & A)
		return 1;
	else
		return 0;
}

int
v3d_qpu_mul_op_num_src(enum v3d_qpu_mul_op op)
{
	v3d_assert(op < V3D_ARRAY_SIZE(mul_op_args));

	v3d_uint8 args = mul_op_args[op];
	if (args & B)
		return 2;
	else if (args & A)
		return 1;
	else
		return 0;
}

enum v3d_qpu_cond
v3d_qpu_cond_invert(enum v3d_qpu_cond cond)
{
	switch (cond) {
	case V3D_QPU_COND_IFA:
		return V3D_QPU_COND_IFNA;
	case V3D_QPU_COND_IFNA:
		return V3D_QPU_COND_IFA;
	case V3D_QPU_COND_IFB:
		return V3D_QPU_COND_IFNB;
	case V3D_QPU_COND_IFNB:
		return V3D_QPU_COND_IFB;
	default:
		v3d_unreachable("Non-invertible cond");
	}
	return V3D_QPU_COND_IFNA;
}

v3d_bool
v3d_qpu_magic_waddr_is_sfu(enum v3d_qpu_waddr waddr)
{
	switch (waddr) {
	case V3D_QPU_WADDR_RECIP:
	case V3D_QPU_WADDR_RSQRT:
	case V3D_QPU_WADDR_EXP:
	case V3D_QPU_WADDR_LOG:
	case V3D_QPU_WADDR_SIN:
	case V3D_QPU_WADDR_RSQRT2:
		return TRUE;
	default:
		return FALSE;
	}
}

v3d_bool
v3d_qpu_magic_waddr_is_tmu(const struct v3d_device_info *devinfo,
                           enum v3d_qpu_waddr waddr)
{
	if (devinfo->ver >= 40) {
		return ((waddr >= V3D_QPU_WADDR_TMUD &&
				 waddr <= V3D_QPU_WADDR_TMUAU) ||
				(waddr >= V3D_QPU_WADDR_TMUC &&
				 waddr <= V3D_QPU_WADDR_TMUHSLOD));
	} else {
		return ((waddr >= V3D_QPU_WADDR_TMU &&
				 waddr <= V3D_QPU_WADDR_TMUAU) ||
				(waddr >= V3D_QPU_WADDR_TMUC &&
				 waddr <= V3D_QPU_WADDR_TMUHSLOD));
	}
}

v3d_bool
v3d_qpu_waits_on_tmu(const struct v3d_qpu_instr *inst)
{
	return (inst->sig.ldtmu ||
			(inst->type == V3D_QPU_INSTR_TYPE_ALU &&
			 inst->instr.alu.add.op == V3D_QPU_A_TMUWT)); /* debugdebug */
}

v3d_bool
v3d_qpu_magic_waddr_is_tlb(enum v3d_qpu_waddr waddr)
{
	return (waddr == V3D_QPU_WADDR_TLB ||
			waddr == V3D_QPU_WADDR_TLBU);
}

v3d_bool
v3d_qpu_magic_waddr_is_vpm(enum v3d_qpu_waddr waddr)
{
	return (waddr == V3D_QPU_WADDR_VPM ||
			waddr == V3D_QPU_WADDR_VPMU);
}

v3d_bool
v3d_qpu_magic_waddr_is_tsy(enum v3d_qpu_waddr waddr)
{
	return (waddr == V3D_QPU_WADDR_SYNC ||
			waddr == V3D_QPU_WADDR_SYNCB ||
			waddr == V3D_QPU_WADDR_SYNCU);
}

v3d_bool
v3d_qpu_magic_waddr_loads_unif(enum v3d_qpu_waddr waddr)
{
	switch (waddr) {
	case V3D_QPU_WADDR_VPMU:
	case V3D_QPU_WADDR_TLBU:
	case V3D_QPU_WADDR_TMUAU:
	case V3D_QPU_WADDR_SYNCU:
		return TRUE;
	default:
		return FALSE;
	}
}

static v3d_bool
v3d_qpu_add_op_reads_vpm(enum  v3d_qpu_add_op op)
{
	switch (op) {
	case V3D_QPU_A_VPMSETUP:
	case V3D_QPU_A_LDVPMV_IN:
	case V3D_QPU_A_LDVPMV_OUT:
	case V3D_QPU_A_LDVPMD_IN:
	case V3D_QPU_A_LDVPMD_OUT:
	case V3D_QPU_A_LDVPMP:
	case V3D_QPU_A_LDVPMG_IN:
	case V3D_QPU_A_LDVPMG_OUT:
		return TRUE;
	default:
		return FALSE;
	}
}

static v3d_bool
v3d_qpu_add_op_writes_vpm(enum  v3d_qpu_add_op op)
{
	switch (op) {
	case V3D_QPU_A_VPMSETUP:
	case V3D_QPU_A_STVPMV:
	case V3D_QPU_A_STVPMD:
	case V3D_QPU_A_STVPMP:
		return TRUE;
	default:
		return FALSE;
	}
}

v3d_bool
v3d_qpu_reads_tlb(const struct v3d_qpu_instr *inst)
{
	return inst->sig.ldtlb || inst->sig.ldtlbu;
}

v3d_bool
v3d_qpu_writes_tlb(const struct v3d_qpu_instr *inst)
{
	if (inst->type == V3D_QPU_INSTR_TYPE_ALU) {
		if (inst->instr.alu.add.op != V3D_QPU_A_NOP &&
			inst->instr.alu.add.magic_write &&
			v3d_qpu_magic_waddr_is_tlb(inst->instr.alu.add.waddr)) {
			return TRUE;
		}

		if (inst->instr.alu.mul.op != V3D_QPU_M_NOP &&
			inst->instr.alu.mul.magic_write &&
			v3d_qpu_magic_waddr_is_tlb(inst->instr.alu.mul.waddr)) {
			return TRUE; /* debugdebug */
		}
	}

	return FALSE;
}

v3d_bool
v3d_qpu_uses_tlb(const struct v3d_qpu_instr *inst)
{
	return  v3d_qpu_writes_tlb(inst) || v3d_qpu_reads_tlb(inst);
}

v3d_bool
v3d_qpu_uses_sfu(const struct v3d_qpu_instr *inst)
{
	return v3d_qpu_instr_is_sfu(inst) || v3d_qpu_instr_is_legacy_sfu(inst);
}

/* Checks whether the instruction implements an SFU operation by the writing
 * to specific magic register addresses instead of using SFU ALU opcodes.
 */
v3d_bool
v3d_qpu_instr_is_legacy_sfu(const struct v3d_qpu_instr *inst)
{
	if (inst->type == V3D_QPU_INSTR_TYPE_ALU) {
		if (inst->instr.alu.add.op != V3D_QPU_A_NOP &&
			inst->instr.alu.add.magic_write &&
			v3d_qpu_magic_waddr_is_sfu(inst->instr.alu.add.waddr)) {
			return TRUE;
		}

		if (inst->instr.alu.mul.op != V3D_QPU_M_NOP &&
			inst->instr.alu.mul.magic_write &&
			v3d_qpu_magic_waddr_is_sfu(inst->instr.alu.mul.waddr)) {
			return TRUE;                            /* debugdebug */
		}
	}

	return FALSE;
}

v3d_bool
v3d_qpu_instr_is_sfu(const struct v3d_qpu_instr *inst)
{
	if (inst->type == V3D_QPU_INSTR_TYPE_ALU) {
		switch (inst->instr.alu.add.op) {
		case V3D_QPU_A_RECIP:
		case V3D_QPU_A_RSQRT:
		case V3D_QPU_A_EXP:
		case V3D_QPU_A_LOG:
		case V3D_QPU_A_SIN:
		case V3D_QPU_A_RSQRT2:
			return TRUE;
		default:
			return FALSE;
		}
	}
	return FALSE;
}

v3d_bool
v3d_qpu_writes_tmu(const struct v3d_device_info *devinfo,
                   const struct v3d_qpu_instr *inst)
{
	return (inst->type == V3D_QPU_INSTR_TYPE_ALU &&
			((inst->instr.alu.add.op != V3D_QPU_A_NOP &&
			  inst->instr.alu.add.magic_write &&
			  v3d_qpu_magic_waddr_is_tmu(devinfo, inst->instr.alu.add.waddr)) ||
			 (inst->instr.alu.mul.op != V3D_QPU_M_NOP &&
			  inst->instr.alu.mul.magic_write &&
			  v3d_qpu_magic_waddr_is_tmu(devinfo, inst->instr.alu.mul.waddr)))); /* debugdebug */
}

v3d_bool
v3d_qpu_writes_tmu_not_tmuc(const struct v3d_device_info *devinfo,
                            const struct v3d_qpu_instr *inst)
{
	return v3d_qpu_writes_tmu(devinfo, inst) &&
		(!inst->instr.alu.add.magic_write ||
		 inst->instr.alu.add.waddr != V3D_QPU_WADDR_TMUC) &&
		(!inst->instr.alu.mul.magic_write ||
		 inst->instr.alu.mul.waddr != V3D_QPU_WADDR_TMUC); /* debugdebug */
}

v3d_bool
v3d_qpu_reads_vpm(const struct v3d_qpu_instr *inst)
{
	if (inst->sig.ldvpm)
		return TRUE;

	if (inst->type == V3D_QPU_INSTR_TYPE_ALU) {
		if (v3d_qpu_add_op_reads_vpm(inst->instr.alu.add.op))
			return TRUE;
	}

	return FALSE;
}

v3d_bool
v3d_qpu_writes_vpm(const struct v3d_qpu_instr *inst)
{
	if (inst->type == V3D_QPU_INSTR_TYPE_ALU) {
		if (v3d_qpu_add_op_writes_vpm(inst->instr.alu.add.op))
			return TRUE;

		if (inst->instr.alu.add.op != V3D_QPU_A_NOP &&
			inst->instr.alu.add.magic_write &&
			v3d_qpu_magic_waddr_is_vpm(inst->instr.alu.add.waddr)) {
			return TRUE;
		}

		if (inst->instr.alu.mul.op != V3D_QPU_M_NOP &&
			inst->instr.alu.mul.magic_write &&
			v3d_qpu_magic_waddr_is_vpm(inst->instr.alu.mul.waddr)) {
			return TRUE;
		}
	}

	return FALSE;
}

v3d_bool
v3d_qpu_writes_unifa(const struct v3d_device_info *devinfo,
                     const struct v3d_qpu_instr *inst)
{
	if (devinfo->ver < 40)
		return FALSE;

	if (inst->type == V3D_QPU_INSTR_TYPE_ALU) {
		if (inst->instr.alu.add.op != V3D_QPU_A_NOP &&
			inst->instr.alu.add.magic_write &&
			inst->instr.alu.add.waddr == V3D_QPU_WADDR_UNIFA) {
			return TRUE;
		}

		if (inst->instr.alu.mul.op != V3D_QPU_M_NOP &&
			inst->instr.alu.mul.magic_write &&
			inst->instr.alu.mul.waddr == V3D_QPU_WADDR_UNIFA) {
			return TRUE;
		}

		if (v3d_qpu_sig_writes_address(devinfo, &inst->sig) &&
			inst->sig_magic &&
			inst->sig_addr == V3D_QPU_WADDR_UNIFA) {
			return TRUE;
		}
	}

	return FALSE;
}

v3d_bool
v3d_qpu_waits_vpm(const struct v3d_qpu_instr *inst)
{
	return inst->type == V3D_QPU_INSTR_TYPE_ALU &&
		inst->instr.alu.add.op == V3D_QPU_A_VPMWT;
}

v3d_bool
v3d_qpu_reads_or_writes_vpm(const struct v3d_qpu_instr *inst)
{
	return v3d_qpu_reads_vpm(inst) || v3d_qpu_writes_vpm(inst);
}

v3d_bool
v3d_qpu_uses_vpm(const struct v3d_qpu_instr *inst)
{
	return v3d_qpu_reads_vpm(inst) ||
		v3d_qpu_writes_vpm(inst) ||
		v3d_qpu_waits_vpm(inst);
}

static v3d_bool
qpu_writes_magic_waddr_explicitly(const struct v3d_device_info *devinfo,
                                  const struct v3d_qpu_instr *inst,
                                  v3d_uint32 waddr)
{
	if (inst->type == V3D_QPU_INSTR_TYPE_ALU) {
		if (inst->instr.alu.add.op != V3D_QPU_A_NOP &&
			inst->instr.alu.add.magic_write && inst->instr.alu.add.waddr == waddr)
			return TRUE;

		if (inst->instr.alu.mul.op != V3D_QPU_M_NOP &&
			inst->instr.alu.mul.magic_write && inst->instr.alu.mul.waddr == waddr)
			return TRUE;
	}

	if (v3d_qpu_sig_writes_address(devinfo, &inst->sig) &&
		inst->sig_magic && inst->sig_addr == waddr) {
		return TRUE;
	}

	return FALSE;
}

v3d_bool
v3d_qpu_writes_r3(const struct v3d_device_info *devinfo,
                  const struct v3d_qpu_instr *inst)
{
	if(!devinfo->has_accumulators)
		return FALSE;

	if (qpu_writes_magic_waddr_explicitly(devinfo, inst, V3D_QPU_WADDR_R3))
		return TRUE;

	return (devinfo->ver < 41 && inst->sig.ldvary) || inst->sig.ldvpm;
}

v3d_bool
v3d_qpu_writes_r4(const struct v3d_device_info *devinfo,
                  const struct v3d_qpu_instr *inst)
{
	if (!devinfo->has_accumulators)
		return FALSE;

	if (inst->type == V3D_QPU_INSTR_TYPE_ALU) {
		if (inst->instr.alu.add.op != V3D_QPU_A_NOP &&
			inst->instr.alu.add.magic_write &&
			(inst->instr.alu.add.waddr == V3D_QPU_WADDR_R4 ||
			 v3d_qpu_magic_waddr_is_sfu(inst->instr.alu.add.waddr))) {
			return TRUE;
		}

		if (inst->instr.alu.mul.op != V3D_QPU_M_NOP &&
			inst->instr.alu.mul.magic_write &&
			(inst->instr.alu.mul.waddr == V3D_QPU_WADDR_R4 ||
			 v3d_qpu_magic_waddr_is_sfu(inst->instr.alu.mul.waddr))) {
			return TRUE;
		}
	}

	if (v3d_qpu_sig_writes_address(devinfo, &inst->sig)) {
		if (inst->sig_magic && inst->sig_addr == V3D_QPU_WADDR_R4)
			return TRUE;
	} else if (inst->sig.ldtmu) {
		return TRUE;
	}

	return FALSE;
}

v3d_bool
v3d_qpu_writes_r5(const struct v3d_device_info *devinfo,
                  const struct v3d_qpu_instr *inst)
{
	if (!devinfo->has_accumulators)
		return FALSE;

	if (qpu_writes_magic_waddr_explicitly(devinfo, inst, V3D_QPU_WADDR_R5))
		return TRUE;

	return inst->sig.ldvary || inst->sig.ldunif || inst->sig.ldunifa;
}

v3d_bool
v3d_qpu_writes_accum(const struct v3d_device_info *devinfo,
                     const struct v3d_qpu_instr *inst)
{
	if (!devinfo->has_accumulators)
		return FALSE;

	if (v3d_qpu_writes_r5(devinfo, inst))
		return TRUE;
	if (v3d_qpu_writes_r4(devinfo, inst))
		return TRUE;
	if (v3d_qpu_writes_r3(devinfo, inst))
		return TRUE;
	if (qpu_writes_magic_waddr_explicitly(devinfo, inst, V3D_QPU_WADDR_R2))
		return TRUE;
	if (qpu_writes_magic_waddr_explicitly(devinfo, inst, V3D_QPU_WADDR_R1))
		return TRUE;
	if (qpu_writes_magic_waddr_explicitly(devinfo, inst, V3D_QPU_WADDR_R0))
		return TRUE;

	return FALSE;
}

v3d_bool
v3d_qpu_writes_rf0_implicitly(const struct v3d_device_info *devinfo,
                              const struct v3d_qpu_instr *inst)
{
	if (devinfo->ver >= 71 &&
		(inst->sig.ldvary || inst->sig.ldunif || inst->sig.ldunifa)) {
		return TRUE;
	}

	return FALSE;
}

v3d_bool
v3d_qpu_uses_mux(const struct v3d_qpu_instr *inst, enum v3d_qpu_mux mux)
{
	int add_nsrc = v3d_qpu_add_op_num_src(inst->instr.alu.add.op);
	int mul_nsrc = v3d_qpu_mul_op_num_src(inst->instr.alu.mul.op);

	return ((add_nsrc > 0 && inst->instr.alu.add.a.input.mux == mux) ||
			(add_nsrc > 1 && inst->instr.alu.add.b.input.mux == mux) ||
			(mul_nsrc > 0 && inst->instr.alu.mul.a.input.mux == mux) ||
			(mul_nsrc > 1 && inst->instr.alu.mul.b.input.mux == mux));
}

v3d_bool
v3d71_qpu_reads_raddr(const struct v3d_qpu_instr *inst, v3d_uint8 raddr)
{
	int add_nsrc = v3d_qpu_add_op_num_src(inst->instr.alu.add.op);
	int mul_nsrc = v3d_qpu_mul_op_num_src(inst->instr.alu.mul.op);

	return (add_nsrc > 0 && !inst->sig.small_imm_a && inst->instr.alu.add.a.input.raddr == raddr) ||
		(add_nsrc >    1 && !inst->sig.small_imm_b && inst->instr.alu.add.b.input.raddr == raddr) ||
		(mul_nsrc >    0 && !inst->sig.small_imm_c && inst->instr.alu.mul.a.input.raddr == raddr) ||
		(mul_nsrc >    1 && !inst->sig.small_imm_d && inst->instr.alu.mul.b.input.raddr == raddr);
}

v3d_bool
v3d71_qpu_writes_waddr_explicitly(const struct v3d_device_info *devinfo,
                                  const struct v3d_qpu_instr *inst,
                                  v3d_uint8 waddr)
{
	if (inst->type != V3D_QPU_INSTR_TYPE_ALU)
		return FALSE;

	if (v3d_qpu_add_op_has_dst(inst->instr.alu.add.op) &&
		!inst->instr.alu.add.magic_write &&
		inst->instr.alu.add.waddr == waddr) {
		return TRUE;
	}

	if (v3d_qpu_mul_op_has_dst(inst->instr.alu.mul.op) &&
		!inst->instr.alu.mul.magic_write &&
		inst->instr.alu.mul.waddr == waddr) {
		return TRUE;
	}

	if (v3d_qpu_sig_writes_address(devinfo, &inst->sig) &&
		!inst->sig_magic && inst->sig_addr == waddr) {
		return TRUE;
	}

	return FALSE;
}

v3d_bool
v3d_qpu_sig_writes_address(const struct v3d_device_info *devinfo,
                           const struct v3d_qpu_sig *sig)
{
	if (devinfo->ver < 41)
		return FALSE;

	return (sig->ldunifrf ||
			sig->ldunifarf ||
			sig->ldvary ||
			sig->ldtmu ||
			sig->ldtlb ||
			sig->ldtlbu);
}

v3d_bool
v3d_qpu_reads_flags(const struct v3d_qpu_instr *inst)
{
	if (inst->type == V3D_QPU_INSTR_TYPE_BRANCH) {
		return inst->instr.branch.cond != V3D_QPU_BRANCH_COND_ALWAYS;
	} else if (inst->type == V3D_QPU_INSTR_TYPE_ALU) {
		if (inst->flags.ac != V3D_QPU_COND_NONE ||
			inst->flags.mc != V3D_QPU_COND_NONE ||
			inst->flags.auf != V3D_QPU_UF_NONE ||
			inst->flags.muf != V3D_QPU_UF_NONE)
			return TRUE;

		switch (inst->instr.alu.add.op) {
		case V3D_QPU_A_VFLA:
		case V3D_QPU_A_VFLNA:
		case V3D_QPU_A_VFLB:
		case V3D_QPU_A_VFLNB:
		case V3D_QPU_A_FLAPUSH:
		case V3D_QPU_A_FLBPUSH:
		case V3D_QPU_A_FLAFIRST:
		case V3D_QPU_A_FLNAFIRST:
			return TRUE;
		default:
			break;
		}
	}

	return FALSE;
}

v3d_bool
v3d_qpu_writes_flags(const struct v3d_qpu_instr *inst)
{
	if (inst->flags.apf != V3D_QPU_PF_NONE ||
		inst->flags.mpf != V3D_QPU_PF_NONE ||
		inst->flags.auf != V3D_QPU_UF_NONE ||
		inst->flags.muf != V3D_QPU_UF_NONE) {
		return TRUE;
	}

	return FALSE;
}

v3d_bool
v3d_qpu_unpacks_f32(const struct v3d_qpu_instr *inst)
{
	if (inst->type != V3D_QPU_INSTR_TYPE_ALU)
		return FALSE;

	switch (inst->instr.alu.add.op) {
	case V3D_QPU_A_FADD:
	case V3D_QPU_A_FADDNF:
	case V3D_QPU_A_FSUB:
	case V3D_QPU_A_FMIN:
	case V3D_QPU_A_FMAX:
	case V3D_QPU_A_FCMP:
	case V3D_QPU_A_FROUND:
	case V3D_QPU_A_FTRUNC:
	case V3D_QPU_A_FFLOOR:
	case V3D_QPU_A_FCEIL:
	case V3D_QPU_A_FDX:
	case V3D_QPU_A_FDY:
	case V3D_QPU_A_FTOIN:
	case V3D_QPU_A_FTOIZ:
	case V3D_QPU_A_FTOUZ:
	case V3D_QPU_A_FTOC:
	case V3D_QPU_A_VFPACK:
		return TRUE;
		break;
	default:
		break;
	}

	switch (inst->instr.alu.mul.op) {
	case V3D_QPU_M_FMOV:
	case V3D_QPU_M_FMUL:
		return TRUE;
		break;
	default:
		break;
	}

	return FALSE;
}
v3d_bool
v3d_qpu_unpacks_f16(const struct v3d_qpu_instr *inst)
{
	if (inst->type != V3D_QPU_INSTR_TYPE_ALU)
		return FALSE;

	switch (inst->instr.alu.add.op) {
	case V3D_QPU_A_VFMIN:
	case V3D_QPU_A_VFMAX:
		return TRUE;
		break;
	default:
		break;
	}

	switch (inst->instr.alu.mul.op) {
	case V3D_QPU_M_VFMUL:
		return TRUE;
		break;
	default:
		break;
	}

	return FALSE;
}

v3d_bool
v3d_qpu_is_nop(struct v3d_qpu_instr *inst)
{
	static const struct v3d_qpu_sig nosig = { 0 };

	if (inst->type != V3D_QPU_INSTR_TYPE_ALU)
		return FALSE;
	if (inst->instr.alu.add.op != V3D_QPU_A_NOP)
		return FALSE;
	if (inst->instr.alu.mul.op != V3D_QPU_M_NOP)
		return FALSE;
	if (v3d_memcmp(&inst->sig, &nosig, sizeof(nosig)))
		return FALSE;
	return TRUE;
}

// >>> qpu_pack.c

#ifndef QPU_MASK
#define QPU_MASK(high, low)										\
	((((v3d_uint64)1ULL << ((high) - (low) + 1)) - 1) << (low))
#endif /* QPU_MASK */

#define V3D_QPU_OP_MUL_SHIFT                58
#define V3D_QPU_OP_MUL_MASK                 QPU_MASK(63, 58)

#define V3D_QPU_SIG_SHIFT                   53
#define V3D_QPU_SIG_MASK                    QPU_MASK(57, 53)

#define V3D_QPU_COND_SHIFT                  46
#define V3D_QPU_COND_MASK                   QPU_MASK(52, 46)
#define V3D_QPU_COND_SIG_MAGIC_ADDR         (1 << 6)

#define V3D_QPU_MM                          QPU_MASK(45, 45)
#define V3D_QPU_MA                          QPU_MASK(44, 44)

#define V3D_QPU_WADDR_M_SHIFT               38
#define V3D_QPU_WADDR_M_MASK                QPU_MASK(43, 38)

#define V3D_QPU_BRANCH_ADDR_LOW_SHIFT       35
#define V3D_QPU_BRANCH_ADDR_LOW_MASK        QPU_MASK(55, 35)

#define V3D_QPU_WADDR_A_SHIFT               32
#define V3D_QPU_WADDR_A_MASK                QPU_MASK(37, 32)

#define V3D_QPU_BRANCH_COND_SHIFT           32
#define V3D_QPU_BRANCH_COND_MASK            QPU_MASK(34, 32)

#define V3D_QPU_BRANCH_ADDR_HIGH_SHIFT      24
#define V3D_QPU_BRANCH_ADDR_HIGH_MASK       QPU_MASK(31, 24)

#define V3D_QPU_OP_ADD_SHIFT                24
#define V3D_QPU_OP_ADD_MASK                 QPU_MASK(31, 24)

#define V3D_QPU_MUL_B_SHIFT                 21
#define V3D_QPU_MUL_B_MASK                  QPU_MASK(23, 21)

#define V3D_QPU_BRANCH_MSFIGN_SHIFT         21
#define V3D_QPU_BRANCH_MSFIGN_MASK          QPU_MASK(22, 21)

#define V3D_QPU_MUL_A_SHIFT                 18
#define V3D_QPU_MUL_A_MASK                  QPU_MASK(20, 18)

#define V3D_QPU_RADDR_C_SHIFT               18
#define V3D_QPU_RADDR_C_MASK                QPU_MASK(23, 18)

#define V3D_QPU_ADD_B_SHIFT                 15
#define V3D_QPU_ADD_B_MASK                  QPU_MASK(17, 15)

#define V3D_QPU_BRANCH_BDU_SHIFT            15
#define V3D_QPU_BRANCH_BDU_MASK             QPU_MASK(17, 15)

#define V3D_QPU_BRANCH_UB                   QPU_MASK(14, 14)

#define V3D_QPU_ADD_A_SHIFT                 12
#define V3D_QPU_ADD_A_MASK                  QPU_MASK(14, 12)

#define V3D_QPU_BRANCH_BDI_SHIFT            12
#define V3D_QPU_BRANCH_BDI_MASK             QPU_MASK(13, 12)

#define V3D_QPU_RADDR_D_SHIFT               12
#define V3D_QPU_RADDR_D_MASK                QPU_MASK(17, 12)

#define V3D_QPU_RADDR_A_SHIFT               6
#define V3D_QPU_RADDR_A_MASK                QPU_MASK(11, 6)

#define V3D_QPU_RADDR_B_SHIFT               0
#define V3D_QPU_RADDR_B_MASK                QPU_MASK(5, 0)

enum v3d_qpu_input_class {
	V3D_QPU_ADD_A,
	V3D_QPU_ADD_B,
	V3D_QPU_MUL_A,
	V3D_QPU_MUL_B,
    V3D_QPU_OP_MUL,
    V3D_QPU_SIG,
    V3D_QPU_COND,
    V3D_QPU_WADDR_M,
    V3D_QPU_BRANCH_ADDR_LOW,
    V3D_QPU_WADDR_A,
    V3D_QPU_BRANCH_COND,
    V3D_QPU_BRANCH_ADDR_HIGH,
    V3D_QPU_OP_ADD,
    V3D_QPU_BRANCH_MSFIGN,
    V3D_QPU_RADDR_C,
    V3D_QPU_BRANCH_BDU,
    V3D_QPU_BRANCH_BDI,
    V3D_QPU_RADDR_D,
    V3D_QPU_RADDR_A,
    V3D_QPU_RADDR_B
};

static const v3d_uint32 shiftmask[] =
{
    /*V3D_QPU_ADD_A,*/             12, 14, 12,
    /*V3D_QPU_ADD_B,*/             15, 17, 15,
    /*V3D_QPU_MUL_A,*/             18, 20, 18,
    /*V3D_QPU_MUL_B,*/             21, 23, 21,
    /*V3D_QPU_OP_MUL,*/            58, 63, 58,
    /*V3D_QPU_SIG,*/               53, 57, 53,
    /*V3D_QPU_COND,*/              46, 52, 46,
    /*V3D_QPU_WADDR_M,*/           38, 43, 38,
    /*V3D_QPU_BRANCH_ADDR_LOW,*/   35, 55, 35,
    /*V3D_QPU_WADDR_A,*/           32, 37, 32,
    /*V3D_QPU_BRANCH_COND,*/       32, 34, 32,
    /*V3D_QPU_BRANCH_ADDR_HIGH,*/  24, 31, 24,
    /*V3D_QPU_OP_ADD,*/            24, 31, 24,
    /*V3D_QPU_BRANCH_MSFIGN,*/     21, 22, 21,
    /*V3D_QPU_RADDR_C,*/           18, 23, 18,
    /*V3D_QPU_BRANCH_BDU,*/        15, 17, 15,
    /*V3D_QPU_BRANCH_BDI,*/        12, 13, 12,
    /*V3D_QPU_RADDR_D,*/           12, 17, 12,
    /*V3D_QPU_RADDR_A,*/            6, 11,  6,
    /*V3D_QPU_RADDR_B,*/            0,  5,  0,
};

#ifndef QPU_SET_FIELD
#define QPU_SET_FIELD qpusetfield
#define QPU_GET_FIELD qpugetfield
//#define QPU_UPDATE_FIELD qpuupdatefield


v3d_uint64 qpusetfield(v3d_uint32 value, v3d_uint32 field)
{
        v3d_uint64 fieldval = (v3d_uint64)(value) << shiftmask[field * 3];
		v3d_assert((fieldval & ~ QPU_MASK(shiftmask[field * 3 + 1], shiftmask[field * 3 + 2])) == 0);
		return (fieldval & QPU_MASK(shiftmask[field * 3 + 1], shiftmask[field * 3 + 2]));
}


v3d_uint64 qpugetfield(v3d_uint32 word, v3d_uint32 field)
{
        return (v3d_uint32)((word & QPU_MASK(shiftmask[field * 3 + 1], shiftmask[field * 3 + 2])) >> shiftmask[field * 3]);
}


/*
#define QPU_SET_FIELD(value, field)										\
	({																	\
		v3d_uint64 fieldval = (v3d_uint64)(value) << field ## _SHIFT;	\
		v3d_assert((fieldval & ~ field ## _MASK) == 0);					\
		fieldval & field ## _MASK;										\
	})

#define QPU_GET_FIELD(word, field) ((v3d_uint32)(((word)  & field ## _MASK) >> field ## _SHIFT))

#define QPU_UPDATE_FIELD(inst, value, field)						\
	(((inst) & ~(field ## _MASK)) | QPU_SET_FIELD(value, field))
*/
#endif /*QPU_SET_FIELD*/

#define THRSW .thrsw = TRUE
#define LDUNIF .ldunif = TRUE
#define LDUNIFRF .ldunifrf = TRUE
#define LDUNIFA .ldunifa = TRUE
#define LDUNIFARF .ldunifarf = TRUE
#define LDTMU .ldtmu = TRUE
#define LDVARY .ldvary = TRUE
#define LDVPM .ldvpm = TRUE
#define LDTLB .ldtlb = TRUE
#define LDTLBU .ldtlbu = TRUE
#define UCB .ucb = TRUE
#define ROT .rotate = TRUE
#define WRTMUC .wrtmuc = TRUE
#define SMIMM_A .small_imm_a = TRUE
#define SMIMM_B .small_imm_b = TRUE
#define SMIMM_C .small_imm_c = TRUE
#define SMIMM_D .small_imm_d = TRUE

static const struct v3d_qpu_sig v33_sig_map[] = {
	/*      MISC   R3       R4      R5 */
	[0]  = { FALSE                         },
	[1]  = { THRSW,                        },
	[2]  = {                        LDUNIF },
	[3]  = { THRSW,                 LDUNIF },
	[4]  = {                LDTMU,         },
	[5]  = { THRSW,         LDTMU,         },
	[6]  = {                LDTMU,  LDUNIF },
	[7]  = { THRSW,         LDTMU,  LDUNIF },
	[8]  = {        LDVARY,                },
	[9]  = { THRSW, LDVARY,                },
	[10] = {        LDVARY,         LDUNIF },
	[11] = { THRSW, LDVARY,         LDUNIF },
	[12] = {        LDVARY, LDTMU,         },
	[13] = { THRSW, LDVARY, LDTMU,         },
	[14] = { SMIMM_B, LDVARY,              },
	[15] = { SMIMM_B,                      },
	[16] = {        LDTLB,                 },
	[17] = {        LDTLBU,                },
	/* 18-21 reserved */
	[22] = { UCB,                          },
	[23] = { ROT,                          },
	[24] = {        LDVPM,                 },
	[25] = { THRSW, LDVPM,                 },
	[26] = {        LDVPM,          LDUNIF },
	[27] = { THRSW, LDVPM,          LDUNIF },
	[28] = {        LDVPM, LDTMU,          },
	[29] = { THRSW, LDVPM, LDTMU,          },
	[30] = { SMIMM_B, LDVPM,               },
	[31] = { SMIMM_B,                      },
};

static const struct v3d_qpu_sig v40_sig_map[] = {
	/*      MISC    R3      R4      R5 */
	[0]  = { FALSE                         },
	[1]  = { THRSW,                        },
	[2]  = {                        LDUNIF },
	[3]  = { THRSW,                 LDUNIF },
	[4]  = {                LDTMU,         },
	[5]  = { THRSW,         LDTMU,         },
	[6]  = {                LDTMU,  LDUNIF },
	[7]  = { THRSW,         LDTMU,  LDUNIF },
	[8]  = {        LDVARY,                },
	[9]  = { THRSW, LDVARY,                },
	[10] = {        LDVARY,         LDUNIF },
	[11] = { THRSW, LDVARY,         LDUNIF },
	/* 12-13 reserved */
	[14] = { SMIMM_B, LDVARY,              },
	[15] = { SMIMM_B,                      },
	[16] = {        LDTLB,                 },
	[17] = {        LDTLBU,                },
	[18] = {                        WRTMUC },
	[19] = { THRSW,                 WRTMUC },
	[20] = {        LDVARY,         WRTMUC },
	[21] = { THRSW, LDVARY,         WRTMUC },
	[22] = { UCB,                          },
	[23] = { ROT,                          },
	/* 24-30 reserved */
	[31] = { SMIMM_B,       LDTMU,         },
};

static const struct v3d_qpu_sig v41_sig_map[] = {
	/*      MISC       phys    R5 */
	[0]  = { FALSE                    },
	[1]  = { THRSW,                   },
	[2]  = {                   LDUNIF },
	[3]  = { THRSW,            LDUNIF },
	[4]  = {           LDTMU,         },
	[5]  = { THRSW,    LDTMU,         },
	[6]  = {           LDTMU,  LDUNIF },
	[7]  = { THRSW,    LDTMU,  LDUNIF },
	[8]  = {           LDVARY,        },
	[9]  = { THRSW,    LDVARY,        },
	[10] = {           LDVARY, LDUNIF },
	[11] = { THRSW,    LDVARY, LDUNIF },
	[12] = { LDUNIFRF                 },
	[13] = { THRSW,    LDUNIFRF       },
	[14] = { SMIMM_B,    LDVARY       },
	[15] = { SMIMM_B,                 },
	[16] = {           LDTLB,         },
	[17] = {           LDTLBU,        },
	[18] = {                          WRTMUC },
	[19] = { THRSW,                   WRTMUC },
	[20] = {           LDVARY,        WRTMUC },
	[21] = { THRSW,    LDVARY,        WRTMUC },
	[22] = { UCB,                     },
	[23] = { ROT,                     },
	[24] = {                   LDUNIFA},
	[25] = { LDUNIFARF                },
	/* 26-30 reserved */
	[31] = { SMIMM_B,          LDTMU, },
};


static const struct v3d_qpu_sig v71_sig_map[] = {
	/*      MISC       phys    RF0 */
	[0]  = { FALSE                    },
	[1]  = { THRSW,                   },
	[2]  = {                   LDUNIF },
	[3]  = { THRSW,            LDUNIF },
	[4]  = {           LDTMU,         },
	[5]  = { THRSW,    LDTMU,         },
	[6]  = {           LDTMU,  LDUNIF },
	[7]  = { THRSW,    LDTMU,  LDUNIF },
	[8]  = {           LDVARY,        },
	[9]  = { THRSW,    LDVARY,        },
	[10] = {           LDVARY, LDUNIF },
	[11] = { THRSW,    LDVARY, LDUNIF },
	[12] = { LDUNIFRF                 },
	[13] = { THRSW,    LDUNIFRF       },
	[14] = { SMIMM_A,                 },
	[15] = { SMIMM_B,                 },
	[16] = {           LDTLB,         },
	[17] = {           LDTLBU,        },
	[18] = {                          WRTMUC },
	[19] = { THRSW,                   WRTMUC },
	[20] = {           LDVARY,        WRTMUC },
	[21] = { THRSW,    LDVARY,        WRTMUC },
	[22] = { UCB,                     },
	/* 23 reserved */
	[24] = {                   LDUNIFA},
	[25] = { LDUNIFARF                },
	/* 26-29 reserved */
	[30] = { SMIMM_C,                 },
	[31] = { SMIMM_D,                 },
};

v3d_bool
v3d_qpu_sig_unpack(const struct v3d_device_info *devinfo,
                   v3d_uint32 packed_sig,
                   struct v3d_qpu_sig *sig)
{
	if (packed_sig >= V3D_ARRAY_SIZE(v33_sig_map))
		return FALSE;

	if (devinfo->ver >= 71)
		*sig = v71_sig_map[packed_sig];
	else if (devinfo->ver >= 41)
		*sig = v41_sig_map[packed_sig];
	else if (devinfo->ver == 40)
		*sig = v40_sig_map[packed_sig];
	else
		*sig = v33_sig_map[packed_sig];

	/* Signals with zeroed unpacked contents after element 0 are reserved. */
	return (packed_sig == 0 ||
			memcmp(sig, &v33_sig_map[0], sizeof(*sig)) != 0);
}

v3d_bool
v3d_qpu_sig_pack(const struct v3d_device_info *devinfo,
                 const struct v3d_qpu_sig *sig,
                 v3d_uint32 *packed_sig)
{
	static const struct v3d_qpu_sig *map;

	if (devinfo->ver >= 71)
		map = v71_sig_map;
	else if (devinfo->ver >= 41)
		map = v41_sig_map;
	else if (devinfo->ver == 40)
		map = v40_sig_map;
	else
		map = v33_sig_map;

	for (int i = 0; i < V3D_ARRAY_SIZE(v33_sig_map); i++) {
		if (memcmp(&map[i], sig, sizeof(*sig)) == 0) {
			*packed_sig = i;
			return TRUE;
		}
	}

	return FALSE;
}

static const v3d_uint32 small_immediates[] = {
	0, 1, 2, 3,
	4, 5, 6, 7,
	8, 9, 10, 11,
	12, 13, 14, 15,
	-16, -15, -14, -13,
	-12, -11, -10, -9,
	-8, -7, -6, -5,
	-4, -3, -2, -1,
	0x3b800000, /* 2.0^-8 */
	0x3c000000, /* 2.0^-7 */
	0x3c800000, /* 2.0^-6 */
	0x3d000000, /* 2.0^-5 */
	0x3d800000, /* 2.0^-4 */
	0x3e000000, /* 2.0^-3 */
	0x3e800000, /* 2.0^-2 */
	0x3f000000, /* 2.0^-1 */
	0x3f800000, /* 2.0^0 */
	0x40000000, /* 2.0^1 */
	0x40800000, /* 2.0^2 */
	0x41000000, /* 2.0^3 */
	0x41800000, /* 2.0^4 */
	0x42000000, /* 2.0^5 */
	0x42800000, /* 2.0^6 */
	0x43000000, /* 2.0^7 */
};

v3d_bool
v3d_qpu_small_imm_unpack(const struct v3d_device_info *devinfo,
                         v3d_uint32 packed_small_immediate,
                         v3d_uint32 *small_immediate)
{
	if (packed_small_immediate >= V3D_ARRAY_SIZE(small_immediates))
		return FALSE;

	*small_immediate = small_immediates[packed_small_immediate];
	return TRUE;
}

v3d_bool
v3d_qpu_small_imm_pack(const struct v3d_device_info *devinfo,
                       v3d_uint32 value,
                       v3d_uint32 *packed_small_immediate)
{
	V3D_STATIC_ASSERT(V3D_V3D_ARRAY_SIZE(small_immediates) == 48);

	for (int i = 0; i < V3D_ARRAY_SIZE(small_immediates); i++) {
		if (small_immediates[i] == value) {
			*packed_small_immediate = i;
			return TRUE;
		}
	}

	return FALSE;
}

// See small_immediates[]. This array has everything to prompt the user what they can possibly provide.
// small_immediates_packed_values[] should be used to get the proper packed immediate.
static const char* small_immediates_names[] =
{
	"0", "1", "2", "3",
	"4", "5", "6", "7",
	"8", "9", "10", "11",
	"12", "13", "14", "15",
	"-16", "-15", "-14", "-13",
	"-12", "-11", "-10", "-9",
	"-8", "-7", "-6", "-5",
	"-4", "-3", "-2", "-1",
	// Extension by Macoy for referring to attributes past 15 in e.g. ldvpm. These CANNOT be used for math.
	// TODO: Verify this is okay
	"16", "17", "18", "19",
	"20", "21", "22", "23",
	"24", "25", "26", "27",
	"28", "29", "30", "31",
	// Extension by Macoy for writing these by humans
	"2f^-8", "2f^-7", "2f^-6", "2f^-5",
	"2f^-4", "2f^-3", "2f^-2", "2f^-1",
	"2f^0", "2f^1", "2f^2", "2f^3",
	"2f^4", "2f^5", "2f^6", "2f^7",
	// Floating point exponents
	"0x3b800000", /* 2.0^-8 */
	"0x3c000000", /* 2.0^-7 */
	"0x3c800000", /* 2.0^-6 */
	"0x3d000000", /* 2.0^-5 */
	"0x3d800000", /* 2.0^-4 */
	"0x3e000000", /* 2.0^-3 */
	"0x3e800000", /* 2.0^-2 */
	"0x3f000000", /* 2.0^-1 */
	"0x3f800000", /* 2.0^0 */
	"0x40000000", /* 2.0^1 */
	"0x40800000", /* 2.0^2 */
	"0x41000000", /* 2.0^3 */
	"0x41800000", /* 2.0^4 */
	"0x42000000", /* 2.0^5 */
	"0x42800000", /* 2.0^6 */
	"0x43000000", /* 2.0^7 */
};

// Must correspond exactly with small_immediates_names[]
static v3d_uint32 small_immediates_packed_indices[] =
{
	0, 1, 2, 3,
	4, 5, 6, 7,
	8, 9, 10, 11,
	12, 13, 14, 15,
	16, 17, 18, 19,
	20, 21, 22, 23,
	24, 25, 26, 27,
	28, 29, 30, 31,
	// Extension (positive to 31)
	16, 17, 18, 19,
	20, 21, 22, 23,
	24, 25, 26, 27,
	28, 29, 30, 31,
	// Extension (2.0^x)
	32, 33, 34, 35,
	36, 37, 38, 39,
	40, 41, 42, 43,
	44, 45, 46, 47,
	// Floating point exponents (hex)
	32,
	33,
	34,
	35,
	36,
	37,
	38,
	39,
	40,
	41,
	42,
	43,
	44,
	45,
	46,
	47,
};

// This accepts the following:
// 0 through 15
// -15 through -1
// 16 through 31 (Extension; invalid for math)
// 0x3b800000... (see small_immediates_names for valid hex constants)
// 2^-8 through 2^7 (Extension)
v3d_bool v3d_qpu_small_imm_from_name(const char* name, v3d_uint32* packed_small_immediate,
                                     const char** endOfNameOut)
{
	for (int index = 0; index < V3D_ARRAY_SIZE(small_immediates_names); ++index)
	{
		if (v3d_symbol_equals(small_immediates_names[index], name, endOfNameOut))
		{
			*packed_small_immediate = small_immediates_packed_indices[index];
			return TRUE;
		}
	}
	return FALSE;
}

v3d_bool
v3d_qpu_flags_unpack(const struct v3d_device_info *devinfo,
                     v3d_uint32 packed_cond,
                     struct v3d_qpu_flags *cond)
{
	static const enum v3d_qpu_cond cond_map[4] = {
		[0] = V3D_QPU_COND_IFA,
		[1] = V3D_QPU_COND_IFB,
		[2] = V3D_QPU_COND_IFNA,
		[3] = V3D_QPU_COND_IFNB,
	};

	cond->ac = V3D_QPU_COND_NONE;
	cond->mc = V3D_QPU_COND_NONE;
	cond->apf = V3D_QPU_PF_NONE;
	cond->mpf = V3D_QPU_PF_NONE;
	cond->auf = V3D_QPU_UF_NONE;
	cond->muf = V3D_QPU_UF_NONE;

	if (packed_cond == 0) {
		return TRUE;
	} else if (packed_cond >> 2 == 0) {
		cond->apf = packed_cond & 0x3;
	} else if (packed_cond >> 4 == 0) {
		cond->auf = (packed_cond & 0xf) - 4 + V3D_QPU_UF_ANDZ;
	} else if (packed_cond == 0x10) {
		return FALSE;
	} else if (packed_cond >> 2 == 0x4) {
		cond->mpf = packed_cond & 0x3;
	} else if (packed_cond >> 4 == 0x1) {
		cond->muf = (packed_cond & 0xf) - 4 + V3D_QPU_UF_ANDZ;
	} else if (packed_cond >> 4 == 0x2) {
		cond->ac = ((packed_cond >> 2) & 0x3) + V3D_QPU_COND_IFA;
		cond->mpf = packed_cond & 0x3;
	} else if (packed_cond >> 4 == 0x3) {
		cond->mc = ((packed_cond >> 2) & 0x3) + V3D_QPU_COND_IFA;
		cond->apf = packed_cond & 0x3;
	} else if (packed_cond >> 6) {
		cond->mc = cond_map[(packed_cond >> 4) & 0x3];
		if (((packed_cond >> 2) & 0x3) == 0) {
			cond->ac = cond_map[packed_cond & 0x3];
		} else {
			cond->auf = (packed_cond & 0xf) - 4 + V3D_QPU_UF_ANDZ;
		}
	}

	return TRUE;
}

v3d_bool
v3d_qpu_flags_pack(const struct v3d_device_info *devinfo,
                   const struct v3d_qpu_flags *cond,
                   v3d_uint32 *packed_cond)
{
#define AC (1 << 0)
#define MC (1 << 1)
#define APF (1 << 2)
#define MPF (1 << 3)
#define AUF (1 << 4)
#define MUF (1 << 5)
	static const struct {
		v3d_uint8 flags_present;
		v3d_uint8 bits;
	} flags_table[] = {
		{ 0,        0 },
		{ APF,      0 },
		{ AUF,      0 },
		{ MPF,      (1 << 4) },
		{ MUF,      (1 << 4) },
		{ AC,       (1 << 5) },
		{ AC | MPF, (1 << 5) },
		{ MC,       (1 << 5) | (1 << 4) },
		{ MC | APF, (1 << 5) | (1 << 4) },
		{ MC | AC,  (1 << 6) },
		{ MC | AUF, (1 << 6) },
	};

	v3d_uint8 flags_present = 0;
	if (cond->ac != V3D_QPU_COND_NONE)
		flags_present |= AC;
	if (cond->mc != V3D_QPU_COND_NONE)
		flags_present |= MC;
	if (cond->apf != V3D_QPU_PF_NONE)
		flags_present |= APF;
	if (cond->mpf != V3D_QPU_PF_NONE)
		flags_present |= MPF;
	if (cond->auf != V3D_QPU_UF_NONE)
		flags_present |= AUF;
	if (cond->muf != V3D_QPU_UF_NONE)
		flags_present |= MUF;

	for (int i = 0; i < V3D_ARRAY_SIZE(flags_table); i++) {
		if (flags_table[i].flags_present != flags_present)
			continue;

		*packed_cond = flags_table[i].bits;

		*packed_cond |= cond->apf;
		*packed_cond |= cond->mpf;

		if (flags_present & AUF)
			*packed_cond |= cond->auf - V3D_QPU_UF_ANDZ + 4;
		if (flags_present & MUF)
			*packed_cond |= cond->muf - V3D_QPU_UF_ANDZ + 4;

		if (flags_present & AC) {
			if (*packed_cond & (1 << 6))
				*packed_cond |= cond->ac - V3D_QPU_COND_IFA;
			else
				*packed_cond |= (cond->ac -
								 V3D_QPU_COND_IFA) << 2;
		}

		if (flags_present & MC) {
			if (*packed_cond & (1 << 6))
				*packed_cond |= (cond->mc -
								 V3D_QPU_COND_IFA) << 4;
			else
				*packed_cond |= (cond->mc -
								 V3D_QPU_COND_IFA) << 2;
		}

		return TRUE;
	}

	return FALSE;
}

/* Make a mapping of the table of opcodes in the spec.  The opcode is
 * determined by a combination of the opcode field, and in the case of 0 or
 * 1-arg opcodes, the mux (version <= 42) or raddr (version >= 71) field as
 * well.
 */
/** Set a single bit */
#define BITFIELD64_BIT(b)      (1ull << (b))  //debugdebugerror
/** Set all bits up to excluding bit b */
#define BITFIELD64_MASK(b)									\
	((b) == 64 ? (~0ull) : BITFIELD64_BIT((b) & 63) - 1)
/** Set count bits starting from bit b  */
#define BITFIELD64_RANGE(b, count)							\
	(BITFIELD64_MASK((b) + (count)) & ~BITFIELD64_MASK(b))
#define OP_MASK(val) BITFIELD64_BIT(val)
#define OP_RANGE(bot, top) BITFIELD64_RANGE(bot, top - bot + 1)
#define ANYMUX OP_RANGE(0, 7)
#define ANYOPMASK OP_RANGE(0, 63)

struct opcode_desc {
	v3d_uint8 opcode_first;
	v3d_uint8 opcode_last;

	union {
		struct {
			v3d_uint8 b_mask;
			v3d_uint8 a_mask;
		} mux;
		v3d_uint64 raddr_mask;
	}mask;

	v3d_uint8 op;

	/* first_ver == 0 if it's the same across all V3D versions.
	 * first_ver == X, last_ver == 0 if it's the same for all V3D versions
	 *   starting from X
	 * first_ver == X, last_ver == Y if it's the same for all V3D versions
	 *   on the range X through Y
	 */
	v3d_uint8 first_ver;
	v3d_uint8 last_ver;
};

static const struct opcode_desc add_ops_v33[] = {
	/* FADD is FADDNF depending on the order of the mux_a/mux_b. */
	{ 0,   47,  .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_FADD },
	{ 0,   47,  .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_FADDNF },
	{ 53,  55,  .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_VFPACK },
	{ 56,  56,  .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_ADD },
	{ 57,  59,  .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_VFPACK },
	{ 60,  60,  .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_SUB },
	{ 61,  63,  .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_VFPACK },
	{ 64,  111, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_FSUB },
	{ 120, 120, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_MIN },
	{ 121, 121, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_MAX },
	{ 122, 122, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_UMIN },
	{ 123, 123, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_UMAX },
	{ 124, 124, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_SHL },
	{ 125, 125, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_SHR },
	{ 126, 126, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_ASR },
	{ 127, 127, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_ROR },
	/* FMIN is instead FMAX depending on the order of the mux_a/mux_b. */
	{ 128, 175, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_FMIN },
	{ 128, 175, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_FMAX },
	{ 176, 180, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_VFMIN },

	{ 181, 181, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_AND },
	{ 182, 182, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_OR },
	{ 183, 183, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_XOR },

	{ 184, 184, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_VADD },
	{ 185, 185, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_VSUB },
	{ 186, 186, .mask.mux.b_mask = OP_MASK(0), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_NOT },
	{ 186, 186, .mask.mux.b_mask = OP_MASK(1), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_NEG },
	{ 186, 186, .mask.mux.b_mask = OP_MASK(2), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_FLAPUSH },
	{ 186, 186, .mask.mux.b_mask = OP_MASK(3), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_FLBPUSH },
	{ 186, 186, .mask.mux.b_mask = OP_MASK(4), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_FLPOP },
	{ 186, 186, .mask.mux.b_mask = OP_MASK(5), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_RECIP },
	{ 186, 186, .mask.mux.b_mask = OP_MASK(6), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_SETMSF },
	{ 186, 186, .mask.mux.b_mask = OP_MASK(7), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_SETREVF },
	{ 187, 187, .mask.mux.b_mask = OP_MASK(0), .mask.mux.a_mask = OP_MASK(0), V3D_QPU_A_NOP, 0 },
	{ 187, 187, .mask.mux.b_mask = OP_MASK(0), .mask.mux.a_mask = OP_MASK(1), V3D_QPU_A_TIDX },
	{ 187, 187, .mask.mux.b_mask = OP_MASK(0), .mask.mux.a_mask = OP_MASK(2), V3D_QPU_A_EIDX },
	{ 187, 187, .mask.mux.b_mask = OP_MASK(0), .mask.mux.a_mask = OP_MASK(3), V3D_QPU_A_LR },
	{ 187, 187, .mask.mux.b_mask = OP_MASK(0), .mask.mux.a_mask = OP_MASK(4), V3D_QPU_A_VFLA },
	{ 187, 187, .mask.mux.b_mask = OP_MASK(0), .mask.mux.a_mask = OP_MASK(5), V3D_QPU_A_VFLNA },
	{ 187, 187, .mask.mux.b_mask = OP_MASK(0), .mask.mux.a_mask = OP_MASK(6), V3D_QPU_A_VFLB },
	{ 187, 187, .mask.mux.b_mask = OP_MASK(0), .mask.mux.a_mask = OP_MASK(7), V3D_QPU_A_VFLNB },

	{ 187, 187, .mask.mux.b_mask = OP_MASK(1), .mask.mux.a_mask = OP_RANGE(0, 2), V3D_QPU_A_FXCD },
	{ 187, 187, .mask.mux.b_mask = OP_MASK(1), .mask.mux.a_mask = OP_MASK(3), V3D_QPU_A_XCD },
	{ 187, 187, .mask.mux.b_mask = OP_MASK(1), .mask.mux.a_mask = OP_RANGE(4, 6), V3D_QPU_A_FYCD },
	{ 187, 187, .mask.mux.b_mask = OP_MASK(1), .mask.mux.a_mask = OP_MASK(7), V3D_QPU_A_YCD },

	{ 187, 187, .mask.mux.b_mask = OP_MASK(2), .mask.mux.a_mask = OP_MASK(0), V3D_QPU_A_MSF },
	{ 187, 187, .mask.mux.b_mask = OP_MASK(2), .mask.mux.a_mask = OP_MASK(1), V3D_QPU_A_REVF },
	{ 187, 187, .mask.mux.b_mask = OP_MASK(2), .mask.mux.a_mask = OP_MASK(2), V3D_QPU_A_VDWWT, 33 },
	{ 187, 187, .mask.mux.b_mask = OP_MASK(2), .mask.mux.a_mask = OP_MASK(2), V3D_QPU_A_IID, 40 },
	{ 187, 187, .mask.mux.b_mask = OP_MASK(2), .mask.mux.a_mask = OP_MASK(3), V3D_QPU_A_SAMPID, 40 },
	{ 187, 187, .mask.mux.b_mask = OP_MASK(2), .mask.mux.a_mask = OP_MASK(4), V3D_QPU_A_BARRIERID, 40 },
	{ 187, 187, .mask.mux.b_mask = OP_MASK(2), .mask.mux.a_mask = OP_MASK(5), V3D_QPU_A_TMUWT },
	{ 187, 187, .mask.mux.b_mask = OP_MASK(2), .mask.mux.a_mask = OP_MASK(6), V3D_QPU_A_VPMWT },
	{ 187, 187, .mask.mux.b_mask = OP_MASK(2), .mask.mux.a_mask = OP_MASK(7), V3D_QPU_A_FLAFIRST, 41 },
	{ 187, 187, .mask.mux.b_mask = OP_MASK(3), .mask.mux.a_mask = OP_MASK(0), V3D_QPU_A_FLNAFIRST, 41 },
	{ 187, 187, .mask.mux.b_mask = OP_MASK(3), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_VPMSETUP, 33 },

	{ 188, 188, .mask.mux.b_mask = OP_MASK(0), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_LDVPMV_IN, 40 },
	{ 188, 188, .mask.mux.b_mask = OP_MASK(0), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_LDVPMV_OUT, 40 },
	{ 188, 188, .mask.mux.b_mask = OP_MASK(1), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_LDVPMD_IN, 40 },
	{ 188, 188, .mask.mux.b_mask = OP_MASK(1), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_LDVPMD_OUT, 40 },
	{ 188, 188, .mask.mux.b_mask = OP_MASK(2), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_LDVPMP, 40 },
	{ 188, 188, .mask.mux.b_mask = OP_MASK(3), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_RSQRT, 41 },
	{ 188, 188, .mask.mux.b_mask = OP_MASK(4), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_EXP, 41 },
	{ 188, 188, .mask.mux.b_mask = OP_MASK(5), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_LOG, 41 },
	{ 188, 188, .mask.mux.b_mask = OP_MASK(6), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_SIN, 41 },
	{ 188, 188, .mask.mux.b_mask = OP_MASK(7), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_RSQRT2, 41 },
	{ 189, 189, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_LDVPMG_IN, 40 },
	{ 189, 189, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_LDVPMG_OUT, 40 },

	/* FIXME: MORE COMPLICATED */
	/* { 190, 191, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_VFMOVABSNEGNAB }, */

	{ 192, 239, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_FCMP },
	{ 240, 244, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_VFMAX },

	{ 245, 245, .mask.mux.b_mask = OP_RANGE(0, 2), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_FROUND },
	{ 245, 245, .mask.mux.b_mask = OP_MASK(3), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_FTOIN },
	{ 245, 245, .mask.mux.b_mask = OP_RANGE(4, 6), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_FTRUNC },
	{ 245, 245, .mask.mux.b_mask = OP_MASK(7), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_FTOIZ },
	{ 246, 246, .mask.mux.b_mask = OP_RANGE(0, 2), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_FFLOOR },
	{ 246, 246, .mask.mux.b_mask = OP_MASK(3), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_FTOUZ },
	{ 246, 246, .mask.mux.b_mask = OP_RANGE(4, 6), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_FCEIL },
	{ 246, 246, .mask.mux.b_mask = OP_MASK(7), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_FTOC },

	{ 247, 247, .mask.mux.b_mask = OP_RANGE(0, 2), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_FDX },
	{ 247, 247, .mask.mux.b_mask = OP_RANGE(4, 6), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_FDY },

	/* The stvpms are distinguished by the waddr field. */
	{ 248, 248, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_STVPMV },
	{ 248, 248, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_STVPMD },
	{ 248, 248, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_A_STVPMP },

	{ 252, 252, .mask.mux.b_mask = OP_RANGE(0, 2), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_ITOF },
	{ 252, 252, .mask.mux.b_mask = OP_MASK(3), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_CLZ },
	{ 252, 252, .mask.mux.b_mask = OP_RANGE(4, 6), .mask.mux.a_mask = ANYMUX, V3D_QPU_A_UTOF },
};

static const struct opcode_desc mul_ops_v33[] = {
	{ 1, 1, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_M_ADD },
	{ 2, 2, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_M_SUB },
	{ 3, 3, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_M_UMUL24 },
	{ 4, 8, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_M_VFMUL },
	{ 9, 9, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_M_SMUL24 },
	{ 10, 10, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_M_MULTOP },
	{ 14, 14, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_M_FMOV, 33, 42 },
	{ 15, 15, .mask.mux.b_mask = OP_RANGE(0, 3), .mask.mux.a_mask = ANYMUX, V3D_QPU_M_FMOV, 33, 42},
	{ 15, 15, .mask.mux.b_mask = OP_MASK(4), .mask.mux.a_mask = OP_MASK(0), V3D_QPU_M_NOP, 33, 42 },
	{ 15, 15, .mask.mux.b_mask = OP_MASK(7), .mask.mux.a_mask = ANYMUX, V3D_QPU_M_MOV, 33, 42 },

	{ 16, 63, .mask.mux.b_mask = ANYMUX, .mask.mux.a_mask = ANYMUX, V3D_QPU_M_FMUL },
};

/* Note that it would have been possible to define all the add/mul opcodes in
 * just one table, using the first_ver/last_ver. But taking into account that
 * for v71 there were a lot of changes, it was more tidy this way. Also right
 * now we are doing a linear search on those tables, so this maintains the
 * tables smaller.
 *
 * Just in case we merge the tables, we define the first_ver as 71 for those
 * opcodes that changed on v71
 */
static const struct opcode_desc add_ops_v71[] = {
	/* FADD is FADDNF depending on the order of the raddr_a/raddr_b. */
	{ 0,   47,  .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_FADD },
	{ 0,   47,  .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_FADDNF },
	{ 53,  55,  .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_VFPACK },
	{ 56,  56,  .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_ADD },
	{ 57,  59,  .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_VFPACK },
	{ 60,  60,  .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_SUB },
	{ 61,  63,  .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_VFPACK },
	{ 64,  111, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_FSUB },
	{ 120, 120, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_MIN },
	{ 121, 121, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_MAX },
	{ 122, 122, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_UMIN },
	{ 123, 123, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_UMAX },
	{ 124, 124, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_SHL },
	{ 125, 125, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_SHR },
	{ 126, 126, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_ASR },
	{ 127, 127, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_ROR },
	/* FMIN is instead FMAX depending on the raddr_a/b order. */
	{ 128, 175, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_FMIN },
	{ 128, 175, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_FMAX },
	{ 176, 180, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_VFMIN },

	{ 181, 181, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_AND },
	{ 182, 182, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_OR },
	{ 183, 183, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_XOR },
	{ 184, 184, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_VADD },
	{ 185, 185, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_VSUB },

	{ 186, 186, .mask.raddr_mask = OP_MASK(0), V3D_QPU_A_NOT },
	{ 186, 186, .mask.raddr_mask = OP_MASK(1), V3D_QPU_A_NEG },
	{ 186, 186, .mask.raddr_mask = OP_MASK(2), V3D_QPU_A_FLAPUSH },
	{ 186, 186, .mask.raddr_mask = OP_MASK(3), V3D_QPU_A_FLBPUSH },
	{ 186, 186, .mask.raddr_mask = OP_MASK(4), V3D_QPU_A_FLPOP },
	{ 186, 186, .mask.raddr_mask = OP_MASK(5), V3D_QPU_A_CLZ },
	{ 186, 186, .mask.raddr_mask = OP_MASK(6), V3D_QPU_A_SETMSF },
	{ 186, 186, .mask.raddr_mask = OP_MASK(7), V3D_QPU_A_SETREVF },

	{ 187, 187, .mask.raddr_mask = OP_MASK(0), V3D_QPU_A_NOP, 0 },
	{ 187, 187, .mask.raddr_mask = OP_MASK(1), V3D_QPU_A_TIDX },
	{ 187, 187, .mask.raddr_mask = OP_MASK(2), V3D_QPU_A_EIDX },
	{ 187, 187, .mask.raddr_mask = OP_MASK(3), V3D_QPU_A_LR },
	{ 187, 187, .mask.raddr_mask = OP_MASK(4), V3D_QPU_A_VFLA },
	{ 187, 187, .mask.raddr_mask = OP_MASK(5), V3D_QPU_A_VFLNA },
	{ 187, 187, .mask.raddr_mask = OP_MASK(6), V3D_QPU_A_VFLB },
	{ 187, 187, .mask.raddr_mask = OP_MASK(7), V3D_QPU_A_VFLNB },
	{ 187, 187, .mask.raddr_mask = OP_MASK(8), V3D_QPU_A_XCD },
	{ 187, 187, .mask.raddr_mask = OP_MASK(9), V3D_QPU_A_YCD },
	{ 187, 187, .mask.raddr_mask = OP_MASK(10), V3D_QPU_A_MSF },
	{ 187, 187, .mask.raddr_mask = OP_MASK(11), V3D_QPU_A_REVF },
	{ 187, 187, .mask.raddr_mask = OP_MASK(12), V3D_QPU_A_IID },
	{ 187, 187, .mask.raddr_mask = OP_MASK(13), V3D_QPU_A_SAMPID },
	{ 187, 187, .mask.raddr_mask = OP_MASK(14), V3D_QPU_A_BARRIERID },
	{ 187, 187, .mask.raddr_mask = OP_MASK(15), V3D_QPU_A_TMUWT },
	{ 187, 187, .mask.raddr_mask = OP_MASK(16), V3D_QPU_A_VPMWT },
	{ 187, 187, .mask.raddr_mask = OP_MASK(17), V3D_QPU_A_FLAFIRST },
	{ 187, 187, .mask.raddr_mask = OP_MASK(18), V3D_QPU_A_FLNAFIRST },

	{ 187, 187, .mask.raddr_mask = OP_RANGE(32, 34), V3D_QPU_A_FXCD },
	{ 187, 187, .mask.raddr_mask = OP_RANGE(36, 38), V3D_QPU_A_FYCD },

	{ 188, 188, .mask.raddr_mask = OP_MASK(0), V3D_QPU_A_LDVPMV_IN, 71 },
	{ 188, 188, .mask.raddr_mask = OP_MASK(1), V3D_QPU_A_LDVPMD_IN, 71 },
	{ 188, 188, .mask.raddr_mask = OP_MASK(2), V3D_QPU_A_LDVPMP, 71 },

	{ 188, 188, .mask.raddr_mask = OP_MASK(32), V3D_QPU_A_RECIP, 71 },
	{ 188, 188, .mask.raddr_mask = OP_MASK(33), V3D_QPU_A_RSQRT, 71 },
	{ 188, 188, .mask.raddr_mask = OP_MASK(34), V3D_QPU_A_EXP, 71 },
	{ 188, 188, .mask.raddr_mask = OP_MASK(35), V3D_QPU_A_LOG, 71 },
	{ 188, 188, .mask.raddr_mask = OP_MASK(36), V3D_QPU_A_SIN, 71 },
	{ 188, 188, .mask.raddr_mask = OP_MASK(37), V3D_QPU_A_RSQRT2, 71 },

	{ 189, 189, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_LDVPMG_IN, 71 },

	/* The stvpms are distinguished by the waddr field. */
	{ 190, 190, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_STVPMV, 71},
	{ 190, 190, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_STVPMD, 71},
	{ 190, 190, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_STVPMP, 71},

	{ 192, 207, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_FCMP, 71 },

	{ 245, 245, .mask.raddr_mask = OP_RANGE(0, 2),   V3D_QPU_A_FROUND, 71 },
	{ 245, 245, .mask.raddr_mask = OP_RANGE(4, 6),   V3D_QPU_A_FROUND, 71 },
	{ 245, 245, .mask.raddr_mask = OP_RANGE(8, 10),  V3D_QPU_A_FROUND, 71 },
	{ 245, 245, .mask.raddr_mask = OP_RANGE(12, 14), V3D_QPU_A_FROUND, 71 },

	{ 245, 245, .mask.raddr_mask = OP_MASK(3),  V3D_QPU_A_FTOIN, 71 },
	{ 245, 245, .mask.raddr_mask = OP_MASK(7),  V3D_QPU_A_FTOIN, 71 },
	{ 245, 245, .mask.raddr_mask = OP_MASK(11), V3D_QPU_A_FTOIN, 71 },
	{ 245, 245, .mask.raddr_mask = OP_MASK(15), V3D_QPU_A_FTOIN, 71 },

	{ 245, 245, .mask.raddr_mask = OP_RANGE(16, 18), V3D_QPU_A_FTRUNC, 71 },
	{ 245, 245, .mask.raddr_mask = OP_RANGE(20, 22), V3D_QPU_A_FTRUNC, 71 },
	{ 245, 245, .mask.raddr_mask = OP_RANGE(24, 26), V3D_QPU_A_FTRUNC, 71 },
	{ 245, 245, .mask.raddr_mask = OP_RANGE(28, 30), V3D_QPU_A_FTRUNC, 71 },

	{ 245, 245, .mask.raddr_mask = OP_MASK(19), V3D_QPU_A_FTOIZ, 71 },
	{ 245, 245, .mask.raddr_mask = OP_MASK(23), V3D_QPU_A_FTOIZ, 71 },
	{ 245, 245, .mask.raddr_mask = OP_MASK(27), V3D_QPU_A_FTOIZ, 71 },
	{ 245, 245, .mask.raddr_mask = OP_MASK(31), V3D_QPU_A_FTOIZ, 71 },

	{ 245, 245, .mask.raddr_mask = OP_RANGE(32, 34), V3D_QPU_A_FFLOOR, 71 },
	{ 245, 245, .mask.raddr_mask = OP_RANGE(36, 38), V3D_QPU_A_FFLOOR, 71 },
	{ 245, 245, .mask.raddr_mask = OP_RANGE(40, 42), V3D_QPU_A_FFLOOR, 71 },
	{ 245, 245, .mask.raddr_mask = OP_RANGE(44, 46), V3D_QPU_A_FFLOOR, 71 },

	{ 245, 245, .mask.raddr_mask = OP_MASK(35), V3D_QPU_A_FTOUZ, 71 },
	{ 245, 245, .mask.raddr_mask = OP_MASK(39), V3D_QPU_A_FTOUZ, 71 },
	{ 245, 245, .mask.raddr_mask = OP_MASK(43), V3D_QPU_A_FTOUZ, 71 },
	{ 245, 245, .mask.raddr_mask = OP_MASK(47), V3D_QPU_A_FTOUZ, 71 },

	{ 245, 245, .mask.raddr_mask = OP_RANGE(48, 50), V3D_QPU_A_FCEIL, 71 },
	{ 245, 245, .mask.raddr_mask = OP_RANGE(52, 54), V3D_QPU_A_FCEIL, 71 },
	{ 245, 245, .mask.raddr_mask = OP_RANGE(56, 58), V3D_QPU_A_FCEIL, 71 },
	{ 245, 245, .mask.raddr_mask = OP_RANGE(60, 62), V3D_QPU_A_FCEIL, 71 },

	{ 245, 245, .mask.raddr_mask = OP_MASK(51), V3D_QPU_A_FTOC },
	{ 245, 245, .mask.raddr_mask = OP_MASK(55), V3D_QPU_A_FTOC },
	{ 245, 245, .mask.raddr_mask = OP_MASK(59), V3D_QPU_A_FTOC },
	{ 245, 245, .mask.raddr_mask = OP_MASK(63), V3D_QPU_A_FTOC },

	{ 246, 246, .mask.raddr_mask = OP_RANGE(0, 2),   V3D_QPU_A_FDX, 71 },
	{ 246, 246, .mask.raddr_mask = OP_RANGE(4, 6),   V3D_QPU_A_FDX, 71 },
	{ 246, 246, .mask.raddr_mask = OP_RANGE(8, 10),  V3D_QPU_A_FDX, 71 },
	{ 246, 246, .mask.raddr_mask = OP_RANGE(12, 14), V3D_QPU_A_FDX, 71 },
	{ 246, 246, .mask.raddr_mask = OP_RANGE(16, 18), V3D_QPU_A_FDY, 71 },
	{ 246, 246, .mask.raddr_mask = OP_RANGE(20, 22), V3D_QPU_A_FDY, 71 },
	{ 246, 246, .mask.raddr_mask = OP_RANGE(24, 26), V3D_QPU_A_FDY, 71 },
	{ 246, 246, .mask.raddr_mask = OP_RANGE(28, 30), V3D_QPU_A_FDY, 71 },

	{ 246, 246, .mask.raddr_mask = OP_RANGE(32, 34), V3D_QPU_A_ITOF, 71 },
	{ 246, 246, .mask.raddr_mask = OP_RANGE(36, 38), V3D_QPU_A_UTOF, 71 },

	{ 247, 247, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_VPACK, 71 },
	{ 248, 248, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_V8PACK, 71 },

	{ 249, 249, .mask.raddr_mask = OP_RANGE(0, 2),   V3D_QPU_A_FMOV, 71 },
	{ 249, 249, .mask.raddr_mask = OP_RANGE(4, 6),   V3D_QPU_A_FMOV, 71 },
	{ 249, 249, .mask.raddr_mask = OP_RANGE(8, 10),  V3D_QPU_A_FMOV, 71 },
	{ 249, 249, .mask.raddr_mask = OP_RANGE(12, 14), V3D_QPU_A_FMOV, 71 },
	{ 249, 249, .mask.raddr_mask = OP_RANGE(16, 18), V3D_QPU_A_FMOV, 71 },
	{ 249, 249, .mask.raddr_mask = OP_RANGE(20, 22), V3D_QPU_A_FMOV, 71 },
	{ 249, 249, .mask.raddr_mask = OP_RANGE(24, 26), V3D_QPU_A_FMOV, 71 },

	{ 249, 249, .mask.raddr_mask = OP_MASK(3),  V3D_QPU_A_MOV, 71 },
	{ 249, 249, .mask.raddr_mask = OP_MASK(7),  V3D_QPU_A_MOV, 71 },
	{ 249, 249, .mask.raddr_mask = OP_MASK(11), V3D_QPU_A_MOV, 71 },
	{ 249, 249, .mask.raddr_mask = OP_MASK(15), V3D_QPU_A_MOV, 71 },
	{ 249, 249, .mask.raddr_mask = OP_MASK(19), V3D_QPU_A_MOV, 71 },

	{ 250, 250, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_V10PACK, 71 },
	{ 251, 251, .mask.raddr_mask = ANYOPMASK, V3D_QPU_A_V11FPACK, 71 },
};

static const struct opcode_desc mul_ops_v71[] = {
	/* For V3D 7.1, second mask field would be ignored */
	{ 1, 1, .mask.raddr_mask = ANYOPMASK, V3D_QPU_M_ADD, 71 },
	{ 2, 2, .mask.raddr_mask = ANYOPMASK, V3D_QPU_M_SUB, 71 },
	{ 3, 3, .mask.raddr_mask = ANYOPMASK, V3D_QPU_M_UMUL24, 71 },
	{ 3, 3, .mask.raddr_mask = ANYOPMASK, V3D_QPU_M_UMUL24, 71 },
	{ 4, 8, .mask.raddr_mask = ANYOPMASK, V3D_QPU_M_VFMUL, 71 },
	{ 9, 9, .mask.raddr_mask = ANYOPMASK, V3D_QPU_M_SMUL24, 71 },
	{ 10, 10, .mask.raddr_mask = ANYOPMASK, V3D_QPU_M_MULTOP, 71 },

	{ 14, 14, .mask.raddr_mask = OP_RANGE(0, 2),   V3D_QPU_M_FMOV, 71 },
	{ 14, 14, .mask.raddr_mask = OP_RANGE(4, 6),   V3D_QPU_M_FMOV, 71 },
	{ 14, 14, .mask.raddr_mask = OP_RANGE(8, 10),  V3D_QPU_M_FMOV, 71 },
	{ 14, 14, .mask.raddr_mask = OP_RANGE(12, 14), V3D_QPU_M_FMOV, 71 },
	{ 14, 14, .mask.raddr_mask = OP_RANGE(16, 18), V3D_QPU_M_FMOV, 71 },
	{ 14, 14, .mask.raddr_mask = OP_RANGE(20, 22), V3D_QPU_M_FMOV, 71 },

	{ 14, 14, .mask.raddr_mask = OP_MASK(3),  V3D_QPU_M_MOV, 71 },
	{ 14, 14, .mask.raddr_mask = OP_MASK(7),  V3D_QPU_M_MOV, 71 },
	{ 14, 14, .mask.raddr_mask = OP_MASK(11), V3D_QPU_M_MOV, 71 },
	{ 14, 14, .mask.raddr_mask = OP_MASK(15), V3D_QPU_M_MOV, 71 },
	{ 14, 14, .mask.raddr_mask = OP_MASK(19), V3D_QPU_M_MOV, 71 },

	{ 14, 14, .mask.raddr_mask = OP_MASK(32), V3D_QPU_M_FTOUNORM16, 71 },
	{ 14, 14, .mask.raddr_mask = OP_MASK(33), V3D_QPU_M_FTOSNORM16, 71 },
	{ 14, 14, .mask.raddr_mask = OP_MASK(34), V3D_QPU_M_VFTOUNORM8, 71 },
	{ 14, 14, .mask.raddr_mask = OP_MASK(35), V3D_QPU_M_VFTOSNORM8, 71 },
	{ 14, 14, .mask.raddr_mask = OP_MASK(48), V3D_QPU_M_VFTOUNORM10LO, 71 },
	{ 14, 14, .mask.raddr_mask = OP_MASK(49), V3D_QPU_M_VFTOUNORM10HI, 71 },

	{ 14, 14, .mask.raddr_mask = OP_MASK(63), V3D_QPU_M_NOP, 71 },

	{ 16, 63, .mask.raddr_mask = ANYOPMASK, V3D_QPU_M_FMUL },
};

/* Returns TRUE if op_desc should be filtered out based on devinfo->ver
 * against op_desc->first_ver and op_desc->last_ver. Check notes about
 * first_ver/last_ver on struct opcode_desc comments.
 */
static v3d_bool
opcode_invalid_in_version(const struct v3d_device_info *devinfo,
                          const v3d_uint8 first_ver,
                          const v3d_uint8 last_ver)
{
	return (first_ver != 0 && devinfo->ver < first_ver) ||
		(last_ver != 0  && devinfo->ver > last_ver);
}

/* Note that we pass as parameters mux_a, mux_b and raddr, even if depending
 * on the devinfo->ver some would be ignored. We do this way just to avoid
 * having two really similar lookup_opcode methods
 */
static const struct opcode_desc *
lookup_opcode_from_packed(const struct v3d_device_info *devinfo,
                          const struct opcode_desc *opcodes,
                          size_t num_opcodes, v3d_uint32 opcode,
                          v3d_uint32 mux_a, v3d_uint32 mux_b,
                          v3d_uint32 raddr)
{
	for (int i = 0; i < num_opcodes; i++) {
		const struct opcode_desc *op_desc = &opcodes[i];

		if (opcode < op_desc->opcode_first ||
			opcode > op_desc->opcode_last)
			continue;

		if (opcode_invalid_in_version(devinfo, op_desc->first_ver, op_desc->last_ver))
			continue;

		if (devinfo->ver < 71) {
			if (!(op_desc->mask.mux.b_mask & (1 << mux_b)))
				continue;

			if (!(op_desc->mask.mux.a_mask & (1 << mux_a)))
				continue;
		} else {
			if (!(op_desc->mask.raddr_mask & ((v3d_uint64) 1 << raddr)))
				continue;
		}

		return op_desc;
	}

	return NULL;
}

static v3d_bool
v3d_qpu_float32_unpack_unpack(v3d_uint32 packed,
                              enum v3d_qpu_input_unpack *unpacked)
{
	switch (packed) {
	case 0:
		*unpacked = V3D_QPU_UNPACK_ABS;
		return TRUE;
	case 1:
		*unpacked = V3D_QPU_UNPACK_NONE;
		return TRUE;
	case 2:
		*unpacked = V3D_QPU_UNPACK_L;
		return TRUE;
	case 3:
		*unpacked = V3D_QPU_UNPACK_H;
		return TRUE;
	default:
		return FALSE;
	}
}

static v3d_bool
v3d_qpu_float32_unpack_pack(enum v3d_qpu_input_unpack unpacked,
                            v3d_uint32 *packed)
{
	switch (unpacked) {
	case V3D_QPU_UNPACK_ABS:
		*packed = 0;
		return TRUE;
	case V3D_QPU_UNPACK_NONE:
		*packed = 1;
		return TRUE;
	case V3D_QPU_UNPACK_L:
		*packed = 2;
		return TRUE;
	case V3D_QPU_UNPACK_H:
		*packed = 3;
		return TRUE;
	default:
		return FALSE;
	}
}

static v3d_bool
v3d_qpu_int32_unpack_unpack(v3d_uint32 packed,
                            enum v3d_qpu_input_unpack *unpacked)
{
	switch (packed) {
	case 0:
		*unpacked = V3D_QPU_UNPACK_NONE;
		return TRUE;
	case 1:
		*unpacked = V3D_QPU_UNPACK_UL;
		return TRUE;
	case 2:
		*unpacked = V3D_QPU_UNPACK_UH;
		return TRUE;
	case 3:
		*unpacked = V3D_QPU_UNPACK_IL;
		return TRUE;
	case 4:
		*unpacked = V3D_QPU_UNPACK_IH;
		return TRUE;
	default:
		return FALSE;
	}
}

static v3d_bool
v3d_qpu_int32_unpack_pack(enum v3d_qpu_input_unpack unpacked,
                          v3d_uint32 *packed)
{
	switch (unpacked) {
	case V3D_QPU_UNPACK_NONE:
		*packed = 0;
		return TRUE;
	case V3D_QPU_UNPACK_UL:
		*packed = 1;
		return TRUE;
	case V3D_QPU_UNPACK_UH:
		*packed = 2;
		return TRUE;
	case V3D_QPU_UNPACK_IL:
		*packed = 3;
		return TRUE;
	case V3D_QPU_UNPACK_IH:
		*packed = 4;
		return TRUE;
	default:
		return FALSE;
	}
}

static v3d_bool
v3d_qpu_float16_unpack_unpack(v3d_uint32 packed,
                              enum v3d_qpu_input_unpack *unpacked)
{
	switch (packed) {
	case 0:
		*unpacked = V3D_QPU_UNPACK_NONE;
		return TRUE;
	case 1:
		*unpacked = V3D_QPU_UNPACK_REPLICATE_32F_16;
		return TRUE;
	case 2:
		*unpacked = V3D_QPU_UNPACK_REPLICATE_L_16;
		return TRUE;
	case 3:
		*unpacked = V3D_QPU_UNPACK_REPLICATE_H_16;
		return TRUE;
	case 4:
		*unpacked = V3D_QPU_UNPACK_SWAP_16;
		return TRUE;
	default:
		return FALSE;
	}
}

static v3d_bool
v3d_qpu_float16_unpack_pack(enum v3d_qpu_input_unpack unpacked,
                            v3d_uint32 *packed)
{
	switch (unpacked) {
	case V3D_QPU_UNPACK_NONE:
		*packed = 0;
		return TRUE;
	case V3D_QPU_UNPACK_REPLICATE_32F_16:
		*packed = 1;
		return TRUE;
	case V3D_QPU_UNPACK_REPLICATE_L_16:
		*packed = 2;
		return TRUE;
	case V3D_QPU_UNPACK_REPLICATE_H_16:
		*packed = 3;
		return TRUE;
	case V3D_QPU_UNPACK_SWAP_16:
		*packed = 4;
		return TRUE;
	default:
		return FALSE;
	}
}

static v3d_bool
v3d_qpu_float32_pack_pack(enum v3d_qpu_output_pack pack,
                          v3d_uint32 *packed)
{
	switch (pack) {
	case V3D_QPU_PACK_NONE:
		*packed = 0;
		return TRUE;
	case V3D_QPU_PACK_L:
		*packed = 1;
		return TRUE;
	case V3D_QPU_PACK_H:
		*packed = 2;
		return TRUE;
	default:
		return FALSE;
	}
}

static v3d_bool
v3d33_qpu_add_unpack(const struct v3d_device_info *devinfo, v3d_uint64 packed_inst,
                     struct v3d_qpu_instr *instr)
{
	v3d_uint32 op = QPU_GET_FIELD(packed_inst, V3D_QPU_OP_ADD);
	v3d_uint32 mux_a = QPU_GET_FIELD(packed_inst, V3D_QPU_ADD_A);
	v3d_uint32 mux_b = QPU_GET_FIELD(packed_inst, V3D_QPU_ADD_B);
	v3d_uint32 waddr = QPU_GET_FIELD(packed_inst, V3D_QPU_WADDR_A);

	v3d_uint32 map_op = op;
	/* Some big clusters of opcodes are replicated with unpack
	 * flags
	 */
	if (map_op >= 249 && map_op <= 251)
		map_op = (map_op - 249 + 245);
	if (map_op >= 253 && map_op <= 255)
		map_op = (map_op - 253 + 245);

	const struct opcode_desc *desc =
		lookup_opcode_from_packed(devinfo, add_ops_v33,
								  V3D_ARRAY_SIZE(add_ops_v33),
								  map_op, mux_a, mux_b, 0);

	if (!desc)
		return FALSE;

	instr->instr.alu.add.op = desc->op;

	/* FADD/FADDNF and FMIN/FMAX are determined by the orders of the
	 * operands.
	 */
	if (((op >> 2) & 3) * 8 + mux_a > (op & 3) * 8 + mux_b) {
		if (instr->instr.alu.add.op == V3D_QPU_A_FMIN)
			instr->instr.alu.add.op = V3D_QPU_A_FMAX;
		if (instr->instr.alu.add.op == V3D_QPU_A_FADD)
			instr->instr.alu.add.op = V3D_QPU_A_FADDNF;
	}

	/* Some QPU ops require a bit more than just basic opcode and mux a/b
	 * comparisons to distinguish them.
	 */
	switch (instr->instr.alu.add.op) {
	case V3D_QPU_A_STVPMV:
	case V3D_QPU_A_STVPMD:
	case V3D_QPU_A_STVPMP:
		switch (waddr) {
		case 0:
			instr->instr.alu.add.op = V3D_QPU_A_STVPMV;
			break;
		case 1:
			instr->instr.alu.add.op = V3D_QPU_A_STVPMD;
			break;
		case 2:
			instr->instr.alu.add.op = V3D_QPU_A_STVPMP;
			break;
		default:
			return FALSE;
		}
		break;
	default:
		break;
	}

	switch (instr->instr.alu.add.op) {
	case V3D_QPU_A_FADD:
	case V3D_QPU_A_FADDNF:
	case V3D_QPU_A_FSUB:
	case V3D_QPU_A_FMIN:
	case V3D_QPU_A_FMAX:
	case V3D_QPU_A_FCMP:
	case V3D_QPU_A_VFPACK:
		if (instr->instr.alu.add.op != V3D_QPU_A_VFPACK)
			instr->instr.alu.add.output_pack = (op >> 4) & 0x3;
		else
			instr->instr.alu.add.output_pack = V3D_QPU_PACK_NONE;

		if (!v3d_qpu_float32_unpack_unpack((op >> 2) & 0x3,
										   &instr->instr.alu.add.a.unpack)) {
			return FALSE;
		}

		if (!v3d_qpu_float32_unpack_unpack((op >> 0) & 0x3,
										   &instr->instr.alu.add.b.unpack)) {
			return FALSE;
		}
		break;

	case V3D_QPU_A_FFLOOR:
	case V3D_QPU_A_FROUND:
	case V3D_QPU_A_FTRUNC:
	case V3D_QPU_A_FCEIL:
	case V3D_QPU_A_FDX:
	case V3D_QPU_A_FDY:
		instr->instr.alu.add.output_pack = mux_b & 0x3;

		if (!v3d_qpu_float32_unpack_unpack((op >> 2) & 0x3,
										   &instr->instr.alu.add.a.unpack)) {
			return FALSE;
		}
		break;

	case V3D_QPU_A_FTOIN:
	case V3D_QPU_A_FTOIZ:
	case V3D_QPU_A_FTOUZ:
	case V3D_QPU_A_FTOC:
		instr->instr.alu.add.output_pack = V3D_QPU_PACK_NONE;

		if (!v3d_qpu_float32_unpack_unpack((op >> 2) & 0x3,
										   &instr->instr.alu.add.a.unpack)) {
			return FALSE;
		}
		break;

	case V3D_QPU_A_VFMIN:
	case V3D_QPU_A_VFMAX:
		if (!v3d_qpu_float16_unpack_unpack(op & 0x7,
										   &instr->instr.alu.add.a.unpack)) {
			return FALSE;
		}

		instr->instr.alu.add.output_pack = V3D_QPU_PACK_NONE;
		instr->instr.alu.add.b.unpack = V3D_QPU_UNPACK_NONE;
		break;

	default:
		instr->instr.alu.add.output_pack = V3D_QPU_PACK_NONE;
		instr->instr.alu.add.a.unpack = V3D_QPU_UNPACK_NONE;
		instr->instr.alu.add.b.unpack = V3D_QPU_UNPACK_NONE;
		break;
	}

	instr->instr.alu.add.a.input.mux = mux_a;
	instr->instr.alu.add.b.input.mux = mux_b;
	instr->instr.alu.add.waddr = QPU_GET_FIELD(packed_inst, V3D_QPU_WADDR_A);

	instr->instr.alu.add.magic_write = FALSE;
	if (packed_inst & V3D_QPU_MA) {
		switch (instr->instr.alu.add.op) {
		case V3D_QPU_A_LDVPMV_IN:
			instr->instr.alu.add.op = V3D_QPU_A_LDVPMV_OUT;
			break;
		case V3D_QPU_A_LDVPMD_IN:
			instr->instr.alu.add.op = V3D_QPU_A_LDVPMD_OUT;
			break;
		case V3D_QPU_A_LDVPMG_IN:
			instr->instr.alu.add.op = V3D_QPU_A_LDVPMG_OUT;
			break;
		default:
			instr->instr.alu.add.magic_write = TRUE;
			break;
		}
	}

	return TRUE;
}

static v3d_bool
v3d71_qpu_add_unpack(const struct v3d_device_info *devinfo, v3d_uint64 packed_inst,
                     struct v3d_qpu_instr *instr)
{
	v3d_uint32 op = QPU_GET_FIELD(packed_inst, V3D_QPU_OP_ADD);
	v3d_uint32 raddr_a = QPU_GET_FIELD(packed_inst, V3D_QPU_RADDR_A);
	v3d_uint32 raddr_b = QPU_GET_FIELD(packed_inst, V3D_QPU_RADDR_B);
	v3d_uint32 waddr = QPU_GET_FIELD(packed_inst, V3D_QPU_WADDR_A);
	v3d_uint32 map_op = op;

	const struct opcode_desc *desc =
		lookup_opcode_from_packed(devinfo,
								  add_ops_v71,
								  V3D_ARRAY_SIZE(add_ops_v71),
								  map_op, 0, 0,
								  raddr_b);
	if (!desc)
		return FALSE;

	instr->instr.alu.add.op = desc->op;

	/* FADD/FADDNF and FMIN/FMAX are determined by the order of the
	 * operands.
	 */
	if (instr->sig.small_imm_a * 256 + ((op >> 2) & 3) * 64 + raddr_a >
		instr->sig.small_imm_b * 256 + (op & 3) * 64 + raddr_b) {
		if (instr->instr.alu.add.op == V3D_QPU_A_FMIN)
			instr->instr.alu.add.op = V3D_QPU_A_FMAX;
		if (instr->instr.alu.add.op == V3D_QPU_A_FADD)
			instr->instr.alu.add.op = V3D_QPU_A_FADDNF;
	}

	/* Some QPU ops require a bit more than just basic opcode and mux a/b
	 * comparisons to distinguish them.
	 */
	switch (instr->instr.alu.add.op) {
	case V3D_QPU_A_STVPMV:
	case V3D_QPU_A_STVPMD:
	case V3D_QPU_A_STVPMP:
		switch (waddr) {
		case 0:
			instr->instr.alu.add.op = V3D_QPU_A_STVPMV;
			break;
		case 1:
			instr->instr.alu.add.op = V3D_QPU_A_STVPMD;
			break;
		case 2:
			instr->instr.alu.add.op = V3D_QPU_A_STVPMP;
			break;
		default:
			return FALSE;
		}
		break;
	default:
		break;
	}

	switch (instr->instr.alu.add.op) {
	case V3D_QPU_A_FADD:
	case V3D_QPU_A_FADDNF:
	case V3D_QPU_A_FSUB:
	case V3D_QPU_A_FMIN:
	case V3D_QPU_A_FMAX:
	case V3D_QPU_A_FCMP:
	case V3D_QPU_A_VFPACK:
		if (instr->instr.alu.add.op != V3D_QPU_A_VFPACK &&
			instr->instr.alu.add.op != V3D_QPU_A_FCMP) {
			instr->instr.alu.add.output_pack = (op >> 4) & 0x3;
		} else {
			instr->instr.alu.add.output_pack = V3D_QPU_PACK_NONE;
		}

		if (!v3d_qpu_float32_unpack_unpack((op >> 2) & 0x3,
										   &instr->instr.alu.add.a.unpack)) {
			return FALSE;
		}

		if (!v3d_qpu_float32_unpack_unpack((op >> 0) & 0x3,
										   &instr->instr.alu.add.b.unpack)) {
			return FALSE;
		}
		break;

	case V3D_QPU_A_FFLOOR:
	case V3D_QPU_A_FROUND:
	case V3D_QPU_A_FTRUNC:
	case V3D_QPU_A_FCEIL:
	case V3D_QPU_A_FDX:
	case V3D_QPU_A_FDY:
		instr->instr.alu.add.output_pack = raddr_b & 0x3;

		if (!v3d_qpu_float32_unpack_unpack((op >> 2) & 0x3,
										   &instr->instr.alu.add.a.unpack)) {
			return FALSE;
		}
		break;

	case V3D_QPU_A_FTOIN:
	case V3D_QPU_A_FTOIZ:
	case V3D_QPU_A_FTOUZ:
	case V3D_QPU_A_FTOC:
		instr->instr.alu.add.output_pack = V3D_QPU_PACK_NONE;

		if (!v3d_qpu_float32_unpack_unpack((raddr_b >> 2) & 0x3,
										   &instr->instr.alu.add.a.unpack)) {
			return FALSE;
		}
		break;

	case V3D_QPU_A_VFMIN:
	case V3D_QPU_A_VFMAX:
		v3d_unreachable("pending v71 update");
		if (!v3d_qpu_float16_unpack_unpack(op & 0x7,
										   &instr->instr.alu.add.a.unpack)) {
			return FALSE;
		}

		instr->instr.alu.add.output_pack = V3D_QPU_PACK_NONE;
		instr->instr.alu.add.b.unpack = V3D_QPU_UNPACK_NONE;
		break;

	case V3D_QPU_A_MOV:
		instr->instr.alu.add.output_pack = V3D_QPU_PACK_NONE;

		if (!v3d_qpu_int32_unpack_unpack((raddr_b >> 2) & 0x7,
										 &instr->instr.alu.add.a.unpack)) {
			return FALSE;
		}
		break;

	case V3D_QPU_A_FMOV:
		instr->instr.alu.add.output_pack = raddr_b & 0x3;

		/* Mul alu FMOV has one additional variant */
		v3d_int32 unpack = (raddr_b >> 2) & 0x7;
		if (unpack == 7)
			return FALSE;

		if (!v3d_qpu_float32_unpack_unpack(unpack,
										   &instr->instr.alu.add.a.unpack)) {
			return FALSE;
		}
		break;

	default:
		instr->instr.alu.add.output_pack = V3D_QPU_PACK_NONE;
		instr->instr.alu.add.a.unpack = V3D_QPU_UNPACK_NONE;
		instr->instr.alu.add.b.unpack = V3D_QPU_UNPACK_NONE;
		break;
	}

	instr->instr.alu.add.a.input.raddr = raddr_a;
	instr->instr.alu.add.b.input.raddr = raddr_b;
	instr->instr.alu.add.waddr = QPU_GET_FIELD(packed_inst, V3D_QPU_WADDR_A);

	instr->instr.alu.add.magic_write = FALSE;
	if (packed_inst & V3D_QPU_MA) {
		switch (instr->instr.alu.add.op) {
		case V3D_QPU_A_LDVPMV_IN:
			instr->instr.alu.add.op = V3D_QPU_A_LDVPMV_OUT;
			break;
		case V3D_QPU_A_LDVPMD_IN:
			instr->instr.alu.add.op = V3D_QPU_A_LDVPMD_OUT;
			break;
		case V3D_QPU_A_LDVPMG_IN:
			instr->instr.alu.add.op = V3D_QPU_A_LDVPMG_OUT;
			break;
		default:
			instr->instr.alu.add.magic_write = TRUE;
			break;
		}
	}

	return TRUE;
}

static v3d_bool
v3d_qpu_add_unpack(const struct v3d_device_info *devinfo, v3d_uint64 packed_inst,
                   struct v3d_qpu_instr *instr)
{
	if (devinfo->ver < 71)
		return v3d33_qpu_add_unpack(devinfo, packed_inst, instr);
	else
		return v3d71_qpu_add_unpack(devinfo, packed_inst, instr);
}

static v3d_bool
v3d33_qpu_mul_unpack(const struct v3d_device_info *devinfo, v3d_uint64 packed_inst,
                     struct v3d_qpu_instr *instr)
{
	v3d_uint32 op = QPU_GET_FIELD(packed_inst, V3D_QPU_OP_MUL);
	v3d_uint32 mux_a = QPU_GET_FIELD(packed_inst, V3D_QPU_MUL_A);
	v3d_uint32 mux_b = QPU_GET_FIELD(packed_inst, V3D_QPU_MUL_B);

	{
		const struct opcode_desc *desc =
			lookup_opcode_from_packed(devinfo,
									  mul_ops_v33,
									  V3D_ARRAY_SIZE(mul_ops_v33),
									  op, mux_a, mux_b, 0);
		if (!desc)
			return FALSE;

		instr->instr.alu.mul.op = desc->op;
	}

	switch (instr->instr.alu.mul.op) {
	case V3D_QPU_M_FMUL:
		instr->instr.alu.mul.output_pack = ((op >> 4) & 0x3) - 1;

		if (!v3d_qpu_float32_unpack_unpack((op >> 2) & 0x3,
										   &instr->instr.alu.mul.a.unpack)) {
			return FALSE;
		}

		if (!v3d_qpu_float32_unpack_unpack((op >> 0) & 0x3,
										   &instr->instr.alu.mul.b.unpack)) {
			return FALSE;
		}

		break;

	case V3D_QPU_M_FMOV:
		instr->instr.alu.mul.output_pack = (((op & 1) << 1) +
									  ((mux_b >> 2) & 1));

		if (!v3d_qpu_float32_unpack_unpack(mux_b & 0x3,
										   &instr->instr.alu.mul.a.unpack)) {
			return FALSE;
		}

		break;

	case V3D_QPU_M_VFMUL:
		instr->instr.alu.mul.output_pack = V3D_QPU_PACK_NONE;

		if (!v3d_qpu_float16_unpack_unpack(((op & 0x7) - 4) & 7,
										   &instr->instr.alu.mul.a.unpack)) {
			return FALSE;
		}

		instr->instr.alu.mul.b.unpack = V3D_QPU_UNPACK_NONE;

		break;

	default:
		instr->instr.alu.mul.output_pack = V3D_QPU_PACK_NONE;
		instr->instr.alu.mul.a.unpack = V3D_QPU_UNPACK_NONE;
		instr->instr.alu.mul.b.unpack = V3D_QPU_UNPACK_NONE;
		break;
	}

	instr->instr.alu.mul.a.input.mux = mux_a;
	instr->instr.alu.mul.b.input.mux = mux_b;
	instr->instr.alu.mul.waddr = QPU_GET_FIELD(packed_inst, V3D_QPU_WADDR_M);
	instr->instr.alu.mul.magic_write = (packed_inst & V3D_QPU_MM) ? TRUE : FALSE;

	return TRUE;
}

static v3d_bool
v3d71_qpu_mul_unpack(const struct v3d_device_info *devinfo, v3d_uint64 packed_inst,
                     struct v3d_qpu_instr *instr)
{
	v3d_uint32 op = QPU_GET_FIELD(packed_inst, V3D_QPU_OP_MUL);
	v3d_uint32 raddr_c = QPU_GET_FIELD(packed_inst, V3D_QPU_RADDR_C);
	v3d_uint32 raddr_d = QPU_GET_FIELD(packed_inst, V3D_QPU_RADDR_D);

	{
		const struct opcode_desc *desc =
			lookup_opcode_from_packed(devinfo,
									  mul_ops_v71,
									  V3D_ARRAY_SIZE(mul_ops_v71),
									  op, 0, 0,
									  raddr_d);
		if (!desc)
			return FALSE;

		instr->instr.alu.mul.op = desc->op;
	}

	switch (instr->instr.alu.mul.op) {
	case V3D_QPU_M_FMUL:
		instr->instr.alu.mul.output_pack = ((op >> 4) & 0x3) - 1;

		if (!v3d_qpu_float32_unpack_unpack((op >> 2) & 0x3,
										   &instr->instr.alu.mul.a.unpack)) {
			return FALSE;
		}

		if (!v3d_qpu_float32_unpack_unpack((op >> 0) & 0x3,
										   &instr->instr.alu.mul.b.unpack)) {
			return FALSE;
		}

		break;

	case V3D_QPU_M_FMOV:
		instr->instr.alu.mul.output_pack = raddr_d & 0x3;

		if (!v3d_qpu_float32_unpack_unpack((raddr_d >> 2) & 0x7,
										   &instr->instr.alu.mul.a.unpack)) {
			return FALSE;
		}

		break;

	case V3D_QPU_M_VFMUL:
		v3d_unreachable("pending v71 update");
		instr->instr.alu.mul.output_pack = V3D_QPU_PACK_NONE;

		if (!v3d_qpu_float16_unpack_unpack(((op & 0x7) - 4) & 7,
										   &instr->instr.alu.mul.a.unpack)) {
			return FALSE;
		}

		instr->instr.alu.mul.b.unpack = V3D_QPU_UNPACK_NONE;

		break;

	case V3D_QPU_M_MOV:
		instr->instr.alu.mul.output_pack = V3D_QPU_PACK_NONE;

		if (!v3d_qpu_int32_unpack_unpack((raddr_d >> 2) & 0x7,
										 &instr->instr.alu.mul.a.unpack)) {
			return FALSE;
		}
		break;

	default:
		instr->instr.alu.mul.output_pack = V3D_QPU_PACK_NONE;
		instr->instr.alu.mul.a.unpack = V3D_QPU_UNPACK_NONE;
		instr->instr.alu.mul.b.unpack = V3D_QPU_UNPACK_NONE;
		break;
	}

	instr->instr.alu.mul.a.input.raddr = raddr_c;
	instr->instr.alu.mul.b.input.raddr = raddr_d;
	instr->instr.alu.mul.waddr = QPU_GET_FIELD(packed_inst, V3D_QPU_WADDR_M);
	instr->instr.alu.mul.magic_write = (packed_inst & V3D_QPU_MM) ? TRUE : FALSE;

	return TRUE;
}

static v3d_bool
v3d_qpu_mul_unpack(const struct v3d_device_info *devinfo, v3d_uint64 packed_inst,
                   struct v3d_qpu_instr *instr)
{
	if (devinfo->ver < 71)
		return v3d33_qpu_mul_unpack(devinfo, packed_inst, instr);
	else
		return v3d71_qpu_mul_unpack(devinfo, packed_inst, instr);
}

static const struct opcode_desc *
lookup_opcode_from_instr(const struct v3d_device_info *devinfo,
                         const struct opcode_desc *opcodes, size_t num_opcodes,
                         v3d_uint8 op)
{
	for (int i = 0; i < num_opcodes; i++) {
		const struct opcode_desc *op_desc = &opcodes[i];

		if (op_desc->op != op)
			continue;

		if (opcode_invalid_in_version(devinfo, op_desc->first_ver, op_desc->last_ver))
			continue;

		return op_desc;
	}

	return NULL;
}

// The below two functions are from Mesa/src/util/bitscan.c:
#ifdef HAVE___BUILTIN_FFS
#elif defined(_MSC_VER) && (_M_IX86 || _M_ARM || _M_AMD64 || _M_IA64)
#else
int
ffs(int i)
{
	int bit = 0;
	if (!i)
		return bit;
	if (!(i & 0xffff)) {
		bit += 16;
		i >>= 16;
	}
	if (!(i & 0xff)) {
		bit += 8;
		i >>= 8;
	}
	if (!(i & 0xf)) {
		bit += 4;
		i >>= 4;
	}
	if (!(i & 0x3)) {
		bit += 2;
		i >>= 2;
	}
	if (!(i & 0x1))
		bit += 1;
	return bit + 1;
}
#endif

#ifdef HAVE___BUILTIN_FFSLL
#elif defined(_MSC_VER) && (_M_AMD64 || _M_ARM64 || _M_IA64)
#else
int
ffsll(v3d_uint64 val)
{
	int bit;

	bit = ffs((ULONG) (val & 0xffffffff));
	if (bit != 0)
		return bit;

	bit = ffs((ULONG) (val >> 32));
	if (bit != 0)
		return 32 + bit;

	return 0;
}
#endif

// Resume v3d_pack.c

static v3d_bool
v3d33_qpu_add_pack(const struct v3d_device_info *devinfo,
                   const struct v3d_qpu_instr *instr, v3d_uint64 *packed_instr)
{
	v3d_uint32 waddr = instr->instr.alu.add.waddr;
	v3d_uint32 mux_a = instr->instr.alu.add.a.input.mux;
	v3d_uint32 mux_b = instr->instr.alu.add.b.input.mux;
	int nsrc = v3d_qpu_add_op_num_src(instr->instr.alu.add.op);
	const struct opcode_desc *desc =
		lookup_opcode_from_instr(devinfo, add_ops_v33,
								 V3D_ARRAY_SIZE(add_ops_v33),
								 instr->instr.alu.add.op);
	if (!desc)
		return FALSE;

	v3d_uint32 opcode = desc->opcode_first;

	/* If an operation doesn't use an arg, its mux values may be used to
	 * identify the operation type.
	 */
	if (nsrc < 2)
		mux_b = ffs(desc->mask.mux.b_mask) - 1;

	if (nsrc < 1)
		mux_a = ffs(desc->mask.mux.a_mask) - 1;

	v3d_bool no_magic_write = FALSE;

	switch (instr->instr.alu.add.op) {
	case V3D_QPU_A_STVPMV:
		waddr = 0;
		no_magic_write = TRUE;
		break;
	case V3D_QPU_A_STVPMD:
		waddr = 1;
		no_magic_write = TRUE;
		break;
	case V3D_QPU_A_STVPMP:
		waddr = 2;
		no_magic_write = TRUE;
		break;

	case V3D_QPU_A_LDVPMV_IN:
	case V3D_QPU_A_LDVPMD_IN:
	case V3D_QPU_A_LDVPMP:
	case V3D_QPU_A_LDVPMG_IN:
		v3d_assert(!instr->instr.alu.add.magic_write);
		break;

	case V3D_QPU_A_LDVPMV_OUT:
	case V3D_QPU_A_LDVPMD_OUT:
	case V3D_QPU_A_LDVPMG_OUT:
		v3d_assert(!instr->instr.alu.add.magic_write);
		*packed_instr |= V3D_QPU_MA;
		break;

	default:
		break;
	}

	switch (instr->instr.alu.add.op) {
	case V3D_QPU_A_FADD:
	case V3D_QPU_A_FADDNF:
	case V3D_QPU_A_FSUB:
	case V3D_QPU_A_FMIN:
	case V3D_QPU_A_FMAX:
	case V3D_QPU_A_FCMP: {
		v3d_uint32 output_pack;
		v3d_uint32 a_unpack;
		v3d_uint32 b_unpack;

		if (!v3d_qpu_float32_pack_pack(instr->instr.alu.add.output_pack,
									   &output_pack)) {
			return FALSE;
		}
		opcode |= output_pack << 4;

		if (!v3d_qpu_float32_unpack_pack(instr->instr.alu.add.a.unpack,
										 &a_unpack)) {
			return FALSE;
		}

		if (!v3d_qpu_float32_unpack_pack(instr->instr.alu.add.b.unpack,
										 &b_unpack)) {
			return FALSE;
		}

		/* These operations with commutative operands are
		 * distinguished by which order their operands come in.
		 */
		v3d_bool ordering = a_unpack * 8 + mux_a > b_unpack * 8 + mux_b;
		if (((instr->instr.alu.add.op == V3D_QPU_A_FMIN ||
			  instr->instr.alu.add.op == V3D_QPU_A_FADD) && ordering) ||
			((instr->instr.alu.add.op == V3D_QPU_A_FMAX ||
			  instr->instr.alu.add.op == V3D_QPU_A_FADDNF) && !ordering)) {
			v3d_uint32 temp;

			temp = a_unpack;
			a_unpack = b_unpack;
			b_unpack = temp;

			temp = mux_a;
			mux_a = mux_b;
			mux_b = temp;
		}

		opcode |= a_unpack << 2;
		opcode |= b_unpack << 0;

		break;
	}

	case V3D_QPU_A_VFPACK: {
		v3d_uint32 a_unpack;
		v3d_uint32 b_unpack;

		if (instr->instr.alu.add.a.unpack == V3D_QPU_UNPACK_ABS ||
			instr->instr.alu.add.b.unpack == V3D_QPU_UNPACK_ABS) {
			return FALSE;
		}

		if (!v3d_qpu_float32_unpack_pack(instr->instr.alu.add.a.unpack,
										 &a_unpack)) {
			return FALSE;
		}

		if (!v3d_qpu_float32_unpack_pack(instr->instr.alu.add.b.unpack,
										 &b_unpack)) {
			return FALSE;
		}

		opcode = (opcode & ~(0x3 << 2)) | (a_unpack << 2);
		opcode = (opcode & ~(0x3 << 0)) | (b_unpack << 0);

		break;
	}

	case V3D_QPU_A_FFLOOR:
	case V3D_QPU_A_FROUND:
	case V3D_QPU_A_FTRUNC:
	case V3D_QPU_A_FCEIL:
	case V3D_QPU_A_FDX:
	case V3D_QPU_A_FDY: {
		v3d_uint32 packed;

		if (!v3d_qpu_float32_pack_pack(instr->instr.alu.add.output_pack,
									   &packed)) {
			return FALSE;
		}
		mux_b |= packed;

		if (!v3d_qpu_float32_unpack_pack(instr->instr.alu.add.a.unpack,
										 &packed)) {
			return FALSE;
		}
		if (packed == 0)
			return FALSE;
		opcode = (opcode & ~(0x3 << 2)) | packed << 2;
		break;
	}

	case V3D_QPU_A_FTOIN:
	case V3D_QPU_A_FTOIZ:
	case V3D_QPU_A_FTOUZ:
	case V3D_QPU_A_FTOC:
		if (instr->instr.alu.add.output_pack != V3D_QPU_PACK_NONE)
			return FALSE;

		v3d_uint32 packed;
		if (!v3d_qpu_float32_unpack_pack(instr->instr.alu.add.a.unpack,
										 &packed)) {
			return FALSE;
		}
		if (packed == 0)
			return FALSE;
		opcode |= packed << 2;

		break;

	case V3D_QPU_A_VFMIN:
	case V3D_QPU_A_VFMAX:
		if (instr->instr.alu.add.output_pack != V3D_QPU_PACK_NONE ||
			instr->instr.alu.add.b.unpack != V3D_QPU_UNPACK_NONE) {
			return FALSE;
		}

		if (!v3d_qpu_float16_unpack_pack(instr->instr.alu.add.a.unpack,
										 &packed)) {
			return FALSE;
		}
		opcode |= packed;
		break;

	default:
		if (instr->instr.alu.add.op != V3D_QPU_A_NOP &&
			(instr->instr.alu.add.output_pack != V3D_QPU_PACK_NONE ||
			 instr->instr.alu.add.a.unpack != V3D_QPU_UNPACK_NONE ||
			 instr->instr.alu.add.b.unpack != V3D_QPU_UNPACK_NONE)) {
			return FALSE;
		}
		break;
	}

	*packed_instr |= QPU_SET_FIELD(mux_a, V3D_QPU_ADD_A);    //GNU Statement Expressions!!! debebugdeug
	*packed_instr |= QPU_SET_FIELD(mux_b, V3D_QPU_ADD_B);
	*packed_instr |= QPU_SET_FIELD(opcode, V3D_QPU_OP_ADD);
	*packed_instr |= QPU_SET_FIELD(waddr, V3D_QPU_WADDR_A);
	if (instr->instr.alu.add.magic_write && !no_magic_write)
		*packed_instr |= V3D_QPU_MA;

	return TRUE;
}

static v3d_bool
v3d71_qpu_add_pack(const struct v3d_device_info *devinfo,
                   const struct v3d_qpu_instr *instr, v3d_uint64 *packed_instr)
{
	v3d_uint32 waddr = instr->instr.alu.add.waddr;
	v3d_uint32 raddr_a = instr->instr.alu.add.a.input.raddr;
	v3d_uint32 raddr_b = instr->instr.alu.add.b.input.raddr;

	int nsrc = v3d_qpu_add_op_num_src(instr->instr.alu.add.op);
	const struct opcode_desc *desc =
		lookup_opcode_from_instr(devinfo, add_ops_v71,
								 V3D_ARRAY_SIZE(add_ops_v71),
								 instr->instr.alu.add.op);
	if (!desc)
		return FALSE;

	v3d_uint32 opcode = desc->opcode_first;

	/* If an operation doesn't use an arg, its raddr values may be used to
	 * identify the operation type.
	 */
	if (nsrc < 2)
		raddr_b = ffsll(desc->mask.raddr_mask) - 1;

	v3d_bool no_magic_write = FALSE;

	switch (instr->instr.alu.add.op) {
	case V3D_QPU_A_STVPMV:
		waddr = 0;
		no_magic_write = TRUE;
		break;
	case V3D_QPU_A_STVPMD:
		waddr = 1;
		no_magic_write = TRUE;
		break;
	case V3D_QPU_A_STVPMP:
		waddr = 2;
		no_magic_write = TRUE;
		break;

	case V3D_QPU_A_LDVPMV_IN:
	case V3D_QPU_A_LDVPMD_IN:
	case V3D_QPU_A_LDVPMP:
	case V3D_QPU_A_LDVPMG_IN:
		v3d_assert(!instr->instr.alu.add.magic_write);
		break;

	case V3D_QPU_A_LDVPMV_OUT:
	case V3D_QPU_A_LDVPMD_OUT:
	case V3D_QPU_A_LDVPMG_OUT:
		v3d_assert(!instr->instr.alu.add.magic_write);
		*packed_instr |= V3D_QPU_MA;
		break;

	default:
		break;
	}

	switch (instr->instr.alu.add.op) {
	case V3D_QPU_A_FADD:
	case V3D_QPU_A_FADDNF:
	case V3D_QPU_A_FSUB:
	case V3D_QPU_A_FMIN:
	case V3D_QPU_A_FMAX:
	case V3D_QPU_A_FCMP: {
		v3d_uint32 output_pack;
		v3d_uint32 a_unpack;
		v3d_uint32 b_unpack;

		if (instr->instr.alu.add.op != V3D_QPU_A_FCMP) {
			if (!v3d_qpu_float32_pack_pack(instr->instr.alu.add.output_pack,
										   &output_pack)) {
				return FALSE;
			}
			opcode |= output_pack << 4;
		}

		if (!v3d_qpu_float32_unpack_pack(instr->instr.alu.add.a.unpack,
										 &a_unpack)) {
			return FALSE;
		}

		if (!v3d_qpu_float32_unpack_pack(instr->instr.alu.add.b.unpack,
										 &b_unpack)) {
			return FALSE;
		}

		/* These operations with commutative operands are
		 * distinguished by the order of the operands come in.
		 */
		v3d_bool ordering =
			instr->sig.small_imm_a * 256 + a_unpack * 64 + raddr_a >
			instr->sig.small_imm_b * 256 + b_unpack * 64 + raddr_b;
		if (((instr->instr.alu.add.op == V3D_QPU_A_FMIN ||
			  instr->instr.alu.add.op == V3D_QPU_A_FADD) && ordering) ||
			((instr->instr.alu.add.op == V3D_QPU_A_FMAX ||
			  instr->instr.alu.add.op == V3D_QPU_A_FADDNF) && !ordering)) {
			v3d_uint32 temp;

			temp = a_unpack;
			a_unpack = b_unpack;
			b_unpack = temp;

			temp = raddr_a;
			raddr_a = raddr_b;
			raddr_b = temp;

			/* If we are swapping raddr_a/b we also need to swap
			 * small_imm_a/b.
			 */
			if (instr->sig.small_imm_a || instr->sig.small_imm_b) {
				v3d_assert(instr->sig.small_imm_a !=
						   instr->sig.small_imm_b);
				struct v3d_qpu_sig new_sig = instr->sig;
				new_sig.small_imm_a = !instr->sig.small_imm_a;
				new_sig.small_imm_b = !instr->sig.small_imm_b;
				v3d_uint32 sig;
				if (!v3d_qpu_sig_pack(devinfo, &new_sig, &sig))
					return FALSE;
				*packed_instr &= ~V3D_QPU_SIG_MASK;
				*packed_instr |= QPU_SET_FIELD(sig, V3D_QPU_SIG);
			}
		}

		opcode |= a_unpack << 2;
		opcode |= b_unpack << 0;

		break;
	}

	case V3D_QPU_A_VFPACK: {
		v3d_uint32 a_unpack;
		v3d_uint32 b_unpack;

		if (instr->instr.alu.add.a.unpack == V3D_QPU_UNPACK_ABS ||
			instr->instr.alu.add.b.unpack == V3D_QPU_UNPACK_ABS) {
			return FALSE;
		}

		if (!v3d_qpu_float32_unpack_pack(instr->instr.alu.add.a.unpack,
										 &a_unpack)) {
			return FALSE;
		}

		if (!v3d_qpu_float32_unpack_pack(instr->instr.alu.add.b.unpack,
										 &b_unpack)) {
			return FALSE;
		}

		opcode = (opcode & ~(0x3 << 2)) | (a_unpack << 2);
		opcode = (opcode & ~(0x3 << 0)) | (b_unpack << 0);

		break;
	}

	case V3D_QPU_A_FFLOOR:
	case V3D_QPU_A_FROUND:
	case V3D_QPU_A_FTRUNC:
	case V3D_QPU_A_FCEIL:
	case V3D_QPU_A_FDX:
	case V3D_QPU_A_FDY: {
		v3d_uint32 packed;

		if (!v3d_qpu_float32_pack_pack(instr->instr.alu.add.output_pack,
									   &packed)) {
			return FALSE;
		}
		raddr_b |= packed;

		if (!v3d_qpu_float32_unpack_pack(instr->instr.alu.add.a.unpack,
										 &packed)) {
			return FALSE;
		}
		if (packed == 0)
			return FALSE;
		raddr_b = (raddr_b & ~(0x3 << 2)) | packed << 2;
		break;
	}

	case V3D_QPU_A_FTOIN:
	case V3D_QPU_A_FTOIZ:
	case V3D_QPU_A_FTOUZ:
	case V3D_QPU_A_FTOC:
		if (instr->instr.alu.add.output_pack != V3D_QPU_PACK_NONE)
			return FALSE;

		v3d_uint32 packed;
		if (!v3d_qpu_float32_unpack_pack(instr->instr.alu.add.a.unpack,
										 &packed)) {
			return FALSE;
		}
		if (packed == 0)
			return FALSE;

		raddr_b |= (raddr_b & ~(0x3 << 2)) | packed << 2;

		break;

	case V3D_QPU_A_VFMIN:
	case V3D_QPU_A_VFMAX:
		if (instr->instr.alu.add.output_pack != V3D_QPU_PACK_NONE ||
			instr->instr.alu.add.b.unpack != V3D_QPU_UNPACK_NONE) {
			return FALSE;
		}

		if (!v3d_qpu_float16_unpack_pack(instr->instr.alu.add.a.unpack,
										 &packed)) {
			return FALSE;
		}
		opcode |= packed;
		break;

	case V3D_QPU_A_MOV: {
		v3d_uint32 packed;

		if (instr->instr.alu.add.output_pack != V3D_QPU_PACK_NONE)
			return FALSE;

		if (!v3d_qpu_int32_unpack_pack(instr->instr.alu.add.a.unpack,
									   &packed)) {
			return FALSE;
		}

		raddr_b |= packed << 2;
		break;
	}

	case V3D_QPU_A_FMOV: {
		v3d_uint32 packed;

		if (!v3d_qpu_float32_pack_pack(instr->instr.alu.add.output_pack,
									   &packed)) {
			return FALSE;
		}
		raddr_b = packed;

		if (!v3d_qpu_float32_unpack_pack(instr->instr.alu.add.a.unpack,
										 &packed)) {
			return FALSE;
		}
		raddr_b |= packed << 2;
		break;
	}

	default:
		if (instr->instr.alu.add.op != V3D_QPU_A_NOP &&
			(instr->instr.alu.add.output_pack != V3D_QPU_PACK_NONE ||
			 instr->instr.alu.add.a.unpack != V3D_QPU_UNPACK_NONE ||
			 instr->instr.alu.add.b.unpack != V3D_QPU_UNPACK_NONE)) {
			return FALSE;
		}
		break;
	}

	*packed_instr |= QPU_SET_FIELD(raddr_a, V3D_QPU_RADDR_A);
	*packed_instr |= QPU_SET_FIELD(raddr_b, V3D_QPU_RADDR_B);
	*packed_instr |= QPU_SET_FIELD(opcode, V3D_QPU_OP_ADD);
	*packed_instr |= QPU_SET_FIELD(waddr, V3D_QPU_WADDR_A);
	if (instr->instr.alu.add.magic_write && !no_magic_write)
		*packed_instr |= V3D_QPU_MA;

	return TRUE;
}

static v3d_bool
v3d33_qpu_mul_pack(const struct v3d_device_info *devinfo,
                   const struct v3d_qpu_instr *instr, v3d_uint64 *packed_instr)
{
	v3d_uint32 mux_a = instr->instr.alu.mul.a.input.mux;
	v3d_uint32 mux_b = instr->instr.alu.mul.b.input.mux;
	int nsrc = v3d_qpu_mul_op_num_src(instr->instr.alu.mul.op);

	const struct opcode_desc *desc =
		lookup_opcode_from_instr(devinfo, mul_ops_v33,
								 V3D_ARRAY_SIZE(mul_ops_v33),
								 instr->instr.alu.mul.op);

	if (!desc)
		return FALSE;

	v3d_uint32 opcode = desc->opcode_first;

	/* Some opcodes have a single valid value for their mux a/b, so set
	 * that here.  If mux a/b determine packing, it will be set below.
	 */
	if (nsrc < 2)
		mux_b = ffs(desc->mask.mux.b_mask) - 1;

	if (nsrc < 1)
		mux_a = ffs(desc->mask.mux.a_mask) - 1;

	switch (instr->instr.alu.mul.op) {
	case V3D_QPU_M_FMUL: {
		v3d_uint32 packed;

		if (!v3d_qpu_float32_pack_pack(instr->instr.alu.mul.output_pack,
									   &packed)) {
			return FALSE;
		}
		/* No need for a +1 because desc->opcode_first has a 1 in this
		 * field.
		 */
		opcode += packed << 4;

		if (!v3d_qpu_float32_unpack_pack(instr->instr.alu.mul.a.unpack,
										 &packed)) {
			return FALSE;
		}
		opcode |= packed << 2;

		if (!v3d_qpu_float32_unpack_pack(instr->instr.alu.mul.b.unpack,
										 &packed)) {
			return FALSE;
		}
		opcode |= packed << 0;
		break;
	}

	case V3D_QPU_M_FMOV: {
		v3d_uint32 packed;

		if (!v3d_qpu_float32_pack_pack(instr->instr.alu.mul.output_pack,
									   &packed)) {
			return FALSE;
		}
		opcode |= (packed >> 1) & 1;
		mux_b = (packed & 1) << 2;

		if (!v3d_qpu_float32_unpack_pack(instr->instr.alu.mul.a.unpack,
										 &packed)) {
			return FALSE;
		}
		mux_b |= packed;
		break;
	}

	case V3D_QPU_M_VFMUL: {
		v3d_uint32 packed;

		if (instr->instr.alu.mul.output_pack != V3D_QPU_PACK_NONE)
			return FALSE;

		if (!v3d_qpu_float16_unpack_pack(instr->instr.alu.mul.a.unpack,
										 &packed)) {
			return FALSE;
		}
		if (instr->instr.alu.mul.a.unpack == V3D_QPU_UNPACK_SWAP_16)
			opcode = 8;
		else
			opcode |= (packed + 4) & 7;

		if (instr->instr.alu.mul.b.unpack != V3D_QPU_UNPACK_NONE)
			return FALSE;

		break;
	}

	default:
		if (instr->instr.alu.mul.op != V3D_QPU_M_NOP &&
			(instr->instr.alu.mul.output_pack != V3D_QPU_PACK_NONE ||
			 instr->instr.alu.mul.a.unpack != V3D_QPU_UNPACK_NONE ||
			 instr->instr.alu.mul.b.unpack != V3D_QPU_UNPACK_NONE)) {
			return FALSE;
		}
		break;
	}

	*packed_instr |= QPU_SET_FIELD(mux_a, V3D_QPU_MUL_A);
	*packed_instr |= QPU_SET_FIELD(mux_b, V3D_QPU_MUL_B);

	*packed_instr |= QPU_SET_FIELD(opcode, V3D_QPU_OP_MUL);
	*packed_instr |= QPU_SET_FIELD(instr->instr.alu.mul.waddr, V3D_QPU_WADDR_M);
	if (instr->instr.alu.mul.magic_write)
		*packed_instr |= V3D_QPU_MM;

	return TRUE;
}

static v3d_bool
v3d71_qpu_mul_pack(const struct v3d_device_info *devinfo,
                   const struct v3d_qpu_instr *instr, v3d_uint64 *packed_instr)
{
	v3d_uint32 raddr_c = instr->instr.alu.mul.a.input.raddr;
	v3d_uint32 raddr_d = instr->instr.alu.mul.b.input.raddr;
	int nsrc = v3d_qpu_mul_op_num_src(instr->instr.alu.mul.op);

	const struct opcode_desc *desc =
		lookup_opcode_from_instr(devinfo, mul_ops_v71,
								 V3D_ARRAY_SIZE(mul_ops_v71),
								 instr->instr.alu.mul.op);
	if (!desc)
		return FALSE;

	v3d_uint32 opcode = desc->opcode_first;

	/* Some opcodes have a single valid value for their raddr_d, so set
	 * that here.  If raddr_b determine packing, it will be set below.
	 */
	if (nsrc < 2)
		raddr_d = ffsll(desc->mask.raddr_mask) - 1;

	switch (instr->instr.alu.mul.op) {
	case V3D_QPU_M_FMUL: {
		v3d_uint32 packed;

		if (!v3d_qpu_float32_pack_pack(instr->instr.alu.mul.output_pack,
									   &packed)) {
			return FALSE;
		}
		/* No need for a +1 because desc->opcode_first has a 1 in this
		 * field.
		 */
		opcode += packed << 4;

		if (!v3d_qpu_float32_unpack_pack(instr->instr.alu.mul.a.unpack,
										 &packed)) {
			return FALSE;
		}
		opcode |= packed << 2;

		if (!v3d_qpu_float32_unpack_pack(instr->instr.alu.mul.b.unpack,
										 &packed)) {
			return FALSE;
		}
		opcode |= packed << 0;
		break;
	}

	case V3D_QPU_M_FMOV: {
		v3d_uint32 packed;

		if (!v3d_qpu_float32_pack_pack(instr->instr.alu.mul.output_pack,
									   &packed)) {
			return FALSE;
		}
		raddr_d |= packed;

		if (!v3d_qpu_float32_unpack_pack(instr->instr.alu.mul.a.unpack,
										 &packed)) {
			return FALSE;
		}
		raddr_d |= packed << 2;
		break;
	}

	case V3D_QPU_M_VFMUL: {
		v3d_unreachable("pending v71 update");
		v3d_uint32 packed;

		if (instr->instr.alu.mul.output_pack != V3D_QPU_PACK_NONE)
			return FALSE;

		if (!v3d_qpu_float16_unpack_pack(instr->instr.alu.mul.a.unpack,
										 &packed)) {
			return FALSE;
		}
		if (instr->instr.alu.mul.a.unpack == V3D_QPU_UNPACK_SWAP_16)
			opcode = 8;
		else
			opcode |= (packed + 4) & 7;

		if (instr->instr.alu.mul.b.unpack != V3D_QPU_UNPACK_NONE)
			return FALSE;

		break;
	}

	case V3D_QPU_M_MOV: {
		v3d_uint32 packed;

		if (instr->instr.alu.mul.output_pack != V3D_QPU_PACK_NONE)
			return FALSE;

		if (!v3d_qpu_int32_unpack_pack(instr->instr.alu.mul.a.unpack,
									   &packed)) {
			return FALSE;
		}

		raddr_d |= packed << 2;
		break;
	}

	default:
		if (instr->instr.alu.mul.op != V3D_QPU_M_NOP &&
			(instr->instr.alu.mul.output_pack != V3D_QPU_PACK_NONE ||
			 instr->instr.alu.mul.a.unpack != V3D_QPU_UNPACK_NONE ||
			 instr->instr.alu.mul.b.unpack != V3D_QPU_UNPACK_NONE)) {
			return FALSE;
		}
		break;
	}

	*packed_instr |= QPU_SET_FIELD(raddr_c, V3D_QPU_RADDR_C);
	*packed_instr |= QPU_SET_FIELD(raddr_d, V3D_QPU_RADDR_D);
	*packed_instr |= QPU_SET_FIELD(opcode, V3D_QPU_OP_MUL);
	*packed_instr |= QPU_SET_FIELD(instr->instr.alu.mul.waddr, V3D_QPU_WADDR_M);
	if (instr->instr.alu.mul.magic_write)
		*packed_instr |= V3D_QPU_MM;

	return TRUE;
}

static v3d_bool
v3d_qpu_add_pack(const struct v3d_device_info *devinfo,
                 const struct v3d_qpu_instr *instr, v3d_uint64 *packed_instr)
{
	if (devinfo->ver < 71)
		return v3d33_qpu_add_pack(devinfo, instr, packed_instr);
	else
		return v3d71_qpu_add_pack(devinfo, instr, packed_instr);
}

static v3d_bool
v3d_qpu_mul_pack(const struct v3d_device_info *devinfo,
                 const struct v3d_qpu_instr *instr, v3d_uint64 *packed_instr)
{
	if (devinfo->ver < 71)
		return v3d33_qpu_mul_pack(devinfo, instr, packed_instr);
	else
		return v3d71_qpu_mul_pack(devinfo, instr, packed_instr);
}

static v3d_bool
v3d_qpu_instr_unpack_alu(const struct v3d_device_info *devinfo,
                         v3d_uint64 packed_instr,
                         struct v3d_qpu_instr *instr)
{
	instr->type = V3D_QPU_INSTR_TYPE_ALU;

	if (!v3d_qpu_sig_unpack(devinfo,
							QPU_GET_FIELD(packed_instr, V3D_QPU_SIG),
							&instr->sig))
		return FALSE;

	v3d_uint32 packed_cond = QPU_GET_FIELD(packed_instr, V3D_QPU_COND);
	if (v3d_qpu_sig_writes_address(devinfo, &instr->sig)) {
		instr->sig_addr = packed_cond & ~V3D_QPU_COND_SIG_MAGIC_ADDR;
		instr->sig_magic = packed_cond & V3D_QPU_COND_SIG_MAGIC_ADDR;

		instr->flags.ac = V3D_QPU_COND_NONE;
		instr->flags.mc = V3D_QPU_COND_NONE;
		instr->flags.apf = V3D_QPU_PF_NONE;
		instr->flags.mpf = V3D_QPU_PF_NONE;
		instr->flags.auf = V3D_QPU_UF_NONE;
		instr->flags.muf = V3D_QPU_UF_NONE;
	} else {
		if (!v3d_qpu_flags_unpack(devinfo, packed_cond, &instr->flags))
			return FALSE;
	}

	if (devinfo->ver <= 71) {
		/*
		 * For v71 this will be set on add/mul unpack, as raddr are now
		 * part of v3d_qpu_input
		 */
		instr->raddr_a = QPU_GET_FIELD(packed_instr, V3D_QPU_RADDR_A);
		instr->raddr_b = QPU_GET_FIELD(packed_instr, V3D_QPU_RADDR_B);
	}

	if (!v3d_qpu_add_unpack(devinfo, packed_instr, instr))
		return FALSE;

	if (!v3d_qpu_mul_unpack(devinfo, packed_instr, instr))
		return FALSE;

	return TRUE;
}

static v3d_bool
v3d_qpu_instr_unpack_branch(const struct v3d_device_info *devinfo,
                            v3d_uint64 packed_instr,
                            struct v3d_qpu_instr *instr)
{
	instr->type = V3D_QPU_INSTR_TYPE_BRANCH;

	v3d_uint32 cond = QPU_GET_FIELD(packed_instr, V3D_QPU_BRANCH_COND);
	if (cond == 0)
		instr->instr.branch.cond = V3D_QPU_BRANCH_COND_ALWAYS;
	else if (V3D_QPU_BRANCH_COND_A0 + (cond - 2) <=
			 V3D_QPU_BRANCH_COND_ALLNA)
		instr->instr.branch.cond = V3D_QPU_BRANCH_COND_A0 + (cond - 2);
	else
		return FALSE;

	v3d_uint32 msfign = QPU_GET_FIELD(packed_instr, V3D_QPU_BRANCH_MSFIGN);
	if (msfign == 3)
		return FALSE;
	instr->instr.branch.msfign = msfign;

	instr->instr.branch.bdi = QPU_GET_FIELD(packed_instr, V3D_QPU_BRANCH_BDI);

	instr->instr.branch.ub = packed_instr & V3D_QPU_BRANCH_UB;
	if (instr->instr.branch.ub) {
		instr->instr.branch.bdu = QPU_GET_FIELD(packed_instr,
										  V3D_QPU_BRANCH_BDU);
	}

	instr->instr.branch.raddr_a = QPU_GET_FIELD(packed_instr,
										  V3D_QPU_RADDR_A);

	instr->instr.branch.offset = 0;

	instr->instr.branch.offset +=
		QPU_GET_FIELD(packed_instr,
					  V3D_QPU_BRANCH_ADDR_LOW) << 3;

	instr->instr.branch.offset +=
		QPU_GET_FIELD(packed_instr,
					  V3D_QPU_BRANCH_ADDR_HIGH) << 24;

	return TRUE;
}

v3d_bool
v3d_qpu_instr_unpack(const struct v3d_device_info *devinfo,
                     v3d_uint64 packed_instr,
                     struct v3d_qpu_instr *instr)
{
	if (QPU_GET_FIELD(packed_instr, V3D_QPU_OP_MUL) != 0) {
		return v3d_qpu_instr_unpack_alu(devinfo, packed_instr, instr);
	} else {
		v3d_uint32 sig = QPU_GET_FIELD(packed_instr, V3D_QPU_SIG);

		if ((sig & 24) == 16) {
			return v3d_qpu_instr_unpack_branch(devinfo, packed_instr,
											   instr);
		} else {
			return FALSE;
		}
	}
}

static v3d_bool //doithere
v3d_qpu_instr_pack_alu(const struct v3d_device_info *devinfo,
                       const struct v3d_qpu_instr *instr,
                       v3d_uint64 *packed_instr)
{
	v3d_uint32 sig;
	if (!v3d_qpu_sig_pack(devinfo, &instr->sig, &sig))
		return FALSE;
	*packed_instr |= QPU_SET_FIELD(sig, V3D_QPU_SIG);

	if (instr->type == V3D_QPU_INSTR_TYPE_ALU) {
		if (devinfo->ver < 71) {
			/*
			 * For v71 this will be set on add/mul unpack, as raddr are now
			 * part of v3d_qpu_input
			 */
			*packed_instr |= QPU_SET_FIELD(instr->raddr_a, V3D_QPU_RADDR_A);
			*packed_instr |= QPU_SET_FIELD(instr->raddr_b, V3D_QPU_RADDR_B);
		}

		if (!v3d_qpu_add_pack(devinfo, instr, packed_instr))
			return FALSE;
		if (!v3d_qpu_mul_pack(devinfo, instr, packed_instr))
			return FALSE;

		v3d_uint32 flags;
		if (v3d_qpu_sig_writes_address(devinfo, &instr->sig)) {
			if (instr->flags.ac != V3D_QPU_COND_NONE ||
				instr->flags.mc != V3D_QPU_COND_NONE ||
				instr->flags.apf != V3D_QPU_PF_NONE ||
				instr->flags.mpf != V3D_QPU_PF_NONE ||
				instr->flags.auf != V3D_QPU_UF_NONE ||
				instr->flags.muf != V3D_QPU_UF_NONE) {
				return FALSE;
			}

			flags = instr->sig_addr;
			if (instr->sig_magic)
				flags |= V3D_QPU_COND_SIG_MAGIC_ADDR;
		} else {
			if (!v3d_qpu_flags_pack(devinfo, &instr->flags, &flags))
				return FALSE;
		}

		*packed_instr |= QPU_SET_FIELD(flags, V3D_QPU_COND);
	} else {
		if (v3d_qpu_sig_writes_address(devinfo, &instr->sig))
			return FALSE;
	}

	return TRUE;
}

static v3d_bool
v3d_qpu_instr_pack_branch(const struct v3d_device_info *devinfo,
                          const struct v3d_qpu_instr *instr,
                          v3d_uint64 *packed_instr)
{
	*packed_instr |= QPU_SET_FIELD(16, V3D_QPU_SIG);

	if (instr->instr.branch.cond != V3D_QPU_BRANCH_COND_ALWAYS) {
		*packed_instr |= QPU_SET_FIELD(2 + (instr->instr.branch.cond -
											V3D_QPU_BRANCH_COND_A0),
									   V3D_QPU_BRANCH_COND);
	}

	*packed_instr |= QPU_SET_FIELD(instr->instr.branch.msfign,
								   V3D_QPU_BRANCH_MSFIGN);

	*packed_instr |= QPU_SET_FIELD(instr->instr.branch.bdi,
								   V3D_QPU_BRANCH_BDI);

	if (instr->instr.branch.ub) {
		*packed_instr |= V3D_QPU_BRANCH_UB;
		*packed_instr |= QPU_SET_FIELD(instr->instr.branch.bdu,
									   V3D_QPU_BRANCH_BDU);
	}

	switch (instr->instr.branch.bdi) {
	case V3D_QPU_BRANCH_DEST_ABS:
	case V3D_QPU_BRANCH_DEST_REL:
		*packed_instr |= QPU_SET_FIELD(instr->instr.branch.msfign,
									   V3D_QPU_BRANCH_MSFIGN);

		*packed_instr |= QPU_SET_FIELD((instr->instr.branch.offset &
										~0xff000000) >> 3,
									   V3D_QPU_BRANCH_ADDR_LOW);

		*packed_instr |= QPU_SET_FIELD(instr->instr.branch.offset >> 24,
									   V3D_QPU_BRANCH_ADDR_HIGH);
		break;
	default:
		break;
	}

	if (instr->instr.branch.bdi == V3D_QPU_BRANCH_DEST_REGFILE ||
		instr->instr.branch.bdu == V3D_QPU_BRANCH_DEST_REGFILE) {
		*packed_instr |= QPU_SET_FIELD(instr->instr.branch.raddr_a,
									   V3D_QPU_RADDR_A);
	}

	return TRUE;
}

v3d_bool
v3d_qpu_instr_pack(const struct v3d_device_info *devinfo,
                   const struct v3d_qpu_instr *instr,
                   v3d_uint64 *packed_instr)
{
	*packed_instr = 0;

	switch (instr->type) {
	case V3D_QPU_INSTR_TYPE_ALU:
		return v3d_qpu_instr_pack_alu(devinfo, instr, packed_instr);
	case V3D_QPU_INSTR_TYPE_BRANCH:
		return v3d_qpu_instr_pack_branch(devinfo, instr, packed_instr);
	default:
		return FALSE;
	}
}

// >>> qpu_disasm.c

struct disasm_state {
	const struct v3d_device_info *devinfo;
	char *string;
	size_t size;
	size_t offset;
};

static void
append(struct disasm_state *disasm, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	size_t result =
		v3d_vsnprintf(disasm->string + disasm->offset,
					  disasm->size - disasm->offset,
					  fmt, args);
	va_end(args);
	if (result >= disasm->size - disasm->offset)
	{
		disasm->offset = disasm->size;
	}
	else
	{
		disasm->offset += result;
	}
}

static void
pad_to(struct disasm_state *disasm, int n)
{
	/* FIXME: Do a single append somehow. */
	while (disasm->offset < n && disasm->offset < disasm->size)
		append(disasm, " ");
}

static void
v3d33_qpu_disasm_raddr(struct disasm_state *disasm,
                       const struct v3d_qpu_instr *instr,
                       enum v3d_qpu_mux mux)
{
	if (mux == V3D_QPU_MUX_A) {
		append(disasm, "rf%d", instr->raddr_a);
	} else if (mux == V3D_QPU_MUX_B) {
		if (instr->sig.small_imm_b) {
			v3d_uint32 val = 0;
			v3d_bool ok =
				v3d_qpu_small_imm_unpack(disasm->devinfo,
										 instr->raddr_b,
										 &val);

			if ((int)val >= -16 && (int)val <= 15)
				append(disasm, "%d", val);
			else
				append(disasm, "0x%08x", val);
			v3d_assert(ok);
		} else {
			append(disasm, "rf%d", instr->raddr_b);
		}
	} else {
		append(disasm, "r%d", mux);
	}
}

// Call when you already expect name to be a register file
static v3d_bool v3d_assemble_parse_register_file(const char* name, v3d_uint8* registerFileOut,
                                                 const char** endOfNameOut)
{
	if (name[0] == 'r' && name[1] == 'f')
	{
		// Avoid needing atoi
		if (name[2] > '9' || name[2] < '0')
		{
			return FALSE;
		}
		v3d_uint8 registerFileNumber = name[2] - '0';
		v3d_bool hasSecondDigit = (name[3] <= '9' && name[3] >= '0');
		if ((name[3] != 0 && name[3] != '.' && name[3] != ' ' && name[3] != '\t' &&
		     name[3] != ',') &&
		    !hasSecondDigit)
		{
			return FALSE;
		}
		if (hasSecondDigit)
		{
			registerFileNumber *= 10;
			registerFileNumber += name[3] - '0';
		}
		if (registerFileNumber > 31)
		{
			return FALSE;
		}
		*registerFileOut = registerFileNumber;
		*endOfNameOut = name + (hasSecondDigit ? 4 : 3);
		return TRUE;
	}
	return FALSE;
}

enum v3d_qpu_assemble_raddr_result
{
	v3d_qpu_assemble_raddr_result_success,
	v3d_qpu_assemble_raddr_result_invalid_register_file,
	v3d_qpu_assemble_raddr_result_invalid_accumulator_register,
	v3d_qpu_assemble_raddr_result_invalid_small_immediate,
	v3d_qpu_assemble_raddr_result_no_raddr_space_too_many_immediates,
	v3d_qpu_assemble_raddr_result_no_raddr_space,
};

// See vir_to_qpu.c set_src() and v3d_generate_code_block()
static enum v3d_qpu_assemble_raddr_result v3d33_qpu_assemble_raddr(struct v3d_qpu_instr* instr,
                                                                   enum v3d_qpu_mux* mux,
                                                                   const char* name,
                                                                   const char** endOfNameOut)
{
	// First, figure out what the desired operand is
	v3d_uint8 desiredOperand = 0;
	if (name[0] == 'r' && name[1] == 'f')
	{
		// Register file
		if (!v3d_assemble_parse_register_file(name, &desiredOperand, endOfNameOut))
			return v3d_qpu_assemble_raddr_result_invalid_register_file;

		if (instr->instr.alu.add.a.input.mux == V3D_QPU_MUX_A || instr->instr.alu.add.b.input.mux == V3D_QPU_MUX_A ||
		    instr->instr.alu.mul.a.input.mux == V3D_QPU_MUX_A || instr->instr.alu.mul.b.input.mux == V3D_QPU_MUX_A)
		{
			// Already set? If so re-use it
			if (instr->raddr_a == desiredOperand)
			{
				*mux = V3D_QPU_MUX_A;
				return v3d_qpu_assemble_raddr_result_success;
			}
			else
			{
				if ((instr->instr.alu.add.a.input.mux == V3D_QPU_MUX_B ||
				     instr->instr.alu.add.b.input.mux == V3D_QPU_MUX_B ||
				     instr->instr.alu.mul.a.input.mux == V3D_QPU_MUX_B ||
				     instr->instr.alu.mul.b.input.mux == V3D_QPU_MUX_B) &&
				    instr->raddr_b != desiredOperand)
					return v3d_qpu_assemble_raddr_result_no_raddr_space;

				*mux = V3D_QPU_MUX_B;
				instr->raddr_b = desiredOperand;
				return v3d_qpu_assemble_raddr_result_success;
			}
		}
		else
		{
			*mux = V3D_QPU_MUX_A;
			instr->raddr_a = desiredOperand;
			return v3d_qpu_assemble_raddr_result_success;
		}
	}
	else if (name[0] == 'r')
	{
		// Accumulator register
		if (name[1] < '0' || name[1] > '5' ||
		    !(name[2] == '\n' || name[2] == '.' || name[2] == ' ' || name[2] == '\t' ||
		      name[2] == ',' || name[2] == 0))
			return v3d_qpu_assemble_raddr_result_invalid_accumulator_register;
		desiredOperand = name[1] - '0';
		*mux = desiredOperand + V3D_QPU_MUX_R0;  // Unnecessary, but illustrative
		*endOfNameOut = name + 2;
		return v3d_qpu_assemble_raddr_result_success;
	}
	else
	{
		// Small immediate
		v3d_uint32 packed_small_immediate = 0;
		if (!v3d_qpu_small_imm_from_name(name, &packed_small_immediate, endOfNameOut))
			return v3d_qpu_assemble_raddr_result_invalid_small_immediate;

		desiredOperand = (v3d_uint8)packed_small_immediate;
		// Small immediate must occupy raddr_b
		if (instr->instr.alu.add.a.input.mux == V3D_QPU_MUX_B || instr->instr.alu.add.b.input.mux == V3D_QPU_MUX_B ||
		    instr->instr.alu.mul.a.input.mux == V3D_QPU_MUX_B || instr->instr.alu.mul.b.input.mux == V3D_QPU_MUX_B)
            {
                if (instr->raddr_b = desiredOperand) //start debugdebug
                {
                    *mux = V3D_QPU_MUX_B;
                    return v3d_qpu_assemble_raddr_result_success;
                }                                    //end debugdebug
			return v3d_qpu_assemble_raddr_result_no_raddr_space_too_many_immediates;
            }
		*mux = V3D_QPU_MUX_B;
		instr->raddr_b = desiredOperand;
		instr->sig.small_imm_b = 1;
		return v3d_qpu_assemble_raddr_result_success;
	}
}


static void
v3d71_qpu_disasm_raddr(struct disasm_state *disasm,
                       const struct v3d_qpu_instr *instr,
                       v3d_uint8 raddr,
                       enum v3d_qpu_input_class input_class)
{
	v3d_bool is_small_imm = FALSE;
	switch(input_class) {
	case V3D_QPU_ADD_A:
		is_small_imm = instr->sig.small_imm_a;
		break;
	case V3D_QPU_ADD_B:
		is_small_imm = instr->sig.small_imm_b;
		break;
	case V3D_QPU_MUL_A:
		is_small_imm = instr->sig.small_imm_c;
		break;
	case V3D_QPU_MUL_B:
		is_small_imm = instr->sig.small_imm_d;
		break;
	}

	if (is_small_imm) {
		v3d_uint32 val = 0;
		v3d_bool ok =
			v3d_qpu_small_imm_unpack(disasm->devinfo,
									 raddr,
									 &val);

		if ((int)val >= -16 && (int)val <= 15)
			append(disasm, "%d", val);
		else
			append(disasm, "0x%08x", val);
		v3d_assert(ok);
	} else {
		append(disasm, "rf%d", raddr);
	}
}

static void
v3d_qpu_disasm_raddr(struct disasm_state *disasm,
                     const struct v3d_qpu_instr *instr,
                     const struct v3d_qpu_input *input,
                     enum v3d_qpu_input_class input_class)
{
	if (disasm->devinfo->ver < 71)
		v3d33_qpu_disasm_raddr(disasm, instr, input->input.mux);
	else
		v3d71_qpu_disasm_raddr(disasm, instr, input->input.raddr, input_class);
}

static void
v3d_qpu_disasm_waddr(struct disasm_state *disasm, v3d_uint32 waddr, v3d_bool magic)
{
	if (!magic) {
		append(disasm, "rf%d", waddr);
		return;
	}

	const char *name = v3d_qpu_magic_waddr_name(disasm->devinfo, waddr);
	if (name)
		append(disasm, "%s", name);
	else
		append(disasm, "waddr UNKNOWN %d", waddr);
}

static void
v3d_qpu_disasm_add(struct disasm_state *disasm,
                   const struct v3d_qpu_instr *instr)
{
	v3d_bool has_dst = v3d_qpu_add_op_has_dst(instr->instr.alu.add.op);
	int num_src = v3d_qpu_add_op_num_src(instr->instr.alu.add.op);

	append(disasm, "%s", v3d_qpu_add_op_name(instr->instr.alu.add.op));
	if (!v3d_qpu_sig_writes_address(disasm->devinfo, &instr->sig))
		append(disasm, "%s", v3d_qpu_cond_name(instr->flags.ac));
	append(disasm, "%s", v3d_qpu_pf_name(instr->flags.apf));
	append(disasm, "%s", v3d_qpu_uf_name(instr->flags.auf));

	append(disasm, " ");

	if (has_dst) {
		v3d_qpu_disasm_waddr(disasm, instr->instr.alu.add.waddr,
							 instr->instr.alu.add.magic_write);
		append(disasm, v3d_qpu_pack_name(instr->instr.alu.add.output_pack));
	}

	if (num_src >= 1) {
		if (has_dst)
			append(disasm, ", ");
		v3d_qpu_disasm_raddr(disasm, instr, &instr->instr.alu.add.a, V3D_QPU_ADD_A);
		append(disasm, "%s",
			   v3d_qpu_unpack_name(instr->instr.alu.add.a.unpack));
	}

	if (num_src >= 2) {
		append(disasm, ", ");
		v3d_qpu_disasm_raddr(disasm, instr, &instr->instr.alu.add.b, V3D_QPU_ADD_B);
		append(disasm, "%s",
			   v3d_qpu_unpack_name(instr->instr.alu.add.b.unpack));
	}
}

static void
v3d_qpu_disasm_mul(struct disasm_state *disasm,
                   const struct v3d_qpu_instr *instr)
{
	v3d_bool has_dst = v3d_qpu_mul_op_has_dst(instr->instr.alu.mul.op);
	int num_src = v3d_qpu_mul_op_num_src(instr->instr.alu.mul.op);

	pad_to(disasm, 30);
	append(disasm, "; ");

	append(disasm, "%s", v3d_qpu_mul_op_name(instr->instr.alu.mul.op));
	if (!v3d_qpu_sig_writes_address(disasm->devinfo, &instr->sig))
		append(disasm, "%s", v3d_qpu_cond_name(instr->flags.mc));
	append(disasm, "%s", v3d_qpu_pf_name(instr->flags.mpf));
	append(disasm, "%s", v3d_qpu_uf_name(instr->flags.muf));

	if (instr->instr.alu.mul.op == V3D_QPU_M_NOP)
		return;

	append(disasm, " ");

	if (has_dst) {
		v3d_qpu_disasm_waddr(disasm, instr->instr.alu.mul.waddr,
							 instr->instr.alu.mul.magic_write);
		append(disasm, v3d_qpu_pack_name(instr->instr.alu.mul.output_pack));
	}

	if (num_src >= 1) {
		if (has_dst)
			append(disasm, ", ");
		v3d_qpu_disasm_raddr(disasm, instr, &instr->instr.alu.mul.a, V3D_QPU_MUL_A);
		append(disasm, "%s",
			   v3d_qpu_unpack_name(instr->instr.alu.mul.a.unpack));
	}

	if (num_src >= 2) {
		append(disasm, ", ");
		v3d_qpu_disasm_raddr(disasm, instr, &instr->instr.alu.mul.b, V3D_QPU_MUL_B);
		append(disasm, "%s",
			   v3d_qpu_unpack_name(instr->instr.alu.mul.b.unpack));
	}
}

static void
v3d_qpu_disasm_sig_addr(struct disasm_state *disasm,
                        const struct v3d_qpu_instr *instr)
{
	if (disasm->devinfo->ver < 41)
		return;

	if (!instr->sig_magic)
		append(disasm, ".rf%d", instr->sig_addr);
	else {
		const char *name =
			v3d_qpu_magic_waddr_name(disasm->devinfo,
									 instr->sig_addr);
		if (name)
			append(disasm, ".%s", name);
		else
			append(disasm, ".UNKNOWN%d", instr->sig_addr);
	}
}

static void
v3d_qpu_disasm_sig(struct disasm_state *disasm,
                   const struct v3d_qpu_instr *instr)
{
	const struct v3d_qpu_sig *sig = &instr->sig;

	if (!sig->thrsw &&
		!sig->ldvary &&
		!sig->ldvpm &&
		!sig->ldtmu &&
		!sig->ldtlb &&
		!sig->ldtlbu &&
		!sig->ldunif &&
		!sig->ldunifrf &&
		!sig->ldunifa &&
		!sig->ldunifarf &&
		!sig->wrtmuc) {
		return;
	}

	pad_to(disasm, 60);

	if (sig->thrsw)
		append(disasm, "; thrsw");
	if (sig->ldvary) {
		append(disasm, "; ldvary");
		v3d_qpu_disasm_sig_addr(disasm, instr);
	}
	if (sig->ldvpm)
		append(disasm, "; ldvpm");
	if (sig->ldtmu) {
		append(disasm, "; ldtmu");
		v3d_qpu_disasm_sig_addr(disasm, instr);
	}
	if (sig->ldtlb) {
		append(disasm, "; ldtlb");
		v3d_qpu_disasm_sig_addr(disasm, instr);
	}
	if (sig->ldtlbu) {
		append(disasm, "; ldtlbu");
		v3d_qpu_disasm_sig_addr(disasm, instr);
	}
	if (sig->ldunif)
		append(disasm, "; ldunif");
	if (sig->ldunifrf) {
		append(disasm, "; ldunifrf");
		v3d_qpu_disasm_sig_addr(disasm, instr);
	}
	if (sig->ldunifa)
		append(disasm, "; ldunifa");
	if (sig->ldunifarf) {
		append(disasm, "; ldunifarf");
		v3d_qpu_disasm_sig_addr(disasm, instr);
	}
	if (sig->wrtmuc)
		append(disasm, "; wrtmuc");
}

// (todo Pi 5) add signals for v3d 7
const char* sig_names[] = {
    "thrsw",  "ldvary",   "ldvpm",   "ldtmu",     "ldtlb",  "ldtlbu",
    "ldunif", "ldunifrf", "ldunifa", "ldunifarf", "wrtmuc",
};

// Matches sig_names
v3d_bool sig_has_address[] = {
	FALSE, TRUE, FALSE, TRUE, TRUE, TRUE,
	FALSE, TRUE, FALSE, TRUE, FALSE,
};

// Using the index from matching name in sig_names, get which bit is associated with the signal
int sig_bits[] =
{
// thrsw = 0
// ldunif = 1
// ldunifa = 2
// ldunifrf = 3
// ldunifarf = 4
// ldtmu = 5
// ldvary = 6
// ldvpm = 7
// ldtlb = 8
// ldtlbu = 9
// ucb = 10
// rotate = 11
// wrtmuc = 12
	0, 6, 7, 5, 8, 9,
	1, 3, 2, 4, 12,
};

static v3d_bool v3d_qpu_assemble_signal(struct v3d_qpu_sig* sig, v3d_bool* signalTakesAddress,
                                        const char* name, const char** endOfNameOut)
{
	for (int index = 0; index < V3D_ARRAY_SIZE(sig_names); ++index)
	{
		if (v3d_symbol_equals(sig_names[index], name, endOfNameOut))
		{
			if (signalTakesAddress)
				*signalTakesAddress = sig_has_address[index];

			//*((v3d_uint32*)sig) |= 1 << sig_bits[index]; //debugdebug endian issue indeed
            *((v3d_uint32*)sig) |= 1 << (31 - sig_bits[index]);
            return TRUE;
		}
	}
	return FALSE;
}

static void
v3d_qpu_disasm_alu(struct disasm_state *disasm,
                   const struct v3d_qpu_instr *instr)
{
	v3d_qpu_disasm_add(disasm, instr);
	v3d_qpu_disasm_mul(disasm, instr);
	v3d_qpu_disasm_sig(disasm, instr);
}

static void
v3d_qpu_disasm_branch(struct disasm_state *disasm,
                      const struct v3d_qpu_instr *instr)
{
	append(disasm, "b");
	if (instr->instr.branch.ub)
		append(disasm, "u");
	append(disasm, "%s", v3d_qpu_branch_cond_name(instr->instr.branch.cond));
	append(disasm, "%s", v3d_qpu_msfign_name(instr->instr.branch.msfign));

	switch (instr->instr.branch.bdi) {
	case V3D_QPU_BRANCH_DEST_ABS:
		append(disasm, "  zero_addr+0x%08x", instr->instr.branch.offset);
		break;

	case V3D_QPU_BRANCH_DEST_REL:
		append(disasm, "  %d", instr->instr.branch.offset);
		break;

	case V3D_QPU_BRANCH_DEST_LINK_REG:
		append(disasm, "  lri");
		break;

	case V3D_QPU_BRANCH_DEST_REGFILE:
		append(disasm, "  rf%d", instr->instr.branch.raddr_a);
		break;
	}

	if (instr->instr.branch.ub) {
		switch (instr->instr.branch.bdu) {
		case V3D_QPU_BRANCH_DEST_ABS:
			append(disasm, ", a:unif");
			break;

		case V3D_QPU_BRANCH_DEST_REL:
			append(disasm, ", r:unif");
			break;

		case V3D_QPU_BRANCH_DEST_LINK_REG:
			append(disasm, ", lri");
			break;

		case V3D_QPU_BRANCH_DEST_REGFILE:
			append(disasm, ", rf%d", instr->instr.branch.raddr_a);
			break;
		}
	}
}

size_t
v3d_qpu_decode(const struct v3d_device_info *devinfo,
               const struct v3d_qpu_instr *instr,
               char* outBuffer, size_t outBufferSize)
{
	struct disasm_state disasm = {
		.string = outBuffer,
		.size = outBufferSize,
		.offset = 0,
		.devinfo = devinfo,
	};

	switch (instr->type) {
	case V3D_QPU_INSTR_TYPE_ALU:
		v3d_qpu_disasm_alu(&disasm, instr);
		break;

	case V3D_QPU_INSTR_TYPE_BRANCH:
		v3d_qpu_disasm_branch(&disasm, instr);
		break;
	}

	return disasm.offset;
}

size_t
v3d_qpu_disasm(const struct v3d_device_info *devinfo, v3d_uint64 inst, char* outBuffer, size_t outBufferSize)
{
	struct v3d_qpu_instr instr;
	v3d_bool ok = v3d_qpu_instr_unpack(devinfo, inst, &instr);
	v3d_assert(ok); //(void)ok;

	return v3d_qpu_decode(devinfo, &instr, outBuffer, outBufferSize);
}

// Skip through whitespace or comments until e.g. a symbol start is encountered.
// Returns false if a non-multiline-commented newline or end of string encountered before a symbol
// was found.
v3d_bool v3d_qpu_skip_whitespace_comments(const char** readHeadInOut)
{
	const char* currentChar;
	int commentDepth = 0;
	for (currentChar = *readHeadInOut; *currentChar && (commentDepth || *currentChar != '\n');
	     ++currentChar)
	{
		if (*currentChar == '\t' || *currentChar == '\r' || *currentChar == ' ')
			continue;

		// C++ style comment to end of line; find the end to make sure we advance the right number
		// of characters
		if (!commentDepth && currentChar[0] == '/' && currentChar[1] == '/')
		{
			while (*currentChar && *currentChar != '\n')
				++currentChar;
			break;
		}
		// /**/-style comments; support nesting.
		if (currentChar[0] == '*' && currentChar[1] == '/')
		{
			--commentDepth;
			++currentChar;
			continue;
		}
		if (currentChar[0] == '/' && currentChar[1] == '*')
		{
			++commentDepth;
			++currentChar;
		}
		if (commentDepth)
			continue;

		*readHeadInOut = currentChar;
		return TRUE;
	}
	*readHeadInOut = currentChar;
	return FALSE;
}

// This assembler is written by Macoy Madson (not from Mesa)
v3d_uint32 v3d_qpu_assemble(struct v3d_qpu_assemble_arguments* args) //gotoass
{
    if (args->devinfo.ver >= 70)
	{
		args->errorMessage = "V3D 7.x assembler not implemented";
		return 0;
	}

	// Silly, but for consistency in error hint lists
	static const char* rf_names[] = {
	    "rf0",  "rf1",  "rf2",  "rf3",  "rf4",  "rf5",  "rf6",  "rf7",  "rf8",  "rf9",  "rf10",
	    "rf11", "rf12", "rf13", "rf14", "rf15", "rf16", "rf17", "rf18", "rf19", "rf20", "rf21",
	    "rf22", "rf23", "rf24", "rf25", "rf26", "rf27", "rf28", "rf29", "rf30", "rf31",
	};
	static const char* accumulator_register_names[] = {"r0", "r1", "r2", "r3", "r4", "r5"};
	v3d_bool parsedSuccessfully = TRUE;
	const char* currentChar = args->assembly;
	const char** errorHintList = NULL;
	int numErrorHints = 0;

#define BREAK_ERROR_HINT_SIZE(message, hintList, hintListSize) \
	if (!parsedSuccessfully)                                   \
	{                                                          \
		args->errorAtOffset = currentChar - args->assembly;    \
		args->errorMessage = (message);                        \
		errorHintList = (hintList);                            \
		numErrorHints = hintListSize;                          \
		break;                                                 \
	}
	// NOTE: Assumes V3D_ARRAY_SIZE works on hintList
#define BREAK_ERROR(message, hintList) \
	BREAK_ERROR_HINT_SIZE(message, hintList, V3D_ARRAY_SIZE(hintList))

#define BREAK_ERROR_NO_HINTS(message) \
	BREAK_ERROR_HINT_SIZE(message, NULL, 0)

	// Mostly just to allow us to break to get to standard exit
	for (int numLoops = 0; numLoops < 1; ++numLoops)
	{
		if (!v3d_qpu_skip_whitespace_comments(&currentChar))
		{
			args->isEmptyLine = TRUE;
			return currentChar - args->assembly;
		}

		args->instructionStartsAtOffset = currentChar - args->assembly;

		// If we got this far we hit a character, so it's time to parse
		// Filter on 'a' because there is a single ALU instruction that starts with b, barrierid.
		// Since branches are either or b bu, we should be safe using the 'a' to discriminate them.
		if (currentChar[0] == 'b' && currentChar[1] != 'a')
		{
			// Branch instruction
			// TODO
			parsedSuccessfully = FALSE;
			args->errorAtOffset = currentChar - args->assembly;
			args->errorMessage = "Branch instructions unimplemented";
			break;
		}
		else
		{
			// ALU instruction
			// Add and mul are parsed nearly exactly the same
			struct instruction_outputs
			{
				v3d_uint32* op;
				const char** availableOperations;
				int numAvailableOperations;
				const char* operationNotFoundError;
				struct v3d_qpu_input* inputs[2];
				v3d_uint8* waddr;
				v3d_bool* magic_write;
				enum v3d_qpu_output_pack* output_pack;
			};
			struct instruction_outputs outputs[2] = {
			    {
			        (v3d_uint32*)&args->instruction.instr.alu.add.op,
			        add_op_names,
					V3D_ARRAY_SIZE(add_op_names),
					"Expected ALU add instruction or nop",
			        {&args->instruction.instr.alu.add.a, &args->instruction.instr.alu.add.b},
			        &args->instruction.instr.alu.add.waddr,
			        &args->instruction.instr.alu.add.magic_write,
			        &args->instruction.instr.alu.add.output_pack,
			    },
				{
			        (v3d_uint32*)&args->instruction.instr.alu.mul.op,
			        mul_op_names,
					V3D_ARRAY_SIZE(mul_op_names),
					"Expected ALU mul instruction or nop",
			        {&args->instruction.instr.alu.mul.a, &args->instruction.instr.alu.mul.b},
			        &args->instruction.instr.alu.mul.waddr,
			        &args->instruction.instr.alu.mul.magic_write,
			        &args->instruction.instr.alu.mul.output_pack,
			    },
			};
			// Search for this for the only other parsing differences
			const int mulIndex = 1;

			for (int outputIndex = 0; outputIndex < V3D_ARRAY_SIZE(outputs); ++outputIndex)
			{
				struct instruction_outputs* output = &outputs[outputIndex];
				if (outputIndex > 0)
				{
					if (!v3d_qpu_skip_whitespace_comments(&currentChar) || currentChar[0] != ';')
					{
						parsedSuccessfully = FALSE;
						BREAK_ERROR_NO_HINTS("Expected ';' between add and mul instructions");
					}
					++currentChar;

					if (!v3d_qpu_skip_whitespace_comments(&currentChar))
					{
						parsedSuccessfully = FALSE;
						BREAK_ERROR_HINT_SIZE(output->operationNotFoundError,
						                      output->availableOperations,
						                      output->numAvailableOperations);
					}
				}

				parsedSuccessfully = v3d_qpu_value_from_name_list(
				    currentChar, output->availableOperations, output->numAvailableOperations,
				    /*dotOptional=*/FALSE, output->op, &currentChar);
				BREAK_ERROR_HINT_SIZE(output->operationNotFoundError, output->availableOperations,
				                      output->numAvailableOperations);

				// From vir_to_qpu.c, v3d_qpu_nop() sets magic for NOP
				if (*output->op == V3D_QPU_M_NOP || *output->op == V3D_QPU_A_NOP)
                {
					*output->waddr = V3D_QPU_WADDR_NOP;
					*output->magic_write = TRUE;
				}

				// Condition and flags -- MiniGLV3D fix: route to the ADD-pipe
				// fields (ac/apf/auf) when parsing the add slot (outputIndex
				// == 0) and the MUL-pipe fields (mc/mpf/muf) when parsing the
				// mul slot (outputIndex == mulIndex), matching real MESA
				// (broadcom/compiler/vir.c's vir_set_pf/vir_set_uf/vir_get_cond,
				// which select apf vs mpf / auf vs muf / ac vs mc based on
				// vir_is_add(inst)). The original ported code always wrote
				// ac/mpf/muf regardless of which pipe was being parsed --
				// coincidentally correct for a cond suffix on an add-pipe op
				// (e.g. setmsf.ifna, needs ac) but wrong for a pf/uf suffix on
				// an add-pipe op (e.g. fcmp.pushc, needs apf, was silently
				// getting mpf instead -- the flag never reflected the add-pipe
				// result at all). Found via a real hardware alpha-test bug:
				// the discard mechanism appeared to fire unconditionally
				// regardless of the actual alpha value, since fcmp's PUSHC
				// was never actually being read by anything.
				{
					v3d_uint32* condField = (outputIndex == mulIndex)
					    ? (v3d_uint32*)&args->instruction.flags.mc
					    : (v3d_uint32*)&args->instruction.flags.ac;
					v3d_uint32* pfField = (outputIndex == mulIndex)
					    ? (v3d_uint32*)&args->instruction.flags.mpf
					    : (v3d_uint32*)&args->instruction.flags.apf;
					v3d_uint32* ufField = (outputIndex == mulIndex)
					    ? (v3d_uint32*)&args->instruction.flags.muf
					    : (v3d_uint32*)&args->instruction.flags.auf;

					while (*currentChar == '.')
					{
						if (v3d_qpu_value_from_name_list(
								currentChar, cond_names, V3D_ARRAY_SIZE(cond_names),
								/*dotOptional=*/TRUE, condField,
								&currentChar))
							continue;

						if (v3d_qpu_value_from_name_list(
								currentChar, pf_names, V3D_ARRAY_SIZE(pf_names),
								/*dotOptional=*/TRUE, pfField,
								&currentChar))
							continue;

						if (v3d_qpu_value_from_name_list(
								currentChar, uf_names, V3D_ARRAY_SIZE(uf_names),
								/*dotOptional=*/TRUE, ufField,
								&currentChar))
							continue;
						parsedSuccessfully = FALSE;
						break;
					}
				}
				BREAK_ERROR("Condition, pack flags, or uf unrecognized", cond_pf_uf_names);

				v3d_bool has_dst = FALSE;
				int num_src = 0;
				if (outputIndex == mulIndex)
				{
					has_dst = v3d_qpu_mul_op_has_dst(*output->op);
					num_src = v3d_qpu_mul_op_num_src(*output->op);
				}
				else
				{
					has_dst = v3d_qpu_add_op_has_dst(*output->op);
					num_src = v3d_qpu_add_op_num_src(*output->op);
				}

				if (has_dst) //checked here
				{
					enum v3d_qpu_waddr waddr = 0; //debugdebug

                    if (!v3d_qpu_skip_whitespace_comments(&currentChar))
					{
						parsedSuccessfully = FALSE;
						BREAK_ERROR("Expected destination operand rf0 through rf31 or waddr",
						            waddr_names);
					}
					if (currentChar[0] == 'r' && currentChar[1] == 'f')
					{
						parsedSuccessfully = v3d_assemble_parse_register_file(
						    currentChar, output->waddr, &currentChar);
						BREAK_ERROR("Expected rf0 through rf31", rf_names);
					}
					else if (v32_qpu_magic_waddr_from_name(
					             currentChar, &waddr,
					             &currentChar))
					{
						*(output->magic_write) = TRUE;
                        *(output->waddr) = waddr;              //debugdebug
					}
					else
						parsedSuccessfully = FALSE;
					BREAK_ERROR("Expected rf0 through rf31 or waddr", waddr_names);

					parsedSuccessfully = v3d_qpu_value_from_name_list(
					    currentChar, pack_names, V3D_ARRAY_SIZE(pack_names),
					    /*dotOptional=*/TRUE, (v3d_uint32*)output->output_pack,
					    &currentChar);
					BREAK_ERROR("Invalid pack operation", pack_names);
				}

                for (int src = 0; src < num_src; ++src)
				{
					struct v3d_qpu_input* srcInput = output->inputs[src];
					if (!v3d_qpu_skip_whitespace_comments(&currentChar))
					{
						parsedSuccessfully = FALSE;
						BREAK_ERROR_NO_HINTS(
						    "Expected source operand rf0 through rf31, accumulator register r0-r5, "
						    "or small immediate");
					}

					if ((has_dst && src == 0) || src > 0)
					{
						if (currentChar[0] != ',')
						{
							parsedSuccessfully = FALSE;
							BREAK_ERROR_NO_HINTS("Expected , before source operand");
						}
						else
						{
							++currentChar;
							if (!v3d_qpu_skip_whitespace_comments(&currentChar))
							{
								parsedSuccessfully = FALSE;
								BREAK_ERROR_NO_HINTS(
								    "Expected source operand rf0 through rf31, accumulator "
								    "register r0-r5, or small immediate");
							}
						}
					}

					// (todo Pi 5) V3D 71+ support (V3D_QPU_ADD_A input)
					enum v3d_qpu_assemble_raddr_result raddrResult = v3d33_qpu_assemble_raddr(
					    &args->instruction, &srcInput->input.mux, currentChar, &currentChar);
					parsedSuccessfully = raddrResult == v3d_qpu_assemble_raddr_result_success;
					const char* raddrError = NULL;
					const char** raddrList = NULL;
					int raddrListLength = 0;
					switch (raddrResult)
					{
						case v3d_qpu_assemble_raddr_result_invalid_register_file:
							raddrError = "Unrecognized register file";
							raddrList = rf_names;
							raddrListLength = V3D_ARRAY_SIZE(rf_names);
							break;
						case v3d_qpu_assemble_raddr_result_invalid_accumulator_register:
							raddrError = "Unrecognized accumulator register";
							raddrList = accumulator_register_names;
							raddrListLength = V3D_ARRAY_SIZE(accumulator_register_names);
							break;
						case v3d_qpu_assemble_raddr_result_invalid_small_immediate:
							raddrError = "Unrecognized small immediate";
							raddrList = small_immediates_names;
							raddrListLength = V3D_ARRAY_SIZE(small_immediates_names);
							break;
						case v3d_qpu_assemble_raddr_result_no_raddr_space_too_many_immediates:
							raddrError =
							    "Too many small immediates. Only one small immediate may be "
							    "specified per instruction";
							break;
						case v3d_qpu_assemble_raddr_result_no_raddr_space:
							raddrError =
							    "Too many unique register files (plus small immediate) specified. "
							    "Only two unique raddrs (two register files or one register file "
							    "and one small immediate) may be specified per instruction";
							break;
						default:
							raddrError = "Unspecified error with raddr parsing";
							break;
					}
					BREAK_ERROR_HINT_SIZE(raddrError, raddrList, raddrListLength);

					parsedSuccessfully = v3d_qpu_value_from_name_list(
					    currentChar, unpack_names, V3D_ARRAY_SIZE(unpack_names),
					    /*dotOptional=*/TRUE, (v3d_uint32*)&srcInput->unpack,
					    &currentChar);
					BREAK_ERROR("Invalid unpack operation", unpack_names);
				}
			}

			if (!parsedSuccessfully)
				break;

			// Finally, parse (optional) signals
			v3d_bool sigWithAddressSpecified = FALSE;
			while (v3d_qpu_skip_whitespace_comments(&currentChar))
			{
				if (currentChar[0] != ';')
				{
					parsedSuccessfully = FALSE;
					BREAK_ERROR_NO_HINTS("Expected ';' before start of signal");
				}
				++currentChar;

				if (!v3d_qpu_skip_whitespace_comments(&currentChar))
				{
					// Finished with the line. We'll allow dangling ; after mul.
					break;
				}

				v3d_bool sigTakesAddress = FALSE;
				parsedSuccessfully = v3d_qpu_assemble_signal(&args->instruction.sig, &sigTakesAddress,
															 currentChar, &currentChar);
				BREAK_ERROR("Unrecognized signal name", sig_names);

				if (sigTakesAddress)
				{
					if (sigWithAddressSpecified)
					{
						parsedSuccessfully = FALSE;
						BREAK_ERROR_NO_HINTS(
						    "Too many signals with addresses specified. Only one signal with "
						    "address may be specified per instruction");
					}
					sigWithAddressSpecified = TRUE;
				}

				// Optional sig_addr
				if (currentChar[0] == '.')
				{
					if (!sigTakesAddress)
					{
						parsedSuccessfully = FALSE;
						BREAK_ERROR(
							"Signal does not support an address. Check for a signal variant which does "
							"take an address",
							sig_names);
					}
					++currentChar;
					if (currentChar[0] == 'r' && currentChar[1] == 'f')
					{
						parsedSuccessfully = v3d_assemble_parse_register_file(
							currentChar, &args->instruction.sig_addr, &currentChar);
						BREAK_ERROR("Expected rf0 through rf31", rf_names);
					}
					else
					{
						enum v3d_qpu_waddr waddr = 0;
						parsedSuccessfully =
							v32_qpu_magic_waddr_from_name(currentChar, &waddr, &currentChar);
						args->instruction.sig_addr = waddr;
						args->instruction.sig_magic = TRUE;
					}
					BREAK_ERROR("Expected rf0 through rf31 or waddr", waddr_names);
				}

				/* if (v3d_qpu_skip_whitespace_comments(&currentChar)) */
				/* { */
				/* 	parsedSuccessfully = FALSE; */
				/* 	BREAK_ERROR("Unexpected text at end of instruction; only one instruction per line allowed", NULL); */
				/* } */
			}
			// Finished with the instruction
			break;
		}
	}
#undef BREAK_ERROR
#undef BREAK_ERROR_HINT_SIZE

	if (errorHintList && numErrorHints)
	{
		args->hintAvailable = errorHintList;
		args->numHints = numErrorHints;
	}
	if (!parsedSuccessfully)
		return 0;
	return currentChar - args->assembly;
}

// >> qpu_validate.c

struct v3d_qpu_validate_state {
	const struct v3d_device_info *devinfo;
	const struct v3d_qpu_instr *last;
	int ip;
	int last_sfu_write;
	int last_branch_ip;
	int last_thrsw_ip;
	int first_tlb_z_write;

	/* Set when we've found the last-THRSW signal, or if we were started
	 * in single-segment mode.
	 */
	v3d_bool last_thrsw_found;

	/* Set when we've found the THRSW after the last THRSW */
	v3d_bool thrend_found;

	int thrsw_count;

	// Include message for ease of use as well as value if e.g. an editor wants to provide helpful
	// fixups or suggestions.
	const char* errorMessage;
	enum v3d_qpu_validate_error error;
};

static void fail_instr(struct v3d_qpu_validate_state* state, enum v3d_qpu_validate_error error,
                       const char* msg)
{
	state->errorMessage = msg;
	state->error = error;
	/* struct v3d_compile *c = state->c; */

	/* fprintf(stderr, "v3d_qpu_validate at ip %d: %s:\n", state->ip, msg); */

	/* int dump_ip = 0; */
	/* vir_for_each_inst_inorder(inst, c) { */
	/*         v3d_qpu_dump(c->devinfo, &inst->qpu); */

	/*         if (dump_ip++ == state->ip) */
	/*                 fprintf(stderr, " *** ERROR ***"); */

	/*         fprintf(stderr, "\n"); */
	/* } */

	/* fprintf(stderr, "\n"); */
}

static v3d_bool
in_branch_delay_slots(struct v3d_qpu_validate_state *state)
{
	return (state->ip - state->last_branch_ip) < 3;
}

static v3d_bool
in_thrsw_delay_slots(struct v3d_qpu_validate_state *state)
{
	return (state->ip - state->last_thrsw_ip) < 3;
}

/* static v3d_bool */
/* qpu_magic_waddr_matches(const struct v3d_qpu_instr *inst, */
/*                         v3d_bool (*predicate)(enum v3d_qpu_waddr waddr)) */
/* { */
/* 	if (inst->type == V3D_QPU_INSTR_TYPE_ALU) */
/* 		return FALSE; */

/* 	if (inst->instr.alu.add.op != V3D_QPU_A_NOP && */
/* 		inst->instr.alu.add.magic_write && */
/* 		predicate(inst->instr.alu.add.waddr)) */
/* 		return TRUE; */

/* 	if (inst->instr.alu.mul.op != V3D_QPU_M_NOP && */
/* 		inst->instr.alu.mul.magic_write && */
/* 		predicate(inst->instr.alu.mul.waddr)) */
/* 		return TRUE; */

/* 	return FALSE; */
/* } */

// Returns whether the instruction is valid relative to the current state
static v3d_bool
qpu_validate_inst(struct v3d_qpu_validate_state *state, const struct v3d_qpu_instr* inst)
{
	const struct v3d_device_info *devinfo = state->devinfo;

	// (todo robustness) Find a way to check for tlb z writes using just the instruction rather
	// than the qinst.
	/* if (qinst->is_tlb_z_write && state->ip < state->first_tlb_z_write) */
	/*         state->first_tlb_z_write = state->ip; */

	if (inst->type == V3D_QPU_INSTR_TYPE_BRANCH && state->first_tlb_z_write >= 0 &&
	    state->ip > state->first_tlb_z_write && inst->instr.branch.msfign != V3D_QPU_MSFIGN_NONE &&
	    inst->instr.branch.cond != V3D_QPU_BRANCH_COND_ALWAYS &&
	    inst->instr.branch.cond != V3D_QPU_BRANCH_COND_A0 && inst->instr.branch.cond != V3D_QPU_BRANCH_COND_NA0)
	{
		fail_instr(state, V3D_QPU_VALIDATE_ERROR_IMPLICIT_BRANCH_MSF_READ_AFTER_TLB_Z_WRITE,
		           "Implicit branch MSF read after TLB Z write");
		return FALSE;
	}

	if (inst->type != V3D_QPU_INSTR_TYPE_ALU)
		return TRUE;

	if (inst->instr.alu.add.op == V3D_QPU_A_SETMSF && state->first_tlb_z_write >= 0 &&
	    state->ip > state->first_tlb_z_write)
	{
		fail_instr(state, V3D_QPU_VALIDATE_ERROR_SETMSF_AFTER_TLB_Z_WRITE,
		           "SETMSF after TLB Z write");
		return FALSE;
	}

	if (state->first_tlb_z_write >= 0 && state->ip > state->first_tlb_z_write &&
	    inst->instr.alu.add.op == V3D_QPU_A_MSF)
	{
		fail_instr(state, V3D_QPU_VALIDATE_ERROR_MSF_READ_AFTER_TLB_Z_WRITE,
		           "MSF read after TLB Z write");
		return FALSE;
	}

	if (devinfo->ver < 71)
	{
		if (inst->sig.small_imm_a || inst->sig.small_imm_c || inst->sig.small_imm_d)
		{
			fail_instr(state, V3D_QPU_VALIDATE_ERROR_SMALL_IMM_A_C_D_ADDED_AFTER_V3D_7_1,
			           "small imm a/c/d added after V3D 7.1");
			return FALSE;
		}
	}
	else
	{
		if ((inst->sig.small_imm_a || inst->sig.small_imm_b) &&
		    !(inst->type == V3D_QPU_INSTR_TYPE_ALU && inst->instr.alu.add.op != V3D_QPU_A_NOP))
		{
			fail_instr(state, V3D_QPU_VALIDATE_ERROR_SMALL_IMM_A_B_USED_BUT_NO_ADD_INST,
			           "small imm a/b used but no ADD inst");
			return FALSE;
		}
		if ((inst->sig.small_imm_c || inst->sig.small_imm_d) &&
		    !(inst->type == V3D_QPU_INSTR_TYPE_ALU && inst->instr.alu.mul.op != V3D_QPU_M_NOP))
		{
			fail_instr(state, V3D_QPU_VALIDATE_ERROR_SMALL_IMM_C_D_USED_BUT_NO_MUL_INST,
			           "small imm c/d used but no MUL inst");
			return FALSE;
		}
		if (inst->sig.small_imm_a + inst->sig.small_imm_b + inst->sig.small_imm_c +
		        inst->sig.small_imm_d >
		    1)
		{
			fail_instr(state, V3D_QPU_VALIDATE_ERROR_MAX_ONE_SMALL_IMMEDIATE_PER_INSTRUCTION,
			           "only one small immediate can be enabled per instruction");
			return FALSE;
		}
	}

	/* LDVARY writes r5 two instructions later and LDUNIF writes
	 * r5 one instruction later, which is illegal to have
	 * together.
	 */
	if (state->last && state->last->sig.ldvary && (inst->sig.ldunif || inst->sig.ldunifa))
	{
		fail_instr(state, V3D_QPU_VALIDATE_ERROR_LDUNIF_AFTER_A_LDVARY, "LDUNIF after a LDVARY");
		return FALSE;
	}

	/* GFXH-1633 (fixed since V3D 4.2.14, which is Rpi4)
	 *
	 * FIXME: This would not check correctly for V3D 4.2 versions lower
	 * than V3D 4.2.14, but that is not a real issue because the simulator
	 * will still catch this, and we are not really targeting any such
	 * versions anyway.
	 */
	if (devinfo->ver < 42)
	{
		v3d_bool last_reads_ldunif =
		    (state->last && (state->last->sig.ldunif || state->last->sig.ldunifrf));
		v3d_bool last_reads_ldunifa =
		    (state->last && (state->last->sig.ldunifa || state->last->sig.ldunifarf));
		v3d_bool reads_ldunif = inst->sig.ldunif || inst->sig.ldunifrf;
		v3d_bool reads_ldunifa = inst->sig.ldunifa || inst->sig.ldunifarf;
		if ((last_reads_ldunif && reads_ldunifa) || (last_reads_ldunifa && reads_ldunif))
		{
			fail_instr(state, V3D_QPU_VALIDATE_ERROR_LDUNIF_AND_LDUNIFA_CANT_BE_NEXT_TO_EACH_OTHER,
			           "LDUNIF and LDUNIFA can't be next to each other");
			return FALSE;
		}
	}

	int tmu_writes = 0;
	int sfu_writes = 0;
	int vpm_writes = 0;
	int tlb_writes = 0;
	int tsy_writes = 0;

	if (inst->instr.alu.add.op != V3D_QPU_A_NOP) {
		if (inst->instr.alu.add.magic_write) {
			if (v3d_qpu_magic_waddr_is_tmu(devinfo,
										   inst->instr.alu.add.waddr)) {
				tmu_writes++;
			}
			if (v3d_qpu_magic_waddr_is_sfu(inst->instr.alu.add.waddr))
				sfu_writes++;
			if (v3d_qpu_magic_waddr_is_vpm(inst->instr.alu.add.waddr))
				vpm_writes++;
			if (v3d_qpu_magic_waddr_is_tlb(inst->instr.alu.add.waddr))
				tlb_writes++;
			if (v3d_qpu_magic_waddr_is_tsy(inst->instr.alu.add.waddr))
				tsy_writes++;
		}
	}

	if (inst->instr.alu.mul.op != V3D_QPU_M_NOP) {
		if (inst->instr.alu.mul.magic_write) {
			if (v3d_qpu_magic_waddr_is_tmu(devinfo,
										   inst->instr.alu.mul.waddr)) {
				tmu_writes++;
			}
			if (v3d_qpu_magic_waddr_is_sfu(inst->instr.alu.mul.waddr))
				sfu_writes++;
			if (v3d_qpu_magic_waddr_is_vpm(inst->instr.alu.mul.waddr))
				vpm_writes++;
			if (v3d_qpu_magic_waddr_is_tlb(inst->instr.alu.mul.waddr))
				tlb_writes++;
			if (v3d_qpu_magic_waddr_is_tsy(inst->instr.alu.mul.waddr))
				tsy_writes++;
		}
	}

	if (in_thrsw_delay_slots(state))
	{
		/* There's no way you want to start SFU during the THRSW delay
		 * slots, since the result would land in the other thread.
		 */
		if (sfu_writes)
		{
			fail_instr(state, V3D_QPU_VALIDATE_ERROR_SFU_WRITE_STARTED_DURING_THRSW_DELAY_SLOTS,
			           "SFU write started during THRSW delay slots ");
			return FALSE;
		}

		if (inst->sig.ldvary)
		{
			if (devinfo->ver == 42)
			{
				fail_instr(state, V3D_QPU_VALIDATE_ERROR_LDVARY_DURING_THRSW_DELAY_SLOTS,
				           "LDVARY during THRSW delay slots");
				return FALSE;
			}
			if (devinfo->ver >= 71 && state->ip - state->last_thrsw_ip == 2)
			{
				fail_instr(state, V3D_QPU_VALIDATE_ERROR_LDVARY_IN_2ND_THRSW_DELAY_SLOT,
				           "LDVARY in 2nd THRSW delay slot");
				return FALSE;
			}
		}
	}

	/* (void)qpu_magic_waddr_matches; /\* XXX *\/ */

	/* SFU r4 results come back two instructions later.  No doing
	 * r4 read/writes or other SFU lookups until it's done.
	 */
	if (state->ip - state->last_sfu_write < 2)
	{
		if (v3d_qpu_uses_mux(inst, V3D_QPU_MUX_R4))
		{
			fail_instr(state, V3D_QPU_VALIDATE_ERROR_R4_READ_TOO_SOON_AFTER_SFU,
			           "R4 read too soon after SFU");
			return FALSE;
		}

		if (v3d_qpu_writes_r4(devinfo, inst))
		{
			fail_instr(state, V3D_QPU_VALIDATE_ERROR_R4_WRITE_TOO_SOON_AFTER_SFU,
			           "R4 write too soon after SFU");
			return FALSE;
		}

		if (sfu_writes)
		{
			fail_instr(state, V3D_QPU_VALIDATE_ERROR_SFU_WRITE_TOO_SOON_AFTER_SFU,
			           "SFU write too soon after SFU");
			return FALSE;
		}
	}

	/* XXX: The docs say VPM can happen with the others, but the simulator
	 * disagrees.
	 */
	if (tmu_writes + sfu_writes + vpm_writes + tlb_writes + tsy_writes +
	        (devinfo->ver == 42 ? inst->sig.ldtmu : 0) + inst->sig.ldtlb + inst->sig.ldvpm +
	        inst->sig.ldtlbu >
	    1)
	{
		fail_instr(state, V3D_QPU_VALIDATE_ERROR_ONLY_ONE_OF_TMU_SFU_TSY_TLB_READ_VPM_ALLOWED,
		           "Only one of [TMU, SFU, TSY, TLB read, VPM] allowed");
		return FALSE;
	}

	if (sfu_writes)
		state->last_sfu_write = state->ip;

	if (inst->sig.thrsw)
	{
		if (in_branch_delay_slots(state))
		{
			fail_instr(state, V3D_QPU_VALIDATE_ERROR_THRSW_IN_A_BRANCH_DELAY_SLOT,
			           "THRSW in a branch delay slot.");
			return FALSE;
		}

		if (state->last_thrsw_found)
			state->thrend_found = TRUE;

		if (state->last_thrsw_ip == state->ip - 1)
		{
			/* If it's the second THRSW in a row, then it's just a
			 * last-thrsw signal.
			 */
			if (state->last_thrsw_found)
			{
				fail_instr(state, V3D_QPU_VALIDATE_ERROR_TWO_LAST_THRSW_SIGNALS,
				           "Two last-THRSW signals");
				return FALSE;
			}
			state->last_thrsw_found = TRUE;
		}
		else
		{
			if (in_thrsw_delay_slots(state))
			{
				fail_instr(state, V3D_QPU_VALIDATE_ERROR_THRSW_TOO_CLOSE_TO_ANOTHER_THRSW,
				           "THRSW too close to another THRSW.");
				return FALSE;
			}
			state->thrsw_count++;
			state->last_thrsw_ip = state->ip;
		}
	}

	if (state->thrend_found && state->last_thrsw_ip - state->ip <= 2 &&
	    inst->type == V3D_QPU_INSTR_TYPE_ALU)
	{
		if ((inst->instr.alu.add.op != V3D_QPU_A_NOP && !inst->instr.alu.add.magic_write))
		{
			if (devinfo->ver == 42)
			{
				fail_instr(state, V3D_QPU_VALIDATE_ERROR_RF_WRITE_AFTER_THREND,
				           "RF write after THREND");
				return FALSE;
			}
			else if (devinfo->ver >= 71)
			{
				if (state->last_thrsw_ip - state->ip == 0)
				{
					fail_instr(state, V3D_QPU_VALIDATE_ERROR_ADD_RF_WRITE_AT_THREND,
					           "ADD RF write at THREND");
					return FALSE;
				}
				if (inst->instr.alu.add.waddr == 2 || inst->instr.alu.add.waddr == 3)
				{
					fail_instr(state, V3D_QPU_VALIDATE_ERROR_RF2_3_WRITE_AFTER_THREND,
					           "RF2-3 write after THREND");
					return FALSE;
				}
			}
		}

		if ((inst->instr.alu.mul.op != V3D_QPU_M_NOP && !inst->instr.alu.mul.magic_write))
		{
			if (devinfo->ver == 42)
			{
				fail_instr(state, V3D_QPU_VALIDATE_ERROR_RF_WRITE_AFTER_THREND,
				           "RF write after THREND");
				return FALSE;
			}
			else if (devinfo->ver >= 71)
			{
				if (state->last_thrsw_ip - state->ip == 0)
				{
					fail_instr(state, V3D_QPU_VALIDATE_ERROR_MUL_RF_WRITE_AT_THREND,
					           "MUL RF write at THREND");
					return FALSE;
				}

				if (inst->instr.alu.mul.waddr == 2 || inst->instr.alu.mul.waddr == 3)
				{
					fail_instr(state, V3D_QPU_VALIDATE_ERROR_RF2_3_WRITE_AFTER_THREND,
					           "RF2-3 write after THREND");
					return FALSE;
				}
			}
		}

		if (v3d_qpu_sig_writes_address(devinfo, &inst->sig) && !inst->sig_magic)
		{
			if (devinfo->ver == 42)
			{
				fail_instr(state, V3D_QPU_VALIDATE_ERROR_RF_WRITE_AFTER_THREND,
				           "RF write after THREND");
				return FALSE;
			}
			else if (devinfo->ver >= 71 && (inst->sig_addr == 2 || inst->sig_addr == 3))
			{
				fail_instr(state, V3D_QPU_VALIDATE_ERROR_RF2_3_WRITE_AFTER_THREND,
				           "RF2-3 write after THREND");
				return FALSE;
			}
		}

		/* GFXH-1625: No TMUWT in the last instruction */
		if (state->last_thrsw_ip - state->ip == 2 && inst->instr.alu.add.op == V3D_QPU_A_TMUWT)
		{
			fail_instr(state, V3D_QPU_VALIDATE_ERROR_TMUWT_IN_LAST_INSTRUCTION,
			           "TMUWT in last instruction");
			return FALSE;
		}
	}

	if (inst->type == V3D_QPU_INSTR_TYPE_BRANCH)
	{
		if (in_branch_delay_slots(state))
		{
			fail_instr(state, V3D_QPU_VALIDATE_ERROR_BRANCH_IN_A_BRANCH_DELAY_SLOT,
			           "branch in a branch delay slot.");
			return FALSE;
		}
		if (in_thrsw_delay_slots(state))
		{
			fail_instr(state, V3D_QPU_VALIDATE_ERROR_BRANCH_IN_A_THRSW_DELAY_SLOT,
			           "branch in a THRSW delay slot.");
			return FALSE;
		}
		state->last_branch_ip = state->ip;
	}
	return TRUE;
}

v3d_bool v3d_qpu_validate(const struct v3d_device_info* devinfo, struct v3d_qpu_instr* instructions,
                          int numInstructions, struct v3d_qpu_validate_result* results)
{
	struct v3d_qpu_validate_state state = {
	    .devinfo = devinfo,
	    .last_sfu_write = -10,
	    .last_thrsw_ip = -10,
	    .last_branch_ip = -10,
	    .first_tlb_z_write = numInstructions + 1 /*INT_MAX*/,
	    .ip = 0,

	    // (todo) Not sure what to put here, since it relies on there having been a compile phase
	    /* .last_thrsw_found = !c->last_thrsw, */
		.last_thrsw_found = FALSE,
	};

	/* // Find the last thrsw. I am not sure this is correct. */
	/* for (int instructionIndex = numInstructions - 1; instructionIndex >= 0; --instructionIndex) */
	/* { */
	/* 	if (instructions[instructionIndex].sig.thrsw) */
	/* 	{ */
	/* 		state.last_thrsw_found = TRUE; */
	/* 		break; */
	/* 	} */
	/* } */

	v3d_bool hasError = FALSE;
	for (int instructionIndex = 0; instructionIndex < numInstructions; ++instructionIndex)
	{
		if (!qpu_validate_inst(&state, &instructions[instructionIndex]))
		{
			results->errorInstructionIndex = instructionIndex;
			hasError = TRUE;
			break;
		}

		state.last = &instructions[instructionIndex];
		state.ip++;
	}

	if (!hasError && (state.thrsw_count > 1 && !state.last_thrsw_found))
	{
		fail_instr(&state, V3D_QPU_VALIDATE_ERROR_THREAD_SWITCH_FOUND_WITHOUT_LAST_THRSW_IN_PROGRAM,
		           "thread switch found without last-THRSW in program");
		results->errorInstructionIndex = numInstructions - 1;
		hasError = TRUE;
	}

	// (todo) Figure out this thrsw business
	/* if (!hasError && !state.thrend_found) */
	/* { */
	/* 	fail_instr(&state, V3D_QPU_VALIDATE_ERROR_NO_PROGRAM_END_THRSW_FOUND, */
	/* 	           "No program-end THRSW found"); */
	/* 	results->errorInstructionIndex = numInstructions - 1; */
	/* 	hasError = TRUE; */
	/* } */

	if (!hasError && (numInstructions < 3 || (instructions[numInstructions - 1].sig.thrsw ||
	                                          instructions[numInstructions - 2].sig.thrsw)))
	{
		fail_instr(&state, V3D_QPU_VALIDATE_ERROR_NO_PROGRAM_END_THRSW_DELAY_SLOTS,
		           "THRSW needs two delay slot instructions");
		results->errorInstructionIndex = numInstructions - 1;
		hasError = TRUE;
	}

	if (hasError)
	{
		results->errorMessage = state.errorMessage;
		results->error = state.error;
		return FALSE;
	}

	return TRUE;
}

#endif // V3D_ASSEMBLER_IMPLEMENTATION

#endif // V3DASSEMBLER_H

/*
 * (C) 2025-2026 Dennis van der Boon
 */

#ifndef _MGLV3D_LVOCALL_COMPAT_H
#define _MGLV3D_LVOCALL_COMPAT_H

/*
 * Shared vbcc/GCC compatibility macros for two AmigaOS ABI patterns
 * used by devicetree_protos.h and v3d_debug.c:
 *
 *  - MGLV3D_LP1/LP1NR/LP2: call a library/resource function at a fixed
 *    negative jump-table offset from a base pointer in a6, with its
 *    further arguments in specific registers (the standard AmigaOS
 *    "LVO" convention). vbcc has a native extension for this
 *    (inline-assembly-function bodies + __reg() parameter placement);
 *    GCC has no equivalent syntax, so its branch follows the same
 *    idiom m68k-amigaos-gcc's own bundled NDK headers use
 *    (ndk-include/inline/macros.h's LP1/LP1NR/LP2: local register
 *    variables pinned via __asm("regname") + an inline asm
 *    "jsr a6@(-N:W)", with d1/a0/a1 (and d0 for the NR case) declared
 *    as dummy "=r" outputs purely to tell GCC they're clobbered by the
 *    call -- copied from that established pattern, not reinvented.
 *
 *  - MGLV3D_REG: tag one parameter of a normal function definition as
 *    living in a specific register, for functions AmigaOS calls back
 *    directly via a fixed-register convention (e.g. RawDoFmt's
 *    PutChProc hook). Same idea as SDI_compiler.h's REG() macro --
 *    vbcc places __reg() before the argument, GCC's __asm() goes after.
 *
 * Both call sites keep the exact same macro invocation under either
 * compiler; only this header's internals differ.
 *
 * rt must not be const-qualified in the LP1/LP2 macros: the GCC branch
 * writes the result into a register variable, and a const-qualified
 * local can't be an asm output. Const on a by-value return type has no
 * observable effect in C (the returned rvalue was never assignable
 * anyway), so callers that had "CONST FOO" in the original vbcc-only
 * declaration should just drop the CONST here.
 */

#ifdef __VBCC__

#define MGLV3D_REG(reg, arg) __reg(#reg) arg

#define MGLV3D_LP1(rt, name, offs, t1, r1) \
	rt name(__reg("a6") void *, __reg(#r1) t1)="\tjsr\t-"#offs"(a6)";

#define MGLV3D_LP1NR(name, offs, t1, r1) \
	void name(__reg("a6") void *, __reg(#r1) t1)="\tjsr\t-"#offs"(a6)";

#define MGLV3D_LP2(rt, name, offs, t1, r1, t2, r2) \
	rt name(__reg("a6") void *, __reg(#r1) t1, __reg(#r2) t2)="\tjsr\t-"#offs"(a6)";

#elif defined(__GNUC__)

#define MGLV3D_REG(reg, arg) arg __asm(#reg)

#define MGLV3D_LP1(rt, name, offs, t1, r1) \
static __inline__ rt name(void *_bn, t1 _v1) \
{ \
	register void *const _b __asm("a6") = _bn; \
	register t1 _n1 __asm(#r1) = _v1; \
	register rt _re __asm("d0"); \
	register int _d1 __asm("d1"); \
	register int _a0 __asm("a0"); \
	register int _a1 __asm("a1"); \
	__asm volatile ("jsr a6@(-"#offs":W)" \
		: "=r"(_re), "=r"(_d1), "=r"(_a0), "=r"(_a1) \
		: "r"(_b), "rf"(_n1) \
		: "fp0", "fp1", "cc", "memory"); \
	return _re; \
}

#define MGLV3D_LP1NR(name, offs, t1, r1) \
static __inline__ void name(void *_bn, t1 _v1) \
{ \
	register void *const _b __asm("a6") = _bn; \
	register t1 _n1 __asm(#r1) = _v1; \
	register int _d0 __asm("d0"); \
	register int _d1 __asm("d1"); \
	register int _a0 __asm("a0"); \
	register int _a1 __asm("a1"); \
	__asm volatile ("jsr a6@(-"#offs":W)" \
		: "=r"(_d0), "=r"(_d1), "=r"(_a0), "=r"(_a1) \
		: "r"(_b), "rf"(_n1) \
		: "fp0", "fp1", "cc", "memory"); \
}

#define MGLV3D_LP2(rt, name, offs, t1, r1, t2, r2) \
static __inline__ rt name(void *_bn, t1 _v1, t2 _v2) \
{ \
	register void *const _b __asm("a6") = _bn; \
	register t1 _n1 __asm(#r1) = _v1; \
	register t2 _n2 __asm(#r2) = _v2; \
	register rt _re __asm("d0"); \
	register int _d1 __asm("d1"); \
	register int _a0 __asm("a0"); \
	register int _a1 __asm("a1"); \
	__asm volatile ("jsr a6@(-"#offs":W)" \
		: "=r"(_re), "=r"(_d1), "=r"(_a0), "=r"(_a1) \
		: "r"(_b), "rf"(_n1), "rf"(_n2) \
		: "fp0", "fp1", "cc", "memory"); \
	return _re; \
}

#else
#error "lvocall_compat.h needs a __VBCC__ or __GNUC__ branch for this compiler"
#endif

#endif /* !_MGLV3D_LVOCALL_COMPAT_H */

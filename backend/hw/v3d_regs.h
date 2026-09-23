/*
 * (C) 2025-2026 Dennis van der Boon
 */

#ifndef V3D_REGS_H
#define V3D_REGS_H

#define V3D_HUB_BASE (0xF2000000 + 0xC00000)
#define V3D_BASE     (0xF2000000 + 0xC04000)
#define PM_BASE      (0xF2000000 + 0x100000)
#define CM_BASE      (0xF2000000 + 0x101000)
#define ASB_BASE     (0xF2000000 + 0xC11000)
#define PASSWORD     0x0000005A

#define V3D_HUB_IDENT1_TVER_SHIFT  0
#define V3D_HUB_IDENT1_TVER_MASK   15
#define V3D_HUB_IDENT1_REV_SHIFT   4
#define V3D_HUB_IDENT1_REV_MASK    15

#define V3D_HUB_INT_STS *(volatile uint32_t*)(V3D_HUB_BASE+0x00050)
#define V3D_HUB_INT_CLR *(volatile uint32_t*)(V3D_HUB_BASE+0x00058)
#define V3D_CTL_INT_STS *(volatile uint32_t*)(V3D_BASE+0x00050)
#define V3D_CTL_INT_CLR *(volatile uint32_t*)(V3D_BASE+0x00058)

/* Bits within V3D_CTL_INT_STS/CLR -- from PoC/linuxregs.c:239-248 (Linux kernel v3d driver register
 * header; its INT_STS/CLR offsets match this file's, 0x50/0x58 off V3D_BASE). Bit order per the Linux
 * DRM v3d register header (drivers/gpu/drm/v3d/v3d_regs.h, rpi-6.6.y -- the V3D 4.x driver, NOT vc4):
 * FRDONE 0, FLDONE 1, OUTOMEM 2, SPILLUSE 3, TRFB 4, GMPV 5, PCTR 6, CSDDONE 7, QPU_MASK 27:16.
 * Add others from linuxregs.c as needed, don't guess. */
#define V3D_INT_FRDONE  0x00000001 /* BIT(0) -- render (frame) done */
#define V3D_INT_FLDONE  0x00000002 /* BIT(1) -- binning (frame list) done */
#define V3D_INT_OUTOMEM 0x00000004 /* BIT(2) -- binner ran out of tile allocation memory; see the spill handling in v3d_wait_binning_timeout (v3d_submit_timeout.c) */
#define V3D_INT_SPILLUSE 0x00000008 /* BIT(3) -- binner started using the spill/overflow memory it was given */

#define PM_GRAFX_POW      0x01000000
#define PM_GRAFX_POWOK    0x02000000
#define PM_GRAFX_ISPOW    0x04000000
#define PM_GRAFX_MEMREP   0x08000000
#define PM_GRAFX_MRDONE   0x10000000
#define PM_GRAFX_ISFUNC   0x20000000
#define PM_V3DRSTN        0x40000000

#define ASB_REQ_STOP      0x01000000
#define ASB_ACK           0x02000000

#define INRUSH_MASK       0x00600000

#define V3D_CTNCS_CTERR   0x08000000
#define V3D_CTNCS_CTRSTA  0x00800000
#define V3D_T0QTS_ENABLE  0x02000000

#define V3D_BFC_FLUSH_COUNT_MASK 0xFF
#define V3D_RFC_FRAME_COUNT_MASK 0xFF

/* Pre-swapped for the PiStorm bridge: register writes go from the 68k
 * big-endian CPU to the little-endian V3D/ARM side, and v3d_hw.c writes
 * V3D_L2TCACTL without LE32, so 0x01000000 lands as bit 0 (L2TFLS in
 * PoC/linuxregs.c's register layout). This value is right as written; do
 * not change it to (1<<0) -- with 0x00000001 textures do not show up at
 * all. */
#define V3D_L2TCACTL_L2TFLS      0x01000000
#define V3D_L2TCACTL_FLM_FLUSH   0

#define PM_IMAGE       *(volatile uint32_t*)( PM_BASE+0x108)

#define PM_GRAFX       *(volatile uint32_t*)( PM_BASE+0x0010C)
#define ASB_V3D_S_CTRL *(volatile uint32_t*)(ASB_BASE+0x00008)
#define ASB_V3D_M_CTRL *(volatile uint32_t*)(ASB_BASE+0x0000C)
#define CM_V3DCTL      *(volatile uint32_t*)( CM_BASE+0x00038)
#define CM_V3DDIV      *(volatile uint32_t*)( CM_BASE+0x0003C)
#define V3D_HUB_IDENT0 *(volatile uint32_t*)(V3D_HUB_BASE+0x00008)
#define V3D_HUB_IDENT1 *(volatile uint32_t*)(V3D_HUB_BASE+0x0000C)
#define V3D_HUB_AXICFG *(volatile uint32_t*)(V3D_HUB_BASE)

#define V3D_IDENT0	*(volatile uint32_t*)(V3D_BASE+0x00000)	// V3D Identification 0 (V3D block identity)
#define V3D_IDENT1	*(volatile uint32_t*)(V3D_BASE+0x00004)	// V3D Identification 1 (V3D Configuration A)
#define V3D_IDENT2	*(volatile uint32_t*)(V3D_BASE+0x00008)	// V3D Identification 2 (V3D Configuration B)
#define V3D_SCRATCH	*(volatile uint32_t*)(V3D_BASE+0x00010)	// Scratch Register
#define V3D_L2CACTL	*(volatile uint32_t*)(V3D_BASE+0x00020)	// L2 Cache Control
#define V3D_SLCACTL	*(volatile uint32_t*)(V3D_BASE+0x00024)	// Slices Cache Control
#define V3D_L2TCACTL  *(volatile uint32_t*)(V3D_BASE+0x00030)
#define V3D_L2TFLSTA  *(volatile uint32_t*)(V3D_BASE+0x00034)
#define V3D_L2TFLEND  *(volatile uint32_t*)(V3D_BASE+0x00038)

#define V3D_CT0CS	*(volatile uint32_t*)(V3D_BASE+0x00100)	// Control List Executor Thread 0 Control and Status.
#define V3D_CT1CS	*(volatile uint32_t*)(V3D_BASE+0x00104)	// Control List Executor Thread 1 Control and Status.
#define V3D_CT0EA	*(volatile uint32_t*)(V3D_BASE+0x00108)	// Control List Executor Thread 0 End Address.
#define V3D_CT1EA	*(volatile uint32_t*)(V3D_BASE+0x0010c)	// Control List Executor Thread 1 End Address.
#define V3D_CT0CA	*(volatile uint32_t*)(V3D_BASE+0x00110)	// Control List Executor Thread 0 Current Address.
#define V3D_CT1CA	*(volatile uint32_t*)(V3D_BASE+0x00114)	// Control List Executor Thread 1 Current Address.
#define V3D_CT00RA0	*(volatile uint32_t*)(V3D_BASE+0x00118)	// Control List Executor Thread 0 Return Address.
#define V3D_CT01RA0	*(volatile uint32_t*)(V3D_BASE+0x0011c)	// Control List Executor Thread 1 Return Address.
#define V3D_CT0LC	*(volatile uint32_t*)(V3D_BASE+0x00120)	// Control List Executor Thread 0 List Counter
#define V3D_CT1LC	*(volatile uint32_t*)(V3D_BASE+0x00124)	// Control List Executor Thread 1 List Counter
#define V3D_CT0PC	*(volatile uint32_t*)(V3D_BASE+0x00128)	// Control List Executor Thread 0 Primitive List Counter
#define V3D_CT1PC	*(volatile uint32_t*)(V3D_BASE+0x0012c)	// Control List Executor Thread 1 Primitive List Counter
#define V3D_PCS	    *(volatile uint32_t*)(V3D_BASE+0x00130)	// V3D Pipeline Control and Status
#define V3D_BFC	    *(volatile uint32_t*)(V3D_BASE+0x00134)	// Binning Mode Flush Count
#define V3D_RFC	    *(volatile uint32_t*)(V3D_BASE+0x00138)	// Rendering Mode Frame Count
#define V3D_CT0QBA  *(volatile uint32_t*)(V3D_BASE+0x00160)
#define V3D_CT0QEA  *(volatile uint32_t*)(V3D_BASE+0x00168)
#define V3D_CT1QBA  *(volatile uint32_t*)(V3D_BASE+0x00164)
#define V3D_CT1QEA  *(volatile uint32_t*)(V3D_BASE+0x0016C)
#define V3D_CT0QMA  *(volatile uint32_t*)(V3D_BASE+0x00170) // Tile memory address
#define V3D_CT0QMS  *(volatile uint32_t*)(V3D_BASE+0x00174) // Tile memory size
#define V3D_CT0QTS  *(volatile uint32_t*)(V3D_BASE+0x0015C)

#define V3D_BPCA	*(volatile uint32_t*)(V3D_BASE+0x00300)	// Current Address of Binning Memory Pool
#define V3D_BPCS	*(volatile uint32_t*)(V3D_BASE+0x00304)	// Remaining Size of Binning Memory Pool
#define V3D_BPOA	*(volatile uint32_t*)(V3D_BASE+0x00308)	// Address of Overspill Binning Memory Block
#define V3D_BPOS	*(volatile uint32_t*)(V3D_BASE+0x0030c)	// Size of Overspill Binning Memory Block
#define V3D_BXCF	*(volatile uint32_t*)(V3D_BASE+0x00310)	// Binner Debug
#define V3D_SQRSV0	*(volatile uint32_t*)(V3D_BASE+0x00410)	// Reserve QPUs 0-7
#define V3D_SQRSV1	*(volatile uint32_t*)(V3D_BASE+0x00414)	// Reserve QPUs 8-15
#define V3D_SQCNTL	*(volatile uint32_t*)(V3D_BASE+0x00418)	// QPU Scheduler Control
#define V3D_SRQPC	*(volatile uint32_t*)(V3D_BASE+0x00430)	// QPU User Program Request Program Address
#define V3D_SRQUA	*(volatile uint32_t*)(V3D_BASE+0x00434)	// QPU User Program Request Uniforms Address
#define V3D_SRQUL	*(volatile uint32_t*)(V3D_BASE+0x00438)	// QPU User Program Request Uniforms Length
#define V3D_SRQCS	*(volatile uint32_t*)(V3D_BASE+0x0043c)	// QPU User Program Request Control and Status
#define V3D_VPACNTL	*(volatile uint32_t*)(V3D_BASE+0x00500)	// VPM Allocator Control
#define V3D_VPMBASE	*(volatile uint32_t*)(V3D_BASE+0x00504)	// VPM base (user) memory reservation
#define V3D_PCTRC	*(volatile uint32_t*)(V3D_BASE+0x00670)	// Performance Counter Clear
#define V3D_PCTRE	*(volatile uint32_t*)(V3D_BASE+0x00674)	// Performance Counter Enables
#define V3D_PCTR(n) *(volatile uint32_t*)(V3D_BASE+0x00680+(n)*8)
#define V3D_PCTRS(n)*(volatile uint32_t*)(V3D_BASE+0x00684+(n)*8)
#define V3D_PCTR0	*(volatile uint32_t*)(V3D_BASE+0x00680)	// Performance Counter Count 0
#define V3D_PCTRS0	*(volatile uint32_t*)(V3D_BASE+0x00684)	// Performance Counter Mapping 0
#define V3D_PCTR1	*(volatile uint32_t*)(V3D_BASE+0x00688)	// Performance Counter Count 1
#define V3D_PCTRS1	*(volatile uint32_t*)(V3D_BASE+0x0068c)	// Performance Counter Mapping 1
#define V3D_PCTR2	*(volatile uint32_t*)(V3D_BASE+0x00690)	// Performance Counter Count 2
#define V3D_PCTRS2	*(volatile uint32_t*)(V3D_BASE+0x00694)	// Performance Counter Mapping 2
#define V3D_PCTR3	*(volatile uint32_t*)(V3D_BASE+0x00698)	// Performance Counter Count 3
#define V3D_PCTRS3	*(volatile uint32_t*)(V3D_BASE+0x0069c)	// Performance Counter Mapping 3
#define V3D_PCTR4	*(volatile uint32_t*)(V3D_BASE+0x006a0)	// Performance Counter Count 4
#define V3D_PCTRS4	*(volatile uint32_t*)(V3D_BASE+0x006a4)	// Performance Counter Mapping 4
#define V3D_PCTR5	*(volatile uint32_t*)(V3D_BASE+0x006a8)	// Performance Counter Count 5
#define V3D_PCTRS5	*(volatile uint32_t*)(V3D_BASE+0x006ac)	// Performance Counter Mapping 5
#define V3D_PCTR6	*(volatile uint32_t*)(V3D_BASE+0x006b0)	// Performance Counter Count 6
#define V3D_PCTRS6	*(volatile uint32_t*)(V3D_BASE+0x006b4)	// Performance Counter Mapping 6
#define V3D_PCTR7	*(volatile uint32_t*)(V3D_BASE+0x006b8)	// Performance Counter Count 7
#define V3D_PCTRS7	*(volatile uint32_t*)(V3D_BASE+0x006bc)	// Performance Counter Mapping 7
#define V3D_PCTR8	*(volatile uint32_t*)(V3D_BASE+0x006c0)	// Performance Counter Count 8
#define V3D_PCTRS8	*(volatile uint32_t*)(V3D_BASE+0x006c4)	// Performance Counter Mapping 8
#define V3D_PCTR9	*(volatile uint32_t*)(V3D_BASE+0x006c8)	// Performance Counter Count 9
#define V3D_PCTRS9	*(volatile uint32_t*)(V3D_BASE+0x006cc)	// Performance Counter Mapping 9
#define V3D_PCTR10	*(volatile uint32_t*)(V3D_BASE+0x006d0)	// Performance Counter Count 10
#define V3D_PCTRS10	*(volatile uint32_t*)(V3D_BASE+0x006d4)	// Performance Counter Mapping 10
#define V3D_PCTR11	*(volatile uint32_t*)(V3D_BASE+0x006d8)	// Performance Counter Count 11
#define V3D_PCTRS11	*(volatile uint32_t*)(V3D_BASE+0x006dc)	// Performance Counter Mapping 11
#define V3D_PCTR12	*(volatile uint32_t*)(V3D_BASE+0x006e0)	// Performance Counter Count 12
#define V3D_PCTRS12	*(volatile uint32_t*)(V3D_BASE+0x006e4)	// Performance Counter Mapping 12
#define V3D_PCTR13	*(volatile uint32_t*)(V3D_BASE+0x006e8)	// Performance Counter Count 13
#define V3D_PCTRS13	*(volatile uint32_t*)(V3D_BASE+0x006ec)	// Performance Counter Mapping 13
#define V3D_PCTR14	*(volatile uint32_t*)(V3D_BASE+0x006f0)	// Performance Counter Count 14
#define V3D_PCTRS14	*(volatile uint32_t*)(V3D_BASE+0x006f4)	// Performance Counter Mapping 14
#define V3D_PCTR15	*(volatile uint32_t*)(V3D_BASE+0x006f8)	// Performance Counter Count 15
#define V3D_PCTRS15	*(volatile uint32_t*)(V3D_BASE+0x006fc)	// Performance Counter Mapping 15
#define V3D_DBCFG   *(volatile uint32_t*)(V3D_BASE+0x00e00)
#define V3D_DBQITE  *(volatile uint32_t*)(V3D_BASE+0x00e2c)
#define V3D_DBQITC  *(volatile uint32_t*)(V3D_BASE+0x00e30)
#define V3D_DBGE	*(volatile uint32_t*)(V3D_BASE+0x00f00)	// PSE Error Signals
#define V3D_FDBGO	*(volatile uint32_t*)(V3D_BASE+0x00f04)	// FEP Overrun Error Signals
#define V3D_FDBGB	*(volatile uint32_t*)(V3D_BASE+0x00f08)	// FEP Interface Ready and Stall Signals, FEP Busy Signals
#define V3D_FDBGR	*(volatile uint32_t*)(V3D_BASE+0x00f0c)	// FEP Internal Ready Signals
#define V3D_FDBGS	*(volatile uint32_t*)(V3D_BASE+0x00f10)	// FEP Internal Stall Input Signals
#define V3D_ERRSTAT	*(volatile uint32_t*)(V3D_BASE+0x00f20)	// Miscellaneous Error Signals (VPM, VDW, VCD, VCM, L2C)

#define V3D_MMU_CTL      *(volatile uint32_t*)(V3D_BASE+0x01200)
#define V3D_MMU_PTA_BASE *(volatile uint32_t*)(V3D_BASE+0x01204)
#define V3D_MMU_MISS     *(volatile uint32_t*)(V3D_BASE+0x0120C)
#define V3D_MMU_HIT      *(volatile uint32_t*)(V3D_BASE+0x01208)
#endif

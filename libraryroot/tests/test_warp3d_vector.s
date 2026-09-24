	.text
	.balign 4
	/* Fixed-size six-byte vectors; do not let the assembler shorten JMP. */
	.word 0x4ef9
	.long _warp3d_probe_ok
	.space 24
	.globl _warp3d_probe_base
_warp3d_probe_base:
	.space 30
	.word 0x4ef9
	.long _warp3d_probe_bad
_warp3d_probe_ok:
	move.l #0x12345678,d0
	rts
_warp3d_probe_bad:
	move.l #0x87654321,d0
	rts

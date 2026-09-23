	section text,code
	even
	xref _MiniGLBase
	xdef _MiniGLGetDispatchTableLVO

_MiniGLGetDispatchTableLVO:
	move.l a6,-(sp)
	move.l _MiniGLBase,a6
	tst.l a6
	beq.s .Lno_dispatch
	jsr -30(a6)
	move.l (sp)+,a6
	rts
.Lno_dispatch:
	move.l (sp)+,a6
	moveq #0,d0
	rts

/ kboot -- stage-1 entry + self-relocation + C runtime bring-up.
/
/ The ROM's fsload() loads /coherent to RAM base (phys 0x080000): text ->
/ seg LDSEG (0x30), data -> seg DSEG (0x31), then jumps LDSEG:l_entry.
/ The WD controller's command block and sector buffer are hardwired at that
/ same address, so before touching the disk both our segments are copied off
/ RAM base and their MMU descriptors remapped to the copies.  Each copy is
/ byte-identical, so the next instruction fetch lands on the same code.  The
/ remap is inline SOUTB, with no CALL between the copy and the remap.
/
/ Physical page map while kboot runs (RAM base 0x080000):
/   0x080000-0x09FFFF  kernel copy-down destination (also WD cmd/sector buf)
/   0x0A0000-0x0BFFFF  kernel text staging (bmain.c TSTAGE, NTSEG=2)
/   0x0C0000           launch stub (launch_, seg 0x2c)
/   0x0D0000           kernel data staging (DSTAGE) + the seg-0x3F C stack
/                      (SP starts at 0x0DFC00, grows down)
/   0x0E0000           kboot text copy (TSCR/seg 0x30 after remap)
/   0x0F0000           kboot data copy (DSCR/seg 0x31 after remap)
/
/ The relocation copies are bounded to TCOPY/DCOPY bytes because a 512 KB
/ machine's ROM keeps its live segment-1 data at phys 0x0FE400..0x0FFFFF, in
/ the tail of the data-copy page.  The Makefile checks the linked sizes fit.
/
/ MMU (Z8010 #1) descriptor load, as the ROM's segload does it:
/   soutb 0x01fc,<seg>                             select descriptor
/   soutb 0x0ffc,<base hi><base lo><limit><attr>   base = phys>>8, limit 0xff
/ attr bit0 = RD (read-only), bit1 = SYS.  0x02 = SYS writable, 0x03 = SYS
/ read-only (code).
/
/ Console I/O goes through the ROM dispatchers (romabi.h), which route to
/ whichever console the ROM selected at boot.
	.globl	start
	.shri

LDSEG	=	0x30		/ our text segment (ROM loaded us here)
DSEG	=	0x31		/ our data segment

/ The segment every compiled frame reference is relocated against: cc2 emits
/ each frame address's segment byte as a relocation adding SS, so the object
/ this loader is linked with has to say which segment its stack is in.  Both
/ bytes carry it, as the kernel's md.s spells it.
	.globl	SS
SS	=	0x3f3f
TSCR	=	0x2e		/ scratch seg for the text copy  -> 0x0E0000
DSCR	=	0x2d		/ scratch seg for the data copy  -> 0x0F0000
TCOPY	=	0x4000		/ bytes of text copied (>= SHRI+PRVI; Makefile-checked)
DCOPY	=	0x4000		/ bytes of data copied (>= data+bss; Makefile-checked)

start:
	/ ===== relocate TEXT: seg 0x30 -> phys 0x0E0000 =====
	/ program TSCR (0x2e) -> 0x0E0000, attr 0x02 (writable copy dst)
	ldb	rl0, $TSCR
	soutb	0x01fc, rl0
	ldb	rl0, $0x0e
	soutb	0x0ffc, rl0
	ldb	rl0, $0x00
	soutb	0x0ffc, rl0
	ldb	rl0, $0xff
	soutb	0x0ffc, rl0
	ldb	rl0, $0x02
	soutb	0x0ffc, rl0
	ldl	rr2, $0x30000000	/ src = seg 0x30 : 0
	ldl	rr4, $0x2e000000	/ dst = seg 0x2e : 0
	ld	r1, $TCOPY		/ bounded: keeps writes off the ROM's seg-1 tail
	ldirb	@rr4, @rr2, r1
	/ remap seg 0x30 -> 0x0E0000, attr 0x03 (read/execute)
	ldb	rl0, $LDSEG
	soutb	0x01fc, rl0
	ldb	rl0, $0x0e
	soutb	0x0ffc, rl0
	ldb	rl0, $0x00
	soutb	0x0ffc, rl0
	ldb	rl0, $0xff
	soutb	0x0ffc, rl0
	ldb	rl0, $0x03
	soutb	0x0ffc, rl0

	/ ===== relocate DATA: seg 0x31 -> phys 0x0F0000 =====
	ldb	rl0, $DSCR
	soutb	0x01fc, rl0
	ldb	rl0, $0x0f
	soutb	0x0ffc, rl0
	ldb	rl0, $0x00
	soutb	0x0ffc, rl0
	ldb	rl0, $0xff
	soutb	0x0ffc, rl0
	ldb	rl0, $0x02
	soutb	0x0ffc, rl0
	ldl	rr2, $0x31000000
	ldl	rr4, $0x2d000000
	ld	r1, $DCOPY		/ bounded: 0x0F0000..0x0F3FFF only, 0x0FE400 untouched
	ldirb	@rr4, @rr2, r1
	ldb	rl0, $DSEG
	soutb	0x01fc, rl0
	ldb	rl0, $0x0f
	soutb	0x0ffc, rl0
	ldb	rl0, $0x00
	soutb	0x0ffc, rl0
	ldb	rl0, $0xff
	soutb	0x0ffc, rl0
	ldb	rl0, $0x02
	soutb	0x0ffc, rl0

	/ ===== C runtime =====
	/ The C is built VKERN (cc2 0012): frame refs carry segment SS, so the
	/ C stack must live there.  Map it to phys 0x0D0000.
	ldb	rl0, $0x3f
	soutb	0x01fc, rl0
	ldb	rl0, $0x0d			/ 0x0D0000 >> 8
	soutb	0x0ffc, rl0
	ldb	rl0, $0x00
	soutb	0x0ffc, rl0
	ldb	rl0, $0xff
	soutb	0x0ffc, rl0
	ldb	rl0, $0x02			/ SYS, writable
	soutb	0x0ffc, rl0
	ld	r14, $0x3f00		/ SP segment = seg 0x3F (<<8 form)
	ld	r15, $0xfc00		/ SP offset (top; grows down)
	sub	r13, r13			/ clear frame pointer
	call	bmain_
hang:
	halt
	jr	hang

/ setdoff_(long base) -- set the ROM's doffset cell (seg1:0x1236), which it
/ adds to every logical fs block, so retargets iread/dirlook onto a partition.
	.globl	setdoff_
setdoff_:
	ldl	rr2, rr14(4)		/ base (long arg)
	ldl	rr4, $0x01001236	/ far ptr seg1:0x1236
	ldl	@rr4, rr2
	ret

/ setgeom_(long cmdfar, long parmfar) -- issue WDSDP, as the kernel's
/ wdsetparam() does: copy the 16-byte command block to WDCMDBLKPADDR and the
/ 6-byte parameter block to WDBUFPADDR, then strobe WDIO.  The args are far
/ pointers into the loader's data.
	.globl	setgeom_
setgeom_:
	/ map seg 0x24 -> phys 0x080000, attr 0x02 (SYS, writable)
	ldb	rl0, $0x24
	soutb	0x01fc, rl0
	ldb	rl0, $0x08
	soutb	0x0ffc, rl0
	ldb	rl0, $0x00
	soutb	0x0ffc, rl0
	ldb	rl0, $0xff
	soutb	0x0ffc, rl0
	ldb	rl0, $0x02
	soutb	0x0ffc, rl0
	/ cmd block: cmdfar -> seg 0x24 : 0 (16 bytes)
	ldl	rr2, rr14(4)		/ src = cmdfar
	ldl	rr4, $0x24000000	/ dst = WDCMDBLKPADDR
	ld	r6, $16
	ldirb	@rr4, @rr2, r6
	/ param block: parmfar -> seg 0x24 : 0x400 (6 bytes)
	ldl	rr2, rr14(8)		/ src = parmfar
	ldl	rr4, $0x24000400	/ dst = WDBUFPADDR
	ld	r6, $6
	ldirb	@rr4, @rr2, r6
	/ strobe the DMA/command line
	ld	r1, $0x0500			/ WDIO
	ld	r0, $1
	out	(r1), r0
	ret

/ mapseg_(seg, basepage, attr) -- program one Z8010 descriptor.
/ basepage = phys>>8 (e.g. 0x0800 for phys 0x080000); attr per mmu.go bits.
	.globl	mapseg_
mapseg_:
	ld	r0, rr14(4)			/ seg (low byte)
	ld	r1, rr14(6)			/ base page (hi:lo)
	ld	r2, rr14(8)			/ attr (low byte)
	soutb	0x01fc, rl0		/ select descriptor
	soutb	0x0ffc, rh1		/ base hi
	soutb	0x0ffc, rl1		/ base lo
	ldb	rl3, $0xff
	soutb	0x0ffc, rl3		/ limit = 0xff (64K)
	soutb	0x0ffc, rl2		/ attr
	ret

/ launch_(entry_off, databasepage, ntextsegs) -- hand off to the copied-down
/ kernel.  Remapping seg 0x30 means remapping our own text, so the code that
/ does it runs from a copy of `stub' in scratch segment 0x2c.  The stub maps
/ ntextsegs consecutive text segments, gives the next one to data, and jumps
/ LDSEG:entry.
	.globl	launch_
launch_:
	ld	r0, rr14(4)		/ r0 = kernel entry offset (preserved into stub)
	ld	r1, rr14(6)		/ r1 = kernel data base page (preserved into stub)
	ld	r2, rr14(8)		/ r2 = number of text segments (preserved into stub)
	/ map scratch seg 0x2c -> phys 0x0C0000, attr 0x02: writable for the
	/ ldirb below, executable for the stub afterwards.
	ldb	rl4, $0x2c
	soutb	0x01fc, rl4
	ldb	rl4, $0x0c
	soutb	0x0ffc, rl4
	ldb	rl4, $0x00
	soutb	0x0ffc, rl4
	ldb	rl4, $0xff
	soutb	0x0ffc, rl4
	ldb	rl4, $0x02
	soutb	0x0ffc, rl4
	/ copy stub (seg 0x30 : stub) -> seg 0x2c : 0
	ldar	rr6, stub
	ldl	rr8, $0x2c000000
	ld	r5, $stubend-stub
	ldirb	@rr8, @rr6, r5
	/ jump into the stub (r0=entry, r1=databasepage, r2=ntextsegs still live)
	ldl	rr4, $0x2c000000
	jp	@rr4

/ PIC stub -- runs from seg 0x2c, at an address it was not assembled at, so
/ its branches are `jr'.
stub:
	/ Kernel text: r2 segments, consecutive from LDSEG, each mapping the next
	/ 64K of physical from RAM base (0x080000).  attr 0x03 = SYS, read/execute.
	ldb	rl5, $0x30		/ segment number being programmed
	ld	r6, $0x0800		/ its base page (phys 0x080000 >> 8)
0:
	soutb	0x01fc, rl5		/ select descriptor
	soutb	0x0ffc, rh6		/ base hi
	soutb	0x0ffc, rl6		/ base lo
	ldb	rl4, $0xff
	soutb	0x0ffc, rl4		/ limit: the whole segment
	ldb	rl4, $0x03
	soutb	0x0ffc, rl4		/ attributes
	incb	rl5, $1
	add	r6, $0x0100		/ next segment = 64K further on
	djnz	r2, 0b
	/ Kernel data: the segment after the last text one (rl5 walked to it),
	/ base page in r1, attr 0x02 = SYS, read/write.
	soutb	0x01fc, rl5
	soutb	0x0ffc, rh1		/ base hi (from r1)
	soutb	0x0ffc, rl1		/ base lo
	ldb	rl4, $0xff
	soutb	0x0ffc, rl4
	ldb	rl4, $0x02
	soutb	0x0ffc, rl4
	/ jump LDSEG(0x30) : entry(r0)
	ld	r2, $0x3000			/ seg 0x30 << 8
	ld	r3, r0				/ offset
	jp	@rr2
stubend:

/ Filler keeping SHRI over one disk block: the ROM's fsload places the data
/ segment 48 bytes too early when the whole text fits in block 0.
pad:
	.ascii	"kboot fsload-block0 pad ........................................."
	.ascii	"................................................................"
	.ascii	"................................................................"
	.ascii	"................................................................"
	.ascii	"................................................................"
	.byte	0x00

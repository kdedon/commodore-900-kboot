/*
 * vid.c -- which video board answers, for the console kboot hands over.
 *
 * bi_console is decided HERE and nowhere after it: a system booted with it
 * does not probe (include/bootinfo.h).  An entry whose kboot.cfg says
 * `console serial' never reaches this file.  Every other entry that takes a
 * handoff is told what these probes find, in the order COHERENT's md.s
 * vidsel (1124-1138) and the boot ROM's own memory test use: the hi-res
 * bitmap at phys 0x3E0000, then the low-res text framebuffer at phys
 * 0x370000, and the serial line when neither answers.  Serial is the answer
 * that is never unusable, so a missing card, and every doubt, ends there.
 *
 * The test is md.s vprobe's (1146-1170): save a word, write 0x55AA and read
 * it back, then 0xAA55, and restore the word.  ONE ADDITION, for the reason
 * scc.c orders its reads: the C900 raises nothing for an unclaimed address,
 * and a bus that still holds the last value driven onto it would echo a
 * write straight back.  So each write at offset 0 is followed by the OTHER
 * pattern at offset 2 before either is read, and an echo returns the wrong
 * word at offset 0.  Both words are framebuffer (a character cell, or
 * sixteen pixels), and both are put back.
 *
 * THE SCRATCH SEGMENT IS 0x25.  This loader programs 0x0A, 0x0B and 0x0D
 * (staging), 0x22-0x24 (the copy-down windows and the WD command block),
 * 0x2C-0x2E (the launch stub and the relocation copies), 0x30/0x31 (itself)
 * and 0x3F (its stack); the ROM runs in 0x00/0x01 and maps its display
 * planes at 0x3A/0x3B.  Nothing names 0x25, so remapping it cannot move
 * anything the loader or the ROM's console output is using.  It is left on
 * the last page probed, as the copy-down windows are left: a system maps
 * the segments it uses.
 */
#include <bootinfo.h>
#include "kboot.h"

#define VSEG	0x25
#define HRPAGE	0x3e00		/* phys 0x3E0000 >> 8: the hi-res bitmap */
#define LRPAGE	0x3700		/* phys 0x370000 >> 8: the text framebuffer */

#ifndef VGET			/* tests/vidtest.c supplies all three */
extern	mapseg();
#define VGET(o)		(((unsigned *)0x25000000L)[o])
#define VPUT(o, v)	(((unsigned *)0x25000000L)[o] = (unsigned)(v))
#endif

/* Is there framebuffer RAM at physical page `page'?  Leaves it as found. */
static int
vidthere(page) unsigned page;
{
	unsigned s0, s2;
	int r;

	mapseg(VSEG, page, 0x02);	/* SYS, writable */
	s0 = VGET(0);
	s2 = VGET(1);
	r = 0;
	VPUT(0, 0x55aa);
	VPUT(1, 0xaa55);
	if (VGET(0) == 0x55aa && VGET(1) == 0xaa55) {
		VPUT(0, 0xaa55);
		VPUT(1, 0x55aa);
		if (VGET(0) == 0xaa55 && VGET(1) == 0x55aa)
			r = 1;
	}
	VPUT(0, s0);
	VPUT(1, s2);
	return (r);
}

/* The console to hand over for an entry that lets the cards decide. */
unsigned
vidprobe()
{
	if (vidthere(HRPAGE))
		return (BI_CON_HR);
	if (vidthere(LRPAGE))
		return (BI_CON_LR);
	return (BI_CON_SER);
}

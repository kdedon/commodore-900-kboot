/*
 * memmap.h -- which 64 KB segments of a C900 hold RAM a system may allocate.
 *
 * The boot ROM sizes memory by marching whole segments upward from 0x08,
 * writing a pattern and reading it back, and stops at the first segment that
 * does not answer; it reports what it found as rom_bram/rom_eram, in 512-byte
 * clicks.  FRAMEBUFFER MEMORY ANSWERS THAT TEST.  The LR text RAM mirrors its
 * 4 KB through the whole of segment 0x37 and an HR bitmap answers at 0x3E and
 * 0x3F, so all of them pass and land inside the range the ROM reports.  Taking
 * that range whole hands a system the screen to allocate out of.
 *
 * So the range is a MAP, not an extent: the ROM's range less the table below,
 * leaving a list of RAM runs.  Memory above a framebuffer is ordinary memory
 * and stays usable.  This header is the table itself, so that the loader and
 * every system it starts subtract the same list.
 */
#ifndef	__SYS_MEMMAP_H__
#define	__SYS_MEMMAP_H__

/*
 * Segments the address space can hold: the Z8010's base register is 16 bits,
 * so 16 MB.  The C900 motherboard decodes only the first 4 MB -- it stops at
 * segment 0x3F -- and nothing here probes for more than the ROM found.
 */
#define	MM_NSEG		256
#define	MM_SEGCLICKS	64			/* 1 KB clicks in a 64 KB segment */
#define	MM_SEG(click)	((unsigned)((click) / MM_SEGCLICKS))

/* What a reserved range IS.  A system that has to explain itself, or decide
 * whether it can map a range for its own use, needs the reason and not just
 * the numbers. */
#define	MR_ROM		1	/* the boot ROM and its aliases */
#define	MR_LRTEXT	2	/* LR text RAM, mirrored through the segment */
#define	MR_HRBITMAP	3	/* HR bitmap */
#define	MR_MBSTACK	4	/* motherboard RAM the ROM keeps for itself */
#define	MR_CONFIG	5	/* named by the loader's config, not by hardware */

/* Whether an entry applies to this machine. */
#define	MW_ALWAYS	0
#define	MW_HR		1	/* only with an HR board fitted */
#define	MW_NOHR		2	/* only without one */

/* One reserved range: segments mr_first .. mr_last, inclusive both ends. */
struct	memres {
	unsigned short	mr_first;
	unsigned short	mr_last;
	unsigned short	mr_why;		/* MR_* */
	unsigned short	mr_when;	/* MW_* */
};

/*
 * THE BUILT-IN TABLE.  Segment 0x3F is the second half of the HR bitmap when a
 * board is fitted and motherboard stack RAM the ROM uses privately when it is
 * not, so it is out either way and only the reason differs -- which is why it
 * appears twice.  0x3E is bitmap only when has_hires says a board is there.
 */
#define	MM_NRES	5
static struct memres mmres[MM_NRES] = {
	{ 0x00, 0x07, MR_ROM, MW_ALWAYS },
	{ 0x37, 0x37, MR_LRTEXT, MW_ALWAYS },
	{ 0x3e, 0x3e, MR_HRBITMAP, MW_HR },
	{ 0x3f, 0x3f, MR_HRBITMAP, MW_HR },
	{ 0x3f, 0x3f, MR_MBSTACK, MW_NOHR },
};

#endif	/* __SYS_MEMMAP_H__ */

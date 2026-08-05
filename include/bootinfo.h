/*
 * bootinfo -- how kboot tells an operating system its disk layout and the
 * flags it was booted with.
 *
 * The block is OFFERED, never required.  kboot loads and starts whatever a
 * menu entry names; a system carrying no bootinfo is started with nothing
 * written into it and nothing said about it, and only an entry that ASKED for
 * a handoff can fail to get one.  Any system that wants a partition table or
 * the boot flags can carry the block and be handed one -- nothing here is
 * particular to the system that happens to use it first.
 *
 * A system with no bootinfo runs on whatever it knows by itself; a bad magic,
 * version, length, checksum or count is damage -- kboot refuses the entry.
 * The block lives in the system's own data, holding the magic, version, size,
 * bi_src = BI_SRC_KERNEL and whatever table it was built with; kboot finds it
 * by bi_magic and overwrites it with kboot.cfg's layout, BI_SRC_KBOOT and the
 * checksum.  COHERENT stages this file as <sys/bootinfo.h> at build time
 * (os/hostbuild/kboot.sh), which is one way to consume it, not the interface.
 */
#ifndef	__SYS_BOOTINFO_H__
#define	__SYS_BOOTINFO_H__

#define	BI_MAGLEN	8
#define	BI_MAGIC	{ 'K', 'B', 'O', 'O', 'T', 'P', 'T', 'B' }
#define	BI_VERSION	3			/* 3 added bi_flags + bi_console */
#define	BI_OLDEST	2			/* 2 added bi_src + the swap extent */
#define	BI_NPART	16			/* wd(4) pseudo-drives: /dev/hd0..hd15 */

#define	BI_SRC_KERNEL	0		/* the compiled-in (generated) table */
#define	BI_SRC_KBOOT	1		/* handed over by the loader */

/*
 * bi_flags -- kernel boot flags.  ZERO IS THE DEFAULT BOOT for every bit, 
 * so a v2 block reads as the ordinary boot. Undefined bits are RESERVED
 * AND MUST BE ZERO.
 */
#define	BF_SINGLE	0x0001	/* come up single user; do not run /etc/rc */

/*
 * bi_console -- which console the operator is at, not which board is fitted.
 * The ROM's flags tell serial from video only.
 */
#define	BI_CON_ANY	0	/* the loader said nothing: the kernel probes */
#define	BI_CON_SER	1	/* serial line (the SCC console) */
#define	BI_CON_VID	2	/* a video board; the kernel picks hi/lo res */

/*
 * One pseudo-drive: /dev/hdN spans bstart .. bstart+bcount-1 of the physical
 * drive.  The driver refuses a block at or past bcount, so an all-zero slot is
 * unused and every access to it fails.
 */
struct	bipart {
	unsigned long	bstart;		/* starting block # */
	unsigned long	bcount;		/* size in blocks (0 = slot unused) */
};

struct	bootinfo {
	char			bi_magic[BI_MAGLEN];/* BI_MAGIC, kboot's search key */
	unsigned short	bi_version;			/* BI_VERSION */
	unsigned short	bi_len;				/* sizeof (struct bootinfo) */
	unsigned short	bi_npart;			/* slots described */
	unsigned short	bi_sum;				/* makes the 16-bit word sum zero */
	unsigned short	bi_src;				/* BI_SRC_*: who wrote the rest */
	/*
	 * The swap extent, in blocks RELATIVE TO the pseudo-drive bi_swapdev
	 * names, as in the media descriptor's swap= key.  swapbot is the first
	 * swap block, swaptop one past the last; bot >= top means no swap.
	 */
	unsigned short	bi_swapdev;	/* pseudo-drive slot holding swap */
	unsigned long	bi_swapbot;
	unsigned long	bi_swaptop;
	struct bipart	bi_part[BI_NPART];
	/* --- version 3 ------------------------------------------------- */
	unsigned short	bi_flags;	/* BF_*: what this boot was asked for */
	unsigned short	bi_console;	/* BI_CON_*: where the operator is */
};

/*
 * THE KERNEL DECLARES; THE LOADER FILLS DOWN.  The block arrives with the
 * version and length that kernel was built for; kboot fills the fields THAT
 * version defines, writes back exactly that many bytes, and says what it had no
 * room to deliver.  A version is a PREFIX of every later one: fields are
 * APPENDED, never inserted, resized or reordered.  A loader that meets a newer
 * kernel's length it does not know refuses the entry.
 */
#define	BI_TAIL3	4		/* bi_flags + bi_console */
#define	BI_LEN3		(sizeof (struct bootinfo))
#define	BI_LEN2		(BI_LEN3 - BI_TAIL3)

/*
 * The length of one version's block, 0 if this side does not know the version.
 * A reader decides from it whether a field is THERE, never from the number.
 */
static unsigned short
bilen(v) unsigned v;
{
	if (v == 2)
		return ((unsigned short)BI_LEN2);
	if (v == 3)
		return ((unsigned short)BI_LEN3);
	return (0);
}

/*
 * The checksum: the 16-bit words of the first bi_len bytes must sum to zero.
 * It covers the HANDOFF, not the image, and lives here so kboot and the kernel
 * cannot compute it differently.  Static: one .c of each program includes it.
 */
static unsigned short
bisum(bp) register struct bootinfo *bp;
{
	register unsigned short *w;
	register unsigned short s;
	register unsigned n;

	s = 0;
	w = (unsigned short *)bp;
	for (n = bp->bi_len >> 1; n != 0; --n)
		s += *w++;
	return (s);
}

#endif	/* __SYS_BOOTINFO_H__ */

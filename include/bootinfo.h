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
#define	BI_VERSION	4			/* 4 added bi_serial */
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
 * bi_console -- which console the system is to use.  SER, LR and HR are a
 * DECISION: the system takes them as sent and does not probe.  kboot always
 * sends one of the three, per menu entry: a kboot.cfg `console serial' line
 * hands over BI_CON_SER as written, and `console probe', or no line at all,
 * hands over what the framebuffer probe finds (src/vid.c: hi-res first,
 * then low-res), or BI_CON_SER when no card answers.  Nothing forces video,
 * because an entry pinned to a card that is not fitted would boot unusable.
 *
 * ANY and VID are what older loaders sent, which could tell serial from
 * video only (by the ROM's flags) and left the rest to the kernel; they, a
 * value a reader does not know, and no block at all leave the choice to the
 * system, which may probe for itself.  LR and HR were added without a
 * version change: the field, the layout and every length are version 3's.
 */
#define	BI_CON_ANY	0	/* the loader said nothing: the system decides */
#define	BI_CON_SER	1	/* serial line (the SCC console) */
#define	BI_CON_VID	2	/* a video board; the system picks hi/lo res */
#define	BI_CON_LR	3	/* low-res: the text framebuffer at phys 0x370000 */
#define	BI_CON_HR	4	/* hi-res: the bitmap at phys 0x3E0000 */

/*
 * bi_serial -- WHICH SERIAL CHANNELS ARE FITTED, one bit each.  This is the
 * hardware question bi_console is not: bi_console says where the operator is
 * sitting, bi_serial says what silicon answered when the loader wrote to it
 * and read it back.  Any system wanting to offer logins, or a second line, on
 * everything the machine really has reads this instead of guessing.
 *
 * A bit is set only for a channel the loader PROVED answers.  Zero therefore
 * means "not found" and never "not looked at": a channel the loader could not
 * prove is reported absent on purpose, because a system that talks to a
 * channel that is not there is worse off than one that ignores a channel that
 * is.  A block older than version 4 carries no bi_serial at all, and a reader
 * that finds none falls back to whatever it knew before.
 *
 * The bit order is the C900's own serial-line numbering -- the order of
 * ascending I/O base address, which is also the minor-device order of the
 * Coherent Z8030 driver (al.c altty[]) and the line numbering of the system
 * hardware spec.  Bit i is line i on every one of them:
 *
 *   bit  I/O base  chip                              connector
 *    0   0x0100    motherboard SCC U74 channel A     rear DB25 (the ROM's
 *                                                    own console line)
 *    1   0x0120    motherboard SCC U74 channel B     rear DB25
 *    2   0x0300    LR board SCC #1 U31 channel A     CN3 DB25
 *    3   0x0320    LR board SCC #1 U31 channel B     CN4 DB25
 *    4   0x0380    LR board SCC #2 U36 channel A     CN5 header
 *    5   0x03A0    LR board SCC #2 U36 channel B     CN6 header
 *    6   0x0600    Aux3 channel A ) spec-architectural expansion; no loader
 *    7   0x0620    Aux3 channel B ) probes them yet, so they read 0
 *    8   0x0680    Aux4 channel A ) reserved by the spec for sync comms,
 *    9   0x06A0    Aux4 channel B ) modems and printers, not for logins
 *
 * Bits 10-15 are RESERVED AND MUST BE ZERO.  A machine with no LR board (an
 * HR system) has nothing at 0x0300-0x03FF and reports 0x03; an LR system with
 * U36 unpopulated -- which is how at least one inventoried board shipped --
 * reports 0x0F; a fully populated LR system reports 0x3F.
 */
#define	BI_NSERIAL	10	/* channels the numbering above defines */
#define	BI_SER_CON	0x0001	/* bit 0: the line the boot ROM consoles on */

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
	/* --- version 4 ------------------------------------------------- */
	unsigned short	bi_serial;	/* serial channels found, one bit each */
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
#define	BI_TAIL4	2		/* bi_serial */
#define	BI_LEN4		(sizeof (struct bootinfo))
#define	BI_LEN3		(BI_LEN4 - BI_TAIL4)
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
	if (v == 4)
		return ((unsigned short)BI_LEN4);
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

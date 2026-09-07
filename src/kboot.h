/*
 * kboot.h -- what the loader's pieces share.
 *
 *	cfg.c	the config text and its parser.  No disk, no console, no ROM.
 *	ui.c	the menu, the countdown, the editor, recovery.  Console only.
 *	bmain.c	the ROM: inode walk, staging, geometry, the bootinfo handoff.
 *
 * cfg.c and ui.c compile on a host against a fake console (tests/).
 */
#ifndef KBOOT_H
#define KBOOT_H

#include <bootinfo.h>	/* for BF_*: the menu is where a flag is set */

#define DIRSIZ	14		/* a COHERENT directory name field */
#define LBLSZ	20		/* a menu label, terminator included */
#define FLGSZ	32		/* the flag names of one entry, as the menu shows them */

/*
 * How much config this loader holds.  CFGBLK bounds kboot.cfg in BYTES: that
 * many 512-byte blocks are read, costing the same in bss, and a longer file
 * is read truncated with a warning.  MAXOS bounds it in ENTRIES, each costing
 * sizeof (struct osent); `os' lines past it are dropped.  oslist is MAXOS+1
 * long because recovery mode builds into the slot past the parsed ones.
 */
#define CFGBLK	2			/* KNOB: 512-byte blocks of kboot.cfg read */
#define CFGMAX	(CFGBLK * 512)
#define MAXOS	8			/* KNOB: `os' entries the menu holds */

struct osent {
	char			label[LBLSZ];
	unsigned long	base;			/* partition start block */
	char			file[DIRSIZ];	/* kernel filename in that partition */
	int				wantbi;			/* this entry asks for the partition table */
	/* Which scope owns the `part' and `swap' lines this entry boots with:
	 * its own `os' line (>= 0, the line's index), the `system' block it
	 * stands in (LAYSYS), or LAYGLOB for the lines before either. */
	int				lay;
	/* The same, for the `bflag' lines that name this entry's flags.  Two
	 * entries may run kernels whose BF_* differ, so names are per scope
	 * and one file may give one word two bits. */
	int				voc;
	/* The bits the `flags' lines and the menu asked for; handed over as
	 * bi_flags (include/bootinfo.h). */
	unsigned		bflags;
	/* A `flags' line named something voc does not define.  The entry is
	 * not booted: the flag cannot be delivered and dropping it quietly
	 * boots a system that looks like the one asked for. */
	int				badflg;
};

/* oslist[].lay and .voc name a scope: an `os' line by its index, or one of
 * these.  LAYGLOB is what stands before any `system' or `os' line; LAYSYS(n)
 * is the n'th `system' block, counted from 0 in the order the file writes
 * them, so a scope needs no storage beyond this int. */
#define LAYGLOB	(-1)
#define LAYSYS(n)	(-2 - (n))

/* --- cfg.c ---------------------------------------------------------------- */
extern char				cfg[];		/* kboot.cfg text, NUL-clobbered in place */
extern int				cfglen;
extern struct osent		oslist[];
extern int				nos;
extern int				gcyl, gheads, gspt, gprecomp;
extern int				cfgwait;	/* `timeout' seconds; 0 = no countdown */
extern int				cfgdflt;	/* `default', 1-based; 0 = the first entry */
/* A config file was read and named nothing bootable -- not the same as there
 * being no config file. */
extern int				cfgfault;
/* A `flags' line stood where no entry owned it, and set nothing. */
extern int				cfgflgerr;

extern int				streqoff();
extern unsigned long	atoloff();
extern unsigned			bfscan();
/* The bit entry k's vocabulary gives a name -- the cfg[] token at `off', or
 * the string `s' when off is negative -- and 0 if it gives it none. */
#define bfbit(k, off, s)	bfscan(k, off, s, (char *)0, 0)
/* The names of the flags entry k carries, into d[0..n), for the screen. */
#define bflabel(k, d, n)	bfscan(k, -1, (char *)0, d, n)
extern					copyoff();
extern					copyname();
extern					cfgparse();
/* Build bi/nparts for entry `k'.  Runs after the menu has chosen, so only the
 * layout that is booted is ever built. */
extern					cfglayout();

/* --- scc.c ---------------------------------------------------------------- */
/* Is a Z8030 channel based at this I/O address really there?  Writes a pattern
 * and reads it back; leaves the channel as it found it. */
extern int				sccthere();
/* The serial channels this machine has, one bit each, as bootinfo.h's
 * bi_serial defines them.  The argument says the operator is at the serial
 * console, whose channel is then taken as present without being written to. */
extern unsigned			sccprobe();

/* --- bipack.c ------------------------------------------------------------- */
/* What a kernel's block was too old to be given.  Zero is everything
 * delivered, which is what a kernel built against this loader's BI_VERSION
 * gets. */
#define BIU_FLAGS	0x0001	/* bi_flags + bi_console: older than version 3 */
#define BIU_SERIAL	0x0002	/* bi_serial: older than version 4 */

/* Fill a kernel's block down to the version it declared, and checksum it.
 * Returns the BIU_* bits it had no room for. */
extern unsigned			bipack();

/* --- ui.c ----------------------------------------------------------------- */
extern int				uimenu();	/* -> the entry to boot, or -1 to give up */

/* --- bmain.c -------------------------------------------------------------- */
/* Does entry `k' name something that could be booted?  Reads the disk.
 * Returns 0, or a diagnostic.  Leaves the ROM's doffset where it found it. */
extern char				*osverify();

#endif /* KBOOT_H */

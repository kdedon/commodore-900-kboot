/*
 * cfg.c -- the kboot.cfg text and its parser.  bmain.c reads the file into
 * cfg[] and calls cfgparse(); the globals below are what the loader then
 * works from.  No disk, no console, no ROM (tests/cfgtest.c).
 *
 * Format: line-oriented, '#' comments, whitespace-separated tokens, the first
 * token selecting the keyword.  Unknown keywords are ignored and every key
 * has a default.  The reference is README.md; cfg/kboot.cfg.sample is a
 * working file.
 */
#include "kboot.h"
#include <bootinfo.h>

char			cfg[CFGMAX];			/* kboot.cfg text */
int				cfglen;
struct osent	oslist[MAXOS+1];		/* +1: recovery's scratch entry */
int				nos;
int				gcyl, gheads, gspt, gprecomp;
int				cfgwait;
int				cfgdflt;
int				cfgfault;
int				cfgflgerr;

/* The handoff block, built from the `part' and `swap' lines and written into
 * the loaded kernel by bmain.c (include/bootinfo.h). */
struct bootinfo	bi = { BI_MAGIC, BI_VERSION, sizeof (struct bootinfo) };
int				nparts;			/* `part' lines seen */

/* --- helpers: no libc here, and tokens are cfg[] OFFSETS, not pointers -----
 * Nothing here writes cfg[], so the text can be walked more than once: the
 * layout pass below is a second walk of the same buffer. */

/* Does offset `off' end a token? */
static int
issep(off) register int off;
{
	register int c;

	if (off >= cfglen)
		return (1);
	c = cfg[off];
	return (c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
		c == '#' || c == '\0');
}

int
streqoff(off, s) int off; register char *s;
{
	while (*s && cfg[off] == *s) { off++; s++; }
	return (*s == '\0' && issep(off));
}

/* Are the tokens at offsets `a' and `b' the same word? */
static int
eqoff(a, b) register int a, b;
{
	while (!issep(a) && !issep(b) && cfg[a] == cfg[b]) { a++; b++; }
	return (issep(a) && issep(b));
}

unsigned long
atoloff(off) register int off;
{
	unsigned long v;

	v = 0;
	while (off < cfglen && cfg[off] >= '0' && cfg[off] <= '9')
		v = v * 10 + (cfg[off++] - '0');
	return v;
}

copyoff(d, off, n) register char *d; register int off; int n;
{
	while (--n > 0 && !issep(off))
		*d++ = cfg[off++];
	*d = '\0';
}

copyname(d, s, n) register char *d, *s; int n;
{
	while (--n > 0 && *s)
		*d++ = *s++;
	*d = '\0';
}

/* Tokenise the line at *pi into tok[0..5) as cfg[] offsets, advance *pi past
 * it, and return how many tokens the line HAS -- which may be more than five,
 * so a keyword taking a list can refuse a line it could only read part of.
 * '#' starts a comment. */
static int
cfgline(pi, tok) int *pi; int tok[];
{
	register int i;
	int nt;

	i = *pi;
	nt = 0;
	while (i < cfglen && cfg[i] != '\n') {
		if (cfg[i] == ' ' || cfg[i] == '\t' || cfg[i] == '\r' ||
		    cfg[i] == '\0') {
			i++;
			continue;
		}
		if (cfg[i] == '#') {
			while (i < cfglen && cfg[i] != '\n')
				i++;
			break;
		}
		if (nt < 5)
			tok[nt] = i;
		nt++;
		while (i < cfglen && !issep(i))
			i++;
	}
	if (i < cfglen && cfg[i] == '\n')
		i++;
	*pi = i;
	return (nt);
}

/* --- boot flags ------------------------------------------------------------
 * The names are the CONFIG's, not this loader's: a `bflag' line pairs a word
 * with the bit the kernel that entry boots was built with, so two entries
 * running different kernels need not agree on what a bit means.
 *
 * A walk of the `bflag' lines of ONE scope -- oslist[k].voc, which cfgparse()
 * has already resolved to the entry's own `os' line, the `system' block it
 * stands in, or LAYGLOB.  All or nothing, as with `part': the nearest scope
 * that names anything is the whole vocabulary.  Two jobs, one walk (kboot.h):
 *
 *	d == 0	bfbit: return the bit named by the token at cfg[] offset
 *		`off', or by the string `s' when `off' is negative; 0 for a
 *		name this vocabulary does not have.
 *	d != 0	bflabel: name into d[0..n) the flags entry `k' carries, blank
 *		separated, "" for none.  A flag set and not shown cannot be
 *		checked against the file that set it.
 */
unsigned
bfscan(k, off, s, d, n) int k, off; char *s, *d; int n;
{
	int i, nt, tok[5], owner, want, oidx, nsys, j;
	unsigned m, hit;

	want = oslist[k].voc;
	owner = LAYGLOB;
	oidx = 0;
	nsys = 0;
	hit = 0;
	i = 0;
	if (d != 0)
		*d = '\0';
	while (i < cfglen) {
		nt = cfgline(&i, tok);
		if (nt >= 1 && streqoff(tok[0], "system")) {
			owner = LAYSYS(nsys++);
			continue;
		}
		if (nt >= 4 && streqoff(tok[0], "os")) {
			owner = oidx++;
			continue;
		}
		if (nt < 3 || owner != want || !streqoff(tok[0], "bflag"))
			continue;
		m = (unsigned)atoloff(tok[2]);
		if (d == 0) {
			if (off < 0 ? streqoff(tok[1], s) : eqoff(tok[1], off))
				hit = m;
			continue;
		}
		if (m == 0 || (oslist[k].bflags & m) == 0)
			continue;
		for (j = 0; d[j] != '\0'; j++)
			;
		if (j != 0 && j < n - 1)
			d[j++] = ' ';
		copyoff(d + j, tok[1], n - j);
	}
	return (hit);
}

/*
 * Parse cfg[0..cfglen) into gcyl/.../oslist.  The disk LAYOUT is not built
 * here -- cfglayout() does that once an entry has been chosen -- but each
 * entry records which layout is its own.  Callers set the geometry defaults
 * first: a config that omits `geom' keeps them.
 */
cfgparse()
{
	int i, nt, tok[5], owner, oidx, sys, nsys, syslay, sysvoc, j;
	unsigned b;

	nos = 0;
	cfgwait = 0;
	cfgdflt = 0;
	cfgflgerr = 0;
	owner = sys = LAYGLOB;
	oidx = nsys = 0;
	syslay = sysvoc = 0;
	i = 0;
	while (i < cfglen) {
		nt = cfgline(&i, tok);

		if (nt >= 1 && streqoff(tok[0], "system")) {
			/* A `system' block opens a scope between the file's
			 * defaults and an entry's own lines: the `part',
			 * `swap' and `bflag' lines up to its first `os' line
			 * are what every entry under it inherits, so one OS's
			 * layout is written once for all its entries.  The
			 * label is for the reader; nothing refers to it. */
			owner = sys = LAYSYS(nsys++);
			syslay = sysvoc = 0;
		} else if (nt >= 5 && streqoff(tok[0], "geom")) {
			gcyl     = (int)atoloff(tok[1]);
			gheads   = (int)atoloff(tok[2]);
			gspt     = (int)atoloff(tok[3]);
			gprecomp = (int)atoloff(tok[4]);
		} else if (nt >= 4 && streqoff(tok[0], "os")) {
			/* Every `os' line owns the part/swap lines that follow
			 * it, whether or not the entry itself was kept. */
			owner = oidx++;
			if (nos < MAXOS) {
				copyoff(oslist[nos].label, tok[1], LBLSZ);
				oslist[nos].base = atoloff(tok[2]);
				copyoff(oslist[nos].file, tok[3], DIRSIZ);
				/* Optional 5th field `part': hand this kernel
				 * the table.  Absent, it runs on its own. */
				oslist[nos].wantbi = (nt >= 5 &&
					streqoff(tok[4], "part"));
				/* Until a line of its own says otherwise, it
				 * takes the nearest scope that declared one:
				 * its `system' block, or the global lines if
				 * that block declared none.  It carries no
				 * flag until a `flags' line or the menu sets
				 * one. */
				oslist[nos].lay = syslay ? sys : LAYGLOB;
				oslist[nos].voc = sysvoc ? sys : LAYGLOB;
				oslist[nos].bflags = 0;
				oslist[nos].badflg = 0;
				nos++;
			}
		} else if (nt >= 4 && (streqoff(tok[0], "part") ||
				       streqoff(tok[0], "swap"))) {
			/* After an `os' line these belong to that entry; after
			 * a `system' line, to that block; before either, they
			 * are the global layout. */
			if (owner >= 0 && owner < nos)
				oslist[owner].lay = owner;
			else if (owner == sys)
				syslay = 1;
		} else if (nt >= 3 && streqoff(tok[0], "bflag")) {
			/* The same scopes name the flags (bfscan). */
			if (owner >= 0 && owner < nos)
				oslist[owner].voc = owner;
			else if (owner == sys)
				sysvoc = 1;
		} else if (nt >= 2 && streqoff(tok[0], "timeout")) {
			/* timeout <seconds> before `default' boots on its own.
			 * 0, and absence, both mean wait forever. */
			cfgwait = (int)atoloff(tok[1]);
		} else if (nt >= 2 && streqoff(tok[0], "default")) {
			/* default <n>, 1-based.  Out of range is ignored
			 * rather than clamped. */
			cfgdflt = (int)atoloff(tok[1]);
		}
	}

	/* flags <NAME> ... -- the bits the entry above boots with, by the
	 * names its `bflag' lines give them.  A second walk, because a name
	 * must be read against the whole vocabulary its entry ends up with,
	 * whichever side of the `flags' line the `bflag' lines were written.
	 *
	 * A name the vocabulary does not define REFUSES the entry.  An unknown
	 * KEYWORD is ignored because this loader was never going to act on it;
	 * a name inside a keyword it does implement is an instruction it
	 * cannot carry out, and a boot missing a flag looks exactly like the
	 * boot that was asked for. */
	owner = LAYGLOB;
	oidx = 0;
	i = 0;
	while (i < cfglen) {
		nt = cfgline(&i, tok);
		if (nt >= 1 && streqoff(tok[0], "system"))
			owner = LAYGLOB;
		else if (nt >= 4 && streqoff(tok[0], "os"))
			owner = oidx++;
		else if (nt >= 2 && streqoff(tok[0], "flags")) {
			if (owner < 0 || owner >= nos) {
				/* Flags belong to an entry, and there is no
				 * entry here for them to be the default of. */
				cfgflgerr = 1;
				continue;
			}
			/* Five tokens a line are kept, so a sixth name would
			 * be dropped unseen.  Write another `flags' line. */
			if (nt > 5)
				oslist[owner].badflg = 1;
			for (j = 1; j < nt && j < 5; j++) {
				b = bfbit(owner, tok[j], (char *)0);
				if (b == 0)
					oslist[owner].badflg = 1;
				else
					oslist[owner].bflags |= b;
			}
		}
	}
}

/*
 * Build the layout entry `k' boots with: bi.bi_part, the swap extent and
 * nparts.  A second walk of cfg[], reading the `part' and `swap' lines of
 * ONE owner -- oslist[k].lay: the entry's own `os' line, the `system' block
 * it stands in, or LAYGLOB for the lines before either.
 *
 * A scope that declares any layout line takes NOTHING from the one outside
 * it, swap included: a swap extent sized for another system's table is worse
 * than none.
 */
cfglayout(k) int k;
{
	int i, i2, nt, tok[5], owner, want, oidx, nsys;

	for (i2 = 0; i2 < BI_NPART; i2++) {
		bi.bi_part[i2].bstart = 0;
		bi.bi_part[i2].bcount = 0;
	}
	bi.bi_swapdev = 0;
	bi.bi_swapbot = 0;
	bi.bi_swaptop = 0;
	nparts = 0;

	want = oslist[k].lay;
	owner = LAYGLOB;
	oidx = 0;
	nsys = 0;
	i = 0;
	while (i < cfglen) {
		nt = cfgline(&i, tok);
		if (nt >= 1 && streqoff(tok[0], "system")) {
			owner = LAYSYS(nsys++);
			continue;
		}
		if (nt >= 4 && streqoff(tok[0], "os")) {
			owner = oidx++;
			continue;
		}
		if (owner != want)
			continue;
		if (nt >= 4 && streqoff(tok[0], "part")) {
			/* part <slot> <start> <blocks> -- one wd(4)
			 * pseudo-drive, /dev/hd<slot>.  Unnamed slots stay
			 * zero, and a zero-length slot refuses every block. */
			i2 = (int)atoloff(tok[1]);
			if (i2 >= 0 && i2 < BI_NPART) {
				bi.bi_part[i2].bstart = atoloff(tok[2]);
				bi.bi_part[i2].bcount = atoloff(tok[3]);
				nparts++;
			}
		} else if (nt >= 4 && streqoff(tok[0], "swap")) {
			/* swap <slot> <swapbot> <swaptop> -- the swap extent,
			 * in blocks relative to /dev/hd<slot>. */
			bi.bi_swapdev = (unsigned)atoloff(tok[1]);
			bi.bi_swapbot = atoloff(tok[2]);
			bi.bi_swaptop = atoloff(tok[3]);
		}
	}
}

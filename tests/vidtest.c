/*
 * vidtest.c -- the framebuffer probe, against memory that is not there.
 *
 * Like scctest.c, the probe is allowed to be wrong in ONE direction only.  A
 * card it misses hands the system the serial line, which is never unusable;
 * a card it invents hands the system a screen nobody can see.  So every empty
 * bus here -- pulled up, pulled down, still holding the last word driven onto
 * it, a device answering one constant, and a page whose offset 2 aliases
 * offset 0 -- must come out BI_CON_SER, and real RAM must be found and left
 * holding exactly what it held.
 *
 * src/vid.c reaches the machine through mapseg() and two word accessors.
 * Supplying those three is the whole substitution: the file under test is the
 * shipped one.
 */
#include <stdio.h>
#include <string.h>

static unsigned	vget();
static void		vput();
static void		fakemap();

#define VGET(o)			vget(o)
#define VPUT(o, v)		vput(o, (unsigned)(v))
#define mapseg(s, p, a)	fakemap(s, p, a)

#ifndef VIDSRC
#define VIDSRC "../src/vid.c"
#endif
#include VIDSRC

#define M_RAM	0	/* framebuffer RAM: holds what is written */
#define M_HIGH	1	/* nothing: pull-ups, reads 0xffff */
#define M_LOW	2	/* nothing: reads 0x0000 */
#define M_ECHO	3	/* nothing: the bus holds the last word driven */
#define M_CONST	4	/* something answering 0x55aa to everything */
#define M_ALIAS	5	/* RAM decoded to one word: offset 2 is offset 0 */

#define P_HR	0
#define P_LR	1

static int		kind[2];
static unsigned	mem[2][2];
static int		page = -1;	/* -1: mapped somewhere not a framebuffer */
static int		nmap[2];
static int		badmap;		/* a segment or attribute not the probe's */
static unsigned	lastbus;
static int		fails;

static void
fakemap(seg, pg, attr) int seg, attr; unsigned pg;
{
	if (seg != 0x25 || attr != 0x02)
		badmap = 1;
	page = pg == 0x3e00 ? P_HR : pg == 0x3700 ? P_LR : -1;
	if (page < 0)
		badmap = 1;
	else
		nmap[page]++;
}

static unsigned
vget(o) int o;
{
	if (page < 0)
		return (0xffff);
	switch (kind[page]) {
	case M_RAM:		return (mem[page][o]);
	case M_ALIAS:	return (mem[page][0]);
	case M_LOW:		return (0);
	case M_ECHO:	return (lastbus);
	case M_CONST:	return (0x55aa);
	}
	return (0xffff);
}

static void
vput(o, v) int o; unsigned v;
{
	lastbus = v & 0xffff;
	if (page < 0)
		return;
	if (kind[page] == M_RAM)
		mem[page][o] = v & 0xffff;
	else if (kind[page] == M_ALIAS)
		mem[page][0] = v & 0xffff;
}

static void
run(what, hr, lr, want) char *what; int hr, lr; unsigned want;
{
	unsigned got;
	int p;

	kind[P_HR] = hr;
	kind[P_LR] = lr;
	for (p = 0; p < 2; p++) {
		mem[p][0] = 0x1234 + p;
		mem[p][1] = 0x5678 + p;
		nmap[p] = 0;
	}
	badmap = 0;
	lastbus = 0xffff;
	got = vidprobe();
	if (got != want) {
		printf("  FAIL %-40s got %u want %u\n", what, got, want);
		fails++;
	}
	if (badmap) {
		printf("  FAIL %-40s mapped a segment that is not the probe's\n", what);
		fails++;
	}
	for (p = 0; p < 2; p++)
		if (kind[p] == M_RAM
		 && (mem[p][0] != 0x1234 + p || mem[p][1] != 0x5678 + p)) {
			printf("  FAIL %-40s left %s RAM changed\n", what,
				p == P_HR ? "HR" : "LR");
			fails++;
		}
	if (want == BI_CON_HR && nmap[P_LR] != 0) {
		printf("  FAIL %-40s probed LR after HR answered\n", what);
		fails++;
	}
}

int
main()
{
	printf("vidtest: the framebuffer probe\n");
	run("no card, pulled up", M_HIGH, M_HIGH, BI_CON_SER);
	run("no card, pulled down", M_LOW, M_LOW, BI_CON_SER);
	run("no card, bus echoes the last word", M_ECHO, M_ECHO, BI_CON_SER);
	run("a constant answer", M_CONST, M_CONST, BI_CON_SER);
	run("one word aliased at offset 2", M_ALIAS, M_ALIAS, BI_CON_SER);
	run("hi-res card", M_RAM, M_HIGH, BI_CON_HR);
	run("low-res card", M_HIGH, M_RAM, BI_CON_LR);
	run("low-res card behind an echoing HR page", M_ECHO, M_RAM, BI_CON_LR);
	run("both answer: hi-res first", M_RAM, M_RAM, BI_CON_HR);
	if (fails)
		printf("vidtest: %d FAILED\n", fails);
	else
		printf("vidtest: all passed\n");
	return fails != 0;
}

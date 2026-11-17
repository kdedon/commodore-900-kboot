/*
 * vidtest.c -- the framebuffer probe, against memory that is not there.
 *
 * Like scctest.c, the probe is allowed to be wrong in ONE direction only.  A
 * card it misses hands the system the serial line, which is never unusable;
 * a card it invents hands the system a screen nobody can see.  So every empty
 * bus here -- pulled up, pulled down, still holding the last word driven onto
 * it, a device answering one constant, and a page whose offset 2 aliases
 * offset 0 -- must come out BI_CON_SER, and real RAM must be found and left
 * holding exactly what it held.  The same holds for the ID-probed cards on
 * the I/O bus, whose 8563 registers must also be left as found.
 *
 * src/vid.c reaches the machine through mapseg(), two word accessors and the
 * ROM's inb/outb.  Supplying those is the whole substitution: the file under
 * test is the shipped one.
 */
#include <stdio.h>
#include <string.h>

static unsigned	vget();
static void		vput();
static void		fakemap();
static int		busin();
static void		busout();

#define ROMABI_H		/* the fake I/O bus below stands in for the ROM */
#define inb(p)		busin((unsigned)(p))
#define outb(p, v)	busout((unsigned)(p), (int)(v))

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

#define IO_HIGH		0	/* nothing: reads 0xff */
#define IO_LOW		1	/* nothing: reads 0x00 */
#define IO_ECHO		2	/* nothing: the bus holds the last byte */
#define IO_LC		3	/* the 64 KB card: ID 0xc1 and a working 8563 */
#define IO_IDONLY	4	/* 0xc1 at the ID port, nothing behind it */
#define IO_BUSY		5	/* the card, but the 8563 never says ready */
#define IO_OTHERID	6	/* a working 8563 behind an ID not in the table */

#define P_HR	0
#define P_LR	1

static int		kind[2];
static unsigned	mem[2][2];
static int		page = -1;	/* -1: mapped somewhere not a framebuffer */
static int		nmap[2];
static int		badmap;		/* a segment or attribute not the probe's */
static unsigned	lastbus;
static int		fails;

static int		iokind;
static int		lastio;
static int		nio;
static int		vreg[64];
static int		vsel;

static int
chip()
{
	return (iokind == IO_LC || iokind == IO_BUSY || iokind == IO_OTHERID);
}

static int
floating()
{
	switch (iokind) {
	case IO_LOW:	return (0);
	case IO_ECHO:	return (lastio);
	}
	return (0xff);
}

static int
busin(port) unsigned port;
{
	nio++;
	if (port == 0x01ff) {
		switch (iokind) {
		case IO_LC: case IO_IDONLY: case IO_BUSY:	return (0xc1);
		case IO_OTHERID:				return (0xc3);
		}
	} else if (port == 0x0181 && chip())
		return (iokind == IO_BUSY ? 0x01 : 0x81);
	else if (port == 0x0183 && chip())
		return (vreg[vsel]);
	return (floating());
}

static void
busout(port, v) unsigned port; int v;
{
	nio++;
	lastio = v & 0xff;
	if (!chip())
		return;
	if (port == 0x0181)
		vsel = v & 0x3f;
	else if (port == 0x0183)
		vreg[vsel] = v & 0xff;
}

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
run(what, hr, lr, io, want) char *what; int hr, lr, io; unsigned want;
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
	iokind = io;
	lastio = 0xff;
	nio = 0;
	vreg[18] = 0x12;
	vreg[19] = 0x34;
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
	if (chip() && (vreg[18] != 0x12 || vreg[19] != 0x34)) {
		printf("  FAIL %-40s left the 8563 address changed\n", what);
		fails++;
	}
	if ((want == BI_CON_HR || want == BI_CON_LR) && nio != 0) {
		printf("  FAIL %-40s probed I/O after a framebuffer answered\n",
			what);
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
	run("no card, pulled up", M_HIGH, M_HIGH, IO_HIGH, BI_CON_SER);
	run("no card, pulled down", M_LOW, M_LOW, IO_HIGH, BI_CON_SER);
	run("no card, bus echoes the last word", M_ECHO, M_ECHO, IO_HIGH, BI_CON_SER);
	run("a constant answer", M_CONST, M_CONST, IO_HIGH, BI_CON_SER);
	run("one word aliased at offset 2", M_ALIAS, M_ALIAS, IO_HIGH, BI_CON_SER);
	run("hi-res card", M_RAM, M_HIGH, IO_HIGH, BI_CON_HR);
	run("low-res card", M_HIGH, M_RAM, IO_HIGH, BI_CON_LR);
	run("low-res card behind an echoing HR page", M_ECHO, M_RAM, IO_HIGH, BI_CON_LR);
	run("both answer: hi-res first", M_RAM, M_RAM, IO_HIGH, BI_CON_HR);
	run("LC: ID bus floats low", M_HIGH, M_HIGH, IO_LOW, BI_CON_SER);
	run("LC: ID bus echoes the last byte", M_HIGH, M_HIGH, IO_ECHO, BI_CON_SER);
	run("LC: ID with no 8563 behind it", M_HIGH, M_HIGH, IO_IDONLY, BI_CON_SER);
	run("LC: 8563 never ready", M_HIGH, M_HIGH, IO_BUSY, BI_CON_SER);
	run("LC: unknown ID", M_HIGH, M_HIGH, IO_OTHERID, BI_CON_SER);
	run("LC card", M_HIGH, M_HIGH, IO_LC, BI_CON_LC);
	run("LC card behind an echoing bus", M_ECHO, M_ECHO, IO_LC, BI_CON_LC);
	run("LC card and hi-res: hi-res first", M_RAM, M_HIGH, IO_LC, BI_CON_HR);
	run("LC card and low-res: low-res first", M_HIGH, M_RAM, IO_LC, BI_CON_LR);
	if (fails)
		printf("vidtest: %d FAILED\n", fails);
	else
		printf("vidtest: all passed\n");
	return fails != 0;
}

/*
 * con.c -- con.h on the real machine: a key that can be tested for, and a
 * clock, on each of the three consoles.  The only file in the loader that
 * names a console type or touches an I/O port.
 *
 * Keys come from the SCC on serial, and on video from COHERENT's keyboard
 * driver (kbd900.h), not the ROM's: the ROM's keyboard routines are what
 * Michal Pleban's web emulator does not serve.  Time comes from CT3 of
 * Z8036 CIO #1 at 100 Hz, programmed as the kernel's md.s programs it, with
 * the time constant chosen from the rom_ctype byte.  The counter's interrupt-pending bit is POLLED and no
 * interrupt is ever enabled, so a kernel inherits nothing armed.
 */
#include "romabi.h"
#include "con.h"
#include "kbd900.h"

/* Z8036 CIO #1; port = 2*register+1.  MCCR/MICR are read-modify-written,
 * never assigned: the ROM has the keyboard on port A there. */
#define MICR	0x01		/* master interrupt control */
#define MCCR	0x03		/* master configuration control */
#define CT3CS	0x19		/* CT3 command and status */
#define CT3CH	0x35		/* CT3 time constant, high */
#define CT3CL	0x37		/* CT3 time constant, low */
#define CT3MS	0x3d		/* CT3 mode select */

#define CT3_CONT	0x80	/* mode: continuous single cycle */
#define CT3_CT3E	0x10	/* MCCR: counter/timer 3 enabled */
#define CS_IP		0x20	/* command+status: interrupt pending */
#define CS_GATE		0x04	/*   gate command bit -- 0 STOPS the counter */
#define CS_TRIG		0x02	/*   trigger */
#define CS_CLRIP	0xa0	/* command 5: clear IP (leaves IUS, IE alone) */

/* 1/2 PCLK, divided to 100 Hz, per md.s CLKVAL0/CLKVAL1. */
#define CLK4MHZ	20000
#define CLK6MHZ	30000

/* The SCC, console channel, as getchar's serial path reads it. */
#define SCC_CS		0x0101	/* RR0: bit 0 = a received character is waiting */
#define SCC_DATA	0x0111
#define RR0_RXAVAIL	0x01

#define C_SER	0		/* serial SCC -- both ROM console flags clear */
#define C_VID	1		/* low-res or hi-res: the same CIO keyboard */

static int		ckind;
static unsigned	tk;		/* hundredths counted so far */
static int		esc;	/* how much of an ESC [ x sequence has arrived */

/* con.h: is the console a video board?  Answered from what coninit()
 * settled, so it names the console the menu was drawn on. */
int
convid()
{
	return (ckind == C_VID);
}

coninit()
{
	unsigned tc;
	char *rc;

	/* Which console the ROM chose.  LR and HR are one case: they differ in
	 * how they draw, not in how they are read. */
	ckind = C_SER;
	if (*(char *)ROMV_CONALT != 0 || *(char *)ROMV_CONHIRES != 0)
		ckind = C_VID;
	esc = 0;
	tk = 0;

	/* rom_ctype, byte 14 of romconf: 1 = 6 MHz.  Anything else reads as
	 * 4 MHz, which makes the countdown longer rather than shorter. */
	rc = *(char **)ROMV_CONFP;
	tc = (rc[ROMC_CTYPE] == 1) ? CLK6MHZ : CLK4MHZ;

	outb(CT3MS, CT3_CONT);
	outb(CT3CH, tc >> 8);
	outb(CT3CL, tc & 0xff);
	outb(CT3CS, CS_IP);				/* clear a stale IP/IUS */
	outb(MCCR, inb(MCCR) | CT3_CT3E);
	outb(CT3CS, CS_GATE | CS_TRIG);	/* gate open, start counting */

	/* Port A for the keyboard, every time, whatever the ROM did: our
	 * writes are the ROM kbd_init's mode less its vector and IE command,
	 * so a port the ROM already set up is set up the same.  Then the
	 * ROM's once-only flag, so a system that reads the keyboard through
	 * the ROM finds it programmed and does not program it again. */
	if (ckind == C_VID) {
		kbdinit();
		*(int *)ROMV_GETINIT = 1;
	}
}

/*
 * Hundredths since coninit(), counted as terminal-count wraps via the
 * interrupt-pending bit; the caller must poll oftener than every 10 ms.
 * CS_CLRIP carries CS_GATE because the gate bit is read/write here and a
 * clear-IP without it stops the clock.
 */
unsigned
contick()
{
	if (inb(CT3CS) & CS_IP) {
		outb(CT3CS, CS_CLRIP | CS_GATE);
		tk++;
	}
	return (tk);
}

/* One key from the hardware, or 0.  Never waits. */
static int
rawkey()
{
	int r;

	if (ckind == C_SER) {
		if ((inb(SCC_CS) & RR0_RXAVAIL) == 0)
			return (0);
		return (inb(SCC_DATA) & 0x7f);
	}
	/* kbd900.h: 0, or a 7-bit character; key-ups and modifiers give 0.
	 * An arrow is ESC and a queued letter.  The letter is discarded, so
	 * no editor line takes it: the video consoles cannot redraw, so the
	 * menu has no bar for an arrow to move (ui.c uibar). */
	r = kbdpoll();
	if (r == 0x1b)
		kbqi = kbqn;
	return (r);
}

/*
 * A key, or 0, with a serial arrow (ESC [ A) turned into K_UP/K_DOWN.  The
 * sequence is recognised across calls because this must not wait, so a bare
 * ESC costs the next key.  Nothing is decoded on the video consoles.
 */
int
conpoll()
{
	int c;

	c = rawkey();
	if (c == 0)
		return (0);
	if (ckind != C_SER)
		return (c);
	if (esc == 1) {
		esc = (c == '[') ? 2 : 0;
		return (0);
	}
	if (esc == 2) {
		esc = 0;
		if (c == 'A')
			return (K_UP);
		if (c == 'B')
			return (K_DOWN);
		return (0);
	}
	if (c == 0x1b) {
		esc = 1;
		return (0);
	}
	return (c);
}

int
conkey()
{
	int c;

	while ((c = conpoll()) == 0)
		;
	return (c);
}

/*
 * Give the current line back for rewriting: a bare CR, which the ROM passes
 * through.  Refused on the video consoles, where a lone CR is not
 * established; the caller degrades.
 */
int
conrew()
{
	if (ckind != C_SER)
		return (0);
	putchar('\r');
	return (1);
}

/*
 * ANSI, and only down the serial line: the ROM's video drivers write glyphs
 * and interpret no escape sequence, so an escape sent there would print.  The
 * serial side already requires a terminal that SENDS ESC [ A for the arrow
 * keys, so one that reads the same sequences back is the same terminal.
 */
static
conesc()
{
	putchar(0x1b);
	putchar('[');
}

/* Column 0, then up n lines.  n == 0 asks and moves nothing. */
int
conup(n) int n;
{
	if (ckind != C_SER)
		return (0);
	if (n > 0) {
		putchar('\r');
		conesc();
		cputn((unsigned long)n);
		putchar('A');
	}
	return (1);
}

int
conrev(on) int on;
{
	if (ckind != C_SER)
		return (0);
	conesc();
	putchar(on ? '7' : '0');
	putchar('m');
	return (1);
}

int
conclr()
{
	if (ckind != C_SER)
		return (0);
	conesc();
	putchar('K');
	return (1);
}

cputs(s) char *s;
{
	puts(s);
}

cputc(c) int c;
{
	putchar(c);
}

cputn(v) unsigned long v;
{
	char b[11];
	int n;

	n = 0;
	do {
		b[n++] = '0' + (int)(v % 10);
		v /= 10;
	} while (v != 0);
	while (--n >= 0)
		putchar(b[n]);
}

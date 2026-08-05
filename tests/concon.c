/*
 * concon.c -- con.h for a host: a scripted keyboard and a captured screen.
 *
 * This is the other implementation of the console interface, and having two is
 * the point of there being an interface.  The menu cannot tell it from the
 * real one, so the state machine under test is the shipped state machine and
 * not a copy of it.
 *
 * conrew() is settable, because the two real consoles differ in exactly that
 * answer and the countdown has a different shape on each side of it.  A test
 * that only ever ran with rewriting available would cover the serial console
 * and quietly not cover the other two.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/con.h"
#include "conhost.h"

static int		keys[256];
static int		nkeys, keyp;
static unsigned	tk, tkstep;
char			conout[8192];
static int		conlen;
int				con_canrew = 1;
int				con_starved;		/* conkey() called with the script empty */

/* Load the key script.  Characters are themselves; '\1' is K_UP, '\2' is
 * K_DOWN, and '.' would have been ambiguous so a pause is '\3' -- it yields no
 * key and lets the clock run, which is how a countdown is tested without a
 * wall clock anywhere in the loop. */
void
conscript(s) char *s;
{
	nkeys = keyp = 0;
	for (; *s; s++)
		keys[nkeys++] = (*s == '\1') ? K_UP : (*s == '\2') ? K_DOWN : *s;
	tk = 0;
	tkstep = 0;
	conlen = 0;
	conout[0] = '\0';
	con_starved = 0;
}

/* Hundredths that pass per poll of the console.  0 stops the clock, which is
 * how "the countdown must not run when timeout is 0" is tested. */
void
conrate(hundredths) unsigned hundredths;
{
	tkstep = hundredths;
}

coninit()
{
	tk = 0;
	return 0;
}

int
conpoll()
{
	tk += tkstep;
	if (keyp >= nkeys)
		return 0;
	if (keys[keyp] == '\7') { keyp++; return 0; }
	return keys[keyp++];
}

int
conkey()
{
	int c;
	int guard = 0;

	while ((c = conpoll()) == 0)
		if (++guard > 100000) {
			/* The one thing a real console has that a script does
			 * not: an end.  A menu asking for a key the script does
			 * not have would spin here for ever, and a suite that
			 * hangs is worse than one that fails. */
			con_starved = 1;
			printf("  FAIL the menu is waiting for a key the script "
			       "does not have.  Transcript:\n---\n%s\n---\n",
			       conout);
			exit(2);
		}
	return c;
}

int		conrew() { if (!con_canrew) return 0; cputc('\r'); return 1; }
unsigned	contick() { return tk; }

/* The redraw calls answer from the same flag: on the machine they all come
 * from the one question of whether this is the serial console, and a fake
 * that let them disagree would test a console that does not exist. */
int
conup(n) int n;
{
	char b[16];

	if (!con_canrew)
		return 0;
	if (n > 0) {
		sprintf(b, "\r\033[%dA", n);
		cputs(b);
	}
	return 1;
}

int		conrev(on) int on; { if (!con_canrew) return 0; cputs(on ? "\033[7m" : "\033[0m"); return 1; }
int		conclr() { if (!con_canrew) return 0; cputs("\033[K"); return 1; }

cputs(s) char *s;
{
	while (*s)
		cputc(*s++);
	return 0;
}

cputc(c) int c;
{
	if (conlen < (int)sizeof conout - 1) {
		conout[conlen++] = (char)c;
		conout[conlen] = '\0';
	}
	return 0;
}

cputn(v) unsigned long v;
{
	char b[24];
	sprintf(b, "%lu", v);
	cputs(b);
	return 0;
}

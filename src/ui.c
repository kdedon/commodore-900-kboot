/*
 * ui.c -- the menu: what is on the screen and what the keys do.  It reaches
 * the machine only through con.h, so tests/uitest.c runs the same state
 * machine on a host against a scripted keyboard.
 */
#include "kboot.h"
#include "con.h"

/* Whether the console can redraw a block it has already printed.  A moving
 * selection is offered only where it can: reprinting the menu under its last
 * copy for every keystroke is worse than not moving at all. */
static int uibar;
static int uilines;		/* lines the block last drawn occupies */

/* Print one menu line: "  1) label", with the current entry marked and the
 * boot flags it carries named after it. */
static
uient(i, cur) int i, cur;
{
	char fb[FLGSZ];

	if (i == cur)
		conrev(1);
	cputs(i == cur ? "> " : "  ");
	cputc('0' + i + 1);
	cputs(") ");
	cputs(oslist[i].label);
	bflabel(i, fb, FLGSZ);
	if (fb[0] != '\0') {
		cputs("  ");
		cputs(fb);
	}
	if (oslist[i].badflg)
		cputs("  ** unknown flag: cannot boot **");
	if (i == cur)
		conrev(0);
	conclr();
	cputs("\n");
}

/* Draw the menu and its legend, and leave the cursor on the line below it.
 * The legend names the movement keys only where they work. */
static
uilist(cur) int cur;
{
	int i;

	for (i = 0; i < nos; i++)
		uient(i, cur);
	cputs(uibar ? "[1-9] boot  [j/k] move  [enter] boot selected\n"
		    : "[1-9] boot  [enter] boot the marked entry\n");
	cputs("[e] edit entry  [r] recovery\n");
	uilines = nos + 2;
}

/* Move the mark without reprinting: back up over the block and draw it again
 * in place.  Only ever called where uibar said it works. */
static
uimove(cur) int cur;
{
	conup(uilines);
	uilist(cur);
}

/* The prompt, on the line below the block.  It takes that line back where the
 * console allows it, so the countdown it replaces leaves nothing behind. */
static
uiprompt()
{
	if (conrew() == 0)
		cputs("\n");
	cputs("boot> ");
	conclr();
}

/* The countdown line, rewritten in place once a second, or degraded to a dot
 * a second on a console that cannot give the line back.  It ends in "> "
 * because it is a prompt. */
static
uicline(k, secs, first) int k; unsigned secs; int first;
{
	if (!first && conrew() == 0) {
		cputc('.');
		return (0);
	}
	cputs("boot ");
	cputc('0' + k + 1);
	cputs(" in ");
	cputn((unsigned long)secs);
	cputs("> ");
	conclr();
	return (0);
}

/* Wait out the countdown.  Returns the key that stopped it, or 0 if it ran
 * out.  Any key stops it, and it never resumes. */
static int
uicount(k) int k;
{
	unsigned t0, e, shown;
	int c;

	t0 = contick();
	shown = 0;
	uicline(k, (unsigned)cfgwait, 1);
	for (;;) {
		e = (contick() - t0) / 100;		/* whole seconds */
		if (e >= (unsigned)cfgwait)
			return (0);
		if (e != shown) {
			shown = e;
			uicline(k, (unsigned)cfgwait - e, 0);
		}
		if ((c = conpoll()) != 0)
			return (c);
	}
}

/* ------------------------------------------------------------------------ *
 * The per-entry editor.  It edits the in-memory entry only: kboot has no
 * write path, and the editor screen says so.
 * ------------------------------------------------------------------------ */

/*
 * Read one line, echoing it here (conkey() does not echo, so backspace
 * works).  Returns the length, -1 if cancelled with ^C, 0 for an empty line
 * meaning "leave it alone".  ESC is not a cancel: on the serial console it
 * starts an arrow sequence.
 */
static int
uiline(buf, max) char *buf; int max;
{
	int n, c;

	n = 0;
	for (;;) {
		c = conkey();
		if (c == '\r' || c == '\n') {
			buf[n] = '\0';
			cputs("\n");
			return (n);
		}
		if (c == 0x03) {			/* ^C */
			cputs(" -- unchanged\n");
			return (-1);
		}
		if (c == '\b' || c == 0x7f) {
			if (n > 0) {
				n--;
				cputs("\b \b");
			}
			continue;
		}
		if (c < ' ' || c > '~' || n >= max - 1)
			continue;
		buf[n++] = (char)c;
		cputc(c);
	}
}

/* Decimal from a typed line; cfg.c's atoloff reads the config buffer. */
static unsigned long
uiatol(s) register char *s;
{
	unsigned long v;

	v = 0;
	while (*s >= '0' && *s <= '9')
		v = v * 10 + (*s++ - '0');
	return (v);
}

/* Prompt for one field.  Returns the length typed, 0 for "unchanged". */
static int
uifield(what, buf, max) char *what, *buf; int max;
{
	int n;

	cputs(what);
	cputs("> ");
	n = uiline(buf, max);
	return (n < 0 ? 0 : n);
}

/*
 * The field screen, for both the editor and recovery mode.  `rec' changes
 * what enter means and nothing else:
 *
 *	editor    enter and `x' both leave
 *	recovery  enter BOOTS this entry, `x' goes back to the menu
 *
 * Returns -1 to leave, or the index to boot.
 */
static int
uiedit(k, rec) int k, rec;
{
	char buf[LBLSZ], fb[FLGSZ];
	char *err;
	unsigned sb;
	int c;

	/* The bit THIS entry's vocabulary calls SINGLE; an entry that names no
	 * such flag falls back to the one the handoff header fixes. */
	sb = bfbit(k, -1, "SINGLE");
	if (sb == 0)
		sb = BF_SINGLE;
	for (;;) {
		cputs(rec ? "\nrecovery " : "\nedit ");
		cputc('0' + k + 1);
		cputs(")\n  a) label  ");
		cputs(oslist[k].label);
		cputs("\n  b) base   ");
		cputn(oslist[k].base);
		cputs("\n  c) file   ");
		cputs(oslist[k].file);
		cputs("\n  d) table  ");
		cputs(oslist[k].wantbi ? "yes" : "no");
		if (oslist[k].lay != LAYGLOB)
			cputs(" (not the default layout)");
		cputs("\n  e) single ");
		cputs((oslist[k].bflags & sb) ? "yes" : "no");
		bflabel(k, fb, FLGSZ);
		if (fb[0] != '\0') {
			cputs("   flags ");
			cputs(fb);
		}
		if (oslist[k].badflg)
			cputs("   ** unknown flag: cannot boot **");
		cputs(rec ? "\n[a-e] change  [v] verify  [enter] boot it  [x] menu"
			    "   (this boot only: kboot cannot write the disk)\n"
			    "recovery> "
			  : "\n[a-e] change  [v] verify  [x] done"
			    "   (this boot only: kboot cannot write the disk)\n"
			    "edit> ");
		c = conkey();
		cputc(c);
		cputs("\n");
		if (c == '\r' || c == '\n')
			return (rec ? k : -1);
		if (c == 'x' || c == 'X')
			return (-1);
		if (c == 'a') {
			if (uifield("label", buf, LBLSZ))
				copyname(oslist[k].label, buf, LBLSZ);
		} else if (c == 'b') {
			/* Verified on the way out: the field most worth
			 * catching a typo in. */
			if (uifield("base block", buf, LBLSZ)) {
				oslist[k].base = uiatol(buf);
				goto check;
			}
		} else if (c == 'c') {
			if (uifield("kernel file", buf, DIRSIZ)) {
				copyname(oslist[k].file, buf, DIRSIZ);
				goto check;
			}
		} else if (c == 'd') {
			/* The partition-table handoff.  Off is a
			 * configuration: the kernel runs on its own. */
			oslist[k].wantbi = !oslist[k].wantbi;
		} else if (c == 'e') {
			/* Come up single user, before /etc/rc runs. */
			oslist[k].bflags ^= sb;
		} else if (c == 'v') {
check:
			err = osverify(k);
			cputs(err == 0 ? "verify: a kernel is there\n"
					   : "verify: ");
			if (err != 0) {
				cputs(err);
				cputs("\n");
			}
		}
	}
}

/*
 * Recovery mode: seed a scratch entry from entry `from', let any field of it
 * be fixed and verified, and boot it once.  The scratch slot is the one past
 * the parsed entries, so the entry it started from is not damaged.
 */
static int
uirecov(from) int from;
{
	int r;

	r = nos > MAXOS ? MAXOS : nos;
	copyname(oslist[r].label, "recovery", LBLSZ);
	copyname(oslist[r].file, oslist[from].file, DIRSIZ);
	oslist[r].base = oslist[from].base;
	oslist[r].wantbi = oslist[from].wantbi;
	oslist[r].lay = oslist[from].lay;
	oslist[r].voc = oslist[from].voc;
	oslist[r].bflags = oslist[from].bflags;
	/* The copy carries the flags that RESOLVED, and not the refusal: a
	 * name the config got wrong is what recovery is here to get past. */
	oslist[r].badflg = 0;
	return (uiedit(r, 1));
}

/* Choose an entry and return its index.  One OS and no `timeout' boots with
 * no menu at all -- unless that entry cannot be booted, which is the one case
 * that must reach a screen.  `nocount' suppresses the countdown, for the
 * second time round after a refusal. */
static int
uipick(nocount) int nocount;
{
	int k, c, i;

	uibar = conup(0);

	/* A config that says nothing bootable goes straight to recovery, past
	 * the fast path and with no countdown. */
	if (cfgfault) {
		cputs("kboot: entering recovery.\n");
		i = uirecov(0);
		if (i >= 0)
			return (i);
	}
	if (nos == 1 && cfgwait <= 0 && oslist[0].badflg == 0)
		return (0);

	/* `default' is 1-based; out of range is ignored, not clamped. */
	k = (cfgdflt >= 1 && cfgdflt <= nos) ? cfgdflt - 1 : 0;
	uilist(k);

	c = 0;
	if (cfgwait > 0 && !nocount) {
		c = uicount(k);
		/* Nobody was there.  An entry that cannot boot does not become
		 * bootable by nobody being there: fall through to the prompt. */
		if (c == 0 && oslist[k].badflg == 0) {
			cputs("\n");
			return (k);
		}
	}
	for (;;) {
		if (c == 0) {
			uiprompt();
			c = conkey();
		}
		/* A digit boots that entry outright. */
		i = c - '1';
		if (i >= 0 && i < nos) {
			cputc(c);
			cputs("\n");
			return (i);
		}
		if (c == '\r' || c == '\n') {
			cputs("\n");
			return (k);
		}
		/* j/k and space move too: arrows decode on serial only. */
		if (uibar && (c == K_UP || c == 'k' || c == 'p')) {
			k = (k == 0) ? nos - 1 : k - 1;
			uimove(k);
		} else if (uibar && (c == K_DOWN || c == 'j' || c == 'n'
				     || c == ' ')) {
			k = (k == nos - 1) ? 0 : k + 1;
			uimove(k);
		} else if (c == 'e') {
			cputs("\n");
			uiedit(k, 0);
			uilist(k);
		} else if (c == 'r') {
			cputs("\n");
			i = uirecov(k);
			if (i >= 0)
				return (i);
			uilist(k);
		} else {
			c = 0;
			continue;
		}
		c = 0;
	}
}

/*
 * The chosen entry.  One whose `flags' line named something kboot.cfg does not
 * define is handed back to the menu instead of booted: the flag was asked for
 * and cannot be delivered, and a boot without it is indistinguishable from the
 * boot that was wanted.  Recovery builds a copy without the refusal.
 */
int
uimenu()
{
	int k, again;

	for (again = 0; ; again = 1) {
		k = uipick(again);
		if (oslist[k].badflg == 0)
			return (k);
		cputs("kboot: that entry asks for a boot flag kboot.cfg does not\n"
		      "define, so it will not be booted.  [r] copies it without one.\n");
	}
}

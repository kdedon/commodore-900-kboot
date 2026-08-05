/*
 * uitest.c -- the menu state machine, on a host.
 *
 * This runs src/ui.c unmodified against tests/concon.c's fake console, so
 * what is under test is the shipped code and not a model of it.  The clock is
 * driven by polling rather than by waiting, which is what makes a five-second
 * countdown testable in microseconds -- and is also the rule the real
 * harnesses must follow, because no clock in this project runs at wall speed.
 *
 * What matters here is mostly the NEGATIVE space: that the countdown does not
 * run when the config did not ask for one, that a key stops it and it stays
 * stopped, that a default naming a missing entry does not silently boot its
 * neighbour, and that the video console -- which cannot rewrite a line -- still
 * gets a countdown rather than an unusable one.
 */
#include <stdio.h>
#include <string.h>
#include "../src/kboot.h"
#include "../src/con.h"
#include "conhost.h"

static int fails;

/* The one thing the menu asks the ROM side for.  Here it answers from a table
 * the test sets, and it RECORDS what it was asked -- because "verify said no"
 * is only worth anything if it verified the entry the operator had just
 * edited, and a stub that ignores its argument would pass either way. */
int				vf_calls;
unsigned long	vf_base;		/* the base osverify() last saw */
char			*vf_answer;		/* what it will say */

char *
osverify(k) int k;
{
	vf_calls++;
	vf_base = oslist[k].base;
	return vf_answer;
}

static void
check(char *what, long got, long want)
{
	if (got != want) {
		printf("  FAIL %-40s got %ld want %ld\n", what, got, want);
		fails++;
	}
}

static void
has(char *what, char *needle)
{
	if (strstr(conout, needle) == 0) {
		printf("  FAIL %-40s no \"%s\" in:\n---\n%s\n---\n", what, needle, conout);
		fails++;
	}
}

static void
hasnt(char *what, char *needle)
{
	if (strstr(conout, needle) != 0) {
		printf("  FAIL %-40s unwanted \"%s\" in:\n---\n%s\n---\n", what, needle, conout);
		fails++;
	}
}

/* How many times something was printed.  The menu is allowed to draw itself
 * again; what it is not allowed to do is draw itself again UNDER the last
 * copy, and only a count can tell those apart. */
static void
times(char *what, char *needle, int want)
{
	int n;
	char *p;

	for (n = 0, p = conout; (p = strstr(p, needle)) != 0; n++, p++)
		;
	if (n != want) {
		printf("  FAIL %-40s \"%s\" %d times, want %d in:\n---\n%s\n---\n",
		       what, needle, n, want, conout);
		fails++;
	}
}

/* Build an OS list directly.  cfgtest covers the parser; this covers the menu,
 * and coupling the two would make every menu case pay for a config text. */
static void
setup(n, wait, dflt) int n, wait, dflt;
{
	int i;
	static char *names[] = { "OpenCoherent-3.5", "CPM-8000", "COHERENT-0.8",
				 "d", "e", "f", "g", "h" };

	nos = n;
	cfgwait = wait;
	cfgdflt = dflt;
	for (i = 0; i < n; i++) {
		strcpy(oslist[i].label, names[i]);
		strcpy(oslist[i].file, "kernel");
		oslist[i].base = 100 * (i + 1);
		oslist[i].wantbi = 0;
		oslist[i].voc = LAYGLOB;
		oslist[i].bflags = 0;
		oslist[i].badflg = 0;
	}
	con_canrew = 1;
	cfgfault = 0;
	conrate(0);
}

/* ---------------------------------------------------------------- */

static void
t_digit()
{
	printf("picking with a digit\n");
	setup(3, 0, 0);
	conscript("2");
	check("digit 2 -> entry 1", uimenu(), 1);
	has("menu listed entry 3", "3) COHERENT-0.8");
	has("prompt printed", "boot> ");
	check("no hang", con_starved, 0);

	/* Out of range, then in range.  The loop must re-prompt rather than
	 * boot something, and must not fall off the end of the list. */
	setup(2, 0, 0);
	conscript("9z0 1");
	check("junk then 1 -> entry 0", uimenu(), 0);
	check("no hang", con_starved, 0);
}

static void
t_fastpath()
{
	printf("the single-entry fast path\n");
	setup(1, 0, 0);
	conscript("");
	check("one entry, no timeout -> boots it", uimenu(), 0);
	hasnt("...with no menu at all", "boot");
	check("...and no key was needed", con_starved, 0);

	/* One entry WITH a timeout is a different request: the operator asked
	 * for a window, so there is a menu and a countdown. */
	setup(1, 3, 0);
	conscript("");
	conrate(10);
	check("one entry with a timeout -> boots it", uimenu(), 0);
	has("...but shows the menu", "1) OpenCoherent-3.5");
	has("...and counts down", "boot 1 in 3> ");
}

static void
t_timeout()
{
	printf("the countdown\n");
	/* No timeout in the config: the clock may run, and nothing may happen.
	 * This is the case an unattended machine used to hang on, and the fix
	 * must not turn into booting a machine nobody configured to boot. */
	setup(2, 0, 0);
	conscript("\7\7\7\7\7\7\7\7\7\7\7\7\7\7\7\7\7\7\7\7" "1");
	conrate(100);
	check("no `timeout' -> waits for a key", uimenu(), 0);
	hasnt("...and prints no countdown", " in ");

	/* Five seconds, nobody there.  The default entry boots. */
	setup(3, 5, 2);
	conscript("");
	conrate(37);				/* an awkward rate: no tick lands on a second */
	check("expiry boots `default'", uimenu(), 1);
	has("counted from 5", "boot 2 in 5> ");
	has("...down to 1", "boot 2 in 1> ");
	check("no hang", con_starved, 0);

	/* A key stops it, and it does not resume: after the key the menu waits
	 * for a choice however long the clock runs. */
	setup(3, 5, 2);
	conscript("x\7\7\7\7\7\7\7\7\7\7\7\7\7\7\7\7\7\7\7\7\7\7\7\7\7\7\7\7\7\7" "3");
	conrate(100);
	check("a key cancels, then 3 -> entry 2", uimenu(), 2);
	check("no hang", con_starved, 0);

	/* The key that cancels can itself be the choice.  Losing it would mean
	 * a user who typed "2" during the countdown had to type it again. */
	setup(3, 5, 1);
	conscript("2");
	conrate(100);
	check("a digit cancels AND chooses", uimenu(), 1);
}

static void
t_navigate()
{
	printf("moving the selection\n");
	/* Enter boots what is marked, and the mark starts on `default'. */
	setup(3, 0, 2);
	conscript("\r");
	check("enter boots the marked entry", uimenu(), 1);
	has("the mark is on entry 2", "> 2) CPM-8000");
	has("entry 1 is not marked", "  1) OpenCoherent-3.5");

	setup(3, 0, 0);
	conscript("jj\r");
	check("down down enter -> entry 2", uimenu(), 2);

	setup(3, 0, 0);
	conscript("\2\2\1\r");
	check("arrows move too", uimenu(), 1);

	/* Wrapping, in both directions.  A three-entry menu where `up' at the
	 * top does nothing is a menu whose last entry needs two keys more than
	 * its first. */
	setup(3, 0, 0);
	conscript("k\r");
	check("up from the top wraps to the end", uimenu(), 2);
	setup(3, 0, 0);
	conscript("jjj\r");
	check("down past the end wraps to 0", uimenu(), 0);

	/* Space moves; it is the key people press when nothing is labelled. */
	setup(3, 0, 0);
	conscript(" \r");
	check("space moves down", uimenu(), 1);

	/* A digit still wins outright, wherever the mark happens to be. */
	setup(3, 0, 0);
	conscript("jj3");
	check("a digit boots that entry", uimenu(), 2);
	setup(3, 0, 0);
	conscript("jj1");
	check("...even against the mark", uimenu(), 0);

	/* An unknown key must re-prompt, not move and not boot. */
	setup(3, 0, 0);
	conscript("Zq\r");
	check("unknown keys do not move the mark", uimenu(), 0);

	/* The legend has to name the keys, or they do not exist. */
	setup(2, 0, 0);
	conscript("\r");
	uimenu();
	has("legend names the digits", "[1-9] boot");
	has("legend names the movement keys", "[j/k] move");
	has("legend names enter", "[enter] boot selected");

	/* Moving redraws the block where it stands.  The block is 3 entries
	 * plus 2 legend lines, so the cursor goes back 5. */
	setup(3, 0, 0);
	conscript("j\r");
	uimenu();
	has("moving backs the cursor over the block", "\033[5A");
	has("the mark is a reversed bar", "\033[7m> 2) CPM-8000");
	has("...and the bar is closed again", "\033[0m");
	times("one redraw, not a second copy printed below",
	      "1) OpenCoherent-3.5", 2);

	/* The countdown line becomes the prompt line rather than scrolling it
	 * away, so the block above it is still the block that was drawn. */
	setup(3, 5, 1);
	conscript("x\r");
	conrate(100);
	uimenu();
	times("the countdown does not cost a redraw", "1) OpenCoherent-3.5", 1);
}

static void
t_nobar()
{
	printf("a console that cannot redraw a block\n");
	/* The video consoles interpret no escape sequence, so there is no way
	 * to move a mark.  Movement is then not offered at all: reprinting the
	 * menu under its last copy for every keystroke is the behaviour this
	 * replaced. */
	setup(3, 0, 0);
	con_canrew = 0;
	conscript("jk\r");
	check("j and k do not move", uimenu(), 0);
	times("...and prints no second menu", "1) OpenCoherent-3.5", 1);
	hasnt("the legend does not name keys that do nothing", "[j/k] move");
	has("...and says what enter boots", "[enter] boot the marked entry");
	hasnt("no escape sequence reaches a console that cannot read one",
	      "\033[");
	check("no hang", con_starved, 0);

	/* Everything else still works there: digits, the editor, recovery. */
	setup(3, 0, 0);
	con_canrew = 0;
	conscript("3");
	check("a digit still boots", uimenu(), 2);
	setup(3, 0, 0);
	con_canrew = 0;
	vf_answer = 0;
	conscript("ex" "\r");
	check("the editor still returns to a working menu", uimenu(), 0);
	check("no hang", con_starved, 0);
}

static void
t_default()
{
	printf("`default'\n");
	setup(3, 2, 0);
	conscript("");
	conrate(100);
	check("no default -> entry 0", uimenu(), 0);

	setup(3, 2, 3);
	conscript("");
	conrate(100);
	check("default 3 -> entry 2", uimenu(), 2);
	has("countdown names entry 3", "boot 3 in ");

	/* Out of range.  NOT clamped to the last entry: a `default 9' on a
	 * three-entry disk is an unfinished edit, and booting entry 3 because
	 * it is nearest is how the wrong kernel gets booted quietly. */
	setup(3, 2, 9);
	conscript("");
	conrate(100);
	check("default 9 of 3 -> entry 0", uimenu(), 0);

	setup(3, 2, 0);
	conscript("");
	conrate(100);
	check("default 0 -> entry 0", uimenu(), 0);
}

static void
t_editor()
{
	printf("the per-entry editor\n");

	/* Change a kernel filename and boot the edited entry.  The point of the
	 * whole feature: a wrong config becomes a few keystrokes rather than a
	 * rescue floppy. */
	setup(2, 0, 0);
	vf_calls = 0; vf_answer = 0;
	conscript("je" "c" "coherent.old\r" "x\r");
	check("edit then enter -> the marked entry", uimenu(), 1);
	check("file was changed", strcmp(oslist[1].file, "coherent.old"), 0);
	check("...and verified on the way out", vf_calls, 1);
	has("editor named the entry", "edit 2)");
	has("editor says it cannot save", "kboot cannot write the disk");

	/* A base is the dangerous field.  It must be verified WITHOUT being
	 * asked to, and the verify must be of the number just typed. */
	setup(2, 0, 0);
	vf_calls = 0; vf_answer = "no filesystem at that block";
	conscript("eb" "23808\r" "x\r");
	uimenu();
	check("base was taken", (long)oslist[0].base, 23808);
	check("base is verified unprompted", vf_calls, 1);
	check("...against the typed number", (long)vf_base, 23808);
	has("and the refusal is shown", "verify: no filesystem at that block");

	/* An empty line leaves the field alone -- the answer to having pressed
	 * a letter by mistake.  It must not blank the label. */
	setup(2, 0, 0);
	vf_calls = 0; vf_answer = 0;
	conscript("ea\r" "x\r");
	uimenu();
	check("empty line leaves the label", strcmp(oslist[0].label, "OpenCoherent-3.5"), 0);

	/* ^C cancels a field, and the old value stands. */
	setup(2, 0, 0);
	conscript("ea" "zzz\003" "x\r");
	uimenu();
	check("^C leaves the label", strcmp(oslist[0].label, "OpenCoherent-3.5"), 0);
	has("...and says so", "unchanged");

	/* Backspace.  The editor echoes itself, so if it does not implement one
	 * there is no other. */
	setup(2, 0, 0);
	conscript("ea" "abX\bc\r" "x\r");
	uimenu();
	check("backspace erased a character", strcmp(oslist[0].label, "abc"), 0);

	/* The table flag is a toggle and cannot fail. */
	setup(2, 0, 0);
	oslist[0].wantbi = 0;
	conscript("ed" "x\r");
	uimenu();
	check("d toggles the table flag on", oslist[0].wantbi, 1);
	setup(2, 0, 0);
	oslist[0].wantbi = 1;
	conscript("ed" "x\r");
	uimenu();
	check("...and off", oslist[0].wantbi, 0);

	/* The single-user boot flag, likewise -- and INDEPENDENT of the table
	 * flag.  They travel in one block and are decided separately, which is
	 * what lets an entry carrying no table still ask for single user; an
	 * editor that tied them would undo that in the one screen an operator
	 * uses it from. */
	setup(2, 0, 0);
	oslist[0].wantbi = 0;
	oslist[0].bflags = 0;
	conscript("ee" "x\r");
	uimenu();
	check("e sets the single-user flag", (long)oslist[0].bflags, BF_SINGLE);
	check("...without asking for the table", oslist[0].wantbi, 0);
	has("and the screen says so", "e) single yes");
	conscript("ee" "x\r");
	uimenu();
	check("...and toggles it off", (long)oslist[0].bflags, 0);

	/* Recovery inherits it, because recovery is what it is for: the entry
	 * recovery builds is seeded from the one it was entered on, and a flag
	 * that did not travel would have to be set twice. */
	setup(2, 0, 0);
	oslist[0].bflags = 0;
	vf_answer = 0;
	conscript("ee" "x" "r" "\r");
	check("recovery boots its own slot", uimenu(), 2);
	check("recovery inherited the flag", (long)oslist[2].bflags, BF_SINGLE);

	/* An over-long label is truncated in the editor as it is in the parser,
	 * and does not run into the next field. */
	setup(2, 0, 0);
	conscript("ea" "0123456789012345678901234567\r" "x\r");
	uimenu();
	check("label truncated to LBLSZ-1",
	      (long)strlen(oslist[0].label), LBLSZ - 1);
	check("the next entry is untouched", strcmp(oslist[1].label, "CPM-8000"), 0);

	/* [v] on its own verifies without changing anything. */
	setup(2, 0, 0);
	vf_calls = 0; vf_answer = 0;
	conscript("ev" "x\r");
	uimenu();
	check("v verifies", vf_calls, 1);
	has("and reports success", "verify: a kernel is there");

	/* Leaving the editor returns to the menu, which is still navigable and
	 * still boots.  An editor that swallowed the menu would be a one-way
	 * trip on a machine with no other console. */
	setup(3, 0, 0);
	vf_answer = 0;
	conscript("ex" "jj" "\r");
	check("x returns to a working menu", uimenu(), 2);
	check("no hang", con_starved, 0);

	/* The editor has to be findable. */
	setup(2, 0, 0);
	conscript("\r");
	uimenu();
	has("legend names the editor", "[e] edit");
}

static void
t_recovery()
{
	printf("recovery mode\n");

	/* `r' from the menu.  It seeds from the marked entry, so the operator
	 * starts from something close to right rather than from nothing. */
	setup(3, 0, 0);
	cfgfault = 0;
	vf_answer = 0;
	conscript("jr" "\r");
	check("recovery boots its own slot", uimenu(), 3);
	has("named as recovery", "recovery ");
	check("seeded from the marked entry", (long)oslist[3].base, 200);

	/* It must not damage the entry it started from.  Somebody trying bases
	 * until one verifies has to be able to go back to the menu and find
	 * their disk as it was. */
	setup(2, 0, 0);
	cfgfault = 0;
	vf_answer = 0;
	conscript("r" "b" "99999\r" "x" "1");
	check("x from recovery -> the menu still works", uimenu(), 0);
	check("the original entry is untouched", (long)oslist[0].base, 100);
	check("...and its file too", strcmp(oslist[0].file, "kernel"), 0);

	/* A CORRUPT config goes to recovery on its own, with no countdown --
	 * the fault is precisely that we no longer know what the default is. */
	setup(1, 5, 1);
	cfgfault = 1;
	vf_answer = 0;
	conscript("\r");
	conrate(100);
	check("a fault boots the recovery entry", uimenu(), 1);
	has("says it is entering recovery", "kboot: entering recovery");
	hasnt("and does NOT count down", " in 5> ");

	/* A fault must not take the single-entry fast path either: that path
	 * boots with no menu at all, which is the one thing a damaged config
	 * must never do. */
	setup(1, 0, 0);
	cfgfault = 1;
	vf_answer = 0;
	conscript("\r");
	check("a fault overrides the fast path", uimenu(), 1);
	has("...and shows recovery", "recovery 2)");

	/* Leaving recovery on a fault falls through to the normal menu rather
	 * than looping: the operator may have decided the defaults are fine. */
	setup(2, 0, 0);
	cfgfault = 1;
	vf_answer = 0;
	conscript("x" "2");
	check("x from a fault falls through to the menu", uimenu(), 1);

	/* Verify works in recovery, and the refusal is visible.  Recovery
	 * without verify is typing numbers into the dark. */
	setup(2, 0, 0);
	cfgfault = 0;
	vf_calls = 0; vf_answer = "no filesystem at that block";
	conscript("r" "b" "77\r" "x" "1");
	uimenu();
	check("recovery verified the typed base", (long)vf_base, 77);
	has("and refused it", "verify: no filesystem at that block");

	/* Recovery at the MAXOS bound.  Its slot is the one PAST the parsed
	 * entries, so a full menu is the case where that slot is the last
	 * element of the array and an off-by-one runs off the end of it. */
	setup(MAXOS, 0, 0);
	cfgfault = 0;
	vf_answer = 0;
	conscript("r" "\r");
	check("recovery slot at a full menu", uimenu(), MAXOS);
	check("the last parsed entry is untouched",
	      strcmp(oslist[MAXOS-1].label, "h"), 0);

	setup(2, 0, 0);
	conscript("\r");
	uimenu();
	has("legend names recovery", "[r] recovery");
}

static void
t_videoconsole()
{
	printf("a console that cannot rewrite a line\n");
	/* The low-res and hi-res consoles reach a driver whose handling of a
	 * bare CR is not established, so con.c refuses to rewrite there.  The
	 * countdown must still exist and must still expire -- the degradation
	 * is in how it LOOKS, never in whether it works. */
	setup(2, 4, 2);
	con_canrew = 0;
	conscript("");
	conrate(100);
	check("expiry still boots `default'", uimenu(), 1);
	has("the line was printed once", "boot 2 in 4> ");
	has("then dots", "...");
	hasnt("and never a carriage return", "\r");
}

/* Build the list from a config TEXT instead of by hand: what the menu shows
 * about flags has to agree with the file that set them, and a hand-built list
 * would agree with itself. */
static void
setupcfg(text) char *text;
{
	memset(cfg, 0, CFGMAX);
	memcpy(cfg, text, strlen(text));
	cfglen = CFGMAX;
	cfgparse();
	con_canrew = 1;
	cfgfault = 0;
	conrate(0);
	vf_answer = 0;
}

static void
t_flags()
{
	printf("boot flags from the config\n");

	/* What the file set, the menu shows.  Before booting it there is no
	 * other way to tell a single-user entry from an ordinary one. */
	setupcfg("bflag SINGLE 1\nbflag QUICK 16\n"
		 "os Rescue 136 coherent\nflags SINGLE\n"
		 "os Normal 900 coherent\n");
	conscript("1");
	check("the entry boots", uimenu(), 0);
	has("the menu names the flag", "1) Rescue  SINGLE");
	hasnt("...and not on the entry without it", "Normal  SINGLE");
	check("...and the bit reached the entry", (long)oslist[0].bflags, 1);

	/* The editor agrees with the file, and its toggle moves the bit THIS
	 * entry's vocabulary calls SINGLE -- not whatever bit the loader was
	 * built believing in. */
	setupcfg("bflag SINGLE 4\nos A 136 coherent\nflags SINGLE\n"
		 "os B 900 coherent\n");
	conscript("e" "x" "1");
	uimenu();
	has("the editor shows it set", "e) single yes");
	setupcfg("bflag SINGLE 4\nos A 136 coherent\nos B 900 coherent\n");
	conscript("ee" "x" "1");
	uimenu();
	check("the toggle uses the config's bit", (long)oslist[0].bflags, 4);

	/* AN UNKNOWN NAME IS NOT BOOTED.  Booting it anyway would come up
	 * multi-user and look exactly like the boot that was asked for. */
	setupcfg("bflag SINGLE 1\nos A 136 coherent\nflags SNGLE\n"
		 "os B 900 coherent\n");
	conscript("1" "2");
	check("the refused entry is not booted", uimenu(), 1);
	has("the menu marks it", "** unknown flag: cannot boot **");
	has("...and says why", "kboot: that entry asks for a boot flag");

	/* Not by the single-entry fast path either, which boots with no menu
	 * at all.  Recovery is the way past it. */
	setupcfg("bflag SINGLE 1\nos A 136 coherent\nflags SNGLE\n");
	conscript("r" "\r");
	check("recovery boots instead", uimenu(), 1);
	check("...and its copy carries no refusal", oslist[1].badflg, 0);
}

int
main()
{
	printf("uitest: the menu state machine\n");
	t_digit();
	t_fastpath();
	t_timeout();
	t_navigate();
	t_default();
	t_editor();
	t_recovery();
	t_videoconsole();
	t_nobar();
	t_flags();
	if (fails)
		printf("uitest: %d FAILED\n", fails);
	else
		printf("uitest: all passed\n");
	return fails != 0;
}

/*
 * scctest.c -- the serial-channel probe, against buses that are not there.
 *
 * The probe's whole job is to be WRONG IN ONE DIRECTION ONLY.  A channel it
 * misses costs the operating system a line it could have offered; a channel it
 * invents costs the operating system a getty writing into an address that
 * nothing answers, forever, with no error to show for it.  So the cases here
 * are the empty bus in every shape a real one takes -- pulled up, pulled down,
 * still holding the last byte anybody drove onto it, and a device that answers
 * some constant to everything -- and each of them must come out ZERO.
 *
 * src/scc.c reaches the machine through exactly two ROM calls, inb and outb.
 * Claiming romabi.h's include guard here and supplying those two is the whole
 * substitution: the file under test is then the shipped one, compiled as it
 * ships, with a fake bus underneath it.
 */
#include <stdio.h>
#include <string.h>

#define ROMABI_H		/* keep the real ROM out; we are the bus now */

static int busin();
static void busout();

#define inb(p)		busin((unsigned)(p))
#define outb(p, v)	busout((unsigned)(p), (int)(v))

/* The file under test.  The mutation gate points this at a deliberately
 * broken copy; nothing else changes. */
#ifndef SCCSRC
#define SCCSRC "../src/scc.c"
#endif
#include SCCSRC

/* --- the fake bus --------------------------------------------------------- */

#define NCH	6
static unsigned chbase[NCH] = { 0x100, 0x120, 0x300, 0x320, 0x380, 0x3a0 };

#define K_SCC		0	/* a Z8030: its registers hold what is written */
#define K_LOW		1	/* nothing there, and the bus reads 0x00 */
#define K_HIGH		2	/* nothing there, and the pull-ups read 0xff */
#define K_ECHO		3	/* nothing there; the bus still holds the last byte */
#define K_CONST		4	/* something answers, but always the same byte */

static int			kind[NCH];
static unsigned char	regs[NCH][32];
static int			nread[NCH], nwrite[NCH];
static int			badport;	/* a write to a register we have no business in */
static unsigned char	lastbus;

static int
chan(port) unsigned port;
{
	int i;

	for (i = 0; i < NCH; i++)
		if ((port & ~0x1fU) == chbase[i])
			return (i);
	return (-1);
}

static int
busin(port) unsigned port;
{
	unsigned char v;
	int i;

	if ((i = chan(port)) < 0)
		return (0xff);
	nread[i]++;
	switch (kind[i]) {
	case K_SCC:		v = regs[i][port - chbase[i]]; break;
	case K_LOW:		v = 0x00; break;
	case K_HIGH:	v = 0xff; break;
	case K_ECHO:	v = lastbus; break;
	default:		v = 0x5a; break;
	}
	lastbus = v;		/* the read drove the bus too */
	return (v);
}

static void
busout(port, val) unsigned port; int val;
{
	unsigned off;
	int i;

	lastbus = (unsigned char)val;
	if ((i = chan(port)) < 0)
		return;
	off = port - chbase[i];
	/* Probing must never reach a command register or the transmitter: a
	 * loader that resets a chip or sends a character while asking whether
	 * it is there has broken the console it is asking from. */
	if (off != 0x19 && off != 0x1b)
		badport++;
	nwrite[i]++;
	if (kind[i] == K_SCC)
		regs[i][off] = (unsigned char)val;
}

/* --- the tests ------------------------------------------------------------ */

static int fails;

#define OK(what, got, want) check(what, (long)(got), (long)(want))

static void
check(char *what, long got, long want)
{
	if (got != want) {
		printf("  FAIL %-46s got 0x%lx want 0x%lx\n", what, got, want);
		fails++;
	}
}

/* Set every channel to one kind, then override the first `n' from a list. */
static void
board(dflt, list, n) int dflt; int *list; int n;
{
	int i;

	memset(regs, 0, sizeof regs);
	memset(nread, 0, sizeof nread);
	memset(nwrite, 0, sizeof nwrite);
	badport = 0;
	lastbus = 0;
	for (i = 0; i < NCH; i++)
		kind[i] = dflt;
	for (i = 0; i < n; i++)
		kind[i] = list[i];
}

/*
 * The three machines that exist.  An HR system has no LR board at all, an LR
 * system may have shipped with its second SCC socket empty, and a full LR
 * system has all six.  The map has to name the right lines in each.
 */
static void
t_machines()
{
	static int hr[2]  = { K_SCC, K_SCC };
	static int lr4[4] = { K_SCC, K_SCC, K_SCC, K_SCC };

	board(K_HIGH, hr, 2);
	OK("HR: two motherboard lines and nothing else", sccprobe(0), 0x03);
	OK("HR: no command register was written", badport, 0);

	board(K_HIGH, lr4, 4);
	OK("LR with U36 unpopulated: four lines", sccprobe(0), 0x0f);

	board(K_SCC, (int *)0, 0);
	OK("LR fully populated: six lines", sccprobe(0), 0x3f);
	OK("LR: no command register was written", badport, 0);
}

/*
 * THE FALSE POSITIVE, in each shape an empty bus takes.  Every one of these
 * must report nothing at all.
 */
static void
t_emptybus()
{
	board(K_LOW, (int *)0, 0);
	OK("a bus that reads 0x00 is not a chip", sccprobe(0), 0);

	board(K_HIGH, (int *)0, 0);
	OK("a bus that reads 0xff is not a chip", sccprobe(0), 0);

	board(K_ECHO, (int *)0, 0);
	OK("a bus still holding the last byte is not a chip", sccprobe(0), 0);

	board(K_CONST, (int *)0, 0);
	OK("something answering one constant is not a chip", sccprobe(0), 0);
}

/*
 * The console line.  When the operator is AT the serial console, that channel
 * is reported present and NOT WRITTEN TO -- it is carrying the loader's own
 * output, and the proof it works arrived on it already.
 */
static void
t_console()
{
	board(K_ECHO, (int *)0, 0);
	OK("at the serial console: bit 0 without a probe",
	   sccprobe(1) & BI_SER_CON, BI_SER_CON);
	OK("at the serial console: nothing was written there", nwrite[0], 0);
	OK("at the serial console: nothing was read there", nread[0], 0);

	board(K_ECHO, (int *)0, 0);
	OK("at a video console: channel 0 is probed like any other",
	   sccprobe(0) & BI_SER_CON, 0);
	OK("at a video console: channel 0 was written to",
	   nwrite[0] != 0, 1);
}

/*
 * A probe that changes the machine is not a probe.  The baud-rate constant a
 * channel was carrying has to be there afterwards -- on the console line above
 * all, where changing it would change the speed of the line the loader is
 * talking on.
 */
static void
t_nondestructive()
{
	board(K_SCC, (int *)0, 0);
	regs[0][0x19] = 0x11;		/* the ROM's own 9600-baud constant */
	regs[0][0x1b] = 0x00;
	regs[1][0x19] = 0x42;
	regs[1][0x1b] = 0x99;
	OK("a present channel is found", sccthere(0x100), 1);
	OK("its time constant, low, is put back", regs[0][0x19], 0x11);
	OK("its time constant, high, is put back", regs[0][0x1b], 0x00);
	(void)sccprobe(0);
	OK("a whole probe puts channel 0 back", regs[0][0x19], 0x11);
	OK("a whole probe puts channel 1 back, low", regs[1][0x19], 0x42);
	OK("a whole probe puts channel 1 back, high", regs[1][0x1b], 0x99);
	OK("and touched no other register", badport, 0);
}

/* No bit above the channels the loader knows may ever be set. */
static void
t_reserved()
{
	board(K_SCC, (int *)0, 0);
	OK("nothing above the six known channels", sccprobe(0) & ~0x3fU, 0);
	board(K_SCC, (int *)0, 0);
	OK("nothing above them at the serial console either",
	   sccprobe(1) & ~0x3fU, 0);
}

int
main()
{
	printf("scctest: the serial-channel probe\n");
	t_machines();
	t_emptybus();
	t_console();
	t_nondestructive();
	t_reserved();
	if (fails)
		printf("scctest: %d FAILED\n", fails);
	else
		printf("scctest: all passed\n");
	return fails != 0;
}

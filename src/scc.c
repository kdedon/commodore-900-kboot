/*
 * scc.c -- which serial channels this machine actually has.
 *
 * The question bootinfo's bi_serial answers, and the only place the loader
 * asks it.  An operating system that wants to offer a login on every line
 * should not have to guess at the hardware, and it cannot probe safely once
 * it is running with interrupts armed and a console in use -- the loader can,
 * so the loader does it once and hands the answer over.
 *
 * THE DANGER IS THE FALSE POSITIVE.  A channel reported present that is not
 * there costs a system a getty talking into a dead address forever; a channel
 * missed costs it one line it could have had.  So every rule below is written
 * to refuse, and a channel that cannot be proved is reported absent.
 *
 * The ports are the Z8030's, addressed as al.c and the ROM address them:
 * register n of the channel based at `b' is at b + 2*n + 1.
 */
#include "romabi.h"
#include "kboot.h"

#define SCC_WR12	0x19	/* baud-rate time constant, low  -- reads back */
#define SCC_WR13	0x1b	/* baud-rate time constant, high -- reads back */

/*
 * The channels this machine may have, in bi_serial bit order: ascending I/O
 * base, which is the machine's own line numbering (see include/bootinfo.h).
 * The two spec-reserved Aux3/Aux4 slots at 0x0600 are not here: no board that
 * carries them has been seen, and a bit is set only for hardware proved.
 */
#define SCC_NCH	6

static unsigned sccbase[SCC_NCH] = {
	0x0100, 0x0120,		/* motherboard SCC U74, channels A and B */
	0x0300, 0x0320,		/* LR board SCC #1 U31, CN3 and CN4 */
	0x0380, 0x03a0		/* LR board SCC #2 U36, CN5 and CN6 */
};

/*
 * Is there an SCC at `base'?  WR12/WR13, the baud-rate time constant, is
 * write/read-back storage that no other register aliases and that no reset
 * clears to a known value, so a pattern written and read out again is a chip
 * answering.
 *
 * AN EMPTY ADDRESS IS WHAT THIS HAS TO SURVIVE.  The C900 has no bus timeout
 * and raises nothing for an unclaimed cycle -- the shipping COHERENT driver
 * writes to the absent channels of an HR machine at every boot and the machine
 * does not notice -- so the read returns whatever the bus has: the pull-ups
 * (0xff), or nothing at all (0x00), or, for as long as the capacitance holds
 * it, WHATEVER WAS LAST DRIVEN ONTO IT.  That last one is why the two
 * registers hold DIFFERENT patterns and are read in the order that makes each
 * read expect a byte other than the one the bus saw last: an echo of the
 * previous cycle cannot pass, and neither can a line stuck high or low.  Then
 * the whole thing again with the patterns exchanged, so a device that answers
 * some constant to everything is not mistaken for storage either.
 *
 * The channel is left exactly as it was found: the old constant is read out
 * first and put back, so probing can never change the speed of a line.
 */
int
sccthere(base) unsigned base;
{
	int o12, o13, r;

	o12 = inb(base + SCC_WR12) & 0xff;
	o13 = inb(base + SCC_WR13) & 0xff;
	r = 0;
	outb(base + SCC_WR12, 0x5a);
	outb(base + SCC_WR13, 0xa5);
	if ((inb(base + SCC_WR12) & 0xff) == 0x5a
	 && (inb(base + SCC_WR13) & 0xff) == 0xa5) {
		outb(base + SCC_WR12, 0xa5);
		outb(base + SCC_WR13, 0x5a);
		if ((inb(base + SCC_WR12) & 0xff) == 0xa5
		 && (inb(base + SCC_WR13) & 0xff) == 0x5a)
			r = 1;
	}
	outb(base + SCC_WR12, o12);
	outb(base + SCC_WR13, o13);
	return (r);
}

/*
 * The map, as bi_serial.  `conser' says the operator is at the serial console:
 * then bit 0 is set WITHOUT writing anything there, because that line is
 * carrying this loader's own output and a character in flight is worth more
 * than a test whose answer already arrived on it.  Every other channel is
 * probed, and one that cannot be proved reads 0.
 */
unsigned
sccprobe(conser) int conser;
{
	unsigned m;
	int i;

	m = 0;
	for (i = 0; i < SCC_NCH; i++) {
		if (i == 0 && conser) {
			m |= BI_SER_CON;
			continue;
		}
		if (sccthere(sccbase[i]))
			m |= (unsigned)1 << i;
	}
	return (m);
}

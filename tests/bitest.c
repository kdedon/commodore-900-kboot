/*
 * bitest.c -- the bootinfo handoff, on a host.
 *
 * The block is the one thing kboot writes INTO another program's memory, and
 * every mistake it can make there is silent.  A length off by two bytes and a
 * kernel reads a field that was never written; a checksum computed over the
 * wrong span and a good handoff is refused; a field written into a block too
 * old to hold it and the loader has scribbled on whatever the kernel put next.
 * None of that shows on the screen -- the machine boots, or does not, later.
 *
 * So what is checked here is the RULE, not the happy path: the kernel declares
 * a version, the loader fills THAT version's fields and no more, and the
 * checksum covers exactly the bytes the kernel said it had.  Version 4 added
 * bi_serial, so the case that matters most is the old one: a version 3 kernel
 * meeting a version 4 loader must come out of this untouched past its own end.
 *
 * These lengths are whatever THIS compiler makes the struct.  That is the
 * point: the two sides of a real handoff are the same compiler, and a test
 * that hard-coded the Z8001's numbers would be testing the machine it is not
 * running on.  Every assertion below is a relation, never an address.
 */
#include <stdio.h>
#include <string.h>
#include "../src/kboot.h"
#include <bootinfo.h>

static int fails;

#define OK(what, got, want) check(what, (long)(got), (long)(want))

static void
check(char *what, long got, long want)
{
	if (got != want) {
		printf("  FAIL %-40s got %ld want %ld\n", what, got, want);
		fails++;
	}
}

/*
 * A block as a kernel presents one: the magic and the version/length that
 * kernel was built for, its own idea of everything else, and a sentinel in
 * every byte the loader has no business touching.  0xa5 because it is neither
 * 0 nor 0xff and so cannot be confused with either a cleared or a filled byte.
 */
#define SENT	0xa5

static struct bootinfo	blk;
static unsigned char	*raw = (unsigned char *)&blk;

static void
declare(v, len) unsigned v; unsigned len;
{
	static char mag[BI_MAGLEN] = BI_MAGIC;

	memset(raw, SENT, sizeof blk);
	memcpy(blk.bi_magic, mag, BI_MAGLEN);
	blk.bi_version = (unsigned short)v;
	blk.bi_len = (unsigned short)len;
	blk.bi_npart = 0;
	blk.bi_sum = 0;
}

/* The checksum as a CONSUMER computes it: over the first `len' bytes, which
 * is the only span it is defined over.  bisum() reads bi_len, so a reader
 * checking a span the block does not claim needs this instead. */
static unsigned short
sumlen(len) unsigned len;
{
	unsigned short s, w;
	unsigned n;

	s = 0;
	for (n = 0; n + 2 <= len; n += 2) {
		memcpy(&w, raw + n, sizeof w);
		s = (unsigned short)(s + w);
	}
	return (s);
}

/* Is every byte from `from' to the end of the struct still the sentinel? */
static int
untouched(from) unsigned from;
{
	unsigned n;

	for (n = from; n < sizeof blk; n++)
		if (raw[n] != SENT)
			return (0);
	return (1);
}

/*
 * The version table itself.  A length that is not even cannot be summed in
 * 16-bit words at all, and a version whose length does not exceed the one
 * before it has not appended anything.
 */
static void
t_lengths()
{
	OK("bilen(2) is v2's length", bilen(2), BI_LEN2);
	OK("bilen(3) is v3's length", bilen(3), BI_LEN3);
	OK("bilen(4) is v4's length", bilen(4), BI_LEN4);
	OK("bilen(1) is refused", bilen(1), 0);
	OK("bilen(5) is refused", bilen(5), 0);
	OK("BI_VERSION is 4", BI_VERSION, 4);
	OK("BI_OLDEST is still 2", BI_OLDEST, 2);
	OK("v4 appends BI_TAIL4 to v3", BI_LEN4 - BI_LEN3, BI_TAIL4);
	OK("v3 appends BI_TAIL3 to v2", BI_LEN3 - BI_LEN2, BI_TAIL3);
	OK("v2's length is even", BI_LEN2 & 1, 0);
	OK("v3's length is even", BI_LEN3 & 1, 0);
	OK("v4's length is even", BI_LEN4 & 1, 0);
	/* The field this version added has to fit in the length it claims. */
	OK("bi_serial fits inside v4's length",
	   (long)((char *)&blk.bi_serial - (char *)&blk + BI_TAIL4 <= BI_LEN4), 1);
}

/* A kernel built against this loader's own version gets everything. */
static void
t_v4()
{
	declare(4, BI_LEN4);
	OK("v4: nothing was undeliverable",
	   bipack(&blk, 4, (unsigned)BI_LEN4, BF_SINGLE, BI_CON_SER, 0x000f), 0);
	OK("v4: version written", blk.bi_version, 4);
	OK("v4: length written", blk.bi_len, BI_LEN4);
	OK("v4: marked as a handoff", blk.bi_src, BI_SRC_KBOOT);
	OK("v4: flags delivered", blk.bi_flags, BF_SINGLE);
	OK("v4: console delivered", blk.bi_console, BI_CON_SER);
	OK("v4: serial map delivered", blk.bi_serial, 0x000f);
	OK("v4: the block sums to zero", sumlen((unsigned)BI_LEN4), 0);
	OK("v4: bisum agrees with the span", bisum(&blk), 0);
}

/* An empty serial map is a REPORT, not an omission: a machine with nothing
 * probed says zero and means it. */
static void
t_v4empty()
{
	declare(4, BI_LEN4);
	(void)bipack(&blk, 4, (unsigned)BI_LEN4, 0, BI_CON_VID, 0);
	OK("v4: an empty map is written as zero", blk.bi_serial, 0);
	OK("v4: an empty map still sums to zero", sumlen((unsigned)BI_LEN4), 0);
	declare(4, BI_LEN4);
	(void)bipack(&blk, 4, (unsigned)BI_LEN4, 0, BI_CON_VID, 0x003f);
	OK("v4: a full map is written whole", blk.bi_serial, 0x003f);
	OK("v4: a full map still sums to zero", sumlen((unsigned)BI_LEN4), 0);
}

/*
 * THE REASON BI_OLDEST EXISTS.  A version 3 kernel, which has no bi_serial and
 * no room for one, meets this version 4 loader.  It must come away with a
 * correct version 3 block and NOT ONE BYTE written past its end.
 */
static void
t_v3()
{
	declare(3, BI_LEN3);
	OK("v3: only bi_serial was undeliverable",
	   bipack(&blk, 3, (unsigned)BI_LEN3, BF_SINGLE, BI_CON_VID, 0x003f),
	   BIU_SERIAL);
	OK("v3: length stays v3's", blk.bi_len, BI_LEN3);
	OK("v3: version stays 3", blk.bi_version, 3);
	OK("v3: flags still delivered", blk.bi_flags, BF_SINGLE);
	OK("v3: console still delivered", blk.bi_console, BI_CON_VID);
	OK("v3: bi_serial was NOT written",
	   blk.bi_serial, (unsigned short)((SENT << 8) | SENT));
	OK("v3: nothing written past its end", untouched((unsigned)BI_LEN3), 1);
	OK("v3: the block sums to zero over ITS length",
	   sumlen((unsigned)BI_LEN3), 0);
	OK("v3: bisum agrees with the span", bisum(&blk), 0);
}

/* The same one version further back: v2 has neither the flags nor the map. */
static void
t_v2()
{
	declare(2, BI_LEN2);
	OK("v2: flags and serial both undeliverable",
	   bipack(&blk, 2, (unsigned)BI_LEN2, BF_SINGLE, BI_CON_SER, 0x0003),
	   BIU_FLAGS | BIU_SERIAL);
	OK("v2: length stays v2's", blk.bi_len, BI_LEN2);
	OK("v2: nothing written past its end", untouched((unsigned)BI_LEN2), 1);
	OK("v2: the block sums to zero over ITS length",
	   sumlen((unsigned)BI_LEN2), 0);
}

/*
 * A version 3 CONSUMER, built before bi_serial existed, reading what this
 * loader wrote.  It knows two lengths and refuses a version it does not know
 * -- so it must accept the v3 block the loader made for it, and must refuse a
 * v4 block rather than read a field off the end of its own struct.
 */
static unsigned short
bilen3(v) unsigned v;
{
	if (v == 2)
		return ((unsigned short)BI_LEN2);
	if (v == 3)
		return ((unsigned short)BI_LEN3);
	return (0);
}

static int
v3reader()
{
	unsigned short len;

	len = bilen3((unsigned)blk.bi_version);
	if (len == 0 || len != blk.bi_len)
		return (0);
	return (sumlen((unsigned)len) == 0);
}

static void
t_oldreader()
{
	declare(3, BI_LEN3);
	(void)bipack(&blk, 3, (unsigned)BI_LEN3, 0, BI_CON_SER, 0x0003);
	OK("old reader takes the v3 block made for it", v3reader(), 1);

	declare(4, BI_LEN4);
	(void)bipack(&blk, 4, (unsigned)BI_LEN4, 0, BI_CON_SER, 0x0003);
	OK("old reader refuses a v4 block", v3reader(), 0);
}

/*
 * The checksum has to be able to FAIL, or none of the zeros above mean
 * anything.  Damage anywhere inside the claimed span must show.
 */
static void
t_damage()
{
	unsigned n;
	int missed;

	missed = 0;
	for (n = 0; n < (unsigned)BI_LEN4; n++) {
		declare(4, BI_LEN4);
		(void)bipack(&blk, 4, (unsigned)BI_LEN4, 0, BI_CON_SER, 0x0003);
		raw[n] ^= 0x01;
		if (sumlen((unsigned)BI_LEN4) == 0)
			missed++;
	}
	OK("every damaged byte fails the sum", missed, 0);
}

int
main()
{
	printf("bitest: the bootinfo handoff\n");
	t_lengths();
	t_v4();
	t_v4empty();
	t_v3();
	t_v2();
	t_oldreader();
	t_damage();
	if (fails)
		printf("bitest: %d FAILED\n", fails);
	else
		printf("bitest: all passed\n");
	return fails != 0;
}

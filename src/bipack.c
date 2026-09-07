/*
 * bipack.c -- filling a kernel's bootinfo block down to the version IT
 * declared.
 *
 * The rule this file exists to keep, and to be testable about: THE KERNEL
 * DECLARES AND THE LOADER FILLS DOWN.  The block arrives carrying the version
 * and length the kernel was built for; a loader that knows more versions than
 * the kernel does writes the fields that kernel's version defines and NOT ONE
 * BYTE MORE, then checksums exactly that many bytes.  A kernel built against
 * version 2 boots off this loader unchanged, and so will a kernel built
 * against version 4 when there is a version 5.
 *
 * It is a separate file because it is pure: no ROM, no disk, no console, so
 * the host tests link it and run the real code rather than a copy of it.
 */
#include <bootinfo.h>
#include "kboot.h"

/*
 * Fill *bp for a kernel declaring version `kver' and length `klen', with the
 * boot flags, console and serial map this boot found, and checksum it.  The
 * caller has already put the partition table (or the kernel's own) in place.
 *
 * Returns the BIU_* bits for what the block was too old to carry, so the
 * caller can say so where it matters.  Nothing is written past klen: that is
 * the whole contract, and the tests watch that byte.
 */
unsigned
bipack(bp, kver, klen, flags, con, serial)
register struct bootinfo *bp;
unsigned kver;
unsigned klen;
unsigned flags;
unsigned con;
unsigned serial;
{
	unsigned miss;

	miss = 0;
	bp->bi_version = (unsigned short)kver;
	bp->bi_len = (unsigned short)klen;
	bp->bi_src = BI_SRC_KBOOT;	/* the kernel's cue that this is a handoff */
	if (klen >= BI_LEN3) {
		bp->bi_flags = (unsigned short)flags;
		bp->bi_console = (unsigned short)con;
	} else
		miss |= BIU_FLAGS;
	if (klen >= BI_LEN4)
		bp->bi_serial = (unsigned short)serial;
	else
		miss |= BIU_SERIAL;
	bp->bi_sum = 0;
	bp->bi_sum = -bisum(bp);
	return (miss);
}

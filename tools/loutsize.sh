#!/bin/sh
# loutsize.sh -- report kboot's two size budgets, and enforce both.
#
# kboot's own build gate.  There are TWO limits on a loader and they are
# different measurements; either can bind, so both are checked here.
#
# 1. THE SEGMENT COPY CAPS (--tcap/--dcap).  crt.s copies a FIXED number of
#    bytes of each segment when it relocates itself off RAM base (TCOPY/DCOPY),
#    so a linked text or data+bss larger than that copy is silently truncated
#    in the running loader.  These are read out of the l.out header.
#
# 2. THE ON-DISK BOOT SPAN (--span-heads).  The ROM has to read the whole
#    loader FILE out of a boot partition only min(heads)*spt blocks long,
#    alongside the filesystem metadata, kboot.cfg and the root directory.  This
#    is a limit on the size of the file, and a loader can sit far inside both
#    caps in (1) and still not fit it.
#
# Both are reported on EVERY build, breach or not: a build sitting at 66 of 68
# blocks must not look like one sitting at 38.
#
# Why the span exists, in one paragraph.  The ROM hands the disk controller a
# LINEAR block number and the controller maps it with whichever built-in
# geometry the ROM programmed (dparam[drivetype]).  Every ROM geometry has
# spt = 17, so for block n < heads*17 the cylinder is 0 and the head is n/17 --
# both independent of the head count, so those blocks land on the same physical
# sectors under all of them.  Past that span the mapping depends on a geometry
# nobody has established yet, and the first thing the ROM reads is not even the
# loader: it is the ROOT DIRECTORY, which is what tells it where the loader is.
# A loader over the span is a machine that does not boot, not an error message.
# `--span-heads 4' is the ROM's default drive type, which is what an automatic
# boot always runs; the derivation and the ROM evidence are in the Makefile and
# in the image builders' cohfs.py / mkimage.py, which enforce the same span
# from the other end.
#
# The header is the one the ROM validates and `ld -i -L' emits: magic 0x0107 at
# offset 0, then l_ssize[9] at offset 8 as 32-bit values stored HIGH WORD FIRST
# with each 16-bit word little-endian (the PDP-11 order the whole l.out format
# uses).  Order: SHRI PRVI BSSI SHRD PRVD BSSD ... ; the entry point is at 0x2c.
#
#     loutsize.sh build/kboot                     report
#     loutsize.sh --tcap 0x4000 --dcap 0x4000 f   report and enforce the caps
#     loutsize.sh --span-heads 4 f                report and enforce the span
#
# sh and awk, because a loader's build should need what a Unix has.  Numbers
# are read a byte at a time through od: every value here is under 2^31, which
# awk holds exactly.

set -u

me=$(basename "$0")
tcap=-1
dcap=-1
spanheads=-1
spanspt=17
cfgbytes=0
file=

# `0x4000' is a C constant to $(( )), which is where the hex is resolved: awk
# has no portable strtonum.
num() {
	echo $(( $1 ))
}

while [ $# -gt 0 ]; do
	case $1 in
	--tcap)		tcap=$(num "$2"); shift 2 ;;
	--dcap)		dcap=$(num "$2"); shift 2 ;;
	--span-heads)	spanheads=$(num "$2"); shift 2 ;;
	--span-spt)	spanspt=$(num "$2"); shift 2 ;;
	--cfg-bytes)	cfgbytes=$(num "$2"); shift 2 ;;
	-*)		echo "$me: unknown option $1" >&2; exit 2 ;;
	*)
		if [ -n "$file" ]; then
			echo "$me: one file, not two" >&2
			exit 2
		fi
		file=$1
		shift
		;;
	esac
done

if [ -z "$file" ]; then
	echo "usage: $me [--tcap n] [--dcap n] [--span-heads n] [--span-spt n]" \
	     "[--cfg-bytes n] file" >&2
	exit 2
fi
if [ ! -f "$file" ]; then
	echo "$me: $file: not there" >&2
	exit 2
fi

disk=$(wc -c < "$file")
disk=$(num "$disk")
# -v so a run of equal bytes is not elided into a `*' line.
hdr=$(od -An -v -tu1 -N 48 "$file" | tr -s ' \n' '  ')

awk -v hdr="$hdr" -v path="$file" -v disk="$disk" \
    -v tcap="$tcap" -v dcap="$dcap" \
    -v spanheads="$spanheads" -v spanspt="$spanspt" -v cfgbytes="$cfgbytes" '
# The on-disk budget.  These describe how the OS image builders pack the boot
# partition -- a COHERENT filesystem, the format src/cohfs.h reads -- so that
# the number this tool prints is the number they will compute:
#
#   BS         block size, and the unit of everything below
#   BOOTISIZE  the filesystem isize: block 0 boot block (unused), block 1
#              superblock, blocks 2..isize-1 inodes.  isize = 4 gives 16
#              inodes, which is more than a two-file directory needs; the
#              builders use 4 because mkfs does.
#   NDIRECT    direct block addresses in an inode (di_addr[13]: 10 direct,
#              1 single indirect, 1 double, 1 unused)
#   NINDIR     block addresses in an indirect block (512 / 4)
#   CFGBLKS    blocks of kboot.cfg.  One: every config we ship is under 512
#              bytes.  src/kboot.h CFGBLK (2) is how many the loader will
#              READ, not how many the file occupies; pass --cfg-bytes if a
#              medium config is bigger, because the second block comes out of
#              the loader budget.
#   DIRBLKS    the root directory: dot, dot-dot, the loader, kboot.cfg -- four
#              16-byte entries, one block.
BEGIN {
	BS = 512
	BOOTISIZE = 4
	NDIRECT = 10
	NINDIR = BS / 4
	CFGBLKS = 1
	DIRBLKS = 1
	L_MAGIC = 263			# 0x0107
	HDRLEN = 48

	n = split(hdr, b, " ")
	if (n < HDRLEN) {
		printf("%s: too short to be an l.out\n", path) > "/dev/stderr"
		exit 1
	}
	# split() is 1-based and the header is 0-based.
	for (i = 0; i < HDRLEN; i++)
		h[i] = b[i + 1]

	mag = gw(0)
	if (mag != L_MAGIC) {
		printf("%s: bad l.out magic 0x%04x (want 0x%04x)\n",
		       path, mag, L_MAGIC) > "/dev/stderr"
		exit 1
	}

	text = gl(8) + gl(12) + gl(16)		# SHRI + PRVI + BSSI
	data = gl(20) + gl(24) + gl(28)		# SHRD + PRVD + BSSD
	printf "kboot: entry 0x%x, %s, %s\n",
	       gl(44), show("text", text, tcap), show("data+bss", data, dcap)

	bad = 0

	# The on-disk budget, reported whether or not it is breached.
	if (spanheads >= 0) {
		span = spanheads * spanspt
		avail = span - (BOOTISIZE + cfgblocks() + DIRBLKS)
		maxbytes = 0
		while (avail > 0 && fileblocks(maxbytes + BS) <= avail)
			maxbytes += BS
		used = BOOTISIZE + fileblocks(disk) + cfgblocks() + DIRBLKS
		if (disk > maxbytes) {
			printf "kboot: on disk %d B, boot partition %d/%d blocks "\
			       "(OVER the ROM-safe span by %d B)\n",
			       disk, used, span, disk - maxbytes
			printf("%s: the loader is %d bytes and the ROM-safe boot span holds at\n"\
			       "  most %d.  The span is %d blocks (%d heads x %d spt): %d for the\n"\
			       "  filesystem metadata, %d for kboot.cfg, %d for the root directory,\n"\
			       "  %d left for the loader file, data blocks and the indirect\n"\
			       "  block together.  This one needs %d.\n",
			       path, disk, maxbytes, span, spanheads, spanspt,
			       BOOTISIZE, cfgblocks(), DIRBLKS, avail,
			       fileblocks(disk)) > "/dev/stderr"
			printf("  Past that span the ROM\047s linear block number maps through a\n"\
			       "  geometry nobody has programmed yet, and the block that falls\n"\
			       "  outside first is the ROOT DIRECTORY -- the thing the ROM reads\n"\
			       "  to find this loader at all.  It presents as a machine that does\n"\
			       "  not boot.  Drop features; do not raise the span without redoing\n"\
			       "  the ROM argument that sets --span-heads.\n") > "/dev/stderr"
			bad = 1
		} else {
			printf "kboot: on disk %d B, boot partition %d/%d blocks "\
			       "(%d B free, %d blocks spare)\n",
			       disk, used, span, maxbytes - disk, span - used
		}
	}

	capbad = 0
	capbad += overcap("text", text, tcap)
	capbad += overcap("data+bss", data, dcap)
	if (capbad > 0)
		bad = 1
	# This advice belongs to the copy caps only: a loader can breach the
	# span with both segments comfortably inside them, and telling that
	# build about crt.s sends the reader to the wrong constant.
	if (capbad > 0)
		printf("  crt.s copies exactly the cap when it relocates itself, so this\n"\
		       "  loader would run truncated.  Do NOT raise the cap: the copy would\n"\
		       "  reach further into the page whose tail holds the ROM\047s live\n"\
		       "  segment-1 data on a 512 KB machine.  Drop features instead.\n") \
		       > "/dev/stderr"
	exit bad
}

# One 16-bit header word (little-endian).
function gw(off) {
	return h[off] + h[off + 1] * 256
}

# One 32-bit header value: high word first, each word little-endian.
function gl(off) {
	return gw(off) * 65536 + gw(off + 2)
}

function show(what, n, cap) {
	if (cap < 0)
		return sprintf("%s 0x%x", what, n)
	if (n > cap)
		return sprintf("%s 0x%x/0x%x (OVER by %d)", what, n, cap, n - cap)
	return sprintf("%s 0x%x/0x%x (%d free)", what, n, cap, cap - n)
}

function overcap(what, n, cap) {
	if (cap < 0 || n <= cap)
		return 0
	printf("%s: %s is 0x%x bytes, past the crt.s relocation cap 0x%x.\n",
	       path, what, n, cap) > "/dev/stderr"
	return 1
}

# Blocks a COHERENT inode spends on a file of nbytes -- data + indirect.
#
# Over 10 blocks the file needs a single indirect block, which is charged to
# the same span as the data; over 138 it needs a double indirect and one L1
# block per further 128.  A loader that crosses one of those boundaries pays a
# whole block for one byte, which is worth seeing in the report.
function fileblocks(nbytes,   d, n, rest) {
	d = ceil(nbytes, BS)
	n = d
	if (d > NDIRECT)
		n += 1				# single indirect
	if (d > NDIRECT + NINDIR) {
		rest = d - (NDIRECT + NINDIR)
		n += 1 + ceil(rest, NINDIR)	# double indirect + its L1s
	}
	return n
}

# Blocks kboot.cfg occupies -- one unless a bigger config was named.
function cfgblocks(   n) {
	if (cfgbytes <= 0)
		return CFGBLKS
	n = ceil(cfgbytes, BS)
	return (n > CFGBLKS) ? n : CFGBLKS
}

function ceil(a, b) {
	return int((a + b - 1) / b)
}
'

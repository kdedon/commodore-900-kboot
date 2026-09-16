/*
 * kboot stage-2: read kboot.cfg, run the menu, load the chosen kernel out of
 * its COHERENT filesystem and launch it.
 *
 * Entered from crt.s with a C stack in seg 0x3F.  Path lookup is the ROM's
 * iread/dirlook (romabi.h); file data is streamed by the inode walk below.
 *
 * Layout produced: text at phys 0x080000 (seg 0x30 base 0x0800), data at
 * 0x080000 + roundup(text,1024) (seg 0x31), BSS zero-filled, entry at
 * LDSEG:l_entry.
 */
#include "cohfs.h"
#include <bootinfo.h>
#include "romabi.h"
#include "kboot.h"
#include "con.h"

extern	mapseg();		/* mapseg(seg, basepage, attr) */
extern	launch();		/* launch(entryoff, databasepage, ntextsegs) */
extern	setdoff();		/* setdoff(long base) */
extern	setgeom();		/* setgeom(long cmdfar, long parmfar) */

#define ROOTIN	2

/* WD command block / DMA buffer physical addresses + SASI opcode (wd.c). */
#define WDBUFPADDR	0x80400L
#define WDSDP		0x0C

/*
 * Staging.  The kernel is read into these segments and copied down once it is
 * all in memory: the WD command block and sector buffer are hardwired at phys
 * 0x80400, inside the kernel's own text.  phys 0x0C0000 is not free -- launch_
 * puts its remap stub there (crt.s seg 0x2c).
 */
#define NTSEG	2			/* text segments the loader can stage (128K) */
#define TSTAGE	0x0a		/* staging: text  -> phys 0x0A0000, 0x0B0000 */
#define TSTPG	0x0a00		/*   its base page */
#define DSTAGE	0x0d		/* staging: data  -> phys 0x0D0000 */
#define DSTPG	0x0d00
#define TXTWIN	0x22		/* copy-down text window -> phys 0x080000 + n*64K */
#define DATWIN	0x23		/* copy-down data window -> data base    */
#define ATTR_RW	0x02		/* SYS, writable */
#define RAMPG	0x0800		/* phys 0x080000 >> 8 */

/* Fallback geometry + OS entry, used only when kboot.cfg is missing
 * (ST-225-class 20 MB: 612 cyl x 4 heads x 17 spt x 512). */
#define DEF_CYL		612
#define DEF_HEADS	4
#define DEF_SPT		17
#define DEF_PRECOMP	128
#define DEF_BASE	136L		/* Coherent 3.5 root: wdbtab[4] {136,10200} */

extern struct bootinfo	bi;		/* cfg.c: built from the `part' lines */
extern int				nparts;

static char				target[DIRSIZ];					/* chosen kernel filename */
static char				cfgname[DIRSIZ] = "kboot.cfg";	/* 14-byte zero-padded for dirlook */
static unsigned			bufoff;							/* current read offset within the ROM sector buf */
static unsigned char	wdcmd[16];						/* WDSDP command block (-> WDCMDBLKPADDR) */
static unsigned char	wdparm[6];						/* drive-parameter block (-> WDBUFPADDR) */

/* Program the drive geometry via WDSDP, as the kernel's wdsetparam() does:
 * build the command + parameter blocks from the parsed geometry and DMA them
 * to the controller.  Every OS kboot loads inherits this geometry. */
static
setdrivegeom()
{
	int i;

	for (i = 0; i < 16; i++)
		wdcmd[i] = 0;
	wdcmd[0]  = WDSDP;							/* c_op */
	wdcmd[6]  = (WDBUFPADDR >> 16) & 0xFF;		/* c_highdma */
	wdcmd[7]  = (WDBUFPADDR >> 8) & 0xFF;		/* c_middma */
	wdcmd[8]  = WDBUFPADDR & 0xFF;				/* c_lowdma */
	wdcmd[12] = 0xFF;							/* c_errorbits = block valid */
	wdparm[0] = 0x0F;							/* p_options: 16uS step */
	wdparm[1] = (gheads << 4) | (gcyl >> 8);	/* p_head_cyl */
	wdparm[2] = gcyl & 0xFF;					/* p_cyl (LSB) */
	wdparm[3] = gprecomp / 16;					/* p_precomp */
	wdparm[4] = gprecomp / 16;					/* p_reduced */
	wdparm[5] = gspt;							/* p_nsectors */
	setgeom((unsigned long)wdcmd, (unsigned long)wdparm);
}

static unsigned
gw(p) register char *p;
{
	return (p[0] & 0xff) | ((p[1] & 0xff) << 8);
}

static unsigned long
gl(p) register char *p;
{
	return ((unsigned long)gw(p) << 16) | (unsigned long)gw(p + 2);
}

/* --- the file reader ------------------------------------------------------
 * An inode walk over the direct, single- and double-indirect levels: 10 + 128
 * + 128*128 blocks, about 8 MB.  Blocks are read into the ROM's own sector
 * buffer.  Block numbers come out of i_addr packed as cohfs.h describes; an
 * indirect block holds plain 4-byte block numbers.
 */

static unsigned long	dblk[CI_NDIRECT];	/* the file's direct block numbers */
static unsigned long	ind1, ind2;			/* its single/double indirect blocks */
static unsigned long	iblk[CI_NIND];		/* contents of one indirect block */
static unsigned long	iblkno;				/* which block that is (0 = none held) */
static unsigned long	dind[CI_NIND];		/* contents of the double-indirect block */
static unsigned long	dindno;				/* which block that is (0 = none held) */
static unsigned long	flbn;				/* next logical block to hand out */

/* A native (big-endian) 16-bit word.  gw/gl above read the on-disk order
 * instead: high word first, each word little-endian. */
static unsigned
mw(p) register char *p;
{
	return ((p[0] & 0xff) << 8) | (p[1] & 0xff);
}

/* Read one physical block of the current partition into the ROM's buffer. */
static
pbread(blk) unsigned long blk;
{
	unsigned long doff;
	char *d;

	d = (char *)ROMV_DOFFSET;
	doff = ((unsigned long)mw(d) << 16) | (unsigned long)mw(d + 2);
	return (wdread(mw((char *)ROMV_UNIT), blk + doff, ROMV_BUF));
}

/* Unpack one l3 (3-byte) block number.  l3 order is high, low, middle. */
static unsigned long
gl3(p) register char *p;
{
	return ((unsigned long)(p[0] & 0xff) << 16)
	     | ((unsigned long)(p[2] & 0xff) << 8)
	     |  (unsigned long)(p[1] & 0xff);
}

/* Take the block list of the inode the ROM just read, and rewind to block 0.
 * `dip' points into the ROM's sector buffer, so this must run before any
 * further disk read. */
static
kopen(dip) struct cohino *dip;
{
	register char *a;
	register int i;

	a = dip->i_addr;
	for (i = 0; i < CI_NDIRECT; i++)
		dblk[i] = gl3(a + 3*i);
	ind1 = gl3(a + 3*CI_NDIRECT);
	ind2 = gl3(a + 3*(CI_NDIRECT+1));
	iblkno = 0;
	dindno = 0;
	flbn = 0;
}

static
krewind()
{
	flbn = 0;
}

/* Load indirect block `blk' into iblk[], unless it is already there. */
static
iload(blk) unsigned long blk;
{
	register int i;
	register char *b;

	if (blk == iblkno)
		return (1);
	iblkno = 0;
	if (blk == 0 || pbread(blk) == 0)
		return (0);
	b = (char *)ROMV_BUF;
	for (i = 0; i < CI_NIND; i++)
		iblk[i] = gl(b + 4*i);
	iblkno = blk;
	return (1);
}

/* The same for the double-indirect block, in its own cache. */
static
dload(blk) unsigned long blk;
{
	register int i;
	register char *b;

	if (blk == dindno)
		return (1);
	dindno = 0;
	if (blk == 0 || pbread(blk) == 0)
		return (0);
	b = (char *)ROMV_BUF;
	for (i = 0; i < CI_NIND; i++)
		dind[i] = gl(b + 4*i);
	dindno = blk;
	return (1);
}

/* Read the next logical block of the open file into the ROM's buffer.
 * Returns 0 on a read error; a hole reads as zeros, as it does in the ROM. */
static
kvread()
{
	unsigned long pb;
	unsigned n;

	pb = 0;
	if (flbn < CI_NDIRECT)
		pb = dblk[(unsigned)flbn];
	else if (flbn < CI_NDIRECT + CI_NIND) {
		if (iload(ind1) == 0)
			return (0);
		pb = iblk[(unsigned)(flbn - CI_NDIRECT)];
	} else {
		n = (unsigned)(flbn - (CI_NDIRECT + CI_NIND));
		if (dload(ind2) == 0)
			return (0);
		if (iload(dind[n / CI_NIND]) == 0)
			return (0);
		pb = iblk[n % CI_NIND];
	}
	flbn++;
	if (pb == 0) {
		pclear(ROMV_BUF, 512);
		return (1);
	}
	return (pbread(pb));
}

/* Copy `size' bytes of the file stream into far segment `dseg' at offset 0.
 * size <= 64K, and is a long because a full segment is 65536. */
static
loadto(dseg, size) unsigned dseg; unsigned long size;
{
	unsigned long doff;
	unsigned count;

	doff = 0;
	while (size != 0) {
		if (bufoff == 0)
			kvread();
		count = 512 - bufoff;
		if (count > size)
			count = size;
		ldirb(ROMV_BUF + bufoff, ((unsigned long)dseg << 24) | doff, count);
		doff += count;
		bufoff += count;
		size -= count;
		if (bufoff >= 512)
			bufoff = 0;
	}
}

/* Block-copy `n' bytes from one far address to another, in chunks the ROM's
 * 16-bit ldirb count can hold. */
static
copyfar(src, dst, n) unsigned long src, dst, n;
{
	unsigned count;

	while (n != 0) {
		count = (n > 32768L) ? 32768 : (unsigned)n;
		ldirb(src, dst, count);
		src += count;
		dst += count;
		n -= count;
	}
}

/* Read the just-kopen()ed config file into cfg[]: CFGBLK whole blocks, no
 * more.  Blocks a short file does not have read as zeros and parse
 * harmlessly.  i_size is not consulted -- it is 32 bits and this loader's
 * arithmetic is 16-bit -- so an overlong file is detected by probing one
 * block past the buffer for a non-zero byte, and warned about rather than
 * refused. */
static
cfgread()
{
	unsigned i, n;
	char *b;

	b = (char *)ROMV_BUF;
	cfglen = CFGMAX;
	for (n = 0; n < CFGBLK; n++) {
		kvread();
		for (i = 0; i < 512; i++)
			cfg[n * 512 + i] = b[i];
	}
	if (kvread() != 0)
		for (i = 0; i < 512; i++)
			if (b[i] != 0) {
				puts("kboot: kboot.cfg is longer than this loader reads; the tail was IGNORED (raise CFGBLK and rebuild)\n");
				break;
			}
}

/* Read and parse kboot.cfg from the boot partition, and install the
 * compiled-in defaults if there is none. */
static
loadcfg()
{
	struct cohino *dip;
	char *err;
	int ino, seen;

	gcyl = DEF_CYL; gheads = DEF_HEADS; gspt = DEF_SPT; gprecomp = DEF_PRECOMP;
	nos = 0;

	/* The ROM's doffset already points at the boot partition; leave it.
	 * dirlook compares the whole 14-byte name field, so cfgname is a DIRSIZ
	 * buffer zero-padded past the terminator. */
	cfgfault = 0;
	seen = 0;
	dip = iread(ROOTIN);
	if (dip != 0) {
		err = dirlook(dip, cfgname, &ino);
		if (err == 0) {
			dip = iread(ino);
			if (dip != 0 && (dip->i_mode & CI_FMT) == CI_REG) {
				seen = 1;
				kopen(dip);
				cfgread();
				cfgparse();
				if (cfgflgerr)
					puts("kboot: a `flags' or `console' line belongs to no entry and set nothing\n");
			}
		}
	}
	/* No config is a configuration; a config naming nothing bootable is a
	 * fault, which says so and goes to recovery.  The defaults are
	 * installed either way, so recovery has something to edit. */
	if (seen && nos == 0) {
		cfgfault = 1;
		puts("kboot: kboot.cfg is there and names nothing bootable.\n");
		puts("kboot: that is a FAULT, not a missing config.\n");
	}
	if (nos == 0) {
		copyname(oslist[0].label, "Coherent-3.5", LBLSZ);
		oslist[0].base = DEF_BASE;
		copyname(oslist[0].file, "coherent", DIRSIZ);
		/* The defaults carry no partition table: the kernel boots on
		 * its own generated one. */
		oslist[0].wantbi = 0;
		oslist[0].lay = LAYGLOB;
		oslist[0].voc = LAYGLOB;
		oslist[0].bflags = 0;
		oslist[0].badflg = 0;
		oslist[0].conser = 0;
		nos = 1;
		if (!seen)
			puts("kboot: no kboot.cfg; using defaults\n");
	}
}

/*
 * Write entry `k's handoff into the kernel staged in segment `dseg' (`dlen'
 * bytes of loaded data).  The block is found by scanning that data for
 * BI_MAGIC and is rewritten in place, so the copy-down that follows carries
 * it into the running kernel.
 *
 * The partition table goes in only for an entry that asked for it; the boot
 * flags and the console go in for every entry, over the kernel's own table
 * read back out of the staged block.  Everything written is bounded by the
 * version and length the KERNEL declared (include/bootinfo.h).
 *
 * Called only when the entry asked for something, because an OS that takes no
 * handoff is not a case to report -- see boot().
 *
 * Returns 0, or a reason nothing could be written.
 */
static char *
bifill(dseg, dlen, k) unsigned dseg; unsigned dlen; int k;
{
	unsigned off, lim, kver, klen, con;
	register char *p;
	register int i;

	/* Bound the scan by the SMALLEST block any kernel may carry, so a
	 * block in the last bytes of the data segment is still found. */
	if (dlen < BI_LEN2)
		return ("this kernel takes no boot information");
	lim = dlen - BI_LEN2;
	for (off = 0; ; off += 2) {
		p = (char *)(((unsigned long)dseg << 24) | (unsigned long)off);
		if (*p == 'K') {
			for (i = 1; i < BI_MAGLEN; i++)
				if (p[i] != bi.bi_magic[i])
					break;
			if (i == BI_MAGLEN)
				break;
		}
		if (off >= lim)
			return ("this kernel takes no boot information");
	}
	kver = mw(p + BI_MAGLEN);
	klen = mw(p + BI_MAGLEN + 2);
	if (bilen(kver) == 0)
		return ("this kernel asks for boot information of another version");
	/* The version says which fields, the length how many bytes are there;
	 * a disagreement, or a length past the data loaded, is damage. */
	if (klen != bilen(kver) || dlen - off < klen)
		return ("this kernel's boot-information block is the wrong size");

	if (oslist[k].wantbi) {
		if (nparts == 0)
			return ("kboot.cfg supplies no partition table");
		bi.bi_npart = BI_NPART;
	} else {
		/* No table asked for: keep the kernel's own. */
		copyfar((unsigned long)p, (unsigned long)&bi,
			(unsigned long)klen);
	}
	/* The console the system is TOLD to use, and obeys without probing:
	 * `console serial' as the entry wrote it, otherwise what the cards
	 * answer (vid.c), serial when none does.  bi_serial's console bit
	 * follows that decision too.  The ROM's flags say only where this
	 * loader's own menu went, which is not the system's console. */
	con = oslist[k].conser ? BI_CON_SER : vidprobe();
	/* Fill down to what this kernel declared, never up to what this loader
	 * knows. */
	if ((bipack(&bi, kver, klen, (unsigned)oslist[k].bflags, con,
		    sccprobe(con == BI_CON_SER)) & BIU_FLAGS) != 0
	    && oslist[k].bflags != 0)
		puts("kboot: kernel too old for boot flags\n");
	ldirb((unsigned long)&bi, (unsigned long)p, klen);
	return (0);
}

/*
 * Does entry `k' name something that could be booted?  Returns 0, or a
 * diagnostic.  The same walk the boot does, stopped one step short of it.
 * The ROM's doffset is saved and put back.
 */
char *
osverify(k) int k;
{
	struct cohino *dip;
	char *err, *d;
	unsigned long save;
	int ino, i;

	d = (char *)ROMV_DOFFSET;
	save = ((unsigned long)mw(d) << 16) | (unsigned long)mw(d + 2);

	/* dirlook compares the whole 14-byte field: zero-fill, do not merely
	 * terminate. */
	for (i = 0; i < DIRSIZ; i++)
		target[i] = '\0';
	copyname(target, oslist[k].file, DIRSIZ);

	setdoff(oslist[k].base);
	err = 0;
	dip = iread(ROOTIN);
	if (dip == 0)
		err = "no filesystem at that block";
	else {
		err = dirlook(dip, target, &ino);
		if (err == 0) {
			dip = iread(ino);
			if (dip == 0)
				err = "the inode is unreadable";
			else if ((dip->i_mode & CI_FMT) != CI_REG)
				err = "that name is not a file";
			else {
				kopen(dip);
				if (kvread() == 0)
					err = "the file is unreadable";
				else if (gw((char *)ROMV_BUF) != 0x0107)
					err = "that file is not an l.out";
			}
		}
	}
	setdoff(save);
	return (err);
}

/* Zero `len' bytes at far address `at'.  The ROM's pclear takes a 16-bit
 * length and clears one byte before testing it, so 0 means 65536 and a
 * zero-length request must not reach it. */
static zapfar(at, len)
unsigned long at, len;
{
	unsigned n;

	while (len != 0) {
		n = (len > 0x4000L) ? 0x4000 : (unsigned)len;
		pclear(at, n);
		at += n;
		len -= n;
	}
}

bmain()
{
	struct cohino *dip;
	char *err, *h;
	int ino, k, i;
	unsigned ds, bds, entry, dbpage, nts;
	unsigned long is, tround, rem, n;

	puts("\nkboot: Commodore 900 boot\n");
	coninit();			/* the tick source and the key path */
	loadcfg();

	/* Before the menu: `verify' reads a partition under it. */
	setdrivegeom();

	k = uimenu();
	copyname(target, oslist[k].file, DIRSIZ);
	setdoff(oslist[k].base);		/* fs reads now hit the chosen partition */
	puts("kboot: loading ");
	puts(target);
	puts("\n");
	dip = iread(ROOTIN);
	if (dip == 0) { puts("kboot: iread(root) failed\n"); return; }
	err = dirlook(dip, target, &ino);
	if (err != 0) { puts("kboot: "); puts(err); puts("\n"); return; }
	dip = iread(ino);
	if (dip == 0) { puts("kboot: iread(file) failed\n"); return; }
	if ((dip->i_mode & CI_FMT) != CI_REG) { puts("kboot: not a file\n"); return; }
	kopen(dip);
	kvread();
	h = (char *)ROMV_BUF;
	if (gw(h) != 0x0107) { puts("kboot: bad l.out magic\n"); return; }

	is    = gl(h + 8) + gl(h + 12);					/* SHRI + PRVI (BSSI is 0) */
	ds    = (unsigned)(gl(h + 20) + gl(h + 24));	/* SHRD + PRVD */
	bds   = (unsigned)gl(h + 28);					/* BSSD */
	/* l_entry offset; the segment is LDSEG, md.o being linked first. */
	entry = (unsigned)gl(h + 0x2c);
	/* How many segments the text needs, and so which segment the data is
	 * in: ld rounds the data base up to a segment boundary. */
	nts = (unsigned)((is + 0xFFFFL) >> 16);
	if (nts == 0 || nts > NTSEG) {
		puts("kboot: kernel text too large\n");
		return;
	}
	tround = (is + 1023L) & ~1023L;				/* text rounded to a click */
	dbpage = RAMPG + (unsigned)(tround >> 8);	/* kernel data base page */

	/* Skip the 48-byte l.out header, then stream each 64K of text into its
	 * own staging segment and the data into DSTAGE. */
	krewind();
	kvread();
	bufoff = 48;
	rem = is;
	for (i = 0; i < nts; i++) {
		n = (rem > 0x10000L) ? 0x10000L : rem;
		mapseg(TSTAGE + i, TSTPG + (i << 8), ATTR_RW);
		loadto(TSTAGE + i, n);
		rem -= n;
	}
	mapseg(DSTAGE, DSTPG, ATTR_RW);
	loadto(DSTAGE, (unsigned long)ds);

	/* Hand over while the data is still staged.  An entry asking for
	 * nothing is launched untouched and in silence: most operating systems
	 * take no handoff, and there is nothing to report about one that does
	 * not want it.  Only what WAS asked for can go undelivered -- the
	 * TABLE refuses the entry, since a kernel booting on another disk's
	 * layout is worse than not booting; FLAGS say what was lost and boot. */
	if (oslist[k].wantbi || oslist[k].bflags != 0) {
		cfglayout(k);
		err = bifill(DSTAGE, ds, k);
		if (err != 0) {
			if (oslist[k].wantbi) {
				puts("kboot: no partition table handed over: ");
				puts(err);
				puts("\n");
				return;
			}
			puts("kboot: booting without the flags asked for: ");
			puts(err);
			puts("\n");
		}
	}
	puts("kboot: staged, copying down\n");

	/* Copy down into the layout the kernel was linked for: text segments
	 * consecutive from RAM base, then the data segment at dbpage. */
	rem = is;
	for (i = 0; i < nts; i++) {
		n = (rem > 0x10000L) ? 0x10000L : rem;
		mapseg(TXTWIN, RAMPG + (i << 8), ATTR_RW);
		copyfar((unsigned long)(TSTAGE + i) << 24,
			(unsigned long)TXTWIN << 24, n);
		rem -= n;
	}
	mapseg(DATWIN, dbpage, ATTR_RW);
	copyfar((unsigned long)DSTAGE << 24, (unsigned long)DATWIN << 24,
		(unsigned long)ds);
	/* Zero the data BSS, if there is one: pclear tests its count after the
	 * first byte, so a zero length would wipe the segment just copied. */
	if (bds != 0)
		pclear(((unsigned long)DATWIN << 24) | ds, bds);

	/* Give the staging RAM back zeroed: it is the front of the kernel's
	 * free pool, and a kernel may assume the pool is zero. */
	rem = is;
	for (i = 0; i < nts; i++) {
		n = (rem > 0x10000L) ? 0x10000L : rem;
		zapfar((unsigned long)(TSTAGE + i) << 24, n);
		rem -= n;
	}
	zapfar((unsigned long)DSTAGE << 24, (unsigned long)ds);

	puts("kboot: launching kernel\n");
	launch(entry, dbpage, nts);	/* maps the segments, jumps LDSEG:entry */
	puts("kboot: launch returned?!\n");
}

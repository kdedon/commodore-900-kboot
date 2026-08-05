/*
 * romabi.h -- entry points and state of the stock C900 boot ROM
 * (boot-L/H_V_1.0), for kboot to reuse.
 *
 * The ROM is MWC-Coherent-C for the Z8001, the same calling convention this
 * loader is compiled with: args pushed right to left, long and pointer
 * results in RR0, small ints in R1.  All addresses are segment-0 offsets, and
 * the ROM is still mapped at segment 0 while kboot runs.
 *
 * Leaf routines (seg-0 offset):
 *   jmp     0x0234  jmp(addr_t segoff)            far jump to loaded image
 *   ldirb   0x0274  ldirb(long src, long dst, unsigned len)   block move
 *   pclear  0x0286  pclear(long segoff, unsigned len)         zero fill
 *   segload 0x023a  segload(seg, base, size)      program one MMU descriptor
 *   wdgo    0x111e  wdgo(char *cmdblk)            kick WD cmd + wait status
 *   wdread  0x11a6  wdread(int unit, long blk, char *buf)     HD sector read
 *   vread   0x1fac  vread()                       next file block -> buf; R1
 *   dirlook 0x1a0e  dirlook(int *inode,char *name,int *ino)   0 ok / errstr
 *   iread   0x1e86  iread(int ino)                inode -> in-core ptr (RR0)
 *   fsload  0x1aee  fsload(inode)                 (ROM l.out loader; buggy)
 *   fload   0x1dc4  (ROM segmented copy; the 64K-buggy one -- do NOT reuse)
 *   inb     0x020a  inb(unsigned port)            byte in, result in R1
 *   outb    0x021c  outb(unsigned port, int data) byte out
 *
 * Boot device state (segment 1 RAM), set up by the ROM's parsedev/boot:
 *   unit    01:123a int      logical unit
 *   doffset 01:1236 long     device start block, ADDED to each logical block
 *   dread   01:123c fn ptr   device read vector (== wdread for hd) -- CODE
 *   hdf     01:1558 int      booted from hard disk
 *   fdf     01:182e int      booted from floppy
 *   buf     01:1240 char[512] sector buffer
 *
 * l.out/n.out header the ROM validates (and that our ld -i -L emits):
 *   L_MAGIC 0x0107, l_flag must have LF_SEP|LF_32 (0x12), M_Z8001 = 4.
 *   l_entry at file offset 0x2c; l_ssize[9] at 0x08; l_tbase at 6.
 */
#ifndef ROMABI_H
#define ROMABI_H

#define ROM_JMP		0x0234
#define ROM_LDIRB	0x0274
#define ROM_PCLEAR	0x0286
#define ROM_SEGLOAD	0x023a
#define ROM_WDGO	0x111e
#define ROM_WDREAD	0x11a6
#define ROM_VREAD	0x1fac
#define ROM_DIRLOOK	0x1a0e
#define ROM_IREAD	0x1e86

/*
 * Console I/O dispatchers.  These follow whichever console the ROM selected
 * at boot, testing con_alt/con_hires below.  putchar/getchar expand '\n' to
 * CR+LF; getchar blocks and echoes.
 */
#define ROM_PUTCHAR	0x0fc2
#define ROM_GETCHAR	0x104a
#define ROM_PUTS	0x0900

/*
 * The pieces getchar()'s VIDEO path is built from, which a countdown needs
 * because getchar() itself blocks:
 *
 *   kbd_init  0x3ed4  kbd_init()          once, guarded by get_init below
 *   kbd_poll  0x3f1e  kbd_poll()          0 or a raw scancode in R1; never
 *                                         blocks; saves R0-R15; wants 32
 *                                         bytes of stack headroom
 *   kbd_decode 0x3f62 kbd_decode(scan)    ASCII, or 0 for a key-up or a bare
 *                                         modifier
 *
 * The serial path has no such routine -- getchar polls the SCC inline -- so
 * con.c does that one itself with inb().
 */
#define ROM_INB		0x020a
#define ROM_OUTB	0x021c
#define ROM_KBDINIT	0x3ed4
#define ROM_KBDPOLL	0x3f1e
#define ROM_KBDDEC	0x3f62

/* The segment-1 cells above, as CPU far pointers: a Z8001 far pointer holds
 * the segment in the HIGH BYTE, (seg<<24)|off, not the ROM's laddr() form. */
#define ROMV_UNIT		0x0100123aL
#define ROMV_DOFFSET	0x01001236L
#define ROMV_DREAD		0x0100123cL
#define ROMV_HDF		0x01001558L
#define ROMV_FDF		0x0100182eL
#define ROMV_BUF		0x01001240L
/* The ROM's console selection, which putchar/getchar dispatch on.  Both zero
 * is the serial SCC. */
#define ROMV_CONALT		0x010017ffL	/* != 0: low-res video */
#define ROMV_CONHIRES	0x01001800L	/* != 0: hi-res video */
#define ROMV_GETINIT	0x0100041cL	/* getchar's keyboard-initialised flag */
/* Segment 1 offset 0 holds a POINTER to the ROM's romconf block, whose byte
 * at offset 14 is rom_ctype: 0 = 4 MHz, 1 = 6 MHz. */
#define ROMV_CONFP		0x01000000L
#define ROMC_CTYPE		14

/* Callable aliases: a segmented indirect call through a far function pointer
 * (segment 0 : offset) reaches the ROM and returns.  crt.s calls the address
 * constants directly instead. */
struct	cohino;
#define iread(ino)			((struct cohino *(*)())ROM_IREAD)(ino)
#define wdread(u,blk,buf)	((int (*)())ROM_WDREAD)((int)(u), \
					(unsigned long)(blk),(unsigned long)(buf))
#define dirlook(ip,nm,inop)	((char *(*)())ROM_DIRLOOK)(ip,nm,inop)
#define vread()				((int (*)())ROM_VREAD)()
#define ldirb(src,dst,n)	((int (*)())ROM_LDIRB)((unsigned long)(src),(unsigned long)(dst),(unsigned)(n))
#define pclear(segoff,n)	((int (*)())ROM_PCLEAR)((unsigned long)(segoff),(unsigned)(n))
#define putchar(c)			((int (*)())ROM_PUTCHAR)((int)(c))
#define getchar()			((int (*)())ROM_GETCHAR)()
#define puts(s)				((int (*)())ROM_PUTS)((char *)(s))
#define inb(p)				((int (*)())ROM_INB)((unsigned)(p))
#define outb(p,v)			((int (*)())ROM_OUTB)((unsigned)(p),(int)(v))
#define kbdinit()			((int (*)())ROM_KBDINIT)()
#define kbdpoll()			((int (*)())ROM_KBDPOLL)()
#define kbddec(r)			((int (*)())ROM_KBDDEC)((int)(r))

#endif /* ROMABI_H */

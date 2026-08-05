/*
 * cohfs.h -- the COHERENT on-disk filesystem, as much of it as kboot reads.
 *
 * The inode is 64 bytes: inode n lives in block INOBASE + (n-1)/8 at slot
 * (n-1)%8, eight to a 512-byte block, and the root is inode 2.  Offsets,
 * which the structure below reproduces exactly:
 *
 *	 0	i_mode		2  file type + permissions (CI_* below)
 *	 2	i_nlink		2  link count
 *	 4	i_uid		2  owner
 *	 6	i_gid		2  group
 *	 8	i_size		4  length in bytes
 *	12	i_addr	    40 block addresses; see below
 *	52	i_atime		4  accessed
 *	56	i_mtime		4  modified
 *	60	i_ctime		4  inode changed
 *	64			   total
 *
 * Every member is 2 or 4 bytes at an even offset, so the Z8001 compiler
 * inserts no padding.  Do not add a member of another width without
 * re-checking that.
 *
 * i_addr is not an array of longs: it is 13 packed 3-byte block numbers (39
 * bytes used, 1 spare) stored high, low, middle.  Entries 0..9 are direct,
 * 10 single-indirect, 11 double-indirect, 12 triple, and an indirect block
 * holds 128 plain 4-byte block numbers.  bmain.c's gl3() unpacks one.
 *
 * Numeric fields are stored little-endian on disk (32-bit values as two such
 * words, high word first) and this machine is big-endian.  The ROM's iread()
 * byte-swaps i_mode and i_size in place before returning, and leaves i_addr
 * alone.  So:
 *
 *	i_mode		native after iread(); read it directly
 *	i_size		native after iread(), but do NOT read it here -- the
 *					loader's 16-bit arithmetic must not read it at all
 *	i_addr		untouched, still packed; use gl3()
 *	the times	untouched, still swapped; declared only to place bytes
 *
 * A pointer from iread() points into the ROM's ONE sector buffer, so the next
 * disk read invalidates it.  Copy what is wanted out first (bmain.c kopen()).
 */
#ifndef COHFS_H
#define COHFS_H

struct cohino {
	unsigned short	i_mode;		/* type + permissions (native) */
	short			i_nlink;	/* link count */
	short			i_uid;		/* owner */
	short			i_gid;		/* group */
	long			i_size;		/* bytes */
	char			i_addr[40];	/* 13 packed 3-byte block numbers */
	long			i_atime;	/* accessed (still byte-swapped) */
	long			i_mtime;	/* modified (still byte-swapped) */
	long			i_ctime;	/* inode changed (still byte-swapped) */
};

#define CI_INOSZ	64		/* bytes per on-disk inode */
#define CI_NADDR	13		/* i_addr entries */
#define CI_NDIRECT	10		/* of which direct */
#define CI_NIND		128		/* block numbers in an indirect block */

/* i_mode: the type field, and the two types kboot distinguishes. */
#define CI_FMT		0170000		/* type mask */
#define CI_DIR		0040000		/* directory */
#define CI_REG		0100000		/* regular file */

/* A directory entry is a 2-byte inode number then a 14-byte name that need
 * not be terminated.  The ROM's dirlook() compares the whole field, so a name
 * handed to it must be a zero-padded buffer of exactly this width. */
#define CI_NAMLEN	14

#endif /* COHFS_H */

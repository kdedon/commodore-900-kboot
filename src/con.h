/*
 * con.h -- the console, as the loader's user interface sees it.  There are
 * three on this machine (serial SCC, low-res video, hi-res video) and the ROM
 * picks one; nothing above this header names which.  What con.c must keep
 * true on all three:
 *
 *   - conpoll() never blocks and never echoes.
 *   - conkey() blocks and still does not echo.
 *   - conrew() returns 0 where it cannot be done, and the caller degrades.
 *   - contick() is polled; it enables no interrupt and installs no vector.
 */
#ifndef CON_H
#define CON_H

/* Keys conpoll()/conkey() return that are not characters.  Above 0x100 so
 * they cannot collide with anything a keyboard produces. */
#define K_UP	0x101
#define K_DOWN	0x102

extern			coninit();	/* start the tick source; pick the key path */
extern int		conpoll();	/* the next key, or 0 -- NEVER blocks */
extern int		conkey();	/* the next key, blocking, no echo */
extern int		conrew();	/* rewind to the line start; 0 if impossible */
extern unsigned	contick();	/* hundredths of a second since coninit() */

/* Redrawing a block already on the screen.  All three answer 0 where it
 * cannot be done, and the menu then offers no moving selection at all rather
 * than reprinting itself under the last copy.  conup(0) asks and moves
 * nothing, which is how the menu decides what to offer before drawing. */
extern int		conup();	/* to column 0, then up n lines */
extern int		conrev();	/* reverse video on (1) or off (0) */
extern int		conclr();	/* erase from the cursor to end of line */

/* Nonzero when the console the ROM chose is a video board rather than the
 * serial line.  It says where THIS LOADER's menu is and nothing more: the
 * console a system is handed (bootinfo.h bi_console) is decided per entry by
 * kboot.cfg and the framebuffer probe (vid.c, bmain.c bifill), and the ROM
 * flags do not tell hi-res from low-res. */
extern int		convid();

/* Output, one call deep over the ROM's puts/putchar -- which is the point:
 * a host build substitutes the whole console here. */
extern			cputs();	/* a string; '\n' becomes CR+LF */
extern			cputc();	/* one character */
extern			cputn();	/* an unsigned long, in decimal */

#endif /* CON_H */

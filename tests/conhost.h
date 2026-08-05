/* conhost.h -- what the fake console (concon.c) offers a test on top of con.h */
#ifndef CONHOST_H
#define CONHOST_H

extern char		conout[];		/* everything the menu has printed */
extern int		con_canrew;		/* 1 = a serial-shaped console, 0 = video */
extern int		con_starved;	/* conkey() ran the script dry: a HANG */

extern void		conscript();	/* the keys the user will press */
extern void		conrate();		/* hundredths of a second per poll */

#endif

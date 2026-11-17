/*
 * romjt.c -- find the ROM's entry points and state, once, before the first
 * call into it.
 */
#include "romabi.h"

/* V1.0's addresses, which a ROM without the table keeps. */
long	romsv[NRS] = {
	0x0fc2L, 0x104aL, 0x0900L, 0x111eL, 0x11a6L, 0x1e86L, 0x1a0eL, 0x1facL
};
char	*romvar[NRV] = {
	(char *)0x0100123aL, (char *)0x01001236L, (char *)0x01001240L,
	(char *)0x0100041cL, (char *)0x01001800L
};

/* Where each romvar[] entry is in the ROM's own list. */
static char	rvidx[NRV] = { 1, 2, 6, 7, 9 };

romjt()
{
	register int i;
	register char **rv;

	if (*(long *)ROMABI != ROMABI_MAGIC)
		return;
	for (i = 0; i < NRS; i++)
		romsv[i] = ROMSVC(i);
	rv = *(char ***)(ROMABI + 8);
	for (i = 0; i < NRV; i++)
		romvar[i] = rv[rvidx[i]];
}

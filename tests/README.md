# Host tests

`make -C tests` builds and runs these with the host compiler.  They need no
Commodore 900, no cross toolchain and no emulator, and they are the fastest
loop this repository has.

## What they prove, and what they cannot

They cover the two pieces of the loader that are **pure logic**: the
`kboot.cfg` parser (`src/cfg.c`) and the menu state machine (`src/ui.c`).
`src/ui.c` reaches the machine only through `src/con.h`, so `concon.c` here
supplies a fake console — a scripted key queue and a captured transcript — and
the state machine runs on the host exactly as it runs on the loader.

They **cannot** prove loader behaviour.  `src/bmain.c` is the ROM, the inode
walk and the segment staging, and none of that exists on a host; neither does
the seg-0x3F frame model the loader is compiled for (`cc2 0012` = VPEEP+VKERN),
which is a shape no host build and no user-mode harness reproduces.  A
miscompile that only appears under that model has happened once here and every
host check passed while it did.

So: **these tests are where a wrong answer is cheap to find, and the emulator
is where the loader is proven.**  Neither replaces the other.

## The rule these are written to

A check that cannot fail is decoration.  Every assertion here was watched
failing before it was trusted — `make -C tests mutate` re-runs the suite
against a deliberately broken parser and a deliberately broken menu and
requires that both are caught.

# Commodore 900 kboot

`kboot` is the Commodore 900 chainloader. The stock boot ROM loads it; kboot
then reads `kboot.cfg`, displays a menu, loads the selected kernel, and passes
the disk layout and boot flags to it.

## Build and test

    make deps             fetch the toolchain and emulator releases DEPS names
    make                  build build/kboot
    make test             run host tests and mutation tests
    make size             check loader size limits
    make headers-dist     package include/*.h
    make compiler-info
    make clean
    make help

kboot is built with the `commodore-900-toolchain` Z8001 cross compiler.
`C900_TOOLCHAIN` names a release or a built checkout, and `C900_EMU` the
emulator.

`make size` checks both the loader's segment-copy limits and the boot-ROM disk
span. An oversized loader is rejected.

## Kernel handoff

`include/bootinfo.h` defines the optional structure passed to kernels that ask
for partition, swap, boot-flag and memory-map information, and
`include/memmap.h` the reserved segments the map is built by subtracting.
Kernels that do not use the handoff can still be loaded.

## Configuration

Copy `cfg/kboot.cfg.sample` to the boot partition as `kboot.cfg`.

    geom <cyl> <heads> <spt> <precomp>
    system <label>
    part <slot> <start> <blocks>
    swap <slot> <bottom> <top>
    bflag <name> <bit>
    os <label> <base-block> <kernel> [part]
    flags <name> ...
    console serial|probe
    reserve <first-segment> <last-segment>
    memtop <segment>
    timeout <seconds>
    default <entry-number>

`part`, `swap`, and `bflag` apply to the nearest preceding `os` or `system`
block, falling back to file-wide defaults. A scope that defines any entries of
one kind replaces the outer set for that kind.

`reserve` and `memtop` shape the memory map every system is handed, and belong
to the file rather than to an entry: the memory a board sits in is the same
whichever system boots. Memory is a list of RAM runs, not one extent, because
the ROM's memory test marches whole 64 KB segments and the framebuffers answer
it -- the text RAM mirrors through segment 0x37 and a hi-res bitmap answers at
0x3E and 0x3F. `include/memmap.h` is the table of what those segments are, and
kboot subtracts it from the range the ROM reports. A ROM with a C9RC block
supplies its RAM runs and bad segments instead, and its HR board and serial
channels. `reserve` takes out a range
as well, for a board this loader knows nothing about, and `memtop` names the
highest segment to use -- it only ever lowers the ROM's top, never raises it.
Segment numbers are decimal or `0x` hex, and `reserve` includes both ends.

`flags` and `console` belong to the `os` entry above them. `console` says
which console that system is handed as `bi_console`, which it uses without
probing: `serial` hands over the serial line as written; `probe`, and no
`console` line at all, has kboot test the hi-res framebuffer, the low-res one
and then the LC (8563) card, and hand over the first that answers, or the serial line when
neither does. No value forces video, because an entry pinned to a card that
is not fitted would boot unusable. Only an entry that takes a handoff (`part`
or `flags`) is told anything.

Important limits:

- 1024-byte configuration file
- 8 menu entries
- 5 tokens per line
- 19-character labels
- 13-character kernel filenames
- partition slots 0 through 15
- 4 `reserve` lines, and 8 RAM runs in the handoff

Unknown keywords are ignored. Invalid requested flags, a `console` value other
than `serial` or `probe`, or missing partition handoffs reject the affected
menu entry. A missing configuration file uses the
compiled defaults; a valid file with no bootable entries enters recovery.

The menu accepts a digit to boot immediately, `j`/`k` to move, Enter to boot
the selected entry, `e` to edit it for the current session, and `r` for
recovery.

## License

MIT. See `LICENSE`.

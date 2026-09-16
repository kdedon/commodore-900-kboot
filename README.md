# Commodore 900 kboot

`kboot` is the Commodore 900 chainloader. The stock boot ROM loads it; kboot
then reads `kboot.cfg`, displays a menu, loads the selected kernel, and passes
the disk layout and boot flags to it.

## Build and test

    make deps
    make                  build build/kboot
    make COMPILER=cross   use the host cross-compiler
    make test             run host tests and mutation tests
    make size             check loader size limits
    make compiler-info
    make clean
    make help

The default `COMPILER=ours` runs the self-hosted Z8001 tools through the
emulator. `COMPILER=cross` uses a built `commodore-900-toolchain` checkout and
is faster. Select local inputs with `C900_ENV`, `C900_EMU`, or
`C900_TOOLCHAIN`.

`make size` checks both the loader's segment-copy limits and the boot-ROM disk
span. An oversized loader is rejected.

## Kernel handoff

`include/bootinfo.h` defines the optional structure passed to kernels that ask
for partition, swap, and boot-flag information. Kernels that do not use the
handoff can still be loaded.

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
    timeout <seconds>
    default <entry-number>

`part`, `swap`, and `bflag` apply to the nearest preceding `os` or `system`
block, falling back to file-wide defaults. A scope that defines any entries of
one kind replaces the outer set for that kind.

`flags` and `console` belong to the `os` entry above them. `console` says
which console that system is handed as `bi_console`, which it uses without
probing: `serial` hands over the serial line as written; `probe`, and no
`console` line at all, has kboot test the hi-res framebuffer and then the
low-res one, and hand over the card that answers, or the serial line when
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

Unknown keywords are ignored. Invalid requested flags, a `console` value other
than `serial` or `probe`, or missing partition handoffs reject the affected
menu entry. A missing configuration file uses the
compiled defaults; a valid file with no bootable entries enters recovery.

The menu accepts a digit to boot immediately, `j`/`k` to move, Enter to boot
the selected entry, `e` to edit it for the current session, and `r` for
recovery.

## License

MIT. See `LICENSE`.

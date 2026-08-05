# Kevin's Commodore 900 bootloader

**kboot** is the chainloader for the **Commodore 900**. The machine's stock boot
ROM loads it, then it:
* reads `kboot.cfg` off the disk
* offers a menu
* programs the drive geometry
* loads an operating-system kernel with correct 32-bit segmented addressing
* hands that kernel the disk layout
* jumps to it

## Building

Needs `make`, `cpp`, a POSIX shell with `awk` and `od`, and the
`commodore-900-toolchain` Z8001 cross compiler — no other language runtime.
`tools/toolchain.sh` finds the compiler from `$C900_TOOLCHAIN`, from
`$Z8001_TOOLCHAIN`, or from `$PATH`; it does not guess at a sibling checkout, so
one of those must be set.

    C900_TOOLCHAIN=/path/to/commodore-900-toolchain make        # -> build/kboot
    C900_TOOLCHAIN=/path/to/commodore-900-toolchain make size
    make clean                                                  # build/ and tests/build/

`make size` reports two independent budgets, either of which can fail the build:
the `TCOPY`/`DCOPY` segment copy caps `src/crt.s` imposes on itself, and the
boot partition span from which the ROM reads the loader. Do not raise the copy caps
as a larger copy overruns the ROM's live segment-1 data. Drop features instead.

## Testing

    make test

is the entry point: it runs the host test suite (`make -C tests`) and the
mutation gate (`make -C tests mutate`), which checks that those tests fail when
they should. Both exercise the config parser and the menu state machine on the
host, need no cross toolchain, and take under a second. See `tests/README.md`.

Loader behaviour itself cannot be tested here: it needs a machine emulator or
real hardware, neither of which is part of this repository.

## `include/bootinfo.h`

`bootinfo` is how kboot tells an operating system its disk layout — the
partition table and the swap extent, read from `kboot.cfg` — and which boot
flags the operator chose. It is offered but not required: kboot loads and starts
whatever a menu entry names, and a system that knows nothing about `bootinfo`
is still viable. Only an entry that asks for a handoff can fail to get one.

## `kboot.cfg`

`cfg/kboot.cfg.sample` is a working file: copy it to the boot partition beside
the loader and edit it. There is no other copy of the format — this section is
it.

Line-oriented. `#` starts a comment, tokens are whitespace-separated, the first
token is the keyword. Unknown keywords are ignored, so a file may carry keys
this loader does not have; every key has a default.

    geom <cyl> <heads> <spt> <precomp>       drive geometry
    system <label>                           opens a block: one OS's lines
    part <slot> <start> <blocks>             /dev/hd<slot>, blocks on the drive
    swap <slot> <bot> <top>                  swap, blocks within /dev/hd<slot>
    bflag <NAME> <bit>                       a boot-flag name and its bit
    os <label> <base-block> <file> [part]    a menu entry
    flags <NAME> ...                         flags that apply to the above entry
    timeout <seconds>                        0 or absence wait forever
    default <n>                              1-based; absence means the first

Labels are one token: `OC-3.5-single`, not `OC-3.5 single`.

* `geom` is programmed into the controller before any OS is loaded, and may be
larger than the ROM's own drive tables describe If absent, the compiled-in
`612x4x17` is programmed. 
* `part` slots are 0..15, a slot outside that is dropped,
a slot named twice keeps the last line, and a slot named in none is zero-length
and refuses every access. 
* `swap` bot >= top or absence means no swap.

An `os` entry's `<base-block>` is where that OS's filesystem starts on the
physical drive and `<file>` is the kernel in its root, matched against the full
14-byte directory field; the trailing `part` flag hands that kernel the
partition table, and an entry that asks for one and cannot be given one is
refused. `default` naming an entry that is not there is ignored, not clamped.

Limits: 1024 bytes of file, comments included — a longer file is read truncated
with a warning; 8 `os` entries, later ones dropped; 5 tokens a line; labels 19
characters; kernel filenames 13. A missing or unparseable file is not fatal —
the loader falls back to compiled-in defaults and says so — but a file that is
present and names nothing bootable goes to recovery rather than guessing.

**Position decides ownership.** `part`, `swap` and `bflag` lines belong to
whatever stands above them: the file itself before any `system` or `os` line,
the `system` block they follow, or the `os` entry they follow. Nothing refers
to a scope by name, and no label is ever repeated.

**Three scopes, nearest wins.** An entry takes its layout from its own lines
if it has any, otherwise from its `system` block, otherwise from the file's
defaults. Its flag vocabulary resolves the same way and separately, so a block
may hold the table while the names stay the file's.

**Inheritance is all or nothing.** A scope with any layout line of its own
inherits no part of the enclosing one, swap extent included: an entry that
declares its own table and no `swap` has no swap. `bflag` works the same way —
declaring any name means taking none of the outer ones. Half a vocabulary is
another kernel's bit under this kernel's word. A scope that declares nothing of
a kind is not a scope for that kind, and inheritance passes through it. The
menu marks an entry not running the file's default layout.

A `system` block is how one OS's layout is written once for several of its
entries — the shape a multi-user and a single-user entry of the same OS need:

    geom 1024 7 17 512

    system 3.5
    part 4 136 10200
    swap 3 2904 7000
    bflag SINGLE 1
    os OpenCoherent-3.5 136 coherent part
    os OC-3.5-single 136 coherent part
    flags SINGLE

    system 0.8
    part 4 60136 10200
    swap 4 1000 5000
    bflag SINGLE 4
    os OpenCoherent-0.8 60000 coherent part
    os OC-0.8-single 60000 coherent part
    flags SINGLE

Four entries, two layouts, two vocabularies. `SINGLE` is bit 1 under the first
block and bit 4 under the second because the two kernels were built that way,
and each entry reads the word against its own block — which is the whole point
of the names being the file's. A file with no `system` line behaves exactly as
it did before the keyword existed.

### Boot flags

`bflag` and `flags` are two halves of one idea: the file declares the names, an
entry spends them.

* **`bflag <NAME> <bit>` declares one.** The names are the *file's*, not the
  loader's — each pairs a word with a `BF_*` bit of the kernel that entry boots,
  so two entries running different kernels need not agree on a bit.
* **`flags <NAME> ...` chooses them.** The named bits are OR-ed. Four names to a
  line; write another `flags` line for more.
* **A name the entry's vocabulary does not define refuses the entry.** An
  unknown *keyword* is ignored, for compatibility — but a flag asked for and not
  delivered boots something indistinguishable from what was wanted.
* **Only an `os` entry can carry flags.** A `flags` line above the first `os`
  line applies to nothing, and neither does one in a `system` block's header: a block
  is not an entry. Both are reported.
* **Order within a scope does not matter.** A name is read against the
  vocabulary its entry ends up with, whichever side of the `flags` line the
  `bflag` lines were written.

In the sample above, `bflag SINGLE 1` and `flags SINGLE` under the first entry
are that entry booting single-user. Delete the `flags` line to boot it
multi-user.

`cfg/kboot.cfg.sample` is parsed by `make test` through the real parser — the
resolved flags included — so it is a file that works, not an illustration.

## The menu

    kboot: Commodore 900 boot
    > 1) OpenCoherent-3.5
      2) CPM-8000
    [1-9] boot  [j/k] move  [enter] boot selected
    [e] edit entry  [r] recovery
    boot 1 in 5>

- A digit boots its entry outright.
- `timeout` and `default` in `kboot.cfg` boot an entry unattended; absence of
  `timeout` means wait forever.
- `e` edits the marked entry and verifies it against the disk. Edits last until
  the next reset — kboot has no write path.
- `flags` in `kboot.cfg` asks for boot flags by name, from the names its `bflag`
  lines give that entry's kernel. The menu shows what each entry carries; a name
  the file does not define refuses the entry rather than booting it without.
- `r` is recovery. A `kboot.cfg` that is present but names nothing bootable
  goes here with no countdown; an absent one boots the compiled-in defaults.

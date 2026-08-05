#!/bin/sh
# deps.sh -- resolve what this build consumes from another repository, and
# refuse by name when it is missing.
#
#   sh tools/deps.sh <dep>              print the resolved path, or nothing
#   sh tools/deps.sh -n <dep> [value]   print nothing; refuse and exit 2 if
#                                       <value> (or, empty, the search) does
#                                       not resolve
#
# The two modes exist because the search runs when the Makefile is read, so a
# variable can be assigned from it, while the REFUSAL belongs where the thing
# is wanted.  -n takes the make variable's current value, so a wrong
# C900_TOOLCHAIN= is refused as what the user asked for rather than silently
# re-searched.
#
#   dep         variable                  what it names
#   ours        C900_ENV                  a compiler ENVIRONMENT: the `ours'
#                                         guest root, whose cc0/cc1/cc2, as and
#                                         ld are Z8001 binaries
#   emu         C900_EMU                  the emulator binary, which is how a
#                                         Z8001 compiler runs on a host
#   toolchain   C900_TOOLCHAIN, Z8001_TOOLCHAIN
#                                         the toolchain's BUILD directory, for
#                                         the gcc-built cross passes
#
# The first two serve COMPILER=ours, the default; the third serves
# COMPILER=cross.  mk/compiler.mk says what each flavour is.
#
# Search order: the variable wins; then an installed copy ($PATH, or the
# unpacked release `make deps' places under deps/); then a sibling checkout,
# walking outward AT MOST THREE PARENTS, then one inside a `repos/' directory
# beside this repository.  Three parents is what reaches the enclosing
# workspace from a repository staged at <workspace>/repos/<repo>; further out
# is not a sibling, it is a coincidence -- an unbounded walk finds another
# job's checkout on a CI runner and reports a false success.
#
# A CHECKOUT OUTRANKS AN UNPACKED RELEASE for the compiler environment: someone
# with a built toolchain beside this repository is testing that compiler, and a
# pinned archive that quietly won would test last month's.
#
# tools/toolchain.sh is the caller's entry point and keeps its own name; this
# file holds the search so that `make deps', which clones a sibling, and the
# build, which looks for one, cannot drift apart.

root=$(cd "$(dirname "$0")/.." && pwd)

# The sibling search list for a repository name: three parents, then repos/.
siblings() {
	_d=$root
	_n=0
	while [ $_n -lt 3 ] && [ "$_d" != / ]; do
		_d=$(cd "$_d/.." && pwd)
		echo "$_d/$1"
		_n=$((_n + 1))
	done
	echo "$root/repos/$1"
}

case "$1" in
-n) mode=need; dep=$2; given=$3 ;;
*)  mode=find; dep=$1; given= ;;
esac

case "$dep" in
ours)
	VAR="C900_ENV"
	WANT="a Z8001 compiler environment (the \`ours' guest root)"
	LIST=
	for d in $(siblings commodore-900-toolchain); do
		LIST="$LIST $d/host/build/env/ours"
	done
	LIST="$LIST $root/deps/env-ours"
	[ -n "$given" ] || given=${C900_ENV:-}
	fixup() { echo "$1"; }
	# The compiler passes are in lib/, the assembler and linker in bin/, which
	# is where the guest's own driver looks for them.  Nothing here needs
	# lib/libc.a or usr/include: the loader is freestanding and links against
	# its own crt.s alone.
	ok() {
		[ -n "$1" ] &&
		[ -f "$1/lib/cc0" ] && [ -f "$1/lib/cc1" ] && [ -f "$1/lib/cc2" ] &&
		[ -f "$1/bin/as" ] && [ -f "$1/bin/ld" ]
	}
	HOW="  kboot is a Z8001 program and COMPILER=ours compiles it with the
  self-hosted compiler -- Z8001 binaries, run under the emulator.  They come
  as a dist:
      make deps DEP=ours
  which unpacks the release DEPS pins into deps/env-ours.  Or compose one from
  a toolchain checkout and name it:
      COHERENT_OS=/path/to/commodore-900-coherent/os \\
          make -C commodore-900-toolchain env CCENV=ours
      C900_ENV=/path/to/commodore-900-toolchain/host/build/env/ours make
  or build with the gcc cross compiler instead: make COMPILER=cross."
	;;
emu)
	VAR="C900_EMU"
	WANT="the Commodore 900 emulator"
	LIST="$root/deps/commodore-900-emulator"
	p=$(command -v c900 2>/dev/null) && LIST="$LIST $p"
	LIST="$LIST $(siblings commodore-900-emulator)"
	[ -n "$given" ] || given=${C900_EMU:-}
	# A directory names the checkout, a file the binary; both spellings are
	# accepted and the BINARY is printed, because that is what the wrappers
	# exec.  On Windows it is c900.exe.
	fixup() {
		if [ -d "$1" ]; then
			if [ -f "$1/bin/c900" ]; then echo "$1/bin/c900"
			else echo "$1/bin/c900.exe"; fi
		else echo "$1"; fi
	}
	# -f as well as -x: every directory is executable, so -x alone accepts a
	# checkout path and prints it as if it were the binary.
	ok() { [ -f "$1" ] && [ -x "$1" ]; }
	HOW="  The compiler of COMPILER=ours is a Z8001 program, and \`c900 --exec'
  is what runs one on a host.  It is not built here:
      make deps DEP=emu
  unpacks the release DEPS pins into deps/commodore-900-emulator, or
      git clone https://github.com/MichalPleban/commodore-900-emulator
      make -C commodore-900-emulator
  beside this repository, or put c900 on \$PATH, or set C900_EMU to the
  checkout or to its bin/c900.  COMPILER=cross needs no emulator."
	;;
toolchain)
	VAR="C900_TOOLCHAIN"
	WANT="the Z8001 cross toolchain"
	LIST=
	p=$(command -v cc0-z8001 2>/dev/null) &&
		LIST="$(dirname "$p") $(dirname "$(dirname "$p")")"
	for d in $(siblings commodore-900-toolchain); do
		LIST="$LIST $d"
	done
	[ -n "$given" ] || given=${C900_TOOLCHAIN:-${Z8001_TOOLCHAIN:-}}
	# A checkout is resolved to its host/build; a build directory names
	# itself, which is what $Z8001_TOOLCHAIN has always been allowed to be.
	fixup() { if ok "$1/host/build"; then echo "$1/host/build"; else echo "$1"; fi; }
	# The compiler passes live in <dir>/z8001 -- that is how the toolchain
	# publishes them; as and ld live in <dir> itself.  A checkout that is
	# present but not yet BUILT resolves to nothing here, on purpose.
	ok() {
		[ -n "$1" ] &&
		[ -x "$1/z8001/cc0-z8001" ] && [ -x "$1/z8001/cc1-z8001" ] &&
		[ -x "$1/z8001/cc2-z8001" ] &&
		[ -x "$1/as-z8001" ] && [ -x "$1/ld-z8001" ]
	}
	HOW="  kboot is a Z8001 program and needs the commodore-900-toolchain cross
  compiler.  Build that repository and name it:
      git clone https://github.com/kdedon/commodore-900-toolchain
      make -C commodore-900-toolchain
      C900_TOOLCHAIN=/path/to/commodore-900-toolchain make
  or put cc0-z8001 and friends on \$PATH, or run \`make deps' to clone it
  beside this repository.  Everything else kboot needs is in this repository."
	;;
*)
	echo "deps.sh: unknown dependency \`$dep' (ours, emu, toolchain)" >&2
	exit 2
	;;
esac

found=
if [ -n "$given" ]; then
	given=$(fixup "$given")
	ok "$given" && found=$given
else
	for c in $LIST; do
		c=$(fixup "$c")
		ok "$c" && { found=$c; break; }
	done
fi

if [ -n "$found" ]; then
	[ "$mode" = find ] && echo "$found"
	exit 0
fi

[ "$mode" = find ] && exit 0

{
	if [ -n "$given" ]; then
		echo "*** $WANT: nothing usable at $VAR=$given."
		echo "*** That is $VAR's own value, so nothing else was tried."
		echo "*** Unset it to search these instead:"
	else
		echo "*** $WANT: none found, and this target needs one."
		echo "*** $VAR is unset; the paths tried were:"
	fi
	for c in $LIST; do echo "***     $c"; done
	echo "$HOW" | sed 's/^/*** /'
} >&2
exit 2

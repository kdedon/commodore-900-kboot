# mk/guest/common.sh -- run one pass of the self-hosted Z8001 compiler.
# Sourced by the cc0/cc1/cc2/cpp/as/ld wrappers beside it; not executable.
#
# Every tool in this flavour is a Z8001 COHERENT binary out of a compiler
# ENVIRONMENT -- bin/{as,ld,cc,ar}, lib/{cc0,cc1,cc2,crts0.o,libc.a},
# usr/include -- so "running" one means handing it to the emulator's process
# runner, `c900 --exec', which services its system calls against the host
# filesystem.  There is exactly one guest process per invocation: the runner
# has no fork, so the environment's own `cc' driver cannot be used at all, and
# make above these wrappers does the sequencing it would have done.
#
# The environment and the emulator are resolved by tools/deps.sh -- the same
# search `make deps' places things for and mk/compiler.mk reads -- so a shell
# and make cannot answer differently.  Either is overridable by variable:
# C900_ENV, C900_EMU.
#
# PATHS.  A path handed to a guest process as an ARGUMENT is a guest path only
# when it is ABSOLUTE: $N2ROOT reroots the guest's absolute opens into the
# environment and leaves relative ones meaning the host's current directory,
# which is where make runs.  So sources, objects and intermediates are named
# relatively and stay in build/obj -- which is also what makes an object built
# here byte-comparable with one the cross flavour built.  The program itself is
# named by a HOST path and is not rerooted.

set -e

# The repository root: this file is at <root>/mk/guest/common.sh.
guest_here=$(cd "$(dirname "$0")" && pwd)
: "${C900_ROOT:=$(cd "$guest_here/../.." && pwd)}"

guest_die() { echo "mk/guest: $*" >&2; exit 1; }

: "${C900_ENV:=$(sh "$C900_ROOT/tools/deps.sh" ours)}"
: "${C900_EMU:=$(sh "$C900_ROOT/tools/deps.sh" emu)}"

# guest_need: the environment and the emulator, refused by name at the command
# that wanted them rather than left to surface as the runner's "cannot open".
# An environment is resolved to a REAL directory: a built toolchain publishes
# its env by rename, so env/ours is a symlink there, and $N2ROOT does not
# follow one -- a guest under it then finds nothing at all.
guest_need() {
	[ -n "$C900_ENV" ] || sh "$C900_ROOT/tools/deps.sh" -n ours
	[ -n "$C900_EMU" ] || sh "$C900_ROOT/tools/deps.sh" -n emu
	C900_ENV=$(cd "$C900_ENV" && pwd -P)
}

# guest_run <tool-relative-to-the-environment> [args...]: one guest process.
# N2QUIET silences the runner's per-process `[exit N]' line, which would
# otherwise land in build/build.log once per pass.
guest_run() {
	guest_t=$1
	shift
	[ -f "$C900_ENV/$guest_t" ] || guest_die "$C900_ENV has no $guest_t"
	N2ROOT=$C900_ENV N2QUIET=1 "$C900_EMU" --exec "$C900_ENV/$guest_t" "$@"
}

# guest_tmp <output>: the private name a pass writes to.  Its output is moved
# into place by guest_done only on success -- a pass that fails after opening
# its output leaves a short or empty file behind, and a make that finds that
# file newer than its source treats the failed compile as a finished one on the
# next run.
guest_tmp() { echo "$1.tmp$$"; }

# guest_done <output> <status>: publish or clean up.
guest_done() {
	if [ "$2" = 0 ]; then
		mv "$(guest_tmp "$1")" "$1"
	else
		rm -f "$(guest_tmp "$1")"
		exit "$2"
	fi
}

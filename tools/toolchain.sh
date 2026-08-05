#!/bin/sh
# toolchain.sh -- locate the Z8001 cross toolchain and print its build directory.
#
# This is COMPILER=cross's entry point.  The default flavour compiles with the
# self-hosted compiler instead and wants no toolchain checkout at all; see
# mk/compiler.mk.
#
# It resolves a directory holding cc0/cc1/cc2-z8001, as-z8001 and ld-z8001.
# The search itself is tools/deps.sh, which every dependency of this repository
# goes through and which `make deps' places clones for; this file is the
# caller's entry point and keeps its name because the Makefile and the tests
# use it.
#
# Resolution order (first complete toolchain wins):
#   1. $C900_TOOLCHAIN            a commodore-900-toolchain checkout
#   2. $Z8001_TOOLCHAIN           either a checkout or a host/build directory
#   3. cc0-z8001 on $PATH         an installed toolchain, like gcc
#   4. a sibling checkout, bounded at three parents, then repos/
#
# Prints the directory on stdout; exits 2 with an explanation if there is none.
# The compiler passes live in <dir>/z8001 (that is how the toolchain publishes
# them); as/ld live in <dir> itself.
HERE=$(cd "$(dirname "$0")" && pwd)

d=$(sh "$HERE/deps.sh" toolchain)
if [ -n "$d" ]; then
	echo "$d"
	exit 0
fi

sh "$HERE/deps.sh" -n toolchain
exit 2

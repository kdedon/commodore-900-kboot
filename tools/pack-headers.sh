#!/bin/sh
# pack-headers.sh -- package include/bootinfo.h, the loader->kernel ABI, on
# its own.
#
#	sh tools/pack-headers.sh [VERSION] [DESTDIR]
#
# Writes c900-kboot-headers-v<V>.tar.gz.  VERSION defaults to a git describe
# off this checkout, DESTDIR to build/dist.
#
# A kernel build compiles against this one header and needs nothing else from
# kboot: no binary, no source tree, no build of its own.  Packaged apart from
# build/kboot so a header-only consumer -- what a KERNEL build is -- unpacks a
# header-only archive instead of an archive built to run on the target.
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)

V=${1:-}
[ -n "$V" ] || V=$(git -C "$HERE" describe --tags --always 2>/dev/null) || V=0.0.0-dev
V=${V#v}
DEST=${2:-$HERE/build/dist}

name="c900-kboot-headers-v$V"
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
A="$W/$name"
mkdir -p "$A/include"
cp "$HERE/include/bootinfo.h" "$A/include/"
echo "$V" > "$A/VERSION"
mkdir -p "$DEST"
(cd "$W" && tar czf "$name.tar.gz" "$name")
mv "$W/$name.tar.gz" "$DEST/"
echo "packed: $DEST/$name.tar.gz"

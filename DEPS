# DEPS -- what this repository consumes from other repositories.
#
#	name  kind  url  [ref]  [asset]  [dest]
#
# Read by `make deps' (tools/deps-fetch.sh) and by CI.  It is a statement of
# what is consumed, not a resolver: nothing in the build reads this file.
# tools/deps.sh still does the finding, and a named variable still wins.
#
# kind release = a binary, unpacked under deps/.  <dest> names that
# directory and defaults to the url's basename, which is right when the asset
# is the publishing repository's own product.  <ref> is a tag, or `latest' to
# resolve the newest published release at fetch time instead of a number
# chosen by hand.
#
# kboot is a Z8001 program and its whole outside world is a compiler and a way
# to run one.  Nothing here boots anything, so there is no OS edge.
#
#   toolchain  THE COMPILER: cc0/cc1/cc2, as and ld, built for the host, taken
#              as the kernel and CP/M take them.  The archive lays out a
#              host/build view whose paths are the ones a built checkout
#              spells, so a build never learns which shape it got, and a
#              developer iterating names a checkout of their own with
#              C900_TOOLCHAIN.
#
#   emu        THE RUNNER, for the tests: what a test here compiles is a Z8001
#              program, and `c900 --exec' is what runs one on a host.  At
#              `latest': nothing here depends on a particular emulator
#              revision.

toolchain  release  https://github.com/kdedon/commodore-900-toolchain  latest  c900-toolchain-@REF@-@HOST@
emu        release  https://github.com/kdedon/commodore-900-emulator   latest  c900-@REF@-@HOST@

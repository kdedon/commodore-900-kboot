# DEPS -- what this repository consumes from other repositories.
#
#	name  kind  url  [ref]  [asset]  [dest]
#
# Read by `make deps' (tools/deps-fetch.sh) and by CI.  It is a statement of
# what is consumed, not a resolver: nothing in the build reads this file.
# tools/deps.sh still does the finding, and a named variable still wins.
#
# kind git = one of ours, cloned to ../<name> and left floating on <ref>.
# kind release = a binary, pinned by tag and unpacked under deps/.  <dest>
# names that directory, for an asset that is not the publishing repository's
# own product -- as `ours' is not.
#
# kboot is a Z8001 program and its whole outside world is a compiler and a way
# to run one.  Nothing here boots anything, so there is no OS edge.
#
#   ours       THE COMPILER, as a dist: the `ours' guest root -- cc0/cc1/cc2,
#              as and ld as Z8001 binaries, with the C library and headers they
#              were built against.  A consumer needs no toolchain checkout, no
#              OS checkout and no compiler build; it unpacks this and compiles.
#              Pinned, because a compiler is a binary and "which one built
#              this" has to be a number chosen in advance rather than a branch
#              tip; the unpacked root's .provenance answers it from the other
#              end.
#
#              A BOOTSTRAP EDGE.  The toolchain repository publishes it only
#              because commodore-900-coherent is unpublished and the
#              toolchain <-> OS cycle has to be broken somewhere.  The image
#              belongs in the OS repository's dist package -- that repository
#              owns libc, csu and the headers -- and when it ships one, this
#              line's url and tag change and nothing else does.
#
#   emu        THE RUNNER.  `ours' is a Z8001 compiler, so a host build of this
#              loader is a series of guest processes under `c900 --exec'.
#
#   toolchain  the same compiler built with gcc, for COMPILER=cross: many times
#              faster, and what a developer iterating wants.  Floating, because
#              that is the version under development and a change there
#              breaking this link is what the check is for.  COMPILER=cross
#              needs neither of the edges above.

ours       release  https://github.com/kdedon/commodore-900-toolchain  fallback-1                @REF@-ours.tar.gz  env-ours
emu        release  https://github.com/kdedon/commodore-900-emulator   v0.1                    c900-@REF@-@HOST@
toolchain  git      https://github.com/kdedon/commodore-900-toolchain  main

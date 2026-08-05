# DEPS -- what this repository consumes from other repositories.
#
#	name  kind  url  [ref]  [asset]
#
# Read by `make deps' (tools/deps-fetch.sh) and by CI.  It is a statement of
# what is consumed, not a resolver: nothing in the build reads this file.
# tools/deps.sh still does the finding, and a named variable still wins.
#
# kind git = one of ours, cloned to ../<name> and left floating on <ref>.
# kind release = a third-party binary, pinned by tag under deps/.
#
# kboot is a Z8001 program and that is its whole outside world: one compiler.
# Nothing here boots anything, so there is no emulator edge and no OS edge.

toolchain  git  https://github.com/kdedon/commodore-900-toolchain  main

# compiler.mk -- select WHICH C compiler builds the loader.  Include, do not run.
#
#	make ... COMPILER=<flavour>
#
# `make compiler-info' prints what the flavour resolved to and, if it did not
# resolve, why.  Resolved means every binary the flavour names exists, no more.
#
# --- the flavours -----------------------------------------------------------
#
#   cross   THE ONLY ONE.  The Z8001 compiler built for the HOST with gcc:
#           cc0/cc1/cc2, as and ld, taken from the toolchain RELEASE that DEPS
#           names, and invoked as three passes in sequence -- which is how the
#           kernel and CP/M compile for this machine, in link-kernel.sh and in
#           their own config.mk.  make does the sequencing a driver would.
#
# A flavour that cannot resolve is not an error here: `make compiler-info' must
# be able to report on it, and a goal that compiles nothing (clean, test, deps)
# must not be stopped by one.  The refusal happens where a compiler is wanted.
COMPILER ?= cross

C900_MKDIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
C900_ROOT  := $(abspath $(C900_MKDIR)/..)
C900_DEPS  := sh $(C900_ROOT)/tools/deps.sh

C900_CC_WHY :=

# A value the caller gave goes THROUGH the resolver, never around it: deps.sh
# is where a checkout becomes its host/build and an emulator directory becomes
# its bin/c900.  `?=' skipped that for anything set in the environment and used
# it raw, so C900_TOOLCHAIN=<checkout> looked for the passes one directory above
# where they are.  The value goes back in through the resolver's own VARIABLE,
# which is the only channel find mode reads; a positional argument is ignored
# there, and passing one gets you the sibling search's answer instead of yours.
ifeq ($(COMPILER),cross)
  C900_TOOLCHAIN := $(shell C900_TOOLCHAIN='$(C900_TOOLCHAIN)' $(C900_DEPS) toolchain)
  ifeq (,$(C900_TOOLCHAIN))
    C900_CC_WHY := no Z8001 cross toolchain (set C900_TOOLCHAIN, or run `sh tools/toolchain.sh' for the paths tried, or `make deps DEP=toolchain')
  else
    # deps.sh resolves a checkout to its host/build and an unpacked release to
    # the host/build view it lays out; both spell the passes in z8001/ with as
    # and ld beside them.
    TC  := $(C900_TOOLCHAIN)
    CC0  = $(TC)/z8001/cc0-z8001
    CC1  = $(TC)/z8001/cc1-z8001
    CC2  = $(TC)/z8001/cc2-z8001
    # The host's own cpp, for the .s sources only: -traditional-cpp because a
    # standard one mangles the assembler's `/' comments and `'' characters.
    CPP  = cpp
    CPPFLAGS = -traditional-cpp -P
    AS   = $(TC)/as-z8001
    LD   = $(TC)/ld-z8001
    C900_CC_ID = cross, $(TC)
  endif
endif

ifeq (,$(CC0)$(C900_CC_WHY))
C900_CC_WHY := unknown COMPILER=$(COMPILER) (known: cross)
endif

.PHONY: compiler-info
compiler-info:
	@echo "COMPILER   = $(COMPILER)"
ifeq (,$(C900_CC_WHY))
	@echo "status     = resolved"
	@echo "cc0        = $(CC0)"
	@echo "cc1        = $(CC1)"
	@echo "cc2        = $(CC2)"
	@echo "cpp        = $(CPP) $(CPPFLAGS)"
	@echo "as         = $(AS)"
	@echo "ld         = $(LD)"
	@echo "compiler   = $(C900_CC_ID)"
else
	@echo "status     = UNRESOLVED"
	@echo "reason     = $(C900_CC_WHY)"
endif
# end of compiler.mk

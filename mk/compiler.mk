# compiler.mk -- select WHICH C compiler builds the loader.  Include, do not run.
#
#	make ... COMPILER=<flavour>
#
# `make compiler-info' prints what the flavour resolved to and, if it did not
# resolve, why.  Resolved means every binary the flavour names exists, no more.
#
# --- the flavours -----------------------------------------------------------
#
#   ours    THE DEFAULT.  The self-hosted compiler: cc0/cc1/cc2, as and ld as
#           Z8001 binaries, out of a compiler environment (the `ours' guest
#           root), each run as one guest process under the emulator's process
#           runner by the wrappers in mk/guest/.  This is the compiler the
#           machine itself runs, so what CI builds is what a C900 would.
#
#   cross   The same compiler built for the HOST with gcc, out of a
#           commodore-900-toolchain checkout.  Byte for byte the same objects
#           and many times faster, which is what a developer iterating wants.
#
# Neither is a different compiler from the other: one is compiled by gcc for
# this host, the other by itself for the target.  An object built by one and an
# object built by the other are expected to compare equal, and that is a gate
# the toolchain runs (its self-host fixpoint).
#
# A flavour that cannot resolve is not an error here: `make compiler-info' must
# be able to report on it, and a goal that compiles nothing (clean, test, deps)
# must not be stopped by one.  The refusal happens where a compiler is wanted.
COMPILER ?= ours

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
ifeq ($(COMPILER),ours)
  C900_ENV := $(shell C900_ENV='$(C900_ENV)' $(C900_DEPS) ours)
  C900_EMU := $(shell C900_EMU='$(C900_EMU)' $(C900_DEPS) emu)
  ifeq (,$(C900_ENV))
    C900_CC_WHY := no compiler environment (set C900_ENV, or run `sh tools/deps.sh -n ours' for the paths tried, or `make deps DEP=ours')
  else ifeq (,$(C900_EMU))
    C900_CC_WHY := the compiler of this flavour is a Z8001 program and needs the emulator to run it (set C900_EMU, or run `sh tools/deps.sh -n emu', or `make deps DEP=emu')
  else
    O   := $(C900_MKDIR)/guest
    CC0  = $(O)/cc0
    CC1  = $(O)/cc1
    CC2  = $(O)/cc2
    CPP  = $(O)/cpp
    # -P suppresses the `#line' markers the assembler rejects.  There is no
    # -traditional-cpp here: this preprocessor has no other dialect.
    CPPFLAGS = -P
    AS   = $(O)/as
    LD   = $(O)/ld
    # The environment says what it is composed of; `release' when it came from
    # a dist, absent when it was composed locally.
    C900_CC_ID = ours, $(C900_ENV)$(shell test -f $(C900_ENV)/.provenance && \
	awk '$$1=="release"{printf " (%s)", $$2} $$1=="toolchain"{printf " toolchain %.12s", $$2}' \
	$(C900_ENV)/.provenance)
  endif
endif

ifeq ($(COMPILER),cross)
  C900_TOOLCHAIN := $(shell C900_TOOLCHAIN='$(C900_TOOLCHAIN)' $(C900_DEPS) toolchain)
  ifeq (,$(C900_TOOLCHAIN))
    C900_CC_WHY := no Z8001 cross toolchain (set C900_TOOLCHAIN, or run `sh tools/toolchain.sh' for the paths tried)
  else
    # deps.sh resolves a checkout to its host/build, which is where the
    # toolchain publishes: passes in z8001/, as and ld beside them.
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
C900_CC_WHY := unknown COMPILER=$(COMPILER) (known: ours cross)
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

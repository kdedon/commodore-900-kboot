# Makefile -- kboot, the Commodore 900 chainloader.
#
# Produces build/kboot, an l.out the stock boot ROM loads (installed on the
# media as /coherent) and which then loads a real OS kernel with correct
# 32-bit addressing.  See README.md.
#
#   make        build build/kboot
#   make test   build and run the host tests, mutation gate included
#   make size   report the segment sizes of an existing build/kboot
#   make deps   acquire what DEPS says this repository consumes
#   make compiler-info   what COMPILER= resolved to, or why it did not
#   make clean  remove build/ and tests/build/
#
# The compiler is selected, not hard-coded: COMPILER=ours (the default) builds
# with the self-hosted Z8001 compiler under the emulator, COMPILER=cross with
# the gcc-built host cross compiler.  mk/compiler.mk holds the flavours.

SHELL = /bin/sh
.DELETE_ON_ERROR:
.PHONY: all test size deps clean

# Set before the include: mk/compiler.mk defines the first target make would
# otherwise take as the default goal.
.DEFAULT_GOAL := all
include mk/compiler.mk

# Goals that must work with no compiler present: `deps' is how one is obtained,
# `compiler-info' exists to report that there is none, and the tests build with
# the host cc.
FREE = clean test deps compiler-info
ifeq ($(strip $(MAKECMDGOALS)),)
NEED = all
else
NEED = $(filter-out $(FREE),$(MAKECMDGOALS))
endif

ifneq ($(NEED),)
ifneq (,$(C900_CC_WHY))
$(error COMPILER=$(COMPILER): $(C900_CC_WHY))
endif
endif

# cc0/cc1 variant word: the 16-bit segmented (VLARGE) model this machine uses.
VAR ?= 800000020800

INCS = -Isrc -Iinclude
DEFS =

OBJDIR = build/obj
LOG    = build/build.log
LOADER = build/kboot

HDRS = $(wildcard src/*.h) $(wildcard include/*.h)
# C first, then crt.s: the layout is absolute, so link order is part of the
# artifact.
OBJ  = $(patsubst src/%.c,$(OBJDIR)/%.o,$(wildcard src/*.c)) \
       $(patsubst src/%.s,$(OBJDIR)/%.o,$(wildcard src/*.s))

all: $(LOADER)

# Never a prerequisite of a build: a build that silently fetched would decide
# for you which version of another repository you are testing against.
deps:
	sh tools/deps-fetch.sh $(DEP)

test:
	$(MAKE) -C tests
	$(MAKE) -C tests mutate

# crt.s self-relocation copy caps, in bytes.  MUST match TCOPY/DCOPY in
# src/crt.s, which copies exactly this much of each segment.  Raising them puts
# the data copy over the ROM's live segment-1 data at 0xE400, so an overflow is
# answered by dropping features.
TCOPY = 0x4000
DCOPY = 0x4000

# The boot span: the loader file and kboot.cfg must fit the blocks the ROM can
# read before the true geometry is programmed.  Every ROM geometry has spt=17,
# so block n < heads*17 is cylinder 0, head n/17 under all of them; past that
# the mapping depends on a geometry nobody has established.  An automatic boot
# always runs the 4-head table.  MUST match ROM_MIN_HEADS in the image builders
# (commodore-900-cpm tools/cohfs.py, coherent os/hostbuild/mkimage.py).
SPANHEADS = 4
SPANSPT   = 17

# Link at seg 0x30, where the ROM maps the load.  loutsize.sh exits nonzero on
# an overflow and .DELETE_ON_ERROR then removes the binary, so a loader that
# would run truncated does not survive the build.
$(LOADER): $(OBJ)
	$(LD) -i -L -e start -R 0x30000000 -o $@ $(OBJ) > $(OBJDIR)/link.txt 2>&1
	@echo "kboot: $$(wc -c < $@) bytes, compiler $(C900_CC_ID)"
	sh tools/loutsize.sh --tcap $(TCOPY) --dcap $(DCOPY) \
		--span-heads $(SPANHEADS) --span-spt $(SPANSPT) $@

size: $(LOADER)
	sh tools/loutsize.sh --tcap $(TCOPY) --dcap $(DCOPY) \
		--span-heads $(SPANHEADS) --span-spt $(SPANSPT) $(LOADER)

# cc2 flags 0012 = VPEEP+VKERN: frame references relocated against SS, which
# src/crt.s defines, as the ROM routines kboot calls use.
$(OBJDIR)/%.o: src/%.c $(HDRS) | $(OBJDIR)
	$(CC0) $(VAR) $< $(OBJDIR)/$*.z0 $(INCS) $(DEFS) >> $(LOG) 2>&1
	$(CC1) $(VAR) $(OBJDIR)/$*.z0 $(OBJDIR)/$*.z1 >> $(LOG) 2>&1
	$(CC2) 0012 $(OBJDIR)/$*.z1 $@ $(OBJDIR)/$*.scr 0 >> $(LOG) 2>&1

$(OBJDIR)/%.o: src/%.s $(HDRS) | $(OBJDIR)
	$(CPP) $(CPPFLAGS) $(DEFS) $(INCS) $< > $(OBJDIR)/$*.i 2>> $(LOG)
	$(AS) -g -o $@ $(OBJDIR)/$*.i >> $(LOG) 2>&1

$(OBJDIR):
	mkdir -p $(OBJDIR)
	: > $(LOG)

clean:
	rm -rf build
	$(MAKE) -C tests clean

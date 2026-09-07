# Makefile -- the host tests.  See README.md.
#
#   make -C tests          build and run them
#   make -C tests mutate   prove they can fail, by breaking the code under test
#   make -C tests clean
#
# The host compiler, not the cross one: these run the loader's pure logic on
# whatever machine you are sitting at.  -std=gnu89 because the loader is K&R C
# and a modern default dialect rejects its function definitions outright.

SHELL = /bin/sh
.DELETE_ON_ERROR:
.PHONY: all run mutate clean

HCC    = cc
# The shipped sample is test input: cfgtest parses the real file, so it cannot
# drift from the parser unnoticed.
SAMPLE = ../cfg/kboot.cfg.sample
HFLAGS = -std=gnu89 -w -I../src -I../include -Ibuild -DCFGSAMPLE=\"$(SAMPLE)\"
OUT    = build

TESTS = $(OUT)/cfgtest $(OUT)/uitest $(OUT)/bitest $(OUT)/scctest

all: run

run: $(TESTS)
	@rc=0; for t in $(TESTS); do $$t || rc=1; done; exit $$rc

$(OUT)/cfgtest: cfgtest.c ../src/cfg.c $(SAMPLE) | $(OUT)
	$(HCC) $(HFLAGS) -o $@ cfgtest.c ../src/cfg.c

$(OUT)/uitest: uitest.c concon.c ../src/ui.c ../src/cfg.c | $(OUT)
	$(HCC) $(HFLAGS) -o $@ uitest.c concon.c ../src/ui.c ../src/cfg.c

# The handoff.  bipack.c is the real fill, linked as it ships: no ROM, no
# disk, so the test runs the loader's own code and not a copy of it.
$(OUT)/bitest: bitest.c ../src/bipack.c ../include/bootinfo.h | $(OUT)
	$(HCC) $(HFLAGS) -o $@ bitest.c ../src/bipack.c

# The probe.  scctest.c #includes ../src/scc.c with a fake bus under it, so
# there is no second object to link -- the file under test IS the shipped one.
$(OUT)/scctest: scctest.c ../src/scc.c | $(OUT)
	$(HCC) $(HFLAGS) -o $@ scctest.c

$(OUT):
	mkdir -p $(OUT)

# The gate on the gate: break the code under test in ways that would really be
# mistyped, and require the suite to notice each one.  The two layout
# mutations are the failures that boot a system on another system's disk
# layout, which is the fault this grammar exists to make impossible.
# A mutation of include/bootinfo.h needs the generated copy searched BEFORE
# the shipped one, which is the only thing this ordering does.
MFLAGS = -std=gnu89 -w -I$(OUT) -I../src -I../include -DCFGSAMPLE=\"$(SAMPLE)\"

mutate: | $(OUT)
	@fail=0; \
	sed 's/if (klen >= BI_LEN4)/if (1)/' ../src/bipack.c > $(OUT)/m15.c; \
	$(HCC) $(HFLAGS) -o $(OUT)/m15 bitest.c $(OUT)/m15.c 2>/dev/null; \
	if $(OUT)/m15 >/dev/null 2>&1; then \
		echo "MUTATE FAIL: bitest passed a loader that writes bi_serial into an old block"; fail=1; \
	else echo "mutate ok: a field written past a kernel's length is caught"; fi; \
	sed 's/bp->bi_sum = -bisum(bp);/bp->bi_sum = bisum(bp);/' ../src/bipack.c > $(OUT)/m16.c; \
	$(HCC) $(HFLAGS) -o $(OUT)/m16 bitest.c $(OUT)/m16.c 2>/dev/null; \
	if $(OUT)/m16 >/dev/null 2>&1; then \
		echo "MUTATE FAIL: bitest passed a block whose checksum does not clear"; fail=1; \
	else echo "mutate ok: a checksum that does not sum to zero is caught"; fi; \
	sed 's/miss |= BIU_SERIAL;/;/' ../src/bipack.c > $(OUT)/m17.c; \
	$(HCC) $(HFLAGS) -o $(OUT)/m17 bitest.c $(OUT)/m17.c 2>/dev/null; \
	if $(OUT)/m17 >/dev/null 2>&1; then \
		echo "MUTATE FAIL: bitest passed a loader that hides what it could not deliver"; fail=1; \
	else echo "mutate ok: an undelivered field reported as delivered is caught"; fi; \
	sed 's/if (i == 0 \&\& conser)/if (0)/' ../src/scc.c > $(OUT)/m19.c; \
	$(HCC) $(HFLAGS) -DSCCSRC=\"$(OUT)/m19.c\" -o $(OUT)/m19 scctest.c 2>/dev/null; \
	if $(OUT)/m19 >/dev/null 2>&1; then \
		echo "MUTATE FAIL: scctest passed a probe that writes to the live console line"; fail=1; \
	else echo "mutate ok: a pattern written down the console line is caught"; fi; \
	sed 's/\toutb(base + SCC_WR12, o12);/\t;/' ../src/scc.c > $(OUT)/m20.c; \
	$(HCC) $(HFLAGS) -DSCCSRC=\"$(OUT)/m20.c\" -o $(OUT)/m20 scctest.c 2>/dev/null; \
	if $(OUT)/m20 >/dev/null 2>&1; then \
		echo "MUTATE FAIL: scctest passed a probe that leaves a line at another speed"; fail=1; \
	else echo "mutate ok: a time constant not put back is caught"; fi; \
	sed 's/m |= (unsigned)1 << i;/m |= 1;/' ../src/scc.c > $(OUT)/m21.c; \
	$(HCC) $(HFLAGS) -DSCCSRC=\"$(OUT)/m21.c\" -o $(OUT)/m21 scctest.c 2>/dev/null; \
	if $(OUT)/m21 >/dev/null 2>&1; then \
		echo "MUTATE FAIL: scctest passed a map that reports every line as line 0"; fail=1; \
	else echo "mutate ok: a channel reported in the wrong bit is caught"; fi; \
	sed -e 's/outb(base + SCC_WR12, 0xa5);/outb(base + SCC_WR12, 0x5a);/' \
	    -e 's/outb(base + SCC_WR13, 0x5a);/outb(base + SCC_WR13, 0xa5);/' \
		../src/scc.c > $(OUT)/m22.c; \
	$(HCC) $(HFLAGS) -DSCCSRC=\"$(OUT)/m22.c\" -o $(OUT)/m22 scctest.c 2>/dev/null; \
	if $(OUT)/m22 >/dev/null 2>&1; then \
		echo "MUTATE FAIL: scctest passed a second pass that repeats the first"; fail=1; \
	else echo "mutate ok: the exchanged-pattern pass mistyped is caught"; fi; \
	sed 's/return ((unsigned short)BI_LEN4);/return ((unsigned short)BI_LEN3);/' \
		../include/bootinfo.h > $(OUT)/bootinfo.h; \
	$(HCC) $(MFLAGS) -o $(OUT)/m18 bitest.c ../src/bipack.c 2>/dev/null; \
	rm -f $(OUT)/bootinfo.h; \
	if $(OUT)/m18 >/dev/null 2>&1; then \
		echo "MUTATE FAIL: bitest passed a version table that gives v4 v3's length"; fail=1; \
	else echo "mutate ok: a wrong length for a version is caught"; fi; \
	sed 's/nos < MAXOS/nos <= MAXOS/' ../src/cfg.c > $(OUT)/m1.c; \
	$(HCC) $(HFLAGS) -o $(OUT)/m1 cfgtest.c $(OUT)/m1.c 2>/dev/null; \
	if $(OUT)/m1 >/dev/null 2>&1; then \
		echo "MUTATE FAIL: cfgtest passed a parser that overruns MAXOS"; fail=1; \
	else echo "mutate ok: MAXOS overrun is caught"; fi; \
	sed 's/if (cfgwait > 0 \&\& !nocount)/if (1)/' ../src/ui.c > $(OUT)/m2.c; \
	$(HCC) $(HFLAGS) -o $(OUT)/m2 uitest.c concon.c $(OUT)/m2.c ../src/cfg.c 2>/dev/null; \
	if $(OUT)/m2 >/dev/null 2>&1; then \
		echo "MUTATE FAIL: uitest passed a menu that counts down uninvited"; fail=1; \
	else echo "mutate ok: an uninvited countdown is caught"; fi; \
	sed 's/if (cfgfault) {/if (0) {/' ../src/ui.c > $(OUT)/m3.c; \
	$(HCC) $(HFLAGS) -o $(OUT)/m3 uitest.c concon.c $(OUT)/m3.c ../src/cfg.c 2>/dev/null; \
	if $(OUT)/m3 >/dev/null 2>&1; then \
		echo "MUTATE FAIL: uitest passed a loader that boots a corrupt config"; fail=1; \
	else echo "mutate ok: a corrupt config booted anyway is caught"; fi; \
	sed 's/if (owner != want)/if (0)/' ../src/cfg.c > $(OUT)/m4.c; \
	$(HCC) $(HFLAGS) -o $(OUT)/m4 cfgtest.c $(OUT)/m4.c 2>/dev/null; \
	if $(OUT)/m4 >/dev/null 2>&1; then \
		echo "MUTATE FAIL: cfgtest passed a layout that takes every entry's lines"; fail=1; \
	else echo "mutate ok: layouts running together is caught"; fi; \
	sed 's/want = oslist\[k\].lay;/want = LAYGLOB;/' ../src/cfg.c > $(OUT)/m5.c; \
	$(HCC) $(HFLAGS) -o $(OUT)/m5 cfgtest.c $(OUT)/m5.c 2>/dev/null; \
	if $(OUT)/m5 >/dev/null 2>&1; then \
		echo "MUTATE FAIL: cfgtest passed a loader that ignores per-entry layouts"; fail=1; \
	else echo "mutate ok: a per-entry layout silently ignored is caught"; fi; \
	sed 's/oslist\[owner\].bflags |= b;/;/' ../src/cfg.c > $(OUT)/m10.c; \
	$(HCC) $(HFLAGS) -o $(OUT)/m10 cfgtest.c $(OUT)/m10.c 2>/dev/null; \
	if $(OUT)/m10 >/dev/null 2>&1; then \
		echo "MUTATE FAIL: cfgtest passed a named flag that reaches no bit"; fail=1; \
	else echo "mutate ok: a named flag that sets nothing is caught"; fi; \
	sed 's/if (b == 0)/if (0)/' ../src/cfg.c > $(OUT)/m6.c; \
	$(HCC) $(HFLAGS) -o $(OUT)/m6 cfgtest.c $(OUT)/m6.c 2>/dev/null; \
	if $(OUT)/m6 >/dev/null 2>&1; then \
		echo "MUTATE FAIL: cfgtest passed a parser that drops an unknown flag name"; fail=1; \
	else echo "mutate ok: an unknown flag name ignored is caught"; fi; \
	sed 's/want = oslist\[k\].voc;/want = LAYGLOB;/' ../src/cfg.c > $(OUT)/m7.c; \
	$(HCC) $(HFLAGS) -o $(OUT)/m7 cfgtest.c $(OUT)/m7.c 2>/dev/null; \
	if $(OUT)/m7 >/dev/null 2>&1; then \
		echo "MUTATE FAIL: cfgtest passed one entry reading another's flag names"; fail=1; \
	else echo "mutate ok: a per-entry flag vocabulary ignored is caught"; fi; \
	sed 's/owner = sys = LAYSYS(nsys++);/;/' ../src/cfg.c > $(OUT)/m11.c; \
	$(HCC) $(HFLAGS) -o $(OUT)/m11 cfgtest.c $(OUT)/m11.c 2>/dev/null; \
	if $(OUT)/m11 >/dev/null 2>&1; then \
		echo "MUTATE FAIL: cfgtest passed a \`system' line that opens no scope"; fail=1; \
	else echo "mutate ok: a \`system' block ignored is caught"; fi; \
	sed 's/want = oslist\[k\].voc;/want = LAYSYS(0);/' ../src/cfg.c > $(OUT)/m12.c; \
	$(HCC) $(HFLAGS) -o $(OUT)/m12 cfgtest.c $(OUT)/m12.c 2>/dev/null; \
	if $(OUT)/m12 >/dev/null 2>&1; then \
		echo "MUTATE FAIL: cfgtest passed an entry reading another system's names"; fail=1; \
	else echo "mutate ok: the wrong system's vocabulary is caught"; fi; \
	sed 's/if (oslist\[k\].badflg == 0)/if (1)/' ../src/ui.c > $(OUT)/m8.c; \
	$(HCC) $(HFLAGS) -o $(OUT)/m8 uitest.c concon.c $(OUT)/m8.c ../src/cfg.c 2>/dev/null; \
	if $(OUT)/m8 >/dev/null 2>&1; then \
		echo "MUTATE FAIL: uitest passed a menu that boots a refused entry"; fail=1; \
	else echo "mutate ok: booting a refused entry is caught"; fi; \
	sed 's/conup(uilines);/;/' ../src/ui.c > $(OUT)/m13.c; \
	$(HCC) $(HFLAGS) -o $(OUT)/m13 uitest.c concon.c $(OUT)/m13.c ../src/cfg.c 2>/dev/null; \
	if $(OUT)/m13 >/dev/null 2>&1; then \
		echo "MUTATE FAIL: uitest passed a menu that redraws below itself"; fail=1; \
	else echo "mutate ok: a menu reprinted under its last copy is caught"; fi; \
	sed 's/if (uibar \&\& (c == K_UP/if ((c == K_UP/' ../src/ui.c > $(OUT)/m14.c; \
	$(HCC) $(HFLAGS) -o $(OUT)/m14 uitest.c concon.c $(OUT)/m14.c ../src/cfg.c 2>/dev/null; \
	if $(OUT)/m14 >/dev/null 2>&1; then \
		echo "MUTATE FAIL: uitest passed movement on a console that cannot redraw"; fail=1; \
	else echo "mutate ok: movement offered where it cannot work is caught"; fi; \
	sed 's/bflabel(i, fb, FLGSZ);/fb[0] = 0;/' ../src/ui.c > $(OUT)/m9.c; \
	$(HCC) $(HFLAGS) -o $(OUT)/m9 uitest.c concon.c $(OUT)/m9.c ../src/cfg.c 2>/dev/null; \
	if $(OUT)/m9 >/dev/null 2>&1; then \
		echo "MUTATE FAIL: uitest passed a menu that hides the flags an entry carries"; fail=1; \
	else echo "mutate ok: flags set and not shown is caught"; fi; \
	exit $$fail

clean:
	rm -rf $(OUT)

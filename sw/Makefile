# 6502 software of the retro konsol (cc65). `make` builds build/*.mem for the hardware; `make test` runs
# the software tests.
CA65 ?= ca65
LD65 ?= ld65
BIN2MEM = ../../Scripts/bin2mem.py
B = build

ROMS = $(B)/boot.mem $(B)/cputest.mem

all: $(ROMS)

$(B)/%.o: boot/%.s include/konsol.inc | $(B)
	$(CA65) -I include -o $@ $<
$(B)/%.o: tests/%.s include/konsol.inc | $(B)
	$(CA65) -I include -o $@ $<
$(B)/%.bin: $(B)/%.o konsol.cfg
	$(LD65) -C konsol.cfg -o $@ $<
$(B)/%.mem: $(B)/%.bin
	python3 $(BIN2MEM) $< $@ --size 8192

$(B):
	mkdir -p $(B)

test: all
	@echo "software tests: none yet"

clean:
	rm -rf $(B)
.PHONY: all test clean
.SECONDARY:

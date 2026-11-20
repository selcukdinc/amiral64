# 6502 software of the retro konsol (cc65). `make` builds build/*.mem for the hardware; `make test` runs
# the software tests.
CA65 ?= ca65
LD65 ?= ld65
BIN2MEM = ../../Scripts/bin2mem.py
B = build

ROMS = $(B)/boot.mem $(B)/cputest.mem $(B)/ppucpu.mem $(B)/iotest.mem $(B)/cart.mem $(B)/cart_flash.mem

# Games on the cartridge: NAME:players:binary
CART_GAMES = "TEST OYUN A:1:$(B)/gametest_A.bin" "TEST OYUN B:2-4:$(B)/gametest_B.bin"
CART_BINS = $(B)/gametest_A.bin $(B)/gametest_B.bin

all: $(ROMS)

$(B)/font.inc: ../tools/font.py | $(B)
	python3 ../tools/font.py asm $@
$(B)/%.o: boot/%.s include/konsol.inc $(B)/font.inc | $(B)
	$(CA65) -I include -I $(B) -o $@ $<
$(B)/%.o: tests/%.s include/konsol.inc | $(B)
	$(CA65) -I include -o $@ $<
$(B)/gametest_%.bin: $(B)/gametest_%.o game_asm.cfg
	$(LD65) -C game_asm.cfg -o $@ $<
$(B)/%.bin: $(B)/%.o konsol.cfg
	$(LD65) -C konsol.cfg -o $@ $<
$(B)/cart.mem: $(CART_BINS) ../tools/mkcart.py
	python3 ../tools/mkcart.py $(B)/cart.bin $@ $(CART_GAMES)
# test image for the flash simulation (tb_flash_boot): game B first
$(B)/cart_flash.mem: $(CART_BINS) ../tools/mkcart.py
	python3 ../tools/mkcart.py $(B)/cart_flash.bin $@ "FLASH OYUN B:1:$(B)/gametest_B.bin"
$(B)/%.mem: $(B)/%.bin
	python3 $(BIN2MEM) $< $@ --size 8192

$(B):
	mkdir -p $(B)

test: all
	python3 tests/test_sw.py

clean:
	rm -rf $(B)
.PHONY: all test clean
.SECONDARY:

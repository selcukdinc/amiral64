# 6502 software of the retro konsol (cc65). `make` builds build/*.mem for the hardware; `make test` runs
.DEFAULT_GOAL := all
# the software tests.
CA65 ?= ca65
LD65 ?= ld65
BIN2MEM = ../../Scripts/bin2mem.py
B = build

ROMS = $(B)/boot.mem $(B)/cputest.mem $(B)/ppucpu.mem $(B)/iotest.mem $(B)/cart.mem $(B)/carttest.mem $(B)/cart_flash.mem

# C games: sw/games/<name>/main.c + art.txt, linked with the library in sw/lib
CC65 ?= cc65
LIBOBJ = $(B)/crt0.o $(B)/knmi.o $(B)/konsol.o
NONELIB = /opt/homebrew/share/cc65/lib/none.lib
$(B)/font.h: ../tools/font.py | $(B)
	python3 ../tools/font.py c $@
$(B)/crt0.o: lib/crt0.s | $(B)
	$(CA65) -o $@ $<
$(B)/knmi.o: lib/knmi.s include/konsol.inc | $(B)
	$(CA65) -I include -o $@ $<
$(B)/konsol.o: lib/konsol.c lib/konsol.h $(B)/font.h | $(B)
	$(CC65) -O -t none -I lib -I $(B) -o $(B)/konsol.s $<
	$(CA65) -o $@ $(B)/konsol.s
$(B)/%/art.h: games/%/art.txt ../tools/gfx.py
	mkdir -p $(dir $@)
	python3 ../tools/gfx.py $< $@
$(B)/%/main.o: games/%/main.c lib/konsol.h $(B)/%/art.h
	$(CC65) -O -t none -I lib -I $(B)/$* -o $(B)/$*/main.s $<
	$(CA65) -o $@ $(B)/$*/main.s
$(B)/game_%.bin: $(B)/%/main.o $(LIBOBJ) game_c.cfg
	$(LD65) -C game_c.cfg -o $@ -Ln $(B)/game_$*.lbl -m $(B)/game_$*.map $(LIBOBJ) $< $(NONELIB)

# Games on the cartridge: NAME:players:binary
CART_GAMES = "KRISTAL OCAK:1:$(B)/game_kristal.bin" "YILDIZ MUHAFIZI:1:$(B)/game_yildiz.bin" "FITIL ARENASI:2-4:$(B)/game_fitil.bin" "MAHALLE OLIMPIYATI:1-4:$(B)/game_olimpiyat.bin"
CART_BINS = $(B)/game_kristal.bin $(B)/game_yildiz.bin $(B)/game_fitil.bin $(B)/game_olimpiyat.bin
# test cartridge (menu tests, tb_boot): two tiny games that report on SYS_TEST
TEST_GAMES = "TEST OYUN A:1:$(B)/gametest_A.bin" "TEST OYUN B:2-4:$(B)/gametest_B.bin"
TEST_BINS = $(B)/gametest_A.bin $(B)/gametest_B.bin

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
.PRECIOUS: $(B)/%/art.h $(B)/%/main.o
$(B)/cart.mem: $(CART_BINS) ../tools/mkcart.py
	python3 ../tools/mkcart.py $(B)/cart.bin $@ $(CART_GAMES)
$(B)/carttest.mem: $(TEST_BINS) ../tools/mkcart.py
	python3 ../tools/mkcart.py $(B)/carttest.bin $@ $(TEST_GAMES)
# test image for the flash simulation (tb_flash_boot): game B first
$(B)/cart_flash.mem: $(TEST_BINS) ../tools/mkcart.py
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

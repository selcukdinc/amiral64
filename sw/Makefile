# 6502 software of the retro konsol (cc65). `make` builds build/*.mem for the hardware; `make test` runs
.DEFAULT_GOAL := all
# the software tests.
CA65 ?= ca65
LD65 ?= ld65
BIN2MEM = ../tools/bin2mem.py
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
# Koridor 64 makes its wall tiles at 96..223 itself: its art starts at tile 208 (the 16 source tiles)
$(B)/koridor/art.h: games/koridor/art.txt ../tools/gfx.py
	mkdir -p $(dir $@)
	python3 ../tools/gfx.py $< $@ --tile-base 208
# games short of room keep their graphics run-length packed (k_vunpack)
RLE_GAMES = tilsim
$(addprefix $(B)/,$(addsuffix /art.h,$(RLE_GAMES))): $(B)/%/art.h: games/%/art.txt ../tools/gfx.py
	mkdir -p $(dir $@)
	python3 ../tools/gfx.py $< $@ --rle
$(B)/%/art.h: games/%/art.txt ../tools/gfx.py
	mkdir -p $(dir $@)
	python3 ../tools/gfx.py $< $@
$(B)/%/main.o: games/%/main.c lib/konsol.h lib/ses.h $(B)/%/art.h $(B)/font.h
	$(CC65) -O -t none -I lib -I games/$* -I $(B)/$* -I $(B) -o $(B)/$*/main.s $<
	$(CA65) -o $@ $(B)/$*/main.s
# music and sound effects of a game: games/<name>/muzik.ses (MML, tools/sesc.py) -> muzik.h + muzik.o
SESC = python3 ../tools/sesc.py
$(B)/%/muzik.h $(B)/%/muzik.s: games/%/muzik.ses lib/ortak.ses ../tools/sesc.py
	mkdir -p $(dir $@)
	$(SESC) $< $(B)/$*/muzik.s $(B)/$*/muzik.h
$(B)/%/muzik.o: $(B)/%/muzik.s
	$(CA65) -o $@ $<
# optional library parts (linked only into the games that use them)
$(B)/bigtext.o: lib/bigtext.c lib/bigtext.h lib/konsol.h | $(B)
	$(CC65) -O -t none -I lib -o $(B)/bigtext.s $<
	$(CA65) -o $@ $(B)/bigtext.s
$(B)/vunpack.o: lib/vunpack.c lib/konsol.h | $(B)
	$(CC65) -O -t none -I lib -o $(B)/vunpack.s $<
	$(CA65) -o $@ $(B)/vunpack.s
# the sound engine, the standard effects (lib/ses_std.ses) and k_sfx
$(B)/ses.o: lib/ses.s | $(B)
	$(CA65) -o $@ $<
$(B)/ses_std.h $(B)/ses_std.s: lib/ses_std.ses ../tools/sesc.py | $(B)
	$(SESC) $< $(B)/ses_std.s $(B)/ses_std.h std
$(B)/ses_std.o: $(B)/ses_std.s
	$(CA65) -o $@ $<
$(B)/ksfx.o: lib/ksfx.c lib/konsol.h lib/ses.h $(B)/ses_std.h | $(B)
	$(CC65) -O -t none -I lib -I $(B) -o $(B)/ksfx.s $<
	$(CA65) -o $@ $(B)/ksfx.s
$(B)/konsolx.lib: $(B)/bigtext.o $(B)/vunpack.o $(B)/ses.o $(B)/ses_std.o $(B)/ksfx.o
	rm -f $@; ar65 a $@ $^
# games bigger than 20 KB load at $0400 instead of $9000 (game_c_low.cfg; the cartridge entry says where)
LOW_GAMES = tilsim koridor
# extra assembly parts of a game: sw/games/<name>/*.s
$(B)/koridor/%.o: games/koridor/%.s | $(B)
	mkdir -p $(dir $@)
	$(CA65) -o $@ $<
GAME_ASM_koridor = $(B)/koridor/blit.o $(B)/koridor/render.o $(B)/koridor/tables.o
$(B)/koridor/main.o: games/koridor/tables.h
.SECONDEXPANSION:
GAME_SES = $(if $(wildcard games/$(1)/muzik.ses),$(B)/$(1)/muzik.o)
$(B)/%/main.o: $$(if $$(wildcard games/$$*/muzik.ses),$(B)/$$*/muzik.h)
$(B)/game_%.bin: $(B)/%/main.o $$(GAME_ASM_$$*) $$(call GAME_SES,$$*) $(LIBOBJ) $(B)/konsolx.lib game_c.cfg game_c_low.cfg
	$(LD65) -C $(if $(filter $*,$(LOW_GAMES)),game_c_low.cfg,game_c.cfg) -o $@ -Ln $(B)/game_$*.lbl -m $(B)/game_$*.map $(LIBOBJ) $< $(GAME_ASM_$*) $(call GAME_SES,$*) $(B)/konsolx.lib $(NONELIB)

# sound test player (tools/sesdinle.py): plays the songs and effects of one music file;
# build/sesoyna_<name>.bin for games/<name>/muzik.ses, sesoyna_std for the library, sesoyna_deneme for tests
$(B)/sesoyna/main.o: tests/sesoyna.c lib/konsol.h lib/ses.h | $(B)
	mkdir -p $(dir $@)
	$(CC65) -O -t none -I lib -I $(B) -o $(B)/sesoyna/main.s $<
	$(CA65) -o $@ $(B)/sesoyna/main.s
$(B)/deneme/muzik.h $(B)/deneme/muzik.s: tests/deneme.ses lib/ortak.ses ../tools/sesc.py
	mkdir -p $(dir $@)
	$(SESC) $< $(B)/deneme/muzik.s $(B)/deneme/muzik.h
$(B)/std/muzik.h $(B)/std/muzik.s: lib/ses_std.ses ../tools/sesc.py
	mkdir -p $(dir $@)
	$(SESC) $< $(B)/std/muzik.s $(B)/std/muzik.h
$(B)/sesoyna_%.bin: $(B)/sesoyna/main.o $(B)/%/muzik.o $(LIBOBJ) $(B)/konsolx.lib game_c_low.cfg
	$(LD65) -C game_c_low.cfg -o $@ -Ln $(B)/sesoyna_$*.lbl $(LIBOBJ) $< $(B)/$*/muzik.o $(B)/konsolx.lib $(NONELIB)
SES_NAMES = std deneme $(patsubst games/%/muzik.ses,%,$(wildcard games/*/muzik.ses))
ses-dinle: $(foreach n,$(SES_NAMES),$(B)/sesoyna_$(n).bin)
	python3 ../tools/sesdinle.py $(SES_NAMES)

# Games on the cartridge: NAME:players:binary[:icon[:load address, hex]] ('*' = the launcher, started by the boot ROM)
CART_GAMES = "*KONSOL:1:$(B)/game_launcher.bin" "KRISTAL OCAK:1:$(B)/game_kristal.bin:1" "BLOK YAGMURU:1:$(B)/game_blok.bin:5" "TILSIM:1:$(B)/game_tilsim.bin:6:0400" "YILDIZ MUHAFIZI:1:$(B)/game_yildiz.bin:2" "FITIL ARENASI:2-4:$(B)/game_fitil.bin:3" "MAHALLE OLIMPIYATI:1-4:$(B)/game_olimpiyat.bin:4" "KORIDOR 64:1:$(B)/game_koridor.bin:7:0400"
CART_BINS = $(B)/game_launcher.bin $(B)/game_kristal.bin $(B)/game_blok.bin $(B)/game_tilsim.bin $(B)/game_yildiz.bin $(B)/game_fitil.bin $(B)/game_olimpiyat.bin $(B)/game_koridor.bin
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

test: all $(B)/sesoyna_deneme.bin $(B)/sesoyna_std.bin
	python3 tests/test_sw.py

clean:
	rm -rf $(B)
.PHONY: all test clean ses-dinle
.SECONDARY:

# Retro konsol

A 6502 game console for the Digilent Nexys 4 DDR, written in VHDL and built with VivadoMac's open-source
flow: T65 CPU, a tile and sprite video chip (PPU), a four-channel sound chip (APU), keyboard, four NES/SNES
pads, UART, a boot ROM with a game menu, and four games on a cartridge (block RAM or the QSPI flash).

| Folder | Contents |
| --- | --- |
| `hw/` | Synthesizable VHDL. Top: `konsol_top` (pins, MMCM, STARTUPE2) around `konsol_core` |
| `hw/t65/` | T65 6502 core (BSD-style license, see `NOTICE.md`) |
| `hw/sim/` | Testbenches and simulation models (Xilinx primitives, SPI flash) |
| `constr/` | Nexys 4 DDR pins |
| `sw/` | 6502 software: `boot/` boot ROM, `lib/` C game library, `games/` the four games, `tests/` |
| `tools/` | `emu6502.py` (fast software model + screenshots), `ppu_model.py`, `gfx.py`, `font.py`, `mkcart.py` |
| `vivado/` | `create_project.tcl` (VivadoMac/Vivado project), `build_bitstream.tcl`, `sources.tcl` |
| `tests/` | `run_tests.sh`: software tests, every testbench, and the full flow to a bitstream |

## Build and test

```sh
brew install cc65                       # once (Scripts/install_toolchain.sh does it too)
make -C sw                              # boot ROM, games, cartridge -> sw/build/*.mem
tests/run_tests.sh                      # everything (about 10 minutes)
tests/run_tests.sh sw                   # software only: emulator tests + screenshots in sw/build/shots
tests/run_tests.sh sim                  # testbenches only
```

To open it in VivadoMac: `cd vivado`, then
`../../build/VivadoMac.app/Contents/MacOS/VivadoMac -mode batch -source create_project.tcl`
creates `vivado/konsol/konsol.xpr`. Open that in the app and use Generate Bitstream, or run
`build_bitstream.tcl` the same way.

## On the board

1. Program the bitstream (Program Device). The menu shows the games built into the bitstream ("BRAM").
2. Optional, games in the flash: tick "Program configuration memory" in Program Device, and as the data file
   pick `sw/build/cart.bin` at offset `0x400000`. With the MODE jumper on QSPI the board then boots straight
   into the menu ("FLASH" in the corner). Tcl: `vivadomac_write_flash -offset 0x400000 sw/build/cart.bin`.
3. Sound: 3.5 mm jack (start quiet). UART: Serial Terminal tab, 115200 baud; the boot ROM monitor understands
   `D addr` (dump), `W addr val`, `G addr` and `?`.

## Controls

| | Player 1 | Player 2 | Players 1-4 |
| --- | --- | --- | --- |
| Directions | W A S D, board buttons | arrow keys | NES/SNES pads on Pmod JA |
| A | Left Shift, BTNC | Right Ctrl | A |
| B | Left Ctrl | Right Shift | B |
| Select / Start | Tab / Space | Backspace / Enter | Select / Start |

Pmod JA: JA1 LATCH, JA2 CLK, JA3/JA4/JA7/JA8 DATA of pads 1-4, 3.3 V supply (do not power pads from 5 V).
Select + Start in a game goes back to the menu.

## Games

| Game | Players | |
| --- | --- | --- |
| Kristal Ocak | 1 | Dig, collect crystals, avoid falling rocks, reach the exit. 3 levels; the status line stays still while the mine scrolls (raster split) |
| Yildiz Muhafizi | 1 | Side-scrolling shooter: saucers that shoot back, darts, mines; waves speed up |
| Fitil Arenasi | 2-4 | Bomb arena: crates, chain reactions, power-ups (bombs, range, speed); first to 3 rounds |
| Mahalle Olimpiyati | 1-4 | Party game in turns: 100 m (A-B mashing), long jump, reflex test |

## Hardware summary

- One 25 MHz clock (MMCM from 100 MHz); the CPU runs at 6.25 MHz (or 1.79 MHz) through a clock enable.
- Memory map: `$0000-$7FFF` RAM, `$8000-$8FFF` I/O, `$9000-$DFFF` game RAM, `$E000-$FFFF` boot ROM.
  Register list: `sw/include/konsol.inc` and the header comment of each block in `hw/`.
- PPU: 320x240 shown 2x on VGA 640x480 @ 60 Hz, 64x32 tile map with scrolling, 256 tiles, 64 sprites of
  16x16 (16 per line), 16 palettes of 16 colours (4096 colours), vblank NMI, raster IRQ, OAM DMA,
  sprite collision flags.
- APU: 2 square, triangle, noise, decay envelopes; first-order delta-sigma on the open-drain `AUD_PWM`.
- Resources (VivadoMac flow): about 2,900 LUTs and 30 block RAMs of the XC7A100T; timing met at 25 MHz
  (Fmax about 60 MHz).

Only simulation has checked all of this so far; see the plan's checklist for the tests to do on the board.

#!/bin/zsh
# Retro konsol test runner.
#   tests/run_tests.sh            all GHDL testbenches + software tests + full VivadoMac flow to a bitstream
#   tests/run_tests.sh sim        testbenches only
#   tests/run_tests.sh tb_vga     testbenches whose name contains "tb_vga"
# The design and testbench file lists come from vivado/sources.tcl, the same list VivadoMac uses.
set -u
K="$(cd "$(dirname "$0")/.." && pwd)"
GHDL="${GHDL:-$HOME/.vivadomac/oss-cad-suite/bin/ghdl}"
# VivadoMac is a separate project: point VIVADOMAC_BIN at its binary if it is not in /Applications
BIN="${VIVADOMAC_BIN:-/Applications/VivadoMac.app/Contents/MacOS/VivadoMac}"
B="$K/build/tests"; mkdir -p "$B"
filter="${1:-}"
pass=0; fail=0

# Software first (the hardware tests load its ROM images).
if [[ -z "$filter" || "$filter" == sw ]] && [[ -f "$K/sw/Makefile" ]]; then
  if make -s -C "$K/sw" > "$B/sw.log" 2>&1 && make -s -C "$K/sw" test >> "$B/sw.log" 2>&1; then
    echo "PASS  software build + tests"; pass=$((pass+1))
  else
    echo "FAIL  software (see $B/sw.log)"; tail -5 "$B/sw.log" | sed 's/^/      /'; fail=$((fail+1))
  fi
fi

design=(${(f)"$(sed -n 's/^add_files  *\(hw\/[^ ]*\.vhd\).*$/\1/p' "$K/vivado/sources.tcl")"})
simsrc=(${(f)"$(sed -n 's/^add_files -fileset sim_1  *\(.*\.vhd\)$/\1/p' "$K/vivado/sources.tcl")"})
work="$B/ghdl"; rm -rf "$work"; mkdir -p "$work"
for m in "$K"/hw/*.mem(N) "$K"/sw/build/*.mem(N); do cp "$m" "$work"/; done
python3 "$K/tools/ppu_model.py" scene 1 "$work"   # PPU testbench scene + expected frame
opts=(--std=08 -fsynopsys --workdir="$work" --work=xil_defaultlib)
if ! (cd "$K" && "$GHDL" -a $opts $design $simsrc) > "$B/analyze.log" 2>&1; then
  echo "FAIL  GHDL analysis (see $B/analyze.log)"; head -5 "$B/analyze.log"; exit 1
fi
for tb in ${(f)"$(for f in $simsrc; do sed -n 's/^entity \(tb_[a-z0-9_]*\) is.*/\1/p' "$K/$f"; done)"}; do
  [[ -n "$filter" && "$filter" != all && "$filter" != sim && "$tb" != *"$filter"* ]] && continue
  log="$B/$tb.log"
  (cd "$work" && "$GHDL" --elab-run $opts "$tb" --assert-level=error --max-stack-alloc=0 --ieee-asserts=disable-at-0) > "$log" 2>&1
  rc=$?
  if [[ $rc -eq 0 ]] && grep -q "TEST PASSED" "$log"; then
    echo "PASS  $tb"; pass=$((pass+1))
  else
    echo "FAIL  $tb (exit $rc)"; grep -E "error|failure|FAIL" "$log" | head -5 | sed 's/^/      /'; fail=$((fail+1))
  fi
done

if [[ -z "$filter" || "$filter" == flow ]]; then
  [[ -x "$BIN" ]] || { echo "VivadoMac not found at $BIN (set VIVADOMAC_BIN)"; exit 1; }
  rm -rf "$K/build/flow"
  if (cd "$K/vivado" && "$BIN" -mode batch -source build_bitstream.tcl) > "$B/flow.log" 2>&1 \
     && grep -q "Bitgen Completed Successfully" "$B/flow.log"; then
    echo "PASS  VivadoMac flow: synthesis, place & route, bitstream"; pass=$((pass+1))
    grep -E "^\| (LUT|FF|BRAM|DSP) |Slice LUTs\*|Block RAM Tile|^sys_clk|^clk " "$B/flow.log" | head -8 | sed 's/^/      /'
  else
    echo "FAIL  VivadoMac flow (see $B/flow.log)"; grep -E "ERROR" "$B/flow.log" | head -5 | sed 's/^/      /'; fail=$((fail+1))
  fi
fi
if [[ -z "$filter" || "$filter" == vboard ]]; then
  # the synthesized design on VivadoMac's virtual board (Verilator): keys through PS/2, a game starts and is played
  [[ -x "$BIN" ]] || { echo "VivadoMac not found at $BIN (set VIVADOMAC_BIN)"; exit 1; }
  mkdir -p "$K/build/vboard"
  (cd "$K/vivado" && "$BIN" -mode batch -source virtual_board_test.tcl) > "$B/vboard.log" 2>&1
  if grep -q "LOADING" "$B/vboard.log" && python3 - "$K/build/vboard/game.ppm" <<'PY'
import sys
d = open(sys.argv[1], "rb").read().split(b"\n", 3)[3]
px = lambda x, y: tuple(d[(y * 640 + x) * 3:(y * 640 + x) * 3 + 3])
# Kristal Ocak 1-1: the hero dug from cell (1,1) to about (6,1). A dug cell shows the dark back wall, dirt is
# bright: cell (3,1) (screen 96..127 x 64..95) is dark on average, cell (9,1) (288..319) still dirt
cell = lambda cx: sum(sum(px(x, y)) for x in range(cx * 32, cx * 32 + 32, 2) for y in range(64, 96, 2)) / 256
sys.exit(0 if cell(3) < 150 and cell(9) > 200 else 1)   # (on the Python model: about 89 and 280)
PY
  then
    echo "PASS  virtual board: boot, launcher by keyboard, Kristal Ocak's screens, level 1-1 played"; pass=$((pass+1))
  else
    echo "FAIL  virtual board (see $B/vboard.log)"; fail=$((fail+1))
  fi
fi
echo "$pass passed, $fail failed"
[[ $fail -eq 0 ]]

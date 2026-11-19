# Retro konsol

A 6502 game console for the Digilent Nexys 4 DDR, written in VHDL and built with VivadoMac's open-source
flow. Plan and phases: see the project plan document; this folder grows one phase (K0–K8) at a time.

| Folder | Contents |
| --- | --- |
| `hw/` | Synthesizable VHDL (top: `konsol_top`) |
| `hw/sim/` | Testbenches and simulation models of the Xilinx primitives (simulation only) |
| `constr/` | Nexys 4 DDR pins |
| `sw/` | 6502 software (cc65): boot ROM, game library, games |
| `tools/` | Graphics, cartridge and emulator tools |
| `vivado/` | Project scripts: `create_project.tcl` makes `vivado/konsol/konsol.xpr` |
| `tests/` | `run_tests.sh`: every testbench, the software tests and the full flow to a bitstream |

```sh
tests/run_tests.sh                    # everything
tests/run_tests.sh sim                # testbenches only
cd vivado && ../../build/VivadoMac.app/Contents/MacOS/VivadoMac -mode batch -source create_project.tcl
```

The design runs from one 25 MHz clock (MMCM from the 100 MHz oscillator); everything slower uses
clock enables.

# Runs the konsol on VivadoMac's virtual board (Verilator) without a window: boot, menu, the S key moves
# the cursor, Enter starts Kristal Ocak and, again, the level (keyboard player 2 Start), D digs right (used by tests/run_tests.sh).
create_project konsol ../build/vboard/konsol -part xc7a100tcsg324-1 -force
cd ..
source vivado/sources.tcl
cd vivado
set_property top konsol_top [get_filesets sources_1]
vivadomac_virtual_board -frames 230 -ppm ../build/vboard/game.ppm -uart ../build/vboard/uart.txt -events "100:k 1b;101:k f0 1b;130:k 76;131:k f0 76;135:k e0 75;137:k e0 f0 75;140:k 5a;141:k f0 5a;175:k 5a;176:k f0 5a;195:k 23;225:k f0 23"

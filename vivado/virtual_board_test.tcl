# Runs the konsol on VivadoMac's virtual board (Verilator) without a window: the boot ROM starts the launcher,
# Enter (keyboard player 2 Start) skips its splash, Enter again starts the chosen game (Kristal Ocak), then
# Enter on its title (the mine select), on the select screen (level 1-1's card) and on the card (the level);
# D digs right (used by tests/run_tests.sh).
create_project konsol ../build/vboard/konsol -part xc7a100tcsg324-1 -force
cd ..
source vivado/sources.tcl
cd vivado
set_property top konsol_top [get_filesets sources_1]
vivadomac_virtual_board -frames 280 -ppm ../build/vboard/game.ppm -uart ../build/vboard/uart.txt -events "40:k 5a;41:k f0 5a;100:k 5a;101:k f0 5a;150:k 5a;151:k f0 5a;175:k 5a;176:k f0 5a;210:k 5a;211:k f0 5a;225:k 23;262:k f0 23"

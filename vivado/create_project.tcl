# Creates the VivadoMac/Vivado project retro-konsol/vivado/konsol/konsol.xpr.
#   VivadoMac -mode batch -source create_project.tcl     (or source it in the Tcl Console)
create_project konsol konsol -part xc7a100tcsg324-1
set_property board_part digilentinc.com:nexys4_ddr:part0:1.1 [current_project]
cd ..
source vivado/sources.tcl
cd vivado
set_property top konsol_top [get_filesets sources_1]
set_property top tb_konsol_k0 [get_filesets sim_1]

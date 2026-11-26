# Creates the VivadoMac/Vivado project vivado/konsol/konsol.xpr. Run it from this folder:
#   VivadoMac -mode batch -source create_project.tcl     (or source it in the Tcl Console)
#   Vivado:  Tcl Console > cd <amiral64>/vivado ; source create_project.tcl
create_project konsol konsol -part xc7a100tcsg324-1
# no board_part: the pins come from constr/nexys4ddr.xdc, so Digilent's board files are not needed
cd ..
source vivado/sources.tcl
cd vivado
set_property top konsol_top [get_filesets sources_1]
set_property top tb_konsol_k0 [get_filesets sim_1]

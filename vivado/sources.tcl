# Source list of the retro konsol (also read by tests/run_tests.sh: keep one file per add_files line).
add_files hw/xilinx_prims.vhd
add_files hw/clock_reset.vhd
add_files hw/vga_timing.vhd
add_files hw/seg7.vhd
add_files hw/konsol_top.vhd
add_files -fileset constrs_1 constr/nexys4ddr.xdc
add_files -fileset sim_1 hw/sim/xilinx_prims_sim.vhd
add_files -fileset sim_1 hw/sim/tb_vga_timing.vhd
add_files -fileset sim_1 hw/sim/tb_konsol_k0.vhd

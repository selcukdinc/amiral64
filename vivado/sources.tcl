# Source list of the retro konsol (also read by tests/run_tests.sh: keep one file per add_files line).
add_files hw/xilinx_prims.vhd
add_files hw/t65/T65_Pack.vhd
add_files hw/t65/T65_ALU.vhd
add_files hw/t65/T65_MCode.vhd
add_files hw/t65/T65.vhd
add_files hw/konsol_pkg.vhd
add_files hw/bram.vhd
add_files hw/bram_dp.vhd
add_files hw/ppu.vhd
add_files hw/display_io.vhd
add_files hw/ps2_rx.vhd
add_files hw/uart.vhd
add_files hw/input_io.vhd
add_files hw/timer_io.vhd
add_files hw/clock_reset.vhd
add_files hw/vga_timing.vhd
add_files hw/seg7.vhd
add_files hw/konsol_core.vhd
add_files hw/konsol_top.vhd
add_files sw/build/boot.mem
add_files -fileset constrs_1 constr/nexys4ddr.xdc
add_files -fileset sim_1 hw/sim/xilinx_prims_sim.vhd
add_files -fileset sim_1 hw/sim/tb_vga_timing.vhd
add_files -fileset sim_1 hw/sim/tb_konsol_k0.vhd
add_files -fileset sim_1 hw/sim/tb_cpu_k1.vhd
add_files -fileset sim_1 hw/sim/tb_ppu.vhd
add_files -fileset sim_1 hw/sim/tb_ppu_cpu.vhd
add_files -fileset sim_1 sw/build/ppucpu.mem
add_files -fileset sim_1 hw/sim/tb_input.vhd
add_files -fileset sim_1 hw/sim/tb_uart_cpu.vhd
add_files -fileset sim_1 sw/build/iotest.mem
add_files -fileset sim_1 sw/build/cputest.mem

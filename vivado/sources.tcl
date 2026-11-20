# Source list of the retro konsol (also read by tests/run_tests.sh: keep one file per add_files line).
add_files hw/xilinx_prims.vhd
add_files hw/t65/T65_Pack.vhd
add_files hw/t65/T65_ALU.vhd
add_files hw/t65/T65_MCode.vhd
add_files hw/t65/T65.vhd
add_files hw/konsol_pkg.vhd
add_files hw/bram.vhd
add_files hw/bram_dp.vhd
add_files hw/bram_banked.vhd
add_files hw/spi_flash_reader.vhd
add_files hw/ppu.vhd
add_files hw/display_io.vhd
add_files hw/ps2_rx.vhd
add_files hw/uart.vhd
add_files hw/input_io.vhd
add_files hw/apu.vhd
add_files hw/timer_io.vhd
add_files hw/clock_reset.vhd
add_files hw/vga_timing.vhd
add_files hw/seg7.vhd
add_files hw/konsol_core.vhd
add_files hw/konsol_top.vhd
add_files sw/build/boot.mem
add_files sw/build/cart_00.mem
add_files sw/build/cart_01.mem
add_files sw/build/cart_02.mem
add_files sw/build/cart_03.mem
add_files sw/build/cart_04.mem
add_files sw/build/cart_05.mem
add_files sw/build/cart_06.mem
add_files sw/build/cart_07.mem
add_files sw/build/cart_08.mem
add_files sw/build/cart_09.mem
add_files sw/build/cart_0a.mem
add_files sw/build/cart_0b.mem
add_files sw/build/cart_0c.mem
add_files sw/build/cart_0d.mem
add_files sw/build/cart_0e.mem
add_files sw/build/cart_0f.mem
add_files -fileset constrs_1 constr/nexys4ddr.xdc
add_files -fileset sim_1 hw/sim/xilinx_prims_sim.vhd
add_files -fileset sim_1 hw/sim/tb_vga_timing.vhd
add_files -fileset sim_1 hw/sim/tb_konsol_k0.vhd
add_files -fileset sim_1 hw/sim/tb_cpu_k1.vhd
add_files -fileset sim_1 hw/sim/tb_ppu.vhd
add_files -fileset sim_1 hw/sim/tb_ppu_cpu.vhd
add_files -fileset sim_1 sw/build/ppucpu.mem
add_files -fileset sim_1 hw/sim/tb_input.vhd
add_files -fileset sim_1 hw/sim/tb_apu.vhd
add_files -fileset sim_1 hw/sim/tb_uart_cpu.vhd
add_files -fileset sim_1 sw/build/iotest.mem
add_files -fileset sim_1 hw/sim/tb_boot.vhd
add_files -fileset sim_1 hw/sim/spi_flash_model.vhd
add_files -fileset sim_1 hw/sim/tb_flash_boot.vhd
add_files -fileset sim_1 hw/sim/tb_game.vhd
add_files -fileset sim_1 sw/build/cart_flash.mem
add_files -fileset sim_1 sw/build/carttest_00.mem
add_files -fileset sim_1 sw/build/carttest_01.mem
add_files -fileset sim_1 sw/build/carttest_02.mem
add_files -fileset sim_1 sw/build/carttest_03.mem
add_files -fileset sim_1 sw/build/carttest_04.mem
add_files -fileset sim_1 sw/build/carttest_05.mem
add_files -fileset sim_1 sw/build/carttest_06.mem
add_files -fileset sim_1 sw/build/carttest_07.mem
add_files -fileset sim_1 sw/build/carttest_08.mem
add_files -fileset sim_1 sw/build/carttest_09.mem
add_files -fileset sim_1 sw/build/carttest_0a.mem
add_files -fileset sim_1 sw/build/carttest_0b.mem
add_files -fileset sim_1 sw/build/carttest_0c.mem
add_files -fileset sim_1 sw/build/carttest_0d.mem
add_files -fileset sim_1 sw/build/carttest_0e.mem
add_files -fileset sim_1 sw/build/carttest_0f.mem
add_files -fileset sim_1 sw/build/cputest.mem

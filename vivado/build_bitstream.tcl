# Synthesis, place & route and bitstream of the retro konsol in a scratch project (used by tests/run_tests.sh).
create_project konsol ../build/flow/konsol -part xc7a100tcsg324-1
set_property board_part digilentinc.com:nexys4_ddr:part0:1.1 [current_project]
cd ..
source vivado/sources.tcl
cd vivado
set_property top konsol_top [get_filesets sources_1]
launch_runs impl_1 -to_step write_bitstream
report_utilization
report_timing_summary

-- K8: the real cartridge: the boot ROM starts the launcher (its first entry, a cc65 C program); its library
-- reports $C0 when it starts and $C1 once vblank interrupts arrive through the RAM vectors.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_game is end;

architecture sim of tb_game is
  signal clk : std_logic := '0';
  signal rst : std_logic := '1';
  signal test_we : std_logic;
  signal code : std_logic_vector(7 downto 0);
  signal txd : std_logic;
  signal done : boolean := false;
begin
  clk <= not clk after 20 ns when not done;
  dut : entity work.konsol_core
    port map (clk => clk, rst => rst, sw => x"0000", buttons => "10000", leds => open, rgb16 => open, rgb17 => open,
              digits => open, digit_blank => open, digit_dots => open,
              ps2_clk => '1', ps2_data => '1', nes_latch => open, nes_clk => open, nes_data => "1111",
              uart_txd => txd, uart_rxd => '1', aud_pwm => open, aud_sd => open, flash_sck => open, flash_cs_n => open, flash_mosi => open, flash_miso => '1', vga_rgb => open, vga_hs => open, vga_vs => open,
              dbg_test_we => test_we, dbg_test_code => code, dbg_addr => open, dbg_cpu_en => open);
  process
  begin
    wait for 200 ns; wait until rising_edge(clk); rst <= '0';
    wait until rising_edge(clk) and test_we = '1' and code = x"C0" for 400 ms;
    wait until rising_edge(clk) and test_we = '1' for 50 ms;
    wait until rising_edge(clk);
    assert code = x"C1" report "the C game did not get going (code " & to_hstring(code) & ")" severity failure;
    report "TEST PASSED (boot ROM -> launcher (C) running at " & time'image(now) & ")";
    done <= true;
    wait;
  end process;
end architecture;


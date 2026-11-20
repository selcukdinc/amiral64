-- K6: the whole core boots the real boot ROM with the real cartridge image. Button C (= A) is held from
-- reset: the logo is skipped, the menu starts the first game, the game is copied from the cartridge into
-- game RAM, runs and reports $A1.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_boot is end;

architecture sim of tb_boot is
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
              uart_txd => txd, uart_rxd => '1', aud_pwm => open, aud_sd => open, vga_rgb => open, vga_hs => open, vga_vs => open,
              dbg_test_we => test_we, dbg_test_code => code, dbg_addr => open, dbg_cpu_en => open);
  process
  begin
    wait for 200 ns; wait until rising_edge(clk); rst <= '0';
    wait until rising_edge(clk) and test_we = '1' for 300 ms;
    wait until rising_edge(clk);
    assert code = x"A1" report "the first game did not start (code " & to_hstring(code) & ")" severity failure;
    report "TEST PASSED (boot ROM -> menu -> game A at " & time'image(now) & ")";
    done <= true;
    wait;
  end process;
end architecture;

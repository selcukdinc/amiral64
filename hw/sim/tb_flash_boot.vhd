-- K7: like tb_boot, but the SPI flash model holds a different cartridge (test game B first): the boot ROM
-- must find it through the SPI flash reader, prefer it to the BRAM cartridge and start game B ($B2).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_flash_boot is end;

architecture sim of tb_flash_boot is
  signal clk : std_logic := '0';
  signal rst : std_logic := '1';
  signal test_we : std_logic;
  signal code : std_logic_vector(7 downto 0);
  signal txd : std_logic;
  signal done : boolean := false;
  signal sck, cs_n, mosi, miso : std_logic;
begin
  clk <= not clk after 20 ns when not done;
  dut : entity work.konsol_core
    port map (clk => clk, rst => rst, sw => x"0000", buttons => "10000", leds => open, rgb16 => open, rgb17 => open,
              digits => open, digit_blank => open, digit_dots => open,
              ps2_clk => '1', ps2_data => '1', nes_latch => open, nes_clk => open, nes_data => "1111",
              uart_txd => txd, uart_rxd => '1', aud_pwm => open, aud_sd => open, flash_sck => sck, flash_cs_n => cs_n, flash_mosi => mosi, flash_miso => miso, vga_rgb => open, vga_hs => open, vga_vs => open,
              dbg_test_we => test_we, dbg_test_code => code, dbg_addr => open, dbg_cpu_en => open);
  flash : entity work.spi_flash_model port map (sck, cs_n, mosi, miso);
  process
  begin
    wait for 200 ns; wait until rising_edge(clk); rst <= '0';
    wait until rising_edge(clk) and test_we = '1' for 300 ms;
    wait until rising_edge(clk);
    assert code = x"B2" report "the flash cartridge game did not start (code " & to_hstring(code) & ")" severity failure;
    report "TEST PASSED (boot ROM -> flash cartridge -> game B at " & time'image(now) & ")";
    done <= true;
    wait;
  end process;
end architecture;


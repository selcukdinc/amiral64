-- K0: the whole top level with the MMCM model: HSYNC period 32 us (25 MHz pixel clock),
-- LEDs follow the switches, exactly one 7-segment digit lit.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_konsol_k0 is end;

architecture sim of tb_konsol_k0 is
  signal clk100 : std_logic := '0';
  signal resetn : std_logic := '0';
  signal sw, led : std_logic_vector(15 downto 0) := x"A5C3";
  signal seg : std_logic_vector(6 downto 0);
  signal an : std_logic_vector(7 downto 0);
  signal r, g, b : std_logic_vector(3 downto 0);
  signal hs, vs, dp : std_logic;
  signal qspi : std_logic_vector(3 downto 0);
  signal done : boolean := false;
begin
  clk100 <= not clk100 after 5 ns when not done;
  dut : entity work.konsol_top port map (
    CLK100MHZ => clk100, CPU_RESETN => resetn, SW => sw,
    BTNC => '0', BTNU => '0', BTNL => '0', BTNR => '0', BTND => '0',
    LED => led, LED16_R => open, LED16_G => open, LED16_B => open, LED17_R => open, LED17_G => open, LED17_B => open,
    SEG => seg, DP => dp, AN => an, VGA_R => r, VGA_G => g, VGA_B => b, VGA_HS => hs, VGA_VS => vs,
    PS2_CLK => '1', PS2_DATA => '1', UART_TXD_IN => '1', UART_RXD_OUT => open, AUD_PWM => open, AUD_SD => open,
    NES_LATCH => open, NES_CLK => open, NES_DATA => "1111", QSPI_CSN => open, QSPI_DQ => qspi);

  process
    variable t1, t2 : time;
    variable lit : integer;
  begin
    wait for 1 us; resetn <= '1';
    wait until falling_edge(hs); t1 := now;
    wait until falling_edge(hs); t2 := now;
    assert t2 - t1 = 32 us report "HSYNC period " & time'image(t2 - t1) severity failure;
    assert led = sw report "LEDs do not follow the switches" severity failure;
    lit := 0;
    for i in an'range loop if an(i) = '0' then lit := lit + 1; end if; end loop;
    assert lit = 1 report "lit digits: " & integer'image(lit) severity failure;
    report "TEST PASSED";
    done <= true;
    std.env.finish;   -- the MMCM model keeps running
    wait;
  end process;
end architecture;

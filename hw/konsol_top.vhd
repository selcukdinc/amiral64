-- Retro konsol: top level for the Digilent Nexys 4 DDR.
-- Clocking and pins around konsol_core.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity konsol_top is
  port (
    CLK100MHZ  : in  std_logic;
    CPU_RESETN : in  std_logic;
    SW         : in  std_logic_vector(15 downto 0);
    BTNC, BTNU, BTNL, BTNR, BTND : in std_logic;
    LED        : out std_logic_vector(15 downto 0);
    LED16_R, LED16_G, LED16_B, LED17_R, LED17_G, LED17_B : out std_logic;
    SEG        : out std_logic_vector(6 downto 0);
    DP         : out std_logic;
    AN         : out std_logic_vector(7 downto 0);
    VGA_R, VGA_G, VGA_B : out std_logic_vector(3 downto 0);
    VGA_HS, VGA_VS : out std_logic;
    PS2_CLK, PS2_DATA : in std_logic;
    UART_TXD_IN  : in  std_logic;    -- from the PC (FTDI TX)
    UART_RXD_OUT : out std_logic;    -- to the PC (FTDI RX)
    AUD_PWM    : out std_logic;
    AUD_SD     : out std_logic;
    NES_LATCH, NES_CLK : out std_logic;              -- Pmod JA1, JA2
    NES_DATA   : in  std_logic_vector(3 downto 0);   -- Pmod JA3, JA4, JA7, JA8 (players 1..4)
    QSPI_CSN   : out std_logic;
    QSPI_DQ    : inout std_logic_vector(3 downto 0)
  );
end entity;

architecture rtl of konsol_top is
  signal clk, rst : std_logic;
  signal rgb : std_logic_vector(11 downto 0);
  signal digits : std_logic_vector(31 downto 0);
  signal blank, dots : std_logic_vector(7 downto 0);
  signal rgb16, rgb17 : std_logic_vector(2 downto 0);
begin
  clocks : entity work.clock_reset port map (clk100 => CLK100MHZ, cpu_resetn => CPU_RESETN, clk => clk, rst => rst);

  core : entity work.konsol_core
    port map (clk => clk, rst => rst, sw => SW, buttons => BTNC & BTNU & BTNL & BTNR & BTND, leds => LED,
              rgb16 => rgb16, rgb17 => rgb17, digits => digits, digit_blank => blank, digit_dots => dots,
              vga_rgb => rgb, vga_hs => VGA_HS, vga_vs => VGA_VS, dbg_test_we => open, dbg_test_code => open, dbg_addr => open, dbg_cpu_en => open);

  display : entity work.seg7
    port map (clk => clk, rst => rst, value => digits, blank => blank, dots => dots, seg => SEG, dp => DP, an => AN);

  VGA_R <= rgb(11 downto 8);
  VGA_G <= rgb(7 downto 4);
  VGA_B <= rgb(3 downto 0);

  LED16_R <= rgb16(2); LED16_G <= rgb16(1); LED16_B <= rgb16(0);
  LED17_R <= rgb17(2); LED17_G <= rgb17(1); LED17_B <= rgb17(0);
  UART_RXD_OUT <= '1';
  AUD_PWM <= '0';
  AUD_SD <= '0';
  NES_LATCH <= '0';
  NES_CLK <= '1';
  QSPI_CSN <= '1';
  QSPI_DQ <= (others => 'Z');
end architecture;

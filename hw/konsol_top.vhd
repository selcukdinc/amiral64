-- Retro konsol: top level for the Digilent Nexys 4 DDR.
-- Clocking and pins around konsol_core. Until the PPU exists (K2) the screen shows a test pattern.
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
  signal hs, vs, active, frame_start : std_logic;
  signal x, y : unsigned(9 downto 0);
  signal frames : unsigned(31 downto 0) := (others => '0');
  signal rgb : std_logic_vector(11 downto 0);
  signal digits : std_logic_vector(31 downto 0);
  signal blank, dots : std_logic_vector(7 downto 0);
  signal rgb16, rgb17 : std_logic_vector(2 downto 0);
begin
  clocks : entity work.clock_reset port map (clk100 => CLK100MHZ, cpu_resetn => CPU_RESETN, clk => clk, rst => rst);

  vga : entity work.vga_timing
    port map (clk => clk, rst => rst, hsync => hs, vsync => vs, active => active, x => x, y => y,
              line_start => open, frame_start => frame_start, vblank_start => open);

  -- Test pattern: 8 colour bars over a 16-pixel grid (320x240 logical pixels, each 2x2 on screen).
  process (clk)
    variable px : unsigned(8 downto 0);
    variable py : unsigned(8 downto 0);
    variable bar : unsigned(2 downto 0);
  begin
    if rising_edge(clk) then
      px := x(9 downto 1);
      py := y(9 downto 1);
      bar := resize(px / 40, 3);
      if active = '0' then
        rgb <= (others => '0');
      elsif px(3 downto 0) = "0000" or py(3 downto 0) = "0000" then
        rgb <= x"FFF";
      else
        rgb <= (bar(2) & bar(2) & bar(2) & bar(2)) & (bar(1) & bar(1) & bar(1) & bar(1)) & (bar(0) & bar(0) & bar(0) & bar(0));
      end if;
      VGA_HS <= hs;   -- registered together with the colour so they stay aligned
      VGA_VS <= vs;
      if frame_start = '1' then frames <= frames + 1; end if;
      if rst = '1' then frames <= (others => '0'); end if;
    end if;
  end process;
  VGA_R <= rgb(11 downto 8);
  VGA_G <= rgb(7 downto 4);
  VGA_B <= rgb(3 downto 0);

  core : entity work.konsol_core
    port map (clk => clk, rst => rst, sw => SW, buttons => BTNC & BTNU & BTNL & BTNR & BTND, leds => LED,
              rgb16 => rgb16, rgb17 => rgb17, digits => digits, digit_blank => blank, digit_dots => dots,
              frame_tick => frame_start, dbg_test_we => open, dbg_test_code => open, dbg_addr => open, dbg_cpu_en => open);

  display : entity work.seg7
    port map (clk => clk, rst => rst, value => digits, blank => blank, dots => dots, seg => SEG, dp => DP, an => AN);

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

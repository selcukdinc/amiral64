-- 100 MHz board clock -> 25 MHz system clock (MMCM, VCO 1000 MHz) and a synchronous reset that is held
-- until the MMCM locks and while CPU_RESETN is pressed.
library ieee;
use ieee.std_logic_1164.all;
use work.xilinx_prims.all;

entity clock_reset is
  port (
    clk100     : in  std_logic;
    cpu_resetn : in  std_logic;
    clk        : out std_logic;   -- 25 MHz
    rst        : out std_logic    -- synchronous to clk, active high
  );
end entity;

architecture rtl of clock_reset is
  signal fb, clk_raw, clk_i, locked : std_logic;
  signal sync : std_logic_vector(3 downto 0) := (others => '1');
begin
  mmcm : MMCME2_BASE
    generic map (CLKIN1_PERIOD => 10.0, CLKFBOUT_MULT_F => 10.0, DIVCLK_DIVIDE => 1, CLKOUT0_DIVIDE_F => 40.0)
    port map (CLKIN1 => clk100, CLKFBIN => fb, RST => '0', PWRDWN => '0',
              CLKOUT0 => clk_raw, CLKOUT1 => open, CLKFBOUT => fb, LOCKED => locked);
  bg : BUFG port map (I => clk_raw, O => clk_i);
  clk <= clk_i;

  process (clk_i, locked)
  begin
    if locked = '0' then
      sync <= (others => '1');
    elsif rising_edge(clk_i) then
      sync <= sync(2 downto 0) & not cpu_resetn;
    end if;
  end process;
  rst <= sync(3);
end architecture;

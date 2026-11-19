-- Behavioural simulation models of the hand-instantiated Xilinx primitives (simulation only:
-- these files are in sim_1, never in synthesis).
library ieee;
use ieee.std_logic_1164.all;

entity MMCME2_BASE is
  generic (
    BANDWIDTH        : string  := "OPTIMIZED";
    CLKIN1_PERIOD    : real    := 0.0;
    CLKFBOUT_MULT_F  : real    := 5.0;
    DIVCLK_DIVIDE    : integer := 1;
    CLKOUT0_DIVIDE_F : real    := 1.0;
    CLKOUT1_DIVIDE   : integer := 1
  );
  port (
    CLKIN1, CLKFBIN, RST, PWRDWN : in std_logic;
    CLKOUT0, CLKOUT1, CLKFBOUT, LOCKED : out std_logic
  );
end entity;

architecture model of MMCME2_BASE is
  constant VCO_NS : real := CLKIN1_PERIOD * real(DIVCLK_DIVIDE) / CLKFBOUT_MULT_F;
  signal c0, c1, lock : std_logic := '0';
begin
  -- Free-running outputs at the programmed frequencies; LOCKED rises after a few input cycles.
  c0 <= not c0 after (VCO_NS * CLKOUT0_DIVIDE_F / 2.0) * 1 ns when RST = '0' else '0';
  c1 <= not c1 after (VCO_NS * real(CLKOUT1_DIVIDE) / 2.0) * 1 ns when RST = '0' else '0';
  process
  begin
    lock <= '0';
    wait for CLKIN1_PERIOD * 20.0 * 1 ns;
    lock <= '1';
    wait until RST = '1';
  end process;
  CLKOUT0 <= c0; CLKOUT1 <= c1; CLKFBOUT <= CLKFBIN; LOCKED <= lock and not RST;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
entity BUFG is
  port (I : in std_logic; O : out std_logic);
end entity;
architecture model of BUFG is
begin
  O <= I;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
entity STARTUPE2 is
  port (
    CFGCLK, CFGMCLK, EOS, PREQ : out std_logic;
    CLK, GSR, GTS, KEYCLEARB, PACK, USRCCLKO, USRCCLKTS, USRDONEO, USRDONETS : in std_logic
  );
end entity;
architecture model of STARTUPE2 is
begin
  CFGCLK <= '0'; CFGMCLK <= '0'; EOS <= '1'; PREQ <= '0';
end architecture;

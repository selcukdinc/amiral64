-- Component declarations of the Xilinx 7-series primitives the console instantiates by hand
-- (no unisim library in the open-source flow). yosys/nextpnr map them to the real primitives;
-- hw/sim/xilinx_prims_sim.vhd has simple behavioural models for GHDL.
library ieee;
use ieee.std_logic_1164.all;

package xilinx_prims is
  component MMCME2_BASE is
    generic (
      BANDWIDTH        : string  := "OPTIMIZED";
      CLKIN1_PERIOD    : real    := 0.0;
      CLKFBOUT_MULT_F  : real    := 5.0;
      DIVCLK_DIVIDE    : integer := 1;
      CLKOUT0_DIVIDE_F : real    := 1.0;
      CLKOUT1_DIVIDE   : integer := 1
    );
    port (
      CLKIN1   : in  std_logic;
      CLKFBIN  : in  std_logic;
      RST      : in  std_logic;
      PWRDWN   : in  std_logic;
      CLKOUT0  : out std_logic;
      CLKOUT1  : out std_logic;
      CLKFBOUT : out std_logic;
      LOCKED   : out std_logic
    );
  end component;

  component BUFG is
    port (I : in std_logic; O : out std_logic);
  end component;

  component STARTUPE2 is
    port (
      CFGCLK, CFGMCLK, EOS, PREQ : out std_logic;
      CLK, GSR, GTS, KEYCLEARB, PACK, USRCCLKO, USRCCLKTS, USRDONEO, USRDONETS : in std_logic
    );
  end component;
end package;

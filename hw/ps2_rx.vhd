-- PS/2 receiver (keyboard -> FPGA, from the Nexys 4 DDR's PIC24 USB host): 11-bit frames, data sampled
-- on the falling edge of PS2_CLK, odd parity. A frame that stalls for 2 ms is dropped.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity ps2_rx is
  generic (CLK_HZ : integer := 25_000_000);
  port (
    clk, rst  : in  std_logic;
    ps2_clk   : in  std_logic;
    ps2_data  : in  std_logic;
    valid     : out std_logic;                     -- one clock, byte in code
    code      : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of ps2_rx is
  constant TIMEOUT : integer := CLK_HZ / 500;
  signal c_sync, d_sync : std_logic_vector(2 downto 0) := (others => '1');
  signal filt : std_logic_vector(7 downto 0) := (others => '1');
  signal c_clean, c_prev : std_logic := '1';
  signal bits : std_logic_vector(10 downto 0) := (others => '0');
  signal n : integer range 0 to 11 := 0;
  signal idle : integer range 0 to TIMEOUT := 0;
begin
  process (clk)
  begin
    if rising_edge(clk) then
      c_sync <= c_sync(1 downto 0) & ps2_clk;
      d_sync <= d_sync(1 downto 0) & ps2_data;
      -- glitch filter: the clock must be stable for 8 samples to change
      filt <= filt(6 downto 0) & c_sync(2);
      if filt = x"FF" then c_clean <= '1'; elsif filt = x"00" then c_clean <= '0'; end if;
      c_prev <= c_clean;
      valid <= '0';
      if c_prev = '1' and c_clean = '0' then
        bits <= d_sync(2) & bits(10 downto 1);
        idle <= 0;
        if n = 10 then
          n <= 0;
          -- bits(0) start (0), bits(8..1) data, bits(9) parity, d_sync = stop (1)
          if bits(1) = '0' and d_sync(2) = '1' and (xor bits(10 downto 2)) = '1' then
            code <= bits(9 downto 2);
            valid <= '1';
          end if;
        else
          n <= n + 1;
        end if;
      elsif idle = TIMEOUT then
        n <= 0;
      else
        idle <= idle + 1;
      end if;
      if rst = '1' then n <= 0; idle <= 0; end if;
    end if;
  end process;
end architecture;

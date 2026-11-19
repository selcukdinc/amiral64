-- 8-digit common-anode 7-segment display (Nexys 4 DDR): shows 32 bits as hex, one digit at a time.
-- blank(i) = '1' turns digit i off. Each digit is lit for 2^SCAN_BITS / 8 clocks.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity seg7 is
  generic (SCAN_BITS : integer := 17);   -- 25 MHz / 2^17 * 8 = 1.5 kHz per digit
  port (
    clk, rst : in  std_logic;
    value    : in  std_logic_vector(31 downto 0);
    blank    : in  std_logic_vector(7 downto 0);
    dots     : in  std_logic_vector(7 downto 0);
    seg      : out std_logic_vector(6 downto 0);   -- CA..CG, active low (seg(0) = CA)
    dp       : out std_logic;                      -- active low
    an       : out std_logic_vector(7 downto 0)    -- active low
  );
end entity;

architecture rtl of seg7 is
  signal cnt : unsigned(SCAN_BITS - 1 downto 0) := (others => '0');
  signal digit : integer range 0 to 7;
  signal nib : std_logic_vector(3 downto 0);
begin
  process (clk)
  begin
    if rising_edge(clk) then
      if rst = '1' then cnt <= (others => '0'); else cnt <= cnt + 1; end if;
    end if;
  end process;

  digit <= to_integer(cnt(SCAN_BITS - 1 downto SCAN_BITS - 3));
  nib <= value(digit * 4 + 3 downto digit * 4);

  process (digit, blank)
  begin
    an <= (others => '1');
    if blank(digit) = '0' then an(digit) <= '0'; end if;
  end process;
  dp <= not dots(digit);

  with nib select seg <=         -- gfedcba, active low
    "1000000" when x"0", "1111001" when x"1", "0100100" when x"2", "0110000" when x"3",
    "0011001" when x"4", "0010010" when x"5", "0000010" when x"6", "1111000" when x"7",
    "0000000" when x"8", "0010000" when x"9", "0001000" when x"A", "0000011" when x"B",
    "1000110" when x"C", "0100001" when x"D", "0000110" when x"E", "0001110" when others;
end architecture;

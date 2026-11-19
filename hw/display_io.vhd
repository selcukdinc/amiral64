-- I/O page $84: 7-segment digits, LEDs, RGB LEDs.
--   $8400-$8403 digits (hex, $8400 = rightmost two)   $8404 digit blank mask   $8405 decimal points
--   $8406 LED[7:0]   $8407 LED[15:8]   $8408 RGB LED16 (bits 2..0 = R,G,B)   $8409 RGB LED17
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity display_io is
  port (
    clk, rst : in  std_logic;
    we       : in  std_logic;
    addr     : in  std_logic_vector(3 downto 0);
    wdata    : in  std_logic_vector(7 downto 0);
    rdata    : out std_logic_vector(7 downto 0);
    digits   : out std_logic_vector(31 downto 0);
    blank    : out std_logic_vector(7 downto 0);
    dots     : out std_logic_vector(7 downto 0);
    leds     : out std_logic_vector(15 downto 0);
    rgb16    : out std_logic_vector(2 downto 0);
    rgb17    : out std_logic_vector(2 downto 0)
  );
end entity;

architecture rtl of display_io is
  type regs_t is array (0 to 9) of std_logic_vector(7 downto 0);
  signal r : regs_t := (others => (others => '0'));
begin
  process (clk)
  begin
    if rising_edge(clk) then
      if rst = '1' then
        r <= (others => (others => '0'));
      elsif we = '1' and unsigned(addr) <= 9 then
        r(to_integer(unsigned(addr))) <= wdata;
      end if;
    end if;
  end process;
  rdata <= r(to_integer(unsigned(addr))) when unsigned(addr) <= 9 else x"00";
  digits <= r(3) & r(2) & r(1) & r(0);
  blank <= r(4);
  dots <= r(5);
  leds <= r(7) & r(6);
  rgb16 <= r(8)(2 downto 0);
  rgb17 <= r(9)(2 downto 0);
end architecture;

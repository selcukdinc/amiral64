-- Dual-port synchronous RAM (inferred block RAM or LUT RAM): port A read-only, port B read/write,
-- one clock. Read data appears one clock after the address on both ports.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity bram_dp is
  generic (ADDR_BITS : integer := 10; WIDTH : integer := 8);
  port (
    clk     : in  std_logic;
    a_addr  : in  std_logic_vector(ADDR_BITS - 1 downto 0);
    a_rdata : out std_logic_vector(WIDTH - 1 downto 0);
    b_we    : in  std_logic;
    b_addr  : in  std_logic_vector(ADDR_BITS - 1 downto 0);
    b_wdata : in  std_logic_vector(WIDTH - 1 downto 0);
    b_rdata : out std_logic_vector(WIDTH - 1 downto 0)
  );
end entity;

architecture rtl of bram_dp is
  type mem_t is array (0 to 2 ** ADDR_BITS - 1) of std_logic_vector(WIDTH - 1 downto 0);
  signal mem : mem_t := (others => (others => '0'));
begin
  process (clk)
  begin
    if rising_edge(clk) then
      if b_we = '1' then mem(to_integer(unsigned(b_addr))) <= b_wdata; end if;
      b_rdata <= mem(to_integer(unsigned(b_addr)));
      a_rdata <= mem(to_integer(unsigned(a_addr)));
    end if;
  end process;
end architecture;

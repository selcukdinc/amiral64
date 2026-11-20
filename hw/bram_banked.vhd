-- A large ROM built from 4 KB banks (one RAMB36 each), each initialized from its own file
-- <INIT_PREFIX>NN.mem (NN = bank number in hex). yosys maps many small initialized memories much faster
-- than one big one. Read data appears one clock after the address.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity bram_banked is
  generic (BANKS : integer := 16; INIT_PREFIX : string := "cart_");
  port (
    clk   : in  std_logic;
    addr  : in  std_logic_vector(23 downto 0);
    rdata : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of bram_banked is
  type q_t is array (0 to BANKS - 1) of std_logic_vector(7 downto 0);
  signal q : q_t;
  signal sel : integer range 0 to 4095 := 0;
  function hex2(n : integer) return string is
    constant d : string(1 to 16) := "0123456789abcdef";
  begin
    return d(n / 16 + 1) & d(n mod 16 + 1);
  end function;
begin
  gen_banks : for i in 0 to BANKS - 1 generate
    b : entity work.bram generic map (ADDR_BITS => 12, DEPTH => 4096, INIT_FILE => INIT_PREFIX & hex2(i) & ".mem")
      port map (clk => clk, we => '0', addr => addr(11 downto 0), wdata => x"00", rdata => q(i));
  end generate;
  process (clk)
  begin
    if rising_edge(clk) then sel <= to_integer(unsigned(addr(23 downto 12))); end if;
  end process;
  rdata <= q(sel) when sel < BANKS else x"FF";
end architecture;

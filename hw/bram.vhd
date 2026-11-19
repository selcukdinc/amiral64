-- Single-port synchronous RAM/ROM (inferred block RAM), optionally initialized from a hex .mem file
-- (one word per line). Read data appears one clock after the address.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;

entity bram is
  generic (
    ADDR_BITS : integer := 10;
    DEPTH     : integer := 1024;          -- may be less than 2**ADDR_BITS
    WIDTH     : integer := 8;
    INIT_FILE : string  := ""             -- relative to the run directory; "" = all zero
  );
  port (
    clk   : in  std_logic;
    we    : in  std_logic;
    addr  : in  std_logic_vector(ADDR_BITS - 1 downto 0);
    wdata : in  std_logic_vector(WIDTH - 1 downto 0);
    rdata : out std_logic_vector(WIDTH - 1 downto 0)
  );
end entity;

architecture rtl of bram is
  type mem_t is array (0 to DEPTH - 1) of std_logic_vector(WIDTH - 1 downto 0);

  impure function load return mem_t is
    file f : text;
    variable l : line;
    variable m : mem_t := (others => (others => '0'));
    variable status : file_open_status;
  begin
    if INIT_FILE'length > 0 then
      file_open(status, f, INIT_FILE, read_mode);
      assert status = open_ok report "bram: cannot open " & INIT_FILE severity failure;
      for i in 0 to DEPTH - 1 loop
        exit when endfile(f);
        readline(f, l);
        hread(l, m(i));
      end loop;
      file_close(f);
    end if;
    return m;
  end function;

  signal mem : mem_t := load;
begin
  process (clk)
    variable a : integer;
  begin
    if rising_edge(clk) then
      a := to_integer(unsigned(addr));
      if a < DEPTH then
        if we = '1' then mem(a) <= wdata; end if;
        rdata <= mem(a);
      else
        rdata <= (others => '0');
      end if;
    end if;
  end process;
end architecture;

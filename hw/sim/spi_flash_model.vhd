-- Simulation model of the board's SPI flash: READ ($03) + 24-bit address, mode 0. The array holds the
-- bytes from flash address BASE on (loaded from a .mem file); other addresses read $FF.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;

entity spi_flash_model is
  generic (INIT_FILE : string := "cart_flash.mem"; BASE : integer := 16#400000#; SIZE : integer := 65536);
  port (sck, cs_n, mosi : in std_logic; miso : out std_logic);
end entity;

architecture model of spi_flash_model is
  type mem_t is array (0 to SIZE - 1) of std_logic_vector(7 downto 0);
  impure function load return mem_t is
    file f : text open read_mode is INIT_FILE;
    variable l : line;
    variable m : mem_t := (others => x"FF");
  begin
    for i in 0 to SIZE - 1 loop
      exit when endfile(f);
      readline(f, l); hread(l, m(i));
    end loop;
    return m;
  end function;
  constant mem : mem_t := load;
begin
  process
    variable cmd : std_logic_vector(31 downto 0);
    variable a : integer;
    variable b : std_logic_vector(7 downto 0);
  begin
    miso <= 'Z';
    wait until cs_n = '0';
    for i in 31 downto 0 loop
      wait until rising_edge(sck) or cs_n = '1';
      if cs_n = '1' then exit; end if;
      cmd(i) := mosi;
    end loop;
    if cs_n = '0' and cmd(31 downto 24) = x"03" then
      a := to_integer(unsigned(cmd(23 downto 0)));
      loop
        if a >= BASE and a < BASE + SIZE then b := mem(a - BASE); else b := x"FF"; end if;
        for i in 7 downto 0 loop
          wait until falling_edge(sck) or cs_n = '1';
          if cs_n = '1' then exit; end if;
          miso <= b(i) after 5 ns;
        end loop;
        exit when cs_n = '1';
        a := a + 1;
      end loop;
    end if;
    if cs_n = '0' then wait until cs_n = '1'; end if;
  end process;
end architecture;

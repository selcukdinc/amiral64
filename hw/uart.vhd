-- 8N1 UART with a 16-byte receive FIFO. BAUD default 115200 at 25 MHz.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity uart is
  generic (CLK_HZ : integer := 25_000_000; BAUD : integer := 115_200);
  port (
    clk, rst   : in  std_logic;
    tx_start   : in  std_logic;
    tx_data    : in  std_logic_vector(7 downto 0);
    tx_busy    : out std_logic;
    txd        : out std_logic;
    rxd        : in  std_logic;
    rx_pop     : in  std_logic;
    rx_avail   : out std_logic;
    rx_data    : out std_logic_vector(7 downto 0);
    rx_overrun : out std_logic
  );
end entity;

architecture rtl of uart is
  constant DIV : integer := (CLK_HZ + BAUD / 2) / BAUD;
  signal tx_cnt : integer range 0 to DIV - 1 := 0;
  signal tx_bit : integer range 0 to 10 := 0;
  signal tx_shift : std_logic_vector(9 downto 0) := (others => '1');
  signal rx_sync : std_logic_vector(2 downto 0) := (others => '1');
  signal rx_cnt : integer range 0 to DIV - 1 := 0;
  signal rx_bit : integer range 0 to 10 := 0;
  signal rx_shift : std_logic_vector(7 downto 0);
  type fifo_t is array (0 to 15) of std_logic_vector(7 downto 0);
  signal fifo : fifo_t;
  signal wp, rp : unsigned(3 downto 0) := (others => '0');
  signal count : integer range 0 to 16 := 0;
  signal ovr : std_logic := '0';
begin
  -- transmitter
  process (clk)
  begin
    if rising_edge(clk) then
      if tx_bit = 0 then
        if tx_start = '1' then
          tx_shift <= '1' & tx_data & '0';
          tx_bit <= 10;
          tx_cnt <= DIV - 1;
        end if;
      elsif tx_cnt = 0 then
        tx_cnt <= DIV - 1;
        tx_shift <= '1' & tx_shift(9 downto 1);
        tx_bit <= tx_bit - 1;
      else
        tx_cnt <= tx_cnt - 1;
      end if;
      if rst = '1' then tx_bit <= 0; tx_shift <= (others => '1'); end if;
    end if;
  end process;
  txd <= tx_shift(0) when tx_bit /= 0 else '1';
  tx_busy <= '1' when tx_bit /= 0 else '0';

  -- receiver: start edge, then sample in the middle of each bit
  process (clk)
    variable push : boolean;
  begin
    if rising_edge(clk) then
      rx_sync <= rx_sync(1 downto 0) & rxd;
      push := false;
      if rx_bit = 0 then
        if rx_sync(2) = '0' then
          rx_bit <= 10;
          rx_cnt <= DIV / 2;
        end if;
      elsif rx_cnt = 0 then
        rx_cnt <= DIV - 1;
        if rx_bit = 10 then
          if rx_sync(2) = '1' then rx_bit <= 0; else rx_bit <= 9; end if;   -- false start
        elsif rx_bit = 1 then
          rx_bit <= 0;
          if rx_sync(2) = '1' then push := true; end if;                       -- stop bit
        else
          rx_shift <= rx_sync(2) & rx_shift(7 downto 1);
          rx_bit <= rx_bit - 1;
        end if;
      else
        rx_cnt <= rx_cnt - 1;
      end if;

      if push then
        if count < 16 then
          fifo(to_integer(wp)) <= rx_shift;
          wp <= wp + 1;
        else
          ovr <= '1';
        end if;
      end if;
      if rx_pop = '1' and count > 0 then rp <= rp + 1; end if;
      if push and count < 16 and not (rx_pop = '1' and count > 0) then
        count <= count + 1;
      elsif not (push and count < 16) and rx_pop = '1' and count > 0 then
        count <= count - 1;
      end if;
      if rx_pop = '1' then ovr <= '0'; end if;
      if rst = '1' then rx_bit <= 0; wp <= (others => '0'); rp <= (others => '0'); count <= 0; ovr <= '0'; end if;
    end if;
  end process;
  rx_avail <= '1' when count > 0 else '0';
  rx_data <= fifo(to_integer(rp));
  rx_overrun <= ovr;
end architecture;

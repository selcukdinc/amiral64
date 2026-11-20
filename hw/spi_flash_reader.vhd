-- Sequential reader for the configuration flash (S25FL128S, SPI mode 0, READ $03, 24-bit address).
-- start = begin reading at addr (prefetches one byte); next = take the byte and fetch the following one
-- (the READ command keeps streaming, so sequential bytes cost only 8 SPI clocks). SCK = clk / (2 * HALF).
-- On the board SCK reaches the CCLK pin through STARTUPE2, which ignores the first clock edges after
-- configuration: the reader sends a few dummy clocks with CS high first.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity spi_flash_reader is
  generic (HALF : integer := 2);
  port (
    clk, rst : in  std_logic;
    start    : in  std_logic;
    addr     : in  std_logic_vector(23 downto 0);
    next_b   : in  std_logic;
    ready    : out std_logic;
    data     : out std_logic_vector(7 downto 0);
    sck, cs_n, mosi : out std_logic;
    miso     : in  std_logic
  );
end entity;

architecture rtl of spi_flash_reader is
  type st_t is (WARMUP, IDLE, CMD, RD, HAVE);
  signal st : st_t := WARMUP;
  signal t : integer range 0 to HALF - 1 := 0;
  signal phase : std_logic := '0';                     -- 0: SCK low half, 1: SCK high half
  signal sh : std_logic_vector(31 downto 0) := (others => '0');
  signal n : integer range 0 to 32 := 0;
  signal din : std_logic_vector(7 downto 0) := (others => '0');
  signal rdy : std_logic := '0';
  signal sck_i : std_logic := '0';
  signal pending : std_logic := '0';
  signal pend_addr : std_logic_vector(23 downto 0);
begin
  process (clk)
  begin
    if rising_edge(clk) then
      if start = '1' then
        pending <= '1'; pend_addr <= addr; rdy <= '0';
      end if;
      case st is
        when WARMUP =>                                   -- 8 clocks with CS high
          cs_n <= '1';
          if t = HALF - 1 then
            t <= 0; sck_i <= not sck_i;
            if sck_i = '1' then
              if n = 7 then n <= 0; st <= IDLE; else n <= n + 1; end if;
            end if;
          else
            t <= t + 1;
          end if;
        when IDLE =>
          cs_n <= '1'; sck_i <= '0';
          if pending = '1' or start = '1' then
            pending <= '0';
            if start = '1' then sh <= x"03" & addr; else sh <= x"03" & pend_addr; end if;
            n <= 0; t <= 0; phase <= '0';
            cs_n <= '0';
            st <= CMD;
          end if;
        when CMD | RD =>
          if start = '1' then
            cs_n <= '1'; sck_i <= '0'; st <= IDLE;      -- new address: restart
          elsif t = HALF - 1 then
            t <= 0;
            if phase = '0' then
              sck_i <= '1'; phase <= '1';
              if st = RD then din <= din(6 downto 0) & miso; end if;   -- sample on the rising edge
            else
              sck_i <= '0'; phase <= '0';
              if st = CMD then
                sh <= sh(30 downto 0) & '0';
                if n = 31 then n <= 0; st <= RD; else n <= n + 1; end if;
              else
                if n = 7 then n <= 0; st <= HAVE; rdy <= '1'; else n <= n + 1; end if;
              end if;
            end if;
          else
            t <= t + 1;
          end if;
        when HAVE =>
          if start = '1' then
            cs_n <= '1'; st <= IDLE;
          elsif next_b = '1' then
            rdy <= '0'; t <= 0; phase <= '0'; n <= 0; st <= RD;
          end if;
      end case;
      if rst = '1' then st <= WARMUP; cs_n <= '1'; sck_i <= '0'; rdy <= '0'; pending <= '0'; n <= 0; t <= 0; end if;
    end if;
  end process;
  sck <= sck_i;
  mosi <= sh(31);
  ready <= rdy;
  data <= din;
end architecture;

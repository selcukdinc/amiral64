-- I/O page $83: frame counter and a microsecond interval timer with IRQ.
--   $8300/$8301 frame counter (read; $8300 latches the high byte)   $8302/$8303 timer period in us (lo/hi)
--   $8304 control: bit0 run, bit1 IRQ enable   $8305 status: bit0 expired (write 1 to clear)
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity timer_io is
  generic (CLKS_PER_US : integer := 25);
  port (
    clk, rst    : in  std_logic;
    frame_tick  : in  std_logic;
    we, re      : in  std_logic;
    addr        : in  std_logic_vector(3 downto 0);
    wdata       : in  std_logic_vector(7 downto 0);
    rdata       : out std_logic_vector(7 downto 0);
    irq         : out std_logic
  );
end entity;

architecture rtl of timer_io is
  signal frames : unsigned(15 downto 0) := (others => '0');
  signal frames_hi : std_logic_vector(7 downto 0) := (others => '0');
  signal period, count : unsigned(15 downto 0) := (others => '0');
  signal prescale : integer range 0 to CLKS_PER_US - 1 := 0;
  signal run, irq_en, expired : std_logic := '0';
begin
  process (clk)
  begin
    if rising_edge(clk) then
      if frame_tick = '1' then frames <= frames + 1; end if;
      if run = '1' then
        if prescale = CLKS_PER_US - 1 then
          prescale <= 0;
          if count = 0 then
            count <= period;
            expired <= '1';
          else
            count <= count - 1;
          end if;
        else
          prescale <= prescale + 1;
        end if;
      end if;
      if re = '1' and addr = x"0" then frames_hi <= std_logic_vector(frames(15 downto 8)); end if;
      if we = '1' then
        case addr is
          when x"2" => period(7 downto 0) <= unsigned(wdata);
          when x"3" => period(15 downto 8) <= unsigned(wdata); count <= unsigned(wdata) & period(7 downto 0); prescale <= 0;
          when x"4" => run <= wdata(0); irq_en <= wdata(1);
          when x"5" => if wdata(0) = '1' then expired <= '0'; end if;
          when others => null;
        end case;
      end if;
      if rst = '1' then
        frames <= (others => '0'); run <= '0'; irq_en <= '0'; expired <= '0'; period <= (others => '0');
      end if;
    end if;
  end process;

  with addr select rdata <=
    std_logic_vector(frames(7 downto 0)) when x"0",
    frames_hi when x"1",
    std_logic_vector(period(7 downto 0)) when x"2",
    std_logic_vector(period(15 downto 8)) when x"3",
    "000000" & irq_en & run when x"4",
    "0000000" & expired when x"5",
    x"00" when others;
  irq <= expired and irq_en;
end architecture;

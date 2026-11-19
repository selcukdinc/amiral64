-- K4: keyboard and pads. A PS/2 device sends W (make), E0 75 (up arrow), F0 1D (W break) and a frame with
-- a parity error; four NES pads (CD4021 models) hold different buttons. Checks the key table, the scan code
-- FIFO, the keyboard players and the pad bytes.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_input is end;

architecture sim of tb_input is
  signal clk : std_logic := '0';
  signal rst : std_logic := '1';
  signal frame_tick, we, re : std_logic := '0';
  signal addr : std_logic_vector(5 downto 0) := (others => '0');
  signal rdata : std_logic_vector(7 downto 0);
  signal pclk, pdata : std_logic := '1';
  signal latch, nclk : std_logic;
  signal ndata : std_logic_vector(3 downto 0);
  signal done : boolean := false;
  type pads_t is array (0 to 3) of std_logic_vector(7 downto 0);   -- A B Sel Start Up Down Left Right, 1 = pressed
  constant PRESSED : pads_t := (x"81", x"10", x"0C", x"FF");
  signal sr : pads_t;
begin
  clk <= not clk after 20 ns when not done;
  dut : entity work.input_io port map (clk => clk, rst => rst, frame_tick => frame_tick, we => we, re => re, addr => addr,
    rdata => rdata, sw => x"BEEF", buttons => "00000", ps2_clk => pclk, ps2_data => pdata,
    nes_latch => latch, nes_clk => nclk, nes_data => ndata);

  -- CD4021-style pads: LATCH high loads, DATA shows the next button (active low), rising CLK shifts
  pads : for i in 0 to 3 generate
    process (latch, nclk)
    begin
      if latch = '1' then
        sr(i) <= PRESSED(i);
      elsif rising_edge(nclk) then
        sr(i) <= sr(i)(6 downto 0) & '0';
      end if;
    end process;
    ndata(i) <= not sr(i)(7);
  end generate;

  process
    procedure ps2_send(b : std_logic_vector(7 downto 0); bad_parity : boolean := false) is
      variable frame : std_logic_vector(10 downto 0);
      variable par : std_logic;
    begin
      par := not (xor b);
      if bad_parity then par := not par; end if;
      frame := '1' & par & b & '0';
      for i in 0 to 10 loop
        pdata <= frame(i); wait for 20 us;
        pclk <= '0'; wait for 40 us;
        pclk <= '1'; wait for 20 us;
      end loop;
      wait for 100 us;
    end procedure;
    procedure rd(a : integer; v : out std_logic_vector(7 downto 0)) is
    begin
      addr <= std_logic_vector(to_unsigned(a, 6)); wait for 1 ns; v := rdata;
      wait until rising_edge(clk);
    end procedure;
    procedure pop is
    begin
      addr <= "000100"; re <= '1'; wait until rising_edge(clk); re <= '0'; wait until rising_edge(clk);
    end procedure;
    variable v : std_logic_vector(7 downto 0);
  begin
    wait for 100 ns; wait until rising_edge(clk); rst <= '0';
    ps2_send(x"1D");                -- W down
    ps2_send(x"E0"); ps2_send(x"75");   -- up arrow down
    ps2_send(x"12", true);          -- corrupted: dropped
    rd(16 + 16#1D# / 8, v); assert v(16#1D# mod 8) = '1' report "W not in key table" severity failure;
    rd(16 + (128 + 16#75#) / 8, v); assert v((128 + 16#75#) mod 8) = '1' report "up arrow not in key table" severity failure;
    rd(0, v); assert v = x"08" report "pad 1 (W = up): " & to_hstring(v) severity failure;
    rd(1, v); assert v = x"08" report "pad 2 (up arrow): " & to_hstring(v) severity failure;
    rd(5, v); assert v = x"01" report "FIFO status" severity failure;
    rd(4, v); assert v = x"1D" report "FIFO byte 1" severity failure; pop;
    rd(4, v); assert v = x"E0" report "FIFO byte 2" severity failure; pop;
    rd(4, v); assert v = x"75" report "FIFO byte 3" severity failure; pop;
    rd(5, v); assert v = x"00" report "FIFO should be empty (bad parity frame dropped)" severity failure;
    ps2_send(x"F0"); ps2_send(x"1D");   -- W up
    rd(0, v); assert v = x"00" report "W release" severity failure;
    -- pads: one frame tick starts a read; it takes about 18 x 6 us
    frame_tick <= '1'; wait until rising_edge(clk); frame_tick <= '0';
    wait for 150 us; wait until rising_edge(clk);
    rd(0, v); assert v = PRESSED(0) report "pad 1 " & to_hstring(v) severity failure;
    rd(1, v); assert v = (PRESSED(1) or x"08") report "pad 2 " & to_hstring(v) severity failure;
    rd(2, v); assert v = PRESSED(2) report "pad 3 " & to_hstring(v) severity failure;
    rd(3, v); assert v = PRESSED(3) report "pad 4 " & to_hstring(v) severity failure;
    rd(9, v); assert v = x"BE" report "switches" severity failure;
    report "TEST PASSED";
    done <= true;
    wait;
  end process;
end architecture;

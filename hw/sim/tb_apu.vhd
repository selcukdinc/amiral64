-- K5: APU. Square frequency and duty, triangle frequency, decay envelope and length, noise, and the
-- delta-sigma bitstream density following the mixed level.
-- APU v2: version byte, wave channel (frequency and the shape from the wave RAM), attack, sustain,
-- sweep, the mix limit and the output low-pass.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_apu is end;

architecture sim of tb_apu is
  signal clk : std_logic := '0';
  signal rst : std_logic := '1';
  signal frame_tick, we : std_logic := '0';
  signal addr : std_logic_vector(5 downto 0);
  signal wdata, rdata : std_logic_vector(7 downto 0);
  signal level : unsigned(9 downto 0);
  signal pwm, sd, bitq : std_logic;
  signal done : boolean := false;
begin
  clk <= not clk after 20 ns when not done;
  dut : entity work.apu port map (clk, rst, frame_tick, we, addr, wdata, rdata, level, pwm, sd, bitq);

  process
    procedure wr(a : integer; d : integer) is
    begin
      addr <= std_logic_vector(to_unsigned(a, 6)); wdata <= std_logic_vector(to_unsigned(d, 8)); we <= '1';
      wait until rising_edge(clk); we <= '0';
    end procedure;
    procedure tick is
    begin
      frame_tick <= '1'; wait until rising_edge(clk); frame_tick <= '0'; wait until rising_edge(clk);
    end procedure;
    -- counts rising edges of "level > 0" and clocks with level > 0 over a window
    procedure measure(window : time; edges : out integer; high_pct : out real; avg_level : out real; dens : out real) is
      variable e, hi, n, ones : integer := 0;
      variable lsum : real := 0.0;
      variable prev : boolean := false;
      variable t0 : time := now;
    begin
      while now - t0 < window loop
        wait until rising_edge(clk);
        n := n + 1;
        if level > 0 then hi := hi + 1; if not prev then e := e + 1; end if; prev := true; else prev := false; end if;
        lsum := lsum + real(to_integer(level));
        if bitq = '1' then ones := ones + 1; end if;
      end loop;
      edges := e; high_pct := 100.0 * real(hi) / real(n); avg_level := lsum / real(n);
      dens := 1024.0 * real(ones) / real(n);
    end procedure;
    variable e : integer;
    variable hp, avg, dens : real;
  begin
    wait for 100 ns; wait until rising_edge(clk); rst <= '0';
    wr(16, 15); wr(17, 15);                -- master 15, all channels on
    assert sd = '1' report "amplifier should be on" severity failure;
    -- square 0: P = 194 -> 1.5625 MHz / (8 * 195) = 1001.6 Hz, duty 25 %, volume 15
    wr(0, 194); wr(1, 0); wr(2, 16#1F#); wr(3, 0);
    measure(20 ms, e, hp, avg, dens);
    report "square: " & integer'image(e) & " cycles in 20 ms, high " & real'image(hp) & " %";
    assert e >= 19 and e <= 21 report "square frequency" severity failure;
    assert hp > 24.0 and hp < 26.0 report "square duty" severity failure;
    -- delta-sigma density follows the average level within 1 %
    report "level average " & real'image(avg) & ", bitstream " & real'image(dens);
    assert abs (dens - avg) < 0.01 * 1024.0 report "delta-sigma density" severity failure;
    -- decay: volume 15, one step per frame -> silent after 15 frames
    wr(2, 16#AF#); wr(3, 0);
    for i in 1 to 16 loop tick; end loop;
    wait for 2 ms;
    measure(2 ms, e, hp, avg, dens);
    assert e = 0 report "decay did not silence the square" severity failure;
    -- length: 1 * 4 frames, then the note stops
    wr(2, 16#2F#); wr(3, 16#10#);
    for i in 1 to 3 loop tick; end loop;
    measure(4 ms, e, hp, avg, dens);
    assert e >= 3 report "note stopped too early" severity failure;
    tick;
    measure(4 ms, e, hp, avg, dens);
    assert e = 0 report "length did not stop the note" severity failure;
    -- triangle: P = 48 -> 1.5625 MHz / (32 * 49) = 996.5 Hz; level is never 0 only at the bottom step
    wr(8, 48); wr(9, 0); wr(10, 15); wr(11, 0);
    measure(20 ms, e, hp, avg, dens);
    report "triangle: " & integer'image(e) & " rises in 20 ms";
    assert e >= 19 and e <= 21 report "triangle frequency" severity failure;
    assert abs (dens - avg) < 0.01 * 1024.0 report "delta-sigma density (triangle)" severity failure;
    wr(11 - 8 + 8, 0);
    -- noise: random levels
    wr(17, 8); wr(12, 3); wr(13, 0); wr(14, 15); wr(15, 0);
    measure(5 ms, e, hp, avg, dens);
    report "noise: high " & real'image(hp) & " %";
    assert hp > 30.0 and hp < 70.0 report "noise" severity failure;
    wr(16, 0); wait for 1 ns;
    assert sd = '0' report "amplifier off with master 0" severity failure;

    ---------------------------------------------------------------- APU v2
    addr <= std_logic_vector(to_unsigned(16#12#, 6)); wait for 1 ns;
    assert rdata = x"A2" report "version byte" severity failure;
    wr(16, 15); wr(17, 16#1F#);                  -- master 15, all five channels
    wr(15, 0); wr(14, 0); wr(15, 0);             -- noise silent
    wr(10, 0); wr(11, 0);                        -- triangle silent
    wr(2, 0); wr(3, 0);                          -- square 0 silent
    -- wave RAM: 16 samples of 15, then 16 of 0 (a square through the wave channel)
    for i in 0 to 7 loop wr(16#20# + i, 16#FF#); end loop;
    for i in 8 to 15 loop wr(16#20# + i, 16#00#); end loop;
    -- wave: P = 48 -> 996.5 Hz, half the time high
    wr(16#14#, 48); wr(16#15#, 0); wr(16#16#, 15); wr(16#17#, 0);
    measure(20 ms, e, hp, avg, dens);
    report "wave: " & integer'image(e) & " cycles in 20 ms, high " & real'image(hp) & " %, level " & real'image(avg);
    assert e >= 19 and e <= 21 report "wave frequency" severity failure;
    assert hp > 48.0 and hp < 52.0 report "wave shape from the wave RAM" severity failure;
    -- level when high: (15 * 15 / 16 = 14) * 15 * 1.125 = 236 (the same scaling as the triangle)
    assert avg > 112.0 and avg < 124.0 report "wave level" severity failure;
    wr(16#16#, 0); wr(16#17#, 0);
    -- attack: square 0 volume 12, attack 2 frames a step -> 0 at the start, 12 after 24 frames
    wr(16#1A#, 16#20#);
    wr(0, 194); wr(1, 0); wr(2, 16#2C#); wr(3, 0);
    wait for 1 us;
    assert level = 0 report "attack starts silent" severity failure;
    for i in 1 to 4 loop tick; end loop;
    wait for 2 ms;
    measure(2 ms, e, hp, avg, dens);
    assert avg > 12.0 and avg < 22.0 report "attack after 4 frames (volume 2: 2 * 15 * 1.125 / 2)" severity failure;
    for i in 1 to 24 loop tick; end loop;
    wait for 2 ms;
    measure(2 ms, e, hp, avg, dens);
    report "attack done: level " & real'image(avg);
    assert avg > 95.0 and avg < 110.0 report "attack reaches the volume (12 * 15 * 1.125 / 2)" severity failure;
    -- sustain: decay from 15 stops at 6
    wr(16#1A#, 16#06#); wr(2, 16#AF#); wr(3, 0);
    for i in 1 to 20 loop tick; end loop;
    wait for 2 ms;
    measure(2 ms, e, hp, avg, dens);
    report "sustain: level " & real'image(avg);
    assert avg > 45.0 and avg < 56.0 report "decay stops at the sustain volume (6 * 15 * 1.125 / 2)" severity failure;
    wr(16#1A#, 0); wr(2, 0); wr(3, 0);
    -- sweep: square 1 at P = 200, period down by P >> 3 every frame
    wr(4, 200); wr(5, 0); wr(6, 16#2F#); wr(7, 0);
    wr(16#19#, 16#8B#);
    tick;
    wait for 1 us;
    wr(16#19#, 0);
    measure(20 ms, e, hp, avg, dens);
    report "sweep: " & integer'image(e) & " cycles in 20 ms";
    -- P 200 -> 175: 1.5625 MHz / (8 * 176) = 1109.7 Hz
    assert e >= 21 and e <= 23 report "sweep changed the period" severity failure;
    -- mix limit: every channel at 15
    wr(4, 10); wr(6, 16#3F#); wr(7, 0);          -- square 1 duty 75 % (mostly high)
    for i in 0 to 15 loop wr(16#20# + i, 16#FF#); end loop;
    wr(16#16#, 15); wr(16#17#, 0);
    wr(0, 10); wr(2, 16#3F#); wr(3, 0);
    measure(1 ms, e, hp, avg, dens);
    assert level <= 1023 and avg > 300.0 report "mix" severity failure;
    -- low-pass: the level stops jumping (square 0 alone at 17.8 kHz, filter 3: 3.9 kHz)
    wr(4, 0); wr(6, 0); wr(7, 0); wr(16#16#, 0); wr(16#17#, 0);
    wr(0, 10); wr(1, 0); wr(2, 16#2F#); wr(3, 0);
    wr(16#13#, 3);
    wait for 2 ms;
    measure(4 ms, e, hp, avg, dens);
    report "low-pass: high " & real'image(hp) & " %, level " & real'image(avg);
    assert hp > 99.0 report "the low-passed level never falls to 0" severity failure;
    assert abs (dens - avg) < 0.01 * 1024.0 report "delta-sigma density (filtered)" severity failure;
    report "TEST PASSED";
    done <= true;
    wait;
  end process;
end architecture;

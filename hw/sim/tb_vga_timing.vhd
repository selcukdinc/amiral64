-- K0: VGA timing checks over a full frame: 800 clocks per line, 96-clock HSYNC, 640 visible pixels,
-- 525 lines per frame, 2-line VSYNC, 480 visible lines.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_vga_timing is end;

architecture sim of tb_vga_timing is
  signal clk : std_logic := '0';
  signal rst : std_logic := '1';
  signal hs, vs, active, ls, fs, vbs : std_logic;
  signal x, y : unsigned(9 downto 0);
  signal done : boolean := false;
begin
  clk <= not clk after 20 ns when not done;
  dut : entity work.vga_timing port map (clk, rst, hs, vs, active, x, y, ls, fs, vbs);

  process
    variable hs_low, act, lines, vs_low_lines, act_lines : integer := 0;
    variable line_had_active, line_vs_low : boolean;
  begin
    wait for 100 ns; wait until rising_edge(clk); rst <= '0';
    wait until rising_edge(clk) and fs = '1';   -- values seen here are those of x = 0, y = 0
    for l in 0 to 524 loop
      hs_low := 0; act := 0; line_had_active := false; line_vs_low := false;
      for c in 0 to 799 loop   -- sample the values of this clock cycle, then advance
        if hs = '0' then hs_low := hs_low + 1; end if;
        if active = '1' then act := act + 1; line_had_active := true; end if;
        if vs = '0' then line_vs_low := true; end if;
        wait until rising_edge(clk);
      end loop;
      assert hs_low = 96 report "HSYNC width " & integer'image(hs_low) severity failure;
      assert act = 0 or act = 640 report "active pixels " & integer'image(act) severity failure;
      if line_had_active then act_lines := act_lines + 1; end if;
      if line_vs_low then vs_low_lines := vs_low_lines + 1; end if;
      lines := lines + 1;
    end loop;
    assert fs = '1' report "frame is not 525 lines" severity failure;
    assert act_lines = 480 report "visible lines " & integer'image(act_lines) severity failure;
    assert vs_low_lines = 2 report "VSYNC lines " & integer'image(vs_low_lines) severity failure;
    report "TEST PASSED";
    done <= true;
    wait;
  end process;
end architecture;

-- K2/K3: the PPU is programmed with the scene from tools/ppu_model.py (palette, 256 tiles, 64 sprite
-- patterns, map, 64 sprites incl. flips, priority and >16 per line, scroll) and one whole frame is
-- compared pixel by pixel with the model; then the collision/overflow flags.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;

entity tb_ppu is end;

architecture sim of tb_ppu is
  signal clk : std_logic := '0';
  signal rst : std_logic := '1';
  signal hs_i, vs_i, act_i, ls, fs, vbs : std_logic;
  signal x, y : unsigned(9 downto 0);
  signal rgb : std_logic_vector(11 downto 0);
  signal hs, vs, nmi_n, irq, vtick : std_logic;
  signal we, re : std_logic := '0';
  signal addr : std_logic_vector(3 downto 0) := x"0";
  signal wdata, rdata : std_logic_vector(7 downto 0);
  signal done : boolean := false;
  type frame_t is array (0 to 239, 0 to 319) of std_logic_vector(11 downto 0);
begin
  clk <= not clk after 20 ns when not done;
  vga : entity work.vga_timing port map (clk, rst, hs_i, vs_i, act_i, x, y, ls, fs, vbs);
  dut : entity work.ppu port map (clk => clk, rst => rst, x => x, y => y, hs_in => hs_i, vs_in => vs_i,
    active_in => act_i, rgb => rgb, hs => hs, vs => vs, we => we, re => re, addr => addr, wdata => wdata,
    rdata => rdata, nmi_n => nmi_n, irq => irq, dma_we => '0', dma_addr => x"00", dma_data => x"00", vblank_tick => vtick);

  process
    file f : text;
    variable l : line;
    variable r : std_logic_vector(3 downto 0);
    variable d : std_logic_vector(7 downto 0);
    variable c : std_logic_vector(11 downto 0);
    variable expect : frame_t;
    type hist_t is array (0 to 3) of unsigned(9 downto 0);
    variable xh, yh : hist_t := (others => (others => '1'));
    variable bad, checked : integer := 0;
    variable px, py : integer;
    variable flags : std_logic_vector(7 downto 0);
  begin
    file_open(f, "ppu_expect.txt", read_mode);
    for j in 0 to 239 loop
      readline(f, l);
      for i in 0 to 319 loop hread(l, c); expect(j, i) := c; end loop;
    end loop;
    file_close(f);
    wait for 100 ns; wait until rising_edge(clk); rst <= '0';
    file_open(f, "ppu_scene.txt", read_mode);
    while not endfile(f) loop
      readline(f, l); hread(l, r); hread(l, d);
      addr <= r; wdata <= d; we <= '1';
      wait until rising_edge(clk);
    end loop;
    file_close(f);
    we <= '0';
    -- clear the flags just before line 0 of the next frame is drawn, then capture that frame
    wait until rising_edge(clk) and y = 522 and x = 0;
    addr <= x"4"; re <= '1'; wait until rising_edge(clk); re <= '0';
    wait until rising_edge(clk) and y = 524 and x = 790;
    loop
      wait until rising_edge(clk);
      xh := x & xh(0 to 2);
      yh := y & yh(0 to 2);
      -- rgb now belongs to the pixel at (xh(3), yh(3))
      if xh(3) < 640 and yh(3) < 480 and xh(3)(0) = '0' and yh(3)(0) = '0' then
        px := to_integer(xh(3)) / 2; py := to_integer(yh(3)) / 2;
        checked := checked + 1;
        if rgb /= expect(py, px) then
          bad := bad + 1;
          if bad <= 8 then
            report "pixel (" & integer'image(px) & "," & integer'image(py) & ") = " & to_hstring(rgb)
                   & ", expected " & to_hstring(expect(py, px)) severity error;
          end if;
        end if;
      end if;
      exit when yh(3) = 480;
    end loop;
    addr <= x"4"; wait for 1 ns; flags := rdata;
    file_open(f, "ppu_flags.txt", read_mode); readline(f, l); hread(l, d); file_close(f);
    report integer'image(checked) & " pixels checked, " & integer'image(bad) & " wrong; STATUS " & to_hstring(flags)
           & " (expected " & to_hstring(d or x"80") & ")";
    assert bad = 0 report "frame differs from the model" severity failure;
    assert checked = 76800 report "not a whole frame" severity failure;
    assert (flags and x"68") = d report "collision/overflow flags" severity failure;
    assert flags(7) = '1' and nmi_n = '0' report "vblank flag and NMI (CTRL bit0 set)" severity failure;
    report "TEST PASSED";
    done <= true;
    wait;
  end process;
end architecture;

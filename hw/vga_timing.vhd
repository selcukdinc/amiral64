-- 640x480 @ 60 Hz VGA timing for a 25 MHz pixel clock (800 x 525 total, negative syncs).
-- x/y count the visible area; the console works at 320x240, so px = x/2 and py = y/2.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vga_timing is
  port (
    clk, rst   : in  std_logic;
    hsync      : out std_logic;
    vsync      : out std_logic;
    active     : out std_logic;
    x          : out unsigned(9 downto 0);   -- 0..799
    y          : out unsigned(9 downto 0);   -- 0..524
    line_start : out std_logic;              -- one cycle at x = 0
    frame_start: out std_logic;              -- one cycle at x = 0, y = 0
    vblank_start: out std_logic              -- one cycle at x = 0, y = 480
  );
end entity;

architecture rtl of vga_timing is
  constant H_VISIBLE : integer := 640;
  constant H_FRONT   : integer := 16;
  constant H_SYNC    : integer := 96;
  constant H_TOTAL   : integer := 800;
  constant V_VISIBLE : integer := 480;
  constant V_FRONT   : integer := 10;
  constant V_SYNC    : integer := 2;
  constant V_TOTAL   : integer := 525;
  signal hc, vc : unsigned(9 downto 0) := (others => '0');
begin
  process (clk)
  begin
    if rising_edge(clk) then
      if rst = '1' then
        hc <= (others => '0');
        vc <= (others => '0');
      elsif hc = H_TOTAL - 1 then
        hc <= (others => '0');
        if vc = V_TOTAL - 1 then vc <= (others => '0'); else vc <= vc + 1; end if;
      else
        hc <= hc + 1;
      end if;
    end if;
  end process;

  x <= hc;
  y <= vc;
  active <= '1' when hc < H_VISIBLE and vc < V_VISIBLE else '0';
  hsync <= '0' when hc >= H_VISIBLE + H_FRONT and hc < H_VISIBLE + H_FRONT + H_SYNC else '1';
  vsync <= '0' when vc >= V_VISIBLE + V_FRONT and vc < V_VISIBLE + V_FRONT + V_SYNC else '1';
  line_start <= '1' when hc = 0 else '0';
  frame_start <= '1' when hc = 0 and vc = 0 else '0';
  vblank_start <= '1' when hc = 0 and vc = V_VISIBLE else '0';
end architecture;

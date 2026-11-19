-- PPU: 320x240 tile + sprite video, shown 2x on 640x480 VGA.
--
-- CPU registers ($8000 + n):
--   0 VADDR_L  1 VADDR_H  2 VDATA (read/write VRAM at VADDR, then VADDR+1)
--   3 CTRL: bit0 NMI at vblank, bit1 background on, bit2 sprites on, bit3 raster IRQ
--   4 STATUS (read): bit7 vblank, bit6 sprite-background hit, bit5 sprite-sprite hit, bit4 raster IRQ,
--     bit3 sprite overflow (>16 on a line). Reading clears bits 7, 6, 5, 3; writing bit4 = 1 clears bit 4.
--   5 SCROLL_X low  6 SCROLL_X high (bit0)  7 SCROLL_Y   9 LINE (read: logical line now shown, 240+ = vblank)
--   10 RASTER: raster IRQ line. A scroll change made in that IRQ applies from line RASTER + 2.
--   (8 OAMDMA is handled by konsol_core.)
-- VRAM (byte addresses):
--   $0000-$1FFF background tiles: 256 x 8 rows x 4 bytes (4 bpp, left pixel in the high nibble)
--   $2000-$3FFF sprite patterns: 64 x 16 rows x 8 bytes
--   $4000-$4FFF tile map 64 x 32: [tile, attr] (attr: bit3-0 palette, bit4 flip x, bit5 flip y)
--   $5000-$51FF palette 256 x [GGGGBBBB, ----RRRR]; colour 0 of every palette is transparent and
--               shows palette entry 0 (the backdrop)
--   $5200-$52FF OAM 64 x [Y, X low, pattern, attr] (attr: bit3-0 palette, bit4 flip x, bit5 flip y,
--               bit6 behind background, bit7 X bit 8). Y >= 240 hides a sprite.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity ppu is
  port (
    clk, rst : in  std_logic;
    -- VGA timing (current counters) and delayed, aligned outputs
    x, y     : in  unsigned(9 downto 0);
    hs_in, vs_in, active_in : in std_logic;
    rgb      : out std_logic_vector(11 downto 0);
    hs, vs   : out std_logic;
    -- CPU port
    we, re   : in  std_logic;
    addr     : in  std_logic_vector(3 downto 0);
    wdata    : in  std_logic_vector(7 downto 0);
    rdata    : out std_logic_vector(7 downto 0);
    nmi_n    : out std_logic;
    irq      : out std_logic;
    -- OAM DMA write port (from konsol_core)
    dma_we   : in  std_logic;
    dma_addr : in  std_logic_vector(7 downto 0);
    dma_data : in  std_logic_vector(7 downto 0);
    vblank_tick : out std_logic
  );
end entity;

architecture rtl of ppu is
  type byte_arr is array (natural range <>) of std_logic_vector(7 downto 0);
  type lane_we_t is array (natural range <>) of std_logic;

  -- CPU-side registers
  signal vaddr : unsigned(15 downto 0) := (others => '0');
  signal ctrl : std_logic_vector(3 downto 0) := (others => '0');
  signal scroll_x : unsigned(8 downto 0) := (others => '0');
  signal scroll_y : unsigned(7 downto 0) := (others => '0');
  signal raster : unsigned(7 downto 0) := (others => '0');
  signal f_vblank, f_sbg, f_sss, f_raster, f_over : std_logic := '0';
  signal set_sbg, set_sss, set_over : std_logic := '0';
  signal read_buf : std_logic_vector(7 downto 0) := (others => '0');
  signal fetch : std_logic_vector(1 downto 0) := "00";

  -- VRAM port B (CPU)
  signal b_tile_addr, b_spr_addr, b_map_addr : std_logic_vector(10 downto 0);
  signal b_pal_addr : std_logic_vector(7 downto 0);
  signal b_oam_addr : std_logic_vector(5 downto 0);
  signal tile_we, spr_we, oam_we : lane_we_t(0 to 3);
  signal map_we, pal_we : lane_we_t(0 to 1);
  signal tile_bq, spr_bq, oam_bq : byte_arr(0 to 3);
  signal map_bq, pal_bq : byte_arr(0 to 1);
  signal oam_bdata : std_logic_vector(7 downto 0);
  signal cpu_vwe : std_logic;
  signal region : integer range 0 to 5;   -- 0 tiles 1 sprites 2 map 3 palette 4 oam 5 none
  signal region_q : integer range 0 to 5;
  signal lane_q : std_logic_vector(1 downto 0);

  -- VRAM port A (renderer / display)
  signal r_tile_addr, r_spr_addr, r_map_addr : std_logic_vector(10 downto 0) := (others => '0');
  signal r_oam_addr : std_logic_vector(5 downto 0) := (others => '0');
  signal tile_aq, spr_aq, oam_aq : byte_arr(0 to 3);
  signal map_aq, pal_aq : byte_arr(0 to 1);
  signal disp_pal_addr : std_logic_vector(7 downto 0);

  -- Line buffers: entry = [9] sprite here, [8] background opaque, [7:0] palette index
  signal lb_a_addr0, lb_a_addr1, lb_b_addr : std_logic_vector(8 downto 0);
  signal lb_q0, lb_q1 : std_logic_vector(9 downto 0);
  signal lb_we0, lb_we1 : std_logic;
  signal r_rd_addr : unsigned(8 downto 0) := (others => '0');
  signal r_we : std_logic := '0';
  signal r_waddr : unsigned(8 downto 0) := (others => '0');
  signal r_wdata : std_logic_vector(9 downto 0) := (others => '0');
  signal r_bank : std_logic := '0';
  signal r_rdq : std_logic_vector(9 downto 0);
  signal disp_bank : std_logic;
  signal disp_q : std_logic_vector(9 downto 0);

  -- Renderer
  type state_t is (IDLE, BG_MAP, BG_MAP_W, BG_PAT, BG_PAT_W, BG_PIX, BG_CLEAR,
                   EV_ADDR, EV_WAIT, EV_CHECK, SP_NEXT, SP_OAM_W, SP_OAM, SP_PAT0_W, SP_PAT1, SP_PIX, SP_DRAIN);
  signal st : state_t := IDLE;
  signal line : unsigned(7 downto 0) := (others => '0');
  signal lsx : unsigned(8 downto 0);
  signal lsy : unsigned(7 downto 0);
  signal lbg, lsp : std_logic;
  signal tcount : integer range 0 to 41;
  signal tcol : unsigned(5 downto 0);
  signal trow, fine_y : unsigned(4 downto 0);
  signal fine_x : unsigned(2 downto 0);
  signal tattr : std_logic_vector(7 downto 0);
  signal tpix : std_logic_vector(31 downto 0);
  signal pcount : integer range 0 to 16;
  signal px_base : integer range -8 to 340;
  signal clr : integer range 0 to 320;
  signal ev_i : integer range 0 to 64;
  type list_t is array (0 to 15) of unsigned(5 downto 0);
  signal list : list_t;
  signal nlist : integer range 0 to 16;
  signal sp_k : integer range -1 to 15;
  signal s_x : unsigned(8 downto 0);
  signal s_attr : std_logic_vector(7 downto 0);
  signal s_row : unsigned(3 downto 0);
  signal s_pix : std_logic_vector(63 downto 0);
  -- sprite pixel pipeline (read line buffer, then write two clocks later)
  signal p1_v, p2_v : std_logic := '0';
  signal p1_px, p2_px : unsigned(8 downto 0);
  signal p1_c, p2_c : std_logic_vector(3 downto 0);

  -- Display pipeline
  signal act_d, hs_d, vs_d : std_logic_vector(2 downto 0) := (others => '0');
  signal start_line : std_logic;
  signal start_num : unsigned(7 downto 0);

  function nib32(w : std_logic_vector(31 downto 0); p : integer) return std_logic_vector is
    variable b : integer := p / 2;   -- byte b = w(8b+7 .. 8b)
  begin
    if p mod 2 = 0 then return w(8 * b + 7 downto 8 * b + 4); else return w(8 * b + 3 downto 8 * b); end if;
  end function;
  function nib64(w : std_logic_vector(63 downto 0); p : integer) return std_logic_vector is
    variable b : integer := p / 2;
  begin
    if p mod 2 = 0 then return w(8 * b + 7 downto 8 * b + 4); else return w(8 * b + 3 downto 8 * b); end if;
  end function;
begin
  ---------------------------------------------------------------- VRAM
  region <= 0 when vaddr(15 downto 13) = "000" else
            1 when vaddr(15 downto 13) = "001" else
            2 when vaddr(15 downto 12) = "0100" else
            3 when vaddr(15 downto 9) = "0101000" else
            4 when vaddr(15 downto 8) = x"52" else 5;
  cpu_vwe <= '1' when we = '1' and addr = x"2" else '0';
  b_tile_addr <= std_logic_vector(vaddr(12 downto 2));
  b_spr_addr <= std_logic_vector(vaddr(12 downto 2));
  b_map_addr <= std_logic_vector(vaddr(11 downto 1));
  b_pal_addr <= std_logic_vector(vaddr(8 downto 1));
  b_oam_addr <= dma_addr(7 downto 2) when dma_we = '1' else std_logic_vector(vaddr(7 downto 2));
  oam_bdata <= dma_data when dma_we = '1' else wdata;

  lanes4 : for i in 0 to 3 generate
    tile_we(i) <= '1' when cpu_vwe = '1' and region = 0 and to_integer(vaddr(1 downto 0)) = i else '0';
    spr_we(i)  <= '1' when cpu_vwe = '1' and region = 1 and to_integer(vaddr(1 downto 0)) = i else '0';
    oam_we(i)  <= '1' when (dma_we = '1' and to_integer(unsigned(dma_addr(1 downto 0))) = i)
                        or (dma_we = '0' and cpu_vwe = '1' and region = 4 and to_integer(vaddr(1 downto 0)) = i) else '0';
    tiles : entity work.bram_dp generic map (11, 8)
      port map (clk, r_tile_addr, tile_aq(i), tile_we(i), b_tile_addr, wdata, tile_bq(i));
    sprs : entity work.bram_dp generic map (11, 8)
      port map (clk, r_spr_addr, spr_aq(i), spr_we(i), b_spr_addr, wdata, spr_bq(i));
    oams : entity work.bram_dp generic map (6, 8)
      port map (clk, r_oam_addr, oam_aq(i), oam_we(i), b_oam_addr, oam_bdata, oam_bq(i));
  end generate;
  lanes2 : for i in 0 to 1 generate
    map_we(i) <= '1' when cpu_vwe = '1' and region = 2 and to_integer(vaddr(0 downto 0)) = i else '0';
    pal_we(i) <= '1' when cpu_vwe = '1' and region = 3 and to_integer(vaddr(0 downto 0)) = i else '0';
    maps : entity work.bram_dp generic map (11, 8)
      port map (clk, r_map_addr, map_aq(i), map_we(i), b_map_addr, wdata, map_bq(i));
    pals : entity work.bram_dp generic map (8, 8)
      port map (clk, disp_pal_addr, pal_aq(i), pal_we(i), b_pal_addr, wdata, pal_bq(i));
  end generate;

  ---------------------------------------------------------------- CPU registers
  process (clk)
  begin
    if rising_edge(clk) then
      -- buffered VDATA reads: the byte at VADDR is fetched two clocks after VADDR changes
      fetch <= fetch(0) & '0';
      if fetch(1) = '1' then
        case region_q is
          when 0 => read_buf <= tile_bq(to_integer(unsigned(lane_q)));
          when 1 => read_buf <= spr_bq(to_integer(unsigned(lane_q)));
          when 2 => read_buf <= map_bq(to_integer(unsigned(lane_q(0 downto 0))));
          when 3 => read_buf <= pal_bq(to_integer(unsigned(lane_q(0 downto 0))));
          when 4 => read_buf <= oam_bq(to_integer(unsigned(lane_q)));
          when others => read_buf <= x"00";
        end case;
      end if;
      region_q <= region;
      lane_q <= std_logic_vector(vaddr(1 downto 0));

      if we = '1' then
        case addr is
          when x"0" => vaddr(7 downto 0) <= unsigned(wdata); fetch(0) <= '1';
          when x"1" => vaddr(15 downto 8) <= unsigned(wdata); fetch(0) <= '1';
          when x"2" => vaddr <= vaddr + 1; fetch(0) <= '1';
          when x"3" => ctrl <= wdata(3 downto 0);
          when x"4" => if wdata(4) = '1' then f_raster <= '0'; end if;
          when x"5" => scroll_x(7 downto 0) <= unsigned(wdata);
          when x"6" => scroll_x(8) <= wdata(0);
          when x"7" => scroll_y <= unsigned(wdata);
          when x"A" => raster <= unsigned(wdata);
          when others => null;
        end case;
      end if;
      if re = '1' and addr = x"2" then vaddr <= vaddr + 1; fetch(0) <= '1'; end if;

      -- flags
      if y = 480 and x = 0 then f_vblank <= '1'; end if;
      if y = 0 and x = 0 then f_vblank <= '0'; end if;
      if set_sbg = '1' then f_sbg <= '1'; end if;
      if set_sss = '1' then f_sss <= '1'; end if;
      if set_over = '1' then f_over <= '1'; end if;
      if x = 0 and y(0) = '0' and y < 480 and y(8 downto 1) = raster then f_raster <= '1'; end if;
      if re = '1' and addr = x"4" then
        f_vblank <= '0'; f_sbg <= '0'; f_sss <= '0'; f_over <= '0';
      end if;
      if rst = '1' then
        ctrl <= (others => '0'); f_vblank <= '0'; f_sbg <= '0'; f_sss <= '0'; f_raster <= '0'; f_over <= '0';
        scroll_x <= (others => '0'); scroll_y <= (others => '0'); vaddr <= (others => '0');
      end if;
    end if;
  end process;

  with addr select rdata <=
    std_logic_vector(vaddr(7 downto 0)) when x"0",
    std_logic_vector(vaddr(15 downto 8)) when x"1",
    read_buf when x"2",
    "0000" & ctrl when x"3",
    f_vblank & f_sbg & f_sss & f_raster & f_over & "000" when x"4",
    std_logic_vector(scroll_x(7 downto 0)) when x"5",
    "0000000" & scroll_x(8) when x"6",
    std_logic_vector(scroll_y) when x"7",
    std_logic_vector(y(8 downto 1)) when x"9",
    std_logic_vector(raster) when x"A",
    x"00" when others;
  nmi_n <= not (f_vblank and ctrl(0));
  irq <= f_raster and ctrl(3);
  vblank_tick <= '1' when y = 480 and x = 0 else '0';

  ---------------------------------------------------------------- line buffers
  disp_bank <= y(1);
  lb_a_addr0 <= std_logic_vector(x(9 downto 1)) when disp_bank = '0' else std_logic_vector(r_rd_addr);
  lb_a_addr1 <= std_logic_vector(x(9 downto 1)) when disp_bank = '1' else std_logic_vector(r_rd_addr);
  lb_b_addr <= std_logic_vector(r_waddr);
  lb_we0 <= r_we and not r_bank;
  lb_we1 <= r_we and r_bank;
  lb0 : entity work.bram_dp generic map (9, 10) port map (clk, lb_a_addr0, lb_q0, lb_we0, lb_b_addr, r_wdata, open);
  lb1 : entity work.bram_dp generic map (9, 10) port map (clk, lb_a_addr1, lb_q1, lb_we1, lb_b_addr, r_wdata, open);
  r_rdq <= lb_q1 when r_bank = '1' else lb_q0;

  ---------------------------------------------------------------- renderer
  -- Line L is drawn into bank L mod 2 while line L-1 is shown (1600 clocks): line 0 at VGA line 523,
  -- line L+1 at VGA line 2L.
  start_line <= '1' when x = 0 and ((y(0) = '0' and y <= 476) or y = 523) else '0';
  start_num <= (others => '0') when y = 523 else y(8 downto 1) + 1;

  process (clk)
    variable c : std_logic_vector(3 downto 0);
    variable px : integer;
    variable dy : signed(9 downto 0);
    variable e : std_logic_vector(9 downto 0);
    variable p : integer range 0 to 15;
  begin
    if rising_edge(clk) then
      r_we <= '0';
      set_sbg <= '0'; set_sss <= '0'; set_over <= '0';
      -- sprite pixel pipeline stage 2: the line buffer entry read for p2_px is in r_rdq now
      if p2_v = '1' then
        e := r_rdq;
        if e(9) = '1' then set_sss <= '1'; end if;
        if e(8) = '1' then set_sbg <= '1'; end if;
        if not (s_attr(6) = '1' and e(8) = '1') then
          r_we <= '1';
          r_waddr <= p2_px;
          r_wdata <= '1' & e(8) & s_attr(3 downto 0) & p2_c;
        end if;
      end if;
      p2_v <= p1_v; p2_px <= p1_px; p2_c <= p1_c;
      p1_v <= '0';

      case st is
        when IDLE =>
          null;

        -- background: one tile = map read, pattern read, 8 pixel writes
        when BG_MAP =>
          r_map_addr <= std_logic_vector(trow(4 downto 0)) & std_logic_vector(tcol);
          st <= BG_MAP_W;
        when BG_MAP_W =>
          st <= BG_PAT;
        when BG_PAT =>
          tattr <= map_aq(1);
          if map_aq(1)(5) = '1' then
            r_tile_addr <= map_aq(0) & std_logic_vector(7 - fine_y(2 downto 0));
          else
            r_tile_addr <= map_aq(0) & std_logic_vector(fine_y(2 downto 0));
          end if;
          st <= BG_PAT_W;
        when BG_PAT_W =>
          pcount <= 0;
          st <= BG_PIX;
        when BG_PIX =>
          if pcount = 0 then
            tpix <= tile_aq(3) & tile_aq(2) & tile_aq(1) & tile_aq(0);
          end if;
          p := pcount;
          if tattr(4) = '1' then p := 7 - pcount; end if;
          if pcount = 0 then
            c := nib32(tile_aq(3) & tile_aq(2) & tile_aq(1) & tile_aq(0), p);
          else
            c := nib32(tpix, p);
          end if;
          px := px_base + pcount;
          if px >= 0 and px < 320 then
            r_we <= '1';
            r_waddr <= to_unsigned(px, 9);
            if c = "0000" then
              r_wdata <= (others => '0');
            else
              r_wdata <= "01" & tattr(3 downto 0) & c;
            end if;
          end if;
          if pcount = 7 then
            if tcount = 40 then
              st <= EV_ADDR; ev_i <= 0; nlist <= 0;
            else
              tcount <= tcount + 1;
              tcol <= tcol + 1;
              px_base <= px_base + 8;
              st <= BG_MAP;
            end if;
          else
            pcount <= pcount + 1;
          end if;
        when BG_CLEAR =>
          r_we <= '1';
          r_waddr <= to_unsigned(clr, 9);
          r_wdata <= (others => '0');
          if clr = 319 then
            st <= EV_ADDR; ev_i <= 0; nlist <= 0;
          else
            clr <= clr + 1;
          end if;

        -- sprite evaluation: up to 16 sprites on this line
        when EV_ADDR =>
          if lsp = '0' or ev_i = 64 then
            sp_k <= nlist - 1;
            st <= SP_NEXT;
          else
            r_oam_addr <= std_logic_vector(to_unsigned(ev_i, 6));
            st <= EV_WAIT;
          end if;
        when EV_WAIT =>
          st <= EV_CHECK;
        when EV_CHECK =>
          dy := signed(resize(line, 10)) - signed(resize(unsigned(oam_aq(0)), 10));
          if unsigned(oam_aq(0)) < 240 and dy >= 0 and dy < 16 then
            if nlist < 16 then
              list(nlist) <= to_unsigned(ev_i, 6);
              nlist <= nlist + 1;
            else
              set_over <= '1';
            end if;
          end if;
          ev_i <= ev_i + 1;
          st <= EV_ADDR;

        -- sprites from the last listed to the first, so lower OAM numbers end up on top
        when SP_NEXT =>
          if sp_k < 0 then
            st <= SP_DRAIN;
          else
            r_oam_addr <= std_logic_vector(list(sp_k));
            st <= SP_OAM_W;
          end if;
        when SP_OAM_W =>
          st <= SP_OAM;
        when SP_OAM =>
          s_attr <= oam_aq(3);
          s_x <= oam_aq(3)(7) & unsigned(oam_aq(1));
          dy := signed(resize(line, 10)) - signed(resize(unsigned(oam_aq(0)), 10));
          if oam_aq(3)(5) = '1' then
            s_row <= 15 - unsigned(dy(3 downto 0));
            r_spr_addr <= oam_aq(2)(5 downto 0) & std_logic_vector(15 - unsigned(dy(3 downto 0))) & '0';
          else
            s_row <= unsigned(dy(3 downto 0));
            r_spr_addr <= oam_aq(2)(5 downto 0) & std_logic_vector(dy(3 downto 0)) & '0';
          end if;
          st <= SP_PAT0_W;
        when SP_PAT0_W =>
          r_spr_addr(0) <= '1';
          st <= SP_PAT1;
        when SP_PAT1 =>
          s_pix(31 downto 0) <= spr_aq(3) & spr_aq(2) & spr_aq(1) & spr_aq(0);   -- bytes 0..3
          pcount <= 0;
          st <= SP_PIX;
        when SP_PIX =>
          if pcount = 0 then
            s_pix(63 downto 32) <= spr_aq(3) & spr_aq(2) & spr_aq(1) & spr_aq(0);   -- bytes 4..7
          end if;
          if pcount < 16 then
            p := pcount;
            if s_attr(4) = '1' then p := 15 - pcount; end if;
            if pcount = 0 then
              c := nib64(spr_aq(3) & spr_aq(2) & spr_aq(1) & spr_aq(0) & s_pix(31 downto 0), p);
            else
              c := nib64(s_pix, p);
            end if;
            px := to_integer(s_x) + pcount;
            if c /= "0000" and px < 320 then
              r_rd_addr <= to_unsigned(px, 9);
              p1_v <= '1'; p1_px <= to_unsigned(px, 9); p1_c <= c;
            end if;
            pcount <= pcount + 1;
          else
            -- let the pipeline finish this sprite before the next one reads the line buffer
            if p1_v = '0' and p2_v = '0' then
              sp_k <= sp_k - 1;
              st <= SP_NEXT;
            end if;
          end if;
        when SP_DRAIN =>
          st <= IDLE;
      end case;

      if start_line = '1' then
        line <= start_num;
        r_bank <= start_num(0);
        lsx <= scroll_x; lsy <= scroll_y; lbg <= ctrl(1); lsp <= ctrl(2);
        p1_v <= '0'; p2_v <= '0';
        if ctrl(1) = '1' then
          -- first visible map pixel: (scroll_x, line + scroll_y)
          trow <= resize((start_num + scroll_y) / 8, 5);
          fine_y <= resize((start_num + scroll_y) mod 8, 5);
          tcol <= scroll_x(8 downto 3);
          fine_x <= scroll_x(2 downto 0);
          px_base <= -to_integer(scroll_x(2 downto 0));
          tcount <= 0;
          st <= BG_MAP;
        else
          clr <= 0;
          st <= BG_CLEAR;
        end if;
      end if;
      if rst = '1' then st <= IDLE; p1_v <= '0'; p2_v <= '0'; end if;
    end if;
  end process;

  ---------------------------------------------------------------- display
  disp_q <= lb_q1 when disp_bank = '1' else lb_q0;
  -- cycle X: line buffer address = x/2; X+1: palette address = entry; X+2: colour -> register
  process (clk)
  begin
    if rising_edge(clk) then
      act_d <= act_d(1 downto 0) & active_in;
      hs_d <= hs_d(1 downto 0) & hs_in;
      vs_d <= vs_d(1 downto 0) & vs_in;
      if act_d(1) = '1' then
        rgb <= pal_aq(1)(3 downto 0) & pal_aq(0);
      else
        rgb <= (others => '0');
      end if;
      hs <= hs_d(1);
      vs <= vs_d(1);
    end if;
  end process;
  disp_pal_addr <= disp_q(7 downto 0);
end architecture;

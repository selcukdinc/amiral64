-- Retro konsol core: everything that runs on the 25 MHz system clock (no pins, no MMCM), so testbenches
-- can drive it directly: T65 CPU, RAM, game RAM, boot ROM, PPU with OAM DMA, I/O (display, timer, switches, system).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.konsol_pkg.all;

entity konsol_core is
  generic (BOOT_ROM : string := "boot.mem");
  port (
    clk, rst   : in  std_logic;
    -- board I/O
    sw         : in  std_logic_vector(15 downto 0);
    buttons    : in  std_logic_vector(4 downto 0);   -- C, U, L, R, D
    leds       : out std_logic_vector(15 downto 0);
    rgb16, rgb17 : out std_logic_vector(2 downto 0);
    digits     : out std_logic_vector(31 downto 0);
    digit_blank, digit_dots : out std_logic_vector(7 downto 0);
    -- VGA
    vga_rgb    : out std_logic_vector(11 downto 0);
    vga_hs, vga_vs : out std_logic;
    -- debug / simulation: writes to $87FF ("test result") and the CPU bus
    dbg_test_we   : out std_logic;
    dbg_test_code : out std_logic_vector(7 downto 0);
    dbg_addr      : out std_logic_vector(15 downto 0);
    dbg_cpu_en    : out std_logic
  );
end entity;

architecture rtl of konsol_core is
  signal cpu_en, cpu_rw_n : std_logic;
  signal cpu_a : std_logic_vector(23 downto 0);
  signal a : std_logic_vector(15 downto 0);
  signal cpu_di, cpu_do : std_logic_vector(7 downto 0);
  signal irq_n, nmi_n : std_logic := '1';
  signal div : integer range 0 to CPU_DIV_SLOW - 1 := 0;
  signal slow : std_logic := '0';

  signal sel_ram, sel_io, sel_game, sel_rom : std_logic;
  signal wr, rd : std_logic;
  signal ram_q, game_q, rom_q, io_q : std_logic_vector(7 downto 0);
  signal ram_we, game_we : std_logic;
  signal game_addr : std_logic_vector(14 downto 0);
  signal io_page : integer range 0 to 15;
  signal io_we, io_re : std_logic_vector(15 downto 0);
  signal disp_q, timer_q, input_q, sys_q : std_logic_vector(7 downto 0);
  signal timer_irq : std_logic;
  signal test_code : std_logic_vector(7 downto 0) := x"00";
  -- video
  signal vx, vy : unsigned(9 downto 0);
  signal v_hs, v_vs, v_act, frame_tick : std_logic;
  signal ppu_q : std_logic_vector(7 downto 0);
  signal ppu_irq : std_logic;
  -- OAM DMA: copies a 256-byte RAM page into OAM while the CPU waits (about 260 clocks)
  signal dma_active : std_logic := '0';
  signal dma_page : std_logic_vector(6 downto 0) := (others => '0');
  signal dma_cnt : unsigned(8 downto 0) := (others => '0');
  signal dma_we : std_logic := '0';
  signal dma_addr, dma_data : std_logic_vector(7 downto 0);
  signal ram_addr : std_logic_vector(14 downto 0);
  signal cpu_en_raw : std_logic;
begin
  -- CPU clock enable: one CPU cycle every CPU_DIV_FAST (or CPU_DIV_SLOW) system clocks. Block RAM reads
  -- take one clock, so the data for the address the CPU put out is ready long before the next enable.
  process (clk)
  begin
    if rising_edge(clk) then
      if rst = '1' then
        div <= 0;
      elsif (slow = '0' and div = CPU_DIV_FAST - 1) or div = CPU_DIV_SLOW - 1 then
        div <= 0;
      else
        div <= div + 1;
      end if;
    end if;
  end process;
  cpu_en_raw <= '1' when (slow = '0' and div = CPU_DIV_FAST - 1) or div = CPU_DIV_SLOW - 1 else '0';
  cpu_en <= cpu_en_raw and not dma_active;

  cpu : entity work.T65
    port map (
      Mode => "00", BCD_en => '1', Res_n => not rst, Enable => cpu_en, Clk => clk, Rdy => '1',
      Abort_n => '1', IRQ_n => irq_n, NMI_n => nmi_n, SO_n => '1', R_W_n => cpu_rw_n,
      Sync => open, EF => open, MF => open, XF => open, ML_n => open, VP_n => open, VDA => open, VPA => open,
      A => cpu_a, DI => cpu_di, DO => cpu_do, Regs => open, DEBUG => open, NMI_ack => open);
  a <= cpu_a(15 downto 0);
  irq_n <= not (timer_irq or ppu_irq);

  -- Address decoding
  sel_ram  <= '1' when a(15) = '0' else '0';
  sel_io   <= '1' when a(15 downto 12) = x"8" else '0';
  sel_game <= '1' when unsigned(a) >= 16#9000# and unsigned(a) < 16#E000# else '0';
  sel_rom  <= '1' when a(15 downto 13) = "111" else '0';
  wr <= cpu_en and not cpu_rw_n;   -- a write happens at the end of the CPU cycle
  rd <= cpu_en and cpu_rw_n;       -- read side effects (FIFO pops, flag clears) happen once

  ram_we  <= wr and sel_ram;
  game_we <= wr and sel_game;
  game_addr <= std_logic_vector(unsigned(a(14 downto 0)) - 16#1000#);

  ram_addr <= dma_page & std_logic_vector(dma_cnt(7 downto 0)) when dma_active = '1' else a(14 downto 0);
  ram : entity work.bram generic map (ADDR_BITS => 15, DEPTH => 32768)
    port map (clk => clk, we => ram_we, addr => ram_addr, wdata => cpu_do, rdata => ram_q);

  process (clk)
  begin
    if rising_edge(clk) then
      dma_we <= '0';
      if dma_active = '1' then
        -- address n goes out in one clock, its byte comes back in the next and is written to OAM n
        if dma_cnt <= 255 then
          dma_cnt <= dma_cnt + 1;
        end if;
        if dma_cnt >= 1 and dma_cnt <= 256 then
          dma_we <= '1';
          dma_addr <= std_logic_vector(resize(dma_cnt - 1, 8));
          dma_data <= ram_q;
        end if;
        if dma_cnt = 256 then dma_active <= '0'; end if;
      elsif io_we(IO_PPU) = '1' and a(3 downto 0) = x"8" then
        dma_active <= '1';
        dma_page <= cpu_do(6 downto 0);
        dma_cnt <= (others => '0');
      end if;
      if rst = '1' then dma_active <= '0'; end if;
    end if;
  end process;
  game : entity work.bram generic map (ADDR_BITS => 15, DEPTH => GAME_RAM_BYTES)
    port map (clk => clk, we => game_we, addr => game_addr, wdata => cpu_do, rdata => game_q);
  rom : entity work.bram generic map (ADDR_BITS => 13, DEPTH => 8192, INIT_FILE => BOOT_ROM)
    port map (clk => clk, we => '0', addr => a(12 downto 0), wdata => x"00", rdata => rom_q);

  -- I/O pages: $8p00-$8pFF, register = low 4 bits (16 registers per block, mirrored)
  io_page <= to_integer(unsigned(a(11 downto 8)));
  process (all)
  begin
    io_we <= (others => '0');
    io_re <= (others => '0');
    if sel_io = '1' then
      io_we(io_page) <= wr;
      io_re(io_page) <= rd;
    end if;
  end process;

  vga : entity work.vga_timing
    port map (clk => clk, rst => rst, hsync => v_hs, vsync => v_vs, active => v_act, x => vx, y => vy,
              line_start => open, frame_start => open, vblank_start => open);

  video : entity work.ppu
    port map (clk => clk, rst => rst, x => vx, y => vy, hs_in => v_hs, vs_in => v_vs, active_in => v_act,
              rgb => vga_rgb, hs => vga_hs, vs => vga_vs, we => io_we(IO_PPU), re => io_re(IO_PPU),
              addr => a(3 downto 0), wdata => cpu_do, rdata => ppu_q, nmi_n => nmi_n, irq => ppu_irq,
              dma_we => dma_we, dma_addr => dma_addr, dma_data => dma_data, vblank_tick => frame_tick);

  disp : entity work.display_io
    port map (clk => clk, rst => rst, we => io_we(IO_DISP), addr => a(3 downto 0), wdata => cpu_do, rdata => disp_q,
              digits => digits, blank => digit_blank, dots => digit_dots, leds => leds, rgb16 => rgb16, rgb17 => rgb17);

  timer : entity work.timer_io
    port map (clk => clk, rst => rst, frame_tick => frame_tick, we => io_we(IO_TIMER), re => io_re(IO_TIMER),
              addr => a(3 downto 0), wdata => cpu_do, rdata => timer_q, irq => timer_irq);

  -- Input page (K1: switches and buttons; controllers and keyboard come in K4)
  --   $8208 SW[7:0]  $8209 SW[15:8]  $820A buttons (bit0 C, 1 U, 2 L, 3 R, 4 D)
  with a(3 downto 0) select input_q <=
    sw(7 downto 0) when x"8", sw(15 downto 8) when x"9", "000" & buttons when x"A", x"00" when others;

  -- System page: $8700 id "K", $8701 version, $8702 CPU speed (bit0: 1 = slow 1.79 MHz), $87FF test result
  process (clk)
  begin
    if rising_edge(clk) then
      dbg_test_we <= '0';
      if io_we(IO_SYS) = '1' then
        if a(7 downto 0) = x"02" then slow <= cpu_do(0); end if;
        if a(7 downto 0) = x"FF" then test_code <= cpu_do; dbg_test_we <= '1'; end if;
      end if;
      if rst = '1' then slow <= '0'; end if;
    end if;
  end process;
  with a(7 downto 0) select sys_q <=
    x"4B" when x"00", KONSOL_VERSION when x"01", "0000000" & slow when x"02", test_code when x"FF", x"00" when others;

  with io_page select io_q <=
    ppu_q when IO_PPU, input_q when IO_INPUT, timer_q when IO_TIMER, disp_q when IO_DISP, sys_q when IO_SYS, x"00" when others;

  cpu_di <= ram_q when sel_ram = '1' else
            game_q when sel_game = '1' else
            rom_q when sel_rom = '1' else
            io_q;

  dbg_test_code <= test_code;
  dbg_addr <= a;
  dbg_cpu_en <= cpu_en;
end architecture;

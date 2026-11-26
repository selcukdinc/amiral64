-- Retro konsol core: everything that runs on the 25 MHz system clock (no pins, no MMCM), so testbenches
-- can drive it directly: T65 CPU, RAM, game RAM, boot ROM, PPU with OAM DMA, I/O (display, timer, switches, system).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.konsol_pkg.all;

entity konsol_core is
  generic (
    BOOT_ROM  : string := "boot.mem";
    CART_ROM  : string := "cart_";      -- cartridge image banks cart_00.mem .. (tools/mkcart.py)
    CART_BANKS : integer := 32;         -- 32 x 4 KB = 128 KB
    FLASH_BASE : integer := 16#400000#; -- flash cartridge starts after the bitstream
    FLASH_HALF : integer := 2           -- SPI clock = 25 MHz / (2 * FLASH_HALF) = 6.25 MHz
  );
  port (
    clk, rst   : in  std_logic;
    -- board I/O
    sw         : in  std_logic_vector(15 downto 0);
    buttons    : in  std_logic_vector(4 downto 0);   -- C, U, L, R, D
    leds       : out std_logic_vector(15 downto 0);
    rgb16, rgb17 : out std_logic_vector(2 downto 0);
    digits     : out std_logic_vector(31 downto 0);
    digit_blank, digit_dots : out std_logic_vector(7 downto 0);
    ps2_clk, ps2_data : in std_logic;
    nes_latch, nes_clk : out std_logic;
    nes_data   : in  std_logic_vector(3 downto 0);
    uart_txd   : out std_logic;     -- to the PC
    uart_rxd   : in  std_logic;     -- from the PC
    aud_pwm, aud_sd : out std_logic;
    flash_sck, flash_cs_n, flash_mosi : out std_logic;   -- SCK goes to STARTUPE2 in the top level
    flash_miso : in std_logic;
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
  signal uart_q : std_logic_vector(7 downto 0);
  signal u_busy, u_avail, u_ovr : std_logic;
  signal u_rxd : std_logic_vector(7 downto 0);
  signal apu_q : std_logic_vector(7 downto 0);
  -- cartridge port: $8710-$8712 address, $8713 data (read, then address + 1), $8714 bit0 data ready
  signal cart_addr : unsigned(23 downto 0) := (others => '0');
  signal cart_q, bram_cart_q, flash_q : std_logic_vector(7 downto 0);
  signal cart_src : std_logic := '0';     -- $8715 bit0: 0 = BRAM cartridge, 1 = QSPI flash
  signal flash_start, flash_next, flash_ready, cart_ready : std_logic;
  signal next_addr : unsigned(23 downto 0);
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

  inputs : entity work.input_io
    port map (clk => clk, rst => rst, frame_tick => frame_tick, we => io_we(IO_INPUT), re => io_re(IO_INPUT),
              addr => a(5 downto 0), rdata => input_q, sw => sw, buttons => buttons,
              ps2_clk => ps2_clk, ps2_data => ps2_data, nes_latch => nes_latch, nes_clk => nes_clk, nes_data => nes_data);

  sound : entity work.apu
    port map (clk => clk, rst => rst, frame_tick => frame_tick, we => io_we(IO_APU), addr => a(4 downto 0),
              wdata => cpu_do, rdata => apu_q, level => open, aud_pwm => aud_pwm, aud_sd => aud_sd, pwm_bit => open);

  -- UART page: $8500 data (write = send, read = next received byte), $8501 bit0 received, bit1 sending, bit2 overrun
  serial : entity work.uart
    port map (clk => clk, rst => rst, tx_start => io_we(IO_UART) and not a(0), tx_data => cpu_do, tx_busy => u_busy,
              txd => uart_txd, rxd => uart_rxd, rx_pop => io_re(IO_UART) and not a(0) and not a(1) and not a(2) and not a(3),
              rx_avail => u_avail, rx_data => u_rxd, rx_overrun => u_ovr);
  uart_q <= u_rxd when a(3 downto 0) = x"0" else "00000" & u_ovr & u_busy & u_avail;

  cart : entity work.bram_banked generic map (BANKS => CART_BANKS, INIT_PREFIX => CART_ROM)
    port map (clk => clk, addr => std_logic_vector(cart_addr), rdata => bram_cart_q);

  -- the address the CPU is writing (for restarting the flash read)
  process (all)
  begin
    next_addr <= cart_addr;
    case a(7 downto 0) is
      when x"10" => next_addr(7 downto 0) <= unsigned(cpu_do);
      when x"11" => next_addr(15 downto 8) <= unsigned(cpu_do);
      when x"12" => next_addr(23 downto 16) <= unsigned(cpu_do);
      when others => null;
    end case;
  end process;
  flash_start <= '1' when io_we(IO_SYS) = '1' and cart_src = '1'
                          and (a(7 downto 0) = x"10" or a(7 downto 0) = x"11" or a(7 downto 0) = x"12") else
                 '1' when io_we(IO_SYS) = '1' and a(7 downto 0) = x"15" and cpu_do(0) = '1' else '0';
  flash_next <= '1' when io_re(IO_SYS) = '1' and a(7 downto 0) = x"13" and cart_src = '1' else '0';
  flash : entity work.spi_flash_reader generic map (HALF => FLASH_HALF)
    port map (clk => clk, rst => rst, start => flash_start,
              addr => std_logic_vector(next_addr + to_unsigned(FLASH_BASE, 24)),
              next_b => flash_next, ready => flash_ready, data => flash_q,
              sck => flash_sck, cs_n => flash_cs_n, mosi => flash_mosi, miso => flash_miso);
  cart_q <= flash_q when cart_src = '1' else bram_cart_q;
  cart_ready <= flash_ready when cart_src = '1' else '1';

  -- System page: $8700 id "K", $8701 version, $8702 CPU speed (bit0: 1 = slow 1.79 MHz), $87FF test result,
  -- $8710-$8714 cartridge port
  process (clk)
  begin
    if rising_edge(clk) then
      dbg_test_we <= '0';
      if io_we(IO_SYS) = '1' then
        if a(7 downto 0) = x"02" then slow <= cpu_do(0); end if;
        if a(7 downto 0) = x"FF" then test_code <= cpu_do; dbg_test_we <= '1'; end if;
        if a(7 downto 0) = x"10" then cart_addr(7 downto 0) <= unsigned(cpu_do); end if;
        if a(7 downto 0) = x"11" then cart_addr(15 downto 8) <= unsigned(cpu_do); end if;
        if a(7 downto 0) = x"12" then cart_addr(23 downto 16) <= unsigned(cpu_do); end if;
        if a(7 downto 0) = x"15" then cart_src <= cpu_do(0); end if;
      end if;
      if io_re(IO_SYS) = '1' and a(7 downto 0) = x"13" then cart_addr <= cart_addr + 1; end if;
      if rst = '1' then slow <= '0'; cart_src <= '0'; end if;
    end if;
  end process;
  with a(7 downto 0) select sys_q <=
    x"4B" when x"00", KONSOL_VERSION when x"01", "0000000" & slow when x"02", test_code when x"FF",
    std_logic_vector(cart_addr(7 downto 0)) when x"10", std_logic_vector(cart_addr(15 downto 8)) when x"11",
    std_logic_vector(cart_addr(23 downto 16)) when x"12", cart_q when x"13", "0000000" & cart_ready when x"14",
    "0000000" & cart_src when x"15", x"00" when others;

  with io_page select io_q <=
    ppu_q when IO_PPU, apu_q when IO_APU, input_q when IO_INPUT, uart_q when IO_UART, timer_q when IO_TIMER, disp_q when IO_DISP, sys_q when IO_SYS, x"00" when others;

  cpu_di <= ram_q when sel_ram = '1' else
            game_q when sel_game = '1' else
            rom_q when sel_rom = '1' else
            io_q;

  dbg_test_code <= test_code;
  dbg_addr <= a;
  dbg_cpu_en <= cpu_en;
end architecture;

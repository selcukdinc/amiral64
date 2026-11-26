-- I/O page $82: game pads, keyboard, switches and buttons.
--   $8200-$8203 pads 1-4: bit7 A, 6 B, 5 Select, 4 Start, 3 Up, 2 Down, 1 Left, 0 Right (1 = pressed).
--               Pad 1 also gets the board buttons and keyboard player 1, pad 2 keyboard player 2.
--   $8204 next PS/2 scan code byte (reading pops)   $8205 bit0 byte available, bit1 FIFO overflowed
--   $8208/$8209 switches   $820A board buttons (bit0 C, 1 U, 2 L, 3 R, 4 D)
--   $8210-$822F key table: bit k of byte $8210 + k/8 = key k is down (scan code set 2; E0 keys at code+$80)
-- NES/SNES pads on Pmod JA are read once per frame (LATCH, then 8 CLK pulses; DATA is active low).
-- Keyboard players: P1 = W A S D, A = Left Shift, B = Left Ctrl, Select = Tab, Start = Space;
--                   P2 = arrows, A = Right Shift, B = Right Ctrl, Select = Backspace, Start = Enter
--                   (so on the keyboard Shift is always A and Ctrl is always B).
-- The Nexys 4 DDR's PIC24 firmware sends Left/Up/Down arrows without their E0 prefix (6B, 75, 72; only Right
-- arrow arrives as E0 74), so player 2's directions accept both forms (the plain codes are keypad 4 8 2 6 too),
-- and the release of either form clears both. A lone E0/F0 prefix expires after 10 ms instead of shifting the
-- next key to the wrong table entry (which left keys stuck down), and the keyboard's self-test byte AA (sent
-- when it is plugged in or reset) clears the whole table.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity input_io is
  generic (CLK_HZ : integer := 25_000_000);
  port (
    clk, rst   : in  std_logic;
    frame_tick : in  std_logic;
    we, re     : in  std_logic;
    addr       : in  std_logic_vector(5 downto 0);
    rdata      : out std_logic_vector(7 downto 0);
    sw         : in  std_logic_vector(15 downto 0);
    buttons    : in  std_logic_vector(4 downto 0);   -- C, U, L, R, D
    ps2_clk, ps2_data : in std_logic;
    nes_latch, nes_clk : out std_logic;
    nes_data   : in  std_logic_vector(3 downto 0)
  );
end entity;

architecture rtl of input_io is
  signal kv : std_logic;
  signal kc : std_logic_vector(7 downto 0);
  signal keys : std_logic_vector(255 downto 0) := (others => '0');
  signal ext, brk : std_logic := '0';
  type fifo_t is array (0 to 15) of std_logic_vector(7 downto 0);
  signal fifo : fifo_t;
  signal wp, rp : unsigned(3 downto 0) := (others => '0');
  signal count : integer range 0 to 16 := 0;
  signal ovr : std_logic := '0';
  signal pop : std_logic;
  constant PFX_TIMEOUT : integer := CLK_HZ / 100;          -- 10 ms
  signal pfx_t : integer range 0 to PFX_TIMEOUT := 0;

  -- NES reader: 6 us half periods
  constant HALF : integer := CLK_HZ / 166_667;
  signal nes_t : integer range 0 to HALF := 0;
  signal nes_step : integer range 0 to 18 := 18;
  type pads_t is array (0 to 3) of std_logic_vector(7 downto 0);
  signal shreg, pads : pads_t := (others => (others => '0'));
  signal btn_q : std_logic_vector(4 downto 0) := (others => '0');
  signal kp1, kp2 : std_logic_vector(7 downto 0);
  signal p1, p2 : std_logic_vector(7 downto 0);
  signal nsync : std_logic_vector(3 downto 0) := (others => '1');

  function k(t : std_logic_vector(255 downto 0); code : integer) return std_logic is
  begin
    return t(code);
  end function;
begin
  ps2 : entity work.ps2_rx generic map (CLK_HZ) port map (clk, rst, ps2_clk, ps2_data, kv, kc);

  pop <= '1' when re = '1' and addr = "000100" else '0';
  process (clk)
    variable idx : integer range 0 to 255;
  begin
    if rising_edge(clk) then
      if kv = '1' then
        -- scan code FIFO
        if count < 16 then
          fifo(to_integer(wp)) <= kc; wp <= wp + 1;
        else
          ovr <= '1';
        end if;
        -- key table
        pfx_t <= 0;
        if kc = x"E0" then
          ext <= '1';
        elsif kc = x"F0" then
          brk <= '1';
        elsif kc = x"AA" then
          keys <= (others => '0');      -- keyboard self-test passed: (re)plugged, nothing is held
          ext <= '0'; brk <= '0';
        else
          idx := to_integer(unsigned(kc(6 downto 0)));
          if ext = '1' or kc(7) = '1' then idx := idx + 128; end if;
          keys(idx) <= not brk;
          -- arrows: a release clears the E0 and the plain entry (the PIC24 mixes the two forms)
          if brk = '1' and (kc = x"6B" or kc = x"72" or kc = x"74" or kc = x"75") then
            keys(to_integer(unsigned(kc(6 downto 0)))) <= '0';
            keys(128 + to_integer(unsigned(kc(6 downto 0)))) <= '0';
          end if;
          ext <= '0'; brk <= '0';
        end if;
      elsif ext = '1' or brk = '1' then
        if pfx_t = PFX_TIMEOUT then
          ext <= '0'; brk <= '0'; pfx_t <= 0;
        else
          pfx_t <= pfx_t + 1;
        end if;
      end if;
      if pop = '1' and count > 0 then rp <= rp + 1; end if;
      if kv = '1' and count < 16 and not (pop = '1' and count > 0) then
        count <= count + 1;
      elsif not (kv = '1' and count < 16) and pop = '1' and count > 0 then
        count <= count - 1;
      end if;
      if re = '1' and addr = "000101" then ovr <= '0'; end if;
      if frame_tick = '1' then btn_q <= buttons; end if;
      if rst = '1' then
        keys <= (others => '0'); ext <= '0'; brk <= '0'; pfx_t <= 0; wp <= (others => '0'); rp <= (others => '0'); count <= 0; ovr <= '0';
      end if;
    end if;
  end process;

  -- NES/SNES pads: at each frame, LATCH for one step, then 8 clock pulses; a bit is read before each pulse
  process (clk)
  begin
    if rising_edge(clk) then
      nsync <= nes_data;
      if frame_tick = '1' and nes_step = 18 then
        nes_step <= 0; nes_t <= 0;
      elsif nes_step < 18 then
        if nes_t = HALF then
          nes_t <= 0;
          -- step 0: latch high; odd steps 1,3..15: read bit, clock low; even steps 2..16: clock high
          if nes_step mod 2 = 1 then
            for i in 0 to 3 loop shreg(i) <= shreg(i)(6 downto 0) & not nsync(i); end loop;
          end if;
          if nes_step = 16 then
            for i in 0 to 3 loop pads(i) <= shreg(i); end loop;
          end if;
          nes_step <= nes_step + 1;
        else
          nes_t <= nes_t + 1;
        end if;
      end if;
      if rst = '1' then nes_step <= 18; pads <= (others => (others => '0')); end if;
    end if;
  end process;
  nes_latch <= '1' when nes_step = 0 else '0';
  nes_clk <= '0' when nes_step mod 2 = 1 and nes_step < 17 else '1';

  -- keyboard players (bit order A B Sel Start Up Down Left Right)
  kp1 <= k(keys, 16#12#) & k(keys, 16#14#) & k(keys, 16#0D#) & k(keys, 16#29#)
       & k(keys, 16#1D#) & k(keys, 16#1B#) & k(keys, 16#1C#) & k(keys, 16#23#);
  kp2 <= k(keys, 16#59#) & k(keys, 128 + 16#14#) & k(keys, 16#66#) & k(keys, 16#5A#)
       & (k(keys, 128 + 16#75#) or k(keys, 16#75#)) & (k(keys, 128 + 16#72#) or k(keys, 16#72#))
       & (k(keys, 128 + 16#6B#) or k(keys, 16#6B#)) & (k(keys, 128 + 16#74#) or k(keys, 16#74#));
  -- board buttons: C = A, U/D/L/R = directions
  p1 <= pads(0) or kp1 or (btn_q(4) & "000" & btn_q(3) & btn_q(0) & btn_q(2) & btn_q(1));
  p2 <= pads(1) or kp2;

  process (all)
    variable a : integer range 0 to 63;
  begin
    a := to_integer(unsigned(addr));
    rdata <= x"00";
    case a is
      when 0 => rdata <= p1;
      when 1 => rdata <= p2;
      when 2 => rdata <= pads(2);
      when 3 => rdata <= pads(3);
      when 4 => rdata <= fifo(to_integer(rp));
      when 5 =>
        rdata <= "000000" & ovr & '0';
        if count > 0 then rdata(0) <= '1'; end if;
      when 8 => rdata <= sw(7 downto 0);
      when 9 => rdata <= sw(15 downto 8);
      when 10 => rdata <= "000" & btn_q(0) & btn_q(1) & btn_q(2) & btn_q(3) & btn_q(4);
      when 16 to 47 =>
        for i in 0 to 7 loop rdata(i) <= keys((a - 16) * 8 + i); end loop;
      when others => null;
    end case;
  end process;
end architecture;

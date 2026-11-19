-- APU: 2 square, 1 triangle, 1 noise channel, mixed and sent to the mono audio output through a
-- first-order delta-sigma modulator at 25 MHz (AUD_PWM is open drain: 0 or 'Z').
--
-- Registers ($8100 + 4 * channel; channel 0, 1 square, 2 triangle, 3 noise):
--   +0 period low   +1 period high (bits 3..0)   timer clock 1.5625 MHz:
--        square f = 1.5625 MHz / (8 * (P + 1)), triangle f = 1.5625 MHz / (32 * (P + 1)),
--        noise: one LFSR step every P + 1 ticks
--   +2 bit3-0 volume, bit5-4 square duty (12.5 / 25 / 50 / 75 %), bit6 noise short mode, bit7 decay on
--   +3 bit3-0 decay speed (volume - 1 every speed + 1 frames), bit7-4 length (n * 4 frames, 0 = endless);
--      writing +3 starts the note (volume and length reload)
--   $8110 master volume (bits 3..0, 0 also turns the amplifier off)   $8111 channel enable mask (bits 3..0)
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity apu is
  port (
    clk, rst   : in  std_logic;
    frame_tick : in  std_logic;
    we         : in  std_logic;
    addr       : in  std_logic_vector(4 downto 0);
    wdata      : in  std_logic_vector(7 downto 0);
    rdata      : out std_logic_vector(7 downto 0);
    level      : out unsigned(9 downto 0);   -- mixed output (for tests)
    aud_pwm    : out std_logic;              -- '0' or 'Z'
    aud_sd     : out std_logic;
    pwm_bit    : out std_logic               -- the modulator output (for tests)
  );
end entity;

architecture rtl of apu is
  type u12_arr is array (0 to 3) of unsigned(11 downto 0);
  type u4_arr is array (0 to 3) of unsigned(3 downto 0);
  type slv8_arr is array (0 to 3) of std_logic_vector(7 downto 0);
  signal period, timer : u12_arr := (others => (others => '0'));
  signal ctl, env : slv8_arr := (others => (others => '0'));
  signal vol : u4_arr := (others => (others => '0'));
  signal outv : u4_arr := (others => (others => '0'));
  signal step : u4_arr := (others => (others => '0'));       -- square: 0..7 step
  signal tri : unsigned(4 downto 0) := (others => '0');       -- triangle 0..31
  signal lfsr : std_logic_vector(14 downto 0) := (0 => '1', others => '0');
  type int_arr is array (0 to 3) of integer range 0 to 15;
  signal decay_cnt : int_arr := (others => 0);
  type len_arr is array (0 to 3) of integer range 0 to 63;
  signal len : len_arr := (others => 0);
  signal active : std_logic_vector(3 downto 0) := (others => '0');
  signal master : unsigned(3 downto 0) := (others => '0');
  signal enable : std_logic_vector(3 downto 0) := (others => '0');
  signal pre : unsigned(3 downto 0) := (others => '0');
  signal mix : unsigned(9 downto 0) := (others => '0');
  -- delta-sigma
  signal i1 : signed(17 downto 0) := (others => '0');
  signal ds : std_logic := '0';
begin
  process (clk)
    variable ch : integer range 0 to 7;
    variable r : integer range 0 to 3;
    variable duty_hi : boolean;
    variable t : unsigned(3 downto 0);
    variable sum : unsigned(7 downto 0);
    variable prod : unsigned(11 downto 0);
    variable v1 : signed(17 downto 0);
  begin
    if rising_edge(clk) then
      ch := to_integer(unsigned(addr(4 downto 2)));
      r := to_integer(unsigned(addr(1 downto 0)));
      if we = '1' then
        if ch < 4 then
          case r is
            when 0 => period(ch)(7 downto 0) <= unsigned(wdata);
            when 1 => period(ch)(11 downto 8) <= unsigned(wdata(3 downto 0));
            when 2 => ctl(ch) <= wdata;
            when others =>
              env(ch) <= wdata;
              vol(ch) <= unsigned(ctl(ch)(3 downto 0));
              decay_cnt(ch) <= 0;
              len(ch) <= to_integer(unsigned(wdata(7 downto 4))) * 4;
              active(ch) <= '1';
          end case;
        elsif addr = "10000" then
          master <= unsigned(wdata(3 downto 0));
        elsif addr = "10001" then
          enable <= wdata(3 downto 0);
        end if;
      end if;

      -- channel timers at 25 MHz / 16
      pre <= pre + 1;
      if pre = 15 then
        for c in 0 to 3 loop
          if timer(c) = 0 then
            timer(c) <= period(c);
            case c is
              when 0 | 1 => step(c) <= (step(c) + 1) and "0111";
              when 2 => tri <= tri + 1;
              when others =>
                if ctl(3)(6) = '1' then
                  lfsr <= lfsr(13 downto 0) & (lfsr(0) xor lfsr(6));
                else
                  lfsr <= lfsr(13 downto 0) & (lfsr(0) xor lfsr(1));
                end if;
            end case;
          else
            timer(c) <= timer(c) - 1;
          end if;
        end loop;
      end if;

      -- envelopes and lengths at 60 Hz
      if frame_tick = '1' then
        for c in 0 to 3 loop
          if ctl(c)(7) = '1' and vol(c) /= 0 then
            if decay_cnt(c) = to_integer(unsigned(env(c)(3 downto 0))) then
              decay_cnt(c) <= 0;
              vol(c) <= vol(c) - 1;
            else
              decay_cnt(c) <= decay_cnt(c) + 1;
            end if;
          end if;
          if len(c) = 1 then active(c) <= '0'; end if;
          if len(c) /= 0 then len(c) <= len(c) - 1; end if;
        end loop;
      end if;

      -- channel outputs 0..15
      for c in 0 to 1 loop
        case ctl(c)(5 downto 4) is
          when "00" => duty_hi := step(c) = 7;
          when "01" => duty_hi := step(c) >= 6;
          when "10" => duty_hi := step(c) >= 4;
          when others => duty_hi := step(c) >= 2;
        end case;
        if duty_hi and active(c) = '1' and enable(c) = '1' then outv(c) <= vol(c); else outv(c) <= (others => '0'); end if;
      end loop;
      if tri(4) = '0' then t := tri(3 downto 0); else t := 15 - tri(3 downto 0); end if;
      if active(2) = '1' and enable(2) = '1' then
        prod := resize(t * vol(2), 12);
        outv(2) <= prod(7 downto 4) + ("000" & prod(3));   -- t * vol / 15 (rounded)
      else
        outv(2) <= (others => '0');
      end if;
      if lfsr(0) = '1' and active(3) = '1' and enable(3) = '1' then outv(3) <= vol(3); else outv(3) <= (others => '0'); end if;

      -- mix: (sum of 4 channels, 0..60) * master (0..15) -> 0..900, scaled to 0..1023 (x 1.125)
      sum := resize(outv(0), 8) + outv(1) + outv(2) + outv(3);
      prod := sum * master;
      mix <= resize(prod, 10) + resize(prod(11 downto 3), 10);

      -- first-order delta-sigma: the carry of an accumulator; its density is exactly mix / 1024
      v1 := i1 + signed(resize(mix, 18));
      if v1 >= 1024 then
        i1 <= v1 - 1024;
        ds <= '1';
      else
        i1 <= v1;
        ds <= '0';
      end if;

      if rst = '1' then
        active <= (others => '0'); master <= (others => '0'); enable <= (others => '0');
        i1 <= (others => '0'); ds <= '0';
      end if;
    end if;
  end process;

  level <= mix;
  pwm_bit <= ds;
  aud_pwm <= '0' when ds = '0' else 'Z';
  aud_sd <= '1' when master /= 0 else '0';
  rdata <= "0000" & active;
end architecture;

-- APU v2: 2 square, 1 triangle, 1 noise and 1 wave channel, mixed and sent to the mono audio output
-- through a first-order delta-sigma modulator at 25 MHz (AUD_PWM is open drain: 0 or 'Z').
-- Everything of the first APU stays where it was; a program that only knows v1 sounds the same.
--
-- Registers ($8100 + 4 * channel; channel 0, 1 square, 2 triangle, 3 noise; channel 4 wave at $8114):
--   +0 period low   +1 period high (bits 3..0)   timer clock 1.5625 MHz:
--        square f = 1.5625 MHz / (8 * (P + 1)), triangle and wave f = 1.5625 MHz / (32 * (P + 1)),
--        noise: one LFSR step every P + 1 ticks
--   +2 bit3-0 volume, bit5-4 square duty (12.5 / 25 / 50 / 75 %), bit6 noise short mode, bit7 decay on
--   +3 bit3-0 decay speed (volume - 1 every speed + 1 frames), bit7-4 length (n * 4 frames, 0 = endless);
--      writing +3 starts the note (volume and length reload)
--   $8110 master volume (bits 3..0, 0 also turns the amplifier off)   $8111 channel enable mask (bits 4..0)
--   $8112 read: version $A2 (a v1 APU answers the channel activity, bits 3..0)
--   $8113 output low-pass (bits 1..0): 0 off, 1..3 = y += (x - y) / 2^(n + 3) at 1.5625 MHz (15.5 / 7.8 / 3.9 kHz)
--   $8118, $8119 sweep of square 0, 1: bit7 on, bit6-4 every n + 1 frames, bit3 1 = period down (pitch up),
--        bit2-0 shift: period +- period >> shift
--   $811A..$811E attack / sustain of channels 0..4: bit7-4 attack (0 = at once, else volume + 1 every
--        n frames from 0 up to the control byte's volume), bit3-0 sustain (the decay stops at this volume)
--   $8120..$812F wave RAM: 32 samples of 4 bits, high nibble first
-- Mix: (sum of the channels, 0..75) * master * 1.125, at most 1023.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity apu is
  port (
    clk, rst   : in  std_logic;
    frame_tick : in  std_logic;
    we         : in  std_logic;
    addr       : in  std_logic_vector(5 downto 0);
    wdata      : in  std_logic_vector(7 downto 0);
    rdata      : out std_logic_vector(7 downto 0);
    level      : out unsigned(9 downto 0);   -- mixed output (for tests)
    aud_pwm    : out std_logic;              -- '0' or 'Z'
    aud_sd     : out std_logic;
    pwm_bit    : out std_logic               -- the modulator output (for tests)
  );
end entity;

architecture rtl of apu is
  constant NCH : integer := 5;
  type u12_arr is array (0 to NCH - 1) of unsigned(11 downto 0);
  type u4_arr is array (0 to NCH - 1) of unsigned(3 downto 0);
  type slv8_arr is array (0 to NCH - 1) of std_logic_vector(7 downto 0);
  type int_arr is array (0 to NCH - 1) of integer range 0 to 15;
  type len_arr is array (0 to NCH - 1) of integer range 0 to 63;
  type wave_t is array (0 to 15) of std_logic_vector(7 downto 0);
  signal period, timer : u12_arr := (others => (others => '0'));
  signal ctl, env, adsr : slv8_arr := (others => (others => '0'));
  signal vol, outv : u4_arr := (others => (others => '0'));
  signal step : u4_arr := (others => (others => '0'));       -- square: 0..7 step
  signal tri : unsigned(4 downto 0) := (others => '0');       -- triangle 0..31
  signal wpos : unsigned(4 downto 0) := (others => '0');      -- wave sample 0..31
  signal wave : wave_t := (others => (others => '0'));
  signal lfsr : std_logic_vector(14 downto 0) := (0 => '1', others => '0');
  signal decay_cnt, atk_cnt : int_arr := (others => 0);
  signal len : len_arr := (others => 0);
  signal active, attacking : std_logic_vector(NCH - 1 downto 0) := (others => '0');
  signal master : unsigned(3 downto 0) := (others => '0');
  signal enable : std_logic_vector(NCH - 1 downto 0) := (others => '0');
  signal filt : unsigned(1 downto 0) := (others => '0');
  type sw_arr is array (0 to 1) of std_logic_vector(7 downto 0);
  type swc_arr is array (0 to 1) of integer range 0 to 7;
  signal sweep : sw_arr := (others => (others => '0'));
  signal sweep_cnt : swc_arr := (others => 0);
  signal pre : unsigned(3 downto 0) := (others => '0');
  signal mix : unsigned(9 downto 0) := (others => '0');
  signal lp : unsigned(17 downto 0) := (others => '0');       -- low-passed level, 10.8 fixed point
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
    variable m : unsigned(12 downto 0);
    variable v1 : signed(17 downto 0);
    variable p, d : unsigned(12 downto 0);
    variable smp : unsigned(3 downto 0);
    variable x18 : unsigned(17 downto 0);
    variable diff, nv : signed(18 downto 0);
    variable wch : integer range 0 to NCH;
  begin
    if rising_edge(clk) then
      r := to_integer(unsigned(addr(1 downto 0)));
      wch := NCH;                                   -- channel of a +0..+3 write, NCH = none
      if addr(5 downto 4) = "00" then
        wch := to_integer(unsigned(addr(3 downto 2)));
      elsif addr(5 downto 2) = "0101" then
        wch := 4;
      end if;
      if we = '1' then
        if wch < NCH then
          case r is
            when 0 => period(wch)(7 downto 0) <= unsigned(wdata);
            when 1 => period(wch)(11 downto 8) <= unsigned(wdata(3 downto 0));
            when 2 => ctl(wch) <= wdata;
            when others =>
              env(wch) <= wdata;
              decay_cnt(wch) <= 0;
              atk_cnt(wch) <= 0;
              len(wch) <= to_integer(unsigned(wdata(7 downto 4))) * 4;
              active(wch) <= '1';
              if adsr(wch)(7 downto 4) /= "0000" then
                vol(wch) <= (others => '0');
                attacking(wch) <= '1';
              else
                vol(wch) <= unsigned(ctl(wch)(3 downto 0));
                attacking(wch) <= '0';
              end if;
          end case;
        elsif addr = "010000" then
          master <= unsigned(wdata(3 downto 0));
        elsif addr = "010001" then
          enable <= wdata(NCH - 1 downto 0);
        elsif addr = "010011" then
          filt <= unsigned(wdata(1 downto 0));
        elsif addr = "011000" or addr = "011001" then
          sweep(to_integer(unsigned(addr(0 downto 0)))) <= wdata;
          sweep_cnt(to_integer(unsigned(addr(0 downto 0)))) <= 0;
        elsif addr(5 downto 3) = "011" and unsigned(addr(2 downto 0)) >= 2 and unsigned(addr(2 downto 0)) <= 6 then
          adsr(to_integer(unsigned(addr(2 downto 0))) - 2) <= wdata;
        elsif addr(5 downto 4) = "10" then
          wave(to_integer(unsigned(addr(3 downto 0)))) <= wdata;
        end if;
      end if;

      -- channel timers at 25 MHz / 16
      pre <= pre + 1;
      if pre = 15 then
        for c in 0 to NCH - 1 loop
          if timer(c) = 0 then
            timer(c) <= period(c);
            case c is
              when 0 | 1 => step(c) <= (step(c) + 1) and "0111";
              when 2 => tri <= tri + 1;
              when 3 =>
                if ctl(3)(6) = '1' then
                  lfsr <= lfsr(13 downto 0) & (lfsr(0) xor lfsr(6));
                else
                  lfsr <= lfsr(13 downto 0) & (lfsr(0) xor lfsr(1));
                end if;
              when others => wpos <= wpos + 1;
            end case;
          else
            timer(c) <= timer(c) - 1;
          end if;
        end loop;
      end if;

      -- attack, decay (down to the sustain volume), lengths and sweeps at 60 Hz
      if frame_tick = '1' then
        for c in 0 to NCH - 1 loop
          if attacking(c) = '1' then
            if atk_cnt(c) >= to_integer(unsigned(adsr(c)(7 downto 4))) - 1 then
              atk_cnt(c) <= 0;
              if vol(c) < unsigned(ctl(c)(3 downto 0)) then
                vol(c) <= vol(c) + 1;
                if vol(c) + 1 >= unsigned(ctl(c)(3 downto 0)) then attacking(c) <= '0'; end if;
              else
                attacking(c) <= '0';
              end if;
            else
              atk_cnt(c) <= atk_cnt(c) + 1;
            end if;
          elsif ctl(c)(7) = '1' and vol(c) /= 0 then
            if decay_cnt(c) = to_integer(unsigned(env(c)(3 downto 0))) then
              decay_cnt(c) <= 0;
              if vol(c) > unsigned(adsr(c)(3 downto 0)) then vol(c) <= vol(c) - 1; end if;
            else
              decay_cnt(c) <= decay_cnt(c) + 1;
            end if;
          end if;
          if len(c) = 1 then active(c) <= '0'; end if;
          if len(c) /= 0 then len(c) <= len(c) - 1; end if;
        end loop;
        for c in 0 to 1 loop
          if sweep(c)(7) = '1' and sweep(c)(2 downto 0) /= "000" then
            if sweep_cnt(c) >= to_integer(unsigned(sweep(c)(6 downto 4))) then
              sweep_cnt(c) <= 0;
              p := resize(period(c), 13);
              d := shift_right(p, to_integer(unsigned(sweep(c)(2 downto 0))));
              if sweep(c)(3) = '1' then
                p := p - d;
              else
                p := p + d;
                if p > 4095 then p := to_unsigned(4095, 13); end if;
              end if;
              period(c) <= p(11 downto 0);
            else
              sweep_cnt(c) <= sweep_cnt(c) + 1;
            end if;
          end if;
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
      if wpos(0) = '0' then
        smp := unsigned(wave(to_integer(wpos(4 downto 1)))(7 downto 4));
      else
        smp := unsigned(wave(to_integer(wpos(4 downto 1)))(3 downto 0));
      end if;
      if active(4) = '1' and enable(4) = '1' then
        prod := resize(smp * vol(4), 12);
        outv(4) <= prod(7 downto 4) + ("000" & prod(3));
      else
        outv(4) <= (others => '0');
      end if;

      -- mix: (sum of 5 channels, 0..75) * master (0..15) * 1.125, at most 1023
      sum := resize(outv(0), 8) + outv(1) + outv(2) + outv(3) + outv(4);
      prod := sum * master;
      m := resize(prod, 13) + resize(prod(11 downto 3), 13);
      if m > 1023 then mix <= (others => '1'); else mix <= m(9 downto 0); end if;

      -- optional low-pass at the channel rate
      if pre = 15 then
        if filt = 0 then
          lp <= mix & x"00";
        else
          x18 := mix & x"00";
          diff := signed(resize(x18, 19)) - signed(resize(lp, 19));
          nv := signed(resize(lp, 19)) + shift_right(diff, to_integer(filt) + 3);
          lp <= unsigned(nv(17 downto 0));
        end if;
      end if;

      -- first-order delta-sigma: the carry of an accumulator; its density is exactly level / 1024
      v1 := i1 + signed(resize(lp(17 downto 8), 18));
      if v1 >= 1024 then
        i1 <= v1 - 1024;
        ds <= '1';
      else
        i1 <= v1;
        ds <= '0';
      end if;

      if rst = '1' then
        active <= (others => '0'); attacking <= (others => '0'); master <= (others => '0');
        enable <= (others => '0'); filt <= (others => '0'); adsr <= (others => (others => '0'));
        sweep <= (others => (others => '0'));
        i1 <= (others => '0'); ds <= '0'; lp <= (others => '0');
      end if;
    end if;
  end process;

  level <= lp(17 downto 8);
  pwm_bit <= ds;
  aud_pwm <= '0' when ds = '0' else 'Z';
  aud_sd <= '1' when master /= 0 else '0';
  rdata <= x"A2" when addr = "010010" or addr = "010011" else "000" & active;
end architecture;

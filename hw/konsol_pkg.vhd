-- Shared constants of the retro konsol: memory map and I/O register addresses.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package konsol_pkg is
  constant KONSOL_VERSION : std_logic_vector(7 downto 0) := x"01";

  -- CPU memory map
  --   $0000-$7FFF RAM (32 KB) | $8000-$8FFF I/O | $9000-$DFFF game RAM (20 KB) | $E000-$FFFF boot ROM (8 KB)
  constant GAME_RAM_BYTES : integer := 20480;

  -- I/O pages ($8000 + page * $100)
  constant IO_PPU   : integer := 16#0#;
  constant IO_APU   : integer := 16#1#;
  constant IO_INPUT : integer := 16#2#;
  constant IO_TIMER : integer := 16#3#;
  constant IO_DISP  : integer := 16#4#;
  constant IO_UART  : integer := 16#5#;
  constant IO_SPI   : integer := 16#6#;
  constant IO_SYS   : integer := 16#7#;

  -- CPU clock enables: one CPU cycle every CPU_DIV_FAST (6.25 MHz) or CPU_DIV_SLOW (1.786 MHz) clocks.
  constant CPU_DIV_FAST : integer := 4;
  constant CPU_DIV_SLOW : integer := 14;
end package;

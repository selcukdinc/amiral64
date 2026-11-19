-- K1: the T65 runs sw/tests/cputest.s from the boot ROM (RAM, stack, BCD, indexed/indirect addressing,
-- game RAM, I/O, timer IRQ) and reports $01 on SYS_TEST.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_cpu_k1 is end;

architecture sim of tb_cpu_k1 is
  signal clk : std_logic := '0';
  signal rst : std_logic := '1';
  signal test_we, cpu_en : std_logic;
  signal code : std_logic_vector(7 downto 0);
  signal addr : std_logic_vector(15 downto 0);
  signal leds : std_logic_vector(15 downto 0);
  signal done : boolean := false;
begin
  clk <= not clk after 20 ns when not done;
  dut : entity work.konsol_core
    generic map (BOOT_ROM => "cputest.mem")
    port map (clk => clk, rst => rst, sw => x"1234", buttons => "00000", leds => leds, rgb16 => open, rgb17 => open,
              digits => open, digit_blank => open, digit_dots => open, frame_tick => '0',
              dbg_test_we => test_we, dbg_test_code => code, dbg_addr => addr, dbg_cpu_en => cpu_en);
  process
  begin
    wait for 200 ns;
    wait until rising_edge(clk);
    rst <= '0';
    wait until rising_edge(clk) and test_we = '1' for 5 ms;
    assert test_we = '1' report "timeout: the CPU test did not finish (PC near " & to_hstring(addr) & ")" severity failure;
    wait until rising_edge(clk);
    assert code = x"01" report "CPU test failed at check " & to_hstring(code) severity failure;
    assert leds(7 downto 0) = x"3C" report "LED register" severity failure;
    report "TEST PASSED (CPU self-test, " & time'image(now) & ")";
    done <= true;
    wait;
  end process;
end architecture;

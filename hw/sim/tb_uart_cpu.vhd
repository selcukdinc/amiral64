-- K4: the PC side of the UART (115200 8N1): the CPU (sw/tests/iotest.s) sends "K65", echoes "abc" as "bcd"
-- and stops at "!".
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_uart_cpu is end;

architecture sim of tb_uart_cpu is
  constant BIT_T : time := 8680 ns;   -- 115200 baud
  signal clk : std_logic := '0';
  signal rst : std_logic := '1';
  signal txd, rxd : std_logic := '1';
  signal test_we : std_logic;
  signal code : std_logic_vector(7 downto 0);
  signal done : boolean := false;
  type bytes_t is array (0 to 63) of std_logic_vector(7 downto 0);
  signal rx_log : bytes_t;
  signal rx_n : integer := 0;
begin
  -- PC receiver: logs every byte the CPU sends
  process
    variable b : std_logic_vector(7 downto 0);
  begin
    wait until txd = '0';
    wait for BIT_T * 1.5;
    for i in 0 to 7 loop b(i) := txd; wait for BIT_T; end loop;
    assert txd = '1' report "stop bit" severity failure;
    rx_log(rx_n) <= b;
    rx_n <= rx_n + 1;
  end process;

  clk <= not clk after 20 ns when not done;
  dut : entity work.konsol_core
    generic map (BOOT_ROM => "iotest.mem")
    port map (clk => clk, rst => rst, sw => x"0000", buttons => "00000", leds => open, rgb16 => open, rgb17 => open,
              digits => open, digit_blank => open, digit_dots => open,
              ps2_clk => '1', ps2_data => '1', nes_latch => open, nes_clk => open, nes_data => "1111",
              uart_txd => txd, uart_rxd => rxd, aud_pwm => open, aud_sd => open, flash_sck => open, flash_cs_n => open, flash_mosi => open, flash_miso => '1', vga_rgb => open, vga_hs => open, vga_vs => open,
              dbg_test_we => test_we, dbg_test_code => code, dbg_addr => open, dbg_cpu_en => open);
  process
    variable got : integer := 0;
    procedure recv(expect : character) is
    begin
      if rx_n <= got then wait until rx_n > got for 5 ms; end if;
      assert rx_n > got report "no byte from the CPU (expected '" & expect & "')" severity failure;
      assert rx_log(got) = std_logic_vector(to_unsigned(character'pos(expect), 8))
        report "got " & to_hstring(rx_log(got)) & ", expected '" & expect & "'" severity failure;
      got := got + 1;
    end procedure;
    procedure send(c : character) is
      variable b : std_logic_vector(7 downto 0) := std_logic_vector(to_unsigned(character'pos(c), 8));
    begin
      rxd <= '0'; wait for BIT_T;
      for i in 0 to 7 loop rxd <= b(i); wait for BIT_T; end loop;
      rxd <= '1'; wait for BIT_T;
    end procedure;
  begin
    wait for 200 ns; wait until rising_edge(clk); rst <= '0';
    recv('K'); recv('6'); recv('5');
    send('a'); recv('b');
    send('b'); send('c');      -- back to back: the FIFO keeps both
    recv('c'); recv('d');
    send('!');
    wait until test_we = '1' for 2 ms;
    wait until rising_edge(clk);
    assert code = x"01" report "the CPU did not finish (code=" & to_hstring(code) & ")" severity failure;
    report "TEST PASSED";
    done <= true;
    wait;
  end process;
end architecture;

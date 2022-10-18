library IEEE;
use IEEE.std_logic_1164.all;

package miscellaneous is
  function bool2std(x: boolean) return std_logic;
end package miscellaneous;

package body miscellaneous is
  function bool2std(x: boolean) return std_logic is
  begin
    if x then
      return '1';
    else
      return '0';
    end if;
  end function bool2std;
end package body miscellaneous;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.std_logic_misc.all;
library work;
use work.miscellaneous.all;
library unisim;
use unisim.vcomponents.all;

entity calpulser is
  port (
    clksel: out std_logic;
    pi_sclk,pi_mosi,pi_cs0_n,pi_cs1_n: in std_logic;
    pi_miso: out std_logic;
    pllsclk,pllsdi,pllcs_n: out std_logic;
    pllsdo: in std_logic;
    asclk,asdi,acs_n: out std_logic;
    asdo: in std_logic;

    asel,async: out std_logic;
    ad: out std_logic_vector(0 to 7);
    apclk_p,apclk_n: in std_logic;
    apclk_fb_pad: inout std_logic;      -- not connected on PCB, used for MMCM FB
    auxo0_n: out std_logic
    );
end calpulser;

architecture calpulser_0 of calpulser is
  attribute PULLTYPE: string;
  attribute PULLTYPE of pllsdo,asdo: signal is "PULLDOWN";
  -- that was PULLUP, but for asdo,bsdo I think pulldown will be better, and either should function
  -- ok, so let's try this this way
  signal apclk,clkreset: std_logic;
  signal init_delay: integer range 0 to 2**16-1;-- := 1;
  signal pp_delay: integer range 0 to 2**16-1;-- := 20;  -- for now, pp_delay+p_width MUST be >8 !!
  signal p_width: integer range 0 to 2**16-1;-- := 9;
  signal kpulsemax: integer range 0 to 2**4-1;-- := 5;  -- num_pulses - 1
  signal kpulse: integer range 0 to 2**4-1;
  signal k,pstart,pfinish: integer range 0 to ((2**4)+2)*(2**16-1);
  signal idle: std_logic := '1';
  signal jj: integer range 0 to 100000 := 100000;  -- hack trigger
  signal ad_pre: std_logic_vector(ad'range);
  signal csr_main: std_logic_vector(9*8-1 downto 0) := (others => '0');
begin

  pllsdi <= pi_mosi;
  pllsclk <= pi_sclk;
  pllcs_n <= not ((not pi_cs1_n) and (not csr_main(9*8-1)));
  asdi <= pi_mosi;
  asclk <= pi_sclk;
  acs_n <= not ((not pi_cs1_n) and csr_main(9*8-1));

  clkreset <= csr_main(9*8-2);
  
  -- local control registers (SPI device 0)
  -- Currently this does not support read-only, but I could easily use a bit in the written data (in
  -- shadow register) to select whether to really write the data out. Do this later, if relevant.
  csr_blk: block
    signal csr_main_shadow: std_logic_vector(csr_main'range);
    signal k: integer range 0 to csr_main'high+2;
  begin
    process(pi_sclk,pi_cs0_n,csr_main)
    begin
      if pi_cs0_n='1' then
        -- effectively, copy reg to shadow at the start of the SPI transaction
        -- this could have been done by OR'ing somehow the clock edges of pi_cs0_n falling and pi_sclk
        -- rising, but this way is simpler and should work (I think). Everything is very slow anyways.
        csr_main_shadow <= csr_main;
        k <= 0;
      elsif pi_sclk'event and pi_sclk='1' then
        csr_main_shadow <= csr_main_shadow(csr_main'high-1 downto 0) & pi_mosi;
        if k/=csr_main'high+2 then     -- park the counter if overrun!
          k <= k+1;
        end if;
      end if;
    end process;
    process(pi_cs0_n)
    begin
      -- copy to register at the end of the read/write sequence
      if pi_cs0_n'event and pi_cs0_n='1' then
        if k=csr_main'high+1 then                     -- ONLY if we got exactly the expected number of sclk's !!
          csr_main <= csr_main_shadow; -- if there will be some read-only bits in future, leave them out of this of course
        end if;
      end if;
    end process;
    pi_miso <= csr_main_shadow(csr_main'high) when pi_cs0_n='0'
               else pllsdo when csr_main(9*8-1)='0'
               else asdo;
  end block csr_blk;
  init_delay <= to_integer(unsigned(csr_main(55 downto 40)));
  pp_delay <= to_integer(unsigned(csr_main(39 downto 24)));
  p_width <= to_integer(unsigned(csr_main(23 downto 8)));
  kpulsemax <= to_integer(unsigned(csr_main(3 downto 0)));
  
  clksel <= '1';

  asel <= '1';
  async <= '0';

  -- old stuff, save for now, but really not the way to do it!
  --x1: IBUFDS port map(I=> apclk_p, IB => apclk_n, O => apclk_i);  if re-instating, invert here for sanity?
  --x2: BUFG port map(I => apclk_i, O => apclk);
  b1: block
    signal apclk_i,apclk_i_bufg,apclk_x,apclk_fb_pad_o,apclk_fb_pad_i,apclk_fb_bufg: std_logic;
  begin
    -- Receive apclk but invert it, because MMCM PFD works with rising edge (I presume) but we care
    -- about the falling edge. Receive FB clock identically (except we use single-ended I/O for it,
    -- small enough delay difference. Run through MMCM/PLL to align the FB clock on its pad, which
    -- has same timing as data out to the MC100EP446, with the clock in to its pad.
    x1: IBUFDS_DIFF_OUT port map(I=> apclk_p, IB => apclk_n, OB => apclk_i);  -- we are INVERTING here!
    x2: BUFR port map(I => apclk_i, O => apclk_i_bufg, CE => '1', CLR => '0');
    x3: MMCME2_BASE
      generic map(BANDWIDTH => "HIGH",  -- Setting the BANDWIDTH to Low can incur an increase in the static offset of the MMCM.
                  CLKIN1_PERIOD => 4.800, --8.000, -- Input clock period in ns to ps resolution.
                  CLKOUT0_DIVIDE_F => 5.0, -- CLKOUT0 divisor (1.000-128.000), set to keep 600MHz<VCO<1440MHz
                  CLKOUT0_DUTY_CYCLE => 0.5,
                  CLKOUT0_PHASE => 0.0,
                  DIVCLK_DIVIDE => 1, -- refclk divisor
                  STARTUP_WAIT => FALSE)
      port map(CLKIN1 => apclk_i_bufg, CLKFBIN => apclk_fb_bufg, CLKOUT0 => apclk_x,
               PWRDWN => '0', RST => clkreset);
    x4: ODDR port map(C => apclk, Q => apclk_fb_pad_o,
                      CE => '1', D1 => '1', D2 => '0', R => '0', S => '0');
    x5: IOBUF port map(I => apclk_fb_pad_o, T => '0', IO => apclk_fb_pad, O => apclk_fb_pad_i);
    x6: BUFR port map(I => apclk_fb_pad_i, O => apclk_fb_bufg, CE => '1', CLR => '0');
    x7: BUFG port map(I => apclk_x, O => apclk);
  end block b1;
  
  ---------------------------------------------------------------------------------------------------
  -- Compute pulse pattern data as we go; currently supports N equal width equal spaced pulses
  -- starting after an initial delay. It would be possible to play a pattern from memory instead of
  -- computing like this. Maybe implement that option some other day.
  process(apclk)
  begin
    if apclk'event and apclk='1' then -- rising edge here corresponds to falling edge out of MC100EP446
      if idle='1' then
        jj <= jj-1;
        ad_pre <= (others => '0');
        if jj=0 then                    -- temporary trigger
          idle <= '0';
          k <= 0;
          kpulse <= 0;
          auxo0_n <= '0';               -- sync output on LEMO conn
          -- if it really still doesn't meet timing, try 8 different pstart/finish (ie.
          -- std_logic_vector of them) so that we absorb the k+j and k+7 below. all we have to do is
          -- offset the initial values, which anyway are "static" calculations and not really to
          -- cover by apclk timing constraint. (check if they are, if they are trouble.)
          pstart <= init_delay;         -- time (pattern tics) to start a pulse (first 1 of pulse)
          pfinish <= init_delay+p_width;  -- time to finish a pulse (first 0 after pulse)
        end if;
      else
        k <= k+8;
        auxo0_n <= '1';
        -- for now, must have pp_delay>=p_width+8, so we handle one pulse at a time in this !!
        -- the issue is that switching to next pulse takes a whole cycle (8 tics of pattern clock)!
        for j in 0 to 7 loop
          ad_pre(j) <= bool2std(k+j>=pstart and k+j<pfinish);  -- xor this with polarity bit, later
        end loop;
        if k+7>=pfinish then  -- finished a pulse on this cycle? then kpulse++ and update pstart/finish
          if kpulse=kpulsemax then
            idle <= '1';
            jj <= 100000;               -- hack periodic trigger
          else
            kpulse <= kpulse+1;
            pstart <= pstart+pp_delay;
            pfinish <= pfinish+pp_delay;
          end if;
        end if;
      end if;
      ad <= ad_pre; -- extra pipeline stage here harmless, and might help timing
    end if;
  end process;

end calpulser_0;

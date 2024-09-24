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
    pllsdo,pllstat: in std_logic;
    asclk,asdi,acs_n: out std_logic;
    asdo: in std_logic;
    bsclk,bsdi,bcs_n: out std_logic;
    bsdo: in std_logic;
    -- signal fsclk is CCLK pin, fed through STARTUPE2 primitive; see UG470 about 3 cycle init!
    fsdi,fcs_n: out std_logic;   -- pins MOSI, FCS_N
    fsdo: in std_logic;                --   and DIN

    asel,async: out std_logic;
    ad: out std_logic_vector(0 to 7);
    apclk_p,apclk_n: in std_logic;
    apclk_fb_pad: inout std_logic;      -- not connected on PCB, but used for MMCM FB
    bsel,bsync: out std_logic;
    bd: out std_logic_vector(0 to 7);
    -- bpclk_p,bpclk_n: in std_logic; -- not used, for now, use apclk for both (check w/ scope if a sane plan!)

    led_n: out std_logic_vector(3 downto 0);
    auxo0_n,auxo1: out std_logic;
    auxi0: in std_logic
    );
end calpulser;

architecture calpulser_0 of calpulser is
  attribute PULLTYPE: string;
  attribute PULLTYPE of pllsdo,asdo: signal is "PULLDOWN";
  signal apclk: std_logic;
  signal trig_delay: unsigned(31 downto 0); -- would have liked integer, but VHDL sucks in this regard
  type array_int_16 is array(natural range <>) of integer range 0 to 2**16-1;
  type array_int_4 is array(natural range <>) of integer range 0 to 2**4-1;
  signal init_delay: array_int_16(0 to 1);
  signal pp_delay: array_int_16(0 to 1);   -- for now, pp_delay+p_width MUST be >8 !!
  signal p_width: array_int_16(0 to 1);
  signal kpulsemax: array_int_4(0 to 1);   -- num_pulses - 1
  signal polinv: std_logic_vector(0 to 1);
  signal idle: std_logic_vector(0 to 1) := "11";
  signal trigsel,go: std_logic;
  -- 2 byte super-csr + 4 byte trig_delay + 7 byte channel-pattern csr per channel
  signal csr_main: std_logic_vector((2+4+2*7)*8-1 downto 0) := (others => '0');
  signal mmcm_locked: std_logic;
  signal fsclk: std_logic;
begin

  -- LED's viewed from front:  0  1  2  3
  led_n(0) <= '0';            -- "FPGA up"
  led_n(1) <= not pllstat;    -- "PLL ok"
  led_n(2) <= not ((not pi_cs0_n) or (not pi_cs1_n));    -- "PI SPI"
  led_n(3) <= not mmcm_locked;
  
  pllsdi <= pi_mosi;
  pllsclk <= pi_sclk;
  pllcs_n <= not ((not pi_cs1_n) and csr_main(csr_main'high));
  asdi <= pi_mosi;
  asclk <= pi_sclk;
  acs_n <= not ((not pi_cs1_n) and csr_main(csr_main'high-1));
  bsdi <= pi_mosi;
  bsclk <= pi_sclk;
  bcs_n <= not ((not pi_cs1_n) and csr_main(csr_main'high-2));
  -- FPGA config SPI flash
  fsdi <= pi_mosi;
  fsclk <= pi_sclk; -- need to mask this off?
  fcs_n <= not ((not pi_cs1_n) and csr_main(csr_main'high-7));
  
  x1: STARTUPE2 port map(
    CFGCLK => open, CFGMCLK => open, EOS => open, PREQ => open,
    CLK => '0', GSR => '0', GTS => '0', KEYCLEARB => '1', PACK => '0',
    USRCCLKO => fsclk, USRCCLKTS => '0', USRDONEO => '0', USRDONETS => '0');
  
  -- local control registers (SPI device 0)
  -- Currently this does not support read-only, but I could easily use a bit in the written data (in
  -- shadow register) to select whether to really write the data out. Do this later, if relevant.
  -- First byte bits:
  --    7: select PLL chip to SPI device 1
  --    6: select VGA A chip to SPI device 1
  --    5: select VGA B chip to SPI device 1    do only one of them at a time!
  --    4: sync to both MC100EP446's
  --    3: apclk MMCM reset
  --    2: clksel (1: local, 0: FTSW)
  --    1: trgsel (1: external AUXIN0, 0: internal timer)
  --    0: enable SPI 1 to FPGA config flash (N25Q032A13ESFA0F)
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
        if k=csr_main'high+1 then      -- ONLY if we got EXACTLY the expected number of sclk's !!
          csr_main <= csr_main_shadow; -- if there will be some read-only bits in future, leave them out of this of course
        elsif k=16 then                -- OR ALTERNATIVELY if we got 16 then it's a write of just the high two bytes!
          csr_main(csr_main'high downto csr_main'high-15) <= csr_main_shadow(15 downto 0);
        end if;
      end if;
    end process;
    pi_miso <= csr_main_shadow(csr_main'high) when pi_cs0_n='0'
               else pllsdo when csr_main(csr_main'high)='1'
               else asdo when csr_main(csr_main'high-1)='1'
               else bsdo when csr_main(csr_main'high-2)='1'
               else fsdo;
  end block csr_blk;
  trig_delay <= unsigned(csr_main(143 downto 112));
  c1: for i in 1 downto 0 generate -- note, order in writing in SW is ch0 stuff first then ch1 stuff
    init_delay(1-i) <= to_integer(unsigned(csr_main(55+56*i downto 40+56*i)));
    pp_delay(1-i) <= to_integer(unsigned(csr_main(39+56*i downto 24+56*i)));
    p_width(1-i) <= to_integer(unsigned(csr_main(23+56*i downto 8+56*i)));
    kpulsemax(1-i) <= to_integer(unsigned(csr_main(3+56*i downto 56*i)));
    polinv(1-i) <= csr_main(7+56*i);
  end generate;
  
  clksel <= csr_main(csr_main'high-5);
  trigsel <= csr_main(csr_main'high-6);

  asel <= '1';
  bsel <= '1';
  async <= csr_main(csr_main'high-3);
  bsync <= csr_main(csr_main'high-3);

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
               PWRDWN => '0', RST => csr_main(csr_main'high-4), LOCKED => mmcm_locked);
    x4: ODDR port map(C => apclk, Q => apclk_fb_pad_o,
                      CE => '1', D1 => '1', D2 => '0', R => '0', S => '0');
    x5: IOBUF port map(I => apclk_fb_pad_o, T => '0', IO => apclk_fb_pad, O => apclk_fb_pad_i);
    x6: BUFR port map(I => apclk_fb_pad_i, O => apclk_fb_bufg, CE => '1', CLR => '0');
    x7: BUFG port map(I => apclk_x, O => apclk);
  end block b1;
  
  ---------------------------------------------------------------------------------------------------
  trig: block
    signal jj: unsigned(31 downto 0);
    signal ext_r,ext_r2,ext_r3: std_logic;
  begin
    process(apclk)
    begin
      if apclk'event and apclk='1' then
        if jj=0 then
          jj <= trig_delay;
        else
          jj <= jj-1;
        end if;
        auxo0_n <= not go;   -- sync out on LEMO conn
        auxo1 <= go;         -- sync out LVDS on RJ45
        ext_r <= auxi0; ext_r2 <= ext_r; ext_r3 <= ext_r2;
      end if;
    end process;
    go <= ((bool2std(jj=0) and not trigsel) or (ext_r2 and (not ext_r3) and trigsel)) 
          and idle(0) and idle(1);
  end block;

  -- Compute pulse pattern data as we go; currently supports N equal width equal spaced pulses
  -- starting after an initial delay. It would be possible to play a pattern from memory instead of
  -- computing like this. Maybe implement that option some other day.
  c2: for i in 0 to 1 generate
    pat: block
      signal kpulse: integer range 0 to 2**4-1;
      signal k,pstart,pfinish: integer range 0 to ((2**4)+2)*(2**16-1);
      signal d_pre: std_logic_vector(ad'range);
    begin
      process(apclk)
      begin
        if apclk'event and apclk='1' then -- rising edge here corresponds to falling edge out of MC100EP446
          if idle(i)='1' then
            d_pre <= (others => polinv(i));
            if go='1' then
              idle(i) <= '0';
              k <= 0;
              kpulse <= 0;
              -- if it really still doesn't meet timing, try 8 different pstart/finish (ie.
              -- std_logic_vector of them) so that we absorb the k+j and k+7 below. all we have to do is
              -- offset the initial values, which anyway are "static" calculations and not really to
              -- cover by apclk timing constraint. (check if they are, if they are trouble.)
              pstart <= init_delay(i);         -- time (pattern tics) to start a pulse (first 1 of pulse)
              pfinish <= init_delay(i)+p_width(i);  -- time to finish a pulse (first 0 after pulse)
            end if;
          else
            k <= k+8;
            -- for now, must have pp_delay>=p_width+8, so we handle one pulse at a time in this !!
            -- the issue is that switching to next pulse takes a whole cycle (8 tics of pattern clock)!
            for j in 0 to 7 loop
              d_pre(j) <= bool2std(k+j>=pstart and k+j<pfinish) xor polinv(i);
            end loop;
            if k+7>=pfinish then  -- finished a pulse on this cycle? then kpulse++ and update pstart/finish
              if kpulse=kpulsemax(i) then
                idle(i) <= '1';
              else
                kpulse <= kpulse+1;
                pstart <= pstart+pp_delay(i);
                pfinish <= pfinish+pp_delay(i);
              end if;
            end if;
          end if;
          if i=0 then
            ad <= d_pre; -- extra pipeline stage here harmless, and might help timing
          else
            bd <= d_pre;
          end if;
        end if;
      end process;
    end block;
  end generate;

end calpulser_0;

set_property PACKAGE_PIN J1 [get_ports pi_mosi]
set_property PACKAGE_PIN J2 [get_ports pi_miso]
set_property PACKAGE_PIN G1 [get_ports pi_sclk]
set_property PACKAGE_PIN G2 [get_ports pi_cs0_n]
set_property PACKAGE_PIN H4 [get_ports pi_cs1_n]
set_property PACKAGE_PIN K17 [get_ports pllcs_n]
set_property PACKAGE_PIN J17 [get_ports pllsclk]
set_property PACKAGE_PIN H17 [get_ports pllsdi]
set_property PACKAGE_PIN L16 [get_ports pllsdo]
set_property PACKAGE_PIN N22 [get_ports acs_n]
set_property PACKAGE_PIN M22 [get_ports asclk]
set_property PACKAGE_PIN P21 [get_ports asdi]
set_property PACKAGE_PIN H19 [get_ports asdo]
set_property PACKAGE_PIN K19 [get_ports clksel]
set_property PACKAGE_PIN J20 [get_ports asel]
set_property PACKAGE_PIN AA20 [get_ports async]
set_property PACKAGE_PIN AB21 [get_ports {ad[0]}]
set_property PACKAGE_PIN AA21 [get_ports {ad[1]}]
set_property PACKAGE_PIN AB22 [get_ports {ad[2]}]
set_property PACKAGE_PIN Y22 [get_ports {ad[3]}]
set_property PACKAGE_PIN Y21 [get_ports {ad[4]}]
set_property PACKAGE_PIN W22 [get_ports {ad[5]}]
set_property PACKAGE_PIN W21 [get_ports {ad[6]}]
set_property PACKAGE_PIN V22 [get_ports {ad[7]}]
set_property SLEW FAST [get_ports {ad[*]}]
set_property PACKAGE_PIN W19 [get_ports apclk_p]
set_property PACKAGE_PIN W20 [get_ports apclk_n]
set_property PACKAGE_PIN V18 [get_ports apclk_fb_pad]; # not connected on PCB
set_property SLEW FAST [get_ports apclk_fb_pad]
set_property PACKAGE_PIN P6 [get_ports auxo0_n]


set_property IOSTANDARD LVCMOS33 [get_ports {*}]
# do I have to exclude that from the LVCMOS33 line? how?
set_property IOSTANDARD LVDS_25 [get_ports apclk_p]

# try to have timing constraint for 2GHz pattern clock (/8 apclk 250 MHz)
#create_clock -period 4.000 -name apclk -waveform {0.000 2.000} [get_ports -filter { NAME =~ "adclk_p" && DIRECTION == "IN" }]
#create_clock -period 4.000 -name apclk [get_ports apclk_p]
# this must be (manually?) kept in sync w/ CLKOUT0_DIVIDE_F and CLKIN1_PERIOD of the MMCM... and must match HW reality?
create_clock -period 4.800 [get_ports apclk_p]

#set_property CLOCK_DEDICATED_ROUTE FALSE [get_nets asclk_OBUF]
set_property CLOCK_DEDICATED_ROUTE FALSE [get_nets pi_sclk]
create_clock -period 500.0 [get_ports pi_sclk]
# will need to make IBUF explicit in the vhdl, to get a fixed name for this thing!
#set_property CLOCK_DEDICATED_ROUTE FALSE [get_nets pi_cs0_n_IBUF]
set_property CLOCK_DEDICATED_ROUTE FALSE [get_nets pi_cs0_n]
create_clock -period 500.0 [get_ports pi_cs0_n]
set_false_path -from [get_clocks pi_cs0_n] -to [get_clocks apclk_x]; # apclk_x is name of derived clk out of MMCM

# can't this be "auto" ???
set_property IOB TRUE [get_cells {ad_reg[*]}]
set_property IOB TRUE [get_cells auxo0_n_reg]
# I should add some output constraint here... Maybe that automatically gets me IOFF???
# BTW this seems OK for 125 MHz PCLK but not for 250. Will need to run PCLK input through DCM to get rid of some delay... Later

# set_property C_CLK_INPUT_FREQ_HZ 300000000 [get_debug_cores dbg_hub]
# set_property C_ENABLE_CLK_DIVIDER false [get_debug_cores dbg_hub]
# set_property C_USER_SCAN_CHAIN 1 [get_debug_cores dbg_hub]
# connect_debug_port dbg_hub/clk [get_nets clk]

set_property CFGBVS VCCO [current_design]
set_property CONFIG_VOLTAGE 3.3 [current_design]
# note: PUDC# is grounded on the board, pullups active before configuration, board is designed with this in mind
set_property BITSTREAM.CONFIG.UNUSEDPIN PULLUP [current_design]
set_property BITSTREAM.CONFIG.USERID 0x12345678 [current_design]

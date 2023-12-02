/*  Belle-II TOP Calibration Pulser
    Gerard Visser, Indiana University

    SPI handling based on examples at https://github.com/sckulkarni246/ke-rpi-samples/tree/main/spi-c-ioctl
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
//#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
//#include <sys/stat.h>   // is this really needed? seems like not
//#include <linux/ioctl.h>   // is this really needed? seems like not
//#include <linux/types.h>   // is this really needed? seems like not
#include <linux/spi/spidev.h>

int main(int argc, char *argv[]) {
  extern char *optarg;
  int c;
  int spifd[2];
  struct spi_ioc_transfer spit[2];
  uint32_t tmp;
  uint8_t tx_buf[32];
  uint8_t rx_buf[32];
  int ix,k,ret;
  double init_delay_ns[2]={0.0,0.0},pp_delay_ns[2]={20.0,20.0},p_width_ns[2]={2.0,2.0};
  int ch=0,init_delay[2],pp_delay[2],p_width[2],npulses[2]={2,2},pol[2]={0,0},atten[2]={32,32};
  double tici=0.6,tic,fin,fout,fvco;
  int pll_R,pll_N,pll_BD,pll_MDA,pll_MDB,clksel;
  double pll_P;
  const uint16_t MD2M[16] =  // table 15 "MDx[3:0] Programming" in LTC6851 datasheet
    {0b0000000000000001,
     0b0000000000000010,
     0b0000000000000100,
     0b0000000000001000,
     0b0000000000001100,
     0b0000000000010000,
     0b0000000000011000,
     0b0000000000100000,
     0b0000000000110000,
     0b0000000001000000,
     0b0000000001100000,
     0b0000000010000000,
     0b0000000011000000,
     0b0000000100000000,
     0b0000000110000000,
     0b0000001000000000};

  while((c = getopt(argc, argv, "t:c:i:s:w:n:va:")) != -1) {
    switch(c) {
    case 't' :  // time tic in ns (1ns or 600ps, maybe 750, 800 added later))
      tici=atof(optarg);
      break;
    case 'c' :  // set channel index for arguments that follow
      ch=atoi(optarg);
      if (ch<0) {
	ch=0;
	printf("channel set to 0, can't be less\n");
      }
      else if (ch>1) {
	ch=1;
	printf("channel set to 1, can't be more\n");
      }
      break;
    case 'i' :  // initial delay in ns
      init_delay_ns[ch]=atof(optarg);
      break;
    case 's' :  // pulse-pulse delay in ns;  REQUIREMENT: pp_delay>=p_width+8  !!!
      pp_delay_ns[ch]=atof(optarg);
      break;
    case 'w' :  // pulse width in ns
      p_width_ns[ch]=atof(optarg);
      break;
    case 'n' :  // number of pulses
      npulses[ch] = atoi(optarg);
      break;
    case 'v' :  // inVert the pulse polarity
      pol[ch] = 1;
      break;
    case 'a' :  // amplitude in V
      atten[ch] = (int) (-20*log10(atof(optarg)/1.6) + 0.5);
      if (atten[ch]<0)
	atten[ch]=0;
      if (atten[ch]>32)
	atten[ch]=32;
      break;
    default :
      printf("invalid argument\n");
      break;
    }
  }

  // Clock setups -- add more here -- nothing below '//////' line should be specific to any setup!
  // For the moment not using delays (but maybe will need to for some clock setups).
  // For the moment, using local oscillator only, need to work in here the FTSW clock stuff.
  clksel=1; // values are 0: FTSW 1: local osc (40 MHz)
  fin=0.040; // later fin will be set according to CLKSEL control and user input (about ext clock)
  // Keep fVCO in range 4 - 5 GHz, and take care w/ B value, see datasheet.
  // Avoid P=x.5 and M=1, due to subharmonic (see datasheet).
  // Minimum P that fits each case is best, to allow for finest/any control of delays.
  if (fabs(tici-0.5)<1e-6) {   // need to test    TBD whether the board can run reliably for 0.5 ns tics
    pll_R=10;   // range 1 to 63
    pll_N=500;  // range 2 to 511 (with RA0=1)
    pll_P=2;    // allowed values 2, 2.5, 3, 3.5, 4
    pll_BD=1;   // see table 11 "BD[3:0] Programming, RAO = 1" in LTC6951 datasheet
    pll_MDA=0;  // for outputs 0-3, see table 15 "MDx[3:0] Programming"
    pll_MDB=3;  // for output 4 (LVDS to FPGA, not really used at the moment)
  }
  else if (fabs(tici-0.6)<1e-6) {   // this is probably the best default/suggested tic
    pll_R=12;   // range 1 to 63
    pll_N=500;  // range 2 to 511 (with RA0=1)
    pll_P=3;    // allowed values 2, 2.5, 3, 3.5, 4
    pll_BD=1;   // see table 11 "BD[3:0] Programming, RAO = 1" in LTC6951 datasheet
    pll_MDA=0;  // for outputs 0-3, see table 15 "MDx[3:0] Programming"
    pll_MDB=3;  // for output 4 (LVDS to FPGA, not really used at the moment)
  }
  else if (fabs(tici-1.0)<1e-6) {   // need to test
    pll_R=12;   // range 1 to 63
    pll_N=300;  // range 2 to 511 (with RA0=1)
    pll_P=2;    // allowed values 2, 2.5, 3, 3.5, 4
    pll_BD=1;   // see table 11 "BD[3:0] Programming, RAO = 1" in LTC6951 datasheet
    pll_MDA=1;  // for outputs 0-3, see table 15 "MDx[3:0] Programming"
    pll_MDB=5;  // for output 4 (LVDS to FPGA, not really used at the moment)
  }
  else if (fabs(tici-6.0)<1e-6) {  // need to test   intended for some bench tests only
    pll_R=12;   // range 1 to 63
    pll_N=50;   // range 2 to 511 (with RA0=1)
    pll_P=3;    // allowed values 2, 2.5, 3, 3.5, 4
    // following BD setting follows the datasheet guidance but I don't understand it really... p14 oversimplified?
    pll_BD=6;   // see table 11 "BD[3:0] Programming, RAO = 1" in LTC6951 datasheet
    pll_MDA=3;  // for outputs 0-3, see table 15 "MDx[3:0] Programming"
    pll_MDB=9;  // for output 4 (LVDS to FPGA, not really used at the moment)
  }
  else {
    printf("requested time tic value not supported - exiting...\n");
    return -1;
  }

  fvco=fin/pll_R*pll_N*pll_P*MD2M[pll_MDA];
  fout=fin/pll_R*pll_N;
  tic=1.0/fout;

  for(ch=0;ch<2;ch++) {
    init_delay[ch] = (int) (init_delay_ns[ch]/tic + 0.5);
    pp_delay[ch] = (int) (pp_delay_ns[ch]/tic + 0.5);
    p_width[ch] = (int) (p_width_ns[ch]/tic + 0.5);
  }

  printf("TOP pulser setup:\nNote: your selections were rounded to nearest actual values\n");
  printf("tic size %7.5lf ns\n",tic);
  printf("  LTC6951 will be set for fout=%7.5lf GHz, fvco=%7.5lf GHz\n",fout,fvco);
  for(ch=0;ch<2;ch++) {
    printf("ch %d: initial delay %d tics (%.3lf ns), ",ch,init_delay[ch],init_delay[ch]*tic);
    if (npulses[ch]==1) {
      printf("1 pulse, width %d tics (%.3lf ns)\n",p_width[ch],p_width[ch]*tic);
    }
    else {
      printf("%d pulses, ",npulses[ch]);
      if (pp_delay[ch]<p_width[ch]+8) {
	pp_delay[ch] = p_width[ch]+8;
	printf("\nWARNING: enforced minimum pulse-pulse delay\n");
      }
      printf("width %d tics (%.3lf ns), pulse-pulse delay %d tics (%.3lf ns)\n",
	     p_width[ch],p_width[ch]*tic,pp_delay[ch],pp_delay[ch]*tic);
    }
    printf("      amplitude %.3lf V (atten %d dB)\n",1.6*pow(10.0,-atten[ch]/20.0),atten[ch]);
  }
  printf("--------doing setup--------\n\n");
  
  ////////////////////////////////////////////////////////////////////////////////////
  
  memset(&spit,0,sizeof(spit));  // there are other fields, which should all be 0 !!
  spit[0].tx_buf = /*(unsigned long)*/(uint32_t) tx_buf;
  spit[0].rx_buf = (unsigned long) rx_buf;
  //spit.bits_per_word = 0;
  spit[0].speed_hz = 200000; // MHz, and seems to have good resolution at least for around few MHz
  // HUH? did I really mean 200 MHZz??? Seems unlikely, should this have been 20?????
  //spit.delay_usecs = 0;
  //    BELOW spit.len = 2;
  spit[1].tx_buf = /*(unsigned long)*/(uint32_t) tx_buf;  // we just use same buffer, for convenience
  spit[1].rx_buf = (unsigned long) rx_buf;                // ditto
  spit[1].speed_hz = 200000; // MHz, and seems to have good resolution at least for around few MHz

  // clean this up w/ for loop, perror
  spifd[0] = open("/dev/spidev0.0", O_RDWR);
  if(spifd[0] < 0) {
    printf("[0] Could not open the SPI device...\r\n");
    exit(-1);
  }
  tmp = SPI_MODE_0;   // for details about modes, see /include/uapi/linux/spi/spi.h
  ret = ioctl(spifd[0], SPI_IOC_WR_MODE32, &tmp);
  if(ret != 0) {
    printf("[0] Could not write SPI mode...\r\n");
    close(spifd[0]);
    exit(-1);
  }
  spifd[1] = open("/dev/spidev0.1", O_RDWR);
  if(spifd[1] < 0) {    // <= ?? check this
    printf("[1] Could not open the SPI device...\r\n");
    exit(-1);
  }
  tmp = SPI_MODE_0;   // for details about modes, see /include/uapi/linux/spi/spi.h
  ret = ioctl(spifd[1], SPI_IOC_WR_MODE32, &tmp);
  if(ret != 0) {
    printf("[1] Could not write SPI mode...\r\n");
    close(spifd[0]);
    exit(-1);
  }

  //-----------------------------------------------------------------------------------------------
  // main CSR (device 0), may branch this off into addressable parts someday later
  // 0,1:  main CSR [15:0]
  //     15: enable SPI 1 to PLL (LTC6951)     use one only of these enable bits at a time!
  //     14: enable SPI 1 to channel A VGA (LMH6401)
  //     13: enable SPI 1 to channel B VGA
  //     12: SYNC bit to A & B serializer chips (MC100EP446)
  //     11: APCLK MMCM reset bit
  //     10: clksel (1: local, 0: FTSW)
  //     9-0 not used
  //         to be added: ch B VGA, A/B MC100EP446 SYNC bit, A & B polarity bits, A & B enable bits
  //to be added: 32 bits trigger timer
  // 2,3:  A init_delay 0 to 2**16-1
  // 4,5:  A pp_delay 0 to 2**16-1
  // 6,7:  A p_width 0 to 2**16-1
  // 8:    A npulses-1, 0 to 2**4-1, polarity
  //-----------------------------------------------------------------------------------------------
  tx_buf[ix=0] = 0x90 | (clksel<<2); // set device 1 to the PLL (using device 0), and assert MC100EP446 SYNC
  tx_buf[++ix] = 0x00;
  spit[0].len = ++ix;
  ret = ioctl(spifd[0], SPI_IOC_MESSAGE(1), &spit[0]);
  if(ret<0) {
    perror("[0] SPI transfer ioctl ERROR");
  }
  printf("[0] Received SPI buffer: ");
  for(k=0; k<spit[0].len;k++) {
    printf("%02x ",rx_buf[k]);
  }
  printf("\n");
  
  // initialize the PLL (LTC6951)
  // note the loop filter is designed for fpfd=3.33 MHz, may be best to stay close to that?
  // it would probably be best to check bounds on pll_R, pll_N, pll_P, pll_MDA, pll_MDB here... Add this someday
  // while at that, can check VCO frequency restriction automatically here too...

  tx_buf[ix=0] = 0x01*2+0;  // start write at register 0x01
  tx_buf[++ix] = 0xba;   // reg 1: STAT = not (ALCHI or ALCLO or (not LOCK) or (not REFOK)) = "PLLOK"
  tx_buf[++ix] = 0x04;   // reg 2: SYNC asserted; nothing powered down; for cal we use autocal so don't set it here
  tx_buf[++ix] = 0x7e;   // reg 3: ALC: during cal only, monitor level always, autocal enabled, RA0=1, low level ref input
  tx_buf[++ix] = (pll_BD<<4) | 0x07;                  // reg 4: BD, LKWIN 10.7ns, LKCNT 2048
  tx_buf[++ix] = (pll_R<<2) | ((pll_N&0x300)>>8);     // reg 5: R and top 2 bits of N
  tx_buf[++ix] = (pll_N&0xff);                        // reg 6: bottom byte of N
  tx_buf[++ix] = 0x05;   // reg 7: CP: no overrides, not WIDE, 5.6 mA
  tx_buf[++ix] = (((int)((pll_P-1.999)*2))&0x07)<<5;  // reg 8: P, no mute
  tx_buf[++ix] = 0x80 | pll_MDA;   // reg 8: out0: sync enable, no mute, MDA
  tx_buf[++ix] = 0x00;  // reg 9: SN=0 SR=0
  tx_buf[++ix] = 0x80 | pll_MDA;  // reg 10: out1: sync enable, no mute, MDA
  tx_buf[++ix] = 0x00;  // reg 11: out1: delay 0
  tx_buf[++ix] = 0x80 | pll_MDA;  // reg 13: out2: sync enable, no mute, MDA
  tx_buf[++ix] = 0x00;  // reg 14: out2: delay 0
  tx_buf[++ix] = 0x80 | pll_MDA;  // reg 15: out3: sync enable, no mute, MDA
  // best polarity for various clocks has to be investigated still   !!! CAUTION !!!
  tx_buf[++ix] = 0x40;  // reg 16: out3: delay 0, let's invert (better FF clock timing?)
  tx_buf[++ix] = 0x80 | pll_MDB;  // reg 17: out4: sync enable, no mute, MDB (!!)
  tx_buf[++ix] = 0x06;  // reg 18: out4: delay 6 (this gets it phase aligned with out0-3)  THIS IS NOT RIGHT DELAY IF MDB/=3
  // I NEED TO REALLY HANDLE THE DELAYS IN VARIOUS CLOCK SETUPS, add this, probably needed for phase details on othe clocks anyway
  spit[1].len = ++ix;
  ret = ioctl(spifd[1], SPI_IOC_MESSAGE(1), &spit[1]);
  if(ret<0) {
    perror("[1] SPI transfer ioctl ERROR");
  }
  usleep(500000);  // wait for lock & stabilization

  // read and check
  tx_buf[0] = 0x00*2+1; // start read at register 00
  spit[1].len=21;
  ret = ioctl(spifd[1], SPI_IOC_MESSAGE(1), &spit[1]);
  if(ret<0) {
    perror("[1] SPI transfer ioctl ERROR");
  }
  printf("[1] Received SPI buffer: ");
  for(k=0; k<spit[1].len;k++) {
    printf("%02x ",rx_buf[k]);
  }
  printf("\n");
  if (rx_buf[1]==0x05)
    printf("LTC6951 status GOOD (locked, ALC ok, REF ok)\n");
  else
    printf("WARNING: LTC6951 status reads no good! 0x%02x\n",rx_buf[1]);

  // release MC100EP446 SYNC
  printf("releasing MC100EP446 SYNC...\n");
  tx_buf[ix=0] = 0x80 | (clksel<<2); // keep device 1 to the PLL (using device 0), and release MC100EP446 SYNC
  tx_buf[++ix] = 0x00;
  spit[0].len = ++ix;
  ret = ioctl(spifd[0], SPI_IOC_MESSAGE(1), &spit[0]);
  if(ret<0) {
    perror("[0] SPI transfer ioctl ERROR");
  }
  printf("[0] Received SPI buffer: ");
  for(k=0; k<spit[0].len;k++) {
    printf("%02x ",rx_buf[k]);
  }
  printf("\n");
  usleep(50000); // short wait

  // release LTC6951 SYNC
  printf("releasing LTC6951 SYNC...\n");
  tx_buf[ix=0] = 0x02*2+0;  // start write at register 0x02
  tx_buf[++ix] = 0x00;  // reg 2: SYNC deasserted; nothing powered down; for cal we use autocal so don't set it here
  spit[1].len = ++ix;
  ret = ioctl(spifd[1], SPI_IOC_MESSAGE(1), &spit[1]);
  if(ret<0) {
    perror("[1] SPI transfer ioctl ERROR");
  }
  // read and check
  tx_buf[0] = 0x00*2+1; // start read at register 00
  spit[1].len=21;
  ret = ioctl(spifd[1], SPI_IOC_MESSAGE(1), &spit[1]);
  if(ret<0) {
    perror("[1] SPI transfer ioctl ERROR");
  }
  printf("[1] Received SPI buffer: ");
  for(k=0; k<spit[1].len;k++) {
    printf("%02x ",rx_buf[k]);
  }
  printf("\n");
  if (rx_buf[1]==0x05)
    printf("LTC6951 status GOOD (locked, ALC ok, REF ok)\n");
  else
    printf("WARNING: LTC6951 status reads no good! 0x%02x\n",rx_buf[1]);
  usleep(50000); // short wait
  // end of LTC6951 setup

  // do MMCM reset bit...
  printf("doing FPGA MMCM reset...\n");
  // set device 1 to the VGA A (using device 0) & set pulse characteristics
  tx_buf[ix=0] = 0x48 | (clksel<<2);  // and assert MMCM reset
  tx_buf[++ix] = 0x00;
  spit[0].len = ++ix;
  ret = ioctl(spifd[0], SPI_IOC_MESSAGE(1), &spit[0]);
  if(ret<0) {
    perror("[0] SPI transfer ioctl ERROR");
  }
  printf("[0] Received SPI buffer: ");
  for(k=0; k<spit[0].len;k++) {
    printf("%02x ",rx_buf[k]);
  }
  printf("\n");
  usleep(50000);
  // set device 1 to the VGA A (using device 0) & set pulse characteristics
  tx_buf[ix=0] = 0x40 | (clksel<<2);  // release the MMCM reset
  tx_buf[++ix] = 0x00;
  spit[0].len = ++ix;
  ret = ioctl(spifd[0], SPI_IOC_MESSAGE(1), &spit[0]);
  if(ret<0) {
    perror("[0] SPI transfer ioctl ERROR");
  }
  printf("[0] Received SPI buffer: ");
  for(k=0; k<spit[0].len;k++) {
    printf("%02x ",rx_buf[k]);
  }
  printf("\n");
  usleep(50000);
  // clocks are all stable (one hopes) and we are addressed to VGA A, proceed with that

  // set gain
  tx_buf[ix=0] = 0x02;
  tx_buf[++ix] = atten[0]&0x3f;
  spit[1].len = ++ix;
  ret = ioctl(spifd[1], SPI_IOC_MESSAGE(1), &spit[1]);
  if(ret<0) {
    perror("[1] SPI transfer ioctl ERROR");
  }
  printf("[1] Received SPI buffer: ");
  for(k=0; k<spit[1].len;k++) {
    printf("%02x ",rx_buf[k]);
  }
  printf("\n");

  // read and check
  tx_buf[0] = 0x80; // start read at register 00
  spit[1].len=7;
  ret = ioctl(spifd[1], SPI_IOC_MESSAGE(1), &spit[1]);
  if(ret<0) {
    perror("[1] SPI transfer ioctl ERROR");
  }
  printf("[1] Received SPI buffer: ");
  for(k=0; k<spit[1].len;k++) {
    printf("%02x ",rx_buf[k]);
  }
  printf("\n");
  
  usleep(50000);
  // set device 1 to the VGA B (using device 0) & set pulse characteristics
  tx_buf[ix=0] = 0x20 | (clksel<<2);
  tx_buf[++ix] = 0x00;
  tx_buf[++ix]=init_delay[0]>>8;
  tx_buf[++ix]=init_delay[0]&0xff;
  tx_buf[++ix]=pp_delay[0]>>8;
  tx_buf[++ix]=pp_delay[0]&0xff;
  tx_buf[++ix]=p_width[0]>>8;
  tx_buf[++ix]=p_width[0]&0xff;
  tx_buf[++ix]=(npulses[0]-1)|(pol[0]<<7);
  spit[0].len = ++ix;
  ret = ioctl(spifd[0], SPI_IOC_MESSAGE(1), &spit[0]);
  if(ret<0) {
    perror("[0] SPI transfer ioctl ERROR");
  }
  printf("[0] Received SPI buffer: ");
  for(k=0; k<spit[0].len;k++) {
    printf("%02x ",rx_buf[k]);
  }
  printf("\n");
  usleep(50000);

  // set gain
  tx_buf[ix=0] = 0x02;
  tx_buf[++ix] = atten[1]&0x3f;
  spit[1].len = ++ix;
  ret = ioctl(spifd[1], SPI_IOC_MESSAGE(1), &spit[1]);
  if(ret<0) {
    perror("[1] SPI transfer ioctl ERROR");
  }
  printf("[1] Received SPI buffer: ");
  for(k=0; k<spit[1].len;k++) {
    printf("%02x ",rx_buf[k]);
  }
  printf("\n");

  // read and check
  tx_buf[0] = 0x80; // start read at register 00
  spit[1].len=7;
  ret = ioctl(spifd[1], SPI_IOC_MESSAGE(1), &spit[1]);
  if(ret<0) {
    perror("[1] SPI transfer ioctl ERROR");
  }
  printf("[1] Received SPI buffer: ");
  for(k=0; k<spit[1].len;k++) {
    printf("%02x ",rx_buf[k]);
  }
  printf("\n");

  close(spifd[1]);
  close(spifd[0]);
  return 0;
}

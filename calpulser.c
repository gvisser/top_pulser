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
  int k,ret;
  int init_delay=0,pp_delay=20,p_width=5,npulses=2,atten=32;

  double tic=0.6;
  while((c = getopt(argc, argv, "t:i:s:w:n:a:")) != -1) {
    switch(c) {
    case 't' :  // time tic in ns (1ns or 600ps, maybe 750, 800 added later))
      printf("-t isn't supported yet\n");
      break;
    case 'i' :  // initial delay in ns
      init_delay = (int) (atof(optarg)/tic + 0.5);
      break;
    case 's' :  // pulse-pulse delay in ns;  REQUIREMENT: pp_delay>=p_width+8  !!!
      pp_delay = (int) (atof(optarg)/tic + 0.5);
      break;
    case 'w' :  // pulse width in ns
      p_width = (int) (atof(optarg)/tic + 0.5);
      break;
    case 'n' :  // number of pulses
      npulses = atoi(optarg);
      break;
    case 'a' :  // amplitude in V
      atten = (int) (-20*log10(atof(optarg)/1.6) + 0.5);
      if (atten<0)
	atten=0;
      if (atten>32)
	atten=32;
      break;
    default :
      printf("invalid argument\n");
      break;
    }
  }

  printf("TOP pulser setup:\nNote: your selections were rounded to nearest actual values\n");
  printf("initial delay %d tics (%.2lf ns)\n",init_delay,init_delay*tic);
  if (npulses==1) {
    printf("1 pulse, width %d tics (%.2lf ns)\n",p_width,p_width*tic);
  }
  else {
    printf("%d pulses, ",npulses);
    if (pp_delay<p_width+8) {
      pp_delay = p_width+8;
      printf("\nWARNING: enforced minimum pulse-pulse delay\n");
    }
    printf("width %d tics (%.2lf ns), pulse-pulse delay %d tics (%.2lf ns)\n",
	   p_width,p_width*tic,pp_delay,pp_delay*tic);
  }
  printf("amplitude %.3lf V (atten %d dB)\n",1.6*pow(10.0,-atten/20.0),atten);
  printf("------doing setup------\n\n");
  
  ////////////////////////////////////////////////////////////////////////////////////
  
  memset(&spit,0,sizeof(spit));  // there are other fields, which should all be 0 !!
  spit[0].tx_buf = /*(unsigned long)*/(uint32_t) tx_buf;
  spit[0].rx_buf = (unsigned long) rx_buf;
  //spit.bits_per_word = 0;
  spit[0].speed_hz = 200000; // MHz, and seems to have good resolution at least for around few MHz
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
  //     10-0 not used
  //         to be added: ch B VGA, A/B MC100EP446 SYNC bit, A & B polarity bits, A & B enable bits
  //to be added: 32 bits trigger timer
  // 2,3:  A init_delay 0 to 2**16-1
  // 4,5:  A pp_delay 0 to 2**16-1
  // 6,7:  A p_width 0 to 2**16-1
  // 8:    A npulses-1, 0 to 2**4-1
  //-----------------------------------------------------------------------------------------------
  tx_buf[0] = 0x90; // set device 1 to the PLL (using device 0), and assert MC100EP446 SYNC
  tx_buf[1] = 0x00;
  tx_buf[2]=init_delay>>8;
  tx_buf[3]=init_delay&0xff;
  tx_buf[4]=pp_delay>>8;
  tx_buf[5]=pp_delay&0xff;
  tx_buf[6]=p_width>>8;
  tx_buf[7]=p_width&0xff;
  tx_buf[8]=npulses-1;
  spit[0].len = 9;
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
  // later we'll have a short selection of clock setups to choose between, just change the data here
  int pll_R=12;    // range 1 to 63
  int pll_N=500;//300;   // range 2 to 511 (with RA0=1)
  double pll_P=3;//4;  // allowed values 2, 2.5, 3, 3.5, 4
  // keep fVCO in range 4 - 5 GHz, and take care w/ B value, see datasheet
  // R=12, N=450, P=3 works for 1.5 GHz
  // R=12, N=500, P=3 for 1.666667 GHz (600 ps)
  // R=12, N=510, P=2.5 works for 1.7 GHz but beware w/ P=2.5 and Mx=1 the duty cycle is not 50%
  
  tx_buf[0] = 0x01*2+0;  // start write at register 0x01
  tx_buf[1] = 0xba;   // reg 1: STAT = not (ALCHI or ALCLO or (not LOCK) or (not REFOK)) = "PLLOK"
  tx_buf[2] = 0x04;   // reg 2: SYNC asserted; nothing powered down; for cal we use autocal so don't set it here
  tx_buf[3] = 0x7e;   // reg 3: ALC: during cal only, monitor level always, autocal enabled, RA0=1, low level ref input
  tx_buf[4] = 0x17;   // reg 4: B=12 (for fPFD=3.333 MHz), LKWIN 10.7ns, LKCNT 2048
  // it would seem I didn't really implement pll_R here -- FIX THIS! !! !!! !!!! !!!!!
  tx_buf[5] = 0x30 | ((pll_N&0x300)>>8); //0x31;    // |
  tx_buf[6] = (pll_N&0xff);    // | R=12, N=300
  tx_buf[7] = 0x05;   // reg 7: CP: no overrides, not WIDE, 5.6 mA
  tx_buf[8] = (((int)((pll_P-1.999)*2))&0x07)<<5;    // no mute
  tx_buf[9] = 0x80;   // reg 8: out0: sync enable, no mute, M=1
  tx_buf[10] = 0x00;  // reg 9: SN=0 SR=0
  tx_buf[11] = 0x80;  // reg 10: out1: sync enable, no mute, M=1
  tx_buf[12] = 0x00;  // reg 11: out1: delay 0
  tx_buf[13] = 0x80;  // reg 13: out2: sync enable, no mute, M=1
  tx_buf[14] = 0x00;  // reg 14: out2: delay 0
  tx_buf[15] = 0x80;  // reg 15: out3: sync enable, no mute, M=1
  // best polarity for various clocks has to be investigated still   !!! CAUTION !!!
  tx_buf[16] = 0x40;  // reg 16: out3: delay 0, let's invert (better FF clock timing?)
  tx_buf[17] = 0x83;  // reg 17: out4: sync enable, no mute, M=8
  tx_buf[18] = 0x06;  // reg 18: out4: delay 6 (this gets it phase aligned with out0-3)
  spit[1].len = 19;
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

  // do MC100EP446 SYNC stuff now (not implemented yet)
  printf("releasing MC100EP446 SYNC...\n");
  tx_buf[0] = 0x80; // keep device 1 to the PLL (using device 0), and release MC100EP446 SYNC
  tx_buf[1] = 0x00;
  tx_buf[2]=init_delay>>8;
  tx_buf[3]=init_delay&0xff;
  tx_buf[4]=pp_delay>>8;
  tx_buf[5]=pp_delay&0xff;
  tx_buf[6]=p_width>>8;
  tx_buf[7]=p_width&0xff;
  tx_buf[8]=npulses-1;
  spit[0].len = 9;
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
  tx_buf[0] = 0x02*2+0;  // start write at register 0x02
  tx_buf[1] = 0x00;  // reg 2: SYNC deasserted; nothing powered down; for cal we use autocal so don't set it here
  spit[1].len = 2;
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
  tx_buf[0] = 0x48;  // and asser MMCM reset
  tx_buf[1] = 0x00;
  tx_buf[2]=init_delay>>8;
  tx_buf[3]=init_delay&0xff;
  tx_buf[4]=pp_delay>>8;
  tx_buf[5]=pp_delay&0xff;
  tx_buf[6]=p_width>>8;
  tx_buf[7]=p_width&0xff;
  tx_buf[8]=npulses-1;
  spit[0].len = 9;
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
  tx_buf[0] = 0x40;  // release the MMCM reset
  tx_buf[1] = 0x00;
  tx_buf[2]=init_delay>>8;
  tx_buf[3]=init_delay&0xff;
  tx_buf[4]=pp_delay>>8;
  tx_buf[5]=pp_delay&0xff;
  tx_buf[6]=p_width>>8;
  tx_buf[7]=p_width&0xff;
  tx_buf[8]=npulses-1;
  spit[0].len = 9;
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

  // NOTE: FOR NOW, the channels will not be properly synchronized despite we did this probably correctly (TBD).
  // I snafu'd the board and used OUT0 for channel B serializer, but OUT0 does not mute for the LTC6951 SYNC, and so
  // channel B serializer has no chance to get in sync with channel A one. I could set RAO=0 for the LTC6951 but that will
  // cause problems to use FTSW trigger/clock input (I think). Or I could (maybe) patch the board to swap the clock lines
  // of OUT1 and OUT0, which will put OUT0 to one of the pulser FF's (which don't care about this snafu). Will try that.

  // set gain
  tx_buf[0] = 0x02;
  tx_buf[1] = atten&0x3f;
  spit[1].len=2;
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

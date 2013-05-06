/*----------------------------------------------------------------------------*
 *  Copyright (c) 2010        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    Authors: Bryan Moffit                                                   *
 *             moffit@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5660             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *
 *             Gerard Visser
 *             gvisser@indiana.edu
 *             Indiana University
 *                                                                            *
 *----------------------------------------------------------------------------*
 *
 * Description:
 *     Initial/development version of driver library and test code for 
 *     IU 125MSPS ADC for GlueX / Hall D
 *
 * SVN: $Rev$
 *
 *----------------------------------------------------------------------------*/

#ifndef __FA125LIB__
#define __FA125LIB__



#define FA125_MAX_BOARDS             20

struct fa125_a24_main {
  volatile const UINT32 id;                 // 0000
  volatile       UINT32 swapctl;            // 0004
  volatile const UINT32 version;            // 0008
  volatile       UINT32 csr;                // 000c
  volatile       UINT32 pwrctl;             // 0010
  volatile       UINT32 dacctl;             // 0014  NOTE WRITE ONLY (but no way to say that in C ??)
  volatile const UINT32 skip1[2];
  volatile const UINT32 serial[4];          // 0020 - 002c
  volatile const UINT32 temperature[2];     // 0030 - 0034
  volatile const UINT32 slot_ga;            // 0038 NOT IMPLEMENTED YET
  volatile const UINT32 a32_ba;             // 003c NOT IMPLEMENTED YET
  volatile const UINT32 skipabunch[1008];
};

struct fa125_a24_fe {
  volatile const UINT32 version;            // 0000
  volatile       UINT32 test;               // 0004
  volatile const UINT32 skip1[6];           //
  volatile const UINT32 adc_async[6];       // 0020 - 0034
  volatile const UINT32 skip2[2];
  volatile const UINT32 acqfifo[6];         // 0040 - 0054
  volatile const UINT32 skip3[2];
  volatile const UINT32 skipabunch[1000];
};

struct fa125_a24_proc {
  volatile const UINT32 version;            // 0000
  volatile       UINT32 csr;
  volatile       UINT32 test2;
  volatile       UINT32 noreg;
};

struct fa125_a24 {
  struct fa125_a24_main main;    // 0000 - 0ffc
  struct fa125_a24_fe   fe[12];  // 1000 - cffc
  struct fa125_a24_proc proc;    // d000 - dffc (actually could allow to fffc)
};


struct fa125_a32 {
  volatile const UINT32 thedata;  // placeholder here -- actually will
				  // be array (multiply-mapped FIFO
				  // read)
};

#define PWRCTL_KEY_ON        0x3000abcd
#define DACCTL_DACCS_MASK    0x00000001
#define DACCTL_DACSCLK_MASK  0x00000002
#define DACCTL_ADACSI_MASK   0x00000004
#define DACCTL_BDACSI_MASK   0x00000008

/* The FFA125 Address mask, as determined from the slot id */
#define FA125_A24_ADDR_MASK   0x00F80000

#define FA125_ID 0xfa12500

/* CSR register definitions */
#define FA125_CSR_BUSY         1<<0
#define FA125_CSR_CLEAR        1<<1
#define FA125_CSR_TEST_TRIGGER 1<<2

/* Define data types and masks */
#define FA125_DATA_FORMAT0     0<<13
#define FA125_DATA_FORMAT1     1<<13
#define FA125_DATA_FORMAT5     5<<13

#define FA125_DATA_END         0xfe00
#define FA125_PAD_WORD         0xf1ea

#define FA125_EVNUM_MASK         0x07f0
#define FA125_DATA_CHANNEL_MASK  0xfc00
#define FA125_DATA_FORMAT_MASK   0xe000
#define FA125_DATA_RAW_MASK      0x3fff
#define FA125_DATA_OVERFLOW_MASK 0x8000


/* MASK where each bit corresponds to a unique slot number.
   each bit is 1 if the FA125 module exists (with correct firmware) */
volatile unsigned int FA125_SLOT_MASK=0;

const int DAC_CHAN_OFFSET[72]=
  {34, 33, 32, 39, 38, 37, 36, 27, 26, 25, 24, 31,
   74, 73, 72, 79, 78, 77, 76, 67, 66, 65, 64, 71,
   30, 29, 28, 18, 17, 16, 23, 22, 21, 20, 10, 9,
   70, 69, 68, 58, 57, 56, 63, 62, 61, 60, 50, 49,
   8, 15, 14, 13, 12, 2, 1, 0, 7, 6, 5, 4,
   48, 55, 54, 53, 52, 42, 41, 40, 47, 46, 45, 44};

const int DAC_CHAN_PULSER[3]={35, 19, 11};

const int DAC_CHAN_MULTH=3;

struct fa125_handle {
  struct fa125_a24 *a24;
  struct fa125_a32 *a32;
};

int  fa125Init (UINT32 addr, int iFlag);
void fa125PowerOff(int id);
void fa125PowerOn(int id);
void fa125SetLTC2620(int id, int dacChan, int dacData);
void fa125SetOffset(int id, int chan, int dacData);
int  fa125SetOffsetFromFile(int id, char *filename);
void fa125SetPulserAmplitude(int id, int chan, int dacData);
void fa125SetMulThreshold(int id, int dacData);
void fa125PrintTemps(int id);
int  fa125Poll(int id);
unsigned int fa125GetBerrCount();
void fa125Clear(int id);
int  fa125ReadEvent(int id, volatile UINT32 *data, int nwrds, unsigned int rflag);

#endif /* __FA125LIB__ */

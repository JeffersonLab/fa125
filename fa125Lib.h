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
 *     Driver library header for readout of the 125MSPS ADC using vxWorks 5.5 
 *     (or later) or Intel based single board computer
 *
 * SVN: $Rev$
 *
 *----------------------------------------------------------------------------*/

#ifndef __FA125LIB__
#define __FA125LIB__

#define FA125_MAX_BOARDS             20

struct fa125_a24_main 
{
  /* 0x0000 */ volatile const UINT32 id;
  /* 0x0004 */ volatile       UINT32 swapctl;
  /* 0x0008 */ volatile const UINT32 version;
  /* 0x000C */ volatile       UINT32 clock;
  /* 0x0010 */ volatile       UINT32 pwrctl;
  /* 0x0014 */ volatile       UINT32 dacctl;
  /* 0x0018 */                UINT32 blank0[(0x20-0x18)/4];
  /* 0x0020 */ volatile const UINT32 serial[4];
  /* 0x0030 */ volatile const UINT32 temperature[2];
  /* 0x0038 */ volatile const UINT32 slot_ga;
  /* 0x003C */ volatile const UINT32 a32_ba;
  /* 0x0040 */ volatile       UINT32 blockCSR;
  /* 0x0044 */ volatile       UINT32 ctrl1;
  /* 0x0048 */ volatile       UINT32 adr32;
  /* 0x004C */ volatile       UINT32 adr_mb;
  /* 0x0050 */ volatile       UINT32 busy_level;
  /* 0x0054 */ volatile       UINT32 block_count;
  /* 0x0058 */                UINT32 blank1[(0x1000-0x58)/4];
};

struct fa125_a24_fe 
{
  /* 0xN000 */ volatile const UINT32 version;
  /* 0xN004 */ volatile       UINT32 test;
  /* 0xN008 */                UINT32 blank0[(0x20-0x08)/4];
  /* 0xN020 */ volatile const UINT32 adc_async[6];
  /* 0xN038 */                UINT32 blank1[(0x40-0x38)/4];
  /* 0xN040 */ volatile const UINT32 acqfifo[6];
  /* 0xN058 */                UINT32 blank2[(0x1000-0x58)/4];
};

struct fa125_a24_proc 
{
  /* 0xD000 */ volatile const UINT32 version;
  /* 0xD004 */ volatile       UINT32 csr;
  /* 0xD008 */ volatile       UINT32 trigsrc;
  /* 0xD00C */ volatile       UINT32 ctrl2;
  /* 0xD010 */                UINT32 blank0;
  /* 0xD014 */ volatile       UINT32 blocklevel;
  /* 0xD018 */ volatile       UINT32 trig_count;
  /* 0xD01C */ volatile       UINT32 ev_count;
  /* 0xD020 */ volatile       UINT32 clock125_count;
  /* 0xD024 */ volatile       UINT32 sync_count;
  /* 0xD028 */ volatile       UINT32 trig2_count;
};

struct fa125_a24 
{
  /* 0x0000 */ struct fa125_a24_main main;
  /* 0x1000 */ struct fa125_a24_fe   fe[12];
  /* 0xD000 */ struct fa125_a24_proc proc;
};


struct fa125_a32 
{
  /* 0x0000 */ volatile const UINT32 data;
};

#define FA125_ID                   0xADC12500

#define FA125_MAIN_SUPPORTED_FIRMWARE   0xaaaa0001
#define FA125_PROC_SUPPORTED_FIRMWARE   0xaaaa0001
#define FA125_FE_SUPPORTED_FIRMWARE     0xaaaa0001

/* pwrctl register definitions */
#define FA125_PWRCTL_KEY_ON        0x3000ABCD

/* dacctl register definitions */
#define FA125_DACCTL_DACCS_MASK    (1<<0)
#define FA125_DACCTL_DACSCLK_MASK  (1<<1)
#define FA125_DACCTL_ADACSI_MASK   (1<<2)
#define FA125_DACCTL_BDACSI_MASK   (1<<3)

#ifdef DOESNOTEXIST
/* main CSR register definitions */
#define FA125_MAIN_CSR_TEST_TRIGGER (1<<2)
#endif

/* slot_ga register definitions */
#define FA125_SLOT_GA_MASK         0x0000001F

/* clock register definitions */
#define FA125_CLOCK_P2               (0)
#define FA125_CLOCK_P0               (1)
#define FA125_CLOCK_INTERNAL_ENABLE  (2)
#define FA125_CLOCK_INTERNAL         (3)

/* blockCSR register definitions */
#define FA125_BLOCKCSR_BLOCK_READY      (1<<2)
#define FA125_BLOCKCSR_BERR_ASSERTED    (1<<3)
#define FA125_BLOCKCSR_HAS_TOKEN        (1<<4)
#define FA125_BLOCKCSR_TAKE_TOKEN       (1<<5)
#define FA125_BLOCKCSR_PULSE_SYNC_RESET (1<<6)
#define FA125_BLOCKCSR_PULSE_TRIGGER    (1<<7)
#define FA125_BLOCKCSR_PULSE_SOFT_RESET (1<<8)
#define FA125_BLOCKCSR_PULSE_HARD_RESET (1<<9)

/* ctrl1 register definitions */
#define FA125_CTRL1_SYNCRESET_SOURCE_MASK 0x3
#define FA125_CTRL1_SYNCRESET_P0          (0)
#define FA125_CTRL1_SYNCRESET_VME         (2)
#define FA125_CTRL1_ENABLE_BERR           (1<<2)
#define FA125_CTRL1_ENABLE_MULTIBLOCK     (1<<3)
#define FA125_CTRL1_FIRST_BOARD           (1<<4)
#define FA125_CTRL1_LAST_BOARD            (1<<5)

/* adr32 register definitions */
#define FA125_ADR32_BASE_MASK       0x0000FF80
#define FA125_ADR32_ENABLE          (1<<0)

/* adr_mb register definitions */
#define FA125_ADRMB_ENABLE          (1<<0)
#define FA125_ADRMB_MIN_MASK        0x0000FF80
#define FA125_ADRMB_MAX_MASK        0xFF800000

/* busy_level register definitions */
#define FA125_BUSYLEVEL_MASK        0x000FFFFF
#define FA125_BUSYLEVEL_FORCE_BUSY  (1<<31)

/* block_count register definitions */
#define FA125_BLOCKCOUNT_MASK       0x000FFFFF

/* proc CSR register definitions */
#define FA125_PROC_CSR_BUSY               (1<<0)
#define FA125_PROC_CSR_CLEAR              (1<<1)
#define FA125_PROC_CSR_RESET              (1<<2)

#define FA125_TRIGSRC_TRIGGER_MASK           0x00000003
#define FA125_TRIGSRC_TRIGGER_P0             (0<<0)
#define FA125_TRIGSRC_TRIGGER_INTERNAL_TIMER (1<<0)
#define FA125_TRIGSRC_TRIGGER_INTERNAL_SUM   (1<<1)
#define FA125_TRIGSRC_TRIGGER_P2             ((1<<1)|(1<<0))

/* proc ctrl2 register definitions */
#define FA125_PROC_CTRL2_TRIGGER_ENABLE      (1<<0)
#define FA125_PROC_CTRL2_SYNCRESET_ENABLE    (1<<1)

/* proc blocklevel register definitions */
#define FA125_PROC_BLOCKLEVEL_MASK    0x0000FFFF

/* proc trig_count register definitions */
#define FA125_PROC_TRIGCOUNT_MASK  0x7FFFFFFF
#define FA125_PROC_TRIGCOUNT_RESET (1<<31)

/* proc ev_count register definitions */
#define FA125_PROC_EVCOUNT_MASK  0x00FFFFFF

/* proc clock125_count register definitions */
#define FA125_PROC_CLOCK125COUNT_MASK  0xFFFFFFFF
#define FA125_PROC_CLOCK125COUNT_RESET 0

/* proc sync_count register definitions */
#define FA125_PROC_SYNCCOUNT_MASK  0xFFFFFFFF
#define FA125_PROC_SYNCCOUNT_RESET 0

/* proc trig2_count register definitions */
#define FA125_PROC_TRIG2COUNT_MASK  0xFFFFFFFF
#define FA125_PROC_TRIG2COUNT_RESET 0

/* Define data types and masks */
#define FA125_DATA_FORMAT0     (0<<13)
#define FA125_DATA_FORMAT1     (1<<13)
#define FA125_DATA_FORMAT5     (5<<13)

#define FA125_DATA_END         0xFE00
#define FA125_PAD_WORD         0xF1EA

#define FA125_EVNUM_MASK         0x07F0
#define FA125_DATA_CHANNEL_MASK  0xFC00
#define FA125_DATA_FORMAT_MASK   0xE000
#define FA125_DATA_RAW_MASK      0x3FFFF
#define FA125_DATA_OVERFLOW_MASK 0x8000

int  fa125Init(UINT32 addr, UINT32 addr_inc, int nadc, int iFlag);
int  fa125Status(int id);
int  fa125Slot(unsigned int i);
int  fa125SetByteSwap(int id, int enable);
int  fa125PowerOff(int id);
int  fa125PowerOn(int id);
#ifdef DOESNOTEXIST
int  fa125SetTestTrigger (int id, int mode);
#endif
int  fa125SetLTC2620(int id, int dacChan, int dacData);
int  fa125SetOffset(int id, int chan, int dacData);
int  fa125SetOffsetFromFile(int id, char *filename);
unsigned short fa125ReadOffset(int id, int chan);
int  fa125ReadOffsetToFile(int id, char *filename);
int  fa125SetPulserAmplitude(int id, int chan, int dacData);
int  fa125SetMulThreshold(int id, int dacData);
int  fa125PrintTemps(int id);
int  fa125SetClockSource(int id, int clksrc);
int  fa125SetTriggerSource(int id, int trigsrc);
int  fa125Poll(int id);
unsigned int fa125GetBerrCount();
int  fa125Clear(int id);
int  fa125ReadEvent(int id, volatile UINT32 *data, int nwrds, unsigned int rflag);

#endif /* __FA125LIB__ */

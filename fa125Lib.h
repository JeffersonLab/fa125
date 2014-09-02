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
 *----------------------------------------------------------------------------*/

#ifndef __FA125LIB__
#define __FA125LIB__

#define FA125_MAX_BOARDS             20
#define FA125_MAX_A32_MEM      0x800000   /* 8 Meg */
#define FA125_MAX_A32MB_SIZE   0x800000  /*  8 MB */

#define FA125_MAX_ADC_CHANNELS       72
#define FA125_MAX_DATA_PER_CHANNEL    8

struct fa125_a24_main 
{
  /* 0x0000 */ volatile UINT32 id;
  /* 0x0004 */ volatile UINT32 swapctl;
  /* 0x0008 */ volatile UINT32 version;
  /* 0x000C */ volatile UINT32 clock;
  /* 0x0010 */ volatile UINT32 pwrctl;
  /* 0x0014 */ volatile UINT32 dacctl;
  /* 0x0018 */          UINT32 blank0[(0x20-0x18)/4];
  /* 0x0020 */ volatile UINT32 serial[4];
  /* 0x0030 */ volatile UINT32 temperature[2];
  /* 0x0038 */ volatile UINT32 slot_ga;
  /* 0x003C */ volatile UINT32 a32_ba;
  /* 0x0040 */ volatile UINT32 blockCSR;
  /* 0x0044 */ volatile UINT32 ctrl1;
  /* 0x0048 */ volatile UINT32 adr32;
  /* 0x004C */ volatile UINT32 adr_mb;
  /* 0x0050 */ volatile UINT32 busy_level;
  /* 0x0054 */ volatile UINT32 block_count;
  /* 0x0058 */ volatile UINT32 configCSR;
  /* 0x005C */ volatile UINT32 configAdrData;
  /* 0x0060 */          UINT32 blank1[(0x1000-0x60)/4];
};

struct fa125_a24_fe 
{
  /* 0xN000 */ volatile UINT32 version;
  /* 0xN004 */ volatile UINT32 test;
  /* 0xN008 */          UINT32 blank0[(0x20-0x08)/4];
  /* 0xN020 */ volatile UINT32 adc_async[6];
  /* 0xN038 */          UINT32 blank1[(0x40-0x38)/4];
  /* 0xN040 */ volatile UINT32 acqfifo[6];
  /* 0xN058 */ volatile UINT32 ptw;
  /* 0xN05C */ volatile UINT32 pl;
  /* 0xN060 */ volatile UINT32 ptw_last_adr;
  /* 0xN064 */ volatile UINT32 ptw_max_buf;
  /* 0xN068 */ volatile UINT32 nsb;
  /* 0xN06C */ volatile UINT32 nsa;
  /* 0xN070 */ volatile UINT32 threshold[6];
  /* 0xN088 */ volatile UINT32 config1;
  /* 0xN08C */ volatile UINT32 trig_count;
  /* 0xN090 */ volatile UINT32 config2;
  /* 0xN094 */ volatile UINT32 test_waveform;
  /* 0xN098 */          UINT32 blank2[(0x1000-0x98)/4];
};

struct fa125_a24_proc 
{
  /* 0xD000 */ volatile UINT32 version;
  /* 0xD004 */ volatile UINT32 csr;
  /* 0xD008 */ volatile UINT32 trigsrc;
  /* 0xD00C */ volatile UINT32 ctrl2;
  /* 0xD010 */ volatile UINT32 softtrig;
  /* 0xD014 */ volatile UINT32 blocklevel;
  /* 0xD018 */ volatile UINT32 trig_count;
  /* 0xD01C */ volatile UINT32 ev_count;
  /* 0xD020 */ volatile UINT32 clock125_count;
  /* 0xD024 */ volatile UINT32 sync_count;
  /* 0xD028 */ volatile UINT32 trig2_count;
  /* 0xD02C */ volatile UINT32 pulser_control;
  /* 0xD030 */ volatile UINT32 pulser_trig_delay;
};

struct fa125_a24 
{
  /* 0x0000 */ struct fa125_a24_main main;
  /* 0x1000 */ struct fa125_a24_fe   fe[12];
  /* 0xD000 */ struct fa125_a24_proc proc;
};


struct fa125_a32 
{
  /* 0x0000 */ volatile UINT32 data;
};

#define FA125_ID                   0xADC12500

#define FA125_MAIN_SUPPORTED_FIRMWARE   0x00010202
#define FA125_PROC_SUPPORTED_FIRMWARE   0x00010202
#define FA125_FE_SUPPORTED_FIRMWARE     0x00010202

/* 0x10 pwrctl register definitions */
#define FA125_PWRCTL_KEY_ON        0x3000ABCD

/* 0x14 dacctl register definitions */
#define FA125_DACCTL_DACCS_MASK    (1<<0)
#define FA125_DACCTL_DACSCLK_MASK  (1<<1)
#define FA125_DACCTL_ADACSI_MASK   (1<<2)
#define FA125_DACCTL_BDACSI_MASK   (1<<3)

#ifdef DOESNOTEXIST
/* main CSR register definitions */
#define FA125_MAIN_CSR_TEST_TRIGGER (1<<2)
#endif

/* 0x0c clock register definitions */
#define FA125_CLOCK_P2               (0)
#define FA125_CLOCK_P0               (1)
#define FA125_CLOCK_INTERNAL_ENABLE  (2)
#define FA125_CLOCK_INTERNAL         (3)
#define FA125_CLOCK_MASK             0xF

/* 0x38 slot_ga register definitions */
#define FA125_SLOT_GA_MASK         0x0000001F

/* 0x40 blockCSR register definitions */
#define FA125_BLOCKCSR_BLOCK_READY      (1<<2)
#define FA125_BLOCKCSR_BERR_ASSERTED    (1<<3)
#define FA125_BLOCKCSR_HAS_TOKEN        (1<<4)
#define FA125_BLOCKCSR_TAKE_TOKEN       (1<<5)
#define FA125_BLOCKCSR_PULSE_SYNC_RESET (1<<6)
#define FA125_BLOCKCSR_PULSE_TRIGGER    (1<<7)
#define FA125_BLOCKCSR_PULSE_SOFT_RESET (1<<8)
#define FA125_BLOCKCSR_PULSE_HARD_RESET (1<<9)


/* 0x44 ctrl1 register definitions */
#define FA125_CTRL1_ENABLE_BERR           (1<<2)
#define FA125_CTRL1_ENABLE_MULTIBLOCK     (1<<3)
#define FA125_CTRL1_FIRST_BOARD           (1<<4)
#define FA125_CTRL1_LAST_BOARD            (1<<5)

/* 0x48 adr32 register definitions */
#define FA125_ADR32_BASE_MASK       0x0000FF80
#define FA125_ADR32_ENABLE          (1<<0)

/* 0x4c adr_mb register definitions */
#define FA125_ADRMB_ENABLE          (1<<0)
#define FA125_ADRMB_MIN_MASK        0x0000FF80
#define FA125_ADRMB_MAX_MASK        0xFF800000

/* 0x50 busy_level register definitions */
#define FA125_BUSYLEVEL_MASK        0x000FFFFF
#define FA125_BUSYLEVEL_FORCE_BUSY  (1<<31)

/* 0x54 block_count register definitions */
#define FA125_BLOCKCOUNT_MASK       0x000FFFFF

/* 0x58 configCSR register definitions */
#define FA125_CONFIGCSR_DATAREAD_MASK  0x000000FF
#define FA125_CONFIGCSR_BUSY           (1<<8)
#define FA125_CONFIGCSR_OPCODE_MASK    0x07000000
#define FA125_CONFIGCSR_PROG_ENABLE    (1<<31)

/* 0x5C configAdrData register definitions */
#define FA125_CONFIGADRDATA_DATA_MASK    0x000000FF
#define FA125_CONFIGADRDATA_BYTEADR_MASK 0x0003FF00
#define FA125_CONFIGADRDATA_PAGEADR_MASK 0x7FFC0000
#define FA125_CONFIGADRDATA_EXEC         (1<<31)

/* 0x1004 FE test definitions */
#define FA125_FE_TEST_COLLECT_ON       (1<<1)
#define FA125_FE_TEST_SYNCRESET_ENABLE (1<<2)

/* 0x1058 FE ptw register defintions */
#define FA125_FE_PTW_MASK          0x000000FF

/* 0x105C FE pl register defintions */
#define FA125_FE_PL_MASK           0x0000FFFF

/* 0x1060 FE ptw_last_adr register defintions */
#define FA125_FE_PTW_LAST_ADR_MASK 0x00000FFF

/* 0x1064 FE ptw_max_buf register defintions */
#define FA125_FE_PTW_MAX_BUF_MASK  0x000000FF

/* 0x1068 FE NSB register defintions */
#define FA125_FE_NSB_MASK          0x00001FFF

/* 0x106C FE NSA register defintions */
#define FA125_FE_NSA_MASK          0x00003FFF

/* 0xN070 - 0xN084 threshold register defintions */
#define FA125_FE_THRESHOLD_MASK          0x00000FFF

/* 0x1088 FE config1 defintions */
#define FA125_FE_CONFIG1_MASK            0x000000FF
#define FA125_FE_CONFIG1_MODE_MASK       0x00000007
#define FA125_FE_CONFIG1_ENABLE          (1<<3)
#define FA125_FE_CONFIG1_NPULSES_MASK    0x00000060
#define FA125_FE_CONFIG1_PLAYBACK_ENABLE (1<<7)

/* 0xN08C FE trig_count definitions */
#define FA125_FE_TRIG_COUNT_MASK  0x0000FFFF

/* 0xN090 FE config2 definitions */
#define FA125_FE_CONFIG2_CH_MASK  0x0000003F

/* 0xN094 FE test_waveform definitions */
#define FA125_FE_TEST_WAVEFORM_PPG_DATA_MASK  0x00000FFF
#define FA125_FE_TEST_WAVEFORM_OVERFLOW       (1<<12)
#define FA125_FE_TEST_WAVEFORM_WRITE_PPG_DATA (1<<15)
#define FA125_PPG_MAX_SAMPLES                 32*6

/* 0xD004 proc CSR register definitions */
#define FA125_PROC_CSR_BUSY               (1<<0)
#define FA125_PROC_CSR_CLEAR              (1<<1)
#define FA125_PROC_CSR_RESET              (1<<2)

/* 0xD008 proc trigsrc register definitions */
#define FA125_TRIGSRC_TRIGGER_MASK           0x00000003
#define FA125_TRIGSRC_TRIGGER_P0             (0<<0)
#define FA125_TRIGSRC_TRIGGER_SOFTWARE       (1<<0)
#define FA125_TRIGSRC_TRIGGER_INTERNAL_SUM   (1<<1)
#define FA125_TRIGSRC_TRIGGER_P2             ((1<<1)|(1<<0))

/* 0xD00C proc ctrl2 register definitions */
#define FA125_PROC_CTRL2_TRIGGER_ENABLE        (1<<0)
#define FA125_PROC_CTRL2_SYNCRESET_SOURCE_MASK 0xC
#define FA125_PROC_CTRL2_SYNCRESET_P0          (0<<2)
#define FA125_PROC_CTRL2_SYNCRESET_VME         (2<<2)

/* 0xD014 proc blocklevel register definitions */
#define FA125_PROC_BLOCKLEVEL_MASK    0x0000FFFF

/* 0xD018 proc trig_count register definitions */
#define FA125_PROC_TRIGCOUNT_MASK  0xFFFFFFFF
#define FA125_PROC_TRIGCOUNT_RESET 1

/* 0xD01C proc ev_count register definitions */
#define FA125_PROC_EVCOUNT_MASK  0x00FFFFFF

/* 0xD020 proc clock125_count register definitions */
#define FA125_PROC_CLOCK125COUNT_MASK  0xFFFFFFFF
#define FA125_PROC_CLOCK125COUNT_RESET 1

/* 0xD024 proc sync_count register definitions */
#define FA125_PROC_SYNCCOUNT_MASK  0xFFFFFFFF
#define FA125_PROC_SYNCCOUNT_RESET 1

/* 0xD028 proc trig2_count register definitions */
#define FA125_PROC_TRIG2COUNT_MASK  0xFFFFFFFF
#define FA125_PROC_TRIG2COUNT_RESET 1

/* 0xD02C pulser_control register definitions */
#define FA125_PROC_PULSER_CONTROL_PULSE            (1<<0)
#define FA125_PROC_PULSER_CONTROL_DELAYED_TRIGGER  (1<<1)

/* 0xD030 pulser_trig_delay register definitions */
#define FA125_PROC_PULSER_TRIG_DELAY_MASK   0x00000FFF
#define FA125_PROC_PULSER_WIDTH_MASK        0x00FFF000

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

#define FA125_DUMMY_DATA             0xf800fafa
#define FA125_DATA_TYPE_DEFINE       0x80000000
#define FA125_DATA_TYPE_MASK         0x78000000

#define FA125_DATA_BLOCK_HEADER      0x00000000
#define FA125_DATA_BLOCK_TRAILER     0x08000000
#define FA125_DATA_BLKNUM_MASK       0x0000003f

/* Define Firmware updating OPCODEs */
#define FA125_OPCODE_BUFFER_WRITE   0
#define FA125_OPCODE_MAIN_READ      1
#define FA125_OPCODE_BUFFER_READ    2
#define FA125_OPCODE_BUFFER_PUSH    3
#define FA125_OPCODE_ERASE          4

/* Define other firmware updating macros */
#define FA125_FIRMWARE_MAX_PAGES 8*1024
#define FA125_FIRMWARE_MAX_BYTE_PER_PAGE 528

/* Define Firmware DEBUG types */
typedef enum
  {
    FA125_FIRMWARE_DEBUG_WRITE_BUFFER      = (1<<0),
    FA125_FIRMWARE_DEBUG_MCS_FILE          = (1<<1),
    FA125_FIRMWARE_DEBUG_WAIT_FOR_READY    = (1<<2),
    FA125_FIRMWARE_DEBUG_MCS_SKIPPED_LINES = (1<<3),
    FA125_FIRMWARE_DEBUG_VERIFY_ERASE      = (1<<4),
#ifndef VXWORKSPPC  
    FA125_FIRMWARE_DEBUG_MEASURE_TIMES     = (1<<5)
#endif
  } FA125_FIRMWARE_DEBUG_FLAGS;

/* fa125Init initialization flag bits */
#define FA125_INIT_VXS_TRIG            (0<<1)
#define FA125_INIT_INT_TIMER_TRIG      (1<<1)
#define FA125_INIT_INT_SUM_TRIG        (1<<2)
#define FA125_INIT_P2_TRIG             ((1<<1) | (1<<2))
#define FA125_INIT_P2_CLKSRC           (0<<4)
#define FA125_INIT_VXS_CLKSRC          (1<<4)
#define FA125_INIT_INT_CLKSRC          (1<<5)
#define FA125_INIT_SKIP                (1<<16)
#define FA125_INIT_USE_ADDRLIST        (1<<17)
#define FA125_INIT_SKIP_FIRMWARE_CHECK (1<<18)

/* fa125Status flags */
#define FA125_STATUS_SHOWREGS          (1<<0)

typedef enum
  {
    FA125_FIRMWARE_ERROR_ERASE            = (1<<0),
    FA125_FIRMWARE_ERROR_VERIFY_ERASE     = (1<<1),
    FA125_FIRMWARE_ERROR_WRITE            = (1<<2),
    FA125_FIRMWARE_ERROR_PUSH             = (1<<3),
    FA125_FIRMWARE_ERROR_PUSH_WAIT        = (1<<4),
    FA125_FIRMWARE_ERROR_VERIFY_WRITE     = (1<<5)
  } FA125_FIRMWARE_ERROR_FLAGS;

/* Default, maximum and minimum ADC Processing parameters */
#define FA125_DEFAULT_PL     50
#define FA125_DEFAULT_PTW    50
#define FA125_DEFAULT_NSB     5
#define FA125_DEFAULT_NSA    10
#define FA125_DEFAULT_NP      3

#define FA125_MAX_PL       1000
#define FA125_MAX_PTW       512
#define FA125_MAX_NSB       500
#define FA125_MAX_NSA       500
#define FA125_MAX_NP          3

/* Processing Modes */
#define FA125_PROC_MODE_RAWWINDOW          1
#define FA125_PROC_MODE_PULSERAW           2
#define FA125_PROC_MODE_INTEGRAL           3
#define FA125_PROC_MODE_TDC                4
#define FA125_PROC_MODE_RAWDATA_TDC        8
#define FA125_SUPPORTED_MODES FA125_PROC_MODE_RAWWINDOW,FA125_PROC_MODE_PULSERAW,FA125_PROC_MODE_INTEGRAL,FA125_PROC_MODE_TDC,FA125_PROC_MODE_RAWDATA_TDC
#define FA125_SUPPORTED_NMODES 5

extern const char *fa125_mode_names[FA125_SUPPORTED_NMODES];

/* fadcBlockError values */
typedef enum
  {
    FA125_BLOCKERROR_NO_ERROR          = 0,
    FA125_BLOCKERROR_TERM_ON_WORDCOUNT = 1,
    FA125_BLOCKERROR_UNKNOWN_BUS_ERROR = 2,
    FA125_BLOCKERROR_ZERO_WORD_COUNT   = 3,
    FA125_BLOCKERROR_DMADONE_ERROR     = 4,
    FA125_BLOCKERROR_NTYPES            = 5
  } FA125_BLOCKERROR_FLAGS;

extern const char *fa125_blockerror_names[FA125_BLOCKERROR_NTYPES];

int  fa125Init(UINT32 addr, UINT32 addr_inc, int nadc, int iFlag);
int  fa125Status(int id, int pflag);
void fa125GStatus(int pflag);
int  fa125SetProcMode(int id, int pmode, unsigned int PL, unsigned int PTW, 
		      unsigned int NSB, unsigned int NSA, unsigned int NP);
int  fa125Slot(unsigned int i);
int  fa125SetByteSwap(int id, int enable);
int  fa125PowerOff(int id);
int  fa125PowerOn(int id);
int  fa125SetOffset(int id, int chan, int dacData);
int  fa125SetOffsetFromFile(int id, char *filename);
unsigned short fa125ReadOffset(int id, int chan);
int  fa125ReadOffsetToFile(int id, char *filename);
int  fa125SetThreshold(int id, unsigned short tvalue, unsigned short chan);
int  fa125SetChannelDisable(int id, int channel);
int  fa125SetChannelDisableMask(int id, unsigned int cmask0, unsigned int cmask1, unsigned int cmask2);
int  fa125SetChannelEnable(int id, int channel);
int  fa125SetChannelEnableMask(int id, unsigned int cmask0, unsigned int cmask1, unsigned int cmask2);
int  fa125SetCommonThreshold(int id, unsigned short tvalue);
void fa125GSetCommonThreshold(unsigned short tvalue);
int  fa125PrintThreshold(int id);
int  fa125SetPulserAmplitude(int id, int chan, int dacData);
int  fa125SetMulThreshold(int id, int dacData);
int  fa125PrintTemps(int id);
int  fa125SetClockSource(int id, int clksrc);
int  fa125SetTriggerSource(int id, int trigsrc);
int  fa125GetTriggerSource(int id);
int  fa125SetSyncResetSource(int id, int srsrc);
int  fa125Poll(int id);
unsigned int fa125GetBerrCount();
int  fa125Clear(int id);
int  fa125Enable(int id);
int  fa125Disable(int id);
int  fa125Reset(int id, int reset);
int  fa125ResetCounters(int id);
int  fa125ResetToken(int id);
int  fa125GetTokenMask();
int  fa125SetBlocklevel(int id, int blocklevel);
int  fa125SoftTrigger(int id);
int  fa125SetPulserTriggerDelay(int id, int delay);
int  fa125SetPulserWidth(int id, int width);
int  fa125SoftPulser(int id, int output);
int  fa125SetPPG(int id, int fe_chip, unsigned short *sdata, int nsamples);
int  fa125PPGEnable(int id);
int  fa125PPGDisable(int id);
int  fa125ReadEvent(int id, volatile UINT32 *data, int nwrds, unsigned int rflag);
int  fa125Bready(int id);
unsigned int fa125GBready();
unsigned int fa125ScanMask();
int  fa125ReadBlockStatus(int pflag);
int  fa125ReadBlock(int id, volatile UINT32 *data, int nwrds, int rflag);
void fa125DecodeData(unsigned int data);

/*  Firmware Updating Routine Prototypes */
void fa125FirmwareSetDebug(unsigned int debug);
int  fa125FirmwareGVerifyFull();
int  fa125FirmwareReadMcsFile(char *filename);
void fa125FirmwarePrintFPGAStats();
void fa125FirmwarePrintPage(int page);
int  fa125FirmwareEraseFull(int id);
int  fa125FirmwareGEraseFull();
int  fa125FirmwareWriteFull(int id);
int  fa125FirmwareGWriteFull();
void fa125FirmwarePrintTimes();
int  fa125FirmwareGCheckErrors();
#endif /* __FA125LIB__ */

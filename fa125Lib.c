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

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#ifdef VXWORKS
#include <vxWorks.h>
#include <logLib.h>
#include <taskLib.h>
#include <intLib.h>
#include <iv.h>
#else
#include <stdlib.h>
#include "jvme.h"
#endif
#include <pthread.h>
#include "fa125Lib.h"

#ifndef LSWAP
#define LSWAP(x)        ((((x) & 0x000000ff) << 24) | \
                         (((x) & 0x0000ff00) <<  8) | \
                         (((x) & 0x00ff0000) >>  8) | \
                         (((x) & 0xff000000) >> 24))
#endif

/* Mutex to guard TD read/writes */
pthread_mutex_t    fa125Mutex = PTHREAD_MUTEX_INITIALIZER;
#define FA125LOCK     if(pthread_mutex_lock(&fa125Mutex)<0) perror("pthread_mutex_lock");
#define FA125UNLOCK   if(pthread_mutex_unlock(&fa125Mutex)<0) perror("pthread_mutex_unlock");

/* Define global variables */
int nfa125=0;
volatile struct fa125_a24 *fa125p[(FA125_MAX_BOARDS+1)]; /* pointers to FA125 memory map */
volatile struct fa125_a32 *fa125pd[(FA125_MAX_BOARDS+1)]; /* pointers to FA125 FIFO memory */
int fa125ID[FA125_MAX_BOARDS]; /* array of slot numbers for FA125s */
int fa125A24Offset=0;                            /* Difference in CPU A24 Base and VME A24 Base */
unsigned int fadcAddrList[FA_MAX_BOARDS];            /* array of a24 addresses for FA125s */
int berr_count=0; /* A count of the number of BERR that have occurred when running fa125Poll() */

#define FA125A24BA 0xa10000   /* just hardcode here for now */

/*******************************************************************************
 *
 * fa125Init - Initialize IO 125MSPS ADC Library
 *
 *   Initialize a single FA125 at the specified VME A24.  Only one
 *   module is supported at the moment. (More to be supported through
 *   other routines)
 *
 *   iFlag: 17 bit integer
 *     16:  Exit before board initialization (just map structure pointer)
 *          0: Initialize fa125(s)
 *          1: Skip initialization
 *
 *     17:  Use fa125AddrList instead of addr and addr_inc
 *           for VME addresses.
 *          0: Initialize with addr and addr_inc
 *          1: Use fa125AddrList 
 */

int
fa125Init (UINT32 addr, UINT32 addr_inc, int nadc, int iFlag)
{
  int res;
  volatile unsigned int rdata=0;
  unsigned int laddr;
  volatile struct fa125_a24 *fa125;
  int useList=0, noBoardInit=0;
  int nfind=0, islot=0, FA_SLOT=0;
  unsigned int boardID=0;

  /* Check if we're skipping initialization, and just mapping the structure pointer */
  if(iFlag & (1<<16))
    noBoardInit=1;

  /* Check if we're initializing using a list */
  if(iFlag & (1<<17))
    useList=1;

  /* Check for valid address */
  if((addr==0) && (useList==0))
    { /* Loop through JLab Standard GEOADDR to VME addresses to make a list */
      useList=1;
      nfind=16;

      for(islot=3; islot<11; islot++) /* First 8 */
	fa125AddrList[islot-3] = (islot<<19);
      
      /* Skip Switch Slots */
      
      for(islot=13; islot<21; islot++) /* Last 8 */
	fa125AddrList[islot-5] = (islot<<19);

    }
  else if(addr > 0x00ffffff)  /* A32 Addressing */
    {
      printf("%s: ERROR: A32 Addressing not allowed for FA125 configuration space\n",
	     __FUNCTION__);
      return(ERROR);
    }
  else if((addr!=0) && (useList==0))
    { /* A24 Addressing */
      if(addr_inc==0) /* Just one module */
	{
	  fa125AddrList[0] = addr;
	  nfind=1;
	}
      else /* Up to nadc modules */
	{
	  for(islot=0; islot<nadc; islot++)
	    {
	      fa125AddrList[islot] = addr+addr_inc*islot;
	    }
	  nfind=nadc;
	}
    }

  /* Determine the A24 offset from the first module address */
#ifdef VXWORKS
  res = sysBusToLocalAdrs(0x39,(char *)fa125AddrList[0],(char **)&laddr);
#else
  res = vmeBusToLocalAdrs(0x39,(char *)fa125AddrList[0],&laddr);
#endif
  if (res != 0) 
    {
#ifdef VXWORKS
      printf("%s: ERROR in sysBusToLocalAdrs(0x39,0x%x,&laddr) \n",
	     __FUNCTION__,addr);
#else
      printf("%s: ERROR in vmeBusToLocalAdrs(0x39,0x%x,&laddr) \n",
	     __FUNCTION__,addr);
#endif
      return(ERROR);
    }

  fa125A24Offset = laddr - fa125AddrList[0];

  for(islot=0; islot<nfind; islot++)
    {
      
      fa125 = (volatile struct fa125_a24 *)(fa125AddrList[islot]);
      /* Check if Board exists at that address */
#ifdef VXWORKS
      res = vxMemProbe((char *) &(fa125->main.id),VX_READ,4,(char *)&rdata);
#else
      res = vmeMemProbe((char *)&(fa125->main.id),4,(char *)&rdata);
#endif
      if(res < 0) 
	{
	  printf("%s: ERROR: No addressable board at addr=0x%x\n",
		 __FUNCTION__,(UINT32) fa125);
	}
      else 
	{
	  /* Check that it is an FA125 (using main firmare version) 
	     ... with and without byte swapping */
	  if( ((rdata) != FA125_ID) &&
	      ((rdata) != LSWAP(FA125_ID)) )
	    {
	      printf("%s: ERROR: Invalid Board ID: 0x%x\n",
		     __FUNCTION__,rdata);
	    }
	  else
	    {
	      /* get the Geographic Address */
	      boardID = islot; /* Hack until we get a register that tells us */
	      fa125p[boardID] = (struct fa125_a24 *) fa125AddrList[islot];
	      fa125ID[nfa125] = boardID;

	      printf("Initialized FA125 %2d  Slot # %2d at address 0x%08x (0x%08x)\n",
		     __FUNCTION__,nfa125,fa125ID[nfa125],
		     (unsigned int)fa125p[fa125ID[nfa125]],
		     (unsigned int)fa125p[fa125ID[nfa125]] - fa125A24Offset);
	      
	    }
	  nfa125++;
	}

    }

  if(noBoardInit)
    {
      if(nfa125>0)
	{
	  printf("%s: %d FA125(s) successfully mapped (not initialized)\n",
		 __FUNCTION__,nfa125);
	  return OK;
	}
    }

  if(nfa125==0)
    {
      printf("%s: ERROR: Unable to initialize any FA125 modules\n",
	     __FUNCTION__);
      return ERROR;
    }

  /* Perform some initialization here */
  for(islot=0; islot<nfa125; islot++)
    {
      FA_SLOT = fa125ID[islot];
      fa125Clear(FA_SLOT);
      
#ifdef VXWORKS
      /* Turn off hardware byte swapping for vxWorks */
      fa125SetByteSwap(0);
#else
      /* Turn on hardware byte swapping for Linux */
      fa125SetByteSwap(1);
#endif

      /* Eventually toss this into fa125Status(...) */
      printf("  ID is 0x%08x\n",fa125p[FA_SLOT]->main.id);
      printf("  Main FPGA firmware version is %d\n",fa125p[FA_SLOT]->main.version);
      printf("  Main board serial 0x%04x%08x / mezzanine board serial 0x%04x%08x\n",
	     fa125p[FA_SLOT]->main.serial[0]&0x0000ffff,
	     fa125p[FA_SLOT]->main.serial[1],
	     fa125p[FA_SLOT]->main.serial[2]&0x0000ffff,
	     fa125p[FA_SLOT]->main.serial[3]);
      printf("  Processor FPGA firmware version is %d\n",fa125p[FA_SLOT]->proc.version);
    }

  return(OK);

}

int
fa125SetByteSwap(int id, int enable)
{
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      printf("%s: ERROR : FA125 in slot %d is not initialized \n",__FUNCTION__,id);
      return ERROR;
    }

  FA125LOCK;
  if(enable)
    fa125p[id]->main.swapct = 0xffffffff;
  else
    fa125p[id]->main.swapct = 0;
  FA125UNLOCK;

  return OK;
}

/*******************************************************************************
 *
 * fa125Slot2VmeAdrs - Determine VME Address from Crate Slot number
 *
 *    - Numerically convert a FA125 slot number into the expected A24 VME address
 *      The slot number is encoded as the Most Significant 5 bits of the A24 address
 *      determined by its GEOADDR at boottime.
 */

unsigned int
fa125Slot2VmeAdrs (int crateslot)
{
  unsigned int rval;

  if(crateslot<2 || crateslot>21)
    {
      printf("%s: Invalid slot number %d.\n",
	     __FUNCTION__,crateslot);
      rval =  0;
    }
  else
    {
      rval = (crateslot<<19)&FA125_A24_ADDR_MASK;
    }

  return rval;
}

/*******************************************************************************
 *
 * fa125GetSlotMask - Determine physical slot mask
 *
 *    - Brute force method of determining which FA125s exist in the crate
 *      and forming the FA125_SLOT_MASK.  This method increments from Slot 2 to 21
 *      determining if a FA125 returns the correct firmware.  If the firmware is
 *      incorrect or a BUS ERROR is returned, the slot in question will not be included
 *      in the FA125_SLOT_MASK.
 */

unsigned int
fa125GetSlotMask()
{
  unsigned int islot, addr, laddr;
  int res, rdata=0;
  volatile struct fa125_a24 *fa125;

  /* Loop over each slot*/
  for(islot=2; islot<22; islot++)
    {
      addr = fa125Slot2VmeAdrs(islot);
      if( (addr==0) || (addr > 0x00ffffff) ) 
	{
	  /* This should happen... but just in case */
	  printf("%s: ERROR: Invalid address (0x%8x) for slot %d\n",
		 __FUNCTION__,addr,islot);
	  continue;
	}
      else
	{
	  /* get the USERSPACE address from the VME A24 Address */
#ifdef VXWORKS
	  res = sysBusToLocalAdrs(0x39,(char *)addr,(char **)&laddr);
#else
	  res = vmeBusToLocalAdrs(0x39,(char *)addr,(char **)&laddr);
#endif
	  if (res != 0) /* Indication of bad map in {sys,vme}BusToLocalAdrs */
	    {
	      /* This should happen... but just in case */
	      printf("%s: ERROR in sysBusToLocalAdrs(0x39,0x%x,&laddr) \n",
		     __FUNCTION__,addr);
	      continue;
	    }
	  fa125 = (volatile struct fa125_a24 *)(laddr);
	  /* Check if Board exists at that address */
#ifdef VXWORKS
	  res = vxMemProbe((char *) &(fa125->main.id),VX_READ,4,(char *)&rdata);
#else
	  res = vmeMemProbe((char *) &(fa125->main.id),4,(char *)&rdata);
#endif
	  if(res < 0) 
	    {
	      /* Bus Error... nothing at that address */
	      continue;
	    } 
	  else 
	    {
	      /* Check that it is an FA125 (using ID) */
	      if((rdata) != FA125_ID) 
		{
		  printf(" ERROR: Slot %d: Invalid Board ID: 0x%x\n",
			 islot,rdata);
		  continue;
		}
	      else
		{
		  FA125_SLOT_MASK |= 1<<(islot-1);
		}

	    } /* valid VME read */

	} /* valid a24 address */

    } /* for islot loop */

  return FA125_SLOT_MASK;
}

/*******************************************************************************
 *
 * fa125PowerOff - Power Off the 125MSPS ADC
 *
 */

int
fa125PowerOff (int id) 
{
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      printf("%s: ERROR : FA125 in slot %d is not initialized \n",__FUNCTION__,id);
      return ERROR;
    }

  printf("%s: Power Off for slot %d\n",__FUNCTION__,id);
#ifdef DEBUG
  printf("%s: pwrctl (0x%08x)= 0x%08x\n",__FUNCTION__,
	 ((unsigned int) &fa125p[id]->main.pwrctl) -
	 ((unsigned int)&fa125p[id]->main.id),
	 fa125p[id]->main.pwrctl);
#endif

  FA125LOCK;
  fa125p[id]->main.pwrctl = 0;
  FA125UNLOCK;

#ifdef DEBUG
  printf("%s: pwrctl (0x%08x)= 0x%08x\n",__FUNCTION__,
	 ((unsigned int) &fa125p[id]->main.pwrctl) -
	 ((unsigned int)&fa125p[id]->main.id),
	 fa125p[id]->main.pwrctl);
#endif

  return OK;
}

/*******************************************************************************
 *
 * fa125PowerOn - Power On the 125MSPS ADC
 *
 */

void 
fa125PowerOn (int id) 
{
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      printf("%s: ERROR : FA125 in slot %d is not initialized \n",__FUNCTION__,id);
      return;
    }

  printf("%s: Power On (0x%08x) for slot %d\n",__FUNCTION__,
	 PWRCTL_KEY_ON,id);
#ifdef DEBUG
  printf("%s: pwrctl (0x%08x)= 0x%08x\n",__FUNCTION__,
	 ((unsigned int) &fa125p[id]->main.pwrctl) -
	 ((unsigned int)&fa125p[id]->main.id),
	 fa125p[id]->main.pwrctl);
#endif

  FA125LOCK;
  fa125p[id]->main.pwrctl = PWRCTL_KEY_ON;
  FA125UNLOCK;

#ifdef DEBUG
  printf("%s: pwrctl (0x%08x)= 0x%08x\n",__FUNCTION__,
	 ((unsigned int) &fa125p[id]->main.pwrctl) -
	 ((unsigned int)&fa125p[id]->main.id),
	 fa125p[id]->main.pwrctl);
#endif

#ifdef VXWORKS
  taskDelay(18);
#else
  usleep(300000);         // delay 300 ms for stable power;
#endif
}

/*******************************************************************************
 *
 * fa125SetTestTrigger - Enable/Disable internal pulser trigger
 *
 */

int
fa125SetTestTrigger (int id, int mode)
{
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      printf("%s: ERROR : FA125 in slot %d is not initialized \n",__FUNCTION__,id);
      return ERROR;
    }

  if(mode < 0 || mode >1)
    {
      printf("%s: ERROR: Invalid mode = %d\n",__FUNCTION__,mode);
      return ERROR;
    }

  FA125LOCK;
  if(mode==0) /* turn test trigger off */
    {
      fa125p[id]->main.csr &= ~FA125_CSR_TEST_TRIGGER;
    }
  else if(mode==1) /* turn test trigger on */
    {
      fa125p[id]->main.csr |= FA125_CSR_TEST_TRIGGER;
    }
  FA125UNLOCK;

  return OK;
}

/*******************************************************************************
 *
 * fa125SetLTC2620 - Set DAC value of a specific channel on the LTC2620
 *
 */

int
fa125SetLTC2620 (int id, int dacChan, int dacData) 
{
  UINT32 sdat[5]={0xffffffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff};
  UINT32 bmask,dmask,x;
  int k,j;

  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      printf("%s: ERROR : FA125 in slot %d is not initialized \n",__FUNCTION__,id);
      return ERROR;
    }

  if ((dacChan<0)||(dacChan>79)) {
    printf("%s: Invalid DAC Channel %d\n", __FUNCTION__,dacChan);
    return ERROR;
  }

  sdat[(dacChan/8)%5] = 0x00200000;               // set command nibble and have other fields zeroed
  sdat[(dacChan/8)%5] |= (dacData & 0x0000ffff);  // fill in the data
  sdat[(dacChan/8)%5] |= ((dacChan%8) << 16);     // fill in the subchannel

  if (dacChan>=40) 
    {
      bmask=DACCTL_DACCS_MASK|DACCTL_ADACSI_MASK; // these assserted for the duration
      dmask=DACCTL_BDACSI_MASK;                      // this is the data bit mask
    }
  else 
    {
      bmask=DACCTL_DACCS_MASK|DACCTL_BDACSI_MASK; // these assserted for the duration
      dmask=DACCTL_ADACSI_MASK;                      // this is the data bit mask
    }

  FA125LOCK;
  for(k=4;k>=0;k--)
    for(j=31;j>=0;j--) 
      {
	x = bmask | ( ((sdat[k]>>j)&1)!=0 ? dmask : 0 );
	fa125p[id]->main.dacctl = x;
	fa125p[id]->main.dacctl = x | DACCTL_DACSCLK_MASK;
      }

  fa125p[id]->main.dacctl = 0;  // this deasserts CS, setting the DAC
  FA125UNLOCK;

  return OK;
}

/*******************************************************************************
 *
 * fa125SetOffset - Set the DAC offset for a specific 125MSPS Channel.
 *
 */

int
fa125SetOffset (int id, int chan, int dacData) 
{
  int rval=0;
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      printf("%s: ERROR : FA125 in slot %d is not initialized \n",__FUNCTION__,id);
      return ERROR;
    }

  if ((chan<0)||(chan>71)) 
    {
      printf("%s: Invalid Channel %d\n",__FUNCTION__,chan);
      return ERROR;
    }

  rval = fa125SetLTC2620(id,DAC_CHAN_OFFSET[chan],dacData);

  return rval;
}

/* fa125SetOffsetFromFile - For now... one filename per module */

/*******************************************************************************
 *
 * fa125SetOffsetFromFile - Set the DAC offsets for each channel from a 
 *                           specified file.
 *
 */

int
fa125SetOffsetFromFile(int id, char *filename)
{
  FILE *fd_1;
  int ichan;
  int offset_control=0;

  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      printf("%s: ERROR : FA125 in slot %d is not initialized \n",__FUNCTION__,id);
      return ERROR;
    }

  if(filename == NULL)
    {
      printf("%s: ERROR: No file specified.\n",__FUNCTION__);
      return ERROR;
    }

  fd_1 = fopen(filename,"r");
  if(fd_1 > 0) 
    {
      printf("%s: Reading Data from file: %s\n",__FUNCTION__,filename);
      for(ichan=0;ichan<72;ichan++) 
	{
	  fscanf(fd_1,"%d",&offset_control);
	  fa125SetOffset(id, ichan, offset_control);
	}

	fclose(fd_1);
    }
  else
    {
      printf("%s: ERROR opening file: %s\n",__FUNCTION__,filename);
      return ERROR;
    }
  
  return OK;
}

/*******************************************************************************
 *
 * fa125SetPulserAmplitude - ... 
 *
 */

int
fa125SetPulserAmplitude (int id, int chan, int dacData) 
{
  int rval=0;
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      printf("%s: ERROR : FA125 in slot %d is not initialized \n",__FUNCTION__,id);
      return ERROR;
    }

  if ((chan<0)||(chan>2)) 
    {
      printf("%s: Invalid Channel %d\n",__FUNCTION__,chan);
      return ERROR;
    }

  rval = fa125SetLTC2620(id,DAC_CHAN_PULSER[chan],dacData);

  return rval;
}

/*******************************************************************************
 *
 * fa125SetMulThreshold ...
 *
 */

int
fa125SetMulThreshold(int id, int dacData) 
{
  int rval=0;
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      printf("%s: ERROR : FA125 in slot %d is not initialized \n",__FUNCTION__,id);
      return ERROR;
    }

  rval = fa125SetLTC2620(id,DAC_CHAN_MULTH,dacData);

  return rval;
}

/*******************************************************************************
 *
 * fa125PrintTemps - Print the temperature of the main board and mezzanine
 *                    to standard out.  Not to be used during a trigger routine.
 *
 */

int
fa125PrintTemps(int id)
{
  double temp1=0, temp2=0;
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      printf("%s: ERROR : FA125 in slot %d is not initialized \n",__FUNCTION__,id);
      return ERROR;
    }

  FA125LOCK;
  temp1 = 0.0625*((int) fa125p[id]->main.temperature[0]);
  temp2 = 0.0625*((int) fa125p[id]->main.temperature[1]);
  FA125UNLOCK;
  
  printf("%s: Main board temperature: %5.2lf \tMezzanine board temperature: %5.2lf\n",
	 __FUNCTION__,
	 temp1,
	 temp2);

  return OK;
}

/*******************************************************************************
 *
 * fa125Poll - Poll the 125MSPS busy status.
 *
 */

int
fa125Poll(int id)
{
  int res;
  int rval=0;
  static int nzero=0;

  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("%s: ERROR : FA125 in slot %d is not initialized \n",__FUNCTION__,id,3,4,5,6);
      return ERROR;
    }

  FA125LOCK;
#ifdef VXWORKS
  res = vxMemProbe((char *) &(fa125p[id]->proc.csr),VX_READ,4,(char *)&rval);
#else
  res = vmeMemProbe((char *) &(fa125p[id]->proc.csr),4,(char *)&rval);
  rval = LSWAP(rval);
#endif
  FA125UNLOCK;

  /* Sometimes get 0xffffffff.  This is accompanied with a bus error. */
  if(res==ERROR)
    {
#define SHOWBERR
#ifndef VXWORKS
#ifdef SHOWBERR
      vmeClearException(1);
#else
      vmeClearException(0);
#endif
#endif
      berr_count++;
      rval=0;
/*       logMsg("%s: BERR      nzero = %6d\n",__FUNCTION__,nzero,3,4,5,6); */
      return 0;
    }

  if(rval&FA125_CSR_BUSY)
    {
/*       logMsg("%s: poll = 1, nzero = %6d\n",__FUNCTION__,nzero,3,4,5,6); */
      nzero=0;
      return 1;
    }
  else
    {
      nzero++;
      return 0;
    }

}

/*******************************************************************************
 *
 * fa125GetBerrCount - Return the BERR count.
 *
 */

unsigned int
fa125GetBerrCount()
{
  return berr_count;
}

/*******************************************************************************
 *
 * fa125Clear - ...
 *
 */

void
fa125Clear(int id)
{
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("%s: ERROR : FA125 in slot %d is not initialized \n",__FUNCTION__,id,3,4,5,6);
      return;
    }

  FA125LOCK;
  fa125p[id]->proc.csr=FA125_CSR_CLEAR;
  fa125p[id]->proc.csr=0;
  FA125UNLOCK;

}

/**************************************************************************************
 *
 *  fa125ReadEvent - General Data readout routine
 *
 *    id    - Slot number of module to read
 *    data  - local memory address to place data
 *    nwrds - Max number of words to transfer
 *    rflag - Readout Flag
 *            Bits 0-3: Readout type
 *            Bits 4-31: Specific to readout type
 *              0 - programmed I/O from the specified board
 *                  Bits 3-31:
 *                  Number of samples to obtain from each channel
 *                  If input number of samples is odd, it will be
 *                  incremented by 1.
 */

int
fa125ReadEvent(int id, volatile UINT32 *data, int nwrds, unsigned int rflag)
{
  int dCnt=0;
  unsigned int rdata=0;
  unsigned int rmode=0;
  int ichan=0, iread=0;
  int nsamples=0,nreads=0;

  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("%s: ERROR : FA125 in slot %d is not initialized \n",__FUNCTION__,id,3,4,5,6);
      return ERROR;
    }

  rmode = rflag&0xf;

  switch(rmode)
    {
    case 0: /* Programmed I/O - emulation of format=0 */
      nsamples=(rflag&(0xfffffff0))>>4;
      nreads= (nsamples>>1) + (nsamples%2);
      /* Event header1,header2 */
      data[dCnt] = 
	( /* header1 */ 
	 id<<11 /* GA */
	 )<<16 | 
	( /* header 2 */
	 1<<15 /* M */
	 );
      dCnt++;
      
      for(ichan=0; ichan<72; ichan++)
	{
	  /* channel data record header1,header2 */
	  data[dCnt] = 
	    ( /* header1 */
	     (ichan<<9) | ((nreads)&0x1ff)
	     )<<16 |
	    ( /* header2 */
	     FA125_DATA_FORMAT0
	     );
	  dCnt++;
	  for(iread=0; iread<nreads; iread++)
	    {
	      if(dCnt>=nwrds)
		{
		  logMsg("%s: WARN: Maximum number of words in readout reached (%d)\n",
			 __FUNCTION__,nwrds,3,4,5,6);
		  fa125Clear(id);
		  return dCnt;
		}

	      FA125LOCK;
	      rdata=fa125p[id]->fe[ichan/6].acqfifo[ichan%6];
	      FA125UNLOCK;
	      if(rdata>>31)
		{
		  logMsg("%s: ERROR: Invalid Data (0x%08x) at channel %d, read %d\n",
			 __FUNCTION__,rdata,ichan,iread,5,6);
#ifndef VXWORKS
		  vmeClearException(1);
#endif
		  fa125Clear(id);
		  return ERROR;
		}
#ifdef NOZERODATA
	      if(rdata==0)
		{
		  logMsg("%s: ERROR: Empty Data (0x%08x) at channel %d, read %d\n",
			 __FUNCTION__,rdata,ichan,iread,5,6);
#ifndef VXWORKS
		  vmeClearException(1);
#endif
		  fa125Clear(id);
		  return ERROR;
		}
#endif
	      data[dCnt] = rdata;
	      dCnt++;
	    } /* sample loop */
	} /* channel loop */
      data[dCnt] = (FA125_DATA_END)<<16 | (FA125_PAD_WORD);
      dCnt++;
      fa125Clear(id);
      return dCnt;
      
    default:
      logMsg("%s: ERROR: rflag = %d not supported.\n",
	     __FUNCTION__,rflag,3,4,5,6);
      return ERROR;
    }

  return 0;
}

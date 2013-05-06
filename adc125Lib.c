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

#include "adc125Lib.h"

#ifndef LSWAP
#define LSWAP(x)        ((((x) & 0x000000ff) << 24) | \
                         (((x) & 0x0000ff00) <<  8) | \
                         (((x) & 0x00ff0000) >>  8) | \
                         (((x) & 0xff000000) >> 24))
#endif

/* Define global variables */
int nadc125=0;
volatile struct adc125_a24 *adc125p[(ADC125_MAX_BOARDS+1)]; /* pointers to ADC125 memory map */
volatile struct adc125_a32 *adc125pd[(ADC125_MAX_BOARDS+1)]; /* pointers to ADC125 FIFO memory */
int adc125ID[ADC125_MAX_BOARDS]; /* array of slot numbers for ADC125s */
int berr_count=0; /* A count of the number of BERR that have occurred when running adc125Poll() */

#define ADC125A24BA 0xa10000   /* just hardcode here for now */

/*******************************************************************************
 *
 * adc125Init - Initialize IO 125MSPS ADC Library
 *
 *   Initialize a single ADC125 at the specified VME A24.  Only one
 *   module is supported at the moment. (More to be supported through
 *   other routines)
 *
 */

int
adc125Init (UINT32 addr, int iFlag)
{
  int res;
  volatile unsigned int rdata=0;
  unsigned int laddr;
  volatile struct adc125_a24 *fa125;

  /* Check for valid address */
  if(addr==0) 
    {
      printf("%s: ERROR: Must specify a Bus (VME-based A24) address for ADC125 0\n",
	     __FUNCTION__);
      return(ERROR);
    }
  else if(addr > 0x00ffffff)  /* A24 Addressing */
    {
      printf("%s: ERROR: A32 Addressing not allowed for ADC125 configuration space\n",
	     __FUNCTION__);
      return(ERROR);
    }
  else
    { /* A24 Addressing */
      /* get the FADC USERSPACE address */
#ifdef VXWORKS
      res = sysBusToLocalAdrs(0x39,(char *)addr,(char **)&laddr);
#else
      res = vmeBusToLocalAdrs(0x39,
			      (char *)addr,
			      &laddr);
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

      fa125 = (volatile struct adc125_a24 *)(laddr);
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
	  return(ERROR);
	}
      else 
	{
	  /* Check that it is an ADC125 (using main firmare version) 
	     ... with and without byte swapping */
	  if( ((rdata) != ADC125_ID) &&
	      ((rdata) != LSWAP(ADC125_ID)) )
	    {
	      printf("%s: ERROR: Invalid Board ID: 0x%x\n",
		     __FUNCTION__,rdata);
	      return(ERROR);
	    }
	}

    }

  adc125p[0] = (struct adc125_a24 *)laddr;
  adc125ID[0] = 0; /* = SLOT_NUMBER (eventually taken from GEOADDR) */

  adc125Clear(0);
  
#ifdef VXWORKS
  /* Turn off hardware byte swapping for vxWorks */
  adc125p[0]->main.swapctl = 0;
#else
  /* Turn on hardware byte swapping for Linux */
  adc125p[0]->main.swapctl = 0xffffffff;
#endif
  printf("%s: Initialized ADC125 at address 0x%08x\n",__FUNCTION__,(unsigned int)adc125p[0]);
  printf("  ID is 0x%08x\n",adc125p[0]->main.id);
  printf("  Main FPGA firmware version is %d\n",adc125p[0]->main.version);
  printf("  Main board serial 0x%04x%08x / mezzanine board serial 0x%04x%08x\n",
	 adc125p[0]->main.serial[0]&0x0000ffff,
	 adc125p[0]->main.serial[1],
	 adc125p[0]->main.serial[2]&0x0000ffff,
	 adc125p[0]->main.serial[3]);
  printf("  Processor FPGA firmware version is %d\n",adc125p[0]->proc.version);

  return(OK);

}

/*******************************************************************************
 *
 * adc125Slot2VmeAdrs - Determine VME Address from Crate Slot number
 *
 *    - Numerically convert a ADC125 slot number into the expected A24 VME address
 *      The slot number is encoded as the Most Significant 5 bits of the A24 address
 *      determined by its GEOADDR at boottime.
 */

unsigned int
adc125Slot2VmeAdrs (int crateslot)
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
      rval = (crateslot<<19)&ADC125_A24_ADDR_MASK;
    }

  return rval;
}

/*******************************************************************************
 *
 * adc125GetSlotMask - Determine physical slot mask
 *
 *    - Brute force method of determining which ADC125s exist in the crate
 *      and forming the ADC125_SLOT_MASK.  This method increments from Slot 2 to 21
 *      determining if a ADC125 returns the correct firmware.  If the firmware is
 *      incorrect or a BUS ERROR is returned, the slot in question will not be included
 *      in the ADC125_SLOT_MASK.
 */

unsigned int
adc125GetSlotMask()
{
  unsigned int islot, addr, laddr;
  int res, rdata=0;
  volatile struct adc125_a24 *fa125;

  /* Loop over each slot*/
  for(islot=2; islot<22; islot++)
    {
      addr = adc125Slot2VmeAdrs(islot);
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
	  fa125 = (volatile struct adc125_a24 *)(laddr);
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
	      /* Check that it is an ADC125 (using ID) */
	      if((rdata) != ADC125_ID) 
		{
		  printf(" ERROR: Slot %d: Invalid Board ID: 0x%x\n",
			 islot,rdata);
		  continue;
		}
	      else
		{
		  ADC125_SLOT_MASK |= 1<<(islot-1);
		}

	    } /* valid VME read */

	} /* valid a24 address */

    } /* for islot loop */

  return ADC125_SLOT_MASK;
}

/*******************************************************************************
 *
 * adc125PowerOff - Power Off the 125MSPS ADC
 *
 */

void 
adc125PowerOff (int id) 
{
  if(id==0) id=adc125ID[0];
  
  if((id<0) || (id>21) || (adc125p[id] == NULL)) 
    {
      printf("%s: ERROR : ADC125 in slot %d is not initialized \n",__FUNCTION__,id);
      return;
    }

  printf("%s: Power Off for slot %d\n",__FUNCTION__,id);
  printf("%s: pwrctl (0x%08x)= 0x%08x\n",__FUNCTION__,
	 ((unsigned int) &adc125p[id]->main.pwrctl) -
	 ((unsigned int)&adc125p[id]->main.id),
	 adc125p[id]->main.pwrctl);
  adc125p[id]->main.pwrctl = 0;
  printf("%s: pwrctl (0x%08x)= 0x%08x\n",__FUNCTION__,
	 ((unsigned int) &adc125p[id]->main.pwrctl) -
	 ((unsigned int)&adc125p[id]->main.id),
	 adc125p[id]->main.pwrctl);
}

/*******************************************************************************
 *
 * adc125PowerOn - Power On the 125MSPS ADC
 *
 */

void 
adc125PowerOn (int id) 
{
  if(id==0) id=adc125ID[0];
  
  if((id<0) || (id>21) || (adc125p[id] == NULL)) 
    {
      printf("%s: ERROR : ADC125 in slot %d is not initialized \n",__FUNCTION__,id);
      return;
    }

  printf("%s: Power On (0x%08x) for slot %d\n",__FUNCTION__,
	 PWRCTL_KEY_ON,id);
  printf("%s: pwrctl (0x%08x)= 0x%08x\n",__FUNCTION__,
	 ((unsigned int) &adc125p[id]->main.pwrctl) -
	 ((unsigned int)&adc125p[id]->main.id),
	 adc125p[id]->main.pwrctl);
  adc125p[id]->main.pwrctl = PWRCTL_KEY_ON;
  printf("%s: pwrctl (0x%08x)= 0x%08x\n",__FUNCTION__,
	 ((unsigned int) &adc125p[id]->main.pwrctl) -
	 ((unsigned int)&adc125p[id]->main.id),
	 adc125p[id]->main.pwrctl);
#ifdef VXWORKS
  taskDelay(18);
#else
  usleep(300000);         // delay 300 ms for stable power;
#endif
}

/*******************************************************************************
 *
 * adc125SetTestTrigger - Enable/Disable internal pulser trigger
 *
 */

void
adc125SetTestTrigger (int id, int mode)
{
  if(id==0) id=adc125ID[0];
  
  if((id<0) || (id>21) || (adc125p[id] == NULL)) 
    {
      printf("%s: ERROR : ADC125 in slot %d is not initialized \n",__FUNCTION__,id);
      return;
    }

  if(mode < 0 || mode >1)
    {
      printf("%s: ERROR: Invalid mode = %d\n",__FUNCTION__,mode);
      return;
    }

  if(mode==0) /* turn test trigger off */
    {
      adc125p[id]->main.csr &= ~ADC125_CSR_TEST_TRIGGER;
    }
  else if(mode==1) /* turn test trigger on */
    {
      adc125p[id]->main.csr |= ADC125_CSR_TEST_TRIGGER;
    }

}

/*******************************************************************************
 *
 * adc125SetLTC2620 - Set DAC value of a specific channel on the LTC2620
 *
 */

void 
adc125SetLTC2620 (int id, int dacChan, int dacData) 
{
  UINT32 sdat[5]={0xffffffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff};
  UINT32 bmask,dmask,x;
  int k,j;

  if(id==0) id=adc125ID[0];
  
  if((id<0) || (id>21) || (adc125p[id] == NULL)) 
    {
      printf("%s: ERROR : ADC125 in slot %d is not initialized \n",__FUNCTION__,id);
      return;
    }

  if ((dacChan<0)||(dacChan>79)) {
    printf("%s: Invalid DAC Channel %d\n", __FUNCTION__,dacChan);
    return;
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

  for(k=4;k>=0;k--)
    for(j=31;j>=0;j--) 
      {
	x = bmask | ( ((sdat[k]>>j)&1)!=0 ? dmask : 0 );
	adc125p[id]->main.dacctl = x;
	adc125p[id]->main.dacctl = x | DACCTL_DACSCLK_MASK;
      }

  adc125p[id]->main.dacctl = 0;  // this deasserts CS, setting the DAC
}

/*******************************************************************************
 *
 * adc125SetOffset - Set the DAC offset for a specific 125MSPS Channel.
 *
 */

void 
adc125SetOffset (int id, int chan, int dacData) 
{

  if(id==0) id=adc125ID[0];
  
  if((id<0) || (id>21) || (adc125p[id] == NULL)) 
    {
      printf("%s: ERROR : ADC125 in slot %d is not initialized \n",__FUNCTION__,id);
      return;
    }

  if ((chan<0)||(chan>71)) 
    {
      printf("%s: Invalid Channel %d\n",__FUNCTION__,chan);
      return;
    }

  adc125SetLTC2620(id,DAC_CHAN_OFFSET[chan],dacData);
}

/* adc125SetOffsetFromFile - For now... one filename per module */

/*******************************************************************************
 *
 * adc125SetOffsetFromFile - Set the DAC offsets for each channel from a 
 *                           specified file.
 *
 */

int
adc125SetOffsetFromFile(int id, char *filename)
{
  FILE *fd_1;
  int ichan;
  int offset_control=0;

  if(id==0) id=adc125ID[0];
  
  if((id<0) || (id>21) || (adc125p[id] == NULL)) 
    {
      printf("%s: ERROR : ADC125 in slot %d is not initialized \n",__FUNCTION__,id);
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
	  adc125SetOffset(id, ichan, offset_control);
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
 * adc125SetPulserAmplitude - ... 
 *
 */

void 
adc125SetPulserAmplitude (int id, int chan, int dacData) 
{

  if(id==0) id=adc125ID[0];
  
  if((id<0) || (id>21) || (adc125p[id] == NULL)) 
    {
      printf("%s: ERROR : ADC125 in slot %d is not initialized \n",__FUNCTION__,id);
      return;
    }

  if ((chan<0)||(chan>2)) 
    {
      printf("%s: Invalid Channel %d\n",__FUNCTION__,chan);
      return;
    }

  adc125SetLTC2620(id,DAC_CHAN_PULSER[chan],dacData);
}

/*******************************************************************************
 *
 * adc125SetMulThreshold ...
 *
 */

void 
adc125SetMulThreshold(int id, int dacData) 
{
  if(id==0) id=adc125ID[0];
  
  if((id<0) || (id>21) || (adc125p[id] == NULL)) 
    {
      printf("%s: ERROR : ADC125 in slot %d is not initialized \n",__FUNCTION__,id);
      return;
    }

  adc125SetLTC2620(id,DAC_CHAN_MULTH,dacData);
}

/*******************************************************************************
 *
 * adc125PrintTemps - Print the temperature of the main board and mezzanine
 *                    to standard out.  Not to be used during a trigger routine.
 *
 */

void
adc125PrintTemps(int id)
{
  if(id==0) id=adc125ID[0];
  
  if((id<0) || (id>21) || (adc125p[id] == NULL)) 
    {
      printf("%s: ERROR : ADC125 in slot %d is not initialized \n",__FUNCTION__,id);
      return;
    }
  
  printf("\tMain board temperature: %5.2lf \tMezzanine board temperature: %5.2lf\n",
	 0.0625*((int) adc125p[id]->main.temperature[0]),
	 0.0625*((int) adc125p[id]->main.temperature[1]));
}

/*******************************************************************************
 *
 * adc125Poll - Poll the 125MSPS busy status.
 *
 */

int
adc125Poll(int id)
{
  int res;
  int rval=0;
  static int nzero=0;

  if(id==0) id=adc125ID[0];
  
  if((id<0) || (id>21) || (adc125p[id] == NULL)) 
    {
      logMsg("%s: ERROR : ADC125 in slot %d is not initialized \n",__FUNCTION__,id,3,4,5,6);
      return ERROR;
    }

#ifdef VXWORKS
  res = vxMemProbe((char *) &(adc125p[id]->proc.csr),VX_READ,4,(char *)&rval);
#else
  res = vmeMemProbe((char *) &(adc125p[id]->proc.csr),4,(char *)&rval);
  rval = LSWAP(rval);
#endif

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

  if(rval&ADC125_CSR_BUSY)
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
 * adc125GetBerrCount - Return the BERR count.
 *
 */

unsigned int
adc125GetBerrCount()
{
  return berr_count;
}

/*******************************************************************************
 *
 * adc125Clear - ...
 *
 */

void
adc125Clear(int id)
{
  if(id==0) id=adc125ID[0];
  
  if((id<0) || (id>21) || (adc125p[id] == NULL)) 
    {
      logMsg("%s: ERROR : ADC125 in slot %d is not initialized \n",__FUNCTION__,id,3,4,5,6);
      return;
    }

  adc125p[id]->proc.csr=ADC125_CSR_CLEAR;
  adc125p[id]->proc.csr=0;

}

/**************************************************************************************
 *
 *  adc125ReadEvent - General Data readout routine
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
adc125ReadEvent(int id, volatile UINT32 *data, int nwrds, unsigned int rflag)
{
  int dCnt=0;
  unsigned int rdata=0;
  unsigned int rmode=0;
  int ichan=0, iread=0;
  int nsamples=0,nreads=0;

  if(id==0) id=adc125ID[0];
  
  if((id<0) || (id>21) || (adc125p[id] == NULL)) 
    {
      logMsg("%s: ERROR : ADC125 in slot %d is not initialized \n",__FUNCTION__,id,3,4,5,6);
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
	     ADC125_DATA_FORMAT0
	     );
	  dCnt++;
	  for(iread=0; iread<nreads; iread++)
	    {
	      if(dCnt>=nwrds)
		{
		  logMsg("%s: WARN: Maximum number of words in readout reached (%d)\n",
			 __FUNCTION__,nwrds,3,4,5,6);
		  adc125Clear(id);
		  return dCnt;
		}
	      rdata=adc125p[id]->fe[ichan/6].acqfifo[ichan%6];
	      if(rdata>>31)
		{
		  logMsg("%s: ERROR: Invalid Data (0x%08x) at channel %d, read %d\n",
			 __FUNCTION__,rdata,ichan,iread,5,6);
#ifndef VXWORKS
		  vmeClearException(1);
#endif
		  adc125Clear(id);
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
		  adc125Clear(id);
		  return ERROR;
		}
#endif
	      data[dCnt] = rdata;
	      dCnt++;
	    } /* sample loop */
	} /* channel loop */
      data[dCnt] = (ADC125_DATA_END)<<16 | (ADC125_PAD_WORD);
      dCnt++;
      adc125Clear(id);
      return dCnt;
      
    default:
      logMsg("%s: ERROR: rflag = %d not supported.\n",
	     __FUNCTION__,rflag,3,4,5,6);
      return ERROR;
    }

  return 0;
}

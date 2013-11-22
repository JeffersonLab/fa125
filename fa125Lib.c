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
 *     Driver library for readout of the 125MSPS ADC using vxWorks 5.5 
 *     (or later) or Intel based single board computer
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
volatile unsigned int *FA125pmb;                        /* pointer to Multblock window */
int fa125ID[FA125_MAX_BOARDS]; /* array of slot numbers for FA125s */
int fa125A32Base   = 0x08000000;                      /* Minimum VME A32 Address for use by FA125s */
int fa125A32Offset = 0x08000000;                      /* Difference in CPU A32 Base - VME A32 Base */
int fa125A24Offset=0;                            /* Difference in CPU A24 Base and VME A24 Base */
unsigned int fa125AddrList[FA125_MAX_BOARDS];            /* array of a24 addresses for FA125s */
int fa125MaxSlot=0;                                   /* Highest Slot hold an FA125 */
int fa125MinSlot=0;                                   /* Lowest Slot holding an FA125 */
int fa125TriggerSource=0;
int berr_count=0; /* A count of the number of BERR that have occurred when running fa125Poll() */
/* store the dacOffsets in the library, until the firmware is able to read them back */
static unsigned short fa125dacOffset[FA125_MAX_BOARDS+1][72];
int fa125BlockError=0;       /* Whether (1) or not (0) Block Transfer had an error */

/*******************************************************************************
 *
 * fa125Init - Initialize the fa125 Library
 *
 *   iFlag: 17 bit integer
 *       Low 6 bits - Specifies the default Signal distribution (clock,trigger) 
 *                    sources for the board (Internal, FrontPanel, VXS, VME(Soft))
 *       bit    0:  defines Sync Reset source
 *                    NOT USED WITH THIS FIRMWARE VERSION
 *       bits 2-1:  defines Trigger source
 *           (0) 0 0  VXS (P0)
 *           (1) 0 1  Internal Timer
 *           (2) 1 0  Internal Multiplicity Sum
 *           (3) 1 1  P2 Connector (Backplane)
 *       bit    3:  NOT USED WITH THIS FIRMWARE VERSION
 *       bits 5-4:  defines Clock Source
 *           (0) 0 0  P2 Connector (Backplane)
 *           (1) 0 1  VXS (P0)
 *           (2) 1 0  Internal 125MHz Clock
 *
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
  int res=0;
  volatile unsigned int rdata=0;
  unsigned int laddr=0, a32addr=0;
  volatile struct fa125_a24 *fa125;
  int useList=0, noBoardInit=0;
  int nfind=0, islot=0, FA_SLOT=0, ii=0;
  int trigSrc=0, clkSrc=0, srSrc=0;
  unsigned int boardID=0, fw_version=0;
  int maxSlot = 1;
  int minSlot = 21;

  /* Initialize some global variables */
  nfa125=0;
  memset((char *)fa125ID,0,sizeof(fa125ID));
  memset((char *)fa125dacOffset,0,sizeof(fa125dacOffset));

  /* Check if we're skipping initialization, and just mapping the structure pointer */
  if(iFlag & (1<<16))
    noBoardInit=1;

  /* Check if we're initializing using a list */
  if(iFlag & (1<<17))
    {
      useList=1;
      nfind = nadc;
    }

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
  res = vmeBusToLocalAdrs(0x39,(char *)fa125AddrList[0],(char **)&laddr);
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
      
      fa125 = (volatile struct fa125_a24 *)(fa125AddrList[islot]+fa125A24Offset);
      /* Check if Board exists at that address */
#ifdef VXWORKS
      res = vxMemProbe((char *) &(fa125->main.id),VX_READ,4,(char *)&rdata);
#else
      res = vmeMemProbe((char *)&(fa125->main.id),4,(char *)&rdata);
#endif
      if(res < 0) 
	{
	  printf("%s: ERROR: No addressable board at addr=0x%x\n",
		 __FUNCTION__,(UINT32) fa125AddrList[islot]);
	}
      else 
	{
	  /* Check that it is an FA125 (using main firmare version) 
	     ... with and without byte swapping */
	  if( ((rdata) != FA125_ID) &&
	      ((rdata) != LSWAP(FA125_ID)) )
	    {
	      printf("%s: ERROR: For module at 0x%x, Invalid Board ID: 0x%x\n",
		     __FUNCTION__,fa125AddrList[islot],rdata);
	      continue;
	    }
	  else
	    {
	      /* Turn on hardware byteswapping, if needed */
#ifdef VXWORKS
	      fa125->main.swapctl = 0;
#else
	      fa125->main.swapctl = 0xffffffff;
#endif

	      /* Check the Firmware Versions */
	      /* MAIN */
	      fw_version = fa125->main.version;
	      if(fw_version != FA125_MAIN_SUPPORTED_FIRMWARE)
		{
		  printf("%s: ERROR: For module at 0x%x, Unsupported MAIN firmware version 0x%x\n",
			 __FUNCTION__,fa125AddrList[islot],fw_version);
		  continue;
		}

	      /* PROC */
	      fw_version = fa125->proc.version;
	      if(fw_version != FA125_PROC_SUPPORTED_FIRMWARE)
		{
		  printf("%s: ERROR: For module at 0x%x, Unsupported PROC firmware version 0x%x\n",
			 __FUNCTION__,fa125AddrList[islot],fw_version);
		  continue;
		}

	      /* FE - just check the first one */
	      fw_version = fa125->fe[0].version;
	      if(fw_version != FA125_FE_SUPPORTED_FIRMWARE)
		{
		  printf("%s: ERROR: For module at 0x%x, Unsupported FE firmware version 0x%x\n",
			 __FUNCTION__,fa125AddrList[islot],fw_version);
		  continue;
		}

	      /* Get the Geographic Address */
	      boardID = fa125->main.slot_ga;

	      if((boardID<2) || (boardID>21))
		{
		  printf("%s: For module at 0x%x, Invalid Slot Number %d\n",
			 __FUNCTION__,fa125AddrList[islot],boardID);
		  continue;
		}

	      if(boardID >= maxSlot) maxSlot = boardID;
	      if(boardID <= minSlot) minSlot = boardID;

	      fa125p[boardID] = (struct fa125_a24 *) (fa125AddrList[islot]+fa125A24Offset);
	      fa125ID[nfa125] = boardID;

	      printf("Initialized FA125 %2d  Slot # %2d at address 0x%08x (0x%08x)\n",
		     nfa125,fa125ID[nfa125],
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

  /* Determine the clock, sync, trigger configuration */
  /* Sync Reset */
  srSrc = 0;

  /* Trigger */
  trigSrc = (iFlag&0x6)>>1;
  fa125TriggerSource = trigSrc;

  /* Clock Source */
  clkSrc = (iFlag&0x30)>>4;

  /* Perform some initialization here */
  for(islot=0; islot<nfa125; islot++)
    {
      FA_SLOT = fa125ID[islot];
#ifdef VXWORKS
      /* Turn off hardware byte swapping for vxWorks */
      fa125SetByteSwap(FA_SLOT,0);
#else
      /* Turn on hardware byte swapping for Linux */
      fa125SetByteSwap(FA_SLOT,1);
#endif

      fa125Clear(FA_SLOT);

      /* Set the clock source */
      fa125SetClockSource(FA_SLOT,clkSrc);

      /* Set the trigger source */
      fa125SetTriggerSource(FA_SLOT,fa125TriggerSource);
      
    }

  for(ii=0;ii<nfa125; ii++) 
    {
    
      /* Program an A32 access address for this FA125's FIFO */
      a32addr = fa125A32Base + ii*FA125_MAX_A32_MEM;
#ifdef VXWORKS
      res = sysBusToLocalAdrs(0x09,(char *)a32addr,(char **)&laddr);
      if (res != 0) 
	{
	  printf("%s: ERROR in sysBusToLocalAdrs(0x09,0x%x,&laddr) \n",
		 __FUNCTION__,a32addr);
	  return(ERROR);
	}
#else
      res = vmeBusToLocalAdrs(0x09,(char *)a32addr,(char **)&laddr);
      if (res != 0) 
	{
	  printf("%s: ERROR in vmeBusToLocalAdrs(0x09,0x%x,&laddr) \n",
		 __FUNCTION__,a32addr);
	  return(ERROR);
	}
#endif
      fa125pd[fa125ID[ii]] = (volatile struct fa125_a32 *)(laddr);  /* Set a pointer to the FIFO */
      if(!noBoardInit)
	{
	  fa125p[fa125ID[ii]]->main.adr32 = (a32addr>>16) + FA125_ADR32_ENABLE;  /* Write the register and enable */
	}

    }

  /* If there are more than 1 FA125 in the crate then setup the Muliblock Address
     window. This must be the same on each board in the crate */
  if(nfa125 > 1) 
    {
      a32addr = fa125A32Base + (nfa125+1)*FA125_MAX_A32_MEM; /* set MB base above individual board base */
#ifdef VXWORKS
      res = sysBusToLocalAdrs(0x09,(char *)a32addr,(char **)&laddr);
      if (res != 0) 
	{
	  printf("%s: ERROR in sysBusToLocalAdrs(0x09,0x%x,&laddr) \n",__FUNCTION__,a32addr);
	  return(ERROR);
	}
#else
      res = vmeBusToLocalAdrs(0x09,(char *)a32addr,(char **)&laddr);
      if (res != 0) 
	{
	  printf("%s: ERROR in vmeBusToLocalAdrs(0x09,0x%x,&laddr) \n",__FUNCTION__,a32addr);
	  return(ERROR);
	}
#endif
      FA125pmb = (unsigned int *)(laddr);  /* Set a pointer to the FIFO */
      if(!noBoardInit)
	{
	  for (ii=0;ii<nfa125;ii++) 
	    {
	      /* Write the register and enable FIXME... not defined yet */
	      fa125p[fa125ID[ii]]->main.adr_mb =
		(a32addr+FA125_MAX_A32MB_SIZE) + (a32addr>>16) + FA125_ADRMB_ENABLE;
	    }
	}    
      /* Set First Board and Last Board */
      fa125MaxSlot = maxSlot;
      fa125MinSlot = minSlot;
      if(!noBoardInit)
	{
	  /* FIXME... not defined yet */
	  fa125p[minSlot]->main.ctrl1 = fa125p[minSlot]->main.ctrl1 | FA125_CTRL1_FIRST_BOARD;
	  fa125p[maxSlot]->main.ctrl1 = fa125p[maxSlot]->main.ctrl1 | FA125_CTRL1_LAST_BOARD;
	}    
    }

  if(nfa125 > 0)
    printf("%s: %d FA125(s) successfully initialized\n",__FUNCTION__,nfa125);

  return(OK);

}

/*******************************************************************************
 *
 * fa125Slot - Convert an index into a slot number, where the index is
 *          the element of an array of FA125s in the order in which they were
 *          initialized.
 *
 * RETURNS: Slot number if Successfull, otherwise ERROR.
 *
 */

int
fa125Slot(unsigned int i)
{
  if(i>=nfa125)
    {
      printf("%s: ERROR: Index (%d) >= FA125s initialized (%d).\n",
	     __FUNCTION__,i,nfa125);
      return ERROR;
    }

  return fa125ID[i];
}

int
fa125Status(int id)
{
  unsigned int main_id, main_swapctl, main_version, main_pwrctl;
  unsigned int main_slot_ga, main_clock;
  unsigned int main_serial[2], mezz_serial[2];
  unsigned int fe_version;
  unsigned int proc_version;
  unsigned int proc_csr;
  unsigned int proc_trigsrc;
  unsigned int clksrc;
  unsigned int trigsrc;
  unsigned int a32Base, ambMin, ambMax;
  unsigned int adr32, adr_mb;
  unsigned int ctrl1;

  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      printf("%s: ERROR : FA125 in slot %d is not initialized \n",__FUNCTION__,id);
      return ERROR;
    }

  FA125LOCK;
  main_id = fa125p[id]->main.id;
  main_swapctl = fa125p[id]->main.swapctl;
  main_version = fa125p[id]->main.version;
#ifdef DOESNOTEXIST
  main_csr = fa125p[id]->main.csr;
#endif
  main_pwrctl = fa125p[id]->main.pwrctl;
  main_slot_ga = fa125p[id]->main.slot_ga;
  main_clock = fa125p[id]->main.clock;

  main_serial[0] = fa125p[id]->main.serial[0];
  main_serial[1] = fa125p[id]->main.serial[1];
  mezz_serial[0] = fa125p[id]->main.serial[2];
  mezz_serial[1] = fa125p[id]->main.serial[3];

  fe_version = fa125p[id]->fe[0].version;

  proc_version = fa125p[id]->proc.version;
  proc_csr     = fa125p[id]->proc.csr;
  proc_trigsrc = fa125p[id]->proc.trigsrc;

  adr32        = fa125p[id]->main.adr32;
  adr_mb       = fa125p[id]->main.adr_mb;

  ctrl1        = fa125p[id]->main.ctrl1;
  FA125UNLOCK;

  a32Base = (adr32 & FA125_ADR32_BASE_MASK)<<16;
  ambMin  = (adr_mb & FA125_ADRMB_MIN_MASK)<<16;
  ambMax  = (adr_mb & FA125_ADRMB_MAX_MASK);

  #ifdef VXWORKS
  printf("\nSTATUS for FA125 in slot %d at base address 0x%x \n",
	 id, (UINT32) fa125p[id]);
#else
  printf("\nSTATUS for FA125 in slot %d at VME (Local) base address 0x%x (0x%x)\n",
	 id, (UINT32) fa125p[id] - fa125A24Offset, (UINT32) fa125p[id]);
#endif
  printf("---------------------------------------------------------------------- \n");
  printf(" Main Firmware Revision     = 0x%08x\n",
	 main_version);
  printf(" FrontEnd Firmware Revision = 0x%08x\n",
	 fe_version);
  printf(" Processing Revision        = 0x%08x\n",
	 proc_version);

  printf("      Main SN = 0x%04x%08x\n",main_serial[0], main_serial[1]);
  printf(" Mezzanine SN = 0x%04x%08x\n",mezz_serial[0], mezz_serial[1]);
  
  if(ctrl1 & FA125_CTRL1_ENABLE_MULTIBLOCK) 
    {
      printf(" Alternate VME Addressing: Multiblock Enabled\n");
      if(adr32&FA125_ADR32_ENABLE)
	printf("   A32 Enabled at VME (Local) base 0x%08x (0x%08x)\n",a32Base,
	       (UINT32) fa125pd[id]);
      else
	printf("   A32 Disabled\n");
    
      printf("   Multiblock VME Address Range 0x%08x - 0x%08x\n",ambMin,ambMax);
    }
  else
    {
      printf(" Alternate VME Addressing: Multiblock Disabled\n");
      if(adr32&FA125_ADR32_ENABLE)
	printf("   A32 Enabled at VME (Local) base 0x%08x (0x%08x)\n",a32Base,
	       (UINT32) fa125pd[id]);
      else
	printf("   A32 Disabled\n");
    }

  /* POWER */
  if(main_pwrctl)
    printf(" Power is ON\n");
  else
    printf(" Power is OFF\n");

  /* CLOCK */
  printf(" Clock Source (0x%x):",main_clock);
  clksrc = main_clock & 0xffff;
  if(clksrc == FA125_CLOCK_P2)
    printf(" P2\n");
  else if (clksrc == FA125_CLOCK_P0)
    printf(" P0 (VXS)\n");
  else if (clksrc == FA125_CLOCK_INTERNAL)
    printf(" Internal\n");
  else
    printf(" ????\n");

  /* TRIGGER */
  printf(" Trigger Source (0x%x):",proc_trigsrc);
  trigsrc = proc_trigsrc & FA125_TRIGSRC_TRIGGER_MASK;
  if(trigsrc == FA125_TRIGSRC_TRIGGER_P0)
    printf(" P0\n");
  else if (trigsrc == FA125_TRIGSRC_TRIGGER_INTERNAL_TIMER)
    printf(" Internal Timer\n");
  else if (trigsrc == FA125_TRIGSRC_TRIGGER_INTERNAL_SUM)
    printf(" Internal Sum\n");
  else if (trigsrc == FA125_TRIGSRC_TRIGGER_P2)
    printf(" P2\n");

  printf("---------------------------------------------------------------------- \n");
  return OK;

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
    fa125p[id]->main.swapctl = 0xffffffff;
  else
    fa125p[id]->main.swapctl = 0;
  FA125UNLOCK;

  return OK;
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

int
fa125PowerOn (int id) 
{
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      printf("%s: ERROR : FA125 in slot %d is not initialized \n",__FUNCTION__,id);
      return ERROR;
    }

  printf("%s: Power On (0x%08x) for slot %d\n",__FUNCTION__,
	 FA125_PWRCTL_KEY_ON,id);
#ifdef DEBUG
  printf("%s: pwrctl (0x%08x)= 0x%08x\n",__FUNCTION__,
	 ((unsigned int) &fa125p[id]->main.pwrctl) -
	 ((unsigned int)&fa125p[id]->main.id),
	 fa125p[id]->main.pwrctl);
#endif

  FA125LOCK;
  fa125p[id]->main.pwrctl = FA125_PWRCTL_KEY_ON;
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
  
  return OK;
}

#ifdef DOESNOTEXIST
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
      fa125p[id]->main.csr &= ~FA125_MAIN_CSR_TEST_TRIGGER;
    }
  else if(mode==1) /* turn test trigger on */
    {
      fa125p[id]->main.csr |= FA125_MAIN_CSR_TEST_TRIGGER;
    }
  FA125UNLOCK;

  return OK;
}
#endif /* DOESNOTEXIST */

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
      bmask=FA125_DACCTL_DACCS_MASK|FA125_DACCTL_ADACSI_MASK; // these assserted for the duration
      dmask=FA125_DACCTL_BDACSI_MASK;                      // this is the data bit mask
    }
  else 
    {
      bmask=FA125_DACCTL_DACCS_MASK|FA125_DACCTL_BDACSI_MASK; // these assserted for the duration
      dmask=FA125_DACCTL_ADACSI_MASK;                      // this is the data bit mask
    }

  FA125LOCK;
  for(k=4;k>=0;k--)
    for(j=31;j>=0;j--) 
      {
	x = bmask | ( ((sdat[k]>>j)&1)!=0 ? dmask : 0 );
	fa125p[id]->main.dacctl = x;
	fa125p[id]->main.dacctl = x | FA125_DACCTL_DACSCLK_MASK;
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
  const int DAC_CHAN_OFFSET[72] =
    {
      34, 33, 32, 39, 38, 37, 36, 27, 26, 25, 24, 31,
      74, 73, 72, 79, 78, 77, 76, 67, 66, 65, 64, 71,
      30, 29, 28, 18, 17, 16, 23, 22, 21, 20, 10, 9,
      70, 69, 68, 58, 57, 56, 63, 62, 61, 60, 50, 49,
      8, 15, 14, 13, 12, 2, 1, 0, 7, 6, 5, 4,
      48, 55, 54, 53, 52, 42, 41, 40, 47, 46, 45, 44
    };

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
  fa125dacOffset[id][chan] = dacData;

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

unsigned short
fa125ReadOffset(int id, int chan)
{
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      printf("%s: ERROR : FA125 in slot %d is not initialized \n",__FUNCTION__,id);
      return ERROR;
    }

  if((chan<0)||(chan>71))
    {
      printf("%s: ERROR: channel (%d) out of range.\n",
	     __FUNCTION__,chan);
      return ERROR;
    }

  return fa125dacOffset[id][chan];

}

int
fa125ReadOffsetToFile(int id, char *filename)
{
  FILE *fd_1;
  int ichan;

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

  fd_1 = fopen(filename,"w");
  if(fd_1 > 0) 
    {
      printf("%s: Writing DAC offsets to file: %s\n",__FUNCTION__,filename);
      for(ichan=0;ichan<72;ichan++) 
	{
	  fprintf(fd_1,"%5d ",fa125dacOffset[id][ichan]);
	  if(((ichan+1)%12)==0)
	    fprintf(fd_1,"\n");
	}
      fprintf(fd_1,"\n");
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
  const int DAC_CHAN_PULSER[3]={35, 19, 11};

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
  const int DAC_CHAN_MULTH=3;

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
 * fa125SetClockSource - Set the clock source
 *
 *   clksrc: defines Clock Source
 *           0  P2 Clock
 *           1  VXS (P0)
 *           2  Internal 125MHz Clock
 *
 */

int
fa125SetClockSource(int id, int clksrc)
{
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      printf("%s: ERROR : FA125 in slot %d is not initialized \n",__FUNCTION__,id);
      return ERROR;
    }

  if(clksrc>2)
    {
      printf("%s: ERROR: Invalid Clock Source specified (%d)\n",
	     __FUNCTION__,clksrc);
      return ERROR;
    }

  switch(clksrc)
    {
    case 0: /* P2 Clock */
      clksrc = FA125_CLOCK_P2;
      break;

    case 1: /* VXS (P0) Clock */
      clksrc = FA125_CLOCK_P0;
      break;

    case 2: /* Internal Clock */
    default:
      clksrc = FA125_CLOCK_INTERNAL;
      break;
    }

  FA125LOCK;
  fa125p[id]->main.clock = clksrc;
  FA125UNLOCK;

  return OK;
}

/*******************************************************************************
 *
 * fa125SetTriggerSource - Set the trigger source
 *
 *   trigsrc: defines Trigger Source
 *           0  P0 (VXS)
 *           1  Internal Timer
 *           2  Internal Sum
 *           3  P2
 *
 */

int
fa125SetTriggerSource(int id, int trigsrc)
{
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      printf("%s: ERROR : FA125 in slot %d is not initialized \n",__FUNCTION__,id);
      return ERROR;
    }

  if((trigsrc<0) || (trigsrc>3))
    {
      printf("%s: ERROR: Invalid Trigger Source specified (%d)\n",
	     __FUNCTION__,trigsrc);
      return ERROR;
    }

  switch(trigsrc)
    {
    case 1: /* Internal Timer */
      trigsrc = FA125_TRIGSRC_TRIGGER_INTERNAL_TIMER;
      break;

    case 2: /* Internal Sum */
      trigsrc = FA125_TRIGSRC_TRIGGER_INTERNAL_SUM;
      break;

    case 3: /* P2 */
      trigsrc = FA125_TRIGSRC_TRIGGER_P2;
      break;

    case 0: /* P0 */
    default:
      trigsrc = FA125_TRIGSRC_TRIGGER_P0;
      break;
    }

  FA125LOCK;
  fa125p[id]->proc.trigsrc = trigsrc;
  FA125UNLOCK;

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
      logMsg("%s: ERROR : FA125 in slot %d is not initialized \n",(int)__FUNCTION__,id,3,4,5,6);
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

  if(rval&FA125_PROC_CSR_BUSY)
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

int
fa125Clear(int id)
{
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("%s: ERROR : FA125 in slot %d is not initialized \n",(int)__FUNCTION__,id,3,4,5,6);
      return ERROR;
    }

  FA125LOCK;
  fa125p[id]->proc.csr=FA125_PROC_CSR_CLEAR;
  fa125p[id]->proc.csr=0;
  FA125UNLOCK;

  return OK;
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
 *                  Bits 4-31:
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
      logMsg("%s: ERROR : FA125 in slot %d is not initialized \n",(int)__FUNCTION__,id,3,4,5,6);
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
		  logMsg("%s (%2d): WARN: Maximum number of words in readout reached (%d)\n",
			 (int)__FUNCTION__,id,nwrds,4,5,6);
		  fa125Clear(id);
		  return dCnt;
		}

	      FA125LOCK;
	      rdata=fa125p[id]->fe[ichan/6].acqfifo[ichan%6];
	      FA125UNLOCK;
	      if(rdata>>31)
		{
		  logMsg("%s (%2d): ERROR: Invalid Data (0x%08x) at channel %d, read %d\n",
			 (int)__FUNCTION__,id,rdata,ichan,iread,6);
#ifndef VXWORKS
		  vmeClearException(1);
#endif
		  fa125Clear(id);
		  return ERROR;
		}
#ifdef NOZERODATA
	      if(rdata==0)
		{
		  logMsg("%s: ERROR (%2d): Empty Data (0x%08x) at channel %d, read %d\n",
			 (int)__FUNCTION__,id,rdata,ichan,iread,5,6);
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
	     (int)__FUNCTION__,rflag,3,4,5,6);
      return ERROR;
    }

  return 0;
}


int
fa125Bready(int id)
{
  int rval=0;
  if(id==0) id=fa125ID[0];

  if((id<=0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("fa125ReadBlock: ERROR : FA125 in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }

  FA125LOCK;
  rval = (fa125p[id]->main.blockCSR & FA125_BLOCKCSR_BLOCK_READY)>>2;
  FA125LOCK;

  return rval;
}

unsigned int
fa125GBready()
{
  int ii, id, stat=0;
  unsigned int dmask=0;
  
  FA125LOCK;
  for(ii=0;ii<nfa125;ii++) 
    {
      id = fa125ID[ii];
      
      stat = (fa125p[id]->main.blockCSR & FA125_BLOCKCSR_BLOCK_READY)>>2;
      
      if(stat)
	dmask |= (1<<id);
    }
  FA125UNLOCK;
  
  return(dmask);
}

unsigned int
fa125ScanMask()
{
  int ifa125, id, dmask=0;

  for(ifa125=0; ifa125<nfa125; ifa125++)
    {
      id = fa125ID[ifa125];
      dmask |= (1<<id);
    }

  return(dmask);
}


/**************************************************************************************
 *
 *  fa125ReadBlock - General Data readout routine
 *
 *    id    - Slot number of module to read
 *    data  - local memory address to place data
 *    nwrds - Max number of words to transfer
 *    rflag - Readout Flag
 *              0 - programmed I/O from the specified board
 *              1 - DMA transfer using Universe/Tempe DMA Engine 
 *                    (DMA VME transfer Mode must be setup prior)
 *              2 - Multiblock DMA transfer (Multiblock must be enabled
 *                     and daisychain in place or SD being used)
 */
int
fa125ReadBlock(int id, volatile UINT32 *data, int nwrds, int rflag)
{
  int ii, blknum;
  int stat, retVal, xferCount, rmode, async;
  int dCnt, berr=0;
  int dummy=0;
  volatile unsigned int *laddr;
  unsigned int bhead, ehead, val;
  unsigned int vmeAdr, csr;

  if(id==0) id=fa125ID[0];

  if((id<=0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("fa125ReadBlock: ERROR : FA125 in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }

  if(data==NULL) 
    {
      logMsg("fa125ReadBlock: ERROR: Invalid Destination address\n",0,0,0,0,0,0);
      return(ERROR);
    }

  fa125BlockError=0;
  if(nwrds <= 0) nwrds= (FA125_MAX_ADC_CHANNELS*FA125_MAX_DATA_PER_CHANNEL) + 8;
  rmode = rflag&0x0f;
  async = rflag&0x80;
  
  if(rmode >= 1) 
    { /* Block Transfers */
    
      /*Assume that the DMA programming is already setup. */
      /* Don't Bother checking if there is valid data - that should be done prior
	 to calling the read routine */

      /* Check for 8 byte boundary for address - insert dummy word (Slot 0 FA125 Dummy DATA)*/
      if((unsigned long) (data)&0x7) 
	{
#ifdef VXWORKS
	  *data = FA215_DUMMY_DATA;
#else
	  *data = LSWAP(FA125_DUMMY_DATA);
#endif
	  dummy = 1;
	  laddr = (data + 1);
	} 
      else 
	{
	  dummy = 0;
	  laddr = data;
	}

      FA125LOCK;
      if(rmode == 2) 
	{ /* Multiblock Mode */
	  if(((fa125p[id]->main.ctrl1)&FA125_CTRL1_FIRST_BOARD)==0) 
	    {
	      logMsg("fa125ReadBlock: ERROR: FA125 in slot %d is not First Board\n",id,0,0,0,0,0);
	      FA125UNLOCK;
	      return(ERROR);
	    }
	  vmeAdr = (unsigned int)(FA125pmb) - fa125A32Offset;
	}
      else
	{
	  vmeAdr = (unsigned int)(fa125pd[id]) - fa125A32Offset;
	}
#ifdef VXWORKS
      retVal = sysVmeDmaSend((UINT32)laddr, vmeAdr, (nwrds<<2), 0);
#else
      retVal = vmeDmaSend((UINT32)laddr, vmeAdr, (nwrds<<2));
#endif
      if(retVal |= 0) 
	{
	  logMsg("fa125ReadBlock: ERROR in DMA transfer Initialization 0x%x\n",retVal,0,0,0,0,0);
	  FA125UNLOCK;
	  return(retVal);
	}

      if(async) 
	{ /* Asynchonous mode - return immediately - don't wait for done!! */
	  FA125UNLOCK;
	  return(OK);
	}
      else
	{
	  /* Wait until Done or Error */
#ifdef VXWORKS
	  retVal = sysVmeDmaDone(10000,1);
#else
	  retVal = vmeDmaDone();
#endif
	}

      if(retVal > 0) 
	{
	  /* Check to see that Bus error was generated by FA125 */
	  if(rmode == 2) 
	    {
	      csr = fa125p[fa125MaxSlot]->main.blockCSR;  /* from Last FA125 */
	      stat = (csr)&FA125_BLOCKCSR_BERR_ASSERTED;  /* from Last FA125 */
	    }
	  else
	    {
	      csr = fa125p[id]->main.blockCSR;  /* from Last FA125 */
	      stat = (csr)&FA125_BLOCKCSR_BERR_ASSERTED;  /* from Last FA125 */
	    }
	  if((retVal>0) && (stat)) 
	    {
#ifdef VXWORKS
	      xferCount = (nwrds - (retVal>>2) + dummy);  /* Number of Longwords transfered */
#else
	      xferCount = ((retVal>>2) + dummy);  /* Number of Longwords transfered */
#endif
	      FA125UNLOCK;
	      return(xferCount); /* Return number of data words transfered */
	    }
	  else
	    {
#ifdef VXWORKS
	      xferCount = (nwrds - (retVal>>2) + dummy);  /* Number of Longwords transfered */
#else
	      xferCount = ((retVal>>2) + dummy);  /* Number of Longwords transfered */
#endif
	      logMsg("fa125ReadBlock: DMA transfer terminated by unknown BUS Error (csr=0x%x xferCount=%d id=%d)\n",
		     csr,xferCount,id,0,0,0);
	      FA125UNLOCK;
	      fa125BlockError=1;
	      return(xferCount);
	    }
	} 
      else if (retVal == 0)
	{ /* Block Error finished without Bus Error */
#ifdef VXWORKS
	  logMsg("fa125ReadBlock: WARN: DMA transfer terminated by word count 0x%x\n",nwrds,0,0,0,0,0);
#else
	  logMsg("fa125ReadBlock: WARN: DMA transfer returned zero word count 0x%x\n",nwrds,0,0,0,0,0);
#endif
	  FA125UNLOCK;
	  fa125BlockError=1;
	  return(nwrds);
	} 
      else 
	{  /* Error in DMA */
#ifdef VXWORKS
	  logMsg("fa125ReadBlock: ERROR: sysVmeDmaDone returned an Error\n",0,0,0,0,0,0);
#else
	  logMsg("fa125ReadBlock: ERROR: vmeDmaDone returned an Error\n",0,0,0,0,0,0);
#endif
	  FA125UNLOCK;
	  fa125BlockError=1;
	  return(retVal>>2);
	}

    } 
  else 
    {  /*Programmed IO */

      /* Check if Bus Errors are enabled. If so then disable for Prog I/O reading */
      FA125LOCK;
      berr = fa125p[id]->main.ctrl1&FA125_CTRL1_ENABLE_BERR;
      if(berr)
	fa125p[id]->main.ctrl1 = fa125p[id]->main.ctrl1 & ~FA125_CTRL1_ENABLE_BERR;

      dCnt = 0;
      /* Read Block Header - should be first word */
      bhead = fa125pd[id]->data; 
#ifndef VXWORKS
      bhead = LSWAP(bhead);
#endif
      if((bhead&FA125_DATA_TYPE_DEFINE)&&((bhead&FA125_DATA_TYPE_MASK) == FA125_DATA_BLOCK_HEADER)) {
	blknum = bhead&FA125_DATA_BLKNUM_MASK;
	ehead = fa125pd[id]->data;
#ifndef VXWORKS
	ehead = LSWAP(ehead);
#endif
#ifdef VXWORKS
	data[dCnt] = bhead;
#else
	data[dCnt] = LSWAP(bhead); /* Swap back to little-endian */
#endif
	dCnt++;
#ifdef VXWORKS
	data[dCnt] = ehead;
#else
	data[dCnt] = LSWAP(ehead); /* Swap back to little-endian */
#endif
	dCnt++;
      }
      else
	{
	  /* We got bad data - Check if there is any data at all */
	  if( ((fa125p[id]->proc.ev_count) & FA125_PROC_EVCOUNT_MASK) == 0) 
	    {
	      logMsg("fa125ReadBlock: FIFO Empty (0x%08x)\n",bhead,0,0,0,0,0);
	      FA125UNLOCK;
	      return(0);
	    } 
	  else 
	    {
	      logMsg("fa125ReadBlock: ERROR: Invalid Header Word 0x%08x\n",bhead,0,0,0,0,0);
	      FA125UNLOCK;
	      return(ERROR);
	    }
	}

      ii=0;
      while(ii<nwrds) 
	{
	  val = fa125pd[id]->data;
	  data[ii+2] = val;
#ifndef VXWORKS
	  val = LSWAP(val);
#endif
	  if( (val&FA125_DATA_TYPE_DEFINE) 
	      && ((val&FA125_DATA_TYPE_MASK) == FA125_DATA_BLOCK_TRAILER) )
	    break;
	  ii++;
	}
      ii++;
      dCnt += ii;


      if(berr)
	fa125p[id]->main.ctrl1 = fa125p[id]->main.ctrl1 | FA125_CTRL1_ENABLE_BERR;

      FA125UNLOCK;
      return(dCnt);
    }

  FA125UNLOCK;
  return(OK);
}

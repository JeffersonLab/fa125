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
 *----------------------------------------------------------------------------*/

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
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
int fa125A32Base   = 0x09000000;                      /* Minimum VME A32 Address for use by FA125s */
int fa125A32Offset = 0x08000000;                      /* Difference in CPU A32 Base - VME A32 Base */
int fa125A24Offset=0;                            /* Difference in CPU A24 Base and VME A24 Base */
unsigned int fa125AddrList[FA125_MAX_BOARDS];            /* array of a24 addresses for FA125s */
int fa125MaxSlot=0;                                   /* Highest Slot hold an FA125 */
int fa125MinSlot=0;                                   /* Lowest Slot holding an FA125 */
int fa125TriggerSource=0;
int berr_count=0; /* A count of the number of BERR that have occurred when running fa125Poll() */
/* store the dacOffsets in the library, until the firmware is able to read them back */
static unsigned short fa125dacOffset[FA125_MAX_BOARDS+1][72];
int fa125BlockError=FA125_BLOCKERROR_NO_ERROR;       /* Whether (1) or not (0) Block Transfer had an error */

/*******************************************************************************
 *
 * fa125Init - Initialize the fa125 Library
 *
 *   iFlag: 17 bit integer
 *       Low 6 bits - Specifies the default Signal distribution (clock,trigger) 
 *                    sources for the board (Internal, FrontPanel, VXS, VME(Soft))
 *       bit    0:  defines Sync Reset source
 *                 0  VXS (P0)
 *                 1  VME (software)
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
 *
 *     18:  Skip firmware check.  Useful for firmware updating.
 *             0 Perform firmware check
 *             1 Skip firmware check
 *
 */

int
fa125Init (UINT32 addr, UINT32 addr_inc, int nadc, int iFlag)
{
  int res=0;
  volatile unsigned int rdata=0;
  unsigned int laddr=0, a32addr=0;
  volatile struct fa125_a24 *fa125;
  int useList=0, noBoardInit=0, noFirmwareCheck=0;
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
  if(iFlag & FA125_INIT_SKIP)
    noBoardInit=1;

  /* Check if we're initializing using a list */
  if(iFlag & FA125_INIT_USE_ADDRLIST)
    {
      useList=1;
      nfind = nadc;
    }

  /* Are we skipping the firmware check? */
  if(iFlag & FA125_INIT_SKIP_FIRMWARE_CHECK)
    {
      noFirmwareCheck=1;
      printf("%s: INFO: Firmware Check Disabled\n",
	     __FUNCTION__);
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

  /* Calculate the A32 Offset for use in Block Transfers */
#ifdef VXWORKS
  res = sysBusToLocalAdrs(0x09,(char *)fa125A32Base,(char **)&laddr);
  if (res != 0) 
    {
      printf("faInit: ERROR in sysBusToLocalAdrs(0x09,0x%x,&laddr) \n",fa125A32Base);
      return(ERROR);
    } 
  else 
    {
      fa125A32Offset = laddr - fa125A32Base;
    }
#else
  res = vmeBusToLocalAdrs(0x09,(char *)fa125A32Base,(char **)&laddr);
  if (res != 0) 
    {
      printf("faInit: ERROR in vmeBusToLocalAdrs(0x09,0x%x,&laddr) \n",fa125A32Base);
      return(ERROR);
    } 
  else 
    {
      fa125A32Offset = laddr - fa125A32Base;
    }
#endif

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
	  printf("%s: WARN: No addressable board at addr=0x%x\n",
		 __FUNCTION__,(UINT32) fa125AddrList[islot]);
	}
      else 
	{
	  /* Check that it is an FA125 */
	  if(rdata != FA125_ID)
	    {
	      printf("%s: ERROR: For module at 0x%x, Invalid Board ID: 0x%x\n",
		     __FUNCTION__,fa125AddrList[islot],rdata);
	      continue;
	    }
	  else
	    {
	      /* Check the Firmware Versions */
	      if(!noFirmwareCheck)
		{
		  int fw_error=0;
		  /* MAIN */
		  fw_version = vmeRead32(&fa125->main.version);
		  if(fw_version==0xffffffff)
		    { /* buggy firmware.. re-read */
		      printf("%s: bum main read\n",__FUNCTION__);
		      fw_version = vmeRead32(&fa125->main.version);
		    }
		  if(fw_version != FA125_MAIN_SUPPORTED_FIRMWARE)
		    {
		      printf("%s: ERROR: For module at 0x%x, Unsupported MAIN firmware version 0x%x\n",
			     __FUNCTION__,fa125AddrList[islot],fw_version);
		      fw_error=1;
		    }

		  /* PROC */
		  fw_version = vmeRead32(&fa125->proc.version);
		  if(fw_version==0xffffffff)
		    { /* buggy firmware.. re-read */
		      printf("%s: bum proc read\n",__FUNCTION__);
		      fw_version = vmeRead32(&fa125->proc.version);
		    }
		  if(fw_version != FA125_PROC_SUPPORTED_FIRMWARE)
		    {
		      printf("%s: ERROR: For module at 0x%x, Unsupported PROC firmware version 0x%x\n",
			     __FUNCTION__,fa125AddrList[islot],fw_version);
		      fw_error=1;
		    }

		  /* FE - just check the first one */
		  fw_version = vmeRead32(&fa125->fe[0].version);
		  if(fw_version==0xffffffff)
		    { /* buggy firmware.. re-read */
		      printf("%s: bum FE read\n",__FUNCTION__);
		      fw_version = vmeRead32(&fa125->fe[0].version);
		    }
		  if(fw_version != FA125_FE_SUPPORTED_FIRMWARE)
		    {
		      printf("%s: ERROR: For module at 0x%x, Unsupported FE firmware version 0x%x\n",
			     __FUNCTION__,fa125AddrList[islot],fw_version);
		      fw_error=1;
		    }

		  if(fw_error)
		    continue;
		}

	      /* Get the Geographic Address */
	      boardID = vmeRead32(&fa125->main.slot_ga);

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
  srSrc = (iFlag&0x1);

  /* Trigger */
  trigSrc = (iFlag&0x6)>>1;
  fa125TriggerSource = trigSrc;

  /* Clock Source */
  clkSrc = (iFlag&0x30)>>4;

  /* Perform some initialization here */
  for(islot=0; islot<nfa125; islot++)
    {
      FA_SLOT = fa125ID[islot];

      fa125Clear(FA_SLOT);

      /* Set the clock source */
      fa125SetClockSource(FA_SLOT,clkSrc);

      /* Set the trigger source */
      fa125SetTriggerSource(FA_SLOT,fa125TriggerSource);

      /* Set the SyncReset source */
      fa125SetSyncResetSource(FA_SLOT,srSrc);
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
	  vmeWrite32(&fa125p[fa125ID[ii]]->main.adr32, (a32addr>>16) | FA125_ADR32_ENABLE);  /* Write the register and enable */
	  vmeWrite32(&fa125p[fa125ID[ii]]->main.ctrl1, 
			     vmeRead32(&fa125p[fa125ID[ii]]->main.ctrl1) | FA125_CTRL1_ENABLE_BERR);/* Enable Bus Error termination */
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
	  unsigned int ctrl1=0;
	  for (ii=0;ii<nfa125;ii++) 
	    {
	      /* Write to the register and enable */
	      vmeWrite32(&fa125p[fa125ID[ii]]->main.adr_mb,
			 (a32addr+FA125_MAX_A32MB_SIZE) | (a32addr>>16) | FA125_ADRMB_ENABLE);
	      ctrl1 = vmeRead32(&fa125p[fa125ID[ii]]->main.ctrl1) & 
		~(FA125_CTRL1_FIRST_BOARD | FA125_CTRL1_LAST_BOARD);
	      vmeWrite32(&fa125p[fa125ID[ii]]->main.ctrl1,
			 ctrl1 | FA125_CTRL1_ENABLE_MULTIBLOCK);
	    }
	}    
      /* Set First Board and Last Board */
      fa125MaxSlot = maxSlot;
      fa125MinSlot = minSlot;
      if(!noBoardInit)
	{
	  vmeWrite32(&fa125p[minSlot]->main.ctrl1, 
		     vmeRead32(&fa125p[minSlot]->main.ctrl1) | FA125_CTRL1_FIRST_BOARD);
	  vmeWrite32(&fa125p[maxSlot]->main.ctrl1,
		     vmeRead32(&fa125p[maxSlot]->main.ctrl1) | FA125_CTRL1_LAST_BOARD);
	}    
    }

  if(nfa125 > 0)
    printf("%s: %d FA125(s) successfully initialized\n",__FUNCTION__,nfa125);

  return(OK);

}

void
fa125CheckAddresses(int id)
{
  unsigned int offset=0, expected=0, base=0;
  int ife=0;

  if(id==0) id=fa125ID[0];
  if((id<=0) || (id>21) || (fa125p[id]==NULL))
    {
      printf("%s: ERROR: FA125 in slot %d not initialized\n",__FUNCTION__,id);
      return;
    }
  
  printf("%s:\n\t ---------- Checking FA125 address space ---------- \n",__FUNCTION__);

  base = (unsigned int) &fa125p[id]->main;

  for(ife=0; ife<12; ife++)
    {
      offset = ((unsigned int) &fa125p[id]->fe[ife]) - base;
      expected = 0x1000 + ife*0x1000;
      if(offset != expected)
	printf("%s: ERROR fa125p[%d]->fe[%d] not at offset = 0x%x (@ 0x%x)\n",
	       __FUNCTION__,id,ife,expected,offset);
    }

  offset = ((unsigned int) &fa125p[id]->proc) - base;
  expected = 0xD000;
  if(offset != expected)
    printf("%s: ERROR fa125p[%d]->proc not at offset = 0x%x (@ 0x%x)\n",
	   __FUNCTION__,id,expected,offset);

  offset = ((unsigned int) &fa125p[id]->proc.trigsrc) - base;
  expected = 0xD008;
  if(offset != expected)
    printf("%s: ERROR fa125p[%d]->proc.trigsrc not at offset = 0x%x (@ 0x%x)\n",
	   __FUNCTION__,id,expected,offset);

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
fa125Status(int id, int pflag)
{
  struct fa125_a24_main m;
  struct fa125_a24_proc p;
  struct fa125_a24_fe   f[12];
  unsigned int clksrc, trigsrc, srsrc;
  unsigned int faBase, a32Base, ambMin, ambMax;
  int i=0, showregs=0;

  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      printf("%s: ERROR : FA125 in slot %d is not initialized \n",__FUNCTION__,id);
      return ERROR;
    }

  if(pflag & FA125_STATUS_SHOWREGS)
    showregs=1;

  FA125LOCK;
  m.id = vmeRead32(&fa125p[id]->main.id);
  m.swapctl = vmeRead32(&fa125p[id]->main.swapctl);
  m.version = vmeRead32(&fa125p[id]->main.version);
  m.pwrctl = vmeRead32(&fa125p[id]->main.pwrctl);
  m.slot_ga = vmeRead32(&fa125p[id]->main.slot_ga);
  m.clock = vmeRead32(&fa125p[id]->main.clock);

  for(i=0; i<4; i++)
    m.serial[i] = vmeRead32(&fa125p[id]->main.serial[i]);

  f[0].version = vmeRead32(&fa125p[id]->fe[0].version);
  if(f[0].version==0xffffffff)
    {
      f[0].version = vmeRead32(&fa125p[id]->fe[0].version);
    }

  p.version = vmeRead32(&fa125p[id]->proc.version);
  p.csr     = vmeRead32(&fa125p[id]->proc.csr);
  p.trigsrc = vmeRead32(&fa125p[id]->proc.trigsrc);
  p.ctrl2   = vmeRead32(&fa125p[id]->proc.ctrl2);

  m.adr32        = vmeRead32(&fa125p[id]->main.adr32);
  m.adr_mb       = vmeRead32(&fa125p[id]->main.adr_mb);

  m.ctrl1        = vmeRead32(&fa125p[id]->main.ctrl1);

  m.block_count  = vmeRead32(&fa125p[id]->main.block_count);

  p.trig_count   = vmeRead32(&fa125p[id]->proc.trig_count);
  p.ev_count     = vmeRead32(&fa125p[id]->proc.ev_count);

  m.blockCSR     = vmeRead32(&fa125p[id]->main.blockCSR);

  f[0].config1   = vmeRead32(&fa125p[id]->fe[0].config1);
  f[0].ptw       = vmeRead32(&fa125p[id]->fe[0].ptw);
  f[0].pl        = vmeRead32(&fa125p[id]->fe[0].pl);
  f[0].nsb       = vmeRead32(&fa125p[id]->fe[0].nsb);
  f[0].nsa       = vmeRead32(&fa125p[id]->fe[0].nsa);

  for(i=0; i<12; i++)
    {
      f[i].test  = vmeRead32(&fa125p[id]->fe[i].test);
    }
  FA125UNLOCK;

  faBase  = (unsigned int) &fa125p[id]->main.id;
  a32Base = (m.adr32 & FA125_ADR32_BASE_MASK)<<16;
  ambMin  = (m.adr_mb & FA125_ADRMB_MIN_MASK)<<16;
  ambMax  = (m.adr_mb & FA125_ADRMB_MAX_MASK);

  #ifdef VXWORKS
  printf("\nSTATUS for FA125 in slot %d at base address 0x%x \n",
	 id, (UINT32) fa125p[id]);
#else
  printf("\nSTATUS for FA125 in slot %d at VME (Local) base address 0x%x (0x%x)\n",
	 id, (UINT32) fa125p[id] - fa125A24Offset, (UINT32) fa125p[id]);
#endif
  printf("---------------------------------------------------------------------- \n");
  printf(" Main Firmware Revision     = 0x%08x\n",
	 m.version);
  printf(" FrontEnd Firmware Revision = 0x%08x\n",
	 f[0].version);
  printf(" Processing Revision        = 0x%08x\n",
	 p.version);

  printf("      Main SN = 0x%04x%08x\n",m.serial[0], m.serial[1]);
  printf(" Mezzanine SN = 0x%04x%08x\n",m.serial[2], m.serial[3]);
  
  printf("\n");
  if(showregs)
    {
      printf("Registers:\n");
      printf("  blockCSR       (0x%04x) = 0x%08x\t", 
	     (unsigned int)(&fa125p[id]->main.blockCSR) - faBase, m.blockCSR);
      printf("  ctrl1          (0x%04x) = 0x%08x\n", 
	     (unsigned int)(&fa125p[id]->main.ctrl1) - faBase, m.ctrl1);
      printf("  adr32          (0x%04x) = 0x%08x\t", 
	     (unsigned int)(&fa125p[id]->main.adr32) - faBase, m.adr32);
      printf("  adr_mb         (0x%04x) = 0x%08x\n", 
	     (unsigned int)(&fa125p[id]->main.adr_mb) - faBase, m.adr_mb);
      printf("  trigsrc        (0x%04x) = 0x%08x\t", 
	     (unsigned int)(&fa125p[id]->proc.trigsrc) - faBase, p.trigsrc);

      printf("  clock          (0x%04x) = 0x%08x\n", 
	     (unsigned int)(&fa125p[id]->main.clock) - faBase, m.clock);

      printf("\n");

      for(i=0; i<12; i=i+2)
	{
	  printf("  test %2d        (0x%04x) = 0x%08x\t", i,
		 (unsigned int)(&fa125p[id]->fe[i].test) - faBase, f[i].test);
	  printf("  test %2d        (0x%04x) = 0x%08x\n", i+1,
		 (unsigned int)(&fa125p[id]->fe[i+1].test) - faBase, f[i+1].test);
	}

      printf("\n");
    }

  if(m.ctrl1 & FA125_CTRL1_ENABLE_MULTIBLOCK) 
    {
      printf(" Alternate VME Addressing: Multiblock Enabled\n");
      if(m.adr32&FA125_ADR32_ENABLE)
	printf("   A32 Enabled at VME (Local) base 0x%08x (0x%08x)\n",a32Base,
	       (UINT32) fa125pd[id]);
      else
	printf("   A32 Disabled\n");
    
      printf("   Multiblock VME Address Range 0x%08x - 0x%08x\n",ambMin,ambMax);
    }
  else
    {
      printf(" Alternate VME Addressing: Multiblock Disabled\n");
      if(m.adr32&FA125_ADR32_ENABLE)
	printf("   A32 Enabled at VME (Local) base 0x%08x (0x%08x)\n",a32Base,
	       (UINT32) fa125pd[id]);
      else
	printf("   A32 Disabled\n");
    }
  printf("\n");

  /* POWER */
  if(m.pwrctl)
    printf(" Power is ON\n");
  else
    printf(" Power is OFF\n");

  /* CLOCK */
  printf(" Clock Source (0x%x):",m.clock);
  clksrc = m.clock & 0xffff;
  if(clksrc == FA125_CLOCK_P2)
    printf(" P2\n");
  else if (clksrc == FA125_CLOCK_P0)
    printf(" P0 (VXS)\n");
  else if (clksrc == FA125_CLOCK_INTERNAL)
    printf(" Internal\n");
  else
    printf(" ????\n");

  /* TRIGGER */
  printf(" Trigger Source (0x%x):",p.trigsrc);
  trigsrc = p.trigsrc & FA125_TRIGSRC_TRIGGER_MASK;
  if(trigsrc == FA125_TRIGSRC_TRIGGER_P0)
    printf(" P0\n");
  else if (trigsrc == FA125_TRIGSRC_TRIGGER_INTERNAL_TIMER)
    printf(" Internal Timer\n");
  else if (trigsrc == FA125_TRIGSRC_TRIGGER_INTERNAL_SUM)
    printf(" Internal Sum\n");
  else if (trigsrc == FA125_TRIGSRC_TRIGGER_P2)
    printf(" P2\n");

  /* SYNCRESET */
  printf(" SyncReset Source:");
  srsrc = (p.ctrl2 & FA125_PROC_CTRL2_SYNCRESET_SOURCE_MASK)>>2;
  if(srsrc == FA125_PROC_CTRL2_SYNCRESET_P0)
    printf(" P0\n");
  else if (srsrc == FA125_PROC_CTRL2_SYNCRESET_VME)
    printf(" VME (software)\n");

  printf("\n");

  printf(" Bus Error %s\n",
	 (m.ctrl1&FA125_CTRL1_ENABLE_BERR)?"ENABLED":"DISABLED");

  if(m.ctrl1 & FA125_CTRL1_ENABLE_MULTIBLOCK)
    {
      if(m.ctrl1&FA125_CTRL1_FIRST_BOARD)
	printf(" MultiBlock transfer ENABLED (First Board)\n");
      else if(m.ctrl1&FA125_CTRL1_LAST_BOARD)
	printf(" MultiBlock transfer ENABLED (Last Board)\n");
      else
	printf(" MultiBlock transfer ENABLED\n");
    }
  else
    printf(" MultiBlock transfer DISABLED\n");

  printf("\n");

  printf(" Processing Configuration: \n");
    printf("   Mode = %d  (%s)  - %s\n",
	   (f[0].config1&FA125_FE_CONFIG1_MODE_MASK)+1,
	   fa125_mode_names[f[0].config1&FA125_FE_CONFIG1_MODE_MASK],
	   (f[0].config1 & FA125_FE_CONFIG1_ENABLE)?"ENABLED":"DISABLED");
  printf("   Lookback (PL)    = %d ns   Time Window (PTW) = %d ns\n",
	 8*f[0].pl, 8*f[0].ptw);
  printf("   Time Before Peak = %d ns   Time After Peak   = %d ns\n",
	 8*f[0].nsb,8*f[0].nsa);
  printf("   Max Peak Count   = %d \n",(f[0].config1 & FA125_FE_CONFIG1_NPULSES_MASK)>>5);
  printf("   Playback Mode    = %s \n",
	 (f[0].config1 & FA125_FE_CONFIG1_PLAYBACK_ENABLE)?"ENABLED":"DISABLED");

  printf("\n");

  printf(" Block Count = %d\n",m.block_count);
  printf(" Trig  Count = %d\n",p.trig_count);
  printf(" Ev    Count = %d\n",p.ev_count);

  printf("---------------------------------------------------------------------- \n");
  return OK;

}

void
fa125GStatus(int pflag)
{
  int ifa, id;
  struct fa125_a24 st[20];
  unsigned int a24addr[20];

  FA125LOCK;
  for (ifa=0;ifa<nfa125;ifa++) 
    {
      id = fa125Slot(ifa);
      a24addr[id]    = (unsigned int)fa125p[id] - fa125A24Offset;

      st[id].main.version     = vmeRead32(&fa125p[id]->main.version);
      st[id].main.adr32       = vmeRead32(&fa125p[id]->main.adr32);
      st[id].main.adr_mb      = vmeRead32(&fa125p[id]->main.adr_mb);
      st[id].main.pwrctl      = vmeRead32(&fa125p[id]->main.pwrctl);
      st[id].main.clock       = vmeRead32(&fa125p[id]->main.clock);
      st[id].main.ctrl1       = vmeRead32(&fa125p[id]->main.ctrl1);
      st[id].main.blockCSR    = vmeRead32(&fa125p[id]->main.blockCSR);
      st[id].main.block_count = vmeRead32(&fa125p[id]->main.block_count);


      st[id].proc.version     = vmeRead32(&fa125p[id]->proc.version);
      st[id].proc.trigsrc     = vmeRead32(&fa125p[id]->proc.trigsrc);
      st[id].proc.ctrl2       = vmeRead32(&fa125p[id]->proc.ctrl2);
      st[id].proc.blocklevel  = vmeRead32(&fa125p[id]->proc.blocklevel);
      st[id].proc.trig_count  = vmeRead32(&fa125p[id]->proc.trig_count);
      st[id].proc.trig2_count = vmeRead32(&fa125p[id]->proc.trig2_count);
      st[id].proc.sync_count  = vmeRead32(&fa125p[id]->proc.sync_count);



      st[id].fe[0].version = vmeRead32(&fa125p[id]->fe[0].version);
      st[id].fe[0].config1 = vmeRead32(&fa125p[id]->fe[0].config1);
      st[id].fe[0].pl      = vmeRead32(&fa125p[id]->fe[0].pl);
      st[id].fe[0].ptw     = vmeRead32(&fa125p[id]->fe[0].ptw);
      st[id].fe[0].nsa     = vmeRead32(&fa125p[id]->fe[0].nsa);
      st[id].fe[0].nsb     = vmeRead32(&fa125p[id]->fe[0].nsb);
    }
  FA125UNLOCK;

  printf("\n");
  
  printf("                      fADC125 Module Configuration Summary\n\n");
  printf("     ..........Firmware Rev.......... .................Addresses................\n");
  printf("Slot    Main        FE        Proc       A24        A32     A32 Multiblock Range\n");
  printf("--------------------------------------------------------------------------------\n");

  for(ifa=0; ifa<nfa125; ifa++)
    {
      id = fa125Slot(ifa);
      printf(" %2d   ",id);

      printf("%08x   %08x   %08x   ",
	     st[id].main.version, st[id].fe[0].version, st[id].proc.version);

      printf("%06x    ",
	     a24addr[id]);

      if(st[id].main.adr32 & FA125_ADR32_ENABLE)
	{
	  printf("%08x    ",
		 (st[id].main.adr32&FA125_ADR32_BASE_MASK)<<16);
	}
      else
	{
	  printf("  Disabled   ");
	}

      if(st[id].main.adr_mb & FA125_ADRMB_ENABLE) 
	{
	  printf("%08x-%08x",
		 (st[id].main.adr_mb&FA125_ADRMB_MIN_MASK)<<16,
		 (st[id].main.adr_mb&FA125_ADRMB_MAX_MASK));
	}
      else
	{
	  printf("Disabled");
	}

      printf("\n");
    }
  printf("--------------------------------------------------------------------------------\n");


  printf("\n");
  printf("              .Signal Sources..                        \n");
  printf("Slot  Power   Clk   Trig   Sync     MBlk  Token  BERR  \n");
  printf("--------------------------------------------------------------------------------\n");
  for(ifa=0; ifa<nfa125; ifa++)
    {
      id = fa125Slot(ifa);
      printf(" %2d    ",id);

      printf("%s   ",
	     st[id].main.pwrctl ? " ON" : "OFF");

      printf("%s  ", 
	     (st[id].main.clock & FA125_CLOCK_MASK)==FA125_CLOCK_INTERNAL ? " INT " :
	     (st[id].main.clock & FA125_CLOCK_MASK)==FA125_CLOCK_INTERNAL_ENABLE ? "*INT*" :
	     (st[id].main.clock & FA125_CLOCK_MASK)==FA125_CLOCK_P0 ? " VXS " :
	     (st[id].main.clock & FA125_CLOCK_MASK)==FA125_CLOCK_P2 ? "  P2 " :
	     " ??? ");

      printf("%s  ",
	     (st[id].proc.trigsrc & FA125_TRIGSRC_TRIGGER_MASK)
	     ==FA125_TRIGSRC_TRIGGER_INTERNAL_TIMER ? "TIMER" :
	     (st[id].proc.trigsrc & FA125_TRIGSRC_TRIGGER_MASK)
	     ==FA125_TRIGSRC_TRIGGER_INTERNAL_SUM ? " SUM " :
	     (st[id].proc.trigsrc & FA125_TRIGSRC_TRIGGER_MASK)
	     ==FA125_TRIGSRC_TRIGGER_P0 ? " VXS " :
	     (st[id].proc.trigsrc & FA125_TRIGSRC_TRIGGER_MASK)
	     ==FA125_TRIGSRC_TRIGGER_P2 ? "  P2 " :
	     " ??? ");

      printf("%s     ",
	     (st[id].proc.ctrl2 & FA125_PROC_CTRL2_SYNCRESET_SOURCE_MASK)>>2
	     == FA125_PROC_CTRL2_SYNCRESET_P0 ? " P0 " :
	     (st[id].proc.ctrl2 & FA125_PROC_CTRL2_SYNCRESET_SOURCE_MASK)>>2
	     == FA125_PROC_CTRL2_SYNCRESET_VME? " VXS " :
	     " ??? ");

      printf("%s   ",
	     (st[id].main.ctrl1 & FA125_CTRL1_ENABLE_MULTIBLOCK) ? "YES":" NO");

      printf(" P0");
      printf("%s  ",
	     st[id].main.ctrl1 & (FA125_CTRL1_FIRST_BOARD) ? "-F":
	     st[id].main.ctrl1 & (FA125_CTRL1_LAST_BOARD) ? "-L":
	     "  ");

      printf("%s     ",
	     st[id].main.ctrl1 & FA125_CTRL1_ENABLE_BERR ? "YES" : " NO");

      printf("\n");
    }
  printf("--------------------------------------------------------------------------------\n");

  printf("\n");
  printf("                        fADC125 Processing Mode Config\n\n");
  printf("      Block                                             \n");
  printf("Slot  Level  Mode    PL   PTW   NSB  NSA  NP   Playback \n");
  printf("--------------------------------------------------------------------------------\n");
  for(ifa=0; ifa<nfa125; ifa++)
    {
      id = fa125Slot(ifa);
      printf(" %2d    ",id);

      printf("%3d     ",st[id].proc.blocklevel & FA125_PROC_BLOCKLEVEL_MASK);

      printf("%d    ",(st[id].fe[0].config1 & FA125_FE_CONFIG1_MODE_MASK) + 1);

      printf("%4d ", 8*st[id].fe[0].pl);

      printf("%4d   ", 8*st[id].fe[0].ptw);

      printf("%3d  ", 8*st[id].fe[0].nsb);

      printf("%3d  ", 8*st[id].fe[0].nsa);

      printf("%1d    ", (st[id].fe[0].config1 & FA125_FE_CONFIG1_NPULSES_MASK)>>5);

      printf("%s   ",
	     (st[id].fe[0].config1 & FA125_FE_CONFIG1_PLAYBACK_ENABLE) ?" Enabled":"Disabled");

      printf("\n");
    }
  printf("--------------------------------------------------------------------------------\n");

  printf("\n");
  printf("                        fADC125 Signal Scalers\n\n");
  printf("Slot       Trig1       Trig2   SyncReset\n");
  printf("--------------------------------------------------------------------------------\n");
  for(ifa=0; ifa<nfa125; ifa++)
    {
      id = fa125Slot(ifa);
      printf(" %2d   ",id);

      printf("%10d  ", st[id].proc.trig_count);

      printf("%10d  ", st[id].proc.trig2_count);

      printf("%10d  ", st[id].proc.sync_count);

      printf("\n");
    }
  printf("--------------------------------------------------------------------------------\n");

  printf("\n");
  printf("                        fADC125 Data Status\n\n");
  printf("      Trigger   Block                 \n");
  printf("Slot  Source    Ready  Blocks In Fifo \n");
  printf("--------------------------------------------------------------------------------\n");
  for(ifa=0; ifa<nfa125; ifa++)
    {
      id = fa125Slot(ifa);
      printf(" %2d  ",id);

      printf("%s    ",
	     st[id].fe[0].config1 & FA125_FE_CONFIG1_ENABLE ? " Enabled" : "Disabled");

      printf("%s       ",
	     st[id].main.blockCSR & FA125_BLOCKCSR_BLOCK_READY ? "YES" : " NO");

      printf("%10d ",
	     st[id].main.block_count & FA125_BLOCKCOUNT_MASK);

      printf("\n");
    }
  printf("--------------------------------------------------------------------------------\n");

  printf("\n");
  printf("\n");

}


/***********************
 *
 *  faSetProcMode - Setup ADC processing modes.
 *
 */

int
fa125SetProcMode(int id, int pmode, unsigned int PL, unsigned int PTW, 
		 unsigned int NSB, unsigned int NSA, unsigned int NP)
{
  int err=0;
  unsigned int ptw_last_adr, ptw_max_buf;
  int imode=0, supported_modes[FA125_SUPPORTED_NMODES] = {FA125_SUPPORTED_MODES};
  int mode_supported=0;
  
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      printf("%s: ERROR : FA125 in slot %d is not initialized \n",__FUNCTION__,id);
      return ERROR;
    }

  for(imode=0; imode<FA125_SUPPORTED_NMODES; imode++)
    {
      if(pmode == supported_modes[imode])
	mode_supported=1;
    }
  if(!mode_supported)
    {
      printf("%s: ERROR: Processing Mode (%d) not supported\n",
	     __FUNCTION__,pmode);
      return ERROR;
    }

  if(NP>3) 
    {
      printf("%s: ERROR: Invalid Peak count %d (must be 0-3)\n",
	     __FUNCTION__,NP);
      return ERROR;
    }

  /* Defaults */
  if((PL==0)||(PL>FA125_MAX_PL))  PL  = FA125_DEFAULT_PL;
  if((PTW==0)||(PTW>FA125_MAX_PTW)) PTW = FA125_DEFAULT_PTW;
  if((NSB==0)||(NSB>FA125_MAX_NSB)) NSB = FA125_DEFAULT_NSB;
  if((NSA==0)||(NSA>FA125_MAX_NSA)) NSA = FA125_DEFAULT_NSA;
  if((NP==0)&&(pmode!=FA125_PROC_MODE_RAWWINDOW))  NP = FA125_DEFAULT_NP;

  /* Consistancy check */
  if(PTW > PL) 
    {
      err++;
      printf("%s: ERROR: Window must be <= Latency\n",__FUNCTION__); 
    }
  if(((NSB+NSA)%2)==0) 
    {
      err++;
      printf("%s: ERROR: NSB+NSA must be an odd number\n",__FUNCTION__); 
    }

  /* Calculate Proc parameters */
  ptw_max_buf  = (unsigned int) (2016/(PTW + 8));
  ptw_last_adr = ptw_max_buf * (PTW + 8) - 1;

  FA125LOCK;
  /* Disable ADC processing while writing window info */
  vmeWrite32(&fa125p[id]->fe[0].config1, ((pmode-1) | (NP<<5)));
  vmeWrite32(&fa125p[id]->fe[0].pl, PL);
  vmeWrite32(&fa125p[id]->fe[0].ptw, PTW);
  vmeWrite32(&fa125p[id]->fe[0].ptw_max_buf, ptw_max_buf);
  vmeWrite32(&fa125p[id]->fe[0].ptw_last_adr, ptw_last_adr);

  /* Enable ADC processing */
  vmeWrite32(&fa125p[id]->fe[0].config1, ((pmode-1) | (NP<<5) | FA125_FE_CONFIG1_ENABLE) );

  FA125UNLOCK;


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
    vmeWrite32(&fa125p[id]->main.swapctl, 0xffffffff);
  else
    vmeWrite32(&fa125p[id]->main.swapctl, 0);
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
	 vmeRead32(&fa125p[id]->main.pwrctl));
#endif

  FA125LOCK;
  vmeWrite32(&fa125p[id]->main.pwrctl, 0);
  FA125UNLOCK;

#ifdef DEBUG
  printf("%s: pwrctl (0x%08x)= 0x%08x\n",__FUNCTION__,
	 ((unsigned int) &fa125p[id]->main.pwrctl) -
	 ((unsigned int)&fa125p[id]->main.id),
	 vmeRead32(&fa125p[id]->main.pwrctl));
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
	 vmeRead32(&fa125p[id]->main.pwrctl));
#endif

  FA125LOCK;
  vmeWrite32(&fa125p[id]->main.pwrctl, FA125_PWRCTL_KEY_ON);
  FA125UNLOCK;

#ifdef DEBUG
  printf("%s: pwrctl (0x%08x)= 0x%08x\n",__FUNCTION__,
	 ((unsigned int) &fa125p[id]->main.pwrctl) -
	 ((unsigned int)&fa125p[id]->main.id),
	 vmeRead32(&fa125p[id]->main.pwrctl));
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
      vmeWrite32(&fa125p[id]->main.csr,
		 vmeRead32(&fa125p[id]->main.csr) & ~FA125_MAIN_CSR_TEST_TRIGGER);
    }
  else if(mode==1) /* turn test trigger on */
    {
      vmeWrite32(&fa125p[id]->main.csr,
		 vmeRead32(&fa125p[id]->main.csr) | FA125_MAIN_CSR_TEST_TRIGGER);
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
	vmeWrite32(&fa125p[id]->main.dacctl, x);
	vmeWrite32(&fa125p[id]->main.dacctl, x | FA125_DACCTL_DACSCLK_MASK);
      }

  vmeWrite32(&fa125p[id]->main.dacctl, 0);  // this deasserts CS, setting the DAC
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

int
fa125SetThreshold(int id, unsigned short tvalue, unsigned short chan)
{
  if(id==0) id=fa125ID[0];

  if((id<=0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("fa125SetThreshold: ERROR : FA125 in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }

  if(tvalue>FA125_FE_THRESHOLD_MASK)
    {
      logMsg("fa125SetThreshold: ERROR: Invalid threshold (%d). Must be <= %d \n",
	     tvalue,FA125_FE_THRESHOLD_MASK,3,4,5,6);
      return ERROR;
    }
  
  if(chan>=FA125_MAX_ADC_CHANNELS)
  {
      logMsg("fa125SetThreshold: ERROR: Invalid channel (%d). Must be 0-%d\n",
	     chan,FA125_MAX_ADC_CHANNELS,3,4,5,6);
      return ERROR;
    }
  

  FA125LOCK;
  vmeWrite32(&fa125p[id]->fe[chan/6].threshold[chan%6],tvalue);
  FA125UNLOCK;

  return(OK);
}

int
fa125SetChannelDisable(int id, int channel)
{
  int feChip=0, feChan=0;
  unsigned int chipMask=0;
  if(id==0) id=fa125ID[0];

  if((id<=0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("fa125SetChannelDisable: ERROR : FA125 in slot %d is not initialized \n",
	     id,0,0,0,0,0);
      return(ERROR);
    }

  if((channel<0) || (channel>=FA125_MAX_ADC_CHANNELS))
    {
      logMsg("faSetChannelDisable: ERROR: Invalid channel (%d).  Must be 0-%d\n",
	     channel, FA125_MAX_ADC_CHANNELS-1,3,4,5,6);
      return ERROR;
    }

  feChip = (int)(channel/6);
  feChan = (int)(channel%6);

  FA125LOCK;
  chipMask = (vmeRead32(&fa125p[id]->fe[feChip].config2) & FA125_FE_CONFIG2_CH_MASK) 
    | (1<<feChan);
  vmeWrite32(&fa125p[id]->fe[feChip].config2,chipMask);
  FA125UNLOCK;

  return(OK);
}

int
fa125SetChannelDisableMask(int id, unsigned int cmask0, 
			   unsigned int cmask1, unsigned int cmask2)
{
  int ichip=0;
  unsigned int chipMask=0;
  if(id==0) id=fa125ID[0];

  if((id<=0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("fa125SetChannelDisableMask: ERROR : FA125 in slot %d is not initialized \n",
	     id,0,0,0,0,0);
      return(ERROR);
    }
  
  if((cmask0 > 0xFFFFFF) || (cmask1 > 0xFFFFFF) || (cmask2 > 0xFFFFFF))
    {
      logMsg("fa125SetChannelDisableMask: ERROR : Invalid channel mask(s) (0x%08x, 0x%08x, 0x%08x).\n",
	     cmask0,cmask1,cmask2,0,0,0);
      logMsg("                          : Each mask must be less than 24 bits.\n",
	     0,0,0,0,0,0);
      return(ERROR);
    }

  FA125LOCK;
  for(ichip=0; ichip<4; ichip++)
    {
      chipMask = (cmask0>>(ichip*6)) & FA125_FE_CONFIG2_CH_MASK;
      vmeWrite32(&fa125p[id]->fe[ichip].config2,chipMask);
    }
  for(ichip=4; ichip<8; ichip++)
    {
      chipMask = (cmask1>>((ichip-4)*6)) & FA125_FE_CONFIG2_CH_MASK;
      vmeWrite32(&fa125p[id]->fe[ichip].config2,chipMask);
    }
  for(ichip=8; ichip<12; ichip++)
    {
      chipMask = (cmask2>>((ichip-8)*6)) & FA125_FE_CONFIG2_CH_MASK;
      vmeWrite32(&fa125p[id]->fe[ichip].config2,chipMask);
    }
  FA125UNLOCK;

  return OK;
}

int
fa125SetChannelEnable(int id, int channel)
{
  int feChip=0, feChan=0;
  unsigned int chipMask=0;
  if(id==0) id=fa125ID[0];

  if((id<=0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("faSetChannelEnable: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return ERROR;
    }

  if((channel<0) || (channel>=FA125_MAX_ADC_CHANNELS))
    {
      logMsg("faSetChannelEnable: ERROR: Invalid channel (%d).  Must be 0-%d\n",
	     channel, FA125_MAX_ADC_CHANNELS-1,3,4,5,6);
      return ERROR;
    }


  feChip = (int)(channel/6);
  feChan = (int)(channel%6);

  FA125LOCK;
  chipMask = (vmeRead32(&fa125p[id]->fe[feChip].config2) & FA125_FE_CONFIG2_CH_MASK) 
    & ~(1<<feChan);

  vmeWrite32(&fa125p[id]->fe[feChip].config2,chipMask);
  FA125UNLOCK;
  
  return OK;
}

int
fa125SetChannelEnableMask(int id, unsigned int cmask0, 
			  unsigned int cmask1, unsigned int cmask2)
{
  int ichip=0;
  unsigned int chipMask=0;
  if(id==0) id=fa125ID[0];

  if((id<=0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("fa125SetChannelEnableMask: ERROR : FA125 in slot %d is not initialized \n",
	     id,0,0,0,0,0);
      return(ERROR);
    }

  if((cmask0 > 0xFFFFFF) || (cmask1 > 0xFFFFFF) || (cmask2 > 0xFFFFFF))
    {
      logMsg("fa125SetChannelEnableMask: ERROR : Invalid channel mask(s) (0x%08x, 0x%08x, 0x%08x).\n",
	     cmask0,cmask1,cmask2,0,0,0);
      logMsg("                          : Each mask must be less than 24 bits.\n",
	     0,0,0,0,0,0);
      return(ERROR);
    }
  
  cmask0 = (~cmask0) & 0xFFFFFF;
  cmask1 = (~cmask1) & 0xFFFFFF;
  cmask2 = (~cmask2) & 0xFFFFFF;

  FA125LOCK;
  for(ichip=0; ichip<4; ichip++)
    {
      chipMask = (cmask0>>(ichip*6)) & FA125_FE_CONFIG2_CH_MASK;
      vmeWrite32(&fa125p[id]->fe[ichip].config2,chipMask);
    }
  for(ichip=4; ichip<8; ichip++)
    {
      chipMask = (cmask1>>((ichip-4)*6)) & FA125_FE_CONFIG2_CH_MASK;
      vmeWrite32(&fa125p[id]->fe[ichip].config2,chipMask);
    }
  for(ichip=8; ichip<12; ichip++)
    {
      chipMask = (cmask2>>((ichip-8)*6)) & FA125_FE_CONFIG2_CH_MASK;
      vmeWrite32(&fa125p[id]->fe[ichip].config2,chipMask);
    }
  FA125UNLOCK;

  return OK;
}

int
fa125SetCommonThreshold(int id, unsigned short tvalue)
{
  int ii,rval=OK;

  for(ii=0;ii<FA125_MAX_ADC_CHANNELS;ii++)
    {
      rval |= fa125SetThreshold(id, tvalue, ii);
    }

  return rval;
}

void
fa125GSetCommonThreshold(unsigned short tvalue)
{
  int ii;
  
  for (ii=0;ii<nfa125;ii++) 
    {
      fa125SetCommonThreshold(fa125Slot(ii),tvalue);
    }
}

int
fa125PrintThreshold(int id)
{
  int ii;
  unsigned short tval[FA125_MAX_ADC_CHANNELS];

  if(id==0) id=fa125ID[0];

  if((id<=0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("fa215PrintThreshold: ERROR : FA125 in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }

  FA125LOCK;
  for(ii=0;ii<FA125_MAX_ADC_CHANNELS;ii++)
    {
      tval[ii] = vmeRead32(&fa125p[id]->fe[ii/6].threshold[ii%6]);
    }
  FA125UNLOCK;


  printf(" Threshold Settings for FA125 in slot %d:",id);
  for(ii=0;ii<FA125_MAX_ADC_CHANNELS;ii++) 
    {
      if((ii%4)==0) 
	{
	  printf("\n");
	}
      printf("Chan %2d: %5d   ",(ii+1),tval[ii]);
    }
  printf("\n");
  

  return(OK);
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
  temp1 = 0.0625*((int) vmeRead32(&fa125p[id]->main.temperature[0]));
  temp2 = 0.0625*((int) vmeRead32(&fa125p[id]->main.temperature[1]));
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
  vmeWrite32(&fa125p[id]->main.clock, clksrc);
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
  unsigned int regset=0;
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
      regset = FA125_TRIGSRC_TRIGGER_INTERNAL_TIMER;
      break;

    case 2: /* Internal Sum */
      regset = FA125_TRIGSRC_TRIGGER_INTERNAL_SUM;
      break;

    case 3: /* P2 */
      regset = FA125_TRIGSRC_TRIGGER_P2;
      break;

    case 0: /* P0 */
    default:
      regset = FA125_TRIGSRC_TRIGGER_P0;
      break;
    }

  FA125LOCK;
  vmeWrite32(&fa125p[id]->proc.trigsrc, regset);
  FA125UNLOCK;

  return OK;
}

int
fa125GetTriggerSource(int id)
{
  int rval=0;
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      printf("%s: ERROR : FA125 in slot %d is not initialized \n",__FUNCTION__,id);
      return ERROR;
    }

  FA125LOCK;
  rval = vmeRead32(&fa125p[id]->proc.trigsrc);
  FA125UNLOCK;
  
  return rval;
}

int
fa125SetSyncResetSource(int id, int srsrc)
{
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      printf("%s: ERROR : FA125 in slot %d is not initialized \n",__FUNCTION__,id);
      return ERROR;
    }

  if((srsrc<0) || (srsrc>1))
    {
      printf("%s: ERROR: Invalid SyncReset Source specified (%d)\n",
	     __FUNCTION__,srsrc);
      return ERROR;
    }

  switch(srsrc)
    {
    case 1: /* VME (software) */
      srsrc = FA125_TRIGSRC_TRIGGER_INTERNAL_SUM;
      break;

    case 0: /* VXS (P0) */
    default:
      srsrc = FA125_PROC_CTRL2_SYNCRESET_P0;
      break;
    }

  FA125LOCK;
  vmeWrite32(&fa125p[id]->proc.ctrl2, 
	     (vmeRead32(&fa125p[id]->proc.ctrl2) & ~FA125_PROC_CTRL2_SYNCRESET_SOURCE_MASK) | 
	     srsrc);
  vmeWrite32(&fa125p[id]->fe[0].test,
	     (vmeRead32(&fa125p[id]->fe[0].test) & ~(1<<2)) |
	     (1<<2));
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
#ifdef DOBYTESWAP
  rval = LSWAP(rval);
#endif //DOBYTESWAP
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
  vmeWrite32(&fa125p[id]->proc.csr, FA125_PROC_CSR_CLEAR);
  vmeWrite32(&fa125p[id]->proc.csr, 0);
  FA125UNLOCK;

  return OK;
}

int
fa125Enable(int id)
{
  int ife=0;
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("%s: ERROR : FA125 in slot %d is not initialized \n",(int)__FUNCTION__,id,3,4,5,6);
      return ERROR;
    }

  FA125LOCK;
  for(ife=0; ife<12; ife++)
    {
      vmeWrite32(&fa125p[id]->fe[ife].test, (1<<1));
    }
  FA125UNLOCK;

  printf("%s(%2d): ENABLED\n",__FUNCTION__,id);

  return OK;
}

int
fa125Reset(int id, int reset)
{
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("%s: ERROR : FA125 in slot %d is not initialized \n",(int)__FUNCTION__,id,3,4,5,6);
      return ERROR;
    }

  FA125LOCK;
  switch(reset)
    {
    case 0:
      vmeWrite32(&fa125p[id]->main.blockCSR, FA125_BLOCKCSR_PULSE_SOFT_RESET);
      vmeWrite32(&fa125p[id]->main.blockCSR, FA125_BLOCKCSR_PULSE_SOFT_RESET);
      vmeWrite32(&fa125p[id]->main.blockCSR, FA125_BLOCKCSR_PULSE_SOFT_RESET);
      break;

    case 1:
      vmeWrite32(&fa125p[id]->main.blockCSR, FA125_BLOCKCSR_PULSE_HARD_RESET);
      break;

    default:
      vmeWrite32(&fa125p[id]->main.blockCSR, FA125_BLOCKCSR_PULSE_SOFT_RESET);
    }
  vmeWrite32(&fa125p[id]->main.blockCSR, 0);
  FA125UNLOCK;

  return OK;
}

int
fa125ResetToken(int id)
{
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("%s: ERROR : FA125 in slot %d is not initialized \n",(int)__FUNCTION__,id,3,4,5,6);
      return ERROR;
    }

  FA125LOCK;
  vmeWrite32(&fa125p[id]->main.blockCSR, FA125_BLOCKCSR_TAKE_TOKEN);
  vmeWrite32(&fa125p[id]->main.blockCSR, 0);
  FA125UNLOCK;

  return OK;
}

int
fa125GetTokenMask()
{
  unsigned int rmask=0;
  int ifa=0, id=0, rval=0;

  for(ifa=0; ifa<nfa125; ifa++)
    {
      id=fa125Slot(ifa);
      rval = (vmeRead32(&fa125p[id]->main.blockCSR) & FA125_BLOCKCSR_HAS_TOKEN)>>4;
      rmask |= (rval<<id);
    }

  return rmask;
}

int
fa125SetBlocklevel(int id, int blocklevel)
{
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("%s: ERROR : FA125 in slot %d is not initialized \n",(int)__FUNCTION__,id,3,4,5,6);
      return ERROR;
    }

  FA125LOCK;
  vmeWrite32(&fa125p[id]->proc.blocklevel, blocklevel);
  FA125UNLOCK;

  return OK;
}

int
fa125SoftTrigger(int id)
{
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("%s: ERROR : FA125 in slot %d is not initialized \n",(int)__FUNCTION__,id,3,4,5,6);
      return ERROR;
    }

  FA125LOCK;
  vmeWrite32(&fa125p[id]->proc.softtrig, 1);
  vmeWrite32(&fa125p[id]->proc.softtrig, 0);
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
	      rdata=vmeRead32(&fa125p[id]->fe[ichan/6].acqfifo[ichan%6]);
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
      logMsg("fa125Bready: ERROR : FA125 in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }

  FA125LOCK;
  rval = (vmeRead32(&fa125p[id]->main.blockCSR) & FA125_BLOCKCSR_BLOCK_READY)>>2;
  FA125UNLOCK;

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
      
      stat = (vmeRead32(&fa125p[id]->main.blockCSR) & FA125_BLOCKCSR_BLOCK_READY)>>2;
/*       printf("%s(%2d): main.blockCSR = 0x%08x\n", */
/* 	     __FUNCTION__,id, fa125p[id]->main.blockCSR); */
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

const char *fa125_blockerror_names[FA125_BLOCKERROR_NTYPES] =
  {
    "No Error",
    "Termination on word count",
    "Unknown Bus Error",
    "Zero Word Count",
    "DmaDone(..) Error"
  };

int
fa125ReadBlockStatus(int pflag)
{
  if(pflag)
    {
      if(fa125BlockError!=FA125_BLOCKERROR_NO_ERROR)
	{
	  printf("%s: ERROR: %s",
		 __FUNCTION__,fa125_blockerror_names[fa125BlockError]);
	}
    }

  return fa125BlockError;
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

  fa125BlockError=FA125_BLOCKERROR_NO_ERROR;
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
	  *data = FA125_DUMMY_DATA;
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
	  if((vmeRead32(&fa125p[id]->main.ctrl1)&FA125_CTRL1_FIRST_BOARD)==0) 
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
      if(retVal != 0) 
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
	      csr = vmeRead32(&fa125p[fa125MaxSlot]->main.blockCSR);  /* from Last FA125 */
	      stat = (csr)&FA125_BLOCKCSR_BERR_ASSERTED;  /* from Last FA125 */
	    }
	  else
	    {
	      csr = vmeRead32(&fa125p[id]->main.blockCSR);  /* from Last FA125 */
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
	      fa125BlockError=FA125_BLOCKERROR_UNKNOWN_BUS_ERROR;
	      return(xferCount);
	    }
	} 
      else if (retVal == 0)
	{ /* Block Error finished without Bus Error */
#ifdef VXWORKS
	  logMsg("fa125ReadBlock: WARN: DMA transfer terminated by word count 0x%x\n",nwrds,0,0,0,0,0);
	  fa125BlockError=FA125_BLOCKERROR_TERM_ON_WORDCOUNT;
#else
	  logMsg("fa125ReadBlock: WARN: DMA transfer returned zero word count 0x%x\n",nwrds,0,0,0,0,0);
	  fa125BlockError=FA125_BLOCKERROR_ZERO_WORD_COUNT;
#endif
	  FA125UNLOCK;
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
	  fa125BlockError=FA125_BLOCKERROR_DMADONE_ERROR;
	  return(retVal>>2);
	}

    } 
  else 
    {  /*Programmed IO */

      /* Check if Bus Errors are enabled. If so then disable for Prog I/O reading */
      FA125LOCK;
      berr = vmeRead32(&fa125p[id]->main.ctrl1)&FA125_CTRL1_ENABLE_BERR;
      if(berr)
	vmeWrite32(&fa125p[id]->main.ctrl1, 
		   vmeRead32(&fa125p[id]->main.ctrl1) & ~FA125_CTRL1_ENABLE_BERR);

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
	  if( (vmeRead32(&fa125p[id]->proc.ev_count) & FA125_PROC_EVCOUNT_MASK) == 0) 
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
	vmeWrite32(&fa125p[id]->main.ctrl1, 
		   vmeRead32(&fa125p[id]->main.ctrl1) | FA125_CTRL1_ENABLE_BERR);

      FA125UNLOCK;
      return(dCnt);
    }

  FA125UNLOCK;
  return(OK);
}

struct data_struct 
{
  unsigned int new_type;
  unsigned int type;
  unsigned int slot_id_hd;
  unsigned int mod_id_hd;
  unsigned int slot_id_tr;
  unsigned int n_evts;
  unsigned int blk_num;
  unsigned int n_words;
  unsigned int evt_num_1;
  unsigned int time_now;
  unsigned int time_1;
  unsigned int time_2;
  unsigned int chan;
  unsigned int width;
  unsigned int valid_1;
  unsigned int adc_1;
  unsigned int valid_2;
  unsigned int adc_2;
  unsigned int over;
  unsigned int adc_sum;
  unsigned int pulse_num;
  unsigned int thres_bin;
  unsigned int quality;
  unsigned int integral;
  unsigned int time;
  unsigned int chan_a;
  unsigned int source_a;
  unsigned int chan_b;
  unsigned int source_b;
  unsigned int group;
  unsigned int time_coarse;  
  unsigned int time_fine;
  unsigned int vmin;
  unsigned int vpeak;
  unsigned int scaler[18];/* data stream scalers */
};

volatile struct data_struct fadc_data;

void 
fa125DecodeData(unsigned int data)
{
  /* for new data format - 10/23/13 - EJ */

  static unsigned int type_last = 15;/* initialize to type FILLER WORD */
  static unsigned int time_last = 0;
  static unsigned int scaler_index = 0;
  static unsigned int num_scalers = 1;
  
  static unsigned int slot_id_ev_hd = 0;
  static unsigned int slot_id_dnv = 0;
  static unsigned int slot_id_fill = 0;

  static int nsamples=0;

  int i_print =1;
  
  if( scaler_index )/* scaler data word */
    {
      fadc_data.type = 16;/* scaler data words as type 16 */
      fadc_data.new_type = 0;
      if( scaler_index < num_scalers )
	{ 
	  fadc_data.scaler[scaler_index - 1] = data;
	  if( i_print ) 
	    printf("%8X - SCALER(%d) = %d\n", data, (scaler_index - 1), data);
	  scaler_index++;
	}
      else/* last scaler word */
	{ 
	  fadc_data.scaler[scaler_index - 1] = data;
	  if( i_print ) 
	    printf("%8X - SCALER(%d) = %d\n", data, (scaler_index - 1), data);
	  scaler_index = 0;
	  num_scalers = 1;
	} 
    }
  else/* non-scaler word */
    {
      if( data & 0x80000000 )/* data type defining word */
	{
	  fadc_data.new_type = 1;
	  fadc_data.type = (data & 0x78000000) >> 27;
	}
      else/* data type continuation word */
	{
	  fadc_data.new_type = 0;
	  fadc_data.type = type_last;
	}
        
      switch( fadc_data.type )
	{
	case 0:/* BLOCK HEADER */
	  fadc_data.slot_id_hd = (data & 0x7C00000) >> 22;
	  fadc_data.mod_id_hd =  (data &  0x3C0000) >> 18;
	  fadc_data.n_evts =  (data & 0x000FF);
	  fadc_data.blk_num = (data & 0x3FF00) >> 8;
	  if( i_print ) 
	    printf("%8X - BLOCK HEADER - slot = %d  id = %d  n_evts = %d  n_blk = %d\n",
		   data, fadc_data.slot_id_hd, fadc_data.mod_id_hd, fadc_data.n_evts, fadc_data.blk_num);
	  break;

	case 1:/* BLOCK TRAILER */
	  fadc_data.slot_id_tr = (data & 0x7C00000) >> 22;
	  fadc_data.n_words = (data & 0x3FFFFF);
	  if( i_print ) 
	    printf("%8X - BLOCK TRAILER - slot = %d   n_words = %d\n",
		   data, fadc_data.slot_id_tr, fadc_data.n_words);
	  break;

	case 2:/* EVENT HEADER */
	  if( fadc_data.new_type )
	    {
	      slot_id_ev_hd        = (data & 0x7C00000) >> 22;
	      fadc_data.evt_num_1 =  (data & 0x03FFFFF);
	      if( i_print ) 
		printf("%8X - EVENT HEADER - slot = %d  evt_num = %d\n", 
		       data, slot_id_ev_hd, fadc_data.evt_num_1);
	    }    
	  break;

	case 3:/* TRIGGER TIME */
	  if( fadc_data.new_type )
	    {
	      fadc_data.time_1 = (data & 0xFFFFFF);
	      if( i_print ) 
		printf("%8X - TRIGGER TIME 1 - time = 0x%08x\n", data, fadc_data.time_1);
	      fadc_data.time_now = 1;
	      time_last = 1;
	    }    
	  else
	    {
	      if( time_last == 1 )
		{
		  fadc_data.time_2 = (data & 0xFFFFFF);
		  if( i_print ) 
		    printf("%8X - TRIGGER TIME 2 - time = 0x%08x\n", data, fadc_data.time_2);
		  fadc_data.time_now = 2;
		}    
	      else
		if( i_print ) 
		  printf("%8X - TRIGGER TIME - (ERROR)\n", data);
	                      
	      time_last = fadc_data.time_now;
	    }    
	  break;

	case 4:/* WINDOW RAW DATA */
	  if( fadc_data.new_type )
	    {
	      fadc_data.chan = (data & 0x7F00000) >> 20;
	      fadc_data.width = (data & 0xFFF);
	      if( i_print ) 
		printf("%8X - WINDOW RAW DATA - chan = %d   width = %d\n", 
		       data, fadc_data.chan, fadc_data.width);
	      nsamples=0;
	    }    
	  else
	    {
	      fadc_data.valid_1 = 1;
	      fadc_data.valid_2 = 1;
	      fadc_data.adc_1 = (data & 0x1FFF0000) >> 16;
	      if( data & 0x20000000 )
		fadc_data.valid_1 = 0;
	      fadc_data.adc_2 = (data & 0x1FFF);
	      if( data & 0x2000 )
		fadc_data.valid_2 = 0;
	      if( i_print ) 
		printf("%8X - RAW SAMPLES (%3d) - valid = %d  adc = %d (%X)  valid = %d  adc = %d (%X)\n", 
		       data, nsamples,fadc_data.valid_1, fadc_data.adc_1, fadc_data.adc_1, 
		       fadc_data.valid_2, fadc_data.adc_2, fadc_data.adc_2);
	      nsamples += 2;
	    }    
	  break;

	case 5:/* WINDOW SUM */
	  fadc_data.over = 0; 
	  fadc_data.chan = (data & 0x7800000) >> 23;
	  fadc_data.adc_sum = (data & 0x3FFFFF);
	  if( data & 0x400000 )
	    fadc_data.over = 1;
	  if( i_print ) 
	    printf("%8X - WINDOW SUM - chan = %d   over = %d   adc_sum = 0x%08x\n",
		   data, fadc_data.chan, fadc_data.over, fadc_data.adc_sum);
	  break;

	case 6:/* PULSE RAW DATA */
	  if( fadc_data.new_type )
	    {
	      fadc_data.chan = (data & 0x7800000) >> 23;
	      fadc_data.pulse_num = (data & 0x600000) >> 21;
	      fadc_data.thres_bin = (data & 0x3FF);
	      if( i_print ) 
		printf("%8X - PULSE RAW DATA - chan = %d   pulse # = %d   threshold bin = %d\n", 
		       data, fadc_data.chan, fadc_data.pulse_num, fadc_data.thres_bin);
	    }    
	  else
	    {
	      fadc_data.valid_1 = 1;
	      fadc_data.valid_2 = 1;
	      fadc_data.adc_1 = (data & 0x1FFF0000) >> 16;
	      if( data & 0x20000000 )
		fadc_data.valid_1 = 0;
	      fadc_data.adc_2 = (data & 0x1FFF);
	      if( data & 0x2000 )
		fadc_data.valid_2 = 0;
	      if( i_print ) 
		printf("%8X - PULSE RAW SAMPLES - valid = %d  adc = %d   valid = %d  adc = %d\n", 
		       data, fadc_data.valid_1, fadc_data.adc_1, 
		       fadc_data.valid_2, fadc_data.adc_2);
	    }    
	  break;

	case 7:/* PULSE INTEGRAL */
	  fadc_data.chan = (data & 0x7800000) >> 23;
	  fadc_data.pulse_num = (data & 0x600000) >> 21;
	  fadc_data.quality = (data & 0x180000) >> 19;
	  fadc_data.integral = (data & 0x7FFFF);
	  if( i_print ) 
	    printf("%8X - PULSE INTEGRAL - chan = %d   pulse # = %d   quality = %d   integral = %d\n", 
		   data, fadc_data.chan, fadc_data.pulse_num, 
		   fadc_data.quality, fadc_data.integral);
	  break;

	  /* !!! */       case 8:/* PULSE TIME */
			    fadc_data.chan = (data & 0x7800000) >> 23;
			    fadc_data.pulse_num = (data & 0x600000) >> 21;
			    fadc_data.quality = (data & 0x180000) >> 19;
			    fadc_data.time = (data & 0xFFFF);
			    fadc_data.time_coarse = (data & 0xFFC0) >> 6;
			    fadc_data.time_fine = (data & 0x3F);
			    if( i_print ) 
			      printf("%8X - PULSE TIME - chan = %d  pulse # = %d  qual = %d  t = %d (c = %d  f = %d)\n", 
				     data, fadc_data.chan, fadc_data.pulse_num, fadc_data.quality, 
				     fadc_data.time, fadc_data.time_coarse, fadc_data.time_fine);
			    break;

	case 9:/* STREAMING RAW DATA */
	  if( fadc_data.new_type )
	    {
	      fadc_data.chan_a = (data & 0x3C00000) >> 22;
	      fadc_data.source_a = (data & 0x4000000) >> 26;
	      fadc_data.chan_b = (data & 0x1E0000) >> 17;
	      fadc_data.source_b = (data & 0x200000) >> 21;
	      if( i_print ) 
		printf("%8X - STREAMING RAW DATA - ena A = %d  chan A = %d   ena B = %d  chan B = %d\n", 
		       data, fadc_data.source_a, fadc_data.chan_a, 
		       fadc_data.source_b, fadc_data.chan_b);
	    }    
	  else
	    {
	      fadc_data.valid_1 = 1;
	      fadc_data.valid_2 = 1;
	      fadc_data.adc_1 = (data & 0x1FFF0000) >> 16;
	      if( data & 0x20000000 )
		fadc_data.valid_1 = 0;
	      fadc_data.adc_2 = (data & 0x1FFF);
	      if( data & 0x2000 )
		fadc_data.valid_2 = 0;
	      fadc_data.group = (data & 0x40000000) >> 30;
	      if( fadc_data.group )
		{
		  if( i_print ) 
		    printf("%8X - RAW SAMPLES B - valid = %d  adc = %d   valid = %d  adc = %d\n", 
			   data, fadc_data.valid_1, fadc_data.adc_1, 
			   fadc_data.valid_2, fadc_data.adc_2);
		} 
	      else
		if( i_print ) 
		  printf("%8X - RAW SAMPLES A - valid = %d  adc = %d   valid = %d  adc = %d\n", 
			 data, fadc_data.valid_1, fadc_data.adc_1, 
			 fadc_data.valid_2, fadc_data.adc_2);            
	    }    
	  break;

	  /* !!! */       case 10:/* PULSE PARAMETERS */
			    fadc_data.chan = (data & 0x7800000) >> 23;
			    fadc_data.pulse_num = (data & 0x600000) >> 21;
			    fadc_data.vmin = (data & 0x1FF000) >> 12;
			    fadc_data.vpeak = (data & 0xFFF);
			    if( i_print ) 
			      printf("%8X - PULSE V - chan = %d   pulse # = %d   vmin = %d   vpeak = %d\n", 
				     data, fadc_data.chan, fadc_data.pulse_num, 
				     fadc_data.vmin, fadc_data.vpeak);
			    break;

	case 11:/* UNDEFINED TYPE */
	  if( i_print ) 
	    printf("%8X - UNDEFINED TYPE = %d\n", data, fadc_data.type);
	  break;
	              
	case 12:/* SCALER HEADER */
	  num_scalers = data & 0x3F;/* number of scaler words to follow */
	  scaler_index = 1;/* identify next word as a scaler value */
	  if( i_print ) 
	    printf("%8X - SCALER HEADER = %d  (NUM SCALERS = %d)\n", 
		   data, fadc_data.type, num_scalers);
	  break;
	              
	case 13:/* END OF EVENT */
	  if( i_print ) 
	    printf("%8X - END OF EVENT = %d\n", data, fadc_data.type);
	  break;

	case 14:/* DATA NOT VALID (no data available) */
	  slot_id_dnv = (data & 0x7C00000) >> 22; 
	  if( i_print )
	    printf("%8X - DATA NOT VALID = %d  slot = %d\n", data, fadc_data.type, slot_id_dnv);
	  break;

	case 15:/* FILLER WORD */
	  slot_id_fill = (data & 0x7C00000) >> 22; 
	  if( i_print ) 
	    printf("%8X - FILLER WORD = %d  slot = %d\n", data, fadc_data.type, slot_id_fill);
	  break;
	}
      
      type_last = fadc_data.type;    /* save type of current data word */
      
    }

}

/************************************************************
 *  fa125 Firmware Updating Routines 
 ************************************************************/
#define        MCS_MAX_SIZE    (FA125_FIRMWARE_MAX_PAGES*FA125_FIRMWARE_MAX_BYTE_PER_PAGE)
static unsigned int   MCS_dataSize = 0;          /* Size of the array holding the firmware */
static unsigned int   MCS_pageSize = 0;          /* Number of pages read from MCS file */
static unsigned char  MCS_DATA[FA125_FIRMWARE_MAX_PAGES][FA125_FIRMWARE_MAX_BYTE_PER_PAGE];    /* The array holding the firmware */
static int            MCS_loaded = 0;             /* 1(0) if firmware loaded (not loaded) */
static unsigned char  tmp_pageData[FA125_FIRMWARE_MAX_BYTE_PER_PAGE];
static int            fa125FirmwareDebug=0;
static int            fa125FirmwareErrorFlags[(FA125_MAX_BOARDS+1)]; /* Firmware Updating Error Flags for each slot */
enum   ifpgatype      {MAIN, FE, PROC, NFPGATYPE};
struct fpga_fw_info 
{
  char name[5];
  unsigned int size;
  unsigned int location;
  unsigned int page_location;
  unsigned int page_byte_location;
};

struct firmware_stats
{
  struct timespec erase_time;
  unsigned int nblocks_erased;
  struct timespec buffer_write_time;
  unsigned int nbuffers_written;
  struct timespec buffer_push_time;
  unsigned int nbuffers_pushed;
  struct timespec main_page_read_time;
  unsigned int npages_read;
};

static struct timespec  
tsSubtract(struct  timespec  time1, struct  timespec  time2)
{    /* Local variables. */
  struct  timespec  result;

  /* Subtract the second time from the first. */
  if ((time1.tv_sec < time2.tv_sec) ||
      ((time1.tv_sec == time2.tv_sec) &&
       (time1.tv_nsec <= time2.tv_nsec))) 
    {		/* TIME1 <= TIME2? */
      result.tv_sec = result.tv_nsec = 0;
    } 
  else 
    {						/* TIME1 > TIME2 */
      result.tv_sec = time1.tv_sec - time2.tv_sec;
      if (time1.tv_nsec < time2.tv_nsec) 
	{
	  result.tv_nsec = time1.tv_nsec + 1000000000L - time2.tv_nsec;
	  result.tv_sec--;				/* Borrow a second. */
	} 
      else 
	{
	  result.tv_nsec = time1.tv_nsec - time2.tv_nsec;
	}
    }
  
  return (result);
}

static struct timespec  
tsAdd(struct  timespec  time1, struct  timespec  time2)
{    
  /* Local variables. */
  struct  timespec  result ;
  
  /* Add the two times together. */
  result.tv_sec = time1.tv_sec + time2.tv_sec ;
  result.tv_nsec = time1.tv_nsec + time2.tv_nsec ;
  if (result.tv_nsec >= 1000000000L) 
    {		/* Carry? */
      result.tv_sec++ ;  result.tv_nsec = result.tv_nsec - 1000000000L ;
    }
    
  return (result) ;
}

struct firmware_stats fa125FWstats;

struct fpga_fw_info sfpga[NFPGATYPE] =
  {
    {"MAIN", 0x45480,  0x0,      0, 0},
    {"FE"  , 0xC435A,  0x754E0,  0, 0},
    {"PROC", 0x1659FA, 0x1E1B90, 0, 0}
  };

/* Static Firmware Updating routine prototypes */
static int fa125FirmwareWaitForReady(int id, int nwait, int *rwait);
static int fa125FirmwareBlockErase(int id, int iblock, int stayon, int waitForDone);
static int fa125FirmwareWriteToBuffer(int id, int ipage);
static int fa125FirmwarePushBufferToMain(int id, int ipage, int waitForDone);
static int fa125FirmwareWaitForPushBufferToMain(int id, int ipage);
static int fa125FirmwareReadMainPage(int id, int ipage, int stayon);
static int fa125FirmwareReadBuffer(int id);
static int fa125FirmwareVerifyFull(int id);
static int fa125FirmwareVerifyPage(int ipage);
static int fa125FirmwareVerifyErasedPage(int ipage);
static int hex2num(char c);

void
fa125FirmwareSetDebug(unsigned int debug)
{
  fa125FirmwareDebug=debug;
}

static int
fa125FirmwareWaitForReady(int id, int nwait, int *rwait)
{
  int iwait=0, rval=0;
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("%s: ERROR : FA125 in slot %d is not initialized \n",(int)__FUNCTION__,id,3,4,5,6);
      return ERROR;
    }

  for(iwait=0; iwait<nwait; iwait++)
    {
      rval = vmeRead32(&fa125p[id]->main.configCSR) & FA125_CONFIGCSR_BUSY;
      if(rval==FA125_CONFIGCSR_BUSY)
	break;
    }

  *rwait = iwait;

  if(iwait==nwait)
    {
      printf("%s: Operation still in progress after %d tries.\n",
	     __FUNCTION__,iwait);
      return ERROR;
    }

  return OK;
}

static int
fa125FirmwareBlockErase(int id, int iblock, int stayon, int waitForDone)
{
#ifdef DOSTAYON
  int rwait=0;
#endif

  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("%s: ERROR : FA125 in slot %d is not initialized \n",(int)__FUNCTION__,id,3,4,5,6);
      return ERROR;
    }

  FA125LOCK;

  /* Configuration csr for block erase */
  vmeWrite32(&fa125p[id]->main.configCSR, 
	     FA125_CONFIGCSR_PROG_ENABLE | (FA125_OPCODE_ERASE<<24));

  /* Erase blocks using top 10 bits of page address [30... 21] 0-1023 */
  vmeWrite32(&fa125p[id]->main.configAdrData,
	     (iblock<<21));
  vmeWrite32(&fa125p[id]->main.configAdrData,
	     FA125_CONFIGADRDATA_EXEC | (iblock<<21));
  vmeWrite32(&fa125p[id]->main.configAdrData,
	     (iblock<<21));

  if(waitForDone==0)
    {
      FA125UNLOCK;
      return OK;
    }

  taskDelay(6);

#ifdef DOSTAYON
  /* Pull Execute low before asserting new configuration type */
  if(stayon==0)
    {
      taskDelay(1);
      vmeWrite32(&fa125p[id]->main.configAdrData, 0);
      
      if(fa125FirmwareWaitForReady(id,100,&rwait)!=OK)
	{
	  printf("%s: ERROR: Pull down execute timeout (rwait = %d).\n",
		 __FUNCTION__,
		 rwait);
	  FA125UNLOCK;
	  return ERROR;
	}
      if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_WAIT_FOR_READY)
	{
	  printf("%s: Pull Execute low. wait ticks = %d\n",
		 __FUNCTION__,rwait);
	}
    }
#endif

  FA125UNLOCK;
  return OK;
}

static int
fa125FirmwareWriteToBuffer(int id, int ipage)
{
  int ibadr=0;
  unsigned char data=0;
  int rwait=0;
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("%s: ERROR : FA125 in slot %d is not initialized \n",(int)__FUNCTION__,id,3,4,5,6);
      return ERROR;
    }

  if(ipage>(FA125_FIRMWARE_MAX_PAGES-1))
    {
      printf("%s: ERROR: ipage > Maximum page count (%d > %d)\n",
	     __FUNCTION__, ipage, (FA125_FIRMWARE_MAX_PAGES-1));
    }

  if(MCS_loaded==0)
    {
      printf("%s: ERROR: MCS file not loaded into memory\n",
	     __FUNCTION__);
      return ERROR;
    }

  FA125LOCK;
  /* Configuration csr for buffer write */
  vmeWrite32(&fa125p[id]->main.configCSR, 
	     FA125_CONFIGCSR_PROG_ENABLE | (FA125_OPCODE_BUFFER_WRITE<<24));

  /* Write configuration data byte using byte addresses 0-527 */
  for(ibadr=0; ibadr<528; ibadr++)
    {
      data = MCS_DATA[ipage][ibadr];

      vmeWrite32(&fa125p[id]->main.configAdrData,
		 (ibadr<<8) | data);
      vmeWrite32(&fa125p[id]->main.configAdrData,
		 FA125_CONFIGADRDATA_EXEC | (ibadr<<8) | data);
      vmeWrite32(&fa125p[id]->main.configAdrData,
		 (ibadr<<8) | data);

      if(fa125FirmwareWaitForReady(id,1000000,&rwait)!=OK)
	{
	  printf("%s: ERROR: Buffer Write timeout (byte address = %d, page = %d) (rwait = %d).\n",
		 __FUNCTION__,
		 ibadr,ipage,
		 rwait);
	  vmeWrite32(&fa125p[id]->main.configAdrData, 0);
	  FA125UNLOCK;
	  return ERROR;
	}
    }

  FA125UNLOCK;
  return OK;
}

static int
fa125FirmwarePushBufferToMain(int id, int ipage, int waitForDone)
{
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("%s: ERROR : FA125 in slot %d is not initialized \n",(int)__FUNCTION__,id,3,4,5,6);
      return ERROR;
    }

  if(ipage>(FA125_FIRMWARE_MAX_PAGES-1))
    {
      printf("%s: ERROR: ipage > Maximum page count (%d > %d)\n",
	     __FUNCTION__, ipage, (FA125_FIRMWARE_MAX_PAGES-1));
      return ERROR;
    }

  FA125LOCK;
  /* Configuration csr for buffer to main memory */
  vmeWrite32(&fa125p[id]->main.configCSR, 
	     FA125_CONFIGCSR_PROG_ENABLE | (FA125_OPCODE_BUFFER_PUSH<<24));

  /* Push buffer contents using page address */
  vmeWrite32(&fa125p[id]->main.configAdrData,
	     (ipage<<18));
  vmeWrite32(&fa125p[id]->main.configAdrData,
	     FA125_CONFIGADRDATA_EXEC | (ipage<<18));
  vmeWrite32(&fa125p[id]->main.configAdrData,
	     (ipage<<18));
  FA125UNLOCK;

  if(waitForDone==0)
    {
      return OK;
    }

  if(fa125FirmwareWaitForPushBufferToMain(id, ipage)!=OK)
    return ERROR;

  return OK;
}

static int
fa125FirmwareWaitForPushBufferToMain(int id, int ipage)
{
  int rwait=0;
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("%s: ERROR : FA125 in slot %d is not initialized \n",(int)__FUNCTION__,id,3,4,5,6);
      return ERROR;
    }

  FA125LOCK;
  if(fa125FirmwareWaitForReady(id,100000,&rwait)!=OK)
    {
      printf("%s: ERROR: Push to main memory timeout (page = %d) (rwait = %d).\n",
	     __FUNCTION__,
	     ipage,rwait);
      vmeWrite32(&fa125p[id]->main.configAdrData, 0);
      FA125UNLOCK;
      return ERROR;
    }
  if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_WAIT_FOR_READY)
    {
      printf("%s: Push buffer contents using page address.  rwait = %d\n",
	     __FUNCTION__,rwait);
    }
  
  /* Pull Execute low before asserting new configuration type */
  vmeWrite32(&fa125p[id]->main.configAdrData, 0);
  if(fa125FirmwareWaitForReady(id,100,&rwait)!=OK)
    {
      printf("%s: ERROR: Pull down execute timeout (rwait = %d).\n",
	     __FUNCTION__,
	     rwait);
      FA125UNLOCK;
      return ERROR;
    }

  FA125UNLOCK;
  return OK;
}

static int
fa125FirmwareReadMainPage(int id, int ipage, int stayon)
{
  int ibadr=0;
  int rwait=0;
  unsigned int csraddr = 0;
  unsigned int data=0;

  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("%s: ERROR : FA125 in slot %d is not initialized \n",(int)__FUNCTION__,id,3,4,5,6);
      return ERROR;
    }

  if(ipage>(FA125_FIRMWARE_MAX_PAGES-1))
    {
      printf("%s: ERROR: ipage > Maximum page count (%d > %d)\n",
	     __FUNCTION__, ipage, FA125_FIRMWARE_MAX_PAGES-1);
    }

  memset((char *)tmp_pageData, 0, sizeof(tmp_pageData));
/*   taskDelay(1); */

  FA125LOCK;
  /* Configuration csr for main memory read */
  vmeWrite32(&fa125p[id]->main.configCSR, 
	     FA125_CONFIGCSR_PROG_ENABLE | (FA125_OPCODE_MAIN_READ<<24));

  for(ibadr=0; ibadr<FA125_FIRMWARE_MAX_BYTE_PER_PAGE; ibadr++)
    {
      csraddr = (ipage<<18) | (ibadr<<8);
      vmeWrite32(&fa125p[id]->main.configAdrData, csraddr);
      vmeWrite32(&fa125p[id]->main.configAdrData, FA125_CONFIGADRDATA_EXEC | csraddr);
      vmeWrite32(&fa125p[id]->main.configAdrData, csraddr);
      if(fa125FirmwareWaitForReady(id,10000,&rwait)!=OK)
	{
	  printf("%s: ERROR: Main memory read timeout (byte address = %d, page = %d) (rwait = %d).\n",
		 __FUNCTION__,
		 ibadr,ipage,rwait);
	  FA125UNLOCK;
	  return ERROR;
	}
      
      data = vmeRead32(&fa125p[id]->main.configCSR);
      tmp_pageData[ibadr] = data & FA125_CONFIGCSR_DATAREAD_MASK;

    }

  /* Pull Execute low before asserting new configuration type */
#ifdef DOSTAYON
  if(stayon==0)
    {
      vmeWrite32(&fa125p[id]->main.configAdrData, 0);
      if(fa125FirmwareWaitForReady(id,100,&rwait)!=OK)
	{
	  printf("%s: ERROR: Pull down execute timeout (rwait = %d).\n",
		 __FUNCTION__,
		 rwait);
	  FA125UNLOCK;
	  return ERROR;
	}
      taskDelay(1);
    }
#endif

  FA125UNLOCK;
  return OK;
}

static int
fa125FirmwareReadBuffer(int id)
{
  int ibadr=0;
  int rwait=0;
  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("%s: ERROR : FA125 in slot %d is not initialized \n",(int)__FUNCTION__,id,3,4,5,6);
      return ERROR;
    }

  memset((char *)tmp_pageData, 0, sizeof(tmp_pageData));

  FA125LOCK;
  /* Configuration csr for buffer memory read */
  vmeWrite32(&fa125p[id]->main.configCSR, 
	     FA125_CONFIGCSR_PROG_ENABLE | (FA125_OPCODE_BUFFER_READ<<24));

/*   taskDelay(1); */

  /* Read main memory using full address (page and byte) */
  for(ibadr=0; ibadr<FA125_FIRMWARE_MAX_BYTE_PER_PAGE; ibadr++)
    {
      vmeWrite32(&fa125p[id]->main.configAdrData,
		 (ibadr<<8));
      vmeWrite32(&fa125p[id]->main.configAdrData,
		 FA125_CONFIGADRDATA_EXEC | (ibadr<<8));
      vmeWrite32(&fa125p[id]->main.configAdrData,
		 (ibadr<<8));
      if(fa125FirmwareWaitForReady(id,100,&rwait)!=OK)
	{
	  printf("%s: ERROR: Main memory read timeout (byte address = %d) (rwait = %d)\n.",
		 __FUNCTION__,
		 ibadr,rwait);
	  FA125UNLOCK;
	  return ERROR;
	}

      tmp_pageData[ibadr] = vmeRead32(&fa125p[id]->main.configCSR) & FA125_CONFIGCSR_DATAREAD_MASK;
    }

  /* Pull Execute low before asserting new configuration type */
  vmeWrite32(&fa125p[id]->main.configAdrData, 0);

  FA125UNLOCK;
  return OK;
}

static int
fa125FirmwareVerifyFull(int id)
{
  int ipage=0;
  int stayon=1;
  struct timespec time_start, time_end, res;

  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("%s: ERROR : FA125 in slot %d is not initialized \n",(int)__FUNCTION__,id,3,4,5,6);
      return ERROR;
    }

  if(MCS_loaded==0)
    {
      printf("%s: ERROR: MCS file not loaded into memory\n",
	     __FUNCTION__);
      return ERROR;
    }


#ifndef VXWORKSPPC  
  if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_MEASURE_TIMES)
    {
      fa125FWstats.npages_read                 = 0;
      fa125FWstats.main_page_read_time.tv_sec  = 0;
      fa125FWstats.main_page_read_time.tv_nsec = 0;
    }
#endif
  
  FA125LOCK;
  vmeWrite32(&fa125p[id]->main.configCSR, 0);
  FA125UNLOCK;

  printf("%3d: ",id);
  fflush(stdout);

  for(ipage=0; ipage<=MCS_pageSize; ipage++)
    {
      if((ipage%(8*0x10))==0)
	{
	  printf(".");
	  fflush(stdout);
	}

#ifndef VXWORKSPPC      
      if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_MEASURE_TIMES)
	{
	  clock_gettime(CLOCK_MONOTONIC, &time_start);
	}
#endif
      if(ipage==(MCS_pageSize-1)) stayon=0;

      /* Read a page from main memory */
      if(fa125FirmwareReadMainPage(id, ipage, stayon)!=OK)
	{
	  vmeWrite32(&fa125p[id]->main.configAdrData, 0);
	  printf("%s: Error reading from main memory (page = %d)\n",
		 __FUNCTION__,ipage);
	  return ERROR;
	}

#ifndef VXWORKSPPC  
      if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_MEASURE_TIMES)
	{
	  fa125FWstats.npages_read++;
	  clock_gettime(CLOCK_MONOTONIC, &time_end);
	  res = tsSubtract(time_end, time_start);
	  fa125FWstats.main_page_read_time = tsAdd(fa125FWstats.main_page_read_time, res);
	}
#endif

      /* Verify the page with that read from the file */
      if(fa125FirmwareVerifyPage(ipage)!=OK)
	{
	  printf("%s: ERROR in verifying page %d\n",
		 __FUNCTION__,ipage);
	  return ERROR;
	}

    }
  printf("\n");


  return OK;

}

int
fa125FirmwareGVerifyFull()
{
  int ifa=0, id=0;

  if(MCS_loaded==0)
    {
      printf("%s: ERROR: MCS file not loaded into memory\n",
	     __FUNCTION__);
      return ERROR;
    }

  printf("** Verifying Main Memory **\n");
  for(ifa=0; ifa<nfa125; ifa++)
    {
      id = fa125Slot(ifa);

      if(fa125FirmwareErrorFlags[id]!=0) 
	continue;

      if(fa125FirmwareVerifyFull(id)!=OK)
	{
	  printf("\n%s: Slot %d: Error in verifying full firmware\n",
		 __FUNCTION__,id);
	  fa125FirmwareErrorFlags[id] |= FA125_FIRMWARE_ERROR_VERIFY_WRITE;
/* 	  return ERROR; */
	}
    }

  return OK;
}

static int
fa125FirmwareVerifyPage(int ipage)
{
  int ibyte=0;
  int nerror=0;
  if(MCS_loaded==0)
    {
      printf("%s: ERROR: MCS file not loaded into memory\n",
	     __FUNCTION__);
      return ERROR;
    }

  for(ibyte=0; ibyte<FA125_FIRMWARE_MAX_BYTE_PER_PAGE; ibyte++)
    {
      if(tmp_pageData[ibyte] != MCS_DATA[ipage][ibyte])
	{
	  nerror++;
	  if(nerror<20)
	    {
	      printf("%s: %4d: Buffer (0x%02x) != MCS file (0x%02x)\n",
		     __FUNCTION__,ibyte,tmp_pageData[ibyte],MCS_DATA[ipage][ibyte]);
	    }
	}
    }

  if(nerror>0)
    {
      printf("%s: ERROR: Total number of errors = %d\n",
	     __FUNCTION__,nerror);
      return ERROR;
    }

  return OK;
}

static int
fa125FirmwareVerifyErasedPage(int ipage)
{
  int ibyte=0;
  int nerror=0;

  for(ibyte=0; ibyte<FA125_FIRMWARE_MAX_BYTE_PER_PAGE; ibyte++)
    {
      if(tmp_pageData[ibyte] != 0xff)
	{
	  nerror++;
	  if(nerror<20)
	    {
	      printf("%s: %4d: Buffer (0x%02x) != Erased (0x%02x)\n",
		     __FUNCTION__,ibyte,tmp_pageData[ibyte],0xff);
	    }
	}
    }

  if(nerror>0)
    {
      printf("%s: ERROR: Total number of errors = %d\n",
	     __FUNCTION__,nerror);
      return ERROR;
    }

  return OK;
}

static int
hex2num(char c)
{
  c = toupper(c);

  if(c > 'F')
    return 0;

  if(c >= 'A')
    return 10 + c - 'A';

  if((c > '9') || (c < '0') )
    return 0;

  return c - '0';
}

int
fa125FirmwareReadMcsFile(char *filename)
{
  FILE *mcsFile=NULL;
  char ihexLine[200], *pData;
  int len=0, datalen=0;
  unsigned int nbytes=0, line=0, hiChar=0, loChar=0;
  int ibyte=0, ipage=0;
  unsigned int readMCS=0;
  int ichar;
  int ifpga=MAIN, fpga_bytes=0, getFirmwareLocation=0;
  unsigned int mcs_addr=0xFFF0, addr0=0, addr1=0, addr2=0, addr3=0;
  unsigned int prev_elar_data=-1, elar_data=0, data0=0, data1=0, data2=0, data3=0;
  unsigned int mcs_line_number=0;

  /* Initialize the local storage array */
  memset((char *)MCS_DATA,0xff,sizeof(MCS_DATA));

  mcsFile = fopen(filename,"r");
  if(mcsFile==NULL)
    {
      perror("fopen");
      printf("%s: ERROR opening file (%s) for reading\n",
	     __FUNCTION__,filename);
      return ERROR;
    }

  ifpga=MAIN; /* First firmware is for the MAIN FPGA */

  while(!feof(mcsFile))
    {
      mcs_line_number++;
      /* Get the current line */
      if(!fgets(ihexLine, sizeof(ihexLine), mcsFile))
	break;
      
      /* Get the the length of this line */
      len = strlen(ihexLine);

      if(len >= 5)
	{
	  /* Check for the start code */
	  if(ihexLine[0] != ':')
	    {
	      printf("%s: ERROR parsing file at line %d\n",
		     __FUNCTION__,line);
	      return ERROR;
	    }

	  /* Get the byte count */
	  hiChar = hex2num(ihexLine[1]);
	  loChar = hex2num(ihexLine[2]);
	  datalen = (hiChar)<<4 | loChar;

	  if(strncmp("00",&ihexLine[7], 2) == 0) /* Data Record */
	    {
	      /* Get the address */
	      addr3 = hex2num(ihexLine[3]);
	      addr2 = hex2num(ihexLine[4]);
	      addr1 = hex2num(ihexLine[5]);
	      addr0 = hex2num(ihexLine[6]);
	      mcs_addr = (elar_data<<16) | (addr3<<12) | (addr2<<8) | (addr1<<4) | addr0;

	      /* Determine the initial page and byte number from this address */
	      ipage = (int)(mcs_addr / FA125_FIRMWARE_MAX_BYTE_PER_PAGE);
	      ibyte = (int)(mcs_addr % FA125_FIRMWARE_MAX_BYTE_PER_PAGE);

	      if(getFirmwareLocation==1)
		{
		  /* ipage = sfpga[ifpga].page_location; */
		  /* ibyte = sfpga[ifpga].page_byte_location; */
		  sfpga[ifpga].page_location = ipage;
		  sfpga[ifpga].page_byte_location = ibyte;
		  getFirmwareLocation=0;
		}

	      pData = &ihexLine[9]; /* point to the beginning of the data */
	      while(datalen--)
		{
		  hiChar = hex2num(*pData++);
		  loChar = hex2num(*pData++);
		  MCS_DATA[ipage][ibyte] = 
		    ((hiChar)<<4) | (loChar);
		  fpga_bytes++;
		  if(readMCS>=MCS_MAX_SIZE)
		    {
		      printf("%s: ERROR: TOO BIG!\n",__FUNCTION__);
		      return ERROR;
		    }
/* 		  if(ipage<2) */
/* 		    printf("%4d %3d: 0x%02x\n",ipage,ibyte,MCS_DATA[ipage][ibyte]); */
		  if((ibyte+1)==FA125_FIRMWARE_MAX_BYTE_PER_PAGE)
		    { /* If at the end of the page, start up a new one */
		      ibyte=0;
		      ipage++;
		    }
		  else
		    {
		      ibyte++;
		    }
#ifdef OLDWAY
		  if(fpga_bytes==sfpga[ifpga].size)
		    { 
		      /* End of this FPGA, Skip to the Next */
		      ifpga++;
		      if(ifpga!=NFPGATYPE) 
			{
			  ipage = sfpga[ifpga].page_location;
			  ibyte = sfpga[ifpga].page_byte_location;
			  fpga_bytes=0;
			}
		    }
#endif
		  readMCS++;
		  nbytes++;
		}
	    }
	  else if(strncmp("04",&ihexLine[7], 2) == 0) /* ELAR */
	    {
	      /* Get the elar data */
	      data3 = hex2num(ihexLine[9]);
	      data2 = hex2num(ihexLine[10]);
	      data1 = hex2num(ihexLine[11]);
	      data0 = hex2num(ihexLine[12]);
	      elar_data = (data3<<12) | (data2<<8) | (data1<<4) | data0;

/* 	      if(prev_addr!=0xFFF0) */
	      if(elar_data != (prev_elar_data + 1))
		{
		  if(ifpga!=NFPGATYPE) 
		    {
		      sfpga[ifpga].size = fpga_bytes;
		      ifpga++;
		      getFirmwareLocation=1;
		      /* ipage = sfpga[ifpga].page_location; */
		      /* ibyte = sfpga[ifpga].page_byte_location; */
		      fpga_bytes=0;
		    }
/* 		  printf("%8d: fpga_bytes = %8d ipage = 0x%06x (bytes = 0x%x)\n", */
/* 			 mcs_line_number,fpga_bytes,ipage, ipage*FA125_FIRMWARE_MAX_BYTE_PER_PAGE); */
		}
	      prev_elar_data = elar_data;
	    }
	  else if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_MCS_SKIPPED_LINES)
	    {
	      printf("%s: Skipped line (%d): \t>%s<\n",
		     __FUNCTION__,line,ihexLine);
	    }

	}
      line++;
    }

  MCS_pageSize = ipage+1;
  MCS_dataSize = readMCS;
  
  if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_MCS_FILE)
    {
      printf("MCS_dataSize = %d   MCS_pageSize = %d\n",MCS_dataSize,MCS_pageSize);
      
      for(ichar=0; ichar<16*10; ichar++)
	{
	  if((ichar%16) == 0)
	    printf("\n");
	  printf("0x%02x ",MCS_DATA[0][ichar]);
	}
      printf("\n\n");
    }

  MCS_loaded = 1;

  fa125FirmwarePrintFPGAStats();

  fclose(mcsFile);
  return OK;
}

void
fa125FirmwarePrintFPGAStats()
{
  int ifpga=MAIN;

  for(ifpga=MAIN; ifpga<NFPGATYPE; ifpga++)
    {
      printf("%s: size = %d   location = 0x%08x\n",
	     sfpga[ifpga].name, sfpga[ifpga].size, sfpga[ifpga].location);
    }


}

void
fa125FirmwarePrintPage(int page)
{
  int ichar=0;

  if(MCS_loaded==0)
    {
      printf("%s: ERROR: MCS file not loaded into memory\n",
	     __FUNCTION__);
      return;
    }

  for(ichar=0; ichar<FA125_FIRMWARE_MAX_BYTE_PER_PAGE; ichar++)
    {
      if((ichar%16) == 0)
	printf("\n");
      printf("0x%02x ",MCS_DATA[page][ichar]);
    }
  printf("\n\n");
  


}


int
fa125FirmwareEraseFull(int id)
{
  int ipage=0;
  int iblock=0, nblocks=1024;
  int stayon=1;
  struct timespec time_start, time_end, res;

  if(id==0) id=fa125ID[0];

  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("%s: ERROR : FA125 in slot %d is not initialized \n",(int)__FUNCTION__,id,3,4,5,6);
      return ERROR;
    }

#ifndef VXWORKSPPC  
  if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_MEASURE_TIMES)
    {
      fa125FWstats.nblocks_erased=0;
      fa125FWstats.erase_time.tv_sec  = 0;
      fa125FWstats.erase_time.tv_nsec = 0;
    }
#endif

  printf("** Erasing Main Memory **\n");
  for(iblock=0; iblock<nblocks; iblock++)
    {
      if((iblock%0x10)==0) 
	{
	  printf(".");
	  fflush(stdout);
	}

#ifndef VXWORKSPPC  
      if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_MEASURE_TIMES)
	{
	  clock_gettime(CLOCK_MONOTONIC, &time_start);
	}
#endif

      /* Perform a block erase */
      if(fa125FirmwareBlockErase(id,iblock,stayon,1)!=OK)
	{
	  printf("\n%s: Block erase failed\n",__FUNCTION__);
	  return ERROR;
	}

#ifndef VXWORKSPPC  
      if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_MEASURE_TIMES)
	{
	  fa125FWstats.nblocks_erased++;
	  clock_gettime(CLOCK_MONOTONIC, &time_end);
	  res = tsSubtract(time_end, time_start);
	  fa125FWstats.erase_time = tsAdd(res, fa125FWstats.erase_time);
	}
#endif
    }
  printf("\n");
  fflush(stdout);

  taskDelay(3);

  if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_VERIFY_ERASE)
    {
      stayon=1;
      printf("** Verify erase **\n");
      for(iblock=0; iblock<nblocks; iblock++)
	{
	  if((iblock%0x10)==0) 
	    {
	      printf(".");
	      fflush(stdout);
	    }
  
	  for(ipage=iblock*8; ipage<8*(iblock+1); ipage++)
	    {

	      /* Read a page from main memory */
	      if(fa125FirmwareReadMainPage(id, ipage, stayon)!=OK)
		{
		  vmeWrite32(&fa125p[id]->main.configAdrData, 0);
		  printf("\n%s: Error reading from main memory (page = %d)\n",
			 __FUNCTION__,ipage);
		  return ERROR;
		}
	  
	      /* Verify the page was erased */
	      if(fa125FirmwareVerifyErasedPage(ipage)!=OK)
		{
		  printf("\n%s: Block erase failed to erase block %d (page %d)\n",
			 __FUNCTION__,iblock,ipage);
		  return ERROR;
		}
	    }
	}
      printf("\n");
      fflush(stdout);
    }

  return OK;

}

int
fa125FirmwareGEraseFull()
{
  int ipage=0;
  int iblock=0, nblocks=1024;
  int stayon=1;
  struct timespec time_start, time_end, res;
  int id=0, ifa=0;
  int nerrors=0;
  
#ifndef VXWORKSPPC  
  if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_MEASURE_TIMES)
    {
      fa125FWstats.nblocks_erased=0;
      fa125FWstats.erase_time.tv_sec  = 0;
      fa125FWstats.erase_time.tv_nsec = 0;
    }
#endif

  memset((char *)fa125FirmwareErrorFlags, 0, sizeof(fa125FirmwareErrorFlags));

  printf("** Erasing Main Memory **\n");
  printf("All: ");
  fflush(stdout);

  for(iblock=0; iblock<nblocks; iblock++)
    {
      for(ifa=0; ifa<nfa125; ifa++)
	{
	  id = fa125Slot(ifa);

	  if(((iblock%0x10)==0) & (ifa==0))
	    {
	      printf(".");
	      fflush(stdout);
	    }

	  if((iblock!=0) && (ifa==0))
	    {
	      /* Wait for the previous block on first module to complete */
	      taskDelay(7);
	    }

	  if(fa125FirmwareErrorFlags[id]!=0) 
	    continue;

	  if(iblock!=0)
	    {
#ifndef VXWORKSPPC  
	      if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_MEASURE_TIMES)
		{
		  fa125FWstats.nblocks_erased++;
		  clock_gettime(CLOCK_MONOTONIC, &time_end);
		  res = tsSubtract(time_end, time_start);
		  fa125FWstats.erase_time = tsAdd(res, fa125FWstats.erase_time);
		}
#endif
	    }
	  
#ifndef VXWORKSPPC  
	  if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_MEASURE_TIMES)
	    {
	      clock_gettime(CLOCK_MONOTONIC, &time_start);
	    }
#endif

	  /* Perform a block erase */
	  if(fa125FirmwareBlockErase(id,iblock,stayon,0)!=OK)
	    {
	      printf("\n%s: Slot %d: Block erase failed to begin\n",
		     __FUNCTION__,id);
	      fa125FirmwareErrorFlags[id] |= FA125_FIRMWARE_ERROR_ERASE;
/* 	      return ERROR; */
	    }

	} /* nfa125 */
    } /* nblocks */
      

  printf("\n");
  fflush(stdout);

  /* Wait for last block erase to complete */
  taskDelay(7);

#ifndef VXWORKSPPC  
  if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_MEASURE_TIMES)
    {
      fa125FWstats.nblocks_erased++;
      clock_gettime(CLOCK_MONOTONIC, &time_end);
      res = tsSubtract(time_end, time_start);
      fa125FWstats.erase_time = tsAdd(res, fa125FWstats.erase_time);
    }
#endif  
  if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_VERIFY_ERASE)
      {
	stayon=1;
	printf("** Verify erase **\n");
	for(ifa=0; ifa<nfa125; ifa++)
	  {
	    id = fa125Slot(ifa);
	    
	    if(fa125FirmwareErrorFlags[id]!=0) 
	      continue;

	    printf("%3d: ",id);
	    fflush(stdout);

	    for(iblock=0; iblock<nblocks; iblock++)
	      {
		if((iblock%0x10)==0)
		  {
		    printf(".");
		    fflush(stdout);
		  }
	      
		if(fa125FirmwareErrorFlags[id]!=0) 
		  break;

		for(ipage=iblock*8; ipage<8*(iblock+1); ipage++)
		  {
		  
		    if(fa125FirmwareErrorFlags[id]!=0) 
		      break;

		    /* Read a page from main memory */
		    if(fa125FirmwareReadMainPage(id, ipage, stayon)!=OK)
		      {
			vmeWrite32(&fa125p[id]->main.configAdrData, 0);
			printf("\n%s: Slot %d: Error reading from main memory (page = %d)\n",
			       __FUNCTION__,id,ipage);
			fa125FirmwareErrorFlags[id] |= FA125_FIRMWARE_ERROR_VERIFY_ERASE;
/* 			return ERROR; */
		      }
		  
		    /* Verify the page with that read from the file */
		    if(fa125FirmwareVerifyErasedPage(ipage)!=OK)
		      {
			printf("\n%s: Slot %d: Block erase failed to erase block %d (page %d)\n",
			       __FUNCTION__,id, iblock,ipage);
			fa125FirmwareErrorFlags[id] |= FA125_FIRMWARE_ERROR_VERIFY_ERASE;
/* 			return ERROR; */
		      }
		  }
	      } /* nblocks */
	    printf("\n");
	  } /* nfa125 */

	fflush(stdout);
      }

  /* Count how many modules had errors */
  for(ifa=0; ifa<nfa125; ifa++)
    {
      id = fa125Slot(ifa);
      if(fa125FirmwareErrorFlags[id]!=0)
	nerrors++;
    }

  /* Return ERROR if all modules had errors, otherwise we can continue */
  if(nerrors==nfa125)
    return ERROR;

  return OK;

}

int
fa125FirmwareWriteFull(int id)
{
  int ipage=0;
  struct timespec time_start, time_end, res;

  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("%s: ERROR : FA125 in slot %d is not initialized \n",(int)__FUNCTION__,id,3,4,5,6);
      return ERROR;
    }

#ifndef VXWORKSPPC  
  if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_MEASURE_TIMES)
    {
      fa125FWstats.nbuffers_written          = 0;
      fa125FWstats.buffer_write_time.tv_sec  = 0;
      fa125FWstats.buffer_write_time.tv_nsec = 0;

      fa125FWstats.nbuffers_pushed          = 0;
      fa125FWstats.buffer_push_time.tv_sec  = 0;
      fa125FWstats.buffer_push_time.tv_nsec = 0;
    }
#endif

  printf("** Writing file to memory **\n");
  for(ipage=0; ipage<=MCS_pageSize; ipage++)
    {
      if((ipage%(8*0x10))==0)
	{
	  printf(".");
	  fflush(stdout);
	}

#ifndef VXWORKSPPC  
      if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_MEASURE_TIMES)
	{
	  clock_gettime(CLOCK_MONOTONIC, &time_start);
	}
#endif
      if(fa125FirmwareWriteToBuffer(id, ipage)!=OK)
	{
	  printf("\n%s: Error writing to buffer\n",__FUNCTION__);
	  return ERROR;
	}

#ifndef VXWORKSPPC  
      if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_MEASURE_TIMES)
	{
	  fa125FWstats.nbuffers_written++;
	  clock_gettime(CLOCK_MONOTONIC, &time_end);
	  res = tsSubtract(time_end, time_start);
	  fa125FWstats.buffer_write_time = tsAdd(fa125FWstats.buffer_write_time, res);
	}
#endif

      if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_WRITE_BUFFER)
	{
	  if(fa125FirmwareReadBuffer(id)!=OK)
	    {
	      printf("\n%s: Error reading from buffer\n",__FUNCTION__);
	      return ERROR;
	    }

	  if(fa125FirmwareVerifyPage(ipage)!=OK)
	    {
	      printf("\n%s: ERROR in verifying page %d\n",
		     __FUNCTION__,ipage);
	      return ERROR;
	    }
	}

#ifndef VXWORKSPPC  
      if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_MEASURE_TIMES)
	{
	  clock_gettime(CLOCK_MONOTONIC, &time_start);
	}
#endif
      if(fa125FirmwarePushBufferToMain(id, ipage, 1)!=OK)
	{
	  printf("\n%s: Error in pushing buffer to main memory (page = %d)\n",
		 __FUNCTION__,ipage);
	  return ERROR;
	}
#ifndef VXWORKSPPC  
      if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_MEASURE_TIMES)
	{
	  fa125FWstats.nbuffers_pushed++;
	  clock_gettime(CLOCK_MONOTONIC, &time_end);
	  res = tsSubtract(time_end, time_start);
	  fa125FWstats.buffer_push_time = tsAdd(fa125FWstats.buffer_push_time, res);
	}
#endif
    }
  
  printf("\n");
  fflush(stdout);

  printf("** Verifying Main Memory **\n");
  if(fa125FirmwareVerifyFull(id)!=OK)
    {
      printf("\n%s: Error in verifying full firmware\n",
	     __FUNCTION__);
      return ERROR;
    }
  
  return OK;
}

int
fa125FirmwareGWriteFull()
{
  int id=0, ifa=0;
  int ipage=0;
  struct timespec time_start, time_end, res;

  if(id==0) id=fa125ID[0];
  
  if((id<0) || (id>21) || (fa125p[id] == NULL)) 
    {
      logMsg("%s: ERROR : FA125 in slot %d is not initialized \n",(int)__FUNCTION__,id,3,4,5,6);
      return ERROR;
    }

#ifndef VXWORKSPPC  
  if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_MEASURE_TIMES)
    {
      fa125FWstats.nbuffers_written          = 0;
      fa125FWstats.buffer_write_time.tv_sec  = 0;
      fa125FWstats.buffer_write_time.tv_nsec = 0;

      fa125FWstats.nbuffers_pushed          = 0;
      fa125FWstats.buffer_push_time.tv_sec  = 0;
      fa125FWstats.buffer_push_time.tv_nsec = 0;
    }
#endif

  printf("** Writing file to memory **\n");
  printf("All: ");
  fflush(stdout);

  for(ipage=0; ipage<=MCS_pageSize; ipage++)
    {
      for(ifa=0; ifa<nfa125; ifa++)
	{
	  id = fa125Slot(ifa);
	  
	  if(((ipage%(8*0x10))==0) && (ifa==0))
	    {
	      printf(".");
	      fflush(stdout);
	    }

	  if(fa125FirmwareErrorFlags[id]!=0) 
	    continue;

	  if(ipage!=0)
	    {
	      if(fa125FirmwareWaitForPushBufferToMain(id, ipage-1)!=OK)
		{
		  printf("\n%s: Slot %d: Failed to push buffer to main (page %d)\n",
			 __FUNCTION__,id,ipage-1);
		  fa125FirmwareErrorFlags[id] |= FA125_FIRMWARE_ERROR_PUSH_WAIT;
/* 		  return ERROR; */
		}

	      /* Wait for the previous page push to complete */
#ifndef VXWORKSPPC  
	      if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_MEASURE_TIMES)
		{
		  fa125FWstats.nbuffers_pushed++;
		  clock_gettime(CLOCK_MONOTONIC, &time_end);
		  res = tsSubtract(time_end, time_start);
		  fa125FWstats.buffer_push_time = tsAdd(fa125FWstats.buffer_push_time, res);
		}
#endif
	    }

#ifndef VXWORKSPPC  
	  if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_MEASURE_TIMES)
	    {
	      clock_gettime(CLOCK_MONOTONIC, &time_start);
	    }
#endif

	  /* Write page to buffer */
	  if(fa125FirmwareWriteToBuffer(id, ipage)!=OK)
	    {
	      printf("\n%s: Slot %d: Error writing to buffer\n",__FUNCTION__,id);
	      fa125FirmwareErrorFlags[id] |= FA125_FIRMWARE_ERROR_WRITE;
/* 	      return ERROR; */
	    }

#ifndef VXWORKSPPC  
	  if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_MEASURE_TIMES)
	    {
	      fa125FWstats.nbuffers_written++;
	      clock_gettime(CLOCK_MONOTONIC, &time_end);
	      res = tsSubtract(time_end, time_start);
	      fa125FWstats.buffer_write_time = tsAdd(fa125FWstats.buffer_write_time, res);
	    }

	  if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_MEASURE_TIMES)
	    {
	      clock_gettime(CLOCK_MONOTONIC, &time_start);
	    }
#endif

	  /* Push buffer to main */
	  if(fa125FirmwarePushBufferToMain(id, ipage, 0)!=OK)
	    {
	      printf("\n%s: Error in pushing buffer to main memory (page = %d)\n",
		     __FUNCTION__,ipage);
	      fa125FirmwareErrorFlags[id] |= FA125_FIRMWARE_ERROR_PUSH;
/* 	      return ERROR; */
	    }

	} /* nfa125 */
    } /* MCS_pageSize */

  for(ifa=0; ifa<nfa125; ifa++)
    {
      id = fa125Slot(ifa);

      if(fa125FirmwareErrorFlags[id]!=0) 
	continue;

      /* Wait for last page push to complete */
      if(fa125FirmwareWaitForPushBufferToMain(id, MCS_pageSize-1)!=OK)
	{
	  printf("\n%s: Slot %d: Failed to push buffer to main (page %d)\n",
		 __FUNCTION__,id,ipage-1);
	  fa125FirmwareErrorFlags[id] |= FA125_FIRMWARE_ERROR_PUSH_WAIT;
/* 	  return ERROR; */
	}

#ifndef VXWORKSPPC  
      if(fa125FirmwareDebug&FA125_FIRMWARE_DEBUG_MEASURE_TIMES)
	{
	  fa125FWstats.nbuffers_pushed++;
	  clock_gettime(CLOCK_MONOTONIC, &time_end);
	  res = tsSubtract(time_end, time_start);
	  fa125FWstats.buffer_push_time = tsAdd(fa125FWstats.buffer_push_time, res);
	}
#endif
      
    } /* nfa125 */
  
  printf("\n");
  fflush(stdout);

  printf("** Verifying Main Memory **\n");
  for(ifa=0; ifa<nfa125; ifa++)
    {
      id = fa125Slot(ifa);

      if(fa125FirmwareErrorFlags[id]!=0) 
	continue;

      if(fa125FirmwareVerifyFull(id)!=OK)
	{
	  printf("\n%s: Slot %d: Error in verifying full firmware\n",
		 __FUNCTION__,id);
	  fa125FirmwareErrorFlags[id] |= FA125_FIRMWARE_ERROR_VERIFY_WRITE;
/* 	  return ERROR; */
	}
    }
  
  return OK;
}

void
fa125FirmwarePrintTimes()
{
  double erase, write, push, read;

  erase = fa125FWstats.erase_time.tv_sec 
    + (double)fa125FWstats.erase_time.tv_nsec*1e-9;
  write = fa125FWstats.buffer_write_time.tv_sec 
    + (double)fa125FWstats.buffer_write_time.tv_nsec*1e-9;
  push  = fa125FWstats.buffer_push_time.tv_sec 
    + (double)fa125FWstats.buffer_push_time.tv_nsec*1e-9;
  read  = fa125FWstats.main_page_read_time.tv_sec 
    + (double)fa125FWstats.main_page_read_time.tv_nsec*1e-9;

  printf("\n");

  printf(" Blocks Erased  = %d\n",
	 fa125FWstats.nblocks_erased);
  printf(" Erase time   %5ld (sec)  %10ld (ns)  = %lf (sec)\n",
	 fa125FWstats.erase_time.tv_sec,
	 fa125FWstats.erase_time.tv_nsec,
	 erase);
  printf("\n");

  printf(" Pages written  = %d\n",
	 fa125FWstats.nbuffers_written);
  printf(" Write time   %5ld (sec)  %10ld (ns)  = %lf (sec)\n",
	 fa125FWstats.buffer_write_time.tv_sec,
	 fa125FWstats.buffer_write_time.tv_nsec,
	 write);
  printf("\n");

  printf(" Pages pushed   = %d\n",
	 fa125FWstats.nbuffers_pushed);
  printf(" Push time    %5ld (sec)  %10ld (ns)  = %lf (sec)\n",
	 fa125FWstats.buffer_push_time.tv_sec,
	 fa125FWstats.buffer_push_time.tv_nsec,
	 push);
  printf("\n");
  

  printf(" Pages verified = %d  (per module)\n",
	 fa125FWstats.npages_read);
  printf(" Read time    %5ld (sec)  %10ld (ns)  = %lf (sec)\n",
	 fa125FWstats.main_page_read_time.tv_sec,
	 fa125FWstats.main_page_read_time.tv_nsec,
	 read);
  printf("\n");
}

int
fa125FirmwareGCheckErrors()
{
  int ifa=0, id=0;
  int rval=OK;

  for(ifa=0; ifa<nfa125; ifa++)
    {
      id = fa125Slot(ifa);

      printf("%3d: ",id);
      fflush(stdout);

      if(fa125FirmwareErrorFlags[id] == 0)
	{
	  printf(" OK!\n");
	  continue;
	}

      if(fa125FirmwareErrorFlags[id] & FA125_FIRMWARE_ERROR_ERASE)
	{
	  printf(" ERROR on Erasing Main Memory\n");
	  rval=ERROR;
	}

      if(fa125FirmwareErrorFlags[id] & FA125_FIRMWARE_ERROR_VERIFY_ERASE)
	{
	  printf(" ERROR on Verifying Erased Main Memory\n");
	  rval=ERROR;
	}

      if(fa125FirmwareErrorFlags[id] & FA125_FIRMWARE_ERROR_WRITE)
	{
	  printf(" ERROR on Writing to Buffer\n");
	  rval=ERROR;
	}

      if(fa125FirmwareErrorFlags[id] & FA125_FIRMWARE_ERROR_PUSH)
	{
	  printf(" ERROR on Pushing Buffer to Main Memory\n");
	  rval=ERROR;
	}

      if(fa125FirmwareErrorFlags[id] & FA125_FIRMWARE_ERROR_PUSH_WAIT)
	{
	  printf(" ERROR on Waiting to Push Buffer to Main Memory\n");
	  rval=ERROR;
	}

      if(fa125FirmwareErrorFlags[id] & FA125_FIRMWARE_ERROR_VERIFY_WRITE)
	{
	  printf(" ERROR on Verifying Firmware in Main Memory\n");
	  rval=ERROR;
	}

    }

  return rval;
}

const char *fa125_mode_names[FA125_SUPPORTED_NMODES] = 
  {
    "Raw Window Mode"
  };

/*
 * File:
 *    fa125PulserTest.c
 *
 * Description:
 *    Test pulser generation with the fADC125
 *
 *
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "jvme.h"
#include "tiLib.h"
#include "sdLib.h"
#include "fa125Lib.h"

void myISR(int arg);

DMA_MEM_ID vmeIN,vmeOUT;
extern DMANODE *the_event;
extern unsigned int *dma_dabufp;

#define BLOCKLEVEL 1
#define PROCMODE   8
#define NSA        0
#define NSB        0
#define PL      0x50
#define PTW     0x28

extern int nfa125;

int 
main(int argc, char *argv[]) 
{
  int doPulse=0, doTrig=0;
  int delay=0;
  int nPulses=1;
  int stat;

  printf("\nfADC125 pulser generation test\n");
  printf("----------------------------\n");

  switch(argc)
    {
    case 1: /* Just issue a pulse */
      doPulse=1;
      break;

    case 2: /* First arg is number of pulses to send (separated by 1 second) */
      doPulse=1;
      nPulses = strtol(argv[1],NULL,10);
      break;

    case 3: /* Second argument is to do a trig1... it's the delay */
      doPulse=1;
      doTrig=1;
      nPulses = strtol(argv[1],NULL,10);
      delay   = strtol(argv[2],NULL,10);
      break;

    default:
      printf("Bad number of arguments (%d).  Must use ",argc);
      exit(-1);
    }

  vmeOpenDefaultWindows();
    
  /* Setup Address and data modes for DMA transfers
   *   
   *  vmeDmaConfig(addrType, dataType, sstMode);
   *
   *  addrType = 0 (A16)    1 (A24)    2 (A32)
   *  dataType = 0 (D16)    1 (D32)    2 (BLK32) 3 (MBLK) 4 (2eVME) 5 (2eSST)
   *  sstMode  = 0 (SST160) 1 (SST267) 2 (SST320)
   */
  vmeDmaConfig(2,5,1);

  /* INIT dmaPList */

  dmaPFreeAll();
  vmeIN  = dmaPCreate("vmeIN",1024*100,5,0);
  vmeOUT = dmaPCreate("vmeOUT",0,0,0);
    
  dmaPStatsAll();

  dmaPReInitAll();

  tiInit(0,TI_READOUT_EXT_POLL,0);
  tiLoadTriggerTable(0);
    
  tiSetTriggerHoldoff(1,4,0);
  tiSetTriggerHoldoff(2,4,0);

  tiSetBlockLimit(0);
  tiDisableDataReadout();
  tiDisableA32();
  tiSetTriggerSource(TI_TRIGGER_PULSER);
  tiClockReset();
  taskDelay(1);
  tiTrigLinkReset();
  taskDelay(1);
  tiEnableVXSSignals();
  tiSetBlockLevel(BLOCKLEVEL);

  tiStatus(1);

  int iFlag=0;
  /*
    bits 2-1:  defines Trigger source
    (0) 0 0  VXS (P0)
    (1) 0 1  Internal Timer
    (2) 1 0  Internal Multiplicity Sum
    (3) 1 1  P2 Connector (Backplane)
    bit    3:  NOT USED WITH THIS FIRMWARE VERSION
    bits 5-4:  defines Clock Source
    (0) 0 0  P2 Connector (Backplane)
    (1) 0 1  VXS (P0)
    (2) 1 0  Internal 125MHz Clock
  */
  iFlag |= (0<<1);  /* Trigger Source */
  iFlag |= (1<<4);  /* Clock Source */
  iFlag |= FA125_INIT_SKIP_FIRMWARE_CHECK;

  stat = fa125Init(7<<19,1<<19,2,iFlag);
  if (stat != OK) 
    {
      printf("ERROR: fa125Init failed \n");
      goto CLOSE;
    } 

  sdInit(0);
  sdSetActiveVmeSlots(fa125ScanMask());


  int iadc=0;
  fa125ResetToken(0);
  for(iadc=0; iadc<nfa125; iadc++)
    {
      fa125PowerOn(fa125Slot(iadc));
      fa125Reset(fa125Slot(iadc), 0);

      fa125SetCommonThreshold(fa125Slot(iadc), 0);
      fa125PrintThreshold(fa125Slot(iadc));

      int ichan=0;
      for(ichan=0; ichan<72; ichan++)
	{
	  fa125SetOffset(fa125Slot(iadc),ichan,0x4000);
	}
      int i=0;
      for(i=0; i<3 ; i++)
	{
	  fa125SetPulserAmplitude(0,i,0x8FFF);
	}
      fa125SetProcMode(fa125Slot(iadc),PROCMODE,PL,PTW,NSB,NSA,0);
      
      fa125SetChannelEnableMask(fa125Slot(iadc),0xFFFFFF,0xFFFFFF,0xFFFFFF);
      fa125PrintTemps(fa125Slot(iadc));

      fa125SetBlocklevel(fa125Slot(iadc), BLOCKLEVEL);

      fa125Enable(fa125Slot(iadc));
    }
  fa125Status(0,1);

  fa125ResetToken(0);
/*   tiSetBlockLimit(nPulses); */

  tiStatus(1);
  stat = tiIntConnect(TI_INT_VEC, myISR, 0);
  if (stat != OK) 
    {
      printf("ERROR: tiIntConnect failed \n");
      goto CLOSE;
    } 
  else 
    {
      printf("INFO: Attached TI Interrupt\n");
    }


  taskDelay(1);
  tiSyncReset(1);
  printf("Hit any key to enable Triggers...\n");
  getchar();
  tiIntEnable(1);
  tiSetRandomTrigger(1,0x7);

  int itrig=0, output=0;
#ifdef PULSERSTUFF
  
  if(doTrig)
    {
      printf("Setting delay to %d\n",delay);
      fa125SetPulserTriggerDelay(0, delay);
      output = 2;
    }
  for(itrig=0; itrig<nPulses; itrig++)
    {
      tiSoftTrig(1,0x1,0x700,1);
/*       fa125SoftPulser(0, output); */
      myISR(0);
      if((itrig+1)!=nPulses)
	{
	  printf("<Enter> for next trigger\n");
	  getchar();
	}
    }
#endif

  printf("Hit any key to Disable TIR and exit.\n");
  getchar();
  tiIntDisable();

  tiIntDisconnect();

  for(iadc=0; iadc<nfa125; iadc++)
    {
      fa125PrintTemps(fa125Slot(iadc));
      fa125PowerOff(fa125Slot(iadc));
    }
  fa125GStatus(0);
  printf("berr_count = %d\n",fa125GetBerrCount());
  tiStatus(0);

 CLOSE:

  vmeCloseDefaultWindows();

  exit(0);
}

/* Interrupt Service routine */
void
myISR(int arg)
{
  int dCnt, len=0,idata;
  DMANODE *outEvent;
  int timeout=0;
  static int intCount=0;
  int tibready=0;

/*   if((tibready==0) && (timeout<100)) */
/*     { */
/*       printf("NOT READY!\n"); */
/*       tibready=tiBReady(); */
/*       timeout++; */
/*     } */

/*   if(tibready==0) */
/*     { */
/*       printf("TIMEOUT!\n"); */
/*       return; */
/*     } */

  GETEVENT(vmeIN,intCount);

  *dma_dabufp++=0;

  int iread=0, iadc=0;
  timeout=100000;

  iadc=0;
  {
    /*       fa125SoftTrigger(faslot); */
    sleep(1);
    
    int rflag=1;
    if(nfa125>1) rflag=2;
    printf("Check for BReady\n");
    for(iread=0; iread<timeout; iread++)
      {
	if(fa125GBready(fa125Slot(iadc))==fa125ScanMask())
	  break;
      }
    printf("Check for Timeout\n");
    if(iread==timeout)
      {
	printf("fa125 (%d) timeout  (fa125GBready: 0x%08x != 0x%08x)\n",fa125Slot(iadc),
	       fa125GBready(fa125Slot(iadc)), fa125ScanMask());
      }
    else
      {
	printf("Readout\n");
	dCnt = fa125ReadBlock(fa125Slot(iadc),(volatile UINT32 *)dma_dabufp,0x3000,rflag);
	if(dCnt<=0)
	  {
	    printf("No fa125 (%d) data or error.  dCnt = %d\n",fa125Slot(iadc),dCnt);
	  }
	else
	  {
	    dma_dabufp += dCnt;
	  }
      }

    fa125GStatus(0);
    fa125ResetToken(fa125Slot(0));
  }

  fa125GStatus(0);
  PUTEVENT(vmeOUT);
  
  outEvent = dmaPGetItem(vmeOUT);

  if(intCount%1==0)
    {
      printf("Received %d triggers...\n",
	     intCount);

      len = outEvent->length;
      
      for(idata=0;idata<len;idata++)
	{
	  fa125DecodeData(LSWAP(outEvent->data[idata]));
	}
      printf("\n\n");
      printf(" event length = %d\n",len);
    }
  dmaPFreeItem(outEvent);

/*   tiIntAck(); */

}

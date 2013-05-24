/*
 * File:
 *    adc125LibTest.c
 *
 * Description:
 *    Test ADC15 with TI interrupts with GEFANUC Linux Driver, 
 *    adc125, and TIR library
 *
 *
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "fa125Lib.h"
#include "tiLib.h"

void myISR(int arg);

DMA_MEM_ID vmeIN,vmeOUT;
extern DMANODE *the_event;
extern unsigned int *dma_dabufp;

#define DO_READOUT

#define FA125_ADDR (15<<19)
#define TI_ADDR (21<<19)

int 
main(int argc, char *argv[]) 
{

  int stat;

  printf("\nFA125 Library Tests\n");
  printf("----------------------------\n");

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
  vmeIN  = dmaPCreate("vmeIN",1024*10,5,0);
  vmeOUT = dmaPCreate("vmeOUT",0,0,0);
    
  dmaPStatsAll();

  dmaPReInitAll();

  /*     gefVmeSetDebugFlags(vmeHdl,0x0); */
  /* Set the TI structure pointer */
  /*     tiInit((2<<19),TI_READOUT_EXT_POLL,0); */
  tiInit(TI_ADDR,TI_READOUT_EXT_POLL,0);
  tiCheckAddresses();

  char mySN[20];
  printf("0x%08x\n",tiGetSerialNumber((char **)&mySN));
  printf("mySN = %s\n",mySN);

#ifndef DO_READOUT
  tiDisableDataReadout();
  tiDisableA32();
#endif

  tiLoadTriggerTable(0);
    
  tiSetTriggerHoldoff(1,4,0);
  tiSetTriggerHoldoff(2,4,0);

  tiSetPrescale(0);
  tiSetBlockLevel(1);

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

  /*     tiSetTriggerSource(TI_TRIGGER_TSINPUTS); */
  tiSetTriggerSource(TI_TRIGGER_PULSER);
  tiEnableTSInput(0x1);

  /*     tiSetFPInput(0x0); */
  /*     tiSetGenInput(0xffff); */
  /*     tiSetGTPInput(0x0); */

  tiSetBusySource(TI_BUSY_LOOPBACK,1);

  tiSetBlockBufferLevel(1);

  tiSetFiberDelay(1,2);
  tiSetSyncDelayWidth(1,0x3f,1);
    
  tiClockReset();
  taskDelay(1);
  tiTrigLinkReset();
  taskDelay(1);
  tiEnableVXSSignals();
  taskDelay(1);
  tiSyncReset();

  taskDelay(1);
    
  tiStatus();

  sdInit();
  sdSetActiveVmeSlots( (1<<14) | (1<<16) | (1<<18) );
  sdStatus();

  extern unsigned int fa125AddrList[FA125_MAX_BOARDS];

  fa125AddrList[0] = (14<<19);
  fa125AddrList[1] = (16<<19);
  fa125AddrList[2] = (18<<19);
  int nmods=3;

  int iFlag=0;
  iFlag  = (1<<17); /* use fa125AddrList */
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

  stat = fa125Init(0,0,nmods,iFlag);
  if (stat != OK) 
    {
      printf("ERROR: fa125Init failed \n");
      goto CLOSE;
    } 
  
  int iadc=0, faslot=0;
  extern int nfa125;

  for(iadc=0; iadc<nfa125; iadc++)
    {
      faslot = fa125Slot(iadc);
      fa125PowerOn(faslot);

      int ichan=0;
      for(ichan=0; ichan<72; ichan++)
	{
	  fa125SetOffset(faslot,ichan,0x8000);
	}
  
      fa125PrintTemps(faslot);
      fa125Clear(faslot);

      fa125Status(faslot);
    }

  printf("Hit any key to enable Triggers...\n");
  getchar();

  /* Enable the TI and clear the trigger counter */
  tiIntEnable(0);
  tiStatus();
#define SOFTTRIG
#ifdef SOFTTRIG
  tiSetRandomTrigger(1,0x7);
  taskDelay(10);
  tiSoftTrig(1,0x1,0x700,0);
#endif


  printf("Hit any key to Disable TIR and exit.\n");
  getchar();

  tiStatus();

#ifdef SOFTTRIG
  /* No more soft triggers */
  /*     tidSoftTrig(0x0,0x8888,0); */
  tiSoftTrig(1,0,0x700,0);
  tiDisableRandomTrigger();
#endif

  for(iadc=0; iadc<nfa125; iadc++)
    {
      faslot = fa125Slot(iadc);
      fa125PrintTemps(faslot);
      fa125PowerOff(faslot);
      fa125Status(faslot);
    }
  printf("berr_count = %d\n",fa125GetBerrCount());

/*   fa125Clear(0); */

  tiIntDisable();

  tiIntDisconnect();


 CLOSE:

  vmeCloseDefaultWindows();

  exit(0);
}


/* Interrupt Service routine */
/* void */
/* myISR(int arg) */
/* { */
/*   volatile unsigned short reg; */
/*   unsigned int timeout=0; */
/*   volatile UINT32 data[72*10]; */
/*   int dCnt=0; */
/*   unsigned int i; */
/*   static unsigned int intCount=0; */

/*   /\*   if(tirIntCount%1000 == 0) *\/ */
/*   /\*     printf("Got %d interrupts (Data = 0x%x)\n",tirIntCount,reg); *\/ */

/*   intCount++; */

/* /\*   reg = tirReadData(); *\/ */
/*   do {timeout++;} while((!fa125Poll(0)) && timeout<100000); */
/*   { */
/*     if(timeout==100000)  */
/*       printf("timeout\n"); */
/*     else */
/*       dCnt = fa125ReadEvent(0,(volatile UINT32 *)&data,72*2,(2)<<3); */
/*     /\* 	  printf("%4d: timeout = %d, dCnt = %d\n",intCount,timeout,dCnt); *\/ */
/*   } */

/* /\*   tirIntOutput(0); *\/ */

/*   if(tirIntCount%10==0 && dCnt>0) */
/*     { */
/*       printf("Received %d triggers... dCnt = %d\n",tirIntCount, dCnt); */
/*       for(i=0; i<100;i++) */
/* 	{ */
/* 	  printf(" 0x%08x  ",data[i]); */
/* 	  if((i+1)%4==0) printf("\n"); */
/* 	} */
/*     } */
/* } */

/* Interrupt Service routine */
void
myISR(int arg)
{
  volatile unsigned short reg;
  int dCnt, len=0,idata;
  DMANODE *outEvent;
  int tibready=0, timeout=0;

  unsigned int tiIntCount = tiGetIntCount();

#ifdef DO_READOUT
  GETEVENT(vmeIN,tiIntCount);

#ifdef DOINT
  tibready = tiBReady();
  if(tibready==ERROR)
    {
      printf("%s: ERROR: tiIntPoll returned ERROR.\n",__FUNCTION__);
      return;
    }

  if(tibready==0 && timeout<10000)
    {
      printf("NOT READY!\n");
      tibready=tiBReady();
      timeout++;
    }

  if(timeout>=100)
    {
      printf("TIMEOUT!\n");
      return;
    }
#endif

  *dma_dabufp++;

  dCnt = tiReadBlock(dma_dabufp,900>>2,1);
  if(dCnt<=0)
    {
      printf("No data or error.  dCnt = %d\n",dCnt);
    }
  else
    {
      dma_dabufp += dCnt;
      /*       printf("dCnt = %d\n",dCnt); */
    
    }
#else /* DO_READOUT */
  tiResetBlockReadout();
#endif /* DO_READOUT */
    
  int iread=0, iadc=0, faslot=0;;
  extern int nfa125;
  timeout=100000;

  for(iadc=0; iadc<nfa125; iadc++)
    {
      faslot = fa125Slot(iadc);
      do {iread++;} while((!fa125Poll(faslot)) && iread<timeout);
      {
	if(iread==timeout) 
	  printf("fa125 (%d) timeout\n",faslot);
	else
	  {
	    dCnt = fa125ReadEvent(faslot,(volatile UINT32 *)dma_dabufp,72*3,(2)<<3);
	    /* 	  printf("%4d: timeout = %d, dCnt = %d\n",intCount,timeout,dCnt); */
	    if(dCnt<=0)
	      {
		printf("No fa125 (%d) data or error.  dCnt = %d\n",faslot,dCnt);
	      }
	    else
	      {
		dma_dabufp += dCnt;
	      }
	  }
      }
    }

  PUTEVENT(vmeOUT);
  
  outEvent = dmaPGetItem(vmeOUT);
#define READOUT
#ifdef READOUT
  if(tiIntCount%1000==0)
    {
      printf("Received %d triggers...\n",
	     tiIntCount);

      len = outEvent->length;
      
      for(idata=0;idata<len;idata++)
	{
	  if((idata%5)==0) printf("\n\t");
	  printf("  0x%08x ",(unsigned int)LSWAP(outEvent->data[idata]));
	}
      printf("\n\n");
    }
#endif
  dmaPFreeItem(outEvent);

  if(tiIntCount%1000==0)
    printf("intCount = %d\n",tiIntCount );
/*     sleep(1); */
}

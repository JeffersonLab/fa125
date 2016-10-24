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
#include <sys/prctl.h>
#include "jvme.h"
#include "fa125Lib.h"
#include "tiLib.h"

static void fa125StartPollingThread(void);
void myISR(int arg);

DMA_MEM_ID vmeIN,vmeOUT;
extern DMANODE *the_event;
extern unsigned int *dma_dabufp;
extern int nfa125;

#define DO_READOUT

#define FA125_ADDR (15<<19)
#define TI_ADDR (21<<19)

int 
main(int argc, char *argv[]) 
{

  int stat;

  printf("\nFA125 Self Triggering Test\n");
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
  vmeIN  = dmaPCreate("vmeIN",1024*100,5,0);
  vmeOUT = dmaPCreate("vmeOUT",0,0,0);
    
  dmaPStatsAll();

  dmaPReInitAll();

  tiInit(TI_ADDR,TI_READOUT_EXT_POLL,0);

  tiSetFiberDelay(1,2);
  tiSetSyncDelayWidth(1,0x3f,1);
    
  tiClockReset();
  taskDelay(1);
  tiTrigLinkReset();
  taskDelay(1);
  tiEnableVXSSignals();
  taskDelay(1);
  tiSyncReset(1);

  taskDelay(1);
    
  tiStatus(0);

  extern unsigned int fa125AddrList[FA125_MAX_BOARDS];

  fa125AddrList[0] = (5<<19);
  fa125AddrList[1] = (6<<19);
  fa125AddrList[2] = (7<<19);

  int nmods=18, iFlag=0, iadc;
/*   iFlag  = (1<<17); /\* use fa125AddrList *\/ */
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

  stat = fa125Init(3<<19, 1<<19, nmods, iFlag);
  if (stat != OK) 
    {
      printf("ERROR: fa125Init failed \n");
      goto CLOSE;
    } 


  fa125ResetToken(0);
  for(iadc=0; iadc<nfa125; iadc++)
    {
      int faslot = fa125Slot(iadc);
      fa125PowerOn(faslot);

      int ichan=0;
      for(ichan=0; ichan<72; ichan++)
	{
	  fa125SetOffset(faslot,ichan,0x4000);
	}
      int i=0;
      for(i=0; i<3 ; i++)
	{
	  fa125SetPulserAmplitude(0,i,0x8000);
	}
      
      fa125PrintTemps(faslot);

      fa125SetBlocklevel(faslot, 1);

      fa125Reset(faslot, 0);
      fa125Enable(faslot);
    }

  fa125GStatus(0);

  fa125ResetToken(0);

  printf("Hit any key to enable Polling from Triggers...\n");
  getchar();

  fa125StartPollingThread();  

  printf("Hit any key to Disable polling and exit.\n");
  getchar();


  for(iadc=0; iadc<nfa125; iadc++)
    {
      int faslot = fa125Slot(iadc);
      fa125PrintTemps(faslot);
      fa125PowerOff(faslot);
    }

  fa125GStatus(0);

 CLOSE:

  vmeCloseDefaultWindows();

  exit(0);
}

pthread_attr_t fa125pollthread_attr;
pthread_t      fa125pollthread;
int fa125IntCount=0;

static void
FA125Poll(void)
{
  int fa125data;
  int policy=0;
  struct sched_param sp;
  
  /* Set scheduler and priority for this thread */
  policy=SCHED_FIFO;
  sp.sched_priority=40;
  printf("%s: Entering polling loop...\n",__FUNCTION__);
  pthread_setschedparam(pthread_self(),policy,&sp);
  pthread_getschedparam(pthread_self(),&policy,&sp);
  printf ("%s: INFO: Running at %s/%d\n",__FUNCTION__,
	  (policy == SCHED_FIFO ? "FIFO"
	   : (policy == SCHED_RR ? "RR"
	      : (policy == SCHED_OTHER ? "OTHER"
		 : "unknown"))), sp.sched_priority);  
  prctl(PR_SET_NAME,"FA125Poll");

  while(1) 
    {

      pthread_testcancel();

      fa125data = 0;
	  
      fa125data = fa125Bready(fa125Slot(0));
      if(fa125data == ERROR) 
	{
	  printf("%s: ERROR: fa125Bready(..) returned ERROR.\n",__FUNCTION__);
	  break;
	}

      if(fa125data)
	{
	  INTLOCK; 
	  fa125IntCount++;

	  myISR(0);

	  INTUNLOCK;
	}
    
    }
  printf("%s: Read ERROR: Exiting Thread\n",__FUNCTION__);
  pthread_exit(0);

}

static void
fa125StartPollingThread(void)
{
  int status;

  status = 
    pthread_create(&fa125pollthread,
		   NULL,
		   (void*(*)(void *)) FA125Poll,
		   (void *)NULL);
  if(status!=0) 
    {						
      printf("%s: ERROR: fADC125 Polling Thread could not be started.\n",
	     __FUNCTION__);	
      printf("\t pthread_create returned: %d\n",status);
    }

}

/* Interrupt Service routine */
void
myISR(int arg)
{
  int dCnt, len=0,idata;
  DMANODE *outEvent;
  int timeout=0;
  int iadc=0, faslot=0;;

  GETEVENT(vmeIN,fa125IntCount);

  extern int nfa125;

  timeout=100000;

  faslot = fa125Slot(iadc);
  int rflag  = 1;

  printf("Readout\n");
  dCnt = fa125ReadBlock(faslot,(volatile UINT32 *)dma_dabufp,0x3000,rflag);
  if(dCnt<=0)
    {
      printf("No fa125 (%d) data or error.  dCnt = %d\n",faslot,dCnt);
    }
  else
    {
      dma_dabufp += dCnt;
    }

  PUTEVENT(vmeOUT);
  
  outEvent = dmaPGetItem(vmeOUT);
#define READOUT
#ifdef READOUT
  len = outEvent->length;
  
  for(idata=0;idata<len;idata++)
    {
      fa125DecodeData(LSWAP(outEvent->data[idata]));
    }
  printf("\n\n");
  printf(" event length = %d\n",len);

  printf("<Enter> to continue\n");
  getchar();
#endif
  dmaPFreeItem(outEvent);

  if(fa125IntCount%1000==0)
    printf("intCount = %d\n",fa125IntCount );
}

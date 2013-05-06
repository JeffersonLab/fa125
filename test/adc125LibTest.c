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
#include "adc125Lib.h"
#include "tirLib.h"

void mytirISR(int arg);
extern unsigned int tirIntCount;


int 
main(int argc, char *argv[]) 
{

  int stat;

  printf("\nADC125 Library Tests\n");
  printf("----------------------------\n");

  vmeOpenDefaultWindows();
    
  tirIntInit((unsigned int)(0xed0),TIR_EXT_POLL,1);

  stat = tirIntConnect(TIR_INT_VEC, mytirISR, 0);
  if (stat != OK) 
    {
      printf("ERROR: tirIntConnect failed \n");
      goto CLOSE;
    } 
  else 
    {
      printf("INFO: Attached TIR Interrupt\n");
    }

  adc125Init(0xa10000,0);
  adc125PowerOn(0);

  int ichan=0;
  for(ichan=0; ichan<72; ichan++)
    {
      adc125SetOffset(0,ichan,32768);
    }
  

  adc125PrintTemps(0);

  printf("Hit any key to enable Triggers...\n");
  getchar();
  adc125Clear(0);

  /* Enable the TIR and clear the trigger counter */
  tirIntEnable(TIR_CLEAR_COUNT);


  printf("Hit any key to Disable TIR and exit.\n");
  getchar();

  tirIntDisable();

  adc125PrintTemps(0);
  adc125PowerOff(0);
  printf("berr_count = %d\n",adc125GetBerrCount());

/*   adc125Clear(0); */

  tirIntDisconnect();

 CLOSE:
  tirIntStatus(1);

  vmeCloseDefaultWindows();

  exit(0);
}


/* Interrupt Service routine */
void
mytirISR(int arg)
{
  volatile unsigned short reg;
  unsigned int timeout=0;
  volatile UINT32 data[72*10];
  int dCnt=0;
  unsigned int i;
  static unsigned int intCount=0;

  /*   if(tirIntCount%1000 == 0) */
  /*     printf("Got %d interrupts (Data = 0x%x)\n",tirIntCount,reg); */

  intCount++;
  tirIntOutput(1<<3);
  tirIntOutput(0);

/*   reg = tirReadData(); */
  do {timeout++;} while((!adc125Poll(0)) && timeout<100000);
  {
    if(timeout==100000) 
      printf("timeout\n");
    else
      dCnt = adc125ReadEvent(0,(volatile UINT32 *)&data,72*2,(2)<<3);
    /* 	  printf("%4d: timeout = %d, dCnt = %d\n",intCount,timeout,dCnt); */
  }

/*   tirIntOutput(0); */

  if(tirIntCount%10==0 && dCnt>0)
    {
      printf("Received %d triggers... dCnt = %d\n",tirIntCount, dCnt);
      for(i=0; i<100;i++)
	{
	  printf(" 0x%08x  ",data[i]);
	  if((i+1)%4==0) printf("\n");
	}
    }
}


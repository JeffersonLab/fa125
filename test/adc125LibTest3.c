/*
 * File:
 *    adc125LibTest2.c
 *
 * Description:
 *    Test ADC125 with hardware polling with GEFANUC Linux Driver, 
 *    and adc125 library
 *
 *
 */

#define OLDWAY

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "adc125Lib.h"

extern int polltest(int ntimes);

int 
main(int argc, char *argv[]) 
{

  int stat;

  printf("\nADC125 Library Tests\n");
  printf("----------------------------\n");

  vmeOpenDefaultWindows();
    
  adc125Init(0xa10000,0);
#ifdef OLDWAY
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

  UINT32 ievent=0, timeout=0, dCnt=0;
  volatile UINT32 data[72*2];


  while(ievent<200)
    {
      do{timeout++;} while(!adc125Poll(0));
      dCnt = adc125ReadEvent(0,&data,72*2,2<<3);
/*       printf("%4d: dCnt = %4d   timeout = %8d\n", ievent, dCnt, timeout); */
      ievent++;
      timeout=0;
    }

/*   printf("Hit any key to Disable TIR and exit.\n"); */
/*   getchar(); */

  adc125PrintTemps(0);
  adc125PowerOff(0);
#else
  polltest(10000000);
  adc125Clear(0);
#endif /*  OLDWAY */

  vmeCloseDefaultWindows();

  exit(0);
}

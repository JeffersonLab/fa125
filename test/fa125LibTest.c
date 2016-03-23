/*
 * File:
 *    fa125LibTest.c
 *
 * Description:
 *    Test fADC125 with TI interrupts with GEFANUC Linux Driver, 
 *    fa125, and TI library
 *
 *
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "jvme.h"
#include "fa125Lib.h"
#include "tiLib.h"
#include "sdLib.h"

#define TI_ADDR (21<<19)

int 
main(int argc, char *argv[]) 
{
  int ichan=0;

  printf("\nFA125 Library Tests\n");
  printf("----------------------------\n");

  vmeOpenDefaultWindows();


  fa125Init(3<<19, 1<<19,18,0);
  fa125CheckAddresses(0);
  fa125PowerOn(0);
  fa125Reset(0,0);
/*   for(ichan = 0; ichan<72; ichan++) */
/*     { */
/*       fa125SetTimingThreshold(8,ichan, ichan, ichan+1); */
/*       usleep(1000); */
/*       printf("%d: 0x%04x   0x%04x\n", */
/* 	     ichan, fa125GetTimingThreshold(8,ichan), ((ichan+1) | (ichan<<8))); */
/*     } */

  fa125SetProcMode(0,"FDC_amp_long",
		   100, // PL
		   100, // NW
		   40,  // IE
		   4,   // PG
		   1,   // NPK
		   4,   // P1
		   4    // P2
		   );
  fa125SetScaleFactors(0, 3, 2, 2);
  fa125SetCommonTimingThreshold(0,25,100);
  fa125SetCommonThreshold(0,125);
  fa125Status(0,0);
  fa125GStatus(0);
  fa125PrintThreshold(0);
  fa125PrintTimingThresholds(0);
  fa125PowerOff(0);

 CLOSE:


  vmeCloseDefaultWindows();

  exit(0);
}


/*
 * File:
 *    fa125HardReset.c
 *
 * Description:
 *    Perform a hard reset on an fADC125 at the specified address
 *
 *
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "fa125Lib.h"

void Usage();

char *progName;

int 
main(int argc, char *argv[]) 
{

  int stat;
  unsigned int fadc_address=0;

  progName = argv[0];

  if(argc<2)
    {
      printf(" ERROR: Must specify two arguments\n");
      Usage();
      exit(-1);
    }
  else
    {
      fadc_address = (unsigned int) strtoll(argv[1],NULL,16)&0xffffffff;
    }

  printf("\nFA125 HardReset\n");
  printf("----------------------------\n");

  vmeOpenDefaultWindows();
    
  int iFlag=0;
  iFlag  = FA125_INIT_SKIP; /* Skip Initialization */
  iFlag |= FA125_INIT_SKIP_FIRMWARE_CHECK; /* Skip Firmware Check */

  stat = fa125Init(fadc_address,0,1,iFlag);
  if (stat != OK) 
    {
      printf("ERROR: fa125Init failed \n");
      goto CLOSE;
    } 


  fa125Reset(0,1);

 CLOSE:

  vmeCloseDefaultWindows();

  exit(0);
}


void
Usage()
{
  printf("\n");
  printf("%s <fADC125 VME ADDRESS>\n",progName);
  printf("\n");

}

/*
 * File:
 *    fa125GFirmwareUpdate.c
 *
 * Description:
 *    JLab fADC125 firmware updating
 *     all found modules in crate
 *
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "fa125Lib.h"

char *progName;

void
Usage();

int 
main(int argc, char *argv[]) {

    int status;
    int stat=0;
    char *mcs_filename;
    int inputchar=10;
    unsigned int fadc_address=0, slotmask=0;
    int islot=0;

    printf("\nJLAB fADC125 firmware update\n");
    printf("----------------------------\n");

    progName = argv[0];

    if(argc<2)
      {
	printf(" ERROR: Must specify one argument\n");
	Usage();
	exit(-1);
      }
    else
      {
	mcs_filename = argv[1];
      }

    if(fa125FirmwareReadMcsFile(mcs_filename) != OK)
      {
	exit(-1);
      }
    
    vmeSetQuietFlag(1);
    status = vmeOpenDefaultWindows();
    if(status<0)
      {
	printf(" Unable to initialize VME driver\n");
	exit(-1);
      }

    int iFlag = (1<<18); /* Do not perform firmware check */
    iFlag |= (2<<4);     /* Internal Clock */
    stat = fa125Init(fadc_address,0x0,18,iFlag);
    if(stat<0)
      {
	printf(" Unable to initialize FADC.\n");
	goto CLOSE;
      }

    /* Should get the firmware versions here and print them out */
    
    slotmask = fa125ScanMask();

    printf(" Will update firmware with file: \n   %s\n",mcs_filename);
    printf(" for fADC125 in slots:\n\t");
    for(islot=2; islot<21; islot++)
      {
	if(slotmask & (1<<islot))
	  printf("%2d ",islot);
	else
	  printf("   ");
      }
    printf("\n");

    printf(" <ENTER> to continue... or q and <ENTER> to quit without update\n");

    inputchar = getchar();

    if((inputchar == 113) ||
       (inputchar == 81))
      {
	printf(" Exiting without update\n");
	goto CLOSE;
      }


    vmeBusLock();
    fa125FirmwareSetDebug(FA125_FIRMWARE_DEBUG_MEASURE_TIMES |
			  FA125_FIRMWARE_DEBUG_VERIFY_ERASE);

    if(fa125FirmwareGEraseFull()!=OK)
      {
	vmeBusUnlock();
	goto CLOSE;
      }


    if(fa125FirmwareGWriteFull()!=OK)
      {
	vmeBusUnlock();
	goto CLOSE;
      }

    vmeBusUnlock();

 CLOSE:

    printf("**********************************************************************\n");
    printf("                 fADC125 Firmware Update Summary\n");
    fa125FirmwareGCheckErrors();
    fa125FirmwarePrintTimes();
    printf("**********************************************************************\n");


    status = vmeCloseDefaultWindows();
    if (status != GEF_SUCCESS)
    {
      printf("vmeCloseDefaultWindows failed: code 0x%08x\n",status);
      return -1;
    }

    exit(0);
}


void
Usage()
{
  printf("\n");
  printf("%s <firmware MCS file>\n\n",progName);
  printf("\n");

}

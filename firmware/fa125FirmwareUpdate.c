/*
 * File:
 *    fa125FirmwareUpdate.c
 *
 * Description:
 *    JLab fADC125 firmware updating
 *     Single board
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
    unsigned int fadc_address=0;

    printf("\nJLAB fADC125 firmware update\n");
    printf("----------------------------\n");

    progName = argv[0];

    if(argc<3)
      {
	printf(" ERROR: Must specify two arguments\n");
	Usage();
	exit(-1);
      }
    else
      {
	mcs_filename = argv[1];
	fadc_address = (unsigned int) strtoll(argv[2],NULL,16)&0xffffffff;
      }

    fa125FirmwareSetDebug(0);

    if(fa125FirmwareReadMcsFile(mcs_filename) != OK)
      {
	exit(-1);
      }
    
    vmeSetQuietFlag(0);
    status = vmeOpenDefaultWindows();
    if(status<0)
      {
	printf(" Unable to initialize VME driver\n");
	exit(-1);
      }

    int iFlag = (1<<18); /* Do not perform firmware check */
    iFlag |= (2<<4);     /* Internal Clock */
    stat = fa125Init(fadc_address,0x0,1,iFlag);
    if(stat<0)
      {
	printf(" Unable to initialize FADC.\n");
	goto CLOSE;
      }

    /* Should get the firmware versions here and print them out */

    printf(" Will update firmware with file: \n   %s\n",mcs_filename);
    printf(" for fADC125 with VME address = 0x%08x\n",fadc_address);

    printf(" <ENTER> to continue... or q and <ENTER> to quit without update\n");

    inputchar = getchar();

    if((inputchar == 113) ||
       (inputchar == 81))
      {
	printf(" Exitting without update\n");
	goto CLOSE;
      }

/*     fa125FirmwareVerifyFull(0); */
    fa125FirmwareSetDebug(FA125_FIRMWARE_DEBUG_MEASURE_TIMES |
			  FA125_FIRMWARE_DEBUG_VERIFY_ERASE);
/*     fa125FirmwareEraseFull(0); */
/*     fa125FirmwareWriteFull(0); */
    fa125FirmwareGEraseFull();
    fa125FirmwareGWriteFull();
    fa125FirmwarePrintTimes();
    goto CLOSE;

 CLOSE:


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
  printf("%s <firmware MCS file> <fADC125 VME ADDRESS>\n",progName);
  printf("\n");

}

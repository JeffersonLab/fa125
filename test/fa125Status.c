/*
 * File:
 *    fa125Status.c
 *
 * Description:
 *    Show status of all fa125 found in crate
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


  fa125Init(3<<19, 1<<19, 18, FA125_INIT_SKIP | FA125_INIT_SKIP_FIRMWARE_CHECK);
  fa125CheckAddresses(0);

  fa125GStatus(0);
 CLOSE:


  vmeCloseDefaultWindows();

  exit(0);
}

/*
  Local Variables:
  compile-command: "make -k fa125Status"
  End:
 */

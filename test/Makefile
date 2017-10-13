#
# File:
#    Makefile
#
# Description:
#    Makefile for the JLAB extensions to the LINUXVME Controller API
#
#
# $Date$
# $Rev$
#

ifndef LINUXVME_LIB
	LINUXVME_LIB	= ${CODA}/extensions/linuxvme/libs
endif
ifndef LINUXVME_INC
	LINUXVME_INC	= ${CODA}/extensions/linuxvme/include
endif

CROSS_COMPILE		=
CC			= $(CROSS_COMPILE)gcc
AR                      = ar
RANLIB                  = ranlib
CFLAGS			= -Wall -g -O2 \
				-I. -I${LINUXVME_INC} \
				-L. -L${LINUXVME_LIB}

PROGS			= fa125LibTest fa125SelfTriggerTest


all: $(PROGS)

adc125LibTest.o: adc125LibTest.c
	$(CC) -c $(CFLAGS) $(INCS) -o $@ $<

adc125LibTest2.o: adc125LibTest2.c
	$(CC) -c $(CFLAGS) $(INCS) -o $@ $<

fa125ReadoutTest_write: fa125ReadoutTest_write.c
	$(CC) $(CFLAGS) $(INCS) -lsd -lrt -ljvme -lfa125 -lti -lts -o $@ $<

clean distclean:
	@rm -f $(PROGS) *~ *.so

%: %.c
	$(CC) $(CFLAGS) -o $@ $(@:%=%.c) -lrt -ljvme -lfa125 -lti -lts -lsd -lctp -lf1tm

.PHONY: all clean distclean

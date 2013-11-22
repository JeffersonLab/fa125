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
CFLAGS			= -Wall -g -O2 -I${LINUXVME_INC} -I. -I/usr/include \
			  -L${LINUXVME_LIB} -L.

PROGS			= adc125LibTest fa125ReadoutTest


all: $(PROGS)

adc125LibTest.o: adc125LibTest.c
	$(CC) -c $(CFLAGS) $(INCS) -o $@ $<

adc125LibTest2.o: adc125LibTest2.c
	$(CC) -c $(CFLAGS) $(INCS) -o $@ $<

clean distclean:
	@rm -f $(PROGS) *~ *.so

%: %.c
	$(CC) $(CFLAGS) -o $@ $(@:%=%.c) -lrt -ljvme -lfa125 -lti

.PHONY: all clean distclean

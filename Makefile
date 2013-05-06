#-----------------------------------------------------------------------------
#  Copyright (c) 2010 Southeastern Universities Research Association,
#                          Continuous Electron Beam Accelerator Facility
# 
#  This software was developed under a United States Government license
#  described in the NOTICE file included as part of this distribution.
# 
#  CEBAF Data Acquisition Group, 12000 Jefferson Ave., Newport News, VA 23606
#  Email: coda@cebaf.gov  Tel: (804) 249-7101  Fax: (804) 249-7363
# -----------------------------------------------------------------------------
#  
#  Description:  Makefile for Flash ADC-125.
# 	
# 	
#  Author:  Bryan Moffit, TJANF Data Acquisition Group
# 
#  SVN: $Rev$
#
#DEBUG=1
#ARCH=Linux

ifndef OSTYPE
  OSTYPE := $(subst -,_,$(shell uname))
endif


ifeq ($(OSTYPE),SunOS)
LIBS = 
EXTRA =
endif

ifndef ARCH
  ARCH=VXWORKSPPC
endif

# Defs and build for VxWorks
ifeq ($(ARCH),VXWORKSPPC)

VXWORKS_ROOT = /site/vxworks/5.5/ppc/target

  DEFS   = -mcpu=604 -DCPU=PPC604 -DVXWORKS -D_GNU_TOOL -mlongcall -fno-for-scope -fno-builtin -fvolatile -DVXWORKSPPC
  INCS   = -I. -I$(VXWORKS_ROOT)/h -I$(VXWORKS_ROOT)/h/rpc -I$(VXWORKS_ROOT)/h/net
  CC     = ccppc $(INCS) $(DEFS)
  LD     = ldppc

# explicit targets

all: adc125Lib.o

clean:
	rm -f adc125Lib.o

adc125Lib.o: adc125Lib.c adc125Lib.h
	$(CC) -c adc125Lib.c

endif

# Defs and build for Linux
ifeq ($(ARCH),Linux)

ifndef LINUXVME_LIB
	LINUXVME_LIB	= ${CODA}/extensions/linuxvme/libs
endif
ifndef LINUXVME_INC
	LINUXVME_INC	= ${CODA}/extensions/linuxvme/include
endif

CROSS_COMPILE           = 
CC			= $(CROSS_COMPILE)gcc
AR                      = ar
RANLIB                  = ranlib
CFLAGS			= -O2 -I. -I${LINUXVME_INC} -I/usr/include \
			  -L${LINUXVME_LIB} -L.
ifdef DEBUG
CFLAGS			+= -Wall -g
endif

OBJS			= adc125Lib.o

LIBS			= libadc125.a

all: $(LIBS) links

libadc125.a: adc125Lib.o
	$(CC) -fpic -shared $(CFLAGS) -o libadc125.so adc125Lib.c
	$(AR) ruv libadc125.a adc125Lib.o
	$(RANLIB) libadc125.a

clean distclean:
	@rm -f $(OBJS) $(LIBS) *.so *~

links: libadc125.a
	ln -sf $(PWD)/libadc125.a $(LINUXVME_LIB)/libadc125.a
	ln -sf $(PWD)/libadc125.so $(LINUXVME_LIB)/libadc125.so
	ln -sf $(PWD)/adc125Lib.h $(LINUXVME_INC)/adc125Lib.h

%: %.c libadc125.a
	$(CC) $(CFLAGS) -o $@ $(@:%=%.c) $(LIBS_$@) -lrt -ljvme -ladc125

rol:
	make -f Makefile-rol

rolclean:
	make -f Makefile-rol clean

.PHONY: all clean distclean

endif

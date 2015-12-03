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
#  __DATE__:
#
DEBUG=1

ifndef ARCH
	ifdef LINUXVME_LIB
		ARCH=Linux
	else
		ARCH=VXWORKSPPC
	endif
endif


# Defs and build for VxWorks
ifeq ($(ARCH),VXWORKSPPC)

VXWORKS_ROOT = /site/vxworks/5.5/ppc/target

# This should be replaced with whichever directories contain jvme.h, tiLib.h, tsLib.h..
VME_INCLUDE             ?= -I$(LINUXVME_INC)

CC			= ccppc
LD			= ldppc
DEFS			= -mcpu=604 -DCPU=PPC604 -DVXWORKS -D_GNU_TOOL -mlongcall \
				-fno-for-scope -fno-builtin -fvolatile -DVXWORKSPPC
INCS			= -I. -I$(VXWORKS_ROOT)/h -I$(VXWORKS_ROOT)/h/rpc -I$(VXWORKS_ROOT)/h/net \
			 $(VME_INCLUDE)
CFLAGS			= $(INCS) $(DEFS)

# explicit targets

all: fa125Lib.o

clean:
	rm -f fa125Lib.o

fa125Lib.o: fa125Lib.c fa125Lib.h
	@echo "Building $@ from $<"
	$(CC) $(CFLAGS) $(INCS) -c -o $@ $<

endif

# Defs and build for Linux
ifeq ($(ARCH),Linux)

LINUXVME_LIB		?= ${CODA}/linuxvme/lib
LINUXVME_INC		?= ${CODA}/linuxvme/include

CROSS_COMPILE           = 
CC			= $(CROSS_COMPILE)gcc
AR                      = ar
RANLIB                  = ranlib
CFLAGS			= -L. -L${LINUXVME_LIB} 
INCS			= -I. -I${LINUXVME_INC} 

ifdef DEBUG
CFLAGS			+= -Wall -g
else
			+= -O2
endif

OBJS			= fa125Lib.o
DEPS			= fa125Lib.d

LIBS			= libfa125.a

all: $(LIBS)

fa125Lib.o: fa125Lib.c 
	@echo "Building $@ from $<"
	$(CC) -c $(CFLAGS) $(INCS) -o $@ fa125Lib.c

libfa125.a: fa125Lib.o
	$(CC) -fpic -shared $(CFLAGS) $(INCS) -o libfa125.so fa125Lib.c
	$(AR) ruv libfa125.a fa125Lib.o
	$(RANLIB) libfa125.a

clean distclean:
	@rm -f $(OBJS) $(LIBS) $(DEPS) *.so *~

links: libfa125.a
	ln -sf $(PWD)/libfa125.a $(LINUXVME_LIB)/libfa125.a
	ln -sf $(PWD)/libfa125.so $(LINUXVME_LIB)/libfa125.so
	ln -sf $(PWD)/fa125Lib.h $(LINUXVME_INC)/fa125Lib.h

install: libfa125.a
	@cp -v $(PWD)/libfa125.a $(LINUXVME_LIB)/libfa125.a
	@cp -v $(PWD)/libfa125.so $(LINUXVME_LIB)/libfa125.so
	@cp -v $(PWD)/fa125Lib.h $(LINUXVME_INC)/fa125Lib.h

%: %.c libfa125.a
	$(CC) $(CFLAGS) -o $@ $(@:%=%.c) $(LIBS_$@) -lrt -ljvme -lfa125

%.d: %.c
	@echo "Building $@ from $<"
	@set -e; rm -f $@; \
	$(CC) -MM -shared $(INCS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

-include $(DEPS)

.PHONY: all clean distclean

endif

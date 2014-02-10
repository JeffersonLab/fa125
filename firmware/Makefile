#
# File:
#    Makefile
#
# Description:
#    Makefile for firmware updating of the fADC125
#
#
# $Date$
# $Rev$
#

ifndef ARCH
	ifdef LINUXVME_LIB
		ARCH=Linux
	else
		ARCH=VXWORKSPPC
	endif
endif

FWFILES	= $(shell cat .currentfw)

# Defs and build for VxWorks
ifeq ($(ARCH),VXWORKSPPC)
VXWORKS_ROOT = /site/vxworks/5.5/ppc/target

CC			= ccppc
LD			= ldppc
DEFS			= -mcpu=604 -DCPU=PPC604 -DVXWORKS -D_GNU_TOOL -mlongcall \
				-fno-for-scope -fno-builtin -fvolatile -DVXWORKSPPC
INCS			= -I. -I../ -I$(VXWORKS_ROOT)/h -I$(VXWORKS_ROOT)/h/rpc -I$(VXWORKS_ROOT)/h/net
CFLAGS			= $(INCS) $(DEFS)
OBJS			= fa125FirmwareUpdate.o
PROGS			=
endif #ARCH=VXWORKSPPC#

# Defs and build for Linux
ifeq ($(ARCH),Linux)

LINUXVME_LIB	?= ../../libs
LINUXVME_INC	?= ../../include
LINUXVME_BIN	?= ../../bin
LINUXVME_FW	?= ../../firmware

CC			= $(CROSS_COMPILE)gcc
AR                      = ar
RANLIB                  = ranlib
CFLAGS			= -Wall -O2 -I${LINUXVME_INC} -I. -I/usr/include \
			  -L${LINUXVME_LIB} -L.
OBJS			=
PROGS			= fa125FirmwareUpdate fa125GFirmwareUpdate
endif

ifeq ($(ARCH),VXWORKSPPC)
all: $(OBJS)
else
all: $(PROGS)
endif

clean:
	@rm -vf $(PROGS) $(OBJS) *~

%: %.c
	@echo "Making $@ for $(ARCH)"
	$(CC) $(CFLAGS) -o $@ $(@:%=%.c) -lrt -ljvme -lfa125

ifeq ($(ARCH),VXWORKSPPC)
fa125FirmwareUpdate.o: fa125FirmwareUpdate.c
	@echo "Making $@ for $(ARCH)"
	$(CC) $(CFLAGS) -c -o $@ $(<)
else
install: $(PROGS)
	@echo "Installing $(PROGS)"
	@install -v -d $(LINUXVME_BIN)
	@install -v -p $(PROGS) -D $(LINUXVME_BIN)/

fwinstall:
	@ln -sf $(FWFILES) fa125_current_firmware.mcs
	@install -v -d $(LINUXVME_FW)/fa125
	@install -p -m 664 -v $(FWFILES) fa125_current_firmware.mcs -D $(LINUXVME_FW)/fa125

endif

.PHONY: all clean

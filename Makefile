#
# File:
#    Makefile
#
# Description:
#    Makefile for fADC125
#
#
BASENAME=fa125
#
# Uncomment DEBUG line, to include some debugging info ( -g and -Wall)
DEBUG	?= 1
QUIET	?= 1
#
ifeq ($(QUIET),1)
        Q = @
else
        Q =
endif

# Override some bad habit of mismatching the ARCH with the OS.
ifeq (Linux, $(findstring Linux, ${ARCH}))
	override ARCH=$(shell uname -m)
	OS=LINUX
endif

ifeq (VXWORKS, $(findstring VXWORKS, $(ARCH)))
	override ARCH=PPC
	OS=VXWORKS
endif


ifndef ARCH
	ifeq (arm, $(findstring arm, ${MACHTYPE}))
		ARCH=armv71
		OS=LINUX
	else
		ifdef LINUXVME_LIB
			ARCH=$(shell uname -m)
			OS=LINUX
		else
			ARCH=PPC
			OS=VXWORKS
		endif
	endif
endif

# Defs and build for VxWorks
ifeq (${OS}, VXWORKS)
VXWORKS_ROOT		?= /site/vxworks/5.5/ppc/target
VME_INCLUDE             ?= -I$(LINUXVME_INC)

CC			= ccppc
LD			= ldppc
DEFS			= -mcpu=604 -DCPU=PPC604 -DVXWORKS -D_GNU_TOOL -mlongcall \
				-fno-for-scope -fno-builtin -fvolatile -DVXWORKSPPC
INCS			= -I. -I$(VXWORKS_ROOT)/h  \
				$(VME_INCLUDE)
CFLAGS			= $(INCS) $(DEFS)

endif #OS=VXWORKS#

# Defs and build for Linux
ifeq ($(OS),LINUX)
LINUXVME_LIB		?= ../lib
LINUXVME_INC		?= ../include

CC			= gcc
ifeq ($(ARCH),i686)
CC			+= -m32
endif
AR                      = ar
RANLIB                  = ranlib
CFLAGS			= -L. -L${LINUXVME_LIB}
INCS			= -I. -I${LINUXVME_INC} 

LIBS			= lib${BASENAME}.a lib${BASENAME}.so
endif #OS=LINUX#

ifdef DEBUG
CFLAGS			+= -Wall -g
else
CFLAGS			+= -O2
endif
SRC			= ${BASENAME}Lib.c
HDRS			= $(SRC:.c=.h)
OBJ			= ${BASENAME}Lib.o
DEPS			= $(SRC:.c=.d)

ifeq ($(OS),LINUX)
all: echoarch ${LIBS}
else
all: echoarch $(OBJ)
endif

%.o: %.c
	@echo " CC     $@"
	${Q}$(CC) $(CFLAGS) $(INCS) -c -o $@ $(SRC)

%.so: $(SRC)
	@echo " CC     $@"
	${Q}$(CC) -fpic -shared $(CFLAGS) $(INCS) -o $(@:%.a=%.so) $(SRC)

%.a: $(SRC)
	@echo " AR     $@"
	${Q}$(AR) ru $@ $<
	@echo " RANLIB $@"
	${Q}$(RANLIB) $@

ifeq ($(OS),LINUX)
links: $(LIBS)
	@echo " LN     $<"
	${Q}ln -sf $(PWD)/$< $(LINUXVME_LIB)/$<
	${Q}ln -sf $(PWD)/$(<:%.a=%.so) $(LINUXVME_LIB)/$(<:%.a=%.so)
	${Q}ln -sf ${PWD}/*Lib.h $(LINUXVME_INC)

install: $(LIBS)
	@echo " CP     $<"
	${Q}cp $(PWD)/$< $(LINUXVME_LIB)/$<
	@echo " CP     $(<:%.a=%.so)"
	${Q}cp $(PWD)/$(<:%.a=%.so) $(LINUXVME_LIB)/$(<:%.a=%.so)
	@echo " CP     ${BASENAME}Lib.h"
	${Q}cp ${PWD}/${BASENAME}Lib.h $(LINUXVME_INC)

%.d: %.c
	@echo " DEP    $@"
	@set -e; rm -f $@; \
	$(CC) -MM -shared $(INCS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

-include $(DEPS)

endif

clean:
	@rm -vf ${BASENAME}Lib.{o,d} lib${BASENAME}.{a,so}

echoarch:
	@echo "Make for $(OS)-$(ARCH)"

.PHONY: clean echoarch

#===-- tools/klee/Makefile ---------------------------------*- Makefile -*--===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

LEVEL=../..
TOOLNAME = kmc-replay
STATIC_FLAGS=
STATIC_LIBS=
ifdef STATIC_BUILD
TOOLNAME = kmc-replay-static
STATIC_FLAGS=-Wl,-no-export-dynamic -static -v 
STATIC_LIBS=-ldl -lpthread -lrt -lcrypto -lz
endif

USEDLIBS=kleeModule.a	\
	kleaverSolver.a kleaverExpr.a \
	kleeSupport.a kleeBasic.a
#X_LINK_COMPONENTS = all
X_LINK_COMPONENTS = mcjit bitreader bitwriter ipo linker engine core irreader
include $(LEVEL)/Makefile.common

ifdef STATIC_BUILD
bleh=-Wl,-export-dynamic
CFLAGS := $(filter-out $(bleh),$(CFLAGS))
CXXFLAGS := $(filter-out $(bleh),$(CXXFLAGS))
endif

CFLAGS += $(VEXLLVM_CFLAGS)
CXXFLAGS += $(VEXLLVM_CFLAGS)
RDYNAMIC=
LDFLAGS += -Wl,-Ttext-segment=0x70000000 $(STATIC_FLAGS)
LIBS += $(VEXLLVM_LIBS)
LIBS += -lrt $(SOLVERLIBS)

#-ltcmalloc

#===-- tools/klee/Makefile ---------------------------------*- Makefile -*--===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

LEVEL=../..

TOOLNAME = klee-mc
STATIC_FLAGS=
STATIC_LIBS=
ifdef STATIC_BUILD
TOOLNAME = klee-mc-static
STATIC_FLAGS=-Wl,-no-export-dynamic -static -v 
STATIC_LIBS=-ldl -lpthread -lrt -lcrypto -lz
endif

X_LINK_COMPONENTS = mcjit bitreader bitwriter ipo linker engine core irreader
USEDLIBS=kleeSearcher.a \
	kleeSkins.a \
	kleeCore.a kleeModule.a	\
	kleaverSolver.a kleaverExpr.a \
	kleeSupport.a kleeBasic.a
include $(LEVEL)/Makefile.common

ifeq ($(ENABLE_STPLOG), 1)
	LIBS += -lstplog
endif

bleh=-Wl,-export-dynamic
CFLAGS := $(filter-out $(bleh),$(CFLAGS))
CXXFLAGS := $(filter-out $(bleh),$(CXXFLAGS))

CFLAGS += $(VEXLLVM_CFLAGS)
#ACK; OCAML HAS HUGE BSS-- we have to link a bit higher up
CXXFLAGS += $(VEXLLVM_CFLAGS) -DGIT_COMMIT=\"$(shell git log | head -n1 | cut -f2 -d' ')\"
RDYNAMIC=
LDFLAGS += -Wl,-Ttext-segment=0x50000000 $(STATIC_FLAGS)
LIBS += $(SOLVERLIBS)
LIBS += -lrt
LIBS += $(VEXLLVM_LIBS)
LIBS += $(STATIC_LIBS)
#-ltcmalloc

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
USEDLIBS = kleeCore.a kleeModule.a kleaverSolver.a kleaverExpr.a kleeSupport.a kleeBasic.a kleeDSA.a
LINK_COMPONENTS = jit bitreader bitwriter ipo linker engine core

include $(LEVEL)/Makefile.common

ifeq ($(ENABLE_STPLOG), 1)
	LIBS += -lstplog
endif

CFLAGS += -I/home/chz/src/vex/src/
CXXFLAGS += -I/home/chz/src/vex/src/ -Wl,-Ttext-segment=0xa000000
LIBS += -lstp  -lrt
LIBS += /home/chz/src/vex/bin/vexllvm.a /usr/lib/valgrind/libvex-amd64-linux.a
#-ltcmalloc

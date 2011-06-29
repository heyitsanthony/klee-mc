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
USEDLIBS = kleeCore.a kleeModule.a kleaverSolver.a kleaverExpr.a kleeSupport.a kleeBasic.a kleeDSA.a
LINK_COMPONENTS = all
#LINK_COMPONENTS = jit bitreader bitwriter ipo linker engine core

include $(LEVEL)/Makefile.common

CFLAGS += -I/home/chz/src/vex/src/
CXXFLAGS += -I/home/chz/src/vex/src/ -Wl,-Ttext-segment=0xa000000
LIBS += -lstp -lrt
ObjectsO += /home/chz/src/vex/bin/vexllvm.a /usr/lib/valgrind/libvex-amd64-linux.a
#-ltcmalloc

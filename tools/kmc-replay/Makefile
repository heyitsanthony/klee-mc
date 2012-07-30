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
USEDLIBS=kleeModule.a	\
	kleaverSolver.a kleaverExpr.a \
	kleeSupport.a kleeBasic.a kleeDSA.a
#X_LINK_COMPONENTS = all
X_LINK_COMPONENTS = jit bitreader bitwriter ipo linker engine core
include $(LEVEL)/Makefile.common

CFLAGS += -I$(VEXLLVM_ROOT)/src/
CXXFLAGS += -I$(VEXLLVM_ROOT)/src/
LDFLAGS += -Wl,-Ttext-segment=0x70000000
LIBS += -lrt $(SOLVERLIBS)
LIBS += $(VEXLLVM_ROOT)/bin/vexllvm.a $(VEXLIB_ROOT)/libvex-amd64-linux.a
LIBS += $(shell llvm-config --libfiles $(X_LINK_COMPONENTS))

#-ltcmalloc

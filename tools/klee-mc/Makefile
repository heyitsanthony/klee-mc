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
X_LINK_COMPONENTS = jit bitreader bitwriter ipo linker engine core

include $(LEVEL)/Makefile.common

ifeq ($(ENABLE_STPLOG), 1)
	LIBS += -lstplog
endif

CFLAGS += -I$(VEXLLVM_ROOT)/src/
#ACK; OCAML HAS HUGE BSS-- we have to link a bit higher up
CXXFLAGS += -I$(VEXLLVM_ROOT)/src/ 
LDFLAGS += -Wl,-Ttext-segment=0x60000000
LIBS += $(SOLVERLIBS)
LIBS += -lrt
LIBS += $(VEXLLVM_ROOT)/bin/vexllvm.a $(VEXLIB_ROOT)/libvex-amd64-linux.a
LIBS += $(shell llvm-config --libfiles $(X_LINK_COMPONENTS))
#-ltcmalloc

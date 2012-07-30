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
X_LINK_COMPONENTS = jit bitreader bitwriter ipo linker engine core
USEDLIBS=kleeSearcher.a kleeCore.a kleeModule.a	\
	kleaverSolver.a kleaverExpr.a \
	kleeSupport.a kleeBasic.a
include $(LEVEL)/Makefile.common

ifeq ($(ENABLE_STPLOG), 1)
	LIBS += -lstplog
endif

CFLAGS += -I$(VEXLLVM_ROOT)/src/
#ACK; OCAML HAS HUGE BSS-- we have to link a bit higher up
CXXFLAGS += -I$(VEXLLVM_ROOT)/src/  -DGIT_COMMIT=\"$(shell git log | head -n1 | cut -f2 -d' ')\"
LDFLAGS += -Wl,-Ttext-segment=0x50000000
LIBS += $(SOLVERLIBS)
LIBS += -lrt
LIBS += $(VEXLLVM_ROOT)/bin/vexllvm.a $(VEXLIB_ROOT)/libvex-amd64-linux.a
LIBS += $(shell llvm-config --libfiles $(X_LINK_COMPONENTS))
#-ltcmalloc

#===-- tools/kcrumb/Makefile ------------------------------*- Makefile -*--===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

LEVEL=../..
TOOLNAME = kcrumb-replay

USEDLIBS = kleeModule.a	\
	kleaverSolver.a kleaverExpr.a \
	kleeSupport.a kleeBasic.a

include $(LEVEL)/Makefile.common

#X_LINK_COMPONENTS = jit bitreader bitwriter ipo linker engine core
CFLAGS += $(VEXLLVM_CFLAGS)
CXXFLAGS += $(VEXLLVM_CFLAGS)
LIBS += -lrt $(SOLVERLIBS)
LIBS += $(VEXLLVM_LIBS)

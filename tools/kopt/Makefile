#===-- tools/kopt/Makefile ------------------------------*- Makefile -*--===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

LEVEL=../..
TOOLNAME = kopt
# FIXME: Ideally we wouldn't have any LLVM dependencies here, which
# means kicking out klee's Support.
USEDLIBS = kleaverSolver.a kleaverExpr.a kleeSupport.a kleeBasic.a
X_LINK_COMPONENTS = support core

include $(LEVEL)/Makefile.common

LIBS += $(SOLVERLIBS)
LIBS += $(shell llvm-config --libfiles $(X_LINK_COMPONENTS))

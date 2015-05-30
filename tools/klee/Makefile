#===-- tools/klee/Makefile ---------------------------------*- Makefile -*--===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

LEVEL=../..
TOOLNAME = klee
X_LINK_COMPONENTS = mcjit bitreader bitwriter ipo linker engine irreader
USEDLIBS= kleeSearcher.a		\
	kleeSkins.a			\
	kleeCore.a			\
	kleeModule.a			\
	kleaverSolver.a kleaverExpr.a	\
	kleeSupport.a kleeBasic.a
include $(LEVEL)/Makefile.common

ifeq ($(ENABLE_STPLOG), 1)
	LIBS += -lstplog
endif

LIBS += $(SOLVERLIBS) -lrt
LIBS += $(shell llvm-config --libfiles $(X_LINK_COMPONENTS))
# XXX this is kind of a stupid hack to get linking to work after
# splitting out the searcher code
ObjectsO += $(LibDir)/libkleeCore.a
#-ltcmalloc

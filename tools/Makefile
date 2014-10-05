#===-- tools/Makefile --------------------------------------*- Makefile -*--===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

#
# Relative path to the top of the source tree.
#
LEVEL=..

#
# List all of the subdirectories that we will compile.
#
PARALLEL_DIRS= 	\
	kopt klee-mc kmc-replay kcrumb kcrumb-replay klee-hw \
	mk_ktest gen-random-bout ktest-tool	\
	klee kleaver klee-stats solverd

#	kleej


include $(LEVEL)/Makefile.config

ifeq ($(ENABLE_POSIX_RUNTIME),1)
PARALLEL_DIRS += klee-replay
endif

include $(LEVEL)/Makefile.common

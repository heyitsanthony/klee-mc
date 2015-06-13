#===-- runtime/Intrinsic/Makefile --------------------------*- Makefile -*--===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

LEVEL=../..

RUNTIME_ENABLE_OPTIMIZED=1
ENABLE_OPTIMIZED=1
LIBRARYNAME=earlyexits
BUILD_ARCHIVE=1
BYTECODE_LIBRARY=1
# Don't strip debug info from the module.
DEBUG_RUNTIME=0

LIBBCA:=$(LibDir)/lib$(LIBRARYNAME).bca

include $(LEVEL)/Makefile.common

all ::  $(LibDir)/$(LIBRARYNAME).bc

$(LibDir)/$(LIBRARYNAME).bc: $(LibDir)/lib$(LIBRARYNAME).bca
	llvm-link -f -o $@  $(BuildMode)/*.bc

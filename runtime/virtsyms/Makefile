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
LIBRARYNAME=virtsyms
BUILD_ARCHIVE=1
BYTECODE_LIBRARY=1
# Don't strip debug info from the module.
DEBUG_RUNTIME=0

include $(LEVEL)/Makefile.common

LIBBCA:=$(LibDir)/lib$(LIBRARYNAME).bca
LIBTXT:=$(LibDir)/$(LIBRARYNAME)-fns.txt

all :: $(LibDir)/libvirtsyms.bc

$(LibDir)/libvirtsyms.bc: $(LibDir)/libvirtsyms.bca
	../../scripts/link-bca $^ $@

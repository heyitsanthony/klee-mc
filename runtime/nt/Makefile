#===-- runtime/POSIX/Makefile ------------------------------*- Makefile -*--===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

LEVEL=../..

DISABLE_ASSERTIONS=1
LIBRARYNAME=kleeRuntimeMC-nt32
DONT_BUILD_RELINKED=1
BUILD_ARCHIVE=1
BYTECODE_LIBRARY=1
# Don't strip debug info from the module.
DEBUG_RUNTIME=1
NO_PEDANTIC=1
C.Flags += -I/usr/include/wine/windows -Wno-attributes -D__builtin_ms_va_list=__builtin_va_list

include $(LEVEL)/Makefile.common

LIBBCA:=$(LibDir)/lib$(LIBRARYNAME).bca
LIBTXT:=$(LibDir)/$(LIBRARYNAME)-fns.txt

all :: $(LibDir)/lib$(LIBRARYNAME).bc

$(LibDir)/lib$(LIBRARYNAME).bc: $(LibDir)/lib$(LIBRARYNAME).bca
	../../scripts/link-bca $^ $@
#	mkdir -p mmu_tmp
#	cd mmu_tmp && ar x $^ && cd ..
#	llvm-link -f -o $@ mmu_tmp/*.bc
#	arm -rf mmu_tmp

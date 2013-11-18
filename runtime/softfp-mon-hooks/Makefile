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
LIBRARYNAME=softfp-mon-hooks
BUILD_ARCHIVE=1
BYTECODE_LIBRARY=1
# Don't strip debug info from the module.
DEBUG_RUNTIME=0

include $(LEVEL)/Makefile.common

all ::  $(LibDir)/softfp-mon-hooks.bc

$(LibDir)/softfp-mon-hooks.bc: $(LibDir)/libsoftfp-mon-hooks.bca
	mkdir -p softfp-mon-hooks_tmp
	cd softfp-mon-hooks_tmp && ar x $^ && cd ..
	llvm-link -f -o $@ softfp-mon-hooks_tmp/*.bc
	rm -rf softfp-mon-hooks_tmp

#!/bin/bash

LLVMDIR=${LLVMDIR:-"/home/chz/src/llvm/llvm-2.6/"}
STPDIR=${STPDIR:-"/home/chz/src/stp-fast/stp/"}
BOOLECTORDIR=${BOOLECTORDIR:-"/home/chz/src/boolector/"}
Z3DIR=${Z3DIR:-"/home/chz/src/z3/"}
UCLIBDIR=${UCLIBDIR:-"/home/chz/src/klee-2.6-uclibc"}

CFLAGS="-g -O3 -I${STPDIR}/include"	\
	./configure			\
		--with-llvm="$LLVMDIR"	\
		--with-stp="$STPDIR"	\
		--with-boolector="$BOOLECTORDIR"	\
		--with-z3="$Z3DIR"			\
		--enable-posix-runtime			\
		--with-uclibc="$UCLIBDIR"		\
		--with-runtime=Release 

make -j6 REQUIRES_RTTI=1
make mc
if [ -z "$VEXLLVM_HELPER_PATH" ]; then
	echo "Can't find vex bitcode path. Not copying libkleeRuntimeMC.bc"
else
	cp Release/lib/libkleeRuntimeMC.bc "$VEXLLVM_HELPER_PATH"/
	llvm-ld Release/lib/libkleeRuntimeMC.bc runtime/klee-libc/Release/*.bc -o ../vexllvm/bitcode/libkleeRuntimeMC.bc -link-as-library
fi

#!/bin/bash

VEXLLVMDIR=${VEXLLVMDIR:-"/home/chz/src/vex/"}
LLVMDIR=${LLVMDIR:-"/home/chz/src/llvm/llvm-3.0/"}
STPDIR=${STPDIR:-"/home/chz/src/stp-fast/stp/"}
BOOLECTORDIR=${BOOLECTORDIR:-"/home/chz/src/boolector/"}
Z3DIR=${Z3DIR:-"/home/chz/src/z3/"}
UCLIBDIR=${UCLIBDIR:-"/home/chz/src/uclibc-pruning"}
VEXLIBDIR=${VEXLIBDIR:-"/usr/lib/valgrind/"}

LLVM_CFLAGS_EXTRA="$EXTRAHEADERS"			\
LLVM_CXXFLAGS_EXTRA="$EXTRAHEADERS"			\
CFLAGS="-g -O3 -I${STPDIR}/include $EXTRAHEADERS"	\
CXXFLAGS="-g -O2  $EXTRAHEADERS"		\
	./configure				\
		--with-llvm="$LLVMDIR"		\
		--with-libvex="$VEXLIBDIR"	\
		--with-vexllvm="$VEXLLVMDIR"	\
		--with-stp="$STPDIR"	\
		--with-boolector="$BOOLECTORDIR"	\
		--with-z3="$Z3DIR"			\
		--enable-posix-runtime			\
		--with-uclibc="$UCLIBDIR"		\
		--with-runtime=Release 

make -j6 REQUIRES_RTTI=1
make mc-clean && make && make mc-std-amd64

BASEDIR="Release+Asserts"
if [ ! -x $BASEDIR ]; then
	BASEDIR="Release"
fi

if [ -z "$VEXLLVM_HELPER_PATH" ]; then
	echo "Can't find vex bitcode path. Not copying libkleeRuntimeMC.bc"
else
	cp "$BASEDIR"/lib/libkleeRuntimeMC-amd64.bc "$VEXLLVM_HELPER_PATH"/
	cp "$BASEDIR"/lib/libkleeRuntimeMC-fdt.bc "$VEXLLVM_HELPER_PATH"/
fi

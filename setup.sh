#!/bin/bash

LLVMDIR=/home/chz/src/llvm/llvm-2.6/
#STPDIR=/home/chz/src/stp_inst/
STPDIR=/home/chz/src/stp-fast/stp/
UCLIBDIR=/home/chz/src/klee-2.6-uclibc

CFLAGS="-g -O3 -I${STPDIR}/include"	\
	./configure			\
		--with-llvm="$LLVMDIR"	\
		--with-stp="$STPDIR"	\
		--enable-posix-runtime	\
		--with-uclibc="$UCLIBDIR" \
		--with-runtime=Release 

make -j6 REQUIRES_RTTI=1
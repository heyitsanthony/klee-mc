#!/bin/bash

#	-use-hookpass				\
#	-psdb-dir=/home/chz/klee-trunk/psdb	\
#	-hookpass-list=`pwd`/hookpass.txt \
#

KLEE_FLAGS="			\
	-use-hookpass		\
	-hookpass-lib=nested.bc	\
	-use-hash-solver=true	\
	-use-softfp		\
	-hcache-fdir=`pwd`/hcache       \
	-mm-type=deterministic		\
	-hcache-pending=`pwd`/hcache    \
	-hcache-sink                    \
	-hcache-dir=`pwd`/hcache        \
	-use-hwaccel=true		\
	-use-cache=false		\
	-use-cex-cache=false		\
	-max-stp-time=10		\
	-pipe-fork-queries		\
	-concretize-early=true		\
	-use-fresh-branch-search	\
	-dump-br-data=10		\
	-use-pdf-interleave		\
	-use-interleaved-RR		\
	-use-interleaved-UO		\
	-use-interleaved-MXI		\
	-dump-select-stack			\
	-use-second-chance -use-batching-search -batch-time=10	\
	-show-syscalls -print-new-ranges -guest-type=sshot -concrete-vfs"


function knest_mk_sshot
{
	rm -rf guest-*
	rm -f p.$ext
	mkfifo p.$ext
	$binpath $trampoline p.$ext $args &
	pid=$!
	echo $pid
	sleep 2s
	VEXLLVM_ATTACH="$!" VEXLLVM_SAVE=1 pt_run $binpath $trampoline  p.$ext $args
	rm p.$ext
	kill -9 "$!"
}

function knest_launch
{
	ext="$1"
	binname="$2"
	scriptname="$3"
	args="$4"

	if [ -z "$SS_REUSE" ]; then
		binpath=`which "$binname"`
		trampoline="$5"
		echo binpath= $binpath
		knest_mk_sshot
	fi

	echo "copying over $scriptname"
	cp $scriptname p.$ext

	ls -l p.$ext
	echo `pwd`

	#gdb --args 
	klee-mc $KLEE_FLAGS - 2>err
	KMC_CONCRETE_VFS=1 kmc-replay 1
}

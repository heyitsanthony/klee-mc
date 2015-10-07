#!/bin/bash

if [ -z "$1" ]; then
	echo "Wanted command line"
	exit -1
fi

echo "COMMAND = $1"

if [ -e "$1" ]; then
	is_gzip=`file "$1" | grep gzip`
	if [ ! -z "$is_gzip" ]; then
		USE_LAST=1
		rm -rf g.tmp
		mkdir g.tmp
		tar zxvf "$1"  --directory="g.tmp" 
		gl=`find  g.tmp -name guest-\* | head -n1`
		if [ ! -z "$gl" ]; then
			rm -f guest-last
			ln -s "$gl" guest-last
		fi
	fi
fi

#PRELOAD_STR=`pwd`/preload/string.so:`pwd`/preload/printf.so
#PRELOAD_STR=`pwd`/preload/dumb_string.so:`pwd`/preload/page_malloc.so
#PRELOAD_STR=`pwd`/preload/page_malloc.so
#PRELOAD_STR=`pwd`/preload/dumb_string.so:`pwd`/preload/dumb_file.so:`pwd`/preload/crc32.so
#PRELOAD_STR=`pwd`/preload/string.so:`pwd`/preload/dumb_file.so:`pwd`/preload/crc32.so
#PRELOAD_STR=`pwd`/preload/dumb_file.so
#:`pwd`/preload/sdl.so
TIMEOUT=${TIMEOUT:-2}
#PRELOAD_STR=""
#PRELOAD_STR=/home/chz/src/research/klee-open/trunk/preload/string.so
#LD_PRELOAD=preload/printf.so $1 &

GUEST_FLAGS="	-guest-type=sshot"

if [ ! -z "$USE_CHKPT" ]; then
	GUEST_FLAGS="-guest-type=sseq -guest-sseq=chkpt"
elif [ ! -z "$FROM_BEGINNING" ]; then
	echo taking snapshot
#	PRELOAD_STR=/home/chz/src/research/klee-open/trunk/preload/string.so
#	PRELOAD_STR=/home/chz/src/research/klee-open/trunk/preload/dumb_file.so
	VEXLLVM_PRELOAD="$PRELOAD_STR" VEXLLVM_SAVE=1 pt_run $1
elif [ -z "$USE_LAST" ]; then
	LD_BIND_NOW=1 LD_PRELOAD="$PRELOAD_STR" $1 &
	childpid=$!
	echo Sleep for a second, waiting for pipe
	sleep $TIMEOUT
	echo PID is $childpid
	VEXLLVM_SAVE=1 VEXLLVM_ATTACH=$childpid pt_run x
	kill -9 $childpid
fi

if [ ! -z "$REPLAYPATH" ]; then
	REPLAYARG="-replay-path=$REPLAYPATH"
else
	REPLAYARG=""
fi

#	-use-markov-search	\
#	-use-pdf-interleave	\
#	-use-interleaved-TRS	\
#	-use-interleaved-BS	\
#	-use-interleaved-TS	\
#	-use-demotion-search	\
#	-use-interleaved-TS	\
#	-use-interleaved-BS	\
#	-use-pdf-interleave	\
#	-use-interleaved-TRS	\


# -use-interleaved-MI	\



p=`dirname $0`
if [ ! -z "$p" ]; then p="$p/"; fi
source "$p"defaults.sh

cmd="$APP_WRAPPER klee-mc 	\
	$EXTRA_ARGS		\
	$PSDBFLAGS		\
	$REPLAYARG		\
	$SCHEDOPTS		\
	$RULEFLAGS		\
	$MMUFLAGS		\
	$GUEST_FLAGS		\
	$HCACHE_FLAGS		\
	$STATOPTS		\
	$SOLVEROPTS		\
	\
	-use-search-filter=false \
	-print-new-ranges	\
	-contig-off-resolution 	\
	-write-paths		\
	$MAX_MEMORY		\
	-mm-type=deterministic	\
	-equivdb-dir=/mnt/biggy/klee/equivdb \
	-use-pid		\
	-max-err-resolves=32	\
	-max-stp-time=8		\
	-randomize-fork		\
	-concretize-early=true	\
	-use-softfp=true	\
	-show-syscalls		\
	-write-new-cov		\
	- "

echo "$cmd" >last_cmd
$cmd 2>err

#	-ctrl-graph		\
#	-use-symhooks		\
#	-use-interleaved-BS	\
#	-use-interleaved-RS	\
#	-pr-kick-rate=4		\
#	-use-non-uniform-random-search	\
#	-weight-type=markov	\
#	-priority-search	\
#	-use-reg-pr		\
#	-use-pdf-interleave	\
#	-use-interleaved-BS	
#	-use-interleaved-RS	\
#	-use-interleaved-TS	\
#	-use-interleaved-MV	\

#	-use-interleaved-RS	\
#	-use-random-search	\
#	-use-non-uniform-random-search	\
#	-weight-type=markov	\
#	-priority-search	\
#	-use-reg-pr		\
#	-use-pdf-interleave	\
#	-use-interleaved-RS	\
#	-use-interleaved-TS	\
#	-use-interleaved-RS	\


#!/bin/bash

if [ -z "$1" ]; then
	echo "Wanted command line"
	exit -1
fi

echo "COMMAND = $1"

if [ -e "$1" ]; then
	USE_LAST=1
	rm -rf g.tmp
	mkdir g.tmp
	tar zxvf "$1"  --directory="g.tmp" 
	gl=`find  g.tmp -name guest-\* | head -n1`
	rm guest-last
	ln -s "$gl" guest-last
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
if [ ! -z "$FROM_BEGINNING" ]; then
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


# -equiv-expr-builder
# -replace-equiv
# -queue-solver-equiv
# -write-equiv-rules

# -use-interleaved-MI	\




RULEFLAGS="-use-rule-builder -rule-file=default_rules.db -dump-used-rules=used.db"

# -use-rule-builder=true \


#	-batch-instructions=20000
SCHEDOPTS="-use-batching-search
	-batch-time=5
	-use-second-chance=true
	-second-chance-boost=1
	-second-chance-boost-cov=1
	-use-pdf-interleave=true
	-use-interleaved-MI=true
	-use-interleaved-FTR=false
	-use-interleaved-BI=false
	-use-interleaved-UNC=false

	-use-interleaved-NI=true
	-use-interleaved-CD=true
	-use-interleaved-MXI=true
	-use-fresh-branch-search=true"

if [ -z "$APP_WRAPPER" ]; then
APP_WRAPPER="gdb --args"
fi

#-allow-negstack	

if [ ! -z "$USE_MMU" ]; then
	MMUFLAGS=" -sym-mmu-type=memcheck -sconc-mmu-type=memcheckc -use-sym-mmu -use-hookpass -hookpass-lib=libkleeRuntimeMMU.bc "
fi


#	-deny-sys-files 	\
#

cmd="$APP_WRAPPER klee-mc 		\
	$EXTRA_ARGS		\
	$REPLAYARG		\
	$SCHEDOPTS		\
	$RULEFLAGS		\
	$MMUFLAGS		\
	\
	-hcache-fdir=`pwd`/hcache	\
	-hcache-pending=`pwd`/hcache	\
	-hcache-sink			\
	-hcache-dir=`pwd`/hcache 	\
	-use-hash-solver=true		\
	\
	-use-search-filter=false \
	-use-cache=false	\
	-use-cex-cache=false	\
	-pipe-fork-queries	\
	-smt-let-arrays=true	\
	-print-new-ranges	\
	-contig-off-resolution 	\
	-pipe-solver		\
	-write-paths		\
	-max-memory=1024	\
	-mm-type=deterministic	\
	-equivdb-dir=/mnt/biggy/klee/equivdb \
	-use-pid		\
	-max-err-resolves=32	\
	-max-stp-time=8		\
	-randomize-fork		\
	-concretize-early	\
	-use-softfp=true	\
	-guest-type=sshot	\
	-write-smt=false	\
	-show-syscalls		\
	-dump-select-stack	\
	-dump-covstats=1	\
	-dump-rbstats=1		\
	-dump-statestats=1	\
	-dump-memstats=1	\
	-dump-exprstats=1	\
	-dump-hashstats=1	\
	-dump-querystats=1	\
	-dump-stateinststats=10	\
	-dump-br-data=5		\
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


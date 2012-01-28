#!/bin/bash

if [ -z "$1" ]; then
	echo "Wanted command line"
	exit -1
fi

#LD_PRELOAD=preload/string.so:preload/printf.so $1 &
LD_PRELOAD=preload/printf.so $1 &
childpid=$!
echo Sleep for a second, waiting for pipe
sleep 2
VEXLLVM_SAVE=1 VEXLLVM_ATTACH=$childpid pt_run x
kill -9 $childpid

klee-mc 			\
	-pipe-fork-queries	\
	-smt-let-arrays=true	\
	-print-new-ranges	\
	-use-search-filter	\
	-contig-off-resolution 	\
	-pipe-solver		\
	-use-markov-search	\
	-use-pdf-interleave	\
	-use-interleaved-TRS	\
	-use-interleaved-BS	\
	-allow-negstack		\
	-deny-sys-files 	\
	-max-memory=1024	\
	-mm-type=deterministic	\
	-use-pid		\
	-max-stp-time=8		\
	-batch-instructions=99999999	\
	-batch-time=4		\
	-use-batching-search	\
	-guest-type=sshot	\
	-dump-covstats=1	\
	-dump-select-stack	\
	-dump-statestats=1	\
	-write-smt		\
	-dump-memstats=1	\
	-dump-exprstats=1	\
	-  2>err

#	-use-symhooks		\
#	-use-pdf-interleave	\
#	-use-interleaved-BS	\
#	-use-interleaved-RS	\
#	-pr-kick-rate=4		\
#	-use-non-uniform-random-search	\
#	-weight-type=markov	\
#	-priority-search	\
#	-use-reg-pr		\
#	-use-pdf-interleave	\
#	-use-interleaved-BS	\
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


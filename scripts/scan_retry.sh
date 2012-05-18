#!/bin/bash

killall -9 klee-mc

#	-gc-timer=0

#	-use-batching-search	\
#	-batch-instructions=99999999	\
#	-batch-time=5		\
#	-use-second-chance=false	\
#	-second-chance-boost=2		\
#	-randomize-fork=true		\
#	-branch-hint=false		\
#	-use-pdf-interleave=false \
#	-use-interleaved-MXI=false \
#	-use-interleaved-MI=false \
#	-use-interleaved-FTR=false	\
#	-use-interleaved-CD=false	\
#	-use-interleaved-fb=false	\
#	-use-random-search=true		\
#	-use-cond-search=false 	\

#	-use-batching-search	\
#	-batch-instructions=99999999	\
#	-batch-time=5		\
#	-use-second-chance=true \
#	-second-chance-boost=2		\
#	-randomize-fork=true		\
#	-branch-hint=true \
#	-use-pdf-interleave=true \
#	-use-interleaved-MXI=true \
#	-use-interleaved-MI=true \
#	-use-interleaved-FTR=false	\
#	-use-interleaved-CD=false	\
#	-use-interleaved-fb=true \
#	-use-cond-search=true \
#	\

#	-use-batching-search	\
#	-batch-instructions=99999999	\
#	-batch-time=5			\
#	-use-second-chance=true		\
#	-second-chance-boost=2		\
#	-randomize-fork=true		\
#	-branch-hint=true		\
#	-use-pdf-interleave=false	\
#	-use-interleaved-MXI=false	\
#	-use-interleaved-MI=false	\
#	-use-interleaved-FTR=false	\
#	-use-interleaved-CD=false	\
#	-use-interleaved-fb=false	\
#	-use-random-search=true		\
#	-use-cond-search=false 	\



klee-mc					\
	$EXTRA_ARGS			\
	-use-gdb			\
	-use-search-filter=false	\
	-use-cache=false	\
	-use-cex-cache=false	\
	-deny-sys-files 	\
	-pipe-fork-queries	\
	-smt-let-arrays=true	\
	-print-new-ranges	\
	-contig-off-resolution 	\
	-pipe-solver		\
	-allow-negstack		\
	-write-paths		\
	-max-memory=1024	\
	-mm-type=deterministic	\
	-use-pid		\
	-max-err-resolves=32	\
	-max-stp-time=5		\
	-concretize-early	\
	\
	-use-batching-search	\
	-batch-instructions=99999999	\
	-batch-time=5		\
	-use-second-chance=true \
	-second-chance-boost=1		\
	-second-chance-boost-cov=1	\
	-randomize-fork=true		\
	-branch-hint=true \
	-use-pdf-interleave=true \
	-use-interleaved-MXI=true \
	-use-interleaved-MI=false \
	-use-interleaved-FTR=false	\
	-use-interleaved-CD=false	\
	-use-interleaved-fb=true \
	-use-cond-search=true \
	\
	-use-softfp		\
	-guest-type=sshot	\
	-write-smt		\
	-dump-select-stack	\
	-dump-covstats=1	\
	-dump-rbstats=1		\
	-dump-statestats=1	\
	-dump-memstats=1	\
	-dump-exprstats=1	\
	-dump-cachestats=1	\
	-dump-querystats=1	\
	-dump-stateinststats=10	\
	-dump-br-data=5		\
	-  2>err &

# sleep for a few seconds so gdb target comes up
sleep 2

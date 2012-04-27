#!/bin/bash

killall -9 klee-mc
#	-use-cache=false
#	-use-cex-cache=false	
klee-mc					\
	-use-gdb			\
	-use-search-filter=false	\
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
	\
	-use-batching-search	\
	-batch-instructions=99999999	\
	-batch-time=5		\
	-use-second-chance	\
	-randomize-fork		\
	-concretize-early	\
	-second-chance-boost=2	\
	-use-pdf-interleave=true \
	-use-interleaved-MXI=true	\
	-use-interleaved-MI=true	\
	-use-interleaved-FTR=false	\
	-use-interleaved-CD=false	\
	-use-interleaved-fb=true	\
	-use-cond-search 	\
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

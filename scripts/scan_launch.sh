#!/bin/bash

attach_pid=`sed "s/ /\n/g" gdb.threads | grep -A1 LWP | tail -n1 | sed "s/[^0-9].*//g"`
VEXLLVM_SAVE=1 VEXLLVM_NOSYSCALL=1 VEXLLVM_ATTACH=$attach_pid pt_run none
ret=$?
echo $ret

klee-mc			\
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
	-second-chance-boost=2	\
	-use-pdf-interleave=false	\
	-use-interleaved-MXI=false	\
	-use-interleaved-MI=false	\
	-use-interleaved-FTR=false	\
	-use-interleaved-CD=false	\
	-use-interleaved-fb=false	\
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

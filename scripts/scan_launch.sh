#!/bin/bash

# grab selected line, if any
#target_line=`grep "^*" gdb.threads`
#if [ -z "$target_line" ]; then
	# none found? just get last listed thread
	target_line=`tail -n1 gdb.threads`
#fi

attach_pid=`echo "$target_line" | sed "s/ /\n/g" | grep -A1 LWP | tail -n1 | sed "s/[^0-9].*//g"`
if [ -z "$attach_pid" ]; then
	attach_pid=`sed "s/ /\n/g" gdb.threads | grep -A1 process | tail -n1 | sed "s/[^0-9].*//g"`
fi
echo "ATTACH_PID=" $attach_pid

if [ ! -d /proc/$attach_pid/ ]; then
	echo Could not get pid $attach_pid
	exit 1
fi

if [ -z "$GDB_SCAN_BLOCKED" ]; then
	VEXLLVM_SAVE=1		\
	VEXLLVM_NOSYSCALL=1 	\
	VEXLLVM_ATTACH=$attach_pid pt_run none
else
	echo "SAVING BLOCKED SNAPSHOT."
	echo "ATTACHPID=" $attach_pid
	VEXLLVM_SAVE=1	\
	VEXLLVM_ATTACH=$attach_pid pt_run none
fi

ret=$?
echo $ret
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

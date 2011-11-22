#!/bin/bash

# NOTE: Requires PATH to have VEXLLVM and KLEE!
if [ -z "$KMC_RUN_OUTPUTPATH" ]; then
	KMC_RUN_OUTPUTPATH="bintests/out/"
fi

if [ -z "$KMC_RUN_KLEEFLAGS" ]; then
#	KMC_RUN_KLEEFLAGS="--guest-type=sshot --use-pcache-rewriteptr -pcache-dir=../../../"
	KMC_RUN_KLEEFLAGS="-guest-type=sshot -pipe-solver -use-symhooks"
fi

if [ -z "$KMC_RUN_TIMEOUT" ]; then
	KMC_RUN_TIMEOUT="120"
fi

if [ -z "$KMC_MEMLIMIT" ]; then
	# 2GB of memory permitted
	KMC_MEMLIMIT="2097152"
fi

if [ -z "$KMC_STACKLIMIT" ]; then
	KMC_STACKLIMIT=131072
fi

trap "exit 0" 1 2 3 6 11 

function run_line
{
	line="$1"

	line_md5=`echo $line | md5sum | cut -f1 -d' '`
	mkdir -p "$KMC_RUN_OUTPUTPATH"/$line_md5
	cd "$KMC_RUN_OUTPUTPATH"/$line_md5

	echo Doing command... "$line"
	echo Command dir... "$KMC_RUN_OUTPUTPATH"/$line_md5


	# save guest so we can do replays
	if [ ! -x guest-last ]; then
		VEXLLVM_SAVE=1 pt_run $line
		echo Saved snapshot "$line"
		ls -l guest-last
	fi

	# Hard limits:
	# 128 MB stacks for STP
	ulimit -s "$KMC_STACKLIMIT"
	ulimit -v "$KMC_MEMLIMIT"

	echo "$line" >line
	klee-mc $KMC_RUN_KLEEFLAGS - $line >stdout 2>stderr &
	kmcpid="$!"

	export TIMEOUT="$KMC_RUN_TIMEOUT"
	( sleep $TIMEOUT && echo "KILL $kmcpid" && kill -s SIGUSR1 $kmcpid >/dev/null 2>&1 && echo "TIMEDOUT">>timeout.txt && echo "KABOOM. " && sleep 30 && kill -9 $kmcpid ) &
	timeoutpid="$!"

	ps
	echo "KMCPID=$kmcpid"
	echo "TIMEOUTPID=$timeoutpid"
	wait "$kmcpid" && echo "Done."

	kill -9 $timeoutpid 2>/dev/null
	kill -9 $kmcpid 2>/dev/null

	echo Byebye.

	cd ../../..
}

if [ ! -z "$KMC_CMDLINE" ]; then
	while read line
	do
		run_line "$line"
	done <<<"$KMC_CMDLINE"
	exit 0
fi


# TODO
# 1. Hard limit memory use with ulimit.
# 2. Soft limit memory use with internal klee stuff
# 3. Set a timeout on execution
while read line
do
	run_line "$line"
done < "bintests/bt.txt"

bintests/process.sh
bintests/mkreport.py

#!/bin/bash

# Expects to be run in source root dir

# NOTE: Requires PATH to have VEXLLVM and KLEE!
KMC_SEED_FUNC='/bin/echo'
KMC_RUN_OUTPUTPATH="bintests/func-out/"
KMC_RUN_KLEEFLAGS="			\
	-guest-type=sshot 		\
	-guest-sshot=../guest-last	\
	-unconstrained			\
	-use-random-search		\
	-use-random-path		\
	-print-new-ranges		\
	-symregs			\
	-pipe-solver"
KMC_RUN_TIMEOUT="60"
KMC_MEMLIMIT="1572864" 
KMC_STACKLIMIT=131072
BASEPATH=`pwd`
trap "exit 0" 1 2 3 6 11 
mkdir -p $KMC_RUN_OUTPUTPATH

function run_line
{
	line="$1"

#	line_md5=`echo $line | md5sum | cut -f1 -d' '`

	# Strip spaces, only use head
	line_md5=`echo $line | cut -f1 -d' '`

	mkdir -p "$KMC_RUN_OUTPUTPATH"/$line_md5
	cd "$KMC_RUN_OUTPUTPATH"/$line_md5

	echo Doing function..."$line"
	echo Function dir... "$KMC_RUN_OUTPUTPATH"/$line_md5

	# Hard limits:
	# 128 MB stacks for STP
	ulimit -s "$KMC_STACKLIMIT"
	ulimit -v "$KMC_MEMLIMIT"

	echo "$line" >func
	echo $KMC_RUN_KLEEFLAGS
	klee-mc $KMC_RUN_KLEEFLAGS -uc-func="$line" - >stdout 2>stderr &
	kmcpid="$!"

	export TIMEOUT="$KMC_RUN_TIMEOUT"
	( 	sleep $TIMEOUT && echo "KILL $kmcpid" && \
		kill -s SIGUSR1 $kmcpid >/dev/null 2>&1 && \
		echo "TIMEDOUT">>timeout.txt && echo "KABOOM. " \
		&& sleep 10 && kill -9 $kmcpid ) &
	timeoutpid="$!"

	echo "KMCPID=$kmcpid"
	echo "TIMEOUTPID=$timeoutpid"
	wait "$kmcpid" && echo "Done."

	kill -9 $timeoutpid 2>/dev/null
	kill -9 $kmcpid 2>/dev/null

	echo Byebye.
}

# Setup guest

# save guest so we can do replays
cd "$KMC_RUN_OUTPUTPATH"
KMC_RUN_OUTPUTPATH=`pwd`
if [ ! -x guest-last ]; then
	VEXLLVM_SAVE=1 pt_run "$KMC_SEED_FUNC"
	echo Saved snapshot "$line"
	ls -l guest-last
fi

if [ ! -x "bintests/funcs.txt" ]; then
	echo "Could not find bintests/funcs.txt"
	exit -1
fi

cd "$BASEPATH"
while read curfunc
do
	cd "$BASEPATH"
	run_line "$curfunc"
done < "bintests/funcs.txt"

#bintests/process.sh
#bintests/mkreport.py

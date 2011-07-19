#!/bin/bash

# NOTE: Requires PATH to have VEXLLVM and KLEE!

# TODO
# 1. Hard limit memory use with ulimit.
# 2. Soft limit memory use with internal klee stuff
# 3. Set a timeout on execution

while read line
do
	echo Doing command... $line

	line_md5=`echo $line | md5sum | cut -f1 -d' '`
	mkdir -p bintests/out/$line_md5
	cd bintests/out/$line_md5

	# save guest so we can do replays
	if [ ! -x guest-last ]; then
		VEXLLVM_SAVE=1 pt_run $line
		echo Saved snapshot "$line"
		ls -l guest-last
	fi

	# Hard limits:
	# 128 MB stacks for STP
	ulimit -s 131072
	# 2GB of memory permitted
	ulimit -v 2097152

	echo "$line" >line
	klee-mc --guest-type=sshot - $line >stdout 2>stderr &
	kmcpid="$!"

	export TIMEOUT=120
	( sleep $TIMEOUT && kill -s SIGUSR1 $kmcpid >/dev/null 2>&1 && echo "TIMEDOUT">>timeout.txt && echo "KABOOM. " && sleep $TIMEOUT && kill -9 $kmcpid ) &

	wait $kmcpid && echo "Done."

	cd ../../..
done < "bintests/bt.txt"

bintests/process.sh
bintests/mkreport.py

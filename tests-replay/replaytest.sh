#!/bin/bash
# to be run from the tests-replay directory

# test registers, but this is slow
#KMC_RNR_LOGREGS=1 ../scripts/kmc-run-n-replay /bin/echo
# don't test registers, much faster
../scripts/kmc-run-n-replay /bin/echo

testcount=`ls klee-last/*ktest.gz | wc -l`
for a in `seq $testcount`; do
	echo Replaying test \#$a
	ec=`kmc-replay $a 2>&1 | grep Exitcode`
	if [ -z "$ec" ]; then
		echo "Failed to replay $a to completion"
		exit 1
	fi
done

echo Replays successful.
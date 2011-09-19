#!/bin/bash
# to be run from the tests-replay directory


# test registers, but this is slow
#KMC_RNR_LOGREGS=1 ../scripts/kmc-run-n-replay /bin/echo
# don't test registers, much faster
KMC_RNR_FLAGS="-write-paths" ../scripts/kmc-run-n-replay /bin/echo
KPATHBASE=`readlink klee-last`
for a in $KPATHBASE/*.path*; do
	echo Replaying test by path: $a
	ec=`klee-mc -guest-type=sshot -replay-path=$a  - /bin/echo 2>&1 | grep -i Exitcode`
	if [ -z "$ec" ]; then
		echo "Failed to replay $a to completion"
		exit 1
	fi

	testsfound=`ls klee-last | grep ktest | wc -l `
	if [ "$testsfound" -ne "1" ]; then
		echo Test count mismatch: $testsfound-- but expected one.
		exit 2
	fi
done

echo Replay paths successful.
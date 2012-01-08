#!/bin/bash

#klee-mc -uc-func=getline -use-batching-search -use-random-path -use-random-search -logregs  -unconstrained -pipe-solver -guest-type=sshot  - /bin/echo asd
TESTFUNC=$1
TESTNUM=$2
TESTGUEST=$3

if [ -z "$TESTFUNC" ]; then
	echo "Expected test function"
	exit -1
fi

if [ -z "$TESTNUM" ]; then
	echo "Expected test number"
	exit -2
fi

if [ -z "$TESTGUEST" ]; then
	echo "Expected test guest"
	exit -3
fi

rm uc.base.sav uc.new.sav

UC_SAVE=uc.base.sav	\
UC_FUNC=$TESTFUNC 	\
	kmc-replay $TESTNUM

if [ ! -e uc.base.sav ]; then
	echo "Failed to replay"
	echo "$TESTFUNC $TESTNUM" >>uc.fail
	exit -1
fi

UC_SAVE=uc.new.sav	\
UC_FUNC=$TESTFUNC 	\
XCHK_GUEST=$TESTGUEST	\
	kmc-replay $TESTNUM

echo Diff:
d=`diff uc.base.sav uc.new.sav`
echo $d
if [ ! -z "$d" ]; then
	echo "$TESTFUNC $TESTNUM" >>uc.diffs 
fi

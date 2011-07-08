#!/bin/bash

# Stack traces of potential bugs
for a in bintests/out/*/klee-last/*err; do
	cat bintests/out/`echo $a | cut -f3 -d'/'`/line
	grep Stack -A5 $a;
done >bintests/out/herp.derp

# get runs that timed out
for a in bintests/out/*/timeout.txt; do
	cat bintests/out/`echo $a | cut -f3 -d'/'`/line
done >bintests/out/timeouts.txt

# runs that died from the crappy solver
for a in bintests/out/*/klee-last/warnings.txt; do
	x=`tail -n1 $a | egrep -i "(flushing|ministat)"`
	if [ -z "$x" ]; then
		continue
	fi
	cat bintests/out/`echo $a | cut -f3 -d'/'`/line
done >bintests/out/"badsolver.txt"

for a in bintests/out/*/stderr; do
	x=`tail -n1 $a | grep -i "KLEE: done:"`
	if [ -z "$x" ]; then
		continue
	fi
	cat bintests/out/`echo $a | cut -f3 -d'/'`/line
done >bintests/out/"done.txt"

for a in bintests/out/*/stderr; do
	x=`grep -i "Error" $a`
	if [ -z "$x" ]; then
		continue
	fi
	cat bintests/out/`echo $a | cut -f3 -d'/'`/line
done >bintests/out/"error.txt"

for a in bintests/out/*/stderr; do
	x=`grep -i "UNKNOWN SYSCALL" $a`
	if [ -z "$x" ]; then
		continue
	fi
	cat bintests/out/`echo $a | cut -f3 -d'/'`/line
done >bintests/out/"syscall.txt"

for a in bintests/out/*/stderr; do
	x=`grep -i "assert_fail" $a`
	if [ -z "$x" ]; then
		continue
	fi
	cat bintests/out/`echo $a | cut -f3 -d'/'`/line
done >bintests/out/"abort.txt"

# output this stuff as python format. fuck all
for a in bintests/out/*/klee-last; do
	FDIR=bintests/out/`echo $a | cut -f3 -d'/'`
	klee-stats --print-all --xml $a >$FDIR/last.stats
	cat $FDIR/last.stats
done

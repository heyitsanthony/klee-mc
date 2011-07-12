#!/bin/bash

# Stack traces of potential bugs
ERRXML="bintests/out/err.xml"
echo '<?xml version="1.0" encoding="ISO-8859-1"?>' >$ERRXML
echo '<?xml-stylesheet type="text/xsl" href="err.xsl"?>' >>$ERRXML
echo "<errors>" >>$ERRXML
for a in bintests/out/*/klee-last/*err; do
	base=`echo $a | cut -f3 -d'/'`
	echo "<error>"
	echo "<errfile>"`echo "$a" | sed 's/\//\n/g' | tail -n1`"</errfile>"
	echo -n "<cmdline>"
	cat bintests/out/$base/line
	echo "</cmdline>"
	echo "<frames>"
	grep Stack -A30 $a | grep "^[[:space:]]*#" | awk ' { print "<frame>"$0"</frame>" } '
	echo "</frames>"

	consline=`grep -n "^Constraints" $a | cut -f1 -d':'`
	infoline=`grep -n "^Info" $a | cut -f1 -d':'`
	echo "<constraints>"
	if [ -z "$infoline" ]; then
		infoline=`wc -l "$a" |  cut -f1 -d' '`
	fi
	if [ ! -z "$consline" ] && [ ! -z "$infoline" ]; then
		consline=`expr $consline + 1`
		infoline=`expr $infoline - 1`
		sed -n "$consline,$infoline p" "$a"
	else
		echo "XXX"
	fi
	echo "</constraints>"


	echo "</error>"
done >>$ERRXML
echo "</errors>" >>$ERRXML

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

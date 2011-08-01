#!/bin/bash

# Stack traces of potential bugs
if [ -z "$KMC_RUN_OUTPUTPATH" ]; then
	KMC_RUN_OUTPUTPATH="bintests/out/"
fi

function path2testdir
{
	testmd5=`echo "$1" | sed "s/\//\n/g" | grep "^[0-9a-f][0-9a-f][0-9a-f][0-9a-f]" | tail -n1`
	testdir="$KMC_RUN_OUTPUTPATH"/$testmd5
	echo "$testdir"
}

ERRXML="$KMC_RUN_OUTPUTPATH/err.xml"
echo '<?xml version="1.0" encoding="ISO-8859-1"?>' >$ERRXML
echo '<?xml-stylesheet type="text/xsl" href="err.xsl"?>' >>$ERRXML
echo "<errors>" >>$ERRXML
for a in "$KMC_RUN_OUTPUTPATH"/*/klee-last/*err; do
	testdir=`path2testdir "$a"`
	echo "<error>"
	echo "<errfile>"`echo "$a" | sed 's/\//\n/g' | tail -n1`"</errfile>"
	echo -n "<cmdline>"
	cat "$testdir"/line
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

echo asdasd
# get runs that timed out

for a in "$KMC_RUN_OUTPUTPATH"/*/timeout.txt; do
	cat `path2testdir "$a"`/line 2>/dev/null
done >"$KMC_RUN_OUTPUTPATH"/timeouts.txt

# runs that died from the crappy solver
for a in "$KMC_RUN_OUTPUTPATH"/*/klee-last/warnings.txt; do
	x=`tail -n1 $a | egrep -i "(flushing|ministat)"`
	if [ -z "$x" ]; then
		continue
	fi
	cat `path2testdir "$a"`/line
done >"$KMC_RUN_OUTPUTPATH"/"badsolver.txt"

for a in "$KMC_RUN_OUTPUTPATH"/*/stderr; do
	x=`tail -n1 $a | grep -i "KLEE: done:"`
	if [ -z "$x" ]; then
		continue
	fi
	cat `path2testdir "$a"`/line
done >"$KMC_RUN_OUTPUTPATH"/"done.txt"

for a in "$KMC_RUN_OUTPUTPATH"/*/stderr; do
	x=`grep -i "Error" $a`
	if [ -z "$x" ]; then
		continue
	fi
	cat `path2testdir "$a"`/line
done >"$KMC_RUN_OUTPUTPATH"/"error.txt"

for a in "$KMC_RUN_OUTPUTPATH"/*/stderr; do
	x=`grep -i "UNKNOWN SYSCALL" $a`
	if [ -z "$x" ]; then
		continue
	fi
	cat `path2testdir "$a"`/line
done >"$KMC_RUN_OUTPUTPATH"/"syscall.txt"

for a in "$KMC_RUN_OUTPUTPATH"/*/stderr; do
	x=`grep -i "assert_fail" $a`
	if [ -z "$x" ]; then
		continue
	fi
	cat `path2testdir "$a"`/line
done >"$KMC_RUN_OUTPUTPATH"/"abort.txt"

# xmlized stats
for a in "$KMC_RUN_OUTPUTPATH"/*/klee-last; do
	FDIR=`path2testdir "$a"`
	klee-stats --xml "$a" >$FDIR/last.stats
done

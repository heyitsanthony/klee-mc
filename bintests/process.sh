#!/bin/bash

# Stack traces of potential bugs
if [ -z "$KMC_RUN_OUTPUTPATH" ]; then
	KMC_RUN_OUTPUTPATH="bintests/out/"
fi

if [ ! -x bintests/error2xml.sh ]; then
	echo "Could not find error2xml.sh. Run from git root."
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
	bintests/error2xml.sh "$a"
done >>$ERRXML
echo "</errors>" >>$ERRXML

# get runs that timed out

for a in "$KMC_RUN_OUTPUTPATH"/*/timeout.txt; do
	cat `path2testdir "$a"`/line 2>/dev/null
done >"$KMC_RUN_OUTPUTPATH"/timeouts.txt

# runs that died from the crappy solver
for a in "$KMC_RUN_OUTPUTPATH"/*/klee-last/warnings.txt; do
	testdir=`path2testdir "$a"`
	x=`tail -n1 $a | egrep -i "(flushing|ministat)"`
	if [ ! -z "$x" ]; then
		cat "$testdir"/line
		continue
	fi
	x=`grep "die\!" "$testdir"/stderr`
	if [ ! -z "$x" ]; then
		cat "$testdir"/line
		continue
	fi
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
	x=`grep -i "assert_fail" "$a"`
	if [ ! -z "$x" ]; then
		cat `path2testdir "$a"`/line
		continue
	fi

	x=`grep "failed\." "$a"`
	if [ ! -z "$x" ]; then
		cat `path2testdir "$a"`/line
		continue
	fi
done >"$KMC_RUN_OUTPUTPATH"/"abort.txt"

# xmlized stats
for a in "$KMC_RUN_OUTPUTPATH"/*/klee-last; do
	FDIR=`path2testdir "$a"`
	klee-stats --xml "$a" >$FDIR/last.stats
done

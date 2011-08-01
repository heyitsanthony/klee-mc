#!/bin/bash

if [ -z "$1" ]; then
	echo "Usage: $0 errfile"
	exit -1
fi

echo "<error>"
echo "<errfile>"`echo "$1" | sed 's/\//\n/g' | tail -n1`"</errfile>"
testdir=`dirname "$1"`"/../"
if [ -e "$testdir"/line ]; then
	echo -n "<cmdline>"
	cat "$testdir"/line
	echo "</cmdline>"
fi
echo "<frames>"
grep Stack -A30 "$1"| grep "^[[:space:]]*#" | awk ' { print "<frame>"$0"</frame>" } '
echo "</frames>"

consline=`grep -n "^Constraints" "$1" | cut -f1 -d':'`
infoline=`grep -n "^Info" "$1" | cut -f1 -d':'`
echo "<constraints>"
if [ -z "$infoline" ]; then
	infoline=`wc -l "$1" |  cut -f1 -d' '`
fi
if [ ! -z "$consline" ] && [ ! -z "$infoline" ]; then
	consline=`expr $consline + 1`
	infoline=`expr $infoline - 1`
	sed -n "$consline,$infoline p" "$1"
else
	echo "XXX"
fi
echo "</constraints>"


echo "</error>"


#!/bin/bash

##################################
# inputs
# arg1 = pre snapshot
# arg2 = post snapshot
#
# output: memory ranges that have changed
#
# pre is usually a symlink, so it might be interesting
# to know when memory was last changed..
#
##################################

kleeoutdir=$1
ssbase=$2
ssdiff=$3
outdir="$4"

x=`ls $ssdiff/*cmp 2>/dev/null`
if [ -z "$x" ]; then exit 1; fi
x=`ls $kleeoutdir/mem* 2>/dev/null`
if [ -z "$x" ]; then exit 1; fi


for a in $ssdiff/*.cmp; do
	seghex=`basename $a  | cut -f1 -d'.'`
	sz=`ls -lH $ssbase/maps/$seghex | awk ' { print $5 } '`
	addr=`basename $a  | cut -f1 -d'.' | xargs printf "%d"`
	segend=`expr $addr + $sz | xargs printf "0x%x"`
	found=""
	for m in $kleeoutdir/mem[0-9]*; do
		for d in $m/*dat; do
			maddr=`basename $d  | cut -f1 -d'.' | xargs printf "%d"`
			if [ "$maddr" -lt "$addr" ]; then continue; fi
			if [ "$maddr" -gt `expr $addr + $sz` ]; then continue; fi
			found="$found $d"
		done
	done

	if [ -z "$found" ]; then
		echo Missed $seghex--$segend
	else
		echo Found $seghex--$segend : $found
	fi
done

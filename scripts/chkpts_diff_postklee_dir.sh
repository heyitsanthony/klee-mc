#!/bin/bash

diffdir="$1"

if [ -z "$diffdir" ]; then echo expected diffdir in arg1; fi

s=`dirname $0`/chkpts_diff_postklee.sh
for a in chkpt-*-pre; do
	if [ -z "$outdir" ]; then
		curout=""
	else
		curout="$outdir/$a"
		mkdir -p $curout
	fi
	echo ============$a===============
	$s klee-$a $a $diffdir/$a $curout >$diffdir/$a/kleemc.diff
done

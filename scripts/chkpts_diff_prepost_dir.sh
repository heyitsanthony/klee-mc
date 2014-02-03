#!/bin/bash

outdir="$1"
s=`dirname $0`/chkpts_diff_prepost.sh
for a in chkpt-*-pre; do
	if [ -z "$outdir" ]; then
		curout=""
	else
		curout="$outdir/$a"
		mkdir -p $curout
	fi
	echo ============$a===============
	$s $a `echo $a | sed "s/pre/post/g"` $curout
done

if [ ! -z "$outdir" ]; then
	wc -l  $outdir/chkpt-*-pre/*.cmp >$outdir/segs.sum
	rm -f $outdir/pages.sum
	for a in $outdir/chkpt-*-pre/; do
		x=`ls $a | wc -l`
		echo $x $a >>$outdir/pages.sum
	done
fi 


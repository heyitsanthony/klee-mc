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

sspre=$1
sspost=$2
outdir="$3"


if [ ! -e $sspost/maps ]; then exit 1; fi
if [ ! -e $sspre/maps ]; then exit 1; fi

function cmp_segs
{
	a="$1"
	if [ -h $a ]; then return; fi
	f=`basename $a`

	prefile="$sspre/maps/$f"
	if [ ! -e "$prefile" ]; then echo "New! $f"; return; fi

	presz=`ls -lH $prefile | awk ' { print $5 }'`
	postsz=`ls -lH $a | awk ' { print $5 } '`

	if [ "$presz" != "$postsz" ]; then
		echo $a $prefile
		echo size mismatch "$presz" vs "$postsz"
	fi

	cmp -l $prefile $a
}

for a in $sspost/maps/*; do
	if [ -z "$outdir" ]; then
		echo "# "`basename $a` `wc -c $a | cut -f1 -d' '`
		cmp_segs "$a"
		echo "##############"
	else
		outf="$outdir"/`basename $a`.cmp
		cmp_segs "$a" >$outf
		if [ ! -s "$outf" ]; then rm "$outf"; fi
	fi
done

if [ ! -z "$outdir" ]; then
	wc -l "$outdir"/*.cmp >"$outdir"/segs.sum
fi

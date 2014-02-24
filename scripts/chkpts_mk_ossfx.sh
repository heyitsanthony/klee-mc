#!/bin/bash

ppdir="$1"
if [ -z "$ppdir" ]; then echo no ppdir in arg1; exit 1; fi
chkptdir="$2"
if [ -z "$chkptdir" ]; then echo no chkptdir in arg2; exit 1; fi



rm -rf ossfx
mkdir -p ossfx
for a in $ppdir/chkpt*; do
	n=`basename $a | cut -f2 -d'-' | bc | tail -n1`

	updir=ossfx/"$n"
	mkdir $updir

	curchkptdir="$chkptdir"/$(basename $a | sed 's/pre/post/g')
	cp `echo "$curchkptdir" | sed 's/post/pre/g'`/regs $updir/regs.pre
	cp $curchkptdir/regs $updir/regs.post
	for b in $a/*cmp; do
		if [ ! -e "$b" ]; then break; fi

		cmpfile="$b"
		echo $cmpfile

		# XXX this should be handle new mmaps...
		isnew=`grep New "$cmpfile"`
		if [ ! -z "$isnew" ]; then
			mname=`basename $cmpfile | cut -f1 -d'.'`
			cp $curchkptdir/maps/$mname $updir/$mname.new
		else
			cp $cmpfile  $updir/
		fi
	done
done
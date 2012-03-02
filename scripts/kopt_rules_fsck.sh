#!/bin/bash

FSCKDIR="$1"

if [ -z "$FSCKDIR" ]; then
	echo expecteed fsck dir
	exit 1
fi

mkdir "$FSCKDIR"/orphans

for a in "$FSCKDIR"/*; do
	if [ -d "$a" ]; then
		continue
	fi

	isvalidfile=`echo $a | grep valid`
	if [ ! -z "$isvalidfile" ]; then
		continue
	fi

	if [ -e "$a.valid" ]; then
		continue
	fi

	kopt -pipe-solver -check-rule $a >$a.valid 2>$a.err
	echo "$a: "`cat $a.valid`

	isval=`grep "^valid" $a.valid`
	if [ -z "$isval" ]; then
		mv "$a" "$FSCKDIR"/orphans
		rm $a.valid
		rm $a.err
		continue
	fi

#	rm $a.valid
	rm $a.err
done
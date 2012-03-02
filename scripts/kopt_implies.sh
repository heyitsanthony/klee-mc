#!/bin/bash

if [ -z "$1" ]; then
	echo gimme rule dir
fi

cd "$1"
mkdir culled
mkdir xtive
for a in *; do
	NEWFNAME="$a.shorter"

	if [ -d "$a" ]; then
		continue
	fi

	echo $a
	kopt -pipe-solver -rule-file="../brule.db" -apply-transitive "$a" >"$NEWFNAME" 2>/dev/null
	
	RULEGREP=`grep "\\->" $NEWFNAME`
	if [ -z "$RULEGREP" ]; then
		rm "$NEWFNAME"
		continue
	fi

	cat "$NEWFNAME"
	RULEHASH=`md5sum $NEWFNAME | cut -f1 -d' '`
	mv $NEWFNAME ur.$RULEHASH.uniqrule
	NEWFNAME="ur.$RULEHASH.uniqrule"

	kopt -builder=constant-folding -pipe-solver -check-rule  $NEWFNAME 2>/dev/null >$RULEHASH.valid
	isvalid=`grep "^valid rule" $RULEHASH.valid`
	rm $RULEHASH.valid
	if [ -z "$isvalid" ]; then
		rm $NEWFNAME
		continue
	fi

	echo "OK."

	cp $NEWFNAME xtive

	h1=`head -n1 $a`
	chkhd=`grep "$h1" $NEWFNAME`
	if [ ! -z "$chkhd" ]; then
		echo "CULLING $a."
		echo "MATCH: $chkhd with $h1"
		mv $a culled
	else
		echo "DID NOT CULL $a."
		echo "MISMATCH: $h1 vs "
		cat $NEWFNAME
	fi
	mv $NEWFNAME $RULEHASH
done
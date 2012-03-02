#!/bin/bash

RULEDIR="$1"
BRULEDIR="$2"

if [ ! -d "$RULEDIR" ]; then
	echo "Provide rule dir"
	exit 1
fi

if [ ! -d "$BRULEDIR" ]; then
	echo "Provide brule dir"
	exit 1
fi


for a in "$RULEDIR"/[0-9a-f]*; do
	echo $a
	kopt -dump-bin $a  2>/dev/null \
		>"$BRULEDIR"/`echo $a | sed "s/\//\n/g" | tail -n1` 
done


for a in "$BRULEDIR"/*; do
	echo $a
	if [ -e $a.valid ]; then
		continue
	fi
	kopt -pipe-solver -use-bin -check-rule $a >$a.valid
done

rm -f "$BRULEDIR"/brule.db
for a in "$BRULEDIR"/*.valid; do
	isvalid=`grep "^valid" $a`
	if [ -z "$isvalid" ]; then
		continue
	fi
	cat `echo $a | sed "s/.valid//g"` >>"$BRULEDIR"/brule.db
done
#!/bin/bash

SRC="$1"
DST="$2"

if [ ! -d "$SRC" ]; then
	echo "Expected first arg to be src dir"
	exit -1
fi

if [ ! -d "$DST" ]; then
	echo "Expected second arg to be dst dir"
	exit -2
fi


lasthash=''
newrules="0"
md5sum `grep ^valid "$SRC"/*valid | cut -f1 -d':' | sed "s/rule/kopt/;s/valid/rule/" | cut -f1 -d' '` | sort | \
while read a; do
	HASHVAL=`echo $a | cut -f1 -d' '`
	FNAME=`echo $a | cut -f2 -d' '`
	if [ "$HASHVAL" = "$lasthash" ]; then
		continue
	fi
	lasthash="$HASHVAL"
	if [ ! -e "$DST"/"$HASHVAL" ]; then
		cp $FNAME "$DST"/"$HASHVAL"
		newrules=`expr $newrules + 1`
	fi
done
echo "New rules: " $newrules
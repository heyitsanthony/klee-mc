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
old_rule_count=`ls -l $DST/ | wc -l`
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
	fi
done
new_rule_count=`ls -l $DST/ | wc -l`
echo "New rules: " `expr $new_rule_count - $old_rule_count`
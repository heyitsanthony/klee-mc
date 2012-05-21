#!/bin/bash

HCACHE="$1"
if [ -z "$HCACHE" ]; then
	HCACHE="hcache/"
fi

echo "HCACHE = $1"

if [ ! -d "$HCACHE" ]; then
	echo $HCACHE does not exist
	exit -1
fi

for a in `find "$1"/`; do
	if [ ! -f "$a" ]; then
		continue
	fi

	sz=`stat --printf="%s" "$a"`
	if [ "$sz" == 0 ]; then
		rm "$a"
		continue
	fi
done
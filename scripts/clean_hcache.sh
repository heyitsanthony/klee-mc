#!/bin/bash

HCACHE="$1"
if [ -z "$HCACHE" ]; then
	HCACHE="hcache/"
fi

echo "HCACHE = $1"
echo "Sleeping for clean."
sleep 2

for a in `find "$1"/`; do
	if [ ! -f "$a" ]; then
		continue
	fi

	sz=`stat --printf="%s" "$a"`
	if [ "$sz" == 0 ]; then
		echo "BYE $a"
		rm "$a"
		continue
	fi
	echo $a
done
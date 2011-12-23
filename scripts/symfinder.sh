#!/bin/bash

BINPATH=`which $1`
NEEDLE="$2"
libs=`ldd "$BINPATH"| grep "=> /" | awk '{ print $3; }'`

echo "Searching '$BINPATH' for '$NEEDLE'"
for a in $libs; do
	echo "$a: "
	found=`readelf -s "$a" | grep $NEEDLE`
	if [ ! -z "$found" ]; then
		echo "$a: $found"
	fi
done
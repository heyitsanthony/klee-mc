#!/bin/bash

if [ ! -d "$1" ]; then
	echo "Expected equivdb dir"
	exit 1
fi

./scripts/equivdb_list_hashes.sh "$1" | tail -n50 | \
while read a; do 
	NUMELEMS=`echo $a | cut -f1 -d' '`
	if [ "$NUMELEMS" -gt 5 ]; then
		echo $a | cut -f2 -d' ' >>"$1"/blacklist.txt
	fi
done

uniq "$1"/blacklist.txt >"$1"/blacklist.uniq.txt
mv "$1"/blacklist.uniq.txt "$1"/blacklist.txt

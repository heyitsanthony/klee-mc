#!/bin/bash

if [ ! -d "$1" ]; then
	echo "Expected equivdb dir"
	exit 1
fi

EQUIVDIR="$1"
# clear out singletons from given equivdb dirs
olddir=`pwd`
cd $EQUIVDIR
mkdir trash
for a in `find "./" | cut -f3 -d'/' | sort | uniq -c | grep " 1 " | awk ' { print $2 } '`; do
	mv */$a trash/
done

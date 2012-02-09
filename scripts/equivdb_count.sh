#!/bin/bash

if [ ! -d "$1" ]; then
	echo "No equivdb directory given."
	exit -1
fi

echo "#BucketSize NumBuckets"

ls "$1"/*/* | cut -f3 -d'/' | sort | uniq -c  | sort -n	\
| awk ' { print $1; } ' \
| uniq -c | awk '{ print $2 " " $1; }'

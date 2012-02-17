#!/bin/bash

if [ ! -d "$1" ]; then
	echo "No equivdb directory given."
	exit -1
fi

if [ ! -z `echo "$1" | grep "/" ` ]; then
	echo "No slashes in $1"
	exit -3
fi

echo "#BucketSize NumBuckets"

ls "$1"/*/*/* | cut -f4 -d'/' | sort | uniq -c  | sort -n	\
| awk ' { print $1; } ' \
| uniq -c | awk '{ print $2 " " $1; }'

#!/bin/bash

cd "$1"
echo `pwd`
echo "#"`cat line`
for a in klee-out-*; do
	klee-stats $a

	if [ ! -e caches/$a* ]; then
		continue
	fi

	echo -n $a" "

	# Cache size
	echo -n `wc -c caches/$a* | cut -f1 -d' '`" "

	# TEST CASES
	echo -n `ls $a | grep  test | grep gz | cut -f1 -d'.' | sort | uniq | wc -l | cut -f1 -d' '`" "

	# UNCOVERED
	echo `klee-stats --xml $a  | grep Uncov | sed "s/<[^>]*>//g"`
done

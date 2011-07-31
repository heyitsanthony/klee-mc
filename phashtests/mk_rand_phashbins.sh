#!/bin/bash

BTLINES=`wc -l bintests/bt.txt | cut -f1 -d' '`
for i in `seq 1 10`; do
	lineno=`expr $RANDOM % $BTLINES`
	sed -n "$lineno p" bintests/bt.txt
done

#!/bin/bash

n=`ls klee-last/*ktest.gz | wc -l`
for a in `seq 1 $n`; do
	kmc-replay $a 2>&1 | tail -n2 | grep 231 | cut -f4 -d= | cut -f1 -d.
done
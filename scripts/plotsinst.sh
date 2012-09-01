#!/bin/bash

#branches that have only been taken one way
grep 18446744073709551615 klee-last/brdata.txt \
	| cut  -d' ' -f5,6 \
	| sed "s/ /\n/g" \
	| grep -v  18446744073709551615 \
	| sort -n >broneway.txt

sed -n "$ p" klee-last/sinst.txt	\
	| python -c "import sys; print reduce(lambda x,y : x+y, map(lambda (x,y) : [x]*y, eval(sys.stdin.read())[1:]));" \
	| sed "s/, /\n/g;s/\]//g;s/\[//g" >stinsts.txt
gnuplot <<<"
set term 'png'
set title 'total instructions for every state'
set output 'stinsts.png'
plot 'stinsts.txt' with points title 'states',\
'broneway.txt' with points title 'branches'"
#feh stinsts.png
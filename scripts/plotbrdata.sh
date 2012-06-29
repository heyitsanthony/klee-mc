#!/bin/bash
# differences
grep -v 18446744073709551615 klee-last/brdata.txt \
	| cut -d' ' -f5,6	\
	| sed "s/ /-/g"		\
	| bc -l | sed "s/-//g"	\
	| sort -n >brdiffs.txt

gnuplot <<<"set term 'png'; set output 'brdiffs.png'; set logscale y; plot 'brdiffs.txt'"
feh brdiffs.png

#branches that have only been taken one way
grep 18446744073709551615 klee-last/brdata.txt \
	| cut  -d' ' -f5,6 \
	| sed "s/ /\n/g" \
	| grep -v  18446744073709551615 \
	| sort -n >broneway.txt
gnuplot <<<"set term 'png'; set output 'broneway.png'; plot 'broneway.txt'"
feh broneway.png

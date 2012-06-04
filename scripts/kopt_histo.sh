#!/bin/bash

RULE_ARG=""
if [ ! -z "$1" ]; then
	RULE_ARG="-rule-file=$1"
	echo "$RULE_ARG"
fi

kopt "$RULE_ARG" -ko-consts=64 -db-histo - >kopt-histo64.out
kopt "$RULE_ARG" -ko-consts=32 -db-histo - >kopt-histo32.out
kopt "$RULE_ARG" -ko-consts=16 -db-histo - >kopt-histo16.out
kopt "$RULE_ARG" -ko-consts=8 -db-histo - >kopt-histo8.out
kopt "$RULE_ARG" -ko-consts=1 -db-histo - >kopt-histo-bytes.out

gnuplot <<<"
set logscale x
set terminal 'png'
set output 'kopt-histo.png'
set xlabel 'Rules found in Equivalence Class'
set ylabel '% Total Rules'
set key bottom right
set title 'Cumulative Equivalence Class Rule Distribution'
set logscale x
set xrange [1:]
plot 	'kopt-histo64.out' with linespoints title '64-bit constants',\
	'kopt-histo32.out' with linespoints title '+ 32-bit',\
	'kopt-histo16.out' with linespoints title '+ 16-bit',\
	'kopt-histo8.out' with linespoints title '+ 8-bit',\
	'kopt-histo-bytes.out' with linespoints title '+ 8*n-bit'
set terminal 'postscript' enhanced 24
set output 'kopt-distrib.eps'
replot
"

#!/bin/bash

kopt -ko-consts=64 -db-histo >kopt-histo64.out
kopt -ko-consts=32 -db-histo >kopt-histo32.out
kopt -ko-consts=16 -db-histo >kopt-histo16.out
kopt -ko-consts=8 -db-histo >kopt-histo8.out
kopt -ko-consts=1 -db-histo >kopt-histo-bytes.out

gnuplot <<<"
set logscale x
set terminal 'png'
set output 'kopt-histo.png'
set xlabel 'Equivalence Class Size'
set ylabel '% Total Rules'
set key bottom right
set title 'Cumulative Equivalence Class Rule Distribution'
set logscale x
plot 	'kopt-histo64.out' with lines title '64-bit constants',\
	'kopt-histo32.out' with lines title '+ 32-bit',\
	'kopt-histo16.out' with lines title '+ 16-bit',\
	'kopt-histo8.out' with lines title '+ 8-bit',\
	'kopt-histo-bytes.out' with lines title '+ 8*n-bit'
"

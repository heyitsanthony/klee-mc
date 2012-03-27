#!/bin/bash

./scripts/plotinsts.py klee-last/sinst.txt >sinst.dat
gnuplot <<<"
set term png size 1024,768
set output 'sinst.png'
set pm3d map
#set logscale y
set xlabel 'time (s)'
set zlabel 'states'
set ylabel 'instrs'
splot 'sinst.dat' with pm3d
"

feh sinst.png
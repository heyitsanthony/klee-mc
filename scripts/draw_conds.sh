#!/bin/bash
sed 's/buf[0-9]*/buf/g;s/reg[0-9]*/reg/g;s/(/\\n(/g;s/readlink[0-9]*/readlink/g;s/\\n[ ]*\\n/\\n/g' klee-last/fconds.txt >fconds2.txt
dot  -Gfontname=courier -Gfontsize=8 fconds2.txt -Tps  -ohey.ps
gv --scale=0.25 hey.ps
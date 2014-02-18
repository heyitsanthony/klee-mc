#!/bin/bash

rm -f record/*
mkdir -p record/
while [ 1 ]; do
	sleep	5s
	t=`date "+%s"`
	echo gimme $t
	dot klee-last/btracker.dot -Teps -o record/$t.ps
	convert -flatten -background white record/$t.ps record/$t.png
done
#!/bin/bash
source flags.sh

if [ ! -z "$1" ]; then
	cd `dirname "$1"`
fi

KLEE_FLAGS="$KLEE_FLAGS " #-xchk-hwaccel=true " #-debug-print-values -debug-print-instructions"
sname=`basename "$1"`
knest_launch "py" "python2.7" "$sname" "" `pwd`/"trampoline.py"

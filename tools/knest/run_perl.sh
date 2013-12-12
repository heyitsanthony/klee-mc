#!/bin/bash
source flags.sh

if [  -z "$1" ]; then echo no script given; exit 1; fi
cd `dirname "$1"`

KLEE_FLAGS="$KLEE_FLAGS " #-xchk-hwaccel=true " #-debug-print-values -debug-print-instructions"

knest_launch "pl" "perl" `basename "$1"` "abcdef abcdef" 

#!/bin/bash

p=`dirname $0`
if [ ! -z "$p" ]; then p="$p/"; fi
echo "p = $p"
source "$p"defaults.sh

nth="$1"
if [ -z "$nth" ]; then nth="0000";
else nth=`printf "%04d" $nth`; fi

gdb --args klee-mc	-guest-type=sshot -guest-sshot=chkpt-$nth-pre \
	-print-new-ranges	\
	$EXTRA_ARGS		\
	$RULEFLAGS		\
	$HCACHE_FLAGS		\
	$SCHEDOPTS		\
	$STATOPTS		\
	$SOLVEROPTS				\
	-write-smt				\
	-max-syscalls-per-state=1		\
	-use-hookpass -hookpass-lib=sysexit.bc	\
	-output-dir=klee-reach	\
	- 2>klee-reach.err


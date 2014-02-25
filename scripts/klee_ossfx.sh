#!/bin/bash

p=`dirname $0`
if [ ! -z "$p" ]; then p="$p/"; fi
echo "p = $p"
source "$p"defaults.sh

gdb --args klee-mc	-guest-type=sshot -guest-sshot=chkpt-0000-pre \
	-print-new-ranges	\
	$EXTRA_ARGS		\
	$RULEFLAGS		\
	$HCACHE_FLAGS		\
	$STATOPTS		\
	$SOLVEROPTS		\
	-ossfx-dir=ossfx	\
	-write-smt		\
	-output-dir=klee-ossfx	\
	- 2>klee.ossfx.err


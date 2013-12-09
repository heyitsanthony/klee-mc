#!/bin/bash

p=`dirname $0`
if [ ! -z "$p" ]; then p="$p/"; fi
source `"$p"defaults.sh`


# here we collect all the snapshot diffs
# TODO NEED TO BE ABLE TO DUMP OUT DIRTY MEMORY
# TODO NEED OT BE ABLE TO DUMP OUT REGISTERS
klee-mc	-guest-type=sseq -guest-sseq=chkpt -use-sseq-pre \
	$EXTRA_ARGS		\
	$RULEFLAGS		\
	$HCACHE_FLAGS		\
	$STATOPTS		\
	$SOLVEROPTS		\
	-write-smt -use-hookpass -hookpass-lib=sysexit.bc	\
	-


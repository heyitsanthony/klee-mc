#!/bin/bash

p=`dirname $0`
if [ ! -z "$p" ]; then p="$p/"; fi
echo "p = $p"
source "$p"defaults.sh


mkdir -p err

# here we collect all the snapshot diffs
# TODO NEED TO BE ABLE TO DUMP OUT DIRTY MEMORY
# TODO NEED OT BE ABLE TO DUMP OUT REGISTERS
for a; do
n=`basename $a`
klee-mc	-guest-type=sshot -guest-sshot=$a \
	$EXTRA_ARGS		\
	$RULEFLAGS		\
	$HCACHE_FLAGS		\
	$STATOPTS		\
	$SOLVEROPTS		\
	-output-dir=klee-$n	\
	-force-cow		\
	-write-mem		\
	-write-smt -use-hookpass -hookpass-lib=sysexit.bc	\
	- 2>err/klee-$n.err
done

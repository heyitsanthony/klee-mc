#!/bin/bash

for a in `grep -i ' t ' /usr/src/linux/System.map | cut -f3 -d' '`; do
	echo $a
	outdir="kuc/klee-$a"
	if [ -e "$outdir" ]; then continue; fi

SCHEDOPTS="-use-batching-search
        -batch-time=5
        -use-second-chance=false
        -second-chance-boost=1
        -second-chance-boost-cov=1
        -use-pdf-interleave=true
        -use-interleaved-UO=true
        -use-fresh-branch-search=true
        -use-interleaved-CD=true
        -use-interleaved-MI=true
        -use-interleaved-MXI=true
        -use-interleaved-BE=false"

RULEFLAGS="-use-rule-builder -rule-file=default_rules.db"

	VEXLLVM_BASE_BIAS=0x100000000 klee-mc	\
		$RULEFLAGS			\
		$SCHEDOPTS			\
		-pipe-solver			\
		-pipe-fork-queries		\
		-max-stp-time=8			\
		-randomize-fork			\
		-print-new-ranges		\
		-use-cex-cache=false -use-cache=false -guest-type=sshot	\
		-hcache-fdir=`pwd`/hcache       \
		-hcache-pending=`pwd`/hcache    \
		-hcache-sink                    \
		-hcache-dir=`pwd`/hcache        \
		-use-hash-solver=true		\
		-guest-sshot=guest-vmlinux -sym-mmu-type=uc -symregs -run-func=$a	\
		-max-time=300			\
		-output-dir="$outdir" 2>$outdir.err
done
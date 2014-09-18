#!/bin/bash

RULEFLAGS="-use-rule-builder -rule-file=default_rules.db"
# -equiv-rule-file=kryptonite_new.dba
# -equiv-expr-builder
## -replace-equiv
# -queue-solver-equiv
# -write-equiv-rules
#"

# -use-rule-builder=true \


#	-batch-instructions=20000
SCHEDOPTS="-use-batching-search
	-batch-time=5
	-use-second-chance=false
	-second-chance-boost=1
	-second-chance-boost-cov=1
	-use-pdf-interleave=true
	-use-fresh-branch-search=true
	-use-interleaved-MI=true
	-use-interleaved-MXI=true
	-use-interleaved-CD=true
	"
#	-use-interleaved-RS=true

#	-use-interleaved-UO=true
#	-use-interleaved-BE=true"

#	-use-interleaved-MI=true
#	-use-interleaved-NI=true
#	-use-interleaved-MXI=true

#	-use-interleaved-FTR=false
#	-use-interleaved-BI=false
#	-use-interleaved-UNC=false


if [ -z "$APP_WRAPPER" ]; then
APP_WRAPPER="gdb --args"
fi

#-allow-negstack	

if [ ! -z "$USE_PSDB" ]; then
	PSDBFLAGS="-use-hookpass -hookpass-lib=partseed.bc"
fi

if [ ! -z "$USE_PSDB_REPLAY" ]; then
	PSDBFLAGS="-psdb-replay=true  -ktest-timeout=15 -use-hookpass -hookpass-lib=partseed.bc"
fi


if [ -z "$MMU_S" ]; then MMU_S="objwide"; fi
MMUFLAGS="-sym-mmu-type=$MMU_S -use-sym-mmu"

if [ ! -z "$USE_MMU" ]; then
	MMUFLAGS=" -sym-mmu-type=memcheck -sconc-mmu-type=memcheckc -use-sym-mmu -use-hookpass -hookpass-lib=libkleeRuntimeMMU.bc "
fi


#	-deny-sys-files 	\
#

MAX_MEMORY="-max-memory=4096"
HCACHE_FLAGS="
	-hcache-fdir=`pwd`/hcache	\
	-hcache-pending=`pwd`/hcache	\
	-hcache-sink			\
	-hcache-dir=`pwd`/hcache 	\
	-use-hash-solver=true		"

STATOPTS="
	-dump-select-stack	\
	-dump-covstats=1	\
	-dump-rbstats=1		\
	-dump-statestats=1	\
	-dump-memstats=1	\
	-dump-exprstats=1	\
	-dump-hashstats=1	\
	-dump-querystats=1	\
	-dump-stateinststats=10	\
	-dump-br-data=5		"

SOLVEROPTS="
	-use-cache=false	\
	-use-cex-cache=false	\
	-pipe-fork-queries	\
	-pipe-solver		\
	-smt-let-arrays=true	"

#!/bin/bash

if [ -f "$PATHDIR" ]; then
	rm -rf pathdir
	mkdir pathdir
	cp "$PATHDIR" pathdir/
	PATHDIR="pathdir"
fi

if [ -z "$PATHDIR" ]; then
	PATHDIR="klee-last"
fi

RUNDIR="pruns-$PATHDIR"
rm -rf "$RUNDIR"
mkdir -p "$RUNDIR"

function do_path_replay
{
	STPTIMEOUT=30
	a="$1"
	n=`echo $a | sed "s/test/\n/g" | tail -n1 | cut -f1 -d'.'`
	#-use-rule-builder 
	# -xchk-expr-builder	\
	klee-mc -logregs \
		-deny-sys-files		\
		$EXTRA_ARGS		\
		-guest-type=sshot	\
		-allow-negstack		\
		-smt-let-arrays=true	\
		-symargs		\
		-pipe-solver		\
		-pipe-fork-queries	\
		-max-stp-time=$STPTIMEOUT	\
		-print-new-ranges		\
		-mm-type=deterministic	\
		-output-dir="$RUNDIR/$n"	\
		-replay-path-only=true		\
		-replay-path="$a" 2>"$n"_err
	mv "$n"_err $RUNDIR/$n/err
}

REPLAYNUM="$1"
if [ ! -z "$REPLAYNUM" ]; then
	do_path_replay "$PATHDIR"/*0"$REPLAYNUM".path.gz
	exit
fi

for a in "$PATHDIR"/*path.gz; do
	do_path_replay "$a"
done
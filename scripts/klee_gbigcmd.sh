#!/bin/bash

GUESTARCH="$2"
GUESTBIN="$1"

if [ -z "$GUESTBIN" ]; then
	echo No guest bin given
	exit
fi

if [ -z "$GUESTARCH" ]; then
	GUESTARCH="x64"
fi

GUESTPATH=~/guests/guests-bin-"$GUESTARCH"/guest-"$GUESTBIN"
if [ ! -d "$GUESTPATH" ]; then
	echo Could not find $GUESTPATH
	exit
fi

rm guest-last
ln -s "$GUESTPATH" guest-last

USE_LAST=1	\
EXTRA_ARGS="$EXTRA_ARGS -symargs -hcache-fdir="`pwd`"/hcache -hcache-pending="`pwd`"/hcache -hcache-sink -hcache-dir="`pwd`"/hcache  -use-hash-solver=true" \
	./scripts/klee_bigcmd.sh "none"

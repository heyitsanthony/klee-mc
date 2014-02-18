#!/bin/bash

GUESTPATH=~/guests/guests-bin-"$1"/guest-"$2"

if [ ! -d "$GUESTPATH" ]; then
	echo $GUESTPATH not found
	exit -1
fi

rm guest-last
ln -s "$GUESTPATH" guest-last
USE_LAST=1 scripts/klee_bigcmd.sh none

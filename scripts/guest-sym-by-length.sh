#!/bin/bash

GUESTDIR="$1"
if [ -z "$GUESTDIR" ]; then
	GUESTDIR="guest-last"
fi

sed "s/ / 0x/g;s/-/ 0x/g;" "$GUESTDIR"/syms  | perl  -e 'for $a (<>) { @b = split(/ /,$a); printf ("%d %s\n",  hex($b[2])-hex($b[1]), $b[0]); }' | sort -n 
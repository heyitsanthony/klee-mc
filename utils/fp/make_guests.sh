#!/bin/bash

rm -rf guest-*
mkdir fpguests
for a in bin/fp*-*-*; do
	if [ ! -d fpguests/guest-$a ]; then
		VEXLLVM_SAVE=1 pt_run ./$a
		mv guest-[0-9][0-9]* fpguests/guest-`basename $a`
	fi
done

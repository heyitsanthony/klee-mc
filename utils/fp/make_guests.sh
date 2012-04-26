#!/bin/bash

rm -rf guest-*
mkdir fpguests
for a in fp*-*-*; do
	VEXLLVM_SAVE=1 pt_run ./$a
	mv guest-[0-9]* fpguests/guest-$a
done

#!/bin/bash

if [ -z "$1" ]; then
	BTPATH="bt.txt"
else
	BTPATH="$1"
fi
mkdir -p bin_pack
while read line
do
	echo $line
	VEXLLVM_SAVE=1 pt_run $line
	GUESTPATH=guest-`echo $line | md5sum | cut -f1 -d' '`
	mv `readlink guest-last` bin_pack/$GUESTPATH
	cd bin_pack
	tar cvf $GUESTPATH.tar $GUESTPATH
	gzip $GUESTPATH.tar
	rm -rf $GUESTPATH
	cd ..
done < $BTPATH
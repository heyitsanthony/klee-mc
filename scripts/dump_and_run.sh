#!/bin/bash

if [ -z "$1" ]; then
	echo "Expected directory for first argument"
	exit -1
fi

if [ -z "$2" ]; then

	exit -2
fi

rm -rf dump_n_run
mkdir -p dump_n_run
cd dump_n_run
for a in ../"$1"/*.ktest.gz; do
	ktest-tool --dump $a
done

for a in kdump-*; do
	cd $a
	readbufs=`ls readbuf*`
	if [ -z "$readbufs" ]; then
		cd ..
		continue
	fi
	../../scripts/join_readbuf.sh
	echo "$a"
	$2 readbuf
	cd ..
done
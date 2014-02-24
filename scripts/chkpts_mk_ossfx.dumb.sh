#!/bin/bash

ppdir="$1"
if [ -z "$ppdir" ]; then echo no ppdir in arg1; exit 1; fi

function mk_update_files
{
	updir=ossfx/"$1"
	cmpfile="$2"
	base=`basename "$cmpfile" | cut -f1 -d'.'`
	basen=`printf "%ld" $base`
	basefile=-100
	lastoff=9999999
	curlineno=1
	echo =========
	echo $cmpfile $basen

	# XXX this should be handle new mmaps...
	isnew=`grep New "$cmpfile"`
	if [ ! -z "$isnew" ]; then
		echo ignoring mmap on $cmpfile
		return
	fi

	while [ 1 ]; do
		l=`sed -n "$curlineno p" "$cmpfile" 2>/dev/null`
		if [ -z "$l" ]; then break; fi
		curoff=`echo $l | cut -f1 -d' '`
		curv=$(printf "%x" 0`echo $l | cut -f3 -d' '`)

		nextoff=`expr $lastoff + 1`
		if [ "$nextoff" -ne "$curoff" ]; then
			basefile=$(printf "0x%lx" `expr "$basen" + "$curoff"`)
		fi

		echo -e -n "\x$curv" >>$updir/$basefile
		lastoff="$curoff"
		curlineno=`expr $curlineno + 1`
	done
}

rm -rf ossfx
mkdir -p ossfx
for a in $ppdir/chkpt*; do
	n=`basename $a | cut -f2 -d'-' | bc | tail -n1`
	mkdir ossfx/$n

	for b in $a/*cmp; do
		if [ ! -e "$b" ]; then continue; fi
		mk_update_files "$n" "$b"
	done
done
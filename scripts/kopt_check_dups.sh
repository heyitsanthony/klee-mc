#!/bin/bash


mkdir "$1"/orphans
rm -f heads
for a in "$1"/*[0-9a-f][0-9a-f][0-9a-f]; do
	head -n1 $a >>heads
done

sort heads | uniq -c |  sort -n | grep "  [2-9]  " | egrep "(Extract|Add|Sub|Eq|Ult|Ule|Ugt|Uge|Shl|Sge|Sgt)" | sed "s/      [2-9] //" | while read a; do
	rulelen=`echo $a | wc -c`
	if [ "$rulelen" -lt  30 ]; then
		continue
	fi
	minlen=99999
	minrule=""
	filesgrep=`grep  "^ $a"'$' rules/* -n | cut -f1,2 -d':'`
	files=`echo "$filesgrep" | grep "1$" | cut -f1 -d':'`
	for b in $files; do
		tolen=`head -n3 $b | tail -n1 | wc -c`
		echo $b "tolen=$tolen"
		if [ "$tolen" -lt "$minlen" ]; then
			minlen="$tolen"
			minrule="$b"
		fi
	done

	if [ -z "$minrule" ]; then
		continue
	fi

	for b in $files; do
		if [ "$b" == "$minrule" ]; then
			continue
		fi

		mv "$b" "$1"/orphans/
		rm "$b".*
		echo "BYEBYE $b"
	done


	echo "MINRULE: $b"

	echo "==================="
done
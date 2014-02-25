#!/bin/bash

targetdir="$1"
if [ -z "$targetdir" ]; then targetdir='.'; fi

d=`dirname $0`
for a in `ls "$targetdir" | grep "^chkpt" | grep "pre$" | sort`; do
	"$d"/chkpts_print_syscall.sh "$targetdir"/"$a"
done | sort | uniq -c  | sort -n
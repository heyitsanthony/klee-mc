#!/bin/bash

d=`dirname $0`
for a in `ls | grep "^chkpt" | grep "pre$" | sort`; do
	"$d"/chkpts_print_syscall.sh "$a"
done | sort | uniq -c  | sort -n
#!/bin/bash

attach_pid=`sed "s/ /\n/g" gdb.threads | grep -A1 LWP | tail -n1 | sed "s/[^0-9].*//g"`
if [ -z "$attach_pid" ]; then
	attach_pid=`sed "s/ /\n/g" gdb.threads | grep -A1 process | tail -n1 | sed "s/[^0-9].*//g"`
fi

kill -s SIGSTOP $attach_pid

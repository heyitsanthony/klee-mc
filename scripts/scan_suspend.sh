#!/bin/bash

attach_pid=`sed "s/ /\n/g" gdb.threads | grep -A1 LWP | tail -n1 | sed "s/[^0-9].*//g"`
kill -s SIGSTOP $attach_pid

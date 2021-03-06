#!/bin/bash

if [ -z "$1" ]; then
	echo "MUST PROVIDE COMMAND"
	exit -1
fi

sudo opcontrol --stop
sudo opcontrol --reset

VEXLLVM_SAVE=1 pt_run "$1"
OPROF_SAMPLERATE=1000000
sudo opcontrol --start --vmlinux=/usr/src/linux/vmlinux --event=CPU_CLK_UNHALTED:${OPROF_SAMPLERATE}:0:1:1

klee-mc -use-cache=false -use-cex-cache=false -pipe-solver -use-fresh-branch-search -batch-time=5 -use-batching-search -batch-instructions=9999999 \
	-guest-type=sshot -mm-type=deterministic - 

sudo opcontrol --dump
sudo opreport -g -a --symbols `which klee-mc` >oprof.log
sudo opcontrol --stop
sudo opcontrol --reset




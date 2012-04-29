#!/bin/bash

target_line=`tail -n1 gdb.threads`
attach_pid=`echo "$target_line" | sed "s/ /\n/g" | grep -A1 LWP | tail -n1 | sed "s/[^0-9].*//g"`
if [ -z "$attach_pid" ]; then
	attach_pid=`sed "s/ /\n/g" gdb.threads | grep -A1 process | tail -n1 | sed "s/[^0-9].*//g"`
fi
echo "ATTACH_PID=" $attach_pid

if [ ! -d /proc/$attach_pid/ ]; then
	echo Could not get pid $attach_pid
	exit 1
fi

if [ -z "$GDB_SCAN_BLOCKED" ]; then
	VEXLLVM_SAVE=1		\
	VEXLLVM_NOSYSCALL=1 	\
	VEXLLVM_ATTACH=$attach_pid pt_run none
else
	echo "SAVING BLOCKED SNAPSHOT."
	echo "ATTACHPID=" $attach_pid
	VEXLLVM_SAVE=1	\
	VEXLLVM_ATTACH=$attach_pid pt_run none
fi

if [ -f gdb.regs ]; then
	echo "FORCING THREAD REGISTER WITH GDB.REGS"
	cp guest-last/regs regs.old
	utils/gdb2vex guest-last/regs gdb.regs >regs.new
	mv regs.new guest-last/regs
	ripaddr=`grep "^rip" gdb.regs | awk ' { print $2; }'`
	perl -e "print pack('Q<',$ripaddr);" >guest-last/entry
	mv gdb.regs gdb.regs.old
fi

ret=$?
echo $ret
scripts/scan_retry.sh

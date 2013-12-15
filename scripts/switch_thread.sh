#!/bin/bash

n="$1"
if [ -z "$n" ]; then echo expect thread number; fi

pushd guest-last

if [ ! -e threads/$n ]; then
	echo threads/$n does not exist
	exit 1
fi

if [ ! -e regs_orig ]; then
	cp regs regs_orig
	cp regs.gdt regs_orig.gdt 2>/dev/null
	cp regs.ldt regs_orig.ldt 2>/dev/null
fi

if [ -e threads/$n ]; then
	rm regs
	ln -s threads/$n regs
fi

if [ -e threads/$n.gdt ]; then
	rm regs.gdt
	ln -s threads/$n.gdt regs.gdt
fi

if [ -e threads/$n.ldt ]; then
	rm regs.ldt
	ln -s threads/$n.ldt regs.ldt
fi

popd
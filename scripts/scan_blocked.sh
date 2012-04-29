#!/bin/bash
# don't force gdb regs for syscall
rm gdb.regs
GDB_SCAN_BLOCKED=1 ./scripts/scan_launch.sh

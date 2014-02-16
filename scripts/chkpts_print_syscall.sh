#!/bin/bash

INCHKPT="$1"

grep `od -An -tx8 -j16 -N8 "$INCHKPT"/regs | sed 's/ /0x/g' | xargs printf "%d"` /usr/include/asm/unistd_64.h | grep NR  | head -n1 | awk ' { print $2 } ' | sed 's/__NR_//g'
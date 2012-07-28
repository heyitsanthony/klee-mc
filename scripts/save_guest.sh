#!/bin/bash

VEXLLVM_SAVE=1 pt_run "$1"

if [ -z "$2" ]; then
	exit 0
fi

mv `readlink guest-last` "$2"
rm guest-last
ln -s "$2" guest-last

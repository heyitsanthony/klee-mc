#!/bin/bash

cd $1
ls -l `ls -Sl kopt* | grep " 0 " | cut -f2 -d'.' | sed 's/$/.smt/;s/^/proof\./g'`
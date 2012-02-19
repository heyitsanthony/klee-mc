#!/bin/bash

KOPTDIR="$1"
cd "$KOPTDIR"
md5sum `grep ^valid *valid | cut -f1 -d':' | sed "s/rule/kopt/;s/valid/rule/"` | cut -f1 -d' ' | sort | uniq -c | sort -n

echo -n "Num uniq: "
md5sum `grep ^valid *valid | cut -f1 -d':' | sed "s/rule/kopt/;s/valid/rule/"` | cut -f1 -d' ' | sort | uniq -c | sort -n | wc -l

echo "Valid Rules: " `grep ^valid *valid | wc -l`
echo "Total Rules Tested:" `ls *valid | wc -l`
echo "Total Proofs:" `ls proof* | wc -l`
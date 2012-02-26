#!/bin/bash

cd "$1"
for a in proof.*.smt; do
	RULEKEY=`echo $a | cut -f2 -d'.'`
	RULEFNAME=kopt.$RULEKEY.rule
	kopt -pipe-solver $a 2>/dev/null >$RULEFNAME

	RULEHASH=`md5sum $RULEFNAME | cut -f1 -d' '`
	cp $RULEFNAME ur.$RULEHASH.uniqrule
done

for a in *.uniqrule; do
	RULEHASH=`echo "$a" | cut -f2 -d'.'`
	kopt -pipe-solver -check-rule $a 2>/dev/null >rule.$RULEHASH.valid
done

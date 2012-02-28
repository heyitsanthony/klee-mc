#!/bin/bash

cd "$1"

# get rid of huge proofs
ls -lS proof.* | awk -e ' { if ($5 > 10000) print $9; } ' | xargs rm

for a in proof.*.smt; do
	RULEKEY=`echo $a | cut -f2 -d'.'`
	RULEFNAME=kopt.$RULEKEY.rule
	echo $RULEFNAME
	kopt -pipe-solver $a 2>/dev/null >$RULEFNAME

	RULEHASH=`md5sum $RULEFNAME | cut -f1 -d' '`
	if [ -e "ur.$RULEHASH.uniqrule" ]; then
		continue
	fi

	cp $RULEFNAME ur.$RULEHASH.uniqrule
	kopt -pipe-solver -check-rule ur.$RULEHASH.uniqrule 2>/dev/null >rule.$RULEHASH.valid
done

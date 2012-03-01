#!/bin/bash

cd "$1"

for a in pending.*.rule; do
	RULEHASH=`md5sum $a | cut -f1 -d' '`
	if [ -e "ur.$RULEHASH.uniqrule" ]; then
		continue
	fi

	cp $a ur.$RULEHASH.uniqrule
	kopt -pipe-solver -check-rule ur.$RULEHASH.uniqrule 2>/dev/null >rule.$RULEHASH.valid
	echo $RULEHASH: `cat rule.$RULEHASH.valid`
done

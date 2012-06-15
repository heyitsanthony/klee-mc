#!/bin/bash
CURDATE=`date +%s`
FPREFIX="pending.$CURDATE"
PROOFDIR=${1:-"proofs"}

function xtive_loop
{
	BRULEF="$1"
	loopc=0
	while [ 1 ]; do
		APPENDED=`kopt -max-stp-time=30 -pipe-solver -brule-xtive -rule-file="$BRULEF" 2>&1 | grep Append | cut -f2 -d' '`
		if [ -z "$APPENDED" ]; then
			break
		fi
		if [ "$APPENDED" -eq "0" ]; then
			break
		fi

		loopc=$(($loopc + 1))
		if [ "$loopc" -gt 32 ]; then
			break;
		fi
	done
}

kopt -max-stp-time=30 -pipe-solver -dump-bin -check-rule "$PROOFDIR" >$FPREFIX.brule

xtive_loop "$FPREFIX.brule"


kopt -pipe-solver -brule-rebuild -rule-file=$FPREFIX.brule $FPREFIX.rebuild.brule
mv $FPREFIX.rebuild.brule $FPREFIX.brule
cp "$FPREFIX.brule" pending.brule
kopt -max-stp-time=30 -pipe-solver -db-punchout		\
	-rule-file=$FPREFIX.brule			\
	-unique-file=$FPREFIX.uniq.brule		\
	-uninteresting-file=$FPREFIX.unint.brule	\
	-stubborn-file=$FPREFIX.stubborn.brule		\
	$FPREFIX.punch.brule
xtive_loop "$FPREFIX.punch.brule"
kopt -pipe-solver -brule-rebuild -rule-file=$FPREFIX.punch.brule $FPREFIX.punch.rebuild.brule
mv $FPREFIX.punch.rebuild.brule $FPREFIX.punch.brule


cp "$FPREFIX".punch.brule punch.brule
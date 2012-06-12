#!/bin/bash
CURDATE=`date +%s`
FPREFIX="pending.$CURDATE"
PROOFDIR=${1:-"proofs"}
kopt -pipe-solver -dump-bin -check-rule "$PROOFDIR" >$FPREFIX.brule
kopt -pipe-solver -brule-xtive -rule-file=$FPREFIX.brule
kopt -pipe-solver -brule-rebuild -rule-file=$FPREFIX.brule $FPREFIX.rebuild.brule
mv $FPREFIX.rebuild.brule $FPREFIX.brule
cp "$FPREFIX.brule" pending.brule
kopt -max-stp-time=30 -pipe-solver -db-punchout		\
	-rule-file=$FPREFIX.brule			\
	-unique-file=$FPREFIX.uniq.brule		\
	-uninteresting-file=$FPREFIX.unint.brule	\
	-stubborn-file=$FPREFIX.stubborn.brule		\
	$FPREFIX.punch.brule
kopt -pipe-solver -brule-xtive -rule-file=$FPREFIX.punch.brule
kopt -pipe-solver -brule-rebuild -rule-file=$FPREFIX.punch.brule $FPREFIX.punch.rebuild.brule
mv $FPREFIX.punch.rebuild.brule $FPREFIX.punch.brule


cp "$FPREFIX".punch.brule punch.brule
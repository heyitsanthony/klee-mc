#!/bin/bash

cd "$1"
for a in proof.*.smt; do
	RULEKEY=`echo $a | cut -f2 -d'.'`
	RULEFNAME=kopt.$RULEKEY.rule
	kopt -pipe-solver $a 2>/dev/null >$RULEFNAME
	kopt -pipe-solver -check-rule $RULEFNAME 2>/dev/null >rule.$RULEKEY.valid
done
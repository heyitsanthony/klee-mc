#!/bin/bash

kopt -dedup-db -rule-file=used.db tmp.dd.db
cp tmp.dd.db tmp.db

for a in `seq 1 16`; do
	kopt -pipe-solver -brule-xtive -rule-file=tmp.db
	kopt -dedup-db -rule-file=tmp.db tmp.dd.db
	cp tmp.dd.db tmp.db
done
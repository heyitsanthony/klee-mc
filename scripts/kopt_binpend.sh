#!/bin/bash
CURDATE=`date +"%s-%N"`
FPREFIX="pending.$CURDATE"
PROOFDIR=${1:-"proofs"}

KOPT_FLAGS="-max-stp-time=30 -pipe-solver"

function xtive_loop
{
	BRULEF="$1"
	loopc=0
	LASTAPPENDED="whatever"
	while [ 1 ]; do
		# normal forms
		kopt -rb-recursive=false $KOPT_FLAGS $KOPT_HASHFLAGS -nf-dest -rule-file="$BRULEF" 2>&1
		APPENDED=`kopt $KOPT_FLAGS $KOPT_HASHFLAGS -rb-recursive=false -brule-xtive -rule-file="$BRULEF" 2>&1 | grep Append | cut -f2 -d' '`
		if [ -z "$APPENDED" ]; then
			break
		fi
		if [ "$APPENDED" -eq "0" ]; then
			break
		fi

		loopc=$(($loopc + 1))
		if [ "$loopc" -gt 8 ]; then
			break;
		fi

		OLDDUPS=`cat $BRULEF.dups`
		kopt -dedup-db -rule-file="$BRULEF" $BRULEF.tmp 2>&1 | grep -i dups >$BRULEF.dups
		mv $BRULEF.tmp $BRULEF

		NEWDUPS=`cat $BRULEF.dups`
		if [ ! -z "$NEWDUPS" ]; then
			if [ ! -z "$OLDDUPS" ]; then
			if [ "$NEWDUPS" == "$OLDDUPS" ]; then
				echo "SAME DUPS!"
				break;
			fi
			fi
		fi

		if [ -z "$APPENDED" ]; then
			if [ -z "$LASTAPPENDED" ]; then
				break;
			fi
		elif [ "$APPENDED" -eq "$LASTAPPENDED" ]; then
			break
		fi

		LASTAPPENDED=$APPENDED
	done

	rm "$BRULEF.dups"
}

if [ -f "$PROOFDIR" ]; then
	ISGZIP=`file "$PROOFDIR" | grep gzip`
	if [ -z "$ISGZIP" ]; then
		cp "$PROOFDIR" "$FPREFIX".brule
	else
		zcat "$PROOFDIR" >"$FPREFIX".brule
	fi
else
	kopt $KOPT_FLAGS $KOPT_HASHFLAGS -dump-bin -check-rule "$PROOFDIR" >$FPREFIX.brule
fi

if [ -f "$SEEDBRULE" ]; then
	zcat "$SEEDBRULE" >>$FPREFIX.brule
	if [ $? -ne 0 ]; then
		cat "$SEEDBRULE" >>$FPREFIX.brule
	fi
fi


# verify rules are working correctly before doing any xtive stuff
kopt -rule-file=$FPREFIX.brule -dedup-db $FPREFIX.dd.brule
kopt -pipe-solver -brule-rebuild -rule-file=$FPREFIX.dd.brule $FPREFIX.rebuild.brule 2>/dev/null
cp "$FPREFIX".rebuild.brule "$FPREFIX".brule

xtive_loop "$FPREFIX.brule"


kopt -pipe-solver -brule-rebuild -rule-file=$FPREFIX.brule $FPREFIX.rebuild.brule 2>/dev/null
mv $FPREFIX.rebuild.brule $FPREFIX.brule
kopt $KOPT_FLAGS $KOPT_HASHFLAGS -db-punchout		\
	-ko-consts=16					\
	-rule-file=$FPREFIX.brule			\
	-unique-file=$FPREFIX.uniq.brule		\
	-uninteresting-file=$FPREFIX.unint.brule	\
	-stubborn-file=$FPREFIX.stubborn.brule		\
	$FPREFIX.punch.brule
xtive_loop "$FPREFIX.punch.brule"


# do full verify
kopt -pipe-solver -brule-rebuild -rule-file=$FPREFIX.punch.brule $FPREFIX.punch.rebuild.brule
mv $FPREFIX.punch.rebuild.brule $FPREFIX.punch.brule
cat $FPREFIX.{punch,uniq,unint,stubborn}.brule  >$FPREFIX.final.brule
xtive_loop $FPREFIX.final.brule
kopt -pipe-solver -brule-rebuild -rule-file=$FPREFIX.final.brule $FPREFIX.final.rb.brule
rm $FPREFIX.final.brule
gzip $FPREFIX.final.rb.brule
mv $FPREFIX.final.rb.brule.gz  $FPREFIX.final.brule.gz 
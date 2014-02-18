#!/bin/bash

# A triple is a snapshot pair and a set of tests from the symbolic
# executor. p = (s, s*); T = {set of tests}-- (s,s*,T).

# this is a really crappy interface; more thought needs to be put into
# the prepost diffing architecture. Lotsa Symlinks?
if [ -z "$1" ]; then echo expected arg1 = chkpt number.; exit 1; fi
if [ -z "$2" ]; then echo expected arg2 = prepost diff dir; exit 2; fi
if [ -z "$3" ]; then echo expected arg3 = klee test dir; exit 3; fi

v=`printf "%04d" $1`
ppdir="$2"
ktestdir="$3"
diffdir="$ppdir/chkpt-$v-pre"

# go through each test case finding matches

function get_best_match
{
for a in "$ktestdir"/*ktest.gz; do
	t=`basename $a | cut -f1 -d'.'`
	m=`echo $t | sed 's/test/mem/g'`
	hits=`grep "$m" $diffdir/kleemc.diff | sed "s/ /\n/g" | grep "$m" | wc -l`
	echo $hits $m
done | sort -n | tail -n1
}

m=`get_best_match`
if [ -z "$m" ]; then echo no match; exit 1; fi
if [ `echo $m | cut -f1 -d' '` == "0" ]; then echo 0 match; exit 2; fi

t="$ktestdir"/`echo $m | cut -f2 -d' ' | sed 's/mem/test/g'`.ktest.gz
template=`ktest-tool $t | egrep "(name|size)" | cut -f3 -d':' | awk '{ if (x) { print $1" "x; x="" } else { x = $1 } }' | sed "s/'//g" `

###### begin building output

outlist=""
mkdir -p synth-test/updates
mkdir -p synth-test/used

###### get register list
hasregs=`echo "$template" | head -n1 | grep reg`
if [ ! -z "$hasregs" ]; then
	outlist=`echo $hasregs | cut -f2 -d' '`
	template=`echo "$template" | sed '1,1d'`
	cp chkpt-$v-post/regs synth-test/$outlist
fi

template=`echo "$template" | sort -r -n`

echo "$template"

function sshot_range_to_file
{
	sshotdir="$1"
	# takes hex addr-- convert to decimal
	target_base=`printf "%ld" "$2"`
	target_sz="$3"
	target_out="$4"

	for m in `ls "$sshotdir"/maps/* | sort -n`; do
		segbegin=`basename $m  | cut -f1 -d'.' | xargs printf "%ld" 2>/dev/null`
		if [ -z "$segbegin" ]; then continue; fi
		sz=`ls -lH "$m" | awk ' { print $5 } '`
		if [ -z "$sz" ]; then continue; fi
		segend=`expr $segbegin + $sz`
		if [ -z "$segend" ]; then continue; fi
		if [ "$segbegin" -gt "$target_base" ]; then continue; fi
		if [ "$segend" -lt "$target_base" ]; then continue; fi

		# kind of dumb-- assume a single segment can handle the request
		off=`expr $target_base - $segbegin`
		dd if="$m" of="$target_out" skip="$off" bs=1 count="$target_sz"
		return
	done
}

# read updated ranges in klee from post-snapshot; these
# are the candidate symbolic buffers
ktmemdir=$ktestdir/`echo $m | awk ' { print $2 } '`

# read all updates from post snapshot
for a in $ktmemdir/*.dat; do
	taddr=`basename $a | cut -f1 -d'.'`
	tsz=`ls $a -lH | awk ' { print $5; } '`
	sshot_range_to_file "chkpt-$v-post" $taddr $tsz synth-test/updates/$taddr
done


echo $outlist >synth-test/list
echo "$template" >synth-test/template

# assign template to updates
echo "$template" | while read a; do
	sz_cur=`echo $a | cut -f1 -d' '`
	name=`echo $a | cut -f2 -d' '`
	match=""
	udir="synth-test/updates/"
	for b in `ls $udir  | sort -n`; do
		echo $udir/$b
		sz_update=`ls $udir/$b -lH | awk ' { print $5 } '`
		if [ -z "$sz_update" ]; then continue; fi

		if [ "$sz_cur" -eq "$sz_update" ]; then
			cp $udir/$b synth-test/used/
			mv $udir/$b synth-test/$name
			match="$b"
			break;
		fi
	done

	if [ -z "$match" ]; then echo "COULD NOT MATCH $a. RUHROH"; continue; fi
	echo $name >>synth-test/list
done


#finally, generate ktest
pushd synth-test
mk_ktest `cat list`
popd
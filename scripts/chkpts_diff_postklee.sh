#!/bin/bash

##################################
# inputs
# arg1 = pre snapshot
# arg2 = post snapshot
#
# output: memory ranges that have changed
#
# pre is usually a symlink, so it might be interesting
# to know when memory was last changed..
#
##################################

kleeoutdir=$1
ssbase=$2
ssdiff=$3
outdir="$4"

x=`ls $ssdiff/*cmp 2>/dev/null`
if [ -z "$x" ]; then exit 1; fi
x=`ls $kleeoutdir/mem* 2>/dev/null`
if [ -z "$x" ]; then exit 1; fi

# goes through all different segments in postsnapshot, tries to find
# equivalent in kleemc run; seg missing => underapprox
function find_underapprox
{
for a in $ssdiff/*.cmp; do
	seghex=`basename $a  | cut -f1 -d'.'`
	sz=`ls -lH $ssbase/maps/$seghex | awk ' { print $5 } '`
	if [ -z "$sz" ]; then continue; fi
	addr=`basename $a  | cut -f1 -d'.' | xargs printf "%d"`
	segend=`expr $addr + $sz | xargs printf "0x%x"`
	found=""
	for m in $kleeoutdir/mem[0-9]*; do
		asz=`expr $addr + $sz`
		for d in $m/*dat; do
			maddr=`basename $d  | cut -f1 -d'.' | xargs printf "%d"`
			if [ "$maddr" -lt "$addr" ]; then continue; fi
			if [ "$maddr" -gt "$asz" ]; then continue; fi
			found="$found $d"
		done
	done

	if [ -z "$found" ]; then
		echo Missed $seghex--$segend
	else
		echo Found $seghex--$segend : $found
	fi
done
}

# did kleemc have a side effect that the system did not?
# this is kind of tricky since the symbolic model will do things
# the os will not...
# overapproximation only counts if *every* branch has a sideeffect that isn't
# present for the actual process?
function find_overapprox
{
overs=""
for m in $kleeoutdir/mem[0-9]*; do
	for d in $m/*dat; do
		#maddr = base of change in klee test
		maddr=`basename $d  | cut -f1 -d'.' | xargs printf "%d"`
		msz=`ls -lH $d | awk ' { print $5 } '`
		mend=`expr $maddr + $msz`
		for a in $ssdiff/*.cmp; do
			# only interested in symlinks since they haven't been
			# modified by the system call...
			seghex=`basename $a  | cut -f1 -d'.'`
			segfile="$ssbase/maps/$seghex"
			sz=`ls -lH "$segfile" | awk ' { print $5 } '`
			if [ -z "$sz" ]; then continue; fi

			addr=`basename $a  | cut -f1 -d'.' | xargs printf "%d"`
			segend=`expr $addr + $sz | xargs printf "0x%x"`
			aend=`expr $addr + $sz`

			if [ ! -h "$segfile" ]; then continue; fi

			if [ "$maddr" -lt "$addr" ]; then continue; fi
			if [ "$maddr" -gt $aend ]; then continue; fi
			if [ "$mend" -le "$aend" ]; then continue; fi

			maddrhex=`printf "%x" $maddr`
			overs="$overs $maddrhex"
		done
	done
done
if [ -z "$overs" ]; then return; fi
lines=`echo -n $overs | sed 's/ /\n/g' | sort | uniq -c`
n=`ls $kleeoutdir | grep "mem[0-9]" | wc -l`
# format: Over [total tests] 
echo "$lines" | awk ' { print "Over "'"$n"'" "$0; } '
}

find_underapprox
find_overapprox

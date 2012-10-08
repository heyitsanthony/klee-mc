#!/bin/bash

if [ -z "$1" ] || [ ! -d "$1" ]; then
	echo "Expected path directory as first arg"
	exit 1
fi

if [ -z "$2" ] || [ ! -e "$2" ]; then
	echo "Expected UNCOV file as second arg"
	exit 2
fi


scriptdir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
pathdir="$1"
uncovfile="$2"


grep "\[UNCOV\] " "$uncovfile" >uncov.dat

function insert_all_sb
{
	echo ".read $scriptdir/riskyfast.sqlite3"
	echo "BEGIN TRANSACTION;"
	IFS=$'\n'
	for covline in `cat uncov.dat`; do
		sbaddr=`echo "$covline" | cut -f1 -d'-' | cut -f2 -d' '`
		sbaddr_lit=`printf "%lu" $sbaddr`
		sbname=`echo "$covline" | cut -f2 -d':'`
		sbname=`echo -e -n $sbname`
		sblen=`echo "$covline" |  cut -f2 -d' '  | sed "s/-/ /" | awk '{ print $2 " " $1 }' | xargs printf "%d - %d\n" | bc` 
		echo -e -n "INSERT OR IGNORE INTO sb (sbaddr, name, sblen) VALUES ($sbaddr_lit, '$sbname', $sblen); "
	done
	echo "COMMIT;"
	unset IFS
}


function insert_nodes
{
	echo ".read $scriptdir/riskyfast.sqlite3"
	echo "BEGIN TRANSACTION;"
	# index blocks by earliest visit
	for sb in `zgrep 'sb_' "$mininstfile"`; do
		sbpair=`printf "%d , %lu " $(echo "$sb" | sed "s/,sb_/ /g" | grep "0x")`
		# n = minpos, sbaddr
		echo -e -n "INSERT INTO pathnode (pathid, minpos, sbaddr) VALUES ($pathid, $sbpair); "
	done
	echo "COMMIT;"
}

function insert_nodes_fast
{
	echo ".read $scriptdir/riskyfast.sqlite3"
	echo "BEGIN TRANSACTION;"
python <<< "
import gzip
mi=map (lambda x : x.strip().split(','), gzip.open('$mininstfile'))
mi = filter (lambda (x,y) : y[:3] == 'sb_', mi)
for mi_e in mi:
	print 'INSERT INTO pathnode (pathid, minpos, sbaddr) VALUES ($pathid, ' + mi_e[0] + ',' + str(int(mi_e[1][3:],0)) + ');'
"
	echo "COMMIT;"
}


function insert_all_nodes
{
	echo ".read $scriptdir/riskyfast.sqlite3"
	echo "BEGIN TRANSACTION;"
	# XXX
	echo "COMMIT;"
}

if [ ! -e exe.db ]; then
	sqlite3  exe.db <<< ".read $scriptdir/pathdb.sqlite3"
fi

insert_all_sb >sb.sqlite3
sqlite3 -batch -init sb.sqlite3 exe.db </dev/null

for a in $pathdir/*.path.gz; do
	b=`basename $a | cut -f1 -d'.'`
	sbfile="$1"/$b.sb
	mininstfile="$1"/$b.mininst.gz
	ktestfile="$1"/$b.ktest.gz

	echo ========PROCESSING PATH $a=============

	phash=`zcat $a | cut -f1 -d',' | sha1sum | cut -f1 -d' '`
	sqlite3 exe.db <<< "SELECT pathid FROM path WHERE phash='$phash' LIMIT 1;" >pathid
	if [ ! -z `cat pathid` ]; then
		echo "PATH FOR $phash HAS BEEN SEEN"
		continue
	fi
	
	echo "GET UNIQUE BLOCKS"
	#find all unique basic blocks 
	zcat "$mininstfile" | cut -f2 -d',' | uniq | sort | uniq | grep "sb_" | sed "s/sb_//g" >"$sbfile"
	ktesthash=`zcat "$ktestfile" | sha1sum | cut -f1 -d' '`
	sqlite3 exe.db <<< "INSERT INTO path (phash, ktesthash) VALUES ('$phash', '$ktesthash');" >/dev/null
	sqlite3 exe.db <<< "SELECT pathid FROM path WHERE phash='$phash' LIMIT 1;" >pathid
	
	echo INSERT NODES===========================
	pathid=`cat pathid`
	insert_nodes_fast >nodes.sqlite3
	echo SQL NODES========================
	sqlite3 -batch -init nodes.sqlite3 exe.db </dev/null
	echo DONE INSERTING============================
done


rm uncov.dat
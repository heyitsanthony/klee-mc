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


function insert_sb
{
	echo ".read $scriptdir/riskyfast.sqlite3"
	echo "BEGIN TRANSACTION;"
	for sbaddr in `cat $sbfile`; do
		sbaddr_lit=`printf "%lu" $sbaddr`
		sbname=`grep " $sbaddr-" uncov.dat | head -n1 | cut -f2 -d':'`
		sbname=`echo -e -n $sbname`
		echo -e -n "INSERT OR IGNORE INTO sb (sbaddr, name) VALUES($sbaddr_lit, '$sbname'); "
	done
	echo "COMMIT;"
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

for a in $pathdir/*path.gz; do
	b=`basename $a | cut -f1 -d'.'`
	sbfile="$1"/$b.sb
	mininstfile="$1"/$b.mininst.gz

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

	sqlite3 exe.db <<< "INSERT INTO path (phash) VALUES ('$phash');" >/dev/null
	sqlite3 exe.db <<< "SELECT pathid FROM path WHERE phash='$phash' LIMIT 1;" >pathid
	
	echo INSERT SB MAKE FILE======================
	insert_sb  >sb.sqlite3
	echo INSERT SB INTO SQL===================
	sqlite3 -batch -init sb.sqlite3 exe.db </dev/null

	echo INSERT NODES===========================
	pathid=`cat pathid`
	insert_nodes >nodes.sqlite3
	echo SQL NODES========================
	sqlite3 -batch -init nodes.sqlite3 exe.db </dev/null
	echo DONE INSERTING============================
done


rm uncov.dat
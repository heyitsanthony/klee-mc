#!/bin/bash

if [ ! -d "$1" ]; then
	echo "Expected path directory as first arg"
fi

if [ ! -e "$2" ]; then
	echo "Expected UNCOV file as second arg"
fi


scriptdir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
pathdir="$1"
uncovfile="$2"

function insert_sb
{
	echo ".read $scriptdir/riskyfast.sqlite3"
	echo "BEGIN TRANSACTION;"
	for sbaddr in `cat $sbfile`; do
		sbaddr_lit=`printf "%lu" $sbaddr`
		sbname=`grep "\[UNCOV\] $sbaddr-" $uncovfile | head -n1 | cut -f2 -d':'`
		sbname=`echo -e -n $sbname`
		echo "INSERT OR IGNORE INTO sb (sbaddr, name) VALUES($sbaddr_lit, '$sbname'); "
	done
	echo "COMMIT;"
}

function insert_nodes
{
	echo ".read $scriptdir/riskyfast.sqlite3"
	echo "BEGIN TRANSACTION;"
	# index blocks by earliest visit
	cp $a $a.tmp.gz
	gzip -d $a.tmp.gz
	for sb in `cat "$sbfile"`; do
		n=`grep -n "$sb" $a.tmp | head -n1 | sed "s/[:,]/ /g" | cut -f1,3 -d ' '`
		n=`printf "%d , %lu" $n`
		# n = minpos, sbaddr
		echo "INSERT INTO pathnode (pathid,minpos,sbaddr) VALUES ($pathid, $n);"
	done
	rm $a.tmp
	echo "COMMIT;"
}

function insert_all_nodes
{
	echo ".read $scriptdir/riskyfast.sqlite3"
	echo "BEGIN TRANSACTION;"
	# index blocks by earliest visit
	cp $a $a.tmp.gz
	gzip -d $a.tmp.gz
	for sb in `cat "$sbfile"`; do
		n=`grep -n "$sb" $a.tmp | head -n1 | sed "s/[:,]/ /g" | cut -f1,3 -d ' '`
		n=`printf "%d , %lu" $n`
		# n = minpos, sbaddr
		echo -e -n "INSERT INTO pathnode 		\
				(pathid, minpos, sbaddr)	\
				VALUES ($pathid, $n); "
	done
	rm $a.tmp
	echo "COMMIT;"

}

if [ ! -e exe.db ]; then
	sqlite3  exe.db <<< ".read $scriptdir/pathdb.sqlite3"
fi

for a in $pathdir/*path.gz; do
	b=`basename $a | cut -f1 -d'.'`
	sbfile="$1"/$b.sb

	echo ========PROCESSING PATH $a=============

	#find all unique basic blocks 
	zcat $a | cut -f2 -d',' | uniq | sort | uniq >"$sbfile"

	phash=`sha1sum $a | cut -f1 -d' '`

	sqlite3 exe.db <<< "SELECT pathid FROM path WHERE phash='$phash' LIMIT 1;" >pathid
	if [ ! -z `cat pathid` ]; then
		echo "PATH FOR $phash HAS BEEN SEEN"
		continue
	fi
	

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
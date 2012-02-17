#!/bin/bash

if [ -z "$1" ]; then
	echo Expected equivdb
	exit -1
fi

if [ ! -z `echo $1 | grep /` ]; then
	echo no slashes
	exit -2
fi

ls "$1"/*/*/*  | cut -f4 -d'/' | sort | uniq -c  | sort -n

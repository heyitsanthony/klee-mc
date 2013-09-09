#!/bin/bash

function mk_hfile
{
	osname="$1"
	prettyname="$2"
	echo "#ifndef WINNT_$prettyname"
	echo "#define WINNT_$prettyname"

	for a in `grep "sym_name" nt32.sys.htm | cut -f2 -d'>' | cut -f1 -d'<'`; do
		n=`grep "$a" -A100 nt32.sys.htm | grep "$osname" |head -n1 |  cut -f2 -d'>' | cut -f1 -d'<' | sed "s/&nbsp;//g"`
		if [ -z "$n" ];  then continue; fi

		echo "#define $a $n"
	done

	echo "#endif"
}


mk_hfile "os_0" "nt4" >ntapi.nt4.h
mk_hfile "os_1" "nt2k" >ntapi.nt2k.h
mk_hfile "os_2" "xp" >ntapi.xp.h
mk_hfile "os_3" "nt2k3" >ntapi.nt2k3.h
mk_hfile "os_4" "vista" >ntapi.vista.h
mk_hfile "os_5" "nt2k8" >ntapi.nt2k8.h
mk_hfile "os_6" "win7" >ntapi.win7.h
mk_hfile "os_7" "win8" >ntapi.win8.h

#!/bin/bash

function mk_hfile_nt
{
	mk_hfile "$1" "$2" nt32.sys.htm "NT"
}

function mk_hfile_w32k
{
	mk_hfile "$1" "$2" win32k.sys.htm "WIN32K"
}

function mk_hfile
{
	osname="$1"
	prettyname="$2"
	fname="$3"

	echo "#ifndef WINNT_$4_$prettyname"
	echo "#define WINNT_$4_$prettyname"

	for a in `grep "sym_name" "$fname" | cut -f2 -d'>' | cut -f1 -d'<'`; do
		n=`grep "$a" -A100 "$fname" |
			awk ' { if (match($0, "</tr>")) exit 0; print $0 } ' |
			grep "$osname" | tail -n1 |  cut -f2 -d'>' | cut -f1 -d'<' | sed "s/&nbsp;//g"`
		if [ -z "$n" ];  then continue; fi
		echo "#define $a $n"
	done

	echo "#endif"
}

mk_hfile_nt "os_0" "nt4" >ntapi.nt4.h
mk_hfile_nt "os_1" "nt2k" >ntapi.nt2k.h
mk_hfile_nt "os_2" "xp" >ntapi.xp.h
mk_hfile_nt "os_3" "nt2k3" >ntapi.nt2k3.h
mk_hfile_nt "os_4" "vista" >ntapi.vista.h
mk_hfile_nt "os_5" "nt2k8" >ntapi.nt2k8.h
mk_hfile_nt "os_6" "win7" >ntapi.win7.h
mk_hfile_nt "os_7" "win8" >ntapi.win8.h

#mk_hfile_w32k "os_0" "nt4" >win32k_nt4.h
#mk_hfile_w32k "os_1" "nt2k" >win32k_nt2k.h
mk_hfile_w32k "\"12_" "xp" >win32k_xp.h
#mk_hfile_w32k "os_3" "nt2k3" >win32k_nt2k3.h
mk_hfile_w32k "\"18_" "vista" >win32k_vista.h
mk_hfile_w32k "\"20_" "nt2k8" >win32k_nt2k8.h
mk_hfile_w32k "\"22_" "win7" >win32k_win7.h
#mk_hfile_w32k "os_7" "win8" >win32k_win8.h

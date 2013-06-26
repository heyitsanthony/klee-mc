#!/bin/bash

D2JDIR=`pwd`/src/dex2jar/dex-tools/target/hey/dex2jar-0.0.9.15 

APKFILE="$1"

if [ -z "$APKFILE" ]; then
	echo expected arg1 to be .apk file
fi

APKFILE=`pwd`/"$1"

pushd $D2JDIR
d2j-dex2jar.sh	"$APKFILE"
popd


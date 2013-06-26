#!/bin/bash

D2JDIR=$HOME/src/dex2jar/dex-tools/target/hey/dex2jar-0.0.9.15 
VMJCBIN=$HOME/src/llvm/llvm-3.3.src/vmkit/Release+Asserts/bin/vmjc
PLATFORM=/opt/klee/jclasses/android-17

APKFILE="$1"
DESTDIR="$2"

if [ -z "$APKFILE" ]; then
	echo expected arg1 to be .apk file
fi

if [ -z "$DESTDIR" ]; then
	DESTDIR=`pwd`
fi

# expects absolute path here
APKFILE="$1"
fname=`echo $APKFILE | sed 's/\//\n/g' | tail -n1 | cut -f1 -d'.'`

function convert_apk
{
# convert from apk to fname.dir
pushd $D2JDIR >/dev/null
rm -f "$fname"-dex2jar.jar
rm -rf "$fname".dir
./d2j-dex2jar.sh "$APKFILE"

mkdir -p "$fname".dir && cd "$fname".dir
unzip ../"$fname"-dex2jar.jar && cd ..
cp "$fname".dir "$DESTDIR"/ -r
popd >/dev/null
}

if [ ! -e "$fname".dir ]; then
	convert_apk
fi

# link in platform
pushd "$PLATFORM" >/dev/null
last_link="xxxxxxx"
for a in `find . -type d | sort`; do
	if [ "$a" == '.' ] || [ "$a" == '..' ]; then
		continue
	fi
	classloader_path=`echo $a | cut -f2- -d'/' `
	is_linked=`echo "$classloader_path" | grep "$last_link"`
	if [ ! -z "$is_linked" ]; then
		# already linked by parent directory
		continue
	fi

	ln -s "$PLATFORM"/"$classloader_path" "$DESTDIR"/"$fname".dir/"$classloader_path" 2>/dev/null
	exitcode="$?"
	if [ "$exitcode" != "0" ]; then
		last_link="xxxxxxxxxxxx"
		continue
	fi

	last_link="$classloader_path"
done
popd >/dev/null

# convert to bitcode
pushd $DESTDIR/"$fname".dir >/dev/null
for a in `find . -name \*.class `; do
	if [ -e "$a".bc ]; then
		continue
	fi
	classloader_path=`echo $a | cut -f2- -d'/'`
	echo =====$a=======
	$VMJCBIN "$classloader_path"
done
popd >/dev/null

pushd $DESTDIR/"$fname".dir >/dev/null
for a in `grep onCreate -r . | grep ctivity | sed "s/ /\n/g" | grep class`; do
	kleej $a.bc
done

popd >/dev/null
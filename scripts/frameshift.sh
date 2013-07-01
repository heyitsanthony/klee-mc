#!/bin/bash

fshift="50000"

ERRFILE="$1"
SSHOTDIR="$2"
TESTNUM="$3"
EXEPATH="$4"

if [ -z "$ERRFILE" ]; then echo no .err given; exit 2; fi
if [ -z "$SSHOTDIR" ]; then echo no sshot dir given; exit 3; fi
if [ -z "$TESTNUM" ]; then echo no testnum given; exit 4; fi
if [ -z "$EXEPATH" ]; then echo no exe path given; exit 5; fi


func=`grep sb_ "$ERRFILE" | head -n1`

if [ -z "$func" ]; then echo "No func found??"; exit 1; fi


# use no frameshift to get min 
minnum=`kcrumb-replay "$SSHOTDIR" "$TESTNUM" "$EXEPATH"  2>&1 | grep -i "Log syscalls used"  | cut -f2 -d':'`
if [ -z "$minnum" ]; then
	echo "Couldn't get used num?"
	exit 7
fi

function do_test
{
	KMC_FRAMESHIFT="$1" kcrumb-replay "$SSHOTDIR" "$TESTNUM" "$EXEPATH"  2>&1 
}

# finds last working frameshift with exponential probing
function find_end
{
	fshift=1
	incr=1
	lastok=0
	while [ 1 ]; do
		usednum=`do_test $fshift | grep "Log syscalls used"  | cut -f2 -d':'`
		if [ -z "$usednum" ]; then
			break;
		fi

		if [ "$usednum" -eq 0 ]; then
			if [ "$incr" -eq 1 ]; then
				break;
			fi
			incr=1
			fshift=`expr $lastok + $incr`
			continue
		else
			lastok="$fshift"
		fi

		incr=`expr $incr \* 2`
		fshift=`expr $fshift + $incr`
	done

	echo $lastok
}

function durr
{
fshift=0
maxnum="$minnum"
bestshift=0
if [ -z "$maxnum" ];  then exit 4; fi

while [ 1 ]; do
	usednum=`KMC_FRAMESHIFT="$fshift" kcrumb-replay "$SSHOTDIR" "$TESTNUM" "$EXEPATH"  2>&1  | grep "Log syscalls used"  | cut -f2 -d':'`
	
	if [ "$maxnum" -le "$usednum" ]; then
		bestshift="$fshift"
		maxnum="$usednum"
	fi

	if [ "$usednum" -eq 0 ]; then
		# nothing left
		break;
	fi
	fshift=`expr $fshift + 1`
done

}


#endshift=`find_end`
durr

KMC_FRAMESHIFT="$bestshift" kcrumb-replay "$SSHOTDIR" "$TESTNUM" "$EXEPATH"  2>&1 

echo "BEST SHIFT $bestshift"
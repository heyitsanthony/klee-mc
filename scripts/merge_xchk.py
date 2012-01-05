#!/usr/bin/python

import sys
from SMTState import *

if len(sys.argv) != 3:
	print "Expected ./merge_xchk.py state1.smt state2.smt"
	sys.exit(-1)

print "MERGING " + sys.argv[1] + " " + sys.argv[2]

st1 = SMTState.fromFile(sys.argv[1])
st2 = SMTState.fromFile(sys.argv[2])

st_xchk = st1.copy()
st_xchk.merge(st2)
st_xchk.writeXChk('xchk.merge.sat.smt', '(= bv1[1] bv1[1])')
#!/usr/bin/python

import sys
from SMTState import *

if len(sys.argv) != 3:
	print "Expected ./formula_xchk.py state1.smt state2.smt"
	sys.exit(-1)

print "FORMULAS " + sys.argv[1] + " " + sys.argv[2]

st1 = SMTState.fromFile(sys.argv[1])
st2 = SMTState.fromFile(sys.argv[2])

# For every assumption in st, create a SMT file with
# st_xchk's assumptions which checks the validity of the assumption of st.
# 1. Check to see if it's satisfiable (it st_xchk refines st, it should be)
# 2. Check to see if it's falsifiable (if st_xchk refines st, it should not be)
def xchkFormulaStates(prefix, st_xchk, st):
	xchk_c = 1
	for a in st.assumptions:
		if a in st_xchk.assumptions:
			continue
		# make sure alien expr is sat under current state
		st_xchk.writeXChk(((prefix + '.%d.sat.smt') % xchk_c), a)
		# make sure NOT alien expr is unsat under current state
		st_xchk.writeXChk(
			((prefix + '.%d.unsat.smt') % xchk_c), 
			'(not ' + a + ')')
		xchk_c = xchk_c + 1

st_xchk = st1.copy()
st_xchk.mergeArrays(st2)
xchkFormulaStates('xchk.l', st_xchk, st2)

st_xchk = st2.copy()
st_xchk.mergeArrays(st1)
xchkFormulaStates('xchk.r', st_xchk, st1)

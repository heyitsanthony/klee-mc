#!/usr/bin/python
import sys
import os

f = open(sys.argv[1])
for l in f.readlines():
	entry = eval(l)
	enttime = entry[0]
	for pt in entry[1:]:
		(insts,states) = pt
		print str(enttime) + " " + str(insts) + " " + str(states)
	print
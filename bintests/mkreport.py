#!/usr/bin/python

import hashlib

def loadRunSet(s):
	f = open(s, "r")
	ret = dict()
	for testrun in f:
		ret[testrun[:-1]] = True
	f.close()
	return ret

rs_master = loadRunSet("bintests/bt.txt")
rs_timeouts = loadRunSet("bintests/out/timeouts.txt")
rs_abort =loadRunSet("bintests/out/abort.txt")
rs_badsolve = loadRunSet("bintests/out/badsolver.txt")
rs_done = loadRunSet("bintests/out/done.txt")
rs_error = loadRunSet("bintests/out/error.txt")
rs_syscall = loadRunSet("bintests/out/syscall.txt")

rs_kstats = dict()
for testrun in rs_master.keys():
	m = hashlib.md5()
	m.update(testrun+"\n")
	md5 = m.hexdigest() 
	try:
		f = open("bintests/out/"+md5+"/last.stats", "r")
		s = f.read()
		f.close()
		rs_kstats[testrun] = s
	except:
		print "No laststats on " + testrun + " / " + md5

f = open("bintests/out/report.xml", "w")
f.write('<?xml version="1.0" encoding="ISO-8859-1"?>\n')
f.write('<?xml-stylesheet type="text/xsl" href="report.xsl"?>\n')
f.write('<testruns>\n')
for testrun in rs_master.keys():
	f.write('<testrun>')
	f.write('<command>'+testrun+'</command>\n')
	if testrun in rs_done:
		f.write('<done/>\n')
	if testrun in rs_error:
		f.write('<error/>\n')
	if testrun in rs_timeouts:
		f.write('<timeout/>\n')
	if testrun in rs_abort:
		f.write('<abort/>\n')
	if testrun in rs_badsolve:
		f.write('<badsolve/>\n')
	if testrun in rs_syscall:
		f.write('<newsyscall/>\n')
	if testrun in rs_kstats and len(rs_kstats[testrun]) > 0:
		f.write(rs_kstats[testrun])
	f.write('</testrun>\n')
f.write('</testruns>\n')
f.close()

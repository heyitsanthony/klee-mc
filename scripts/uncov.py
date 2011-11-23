#!/usr/bin/python
#
# Expects output from -print-new-regs in klee.
# Format example:
# [UNCOV] 0x7fb82f65ace0-0x7fb82f65ace8 : __read+0x10
#
import sys
import os
import subprocess
from optparse import OptionParser

op = OptionParser("usage: %prog binpath [args]")
op.add_option(
	'-i',
	'--input-uncov',
	dest='inputFile',
	action='store',
	default='klee-last.uncov',
	type='string')
op.add_option(
	'-x',
	'--xsl',
	dest='xslPath',
	action='store',
	default='uncov.xsl',
	type='string')
opts,args = op.parse_args()

binpath=args[0]

f = open(opts.inputFile, 'r')
uncov = dict()
for l in f:
	l = l.rstrip()
	v = l.split(' ')
	(addr_lo, addr_hi) = v[1].split('-')
	uncov[addr_lo[2:]] = (addr_hi[2:], v[3])
f.close()

p = subprocess.Popen(['objdump', '-Sw', binpath], stdout=subprocess.PIPE)

in_func = False
func_addr = 0
func_name =''
end_addr = 0
seen_func = False

print '<?xml version="1.0" encoding="ISO-8859-1"?>'
print '<?xml-stylesheet type="text/xsl" href="' + opts.xslPath + '"?>'
print "<appcov>"
print "<binary>"+binpath+"</binary>"
for l in p.stdout:
	l = l.rstrip()

	if len(l) == 0:
		continue

	if l[0] == 'D':
		if seen_func == True:
			print "</func>"
		seen_func = False
		print "<section>"+l+"</section>"
		continue

	# function header? ala:
	# 000000000040daf0 <vasnprintf>:
	if len(l.split('>:')) != 1:
		in_func = False
		if seen_func == True:
			print "</func>"
		seen_func = True
		func_name = l.split(' ')[1]
		func_name = func_name[1:]
		func_name = func_name[:-2]
		print "<func><name>"+func_name+"</name>"
		continue

	fields = l.split('\t')
	cur_addr = fields[0].lstrip()
	cur_addr = cur_addr[:-1]

	if len(fields) < 2:
		continue

	if in_func and end_addr == cur_addr:
		in_func = False

	if cur_addr in uncov.keys():
		func_addr = cur_addr
		end_addr = uncov[cur_addr][0]
		in_func = True

	ins_bytes = fields[1]
	ins_asm = ''
	if len(fields) > 2:
		sanitized = fields[2].replace('>', '&gt;')
		sanitized = sanitized.replace('<', '&lt;')
		ins_asm = sanitized

	if in_func:
		print "<ins active=\"true\">"
	else:
		print "<ins>"
	print "<addr>"+cur_addr+"</addr>"
	print "<op>"+ins_asm+"</op>"
	print "</ins>"

if seen_func == True:
	print "</func>"

	# print  "<bytes>"+ins_bytes+"</bytes>"
	#<td>"+ins_bytes+"</td>
	#table_row = "<tr><td class=\"addr\">"+cur_addr+"</td><td class=\"ins\">"+ins_asm+"</td></tr>"
	#print table_row

print "</appcov>"

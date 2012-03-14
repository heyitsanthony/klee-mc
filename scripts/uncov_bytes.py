#!/usr/bin/python
#
# Expects output from -print-new-regs in klee.
# Format example:
# [UNCOV] 0x7fb82f65ace0-0x7fb82f65ace8 : __read+0x10
#
import sys
import os

if sys.argv[1] == '-':
	f = sys.stdin
else:
	f = open(sys.argv[1])
uncov = dict()
uncov_bytes = set()
func_uncov = dict()
for l in f:
	l = l.rstrip()
	v = l.split(' ')
	addrs = v[1].split('-')
	if len(addrs) < 2:
		continue
	(addr_lo, addr_hi) = addrs
	(addr_lo, addr_hi) = (int(addr_lo, 16), int(addr_hi, 16))
	uncov[addr_lo] = (addr_hi, v[3])

	func_name = v[3].split('+')[0]
	if not func_name in func_uncov:
		func_uncov[func_name] = set()

	for byte_addr in range(addr_lo, addr_hi):
		uncov_bytes.add(byte_addr)
		func_uncov[func_name].add(byte_addr)

	
f.close()



print 'Total Code Bytes: ' + str(len(uncov_bytes))

mb_buckets = dict()
x = 0
for addr_lo in uncov.keys():
	mb_aligned = (1024*1024)*(addr_lo / (1024*1024))
	if not mb_aligned in mb_buckets:
		mb_buckets[mb_aligned] = set()

	(addr_hi, name) = uncov[addr_lo]
	byte_c = addr_hi - addr_lo

	for byte_addr in range(addr_lo, addr_hi):
		mb_buckets[mb_aligned].add(byte_addr)

# preload pretty idx
mb_pretty_idx = dict()
for k in mb_buckets.keys():
	mb_pretty_idx[k] = hex(k)

# load sshot pretty names
if len(sys.argv) > 2:
	mapf = open(sys.argv[2] + '/mapinfo')
	for l in mapf.readlines():
		spaced = l.split(' ')
		(addr_lo, addr_hi) = map(
			lambda x : int(x,16), spaced[0].split('-'))
		libname = spaced[3]

		# round down to meet alignment
		addr_lo_down = (1024*1024)*(addr_lo / (1024*1024))
		for k in mb_buckets.keys():
			if k < addr_lo_down or k > addr_hi:
				continue
			mb_pretty_idx[k] = libname[:-1]

	mapf.close()

# sum by labels
print_dict=dict()
for k in mb_buckets.keys():
	if mb_pretty_idx[k] not in print_dict:
		print_dict[mb_pretty_idx[k]] = 0

	print_dict[mb_pretty_idx[k]] = print_dict[mb_pretty_idx[k]] + len(mb_buckets[k])

for k in print_dict:
	print k+" "+str(print_dict[k])

print "================="
for k in sorted(func_uncov.keys()):
	print k+": "+str(len(func_uncov[k]))

#!/usr/bin/python

import gzip
import sys

class SMTState:
	def __init__(self):
		self.assumptions = []
		self.arrays = []

	@staticmethod
	def fromFiles(fnames):
		s = SMTState()
		for f in fnames:
			s.merge(SMTState.fromFile(f))
		return s

	@staticmethod
	def fromFile(fname):
		s = SMTState()
		s.readFromFile(fname)
		return s

	def copy(self):
		s = SMTState()
		s.assumptions = self.assumptions
		s.arrays = self.arrays
		return s

	# merge arrays from s2 into s1
	# this is prepping for individually testing every
	# assumption in s2
	def mergeArrays(self, s):
		new_arrays = list(set(self.arrays + s.arrays))

		# nothing added?
		if len(new_arrays) == len(self.arrays):
			return

		self.arrays = new_arrays
		for a in s.assumptions:
			if '[8])\n(= (select const_arr' in a:
				self.assumptions.append(a)
				continue

			if '[8])\n(= (select simpl_arr' in a:
				self.assumptions.append(a)

	def andAssumptions(self):
		andexpr = = '(and ' + ('(and '.join(self.assumptions))
		andexpr += ' true '
		andexpr += ')'*self.assumptions
		return andexpr


	def mergeOr(self, s):
		lhs = self.andAssumptions()
		rhs = s.andAssumptions()
		self.arrays = list(set(self.arrays + s.arrays))
		self.assumptions = ['(or ' + lhs + ' ' + rhs + ')']


	def merge(self, s):
		self.arrays = list(set(self.arrays + s.arrays))
		self.assumptions = list(set(self.assumptions + s.assumptions))

	# fills up assumptions and arrays
	def readFromFile(self, fname):

		if fname[-3:] == ".gz":
			f = gzip.open(fname, 'rb')
		else:
			f = open(fname, 'r')

		states = { ':assumption\n' : 1, ':extrafuns\n' : 2 }

		# initialization state
		cur_state = 0
		cur_chunk = ''

		for line in f:
			if not line in states:
				# no new assumption or extrafun?
				# append to query string
				cur_chunk = cur_chunk + line
				continue

			if cur_state == 1:
				self.assumptions.append(cur_chunk)
			elif cur_state == 2:
				self.arrays.append(cur_chunk.rstrip())
			cur_chunk = ''

			cur_state = states[line]

	def writeXChk(self, fname, formula):
		f = open(fname, 'w')
		f.write('(\n')
		f.write(
			'benchmark XCHK.smt\n' +
			':logic QF_AUFBV\n' +
			':source { kleemc-xchk }\n' +
			':category {industrial}\n')

		for a in self.arrays:
			f.write(':extrafuns\n')
			f.write(a + '\n')

		for a in self.assumptions:
			f.write(':assumption\n')
			f.write(a)

		f.write(':formula\n')
		f.write(formula)
		f.write(')\n')

		f.close()

	def cexFormula(self):
		assert len(self.assumptions) > 0

		out = '(not \n'
		for a in self.assumptions:
			out = ' (or ' + a

		out = '(= bv1[1] bv0[1])'
		out = out + (')'*(len(self.assumptions)+1))
		return out

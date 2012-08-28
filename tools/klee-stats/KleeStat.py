# represents a single stat entry from run.stats
class KleeStat:
	def __init__(self, ln):
		self.drec = eval(ln)

		# special case for straight-line code: report 100% branch coverage
		if self.drec['NumBranches'] == 0:
			self.drec['NumBranches'] = 1
			self.drec['FullBranches'] = 1

		self.row = None

	def getTableRec(self, Path):
		if not self.row is None: 
			return self.row
		self.row = self.__rd2row(Path)
		return self.row
	
	# for the awful table printing
	def __rd2row(self, Path):
		rd = self.drec
		Mem=rd['MemUsedKB']/1024.
		InsSum = rd['CoveredInstructions']+rd['UncoveredInstructions']
		if InsSum == 0:
			InsSum = 1
		AvgQC = int(rd['NumQueryConstructs']/max(1,rd['NumQueries']))
		return (
			Path,
			rd['Instructions'],
			rd['WallTime'],
			100.*rd['CoveredInstructions']/InsSum, 
			100.*(2*rd['FullBranches']+rd['PartialBranches'])/
				(2.*rd['NumBranches']),
			InsSum,
			100.*rd['SolverTime']/rd['WallTime'],
			rd['NumStates'],
			rd['NumStatesNC'],
			Mem,
			rd['NumQueries'],
			AvgQC,
			100.*rd['CexCacheTime']/rd['WallTime'],
			100.*rd['ForkTime']/rd['WallTime'])

	def dumpXML(self):
		print "<kstats>"
		for k in self.drec.keys():
			print "<" + k + ">" + str(self.drec[k]) + "</" + k + ">"
		print "</kstats>"

	def dumpJSON(self):
		print "{"
		for k in self.drec.keys():
			print  k + " : " + str(self.drec[k]) +  ","
		print "}"
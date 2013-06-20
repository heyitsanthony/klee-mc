//===-- InstructionInfoTable.cpp ------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Internal/Module/InstructionInfoTable.h"

#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/Linker.h>
#include <llvm/IR/Module.h>
#include <llvm/Assembly/AssemblyAnnotationWriter.h>
#include <llvm/Support/CFG.h>
#include <llvm/Support/InstIterator.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/DebugInfo.h>

#include "static/Sugar.h"

#include <map>
#include <string>

using namespace llvm;
using namespace klee;

class InstructionToLineAnnotator : public llvm::AssemblyAnnotationWriter
{
public:
	void emitInstructionAnnot(
		const Instruction *i, llvm::formatted_raw_ostream &os)
	{
		os << "%%%";
		os << (uintptr_t) i;
	}
};

static void buildInstructionToLineMap(
	Module *m,
	std::map<const Instruction*, unsigned> &out)
{
	unsigned count = 1;
	foreach (mit, m->begin(), m->end()) {
		foreach (fit, mit->begin(), mit->end()) {
			foreach (bit, fit->begin(),fit->end()) {
				Instruction * i = &*bit;
				out.insert(std::make_pair(i,count));
				++count;
			}
		}
	}
}

static std::string getDSPIPath(DILocation Loc)
{
	std::string dir = Loc.getDirectory();
	std::string file = Loc.getFilename();
	if (dir.empty()) {
		return file;
	} else if (*dir.rbegin() == '/') {
		return dir + file;
	} else {
		return dir + "/" + file;
	}
}

bool InstructionInfoTable::getInstructionDebugInfo(
	const llvm::Instruction *I,
	const std::string *&File,
	unsigned &Line)
{
	MDNode *N;

	if (!(N = I->getMetadata("dbg"))) {
		return false;
	}

	DILocation	Loc(N);
	std::string	s(getDSPIPath(Loc));
	File = internString(getDSPIPath(Loc));
	Line = Loc.getLineNumber();
	return true;
}


/* XXX VOMIT VOMIT PUKE */
InstructionInfoTable::InstructionInfoTable(Module *m)
: dummyString("")
, dummyInfo(0, dummyString, 0, 0)
{
	std::map<const Instruction*, unsigned> lineTable;
	unsigned id = 0;

	buildInstructionToLineMap(m, lineTable);
	foreach (fnIt, m->begin(), m->end()) {
		addFunction(lineTable, id, fnIt);
	}
}

void InstructionInfoTable::addFunction(
	std::map<const llvm::Instruction*, unsigned>& lineTable,
	unsigned& id,
	llvm::Function* fnIt)
{
	const std::string *initialFile = &dummyString;
	unsigned initialLine = 0;

	typedef std::map<BasicBlock*, std::pair<const std::string*,unsigned> >
		sourceinfo_ty;

	// It may be better to look for the closest stoppoint to the entry
	// following the CFG, but it is not clear that it ever matters in
	// practice.
	foreach (it, inst_begin(fnIt), inst_end(fnIt)) {
		if (getInstructionDebugInfo(&*it, initialFile, initialLine))
			break;
	}

	sourceinfo_ty sourceInfo;
	foreach (bbIt, fnIt->begin(), fnIt->end()) {
		std::pair<sourceinfo_ty::iterator, bool>	res;

		res = sourceInfo.insert(
			std::make_pair(
				bbIt,
				std::make_pair(
					initialFile,
					initialLine)));

		if (!res.second)
			continue;

		std::vector<BasicBlock*> worklist;
		worklist.push_back(bbIt);

		do {
			BasicBlock *bb = worklist.back();
			worklist.pop_back();

			sourceinfo_ty::iterator si = sourceInfo.find(bb);
			assert(si != sourceInfo.end());

			const std::string *file = si->second.first;
			unsigned line = si->second.second;

			foreach (it, bb->begin(), bb->end()) {
				Instruction *instr = it;
				unsigned assemblyLine = 0;
				std::map<const Instruction*, unsigned>::const_iterator
					ltit;

				ltit =  lineTable.find(instr);
				if (ltit != lineTable.end())
					assemblyLine = ltit->second;

				getInstructionDebugInfo(instr, file, line);
				infos.insert(
					std::make_pair(
						instr,
						InstructionInfo(
							id++, *file, line,
							assemblyLine)));
			}

			foreach (it, succ_begin(bb), succ_end(bb)) {
				if (sourceInfo.insert(std::make_pair(
					*it,
					std::make_pair(file, line))).second)
				{
					worklist.push_back(*it);
				}
			}
		} while (!worklist.empty());
	}
}

InstructionInfoTable::~InstructionInfoTable()
{
	foreach(it, internedStrings.begin(), internedStrings.end())
		delete *it;
}

const std::string *InstructionInfoTable::internString(std::string s)
{
	std::set<const std::string *, ltstr>::iterator it;

	it = internedStrings.find(&s);
	if (it != internedStrings.end())
		return *it;

	std::string *interned = new std::string(s);
	internedStrings.insert(interned);
	return interned;
}

unsigned InstructionInfoTable::getMaxID() const { return infos.size(); }

const InstructionInfo &
InstructionInfoTable::getInfo(const Instruction *inst) const
{
	std::map<const llvm::Instruction*, InstructionInfo>::const_iterator it;
	it = infos.find(inst);
	return (it == infos.end()) ? dummyInfo : it->second;
}

const InstructionInfo&
InstructionInfoTable::getFunctionInfo(const Function *f) const
{
	if (f->isDeclaration()) {
		return dummyInfo;
	}
	return getInfo(f->begin()->begin());
}

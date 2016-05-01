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
//#include <llvm/Linker.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/AssemblyAnnotationWriter.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/IR/DebugInfo.h>

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
	Module &m,
	std::map<const Instruction*, unsigned> &out)
{
	unsigned count = 1;
	for (auto &fn : m) {
	for (auto &bb : fn) {
	for (auto &ii : bb) {
		out.insert(std::make_pair(&ii, count));
		++count;
	}
	}
	}
}

static std::string getPath(const std::string& dir, const std::string& file)
{
	if (dir.empty()) {
		return file;
	} else if (*dir.rbegin() == '/') {
		return dir + file;
	}
	return dir + "/" + file;
}

bool InstructionInfoTable::getInstructionDebugInfo(
	const llvm::Instruction &I,
	const std::string *&File,
	unsigned &Line)
{
	const DebugLoc	&dl(I.getDebugLoc());

	if (!dl) return false;
	Line = dl.getLine();

	auto scope = cast<DIScope>(dl.getScope());
	File = internString(getPath(	scope->getDirectory(),
					scope->getFilename()));

	return true;
}


/* XXX VOMIT VOMIT PUKE */
InstructionInfoTable::InstructionInfoTable(Module& m)
: dummyString("")
, dummyInfo(0, dummyString, 0, 0)
{
	std::map<const Instruction*, unsigned> lineTable;
	unsigned id = 0;

	buildInstructionToLineMap(m, lineTable);
	for (auto &fn : m) addFunction(lineTable, id, &fn);
}

void InstructionInfoTable::addFunction(
	std::map<const llvm::Instruction*, unsigned>& lineTable,
	unsigned& id,
	llvm::Function* fnIt)
{
	const std::string *initialFile = &dummyString;
	unsigned initialLine = 0;

	typedef std::map<const BasicBlock*, std::pair<const std::string*,unsigned> >
		sourceinfo_ty;

	// It may be better to look for the closest stoppoint to the entry
	// following the CFG, but it is not clear that it ever matters in
	// practice.
	foreach (it, inst_begin(fnIt), inst_end(fnIt)) {
		if (getInstructionDebugInfo(*it, initialFile, initialLine))
			break;
	}

	sourceinfo_ty sourceInfo;
	for (const auto &bbIt : *fnIt) {
		std::pair<sourceinfo_ty::iterator, bool>	res;

		res = sourceInfo.emplace(&bbIt, std::make_pair(initialFile, initialLine));
		if (!res.second)
			continue;

		std::vector<const BasicBlock*> worklist;
		worklist.emplace_back(&bbIt);

		do {
			auto bb = worklist.back();
			worklist.pop_back();

			sourceinfo_ty::iterator si = sourceInfo.find(bb);
			assert(si != sourceInfo.end());

			const std::string *file = si->second.first;
			unsigned line = si->second.second;

			foreach (it, bb->begin(), bb->end()) {
				const Instruction& instr = *it;
				unsigned assemblyLine = 0;

				auto ltit =  lineTable.find(&instr);
				if (ltit != lineTable.end())
					assemblyLine = ltit->second;

				getInstructionDebugInfo(instr, file, line);
				infos.emplace(
					&instr,
					InstructionInfo(id++, *file, line, assemblyLine));
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
	auto it = infos.find(inst);
	return (it == infos.end()) ? dummyInfo : it->second;
}

const InstructionInfo&
InstructionInfoTable::getFunctionInfo(const Function *f) const
{
	if (f->isDeclaration()) {
		return dummyInfo;
	}
	return getInfo(&(*(f->begin()->begin())));
}

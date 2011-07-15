//===-- InstructionInfoTable.cpp ------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Internal/Module/InstructionInfoTable.h"

#include "llvm/Function.h"
#include "llvm/Instructions.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Linker.h"
#include "llvm/Module.h"
#include "llvm/Assembly/AsmAnnotationWriter.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/ValueTracking.h"

#include "static/Sugar.h"

#include <map>
#include <string>

using namespace llvm;
using namespace klee;

class InstructionToLineAnnotator : public llvm::AssemblyAnnotationWriter {
public:
  void emitInstructionAnnot(const Instruction *i, llvm::raw_ostream &os) {
    os << "%%%" << (uintptr_t) i;
  }
};
        
static void buildInstructionToLineMap(
	Module *m,
	std::map<const Instruction*, unsigned> &out)
{
	InstructionToLineAnnotator	a;
	std::string			str;
	llvm::raw_string_ostream	os(str);


	m->print(os, &a);
	os.flush();

	unsigned line = 1;
	for (const char *s = str.c_str(); *s; s++) {
		char			*end;
		unsigned long long	value;

		if (*s != '\n') continue;

		line++;
		if (strncmp(&s[1], "%%%", 3) != 0)
			continue;

		s += 4;
		value = strtoull(s, &end, 10);
		if (end != s) {
			out.insert(std::make_pair(
				(const Instruction*) value, line));
		}
		s = end;
	}
}

static std::string getDSPIPath(DbgStopPointInst *dspi)
{
	std::string	dir, file;
	bool		res;

	res = GetConstantStringInfo(dspi->getDirectory(), dir);
	assert(res && "GetConstantStringInfo failed");

	res = GetConstantStringInfo(dspi->getFileName(), file);
	assert(res && "GetConstantStringInfo failed");

	if (dir.empty()) return file;
	if (*dir.rbegin() == '/') return dir + file;

	return dir + "/" + file;
}

InstructionInfoTable::InstructionInfoTable(Module *m) 
: dummyString("")
, dummyInfo(0, dummyString, 0, 0)
{
  unsigned id = 0;
  std::map<const Instruction*, unsigned> lineTable;
  buildInstructionToLineMap(m, lineTable);

  foreach (fnIt, m->begin(), m->end()) {
    const std::string *initialFile = &dummyString;
    unsigned initialLine = 0;

    // It may be better to look for the closest stoppoint to the entry
    // following the CFG, but it is not clear that it ever matters in
    // practice.
    foreach (it, inst_begin(fnIt), inst_end(fnIt)) {
      if (DbgStopPointInst *dspi = dyn_cast<DbgStopPointInst>(&*it)) {
        initialFile = internString(getDSPIPath(dspi));
        initialLine = dspi->getLine();
        break;
      }
    }
    
    typedef std::map<BasicBlock*, std::pair<const std::string*,unsigned> > 
      sourceinfo_ty;
    sourceinfo_ty sourceInfo;
    foreach (bbIt, fnIt->begin(), fnIt->end()) {
      std::pair<sourceinfo_ty::iterator, bool>
        res = sourceInfo.insert(std::make_pair(bbIt,
                                               std::make_pair(initialFile,
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
          std::map<const Instruction*, unsigned>::const_iterator ltit;
	  
	  ltit = lineTable.find(instr);
          if (ltit!=lineTable.end())
            assemblyLine = ltit->second;
          if (DbgStopPointInst *dspi = dyn_cast<DbgStopPointInst>(instr)) {
            file = internString(getDSPIPath(dspi));
            line = dspi->getLine();
          }
          infos.insert(std::make_pair(instr,
                                      InstructionInfo(id++,
                                                      *file,
                                                      line,
                                                      assemblyLine)));        
        }
        
	foreach (it, succ_begin(bb), succ_end(bb)) {
          if (sourceInfo.insert(
	  	std::make_pair(
			*it,
			std::make_pair(file, line))).second)
          {
            worklist.push_back(*it);
	  }
        }
      } while (!worklist.empty());
    }
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
  if (it!=internedStrings.end()) return *it;

  std::string *interned = new std::string(s);
  internedStrings.insert(interned);
  return interned;
}

unsigned InstructionInfoTable::getMaxID() const {
  return infos.size();
}

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
	if (f->isDeclaration())
		return dummyInfo;
	return getInfo(f->begin()->begin());
}

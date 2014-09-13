//===-- StatsTracker.h ------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_STATSTRACKER_H
#define KLEE_STATSTRACKER_H

#include <iostream>
#include <set>
#include <list>

namespace llvm {
  class BranchInst;
  class Function;
  class Instruction;
}

namespace klee {
class ExecutionState;
class Executor;
class InstructionInfoTable;
class InterpreterHandler;
class KInstruction;
class KBrInstruction;
struct StackFrame;
class KModule;
class KFunction;

class StatsTracker
{
    friend class WriteStatsTimer;
    friend class WriteIStatsTimer;

    Executor &executor;
    std::string objectFilename;

    std::ostream *statsFile, *istatsFile;
    double startWallTime;

    unsigned numBranches;
    unsigned fullBranches, partialBranches;


    bool updateMinDistToUncovered;

private:
    class TimeAmountFormat;
    void trackInstTime(ExecutionState& es);
    void stepInstUpdateFrame(ExecutionState &es);
    void updateStateStatistics(uint64_t addend);
    void writeStatsLine();
    void writeIStats();
    void writeInstIStat(
    	std::ostream& of, uint64_t istatsMask,
	std::string& sourceFile,
	CallSiteSummaryTable& css,
	llvm::Instruction *instr);

    const KModule *km;
    std::set<std::string> excludeNames;

protected:
    StatsTracker(Executor &_executor,
    		const KModule* km,
		std::string _objectFilename,
                const std::vector<std::string> &excludeCovFiles);

	static bool useStatistics(void);
public:
	static StatsTracker* create(
		Executor &_executor,
		const KModule* km,
		std::string _objectFilename,
		const std::vector<std::string> &excludeCovFiles);
    ~StatsTracker();

    void addKFunction(KFunction*);

    // called after a new StackFrame has been pushed (for callpath tracing)
    void framePushed(ExecutionState &es, StackFrame *parentFrame);

    // called after a StackFrame has been popped
    void framePopped(ExecutionState &es);

    // called when some side of a branch has been visited. it is
    // imperative that this be called when the statistics index is at
    // the index for the branch itself.
    void markBranchVisited(
	KBrInstruction* kbr,
	ExecutionState *visitedTrue,
	ExecutionState *visitedFalse);

    // called when execution is done and stats files should be flushed
    void done();

    // process stats for a single instruction step, es is the state
    // about to be stepped
    void stepInstruction(ExecutionState &es);

    /// Return time in seconds since execution start.
    double elapsed();

    void computeReachableUncovered();

    uint64_t getLastNewInstruction(void) const
    { return lastCoveredInstruction; }

    void setUpdateMinDist(void);
private:
    void computeReachableUncoveredInit(void);
    void computeCallTargets(llvm::Function* f);
    void initMinDistToReturn(
	llvm::Function* fnIt,
	std::vector<llvm::Instruction* >& instructions);
    bool computePathsInit(std::vector<llvm::Instruction*>& insts);
    bool computePaths(std::vector<llvm::Instruction*>& insts);
    static bool init;
    uint64_t lastCoveredInstruction;
};

uint64_t computeMinDistToUncovered(
	const KInstruction *ki, uint64_t minDistAtRA);

}

#endif

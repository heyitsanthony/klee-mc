#ifndef KLEE_KFUNCTION_H
#define KLEE_KFUNCTION_H

#include <set>
#include <map>

namespace llvm
{
class Function;
class BasicBlock;
class Value;
class Instruction;
}

namespace klee
{
class KModule;
class KInstruction;

class KFunction
{
public:
	typedef std::set<const KFunction*>::const_iterator	exit_iter_ty;
	llvm::Function *function;

	unsigned numArgs;
	unsigned numRegisters;
	unsigned numInstructions;

	unsigned callcount;
	KInstruction **instructions;
	llvm::Value **arguments;

	/// Whether instructions in this function should count as
	/// "coverable" for statistics and search heuristics.
	bool trackCoverage;

private:
	std::map<llvm::BasicBlock*, unsigned> basicBlockEntry;
	std::set<const KFunction*>	exits_seen;


	KFunction(const KFunction&);
	KFunction &operator=(const KFunction&);
	void addInstruction(
		KModule* km,
		llvm::Instruction* inst,
		const std::map<llvm::Instruction*, unsigned>& registerMap,
		unsigned int ins_num);
public:
	explicit KFunction(llvm::Function*, KModule *);
	~KFunction();

	unsigned getArgRegister(unsigned index) { return index; }
	llvm::Value* getValueForRegister(unsigned reg);
	unsigned getBasicBlockEntry(llvm::BasicBlock* bb) const
	{ return basicBlockEntry.find(bb)->second; }

	void addExit(const KFunction* ex) { exits_seen.insert(ex); }
	exit_iter_ty beginExits(void) const { return exits_seen.begin(); }
	exit_iter_ty endExits(void) const { return exits_seen.end(); }
};
}

#endif

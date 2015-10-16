#ifndef KLEE_KFUNCTION_H
#define KLEE_KFUNCTION_H

#include <set>
#include <map>
#include <string>
#include <stdint.h>
#include <unordered_map>

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
	bool pathCommitted;	/* did we write a path out with this func? */
	bool isSpecial;		/* uses "special" semantics on enter/exit */
private:
	typedef std::unordered_map<llvm::BasicBlock*, unsigned>
		bbentry_ty;
	bbentry_ty basicBlockEntry;
	std::set<const KFunction*>	exits_seen;
	uint64_t			enter_c;
	uint64_t			exit_c;
	const std::string		*mod_name;

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

	void setModName(const std::string& s) { mod_name = &s; }
	std::string getModName(void) const
	{ if (mod_name) return *mod_name; return std::string(""); }

	unsigned getArgRegister(unsigned index) { return index; }
	llvm::Value* getValueForRegister(unsigned reg);
	unsigned getBasicBlockEntry(llvm::BasicBlock* bb) const
	{ return basicBlockEntry.find(bb)->second; }

	void addExit(const KFunction* ex) { exits_seen.insert(ex); }
	exit_iter_ty beginExits(void) const { return exits_seen.begin(); }
	exit_iter_ty endExits(void) const { return exits_seen.end(); }
	unsigned getUncov(void) const;
	unsigned getCov(void) const;
	std::string getCovStr(void) const;

	void incExits(void) { exit_c++; }
	void incEnters(void) { enter_c++; }
	uint64_t getNumExits(void) const { return exit_c; }
	uint64_t getNumEnters(void) const { return enter_c; }

	static unsigned getClock(void) { return inst_clock; }
	unsigned getTick(void) const { return inst_tick; }

	bool isCommitted(uint32_t idx = (~0 - 1)) const
	{ return (path_commit_tick <= idx); }

	void markCommitted(uint32_t idx)
	{ if (idx > path_commit_tick) return; path_commit_tick = idx; }
private:
	static unsigned		inst_clock;
	unsigned		inst_tick;
	uint32_t		path_commit_tick;
};
}

#endif

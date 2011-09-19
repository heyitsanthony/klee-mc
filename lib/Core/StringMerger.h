#ifndef STRINGMERGER_H
#define STRINGMERGER_H

#include "Executor.h"
#include "Searcher.h"

namespace klee
{
class Expr;
typedef std::map<std::string, std::vector<const Expr* > > ArrExprMap;
class StringMerger : public Searcher
{
public:
	StringMerger(Searcher* inBaseSearch) : baseSearcher(inBaseSearch) {}
	virtual ~StringMerger();

	virtual ExecutionState &selectState(bool allowCompact);

	virtual void update(ExecutionState *current, const States s);
	virtual bool empty() const;

private:
	void subsume(
		ExecutionState* current,
		const std::string& arr_name,
		const std::vector<const Expr*> arr_exprs,
		ExeStateSet& subsumed);
	bool isArrCmp(
		const Expr* expr, std::string& arrName,
		uint64_t& arr_idx, uint8_t& cmp_val) const;
	void buildArrExprList(ExecutionState* s, ArrExprMap& arr_exprs) const;
	int runLength(const std::vector<const Expr*>& exprs) const;

	void addState(ExecutionState* s);
	void removeState(ExecutionState* s);
	void removeAscenders(
		ExecutionState* current, ExeStateSet& removed);

	ExeStateSet states;
	ExeStateSet ascendingStates;
	std::map<std::string, ExeStateSet*> arrStates;
	Searcher* baseSearcher;
};
}

#endif

#ifndef FILTERSEARCHER_H
#define FILTERSEARCHER_H

#include "klee/ExecutionState.h"
#include "Executor.h"
#include "Searcher.h"

namespace klee {
class FilterSearcher : public Searcher
{
public:
	ExecutionState &selectState(bool allowCompact);
	FilterSearcher(Executor& exe, Searcher* _searcher_base);
	virtual ~FilterSearcher(void) { delete searcher_base; }

	virtual Searcher* createEmpty(void) const
	{ return new FilterSearcher(
		exe,
		searcher_base->createEmpty()); }

	void update(ExecutionState *current, States s);

	bool empty(void) const
	{ return (scheduled.size() + blacklisted.size()) == 0; }

	void printName(std::ostream &os) const
	{
		os << "<FilterSearcher>\n";
		searcher_base->printName(os);
		os << "</FilterSearcher>\n";
	}

private:
	void recoverBlacklisted(void);
	bool isBlacklisted(ExecutionState& es) const;

	std::set<std::string>		blacklist_strs;
	mutable std::set<llvm::Function*>	blacklist_f;
	mutable std::set<llvm::Function*>	whitelist_f;

	ExeStateSet	scheduled;
	ExeStateSet	blacklisted;
	Searcher	*searcher_base;
	uint64_t	last_uncovered;
	Executor	&exe;
};
}

#endif

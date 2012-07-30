#ifndef FILTERSEARCHER_H
#define FILTERSEARCHER_H

#include "klee/ExecutionState.h"
#include "../Core/Searcher.h"

namespace klee
{
class Executor;
class FilterSearcher : public Searcher
{
public:
	ExecutionState &selectState(bool allowCompact);
	FilterSearcher(
		Executor& exe,
		Searcher* _searcher_base,
		const char* filter_name = "filtered_funcs.txt");
	virtual ~FilterSearcher(void) { delete searcher_base; }

	virtual Searcher* createEmpty(void) const
	{ return new FilterSearcher(
		exe,
		searcher_base->createEmpty()); }

	void update(ExecutionState *current, States s);

	bool empty(void) const
	{ return (scheduled.size() + blacklisted.size()) == 0; }

	virtual void printName(std::ostream &os) const
	{
		os << "<FilterSearcher>\n";
		searcher_base->printName(os);
		os << "</FilterSearcher>\n";
	}

	const char* getFilterFileName(void) const { return filter_fname; }

protected:
	virtual bool isBlacklisted(ExecutionState& es) const;
	Searcher* getSearcherBase(void) const { return searcher_base; }
	Executor& getExe(void) const { return exe; }
private:
	bool recheckListing(ExecutionState* current);
	void recoverBlacklisted(void);

	std::set<std::string>			blacklist_strs;
	mutable std::set<llvm::Function*>	blacklist_f;
	mutable std::set<llvm::Function*>	whitelist_f;

	ExeStateSet	scheduled;
	ExeStateSet	blacklisted;
	Searcher	*searcher_base;
	uint64_t	last_uncovered;
	Executor	&exe;
	const char*	filter_fname;
};

class WhitelistFilterSearcher : public FilterSearcher
{
public:
	WhitelistFilterSearcher(
		Executor& exe,
		Searcher* _searcher_base,
		const char* filter_name = "whitelist_funcs.txt")
	: FilterSearcher(exe, _searcher_base, filter_name)
	{
		std::cerr	<< "[Whitelist] Whitelisting file="
				<< filter_name << '\n';
	}

	virtual ~WhitelistFilterSearcher() {}

	virtual Searcher* createEmpty(void) const
	{ return new WhitelistFilterSearcher(
		getExe(),
		getSearcherBase()->createEmpty(),
		getFilterFileName()); }

	virtual void printName(std::ostream &os) const
	{
		os << "<WhitelistFilterSearcher>\n";
		getSearcherBase()->printName(os);
		os << "</WhitelistFilterSearcher>\n";
	}
protected:
	virtual bool isBlacklisted(ExecutionState& es) const
	{ return !FilterSearcher::isBlacklisted(es); }
};
}

#endif

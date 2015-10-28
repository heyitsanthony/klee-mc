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
	FilterSearcher(
		Executor& exe,
		Searcher* _searcher_base,
		const char* filter_name = "filtered_funcs.txt");

	Searcher* createEmpty(void) const override {
		return new FilterSearcher(
			exe,
			searcher_base->createEmpty()); }

	ExecutionState *selectState(bool allowCompact) override;
	void update(ExecutionState *current, States s) override;

	void printName(std::ostream &os) const override {
		os << "<FilterSearcher>\n";
		searcher_base->printName(os);
		os << "</FilterSearcher>\n";
	}

	const char* getFilterFileName(void) const { return filter_fname; }

protected:
	virtual bool isBlacklisted(ExecutionState& es) const;
	Searcher* getSearcherBase(void) const { return searcher_base.get(); }
	Executor& getExe(void) const { return exe; }

private:
	bool recheckListing(ExecutionState* current);
	void recoverBlacklisted(void);

	std::set<std::string>			blacklist_strs;
	mutable std::set<llvm::Function*>	blacklist_f;
	mutable std::set<llvm::Function*>	whitelist_f;

	ExeStateSet	scheduled;
	ExeStateSet	blacklisted;
	usearcher_t 	searcher_base;
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

	Searcher* createEmpty(void) const override {
		return new WhitelistFilterSearcher(
			getExe(),
			getSearcherBase()->createEmpty(),
			getFilterFileName()); }

	void printName(std::ostream &os) const override	{
		os << "<WhitelistFilterSearcher>\n";
		getSearcherBase()->printName(os);
		os << "</WhitelistFilterSearcher>\n";
	}

protected:
	bool isBlacklisted(ExecutionState& es) const override {
		return !FilterSearcher::isBlacklisted(es);
	}
};
}

#endif

#include <llvm/Function.h>

#include <iostream>
#include <fstream>
#include "klee/Common.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Module/KFunction.h"
#include "../Core/Executor.h"
#include "static/Sugar.h"
#include "FilterSearcher.h"

using namespace klee;

static const char* default_filters[] =
{
	"LoadDelegateList",
	"ConfigureFileToStringInfo",
	"ThrowMagickException",
	"_ZNK4llvm10error_code7messageEv",
	"mpc_reader_exit_stdio",
	"_ZNK10GException6perrorEv",
	"alphasort",
	"qsort",
	"rpl_getopt_long",
	"__tz_convert",
	NULL
};

FilterSearcher::FilterSearcher(
	Executor& _exe,
	Searcher* _searcher_base,
	const char* _filter_fname)
: searcher_base(_searcher_base)
, exe(_exe)
, filter_fname(_filter_fname)
{
	std::ifstream	ifs(filter_fname);

	if (ifs.good()) {
		std::string	s;
		std::cerr << "[Blacklist] Blacklisting file="
			<< filter_fname << '\n';
		while (ifs >> s)
			blacklist_strs.insert(s);
		return;
	}

	klee_warning("Could not find filter file; using defaults.");
	for (unsigned i = 0; default_filters[i] != NULL; i++)
		blacklist_strs.insert(default_filters[i]);
}

ExecutionState& FilterSearcher::selectState(bool allowCompact)
{
	ExecutionState	*es;

	do {
		if (searcher_base->empty()) {
			recoverBlacklisted();
			es = &searcher_base->selectState(allowCompact);
			break;
		}

		es = &searcher_base->selectState(allowCompact);

		/* another scheduler may have run this andv>
		 * suddenly blacklisted it! */
	} while (recheckListing(es));

	return *es;
}

bool FilterSearcher::isBlacklisted(ExecutionState& es) const
{
	foreach (it, es.stack.begin(), es.stack.end()) {
		KFunction	*kf;
		llvm::Function	*f;
		std::string	s;
		int		plus_off;

		kf = (*it).kf;
		if (kf == NULL)
			continue;

		f = kf->function;
		if (f == NULL)
			continue;

		if (whitelist_f.count(f))
			continue;

		if (blacklist_f.count(f))
			return true;

		/* slow path */
		s = exe.getKModule()->getPrettyName(f);
		if (s.empty()) s = f->getName().str();
		plus_off = s.find('+');
		if (plus_off != 0) {
			s = s.substr(0, plus_off);
		}

		if (blacklist_strs.count(s)) {
			blacklist_f.insert(f);
			return true;
		}

		whitelist_f.insert(f);
	}

	return false;
}

void FilterSearcher::recoverBlacklisted(void)
{
	ExecutionState		*recovered_state;
	ExeStateSet::iterator	it;

	assert (scheduled.empty());

	if (blacklisted.empty())
		return;

	it = blacklisted.begin();
	recovered_state = *it;

	searcher_base->addState(recovered_state);
	scheduled.insert(recovered_state);
	blacklisted.erase(it);
}

bool FilterSearcher::recheckListing(ExecutionState* es)
{
	/* scheduled, but should be blacklisted */
	if (scheduled.count(es) && isBlacklisted(*es)) {
		std::cerr
			<< "[Blacklist] Rechecked to BL. ST="
			<< (void*)es << '\n';
		searcher_base->removeState(es);
		scheduled.erase(es);
		blacklisted.insert(es);
		std::cerr << "[Blacklist] Recheck done.\n";
		return true;
	}

	/* blacklisted, but should be scheduled */
	if (blacklisted.count(es) && !isBlacklisted(*es)) {
		searcher_base->addState(es);
		blacklisted.erase(es);
		scheduled.insert(es);

		std::cerr
			<< "[Blacklist] Rechecked out of BL. ST="
			<< (void*)es << '\n';

		return true;
	}

	return false;
}

void FilterSearcher::update(ExecutionState *current, States s)
{
	ExeStateSet	addSet, rmvSet;

	foreach (it, s.getAdded().begin(), s.getAdded().end()) {
		ExecutionState	*es = *it;

		if (isBlacklisted(*es) == false)
			addSet.insert(es);
		else {
			std::cerr
				<< "[Blacklist] Added to BL. ST="
				<< (void*)es << '\n';
			blacklisted.insert(es);
		}
	}

	foreach (it, s.getRemoved().begin(), s.getRemoved().end()) {
		ExecutionState	*es = *it;

		if (scheduled.count(es))
			rmvSet.insert(es);
		else
			blacklisted.erase(es);
	}


	foreach (it, addSet.begin(), addSet.end())
		scheduled.insert(*it);
	foreach (it, rmvSet.begin(), rmvSet.end())
		scheduled.erase(*it);

	searcher_base->update(current, States(addSet, rmvSet));

	/* if current state is blacklisted, remove it */
	if (current != NULL && !s.getRemoved().count(current))
		recheckListing(current);

	/* pull a state from the black list if nothing left to schedule */
	if (searcher_base->empty())
		recoverBlacklisted();
}

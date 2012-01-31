#include <iostream>
#include <llvm/Function.h>
#include "klee/Internal/Module/KFunction.h"
#include "static/Sugar.h"
#include "FilterSearcher.h"

using namespace klee;

FilterSearcher::FilterSearcher(Executor& _exe, Searcher* _searcher_base)
: searcher_base(_searcher_base)
, exe(_exe)
{
	blacklist_strs.insert("LoadDelegateList");
	blacklist_strs.insert("ConfigureFileToStringInfo");
	blacklist_strs.insert("ThrowMagickException");
//	blacklist_strs.insert("getch2");
//	blacklist_strs.insert("lavf_check_preferred_file");
	blacklist_strs.insert("_ZNK4llvm10error_code7messageEv");
	//blacklist_strs.insert("___printf_chk");
	//blacklist_strs.insert("_IO_file_doallocate_internal");
	blacklist_strs.insert("mpc_reader_exit_stdio");
	blacklist_strs.insert("_ZNK10GException6perrorEv");
	blacklist_strs.insert("alphasort");
	blacklist_strs.insert("qsort");
//	blacklist_strs.insert("poll_for_event");
}

ExecutionState& FilterSearcher::selectState(bool allowCompact)
{
	assert (searcher_base->empty() == false);
	return searcher_base->selectState(allowCompact);
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
		s = exe.getPrettyName(f);
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

void FilterSearcher::update(ExecutionState *current, States s)
{
	ExeStateSet	addSet, rmvSet;

	foreach (it, s.getAdded().begin(), s.getAdded().end()) {
		ExecutionState	*es = *it;

		if (isBlacklisted(*es) == false)
			addSet.insert(es);
		else
			blacklisted.insert(es);
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
	if (current != NULL && !s.getRemoved().count(current)) {
		if (isBlacklisted(*current)) {
			searcher_base->removeState(current);
			scheduled.erase(current);
			blacklisted.insert(current);
		}
	}

	/* pull a state from the black list if nothing left to schedule */
	if (searcher_base->empty()) {
		recoverBlacklisted();
	}
}

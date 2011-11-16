#include <assert.h>
#include <iostream>

#include "klee/ExecutionState.h"
#include "static/Sugar.h"
#include "XChkSearcher.h"

using namespace klee;

XChkSearcher::XChkSearcher(Searcher *in_base)
: last_selected(NULL)
, base(in_base)
{}

XChkSearcher::~XChkSearcher()
{
	delete base;
}

void XChkSearcher::updateHash(ExecutionState* s, unsigned hash)
{
	if (hash == 0)
		hash = s->addressSpace.hash();

	as_hashes[s] = hash;
}

void XChkSearcher::xchk(ExecutionState* s)
{
	as_hashes_ty::const_iterator	it;
	unsigned			h;

	it = as_hashes.find(s);
	assert (it != as_hashes.end() && "Never seen this state before??");

	h = s->addressSpace.hash();
	if (h != it->second) {
		std::cerr << "FAILED XCHK ON RESCHEDULE!! " <<
			last_selected << "->" << s << "\n";
		std::cerr
			<< "NEW HASH=" << h
			<< " ||| OLD HASH=" << it->second << "\n";

		std::cerr << "BAD ADDRSPACE DUMP:\n";
		s->addressSpace.print(std::cerr);
		std::cerr << "\n---\n";

		if (last_selected) {
			std::cerr << "LAST STATE ADDRSPACE DUMP:\n";
			last_selected->addressSpace.print(std::cerr);
			std::cerr << "\n";
		}

		assert (0 == 1 && "WHOOPS");
	}

	std::cerr << "XCHKSearch: XCHK OK! "
		<< last_selected << " -> " << s
		<< "\n";
}


ExecutionState& XChkSearcher::selectState(bool allowCompact)
{
	ExecutionState			&res(base->selectState(allowCompact));

	if (&res == last_selected)
		return res;

	xchk(&res);
	last_selected = &res;

	return res;
}

void XChkSearcher::update(ExecutionState *current, const States s)
{
	if (!s.getRemoved().empty()) {
		foreach(it, s.getRemoved().begin(), s.getRemoved().end()) {
			as_hashes.erase(*it);
			if (*it == last_selected)
				last_selected = NULL;
		}
	}

	if (!s.getAdded().empty()) {
		foreach(it, s.getAdded().begin(), s.getAdded().end()) {
			ExecutionState	*cur_s = *it;
			updateHash(cur_s);
		}
	}

	if (current) updateHash(current);

	base->update(current, s);
}

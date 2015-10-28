#include "klee/ExecutionState.h"
#include "klee/Internal/Module/KModule.h"
#include "RRPrSearcher.h"

#include "static/Sugar.h"

using namespace klee;

ExecutionState *RRPrSearcher::selectState(bool allowCompact)
{
	std::map<int, std::vector<ExecutionState*>>	prs;

	// compute all priorities
	for (auto es : states) {
		if (allowCompact || !es->isCompact()) {
			int	es_pr = pr->getPriority(*es);
			if (es_pr > cut_off) {
				auto	&v = prs[es_pr];
				v.push_back(es);
			}
		}
	}
	
	if (prs.empty()) {
		return nullptr;
	}

	// find priority following last_pr
	auto it = prs.upper_bound(last_pr);
	if (it == prs.end()) {
		it = prs.begin();
	}

	// remember last priority
	last_pr = it->first;
	const auto &v = it->second;
	if (v.empty()) return nullptr;

	std::cerr << "[RRPrSearcher] SELECTING: PR: " << last_pr
		<< " (" << v.size() << " in class)\n";
	return v[0];
}

void RRPrSearcher::update(ExecutionState *current, const States s)
{
	states.insert(s.getAdded().begin(), s.getAdded().end());
	for (auto rmv : s.getRemoved()) states.erase(rmv);
}

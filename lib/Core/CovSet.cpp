#include <assert.h>
#include "CovSet.h"
#include "klee/Internal/Module/KFunction.h"

#define COVSET_WATERMARK	8192
#define GC_WATERMARK		128

using namespace klee;

std::map<unsigned /* size*/, 
	std::set<std::shared_ptr<CovSet::covset_t>>> CovSet::all_sets;

CovSet::CovSet()
	: covered(std::make_shared<covset_t>())
{
}

CovSet::CovSet(const CovSet& cs)
{
	cs.flushPending();
	covered = cs.covered;
	assert (covered != nullptr);
}

void CovSet::insert(const KFunction* kf)
{
	if (kf->trackCoverage == false) {
		return;
	}

	pending.push_back(kf);
	if (pending.size() > COVSET_WATERMARK)
		flushPending();
}

void CovSet::commit(void) const
{
	flushPending();
	for (auto kf : *covered) {
		if (kf->pathCommitted) continue;
		const_cast<KFunction*>(kf)->pathCommitted = true;
	}
}

unsigned CovSet::numUncommitted(void) const
{
	unsigned uncommitted = 0;

	flushPending();
	for (auto kf : *covered) {
		if (kf->pathCommitted == false)
			uncommitted++;
	}

	return uncommitted;
}

void CovSet::flushPending(void) const
{
	covset_t	pending_set;

	for (auto kf : pending) {
		if (pending_set.count(kf) == 0 && covered->count(kf) == 0)
			pending_set.insert(kf);
	}
	pending.clear();

	// already covered all pending kf's?
	if (pending_set.empty())
		return;

	// create new set for all covered kf's
	auto new_set = std::make_shared<covset_t>(*covered);
	for (auto kf : pending_set) new_set->insert(kf);

	// totally new set size class?
	if (all_sets.count(new_set->size()) == 0) {
		all_sets[new_set->size()].insert(new_set);
		garbageCollect(covered);
		covered = new_set;
		return;
	}

	// scan sets in size class
	auto &candidate_sets = all_sets.find(new_set->size())->second;
	for (auto &cs : candidate_sets) {
		if (*cs == *new_set) {
			covered = cs;
			return;
		}
	}

	candidate_sets.insert(new_set);
	garbageCollect(covered);
	covered = new_set;
}

void CovSet::garbageCollect(const std::shared_ptr<CovSet::covset_t>& cs)
{
	unsigned	set_size = cs->size();

	// only used by all_sets and the 'cs' pointer?
	if (cs.use_count() != 2) return;

	auto it = all_sets.find(set_size);
	if (it == all_sets.end()) return;

	auto &sets = it->second;
	sets.erase(cs);
	if (sets.empty()) {
		all_sets.erase(set_size);
	}
}

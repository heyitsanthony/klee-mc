#include <llvm/Support/CommandLine.h>
#include "klee/Internal/ADT/KTest.h"
#include "ReplayKTestsMerging.h"
#include "Executor.h"

using namespace klee;

llvm::cl::opt<std::string>
	KTestMergeDir("ktest-merge-dir",
	llvm::cl::desc("Directory for merged ktest output."),
	llvm::cl::init(""));

bool ReplayKTestsMerging::isReplayMerging(void)
{
	return KTestMergeDir.empty() == false;
}

ExecutionState* ReplayKTestsMerging::replayKTest(
	Executor& exe,
	ExecutionState& initSt,
	const KTest* kt)
{
	auto es = ReplayKTestsFast::replayKTest(exe, initSt, kt);
	if (es) {
		ktest_cov[kt] = es->covset.getCovered();
	}
	return es;
}

void ReplayKTestsMerging::buildReps(void)
{
	CovSet::covset_t	uniqs, dups;

	for (auto p : ktest_cov) {
		for (const auto& s : p.second) {
			if (dups.count(s))
				continue;
			if (uniqs.insert(s).second == false) {
				uniqs.erase(s);
				dups.insert(s);
			}
		}
	}

	for (auto p : ktest_cov) {
		CovSet::covset_t	rep;
		for (const auto& s : p.second) {
			if (uniqs.count(s)) {
				rep.insert(s);
			}
		}
		ktest_reps[p.first] = rep;
	}
}

// Object indexes. (x,y) = (x.numObjects, y.numObjects) => no match
typedef std::pair<unsigned, unsigned>	ktest_match_t;

static ktest_match_t getShortestMatch(const KTest& ktest, const KTest& target)
{
	for (unsigned i = 0; i < ktest.numObjects; i++) {
		unsigned	j = 0;
		const auto	&obj1 = ktest.objects[i];

		for (j = 0; j < target.numObjects; j++) {
			const auto &obj2 = target.objects[j];

			// must match on name
			if (strcmp(obj1.name, obj2.name) != 0)
				continue;

			// names are unique, so bytes sizes must be equal
			if (obj1.numBytes != obj2.numBytes)
				break;

			// data must be equal
			if (memcmp(obj1.bytes, obj2.bytes, obj1.numBytes) != 0)
				break;

			return ktest_match_t(i, j);
		}
	}

	return ktest_match_t(ktest.numObjects, target.numObjects);
}

// XXX need a function that will replace ktest's head with target's head

KTest* ReplayKTestsMerging::mutate(const KTest* ktest)
{
	// XXX: how to choose best mutation?
	// first attempt: match argv's and argc with *any*
	for (unsigned i = 0; i < ktest->numObjects; i++) {
		// find first matching object
		for (auto in_kts : kts) {
			if (ktest == in_kts) continue;

		}
	}
	abort();
}

bool ReplayKTestsMerging::tryMutant(
	Executor& exe,
	ExecutionState& initSt,
	const KTest* ktest_orig,
	const KTest* mutant)
{
	// XXX how to get around inevitable ktest errors?
	auto	es = ReplayKTestsFast::replayKTest(exe, initSt, mutant);
	if (!es) return false;

	const auto& rep = ktest_reps.find(ktest_orig)->second;
	const auto& cs = ktest_cov.find(mutant)->second;
	for (const auto &s : rep) {
		if (cs.count(s)) {
			return false;
		}
	}
	return true;
}

bool ReplayKTestsMerging::replayKTests(Executor& exe, ExecutionState& initSt)
{
	if (!ReplayKTestsFast::replayKTests(exe, initSt))
		return false;
	
	// collected all ktest coverage info; find supporting set
	buildReps();

	// XXX: need to inhibit forking
	for (auto p : ktest_reps) {
		KTest	*mutant = mutate(p.first);

		if (!mutant)
			continue;

		if (!tryMutant(exe, initSt, p.first, mutant)) {
			kTest_free(mutant);
			continue;
		}

		abort();
	}
	// XXX: re-enable forking

	abort();
	return true;
}

#include <llvm/Support/CommandLine.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include "klee/Internal/ADT/KTest.h"
#include "ReplayKTestsMerging.h"
#include "Forks.h"
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
	// XXX: if two tests cover the same set, won't have uniqs!
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

// steps through ktest's objects until it finds the first match
// with target (e.g., can skip over argc, argv)
static ktest_match_t getShortestMatch(const KTest& ktest, const KTest& target)
{
	unsigned	i;

	// ignore matching prefixes
	for (i = 0; i < ktest.numObjects && i < target.numObjects; i++) {
		if (ktest.objects[i] != target.objects[i])
			break;
	}


	for (; i < ktest.numObjects; i++) {
		const auto	&obj1 = ktest.objects[i];

		// go until match next match is found
		for (unsigned j = 0; j < target.numObjects; j++) {
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
	
	std::map<ktest_match_t, const KTest*>	matches;
	for (auto cur_ktest : kts) {
		if (ktest == cur_ktest) continue;
		auto	m = getShortestMatch(*ktest, *cur_ktest);
		matches[m] = cur_ktest;
		std::cerr << m.first << "/" << m.second << '\n';
	}

	if (matches.empty())
		return nullptr;

	auto p = *(matches.begin());
	if (p.first.first == ktest->numObjects) {
		// no suitable matches?
		return nullptr;
	}

	// now do the actual mutation
	std::cerr << "MUTATING WITH: " << p.first.first << "--" << p.first.second << '\n';
	auto ret = new KTest(*ktest);
	ret->newPrefix(*p.second, p.first.first, p.first.second);
	std::cerr << "NEW PREFIX DONE\n";
	return ret;
}

bool ReplayKTestsMerging::tryMutant(
	Executor& exe,
	ExecutionState& initSt,
	const KTest* ktest_orig,
	const KTest* mutant)
{
	// XXX how to get around inevitable ktest errors?
	auto	es = replayKTest(exe, initSt, mutant);
	if (!es) return false;

	assert (ktest_reps.count(ktest_orig));
	assert (ktest_cov.count(mutant));

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
	std::cerr << "[Merging] Replaying seed KTests.\n";
	if (!ReplayKTestsFast::replayKTests(exe, initSt))
		return false;
	
	std::cerr << "[Merging] Mutating KTests.\n";

	// collected all ktest coverage info; find supporting set
	buildReps();

	std::vector<const KTest*>		out_ktests;
	std::vector<std::unique_ptr<KTest>>	good_mutants;

	exe.getForking()->setForkSuppress(false);
	for (auto p : ktest_reps) {
		auto mutant = std::unique_ptr<KTest>(mutate(p.first));

		if (!mutant)
			continue;

		if (!tryMutant(exe, initSt, p.first, mutant.get())) {
			out_ktests.push_back(p.first);
			std::cerr << "BAD MUTANT!!\n";
			continue;
		}

		out_ktests.push_back(mutant.get());
		good_mutants.push_back(std::move(mutant));
	}
	exe.getForking()->setForkSuppress(true);

	std::cerr
		<< "[Merging] Completed mutations. Successful merges:"
		<< good_mutants.size() << "\n";

	// write out new ktest set
	mkdir(KTestMergeDir.c_str(), 0750);
	for (unsigned i = 0; i < out_ktests.size(); i++) {
		std::stringstream ss;
		ss << KTestMergeDir << "/" << i << ".ktest.gz";
		auto fname = ss.str();
		out_ktests[i]->toFile(fname.c_str());
	}
	good_mutants.clear();

	return true;
}

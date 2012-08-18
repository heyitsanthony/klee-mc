#include <sstream>
#include <llvm/Support/CommandLine.h>
#include "klee/Internal/ADT/KTest.h"
#include "klee/Internal/Support/Timer.h"
#include "klee/Internal/System/Time.h"
#include "klee/ExecutionState.h"

#include "static/Sugar.h"

#include "CoreStats.h"
#include "StateSolver.h"
#include "Executor.h"
#include "SeedCore.h"

namespace llvm
{
cl::opt<bool> AlwaysOutputSeeds("always-output-seeds", cl::init(true));

cl::opt<bool>
OnlySeed("only-seed", cl::desc("Stop execution after seeding is done."));

cl::opt<bool>
AllowSeedExtension("allow-seed-extension",
cl::desc("Allow extra (unbound) values to become symbolic during seeding."));

cl::opt<bool> ZeroSeedExtension("zero-seed-extension");

cl::opt<bool>
AllowSeedTruncation("allow-seed-truncation",
cl::desc("Allow smaller buffers than in seeds."));

cl::opt<bool>
NamedSeedMatching("named-seed-matching",
cl::desc("Use names to match symbolic objects to inputs."));

cl::opt<double>
SeedTime("seed-time",
cl::desc("Seconds to dedicate to seeds before normal search (default=0 (off))"),
cl::init(0));
}

using namespace klee;
using namespace llvm;

bool SeedCore::executeGetValueSeeding(
	ExecutionState &st, ref<Expr> e, KInstruction *target)
{
	SeedInfoIterator	si_begin, si_end;
	bool			isSeeding;

	e = st.constraints.simplifyExpr(e);
	if (isa<ConstantExpr>(e))
		return false;

	isSeeding = getSeedInfoIterRange(&st, si_begin, si_end);
	if (!isSeeding)
		return false;

	std::set< ref<Expr> > values;
	foreach (siit, si_begin, si_end) {
		ref<ConstantExpr> value;
		bool		ok;

		ok = exe->getSolver()->getValue(
			st, siit->assignment.evaluate(e), value);
		if (!ok) {
			exe->terminateEarly(st, "exeGetValues timeout");
			return true;
		}

		values.insert(value);
	}

	Executor::StateVector		branches;
	std::vector< ref<Expr> >	conditions;

	foreach (vit, values.begin(), values.end())
		conditions.push_back(EqExpr::create(e, *vit));

	branches = exe->fork(st, conditions.size(), conditions.data(), true);
	if (target == NULL)
		return true;

	Executor::StateVector::iterator	bit(branches.begin());
	foreach (vit, values.begin(), values.end()) {
		ExecutionState	*es(*bit);
		++bit;
		if (es) es->bindLocal(target, *vit);
	}

	return true;
}

void SeedCore::stepSeedInst(ExecutionState* &lastState)
{
	SeedMapType::iterator	it;

	it = seedMap.upper_bound(lastState);
	if (it == seedMap.end()) it = seedMap.begin();
	lastState = it->first;

	ExecutionState *state = lastState;

	exe->stepStateInst(state);
	exe->notifyCurrent(state);
}

bool SeedCore::isOnlySeed(void) const { return OnlySeed; }

#define SEED_CHECK_INTERVAL_INSTS	1000

bool SeedCore::seedRun(ExecutionState& initialState)
{
	ExecutionState *lastState = 0;
	double lastTime, startTime = lastTime = util::estWallTime();
	std::vector<SeedInfo> &v = seedMap[&initialState];

	foreach (it, usingSeeds->begin(), usingSeeds->end())
		v.push_back(SeedInfo(*it));

	int lastNumSeeds = usingSeeds->size()+10;
	while (!seedMap.empty() && !exe->isHalted()) {
		double time;

		stepSeedInst(lastState);

		/* every 1000 instructions, check timeouts, seed counts */
		if ((stats::instructions % SEED_CHECK_INTERVAL_INSTS) != 0)
			continue;

		unsigned numSeeds = 0;
		unsigned numStates = seedMap.size();
		foreach (it, seedMap.begin(), seedMap.end())
			numSeeds += it->second.size();

		time = util::estWallTime();
		if (SeedTime>0. && time > startTime + SeedTime) {
			klee_warning(
				"seed time expired, %d seeds remain over %d states",
				numSeeds, numStates);
			break;
		} else if ((int)numSeeds<=lastNumSeeds-10 || time >= lastTime+10) {
			lastTime = time;
			lastNumSeeds = numSeeds;
			klee_message("%d seeds remaining over: %d states",
				numSeeds, numStates);
		}
	}

	if (exe->isHalted()) return false;

	return true;
}

bool SeedCore::isInterestingTestCase(ExecutionState* st) const
{ return (AlwaysOutputSeeds && seedMap.count(st)); }

bool SeedCore::seedObject(
	ExecutionState& state,
	SeedInfo& si,
	const MemoryObject* mo,
	const Array* array)
{
	KTestObject *obj = si.getNextInput(mo, NamedSeedMatching);

	/* if no test objects, create zeroed array object */
	if (!obj) {
		if (ZeroSeedExtension) {
			std::vector<unsigned char> &values(
				si.assignment.bindings[array]);
			values = std::vector<unsigned char>(mo->size, '\0');
		} else if (!AllowSeedExtension) {
			exe->terminateOnError(
				state, "ran out of seed inputs", "seed.err");
			return false;
		}
		return true;
	}

	/* resize permitted? */
	if (	obj->numBytes != mo->size &&
		((!(AllowSeedExtension || ZeroSeedExtension) &&
			obj->numBytes < mo->size) ||
			(!AllowSeedTruncation && obj->numBytes > mo->size)))
	{
		std::stringstream msg;
		msg	<< "replace size mismatch: "
			<< mo->name << "[" << mo->size << "]"
			<< " vs " << obj->name << "[" << obj->numBytes << "]"
			<< " in test\n";

		exe->terminateOnError(state, msg.str(), "user.err");
		return false;
	}

	/* resize object to memory size */
	std::vector<unsigned char> &values(si.assignment.bindings[array]);
	values.insert(
		values.begin(),
		obj->bytes,
		obj->bytes + std::min(obj->numBytes, mo->size));
	if (ZeroSeedExtension) {
		for (unsigned i=obj->numBytes; i<mo->size; ++i)
		values.push_back('\0');
	}

	return true;
}

// Check to see if this constraint violates seeds.
// N.B. There's a problem here-- it won't catch what happens if the seed
// is wrong but we screwed up the constraints so that it's permitted.
//
// Offline solution-- save states, run through them, check for subsumption
//
void SeedCore::checkAddConstraintSeeds(ExecutionState& state, ref<Expr>& cond)
{
	SeedMapType::iterator	it;
	bool			warn = false;

	it = seedMap.find(&state);
	if (it == seedMap.end())
		return;

	foreach (siit, it->second.begin(), it->second.end()) {
		bool	mustBeFalse, ok;

		ok = exe->getSolver()->mustBeFalse(
			state,
			siit->assignment.evaluate(cond),
			mustBeFalse);
		assert(ok && "FIXME: Unhandled solver failure");
		if (mustBeFalse) {
			siit->patchSeed(state, cond, exe->getSolver());
			warn = true;
		}
	}

	if (warn)
		klee_warning("seeds patched for violating constraint");
}

bool SeedCore::getSeedInfoIterRange(
	ExecutionState* s, SeedInfoIterator &b, SeedInfoIterator& e)
{
	SeedMapType::iterator it;
	it = seedMap.find(s);
	if (it == seedMap.end()) return false;
	b = it->second.begin();
	e = it->second.end();
	return false;
}

void SeedCore::addSymbolicToSeeds(
	ExecutionState& state,
	const MemoryObject* mo,
	const  Array* array)
{
	SeedMapType::iterator it;
	it = seedMap.find(&state);
	if (it == seedMap.end()) return;

	// In seed mode we need to add this as a binding.
	foreach (siit, it->second.begin(), it->second.end()) {
		if (!seedObject(state, *siit, mo, array))
			break;
	}
}

void SeedCore::erase(ExecutionState* es)
{
	SeedMapType::iterator it3;
	it3 = seedMap.find(es);
	if (it3 != seedMap.end()) seedMap.erase(it3);
}

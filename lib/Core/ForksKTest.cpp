#include <llvm/Support/CommandLine.h>
#include "static/Sugar.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/util/Assignment.h"
#include "klee/ExecutionState.h"
#include "klee/KleeHandler.h"
#include "ForksKTest.h"

using namespace klee;

llvm::cl::opt<unsigned> KTestTimeout("ktest-timeout");


bool ForksKTest::updateSymbolics(ExecutionState& current)
{
	ExecutionState::SymIt		it(current.symbolicsBegin());
	unsigned			i;

	assert (arrs.size() < current.getNumSymbolics());

	for (i = 0; i < arrs.size(); i++) it++;

	for (; i < current.getNumSymbolics(); i++) {
		const SymbolicArray	&sa(*it);
		unsigned		oi = i - base_objs;
		ref<Array>		arr(sa.getArrayRef());
		const uint8_t		*v_buf = kt->objects[oi].bytes;
		unsigned		v_len = kt->objects[oi].numBytes;
		std::vector<uint8_t>	v(v_buf, v_buf + v_len);

		std::cerr
			<< "[KTest] Adding ARR name=\"" << arr->name
			<< "\". size=" << arr->getSize()
			<< ". ObjBytes = "<< kt->objects[oi].numBytes << '\n';

		if (arr->getSize() != kt->objects[oi].numBytes) {
			std::cerr << "[KTest] Size mismatch!\n";
			return false;
		}

		addBinding(arr, v);
		it++;
	}

	return true;
}

void ForksKTest::addBinding(ref<Array>& arr, std::vector<uint8_t>& v)
{
	arrs.push_back(arr);
	kt_assignment->addBinding(arr.get(), v);
}

bool ForksKTest::isBadOverflow(ExecutionState& current)
{
	if (current.getNumSymbolics() <= arrs.size())
		return false;

	if (current.getNumSymbolics() > kt->numObjects + base_objs) {
		std::cerr << "[KTest] State Symbolics="
			<< current.getNumSymbolics()
			<< ". Last="
			<< (*(current.symbolicsEnd()-1)).getArray()->name
			<< '\n';
		std::cerr << "[KTest] KTest Objects="
			<< kt->numObjects
			<< ". Last="
			<< kt->objects[kt->numObjects-1].name
			<< '\n';

		std::cerr << "[KTest] Ran out of objects!!\n";

		if (base_objs == 0) {
			ExecutionState	*es(pureFork(current));
			es->abortInstruction();
		}

		if (make_err_tests) {
			TERMINATE_ERROR(&exe, current,
				"KTest seeding failed. Ran out of objects!",
				"ktest.err");
		} else {
			std::cerr << "[KTest] Goodbye. Object underflow!\n";
			exe.terminate(current);
		}

		return true;
	}

	if (updateSymbolics(current) == false) {
		/* create ktest state outside replay; deal with later */
		if (base_objs == 0) {
			ExecutionState	*es(pureFork(current));
			es->abortInstruction();
		}

		if (make_err_tests) {
			TERMINATE_ERROR(&exe, current,
				"KTest seeding failed. Bad objsize?",
				"ktest.err");
		} else {
			std::cerr << "[KTest] Goodbye. Bad Objsize\n";
			exe.terminate(current);
		}

		return true;
	}

	return false;
}


int ForksKTest::findCondIndex(const struct ForkInfo& fi, bool& non_const)
{
	non_const = false;

	/* find + steer to condition which ktest satisfies */
	for (unsigned i = 0; i < fi.N; i++) {
		ref<Expr>	cond_eval;

		if (fi.res[i] == false)
			continue;

		if (fi.conditions[i].isNull())
			continue;

		if (fi.conditions[i]->getKind() != Expr::Constant) {
			cond_eval = kt_assignment->evaluate(fi.conditions[i]);
		} else
			cond_eval = fi.conditions[i];

		if (cond_eval->getKind() != Expr::Constant)
			non_const = true;

		if (cond_eval->isTrue())
			return i;
	}

	/* this occassionally happens with pure forks, don't worry */
	return -1;
}

bool ForksKTest::setupForkAffinity(
	ExecutionState& current,
	struct ForkInfo& fi,
	unsigned* cond_idx_map)
{
	bool	non_const;
	int	cond_idx;

	assert (kt_assignment);

	if (isBadOverflow(current))
		return false;
	
	cond_idx = findCondIndex(fi, non_const);
	if (cond_idx == -1)
		return true;

	cond_idx_map[0] = cond_idx;
	cond_idx_map[cond_idx] = 0;
	return true;
}

ForksKTest::~ForksKTest() { if (kt_assignment) delete kt_assignment; }

void ForksKTest::setKTest(const KTest* _kt, const ExecutionState* es_base)
{
	std::vector< std::vector<unsigned char> >	vals;
	std::vector<const Array*>			arr;

	kt = _kt;
	if (kt_assignment) delete kt_assignment;
	kt_assignment = new Assignment(true);

	arrs.clear();
	base_objs = 0;

	if (es_base == NULL)
		return;

	/* augment with base state's arrays */
	foreach (it, es_base->symbolicsBegin(), es_base->symbolicsEnd())
		arrs.push_back(it->getArrayRef());

	base_objs = arrs.size();
	assert (es_base->getNumSymbolics() == base_objs);
}

ForksKTestStateLogger::~ForksKTestStateLogger(void)
{
	bool	isHalted = exe.isHalted();

	/* this is so we don't verify the paths for the states we're killing */
	exe.setHaltExecution(true);

	foreach (it, state_cache.begin(), state_cache.end())
		exe.terminate(*(it->second));

	exe.setHaltExecution(isHalted);
}


ExecutionState* ForksKTestStateLogger::getNearState(const KTest* t)
{
	ExecutionState	*best_es = NULL;
	Assignment	a(true);

	/* add successive objects until no more head state available */
	for (unsigned i = 0; i < t->numObjects; i++) {
		const uint8_t		*v_buf = t->objects[i].bytes;
		unsigned		v_len = t->objects[i].numBytes;
		std::vector<uint8_t>	v(v_buf, v_buf + v_len);
		ref<Array>		arr;
		arrcache_ty::iterator	arr_it;
		statecache_ty::const_iterator st_it;

		arr_it = arr_cache.find(arrkey_ty(
			t->objects[i].numBytes,
			std::string(t->objects[i].name)));
		if (arr_it == arr_cache.end()) {
			std::cerr << "[KTest] could not find arr="
				<< t->objects[i].name << '\n';
			break;
		}
	
		/* found a matching array, add it */
		arr = arr_it->second;
		a.addBinding(arr.get(), v);

		/* state matches for this assignment? */
		st_it = state_cache.find(a);
		if (st_it == state_cache.end()) {
			std::cerr << "[KTest] could not find state on sz="
				<< a.getNumBindings() << '\n';
#if 0
			std::cerr << "[KTest] our assignment: \n";
			a.print(std::cerr);
			std::cerr << "[KTest] equisize assignments:\n";
			foreach (it, state_cache.begin(), state_cache.end()) {
				if (it->first.getNumBindings() != 2)
					continue;
				std::cerr << "[KTest] sz=2\n";
				it->first.print(std::cerr);
			}
			std::cerr << "[KTest] ======= done printing\n";
#endif
			break;
		}

		best_es = st_it->second;
	}

	if (best_es == NULL)
		return NULL;

	/* found matching head, return copy */
	best_es = pureFork(*best_es);
	return best_es;
}

void ForksKTestStateLogger::addBinding(ref<Array>& a, std::vector<uint8_t>& v)
{
	arrkey_ty	ak(a->getSize(), a->name);
	arr_cache.insert(std::make_pair(ak, a));

//	ref<Array>	arr((arr_cache.find(ak))->second);
//	ForksKTest::addBinding(arr, v);
	ForksKTest::addBinding(a, v);
}

bool ForksKTestStateLogger::updateSymbolics(ExecutionState& current)
{
	ExecutionState	*new_es;

	if (ForksKTest::updateSymbolics(current) == false)
		return false;

	/* already found? */
	if (state_cache.count(*getCurrentAssignment()) != 0) {
		std::cerr << "[KTEST] Discarding state; already known.\n";
		return true;
	}

	new_es = pureFork(current);
	if (new_es == NULL)
		return true;

	std::cerr << "[KTEST] INSERTING NEW ASSIGNMENT SZ="
		<< getCurrentAssignment()->getNumBindings() <<  "\n";
	/* place state immediately before the forking instruction;
	 * state isn't constrained until after affinities are assigned */
	new_es->abortInstruction();
	state_cache.insert(std::make_pair(*getCurrentAssignment(), new_es));

	return true;
}

bool ForksKTest::evalForks(ExecutionState& current, struct ForkInfo& fi)
{
	bool	non_const;
	int	i;

	fi.forkDisabled = true;

	if (!suppressForks || !fi.conditions || isBadOverflow(current))
		return Forks::evalForks(current, fi);

	if (KTestTimeout && wt.checkSecs() > KTestTimeout) {
		std::cerr << "[KTest] Time out "<< &current <<'\n';
		exe.printStackTrace(current, std::cerr);
		exe.terminate(current);
		fi.conditions = NULL;
		return false;
	}

	setConstraintOmit(false);

	/* assume all conds are feasible so findCondIndex checks everything */
	fi.res.assign(true, fi.N);
	if (fi.conditions[0].isNull())
		fi.conditions[0] = Expr::createIsZero(fi.conditions[1]);
	i = findCondIndex(fi, non_const);
	/* drop assumptions */
	fi.res.assign(false, fi.N);

	/* KILL ALL PATHS */
	if (non_const == true) {
		std::cerr << "[KTest] Non const, oops\n";
		KleeHandler	*kh;
		kh = static_cast<KleeHandler*>(exe.getInterpreterHandler());
		if (base_objs) {
			std::cerr << "[KTest] Terminating "
				<<  &current << '\n';
			exe.printStackTrace(current, std::cerr);
			exe.terminate(current);
		} else
			kh->incPathsExplored();
		fi.res.assign(fi.N, false);
		fi.conditions = NULL;
		return false;
	}

	if (i >= 0 && !fi.conditions[i].isNull()) {
		cheap_fork_c++;
		fi.res.assign(fi.N, false);
		fi.replayTargetIdx = i;
		fi.res[i] = true;
		fi.validTargets = 1;

		// Suppress forking; constraint will be added to path
		// after forkSetup is complete.
		for (unsigned j = 0; j < fi.N; j++)
			if (j != fi.replayTargetIdx)
				fi.conditions[j] = MK_CONST(0,1);

		return true;
	}

	std::cerr << "DEFAULTING ON BRANCH\n";
	return Forks::evalForks(current, fi);
}

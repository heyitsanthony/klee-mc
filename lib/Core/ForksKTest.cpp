#include "static/Sugar.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/util/Assignment.h"
#include "klee/ExecutionState.h"
#include "ForksKTest.h"

using namespace klee;

bool ForksKTest::updateSymbolics(ExecutionState& current)
{
	ExecutionState::SymIt		it(current.symbolicsBegin());
	unsigned			i;

	assert (arrs.size() < current.getNumSymbolics());

	for (i = 0; i < arrs.size(); i++) it++;

	for (; i < current.getNumSymbolics(); i++) {
		const SymbolicArray	&sa(*it);
		ref<Array>		arr(sa.getArrayRef());
		const uint8_t		*v_buf = kt->objects[i].bytes;
		unsigned		v_len = kt->objects[i].numBytes;
		std::vector<uint8_t>	v(v_buf, v_buf + v_len);

		std::cerr
			<< "[KTest] Adding ARR name=\"" << arr->name
			<< "\". size=" << arr->getSize() << '\n';

		std::cerr
			<< "[KTest] Obj Bytes = "
			<< kt->objects[i].numBytes << '\n';

		if (arr->getSize() != kt->objects[i].numBytes) {
			std::cerr << "[KTest] Size mismatch\n";
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

	if (current.getNumSymbolics() > kt->numObjects) {
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

		pureFork(current);
		exe.terminateOnError(
			current,
			"KTest seeding failed. Ran out of objects!",
			"ktest.err");
		return true;
	}

	if (updateSymbolics(current) == false) {
		/* create ktest state outside replay; deal with later */
		pureFork(current);
		exe.terminateOnError(
			current,
			"KTest seeding failed. Bad objsize?",
			"ktest.err");
		return true;
	}

	return false;
}

bool ForksKTest::setupForkAffinity(
	ExecutionState& current,
	struct ForkInfo& fi,
	unsigned* cond_idx_map)
{
	assert (kt_assignment);

	if (isBadOverflow(current))
		return false;
	
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

		if (cond_eval->getKind() != Expr::Constant) {
			std::cerr << "[KTest] Non-Const eval: "
				<< cond_eval << '\n';
			std::cerr << "[KTest] BAD ASSIGNMENT=\n";
			kt_assignment->print(std::cerr);
		}

		assert (cond_eval->getKind() == Expr::Constant);

		if (cond_eval->isTrue()) {
			cond_idx_map[0] = i;
			cond_idx_map[i] = 0;
			return true;
		}	
	}

	/* this occassionally happens with pure forks */
	return true;
}


ForksKTest::~ForksKTest() { if (kt_assignment) delete kt_assignment; }

void ForksKTest::setKTest(const KTest* _kt)
{
	std::vector< std::vector<unsigned char> >	vals;
	std::vector<const Array*>			arr;

	kt = _kt;
	if (kt_assignment) delete kt_assignment;
	kt_assignment = new Assignment(true);

	arrs.clear();
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

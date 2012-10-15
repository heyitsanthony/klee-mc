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
		const ref<Array>	arr(sa.getArrayRef());
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

		arrs.push_back(arr);
		kt_assignment->addBinding(arr.get(), v);
		it++;
	}

	return true;
}

bool ForksKTest::setupForkAffinity(
	ExecutionState& current,
	struct ForkInfo& fi,
	unsigned* cond_idx_map)
{
	assert (kt_assignment);

	if (current.getNumSymbolics() > arrs.size()) {
		if (current.getNumSymbolics() > kt->numObjects) {
			std::cerr << "[KTest] State Symbolics="
				<< current.getNumSymbolics()
				<< ". Last="
				<< (*(current.symbolicsEnd()-1)).getArray()->name
				<< '\n';
			std::cerr	<< "[KTest] KTest Objects="
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
			return false;
		}

		if (updateSymbolics(current) == false) {
			/* create ktest state outside replay;
			 * deal with later */
			pureFork(current);
			exe.terminateOnError(
				current,
				"KTest seeding failed. Bad objsize?",
				"ktest.err");
			return false;
		}
	}

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
			break;
		}	
	}

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

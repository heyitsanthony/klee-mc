#include "static/Sugar.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/util/Assignment.h"
#include "ForksKTest.h"

using namespace klee;

void ForksKTest::setupForkAffinity(
	ExecutionState& current,
	struct ForkInfo& fi,
	unsigned* cond_idx_map)
{
	assert (kt_assignment);

	/* find + steer to condition which ktest satisfies */
	for (unsigned i = 0; i < fi.N; i++) {
		ref<Expr>	cond_eval;

		if (fi.res[i] == false)
			continue;

		cond_eval = kt_assignment->evaluate(fi.conditions[i]);
		assert (cond_eval->getKind() == Expr::Constant);

		if (!cond_eval->isZero()) {
			cond_idx_map[0] = i;
			cond_idx_map[i] = 0;
			break;
		}	
	}
}


ForksKTest::~ForksKTest() { if (kt_assignment) delete kt_assignment; }

void ForksKTest::setKTest(const KTest* _kt)
{
	std::vector< std::vector<unsigned char> >	vals;
	std::vector<const Array*>			arr;

	kt = _kt;
	if (kt_assignment) delete kt_assignment;

	arrs.clear();

	for (unsigned i = 0; i < kt->numObjects; i++) {
		const KTestObject		*kobj(&kt->objects[i]);
		std::vector<unsigned char>	arr_vals;

		for (unsigned j = 0; j < kobj->numBytes; j++)
			arr_vals.push_back(kobj->bytes[i]);

		arrs.push_back(Array::create(kobj->name, kobj->numBytes));
		vals.push_back(arr_vals);
	}

	foreach (it, arrs.begin(), arrs.end())
		arr.push_back((*it).get());

	kt_assignment = new Assignment(arr, vals, false);
}

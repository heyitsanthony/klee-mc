#include "static/Sugar.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/util/Assignment.h"
#include "klee/ExecutionState.h"
#include "ForksKTest.h"

using namespace klee;

void ForksKTest::setupForkAffinity(
	ExecutionState& current,
	struct ForkInfo& fi,
	unsigned* cond_idx_map)
{
	assert (kt_assignment);

	if (current.getNumSymbolics() > arrs.size()) {
		ExecutionState::SymIt		it(current.symbolicsBegin());
		unsigned			i;
		for (i = 0; i < arrs.size(); i++) it++;
		for (; i < current.getNumSymbolics(); i++) {
			const SymbolicArray	&sa(*it);
			const ref<Array>	arr(sa.getArrayRef());
			const uint8_t		*v_buf = kt->objects[i].bytes;
			unsigned		v_len = kt->objects[i].numBytes;
			std::vector<uint8_t>	v(v_buf, v_buf + v_len);

			arrs.push_back(arr);
			kt_assignment->addBinding(arr.get(), v);
			it++;
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
			std::cerr << "WTFFFFFFF: " << cond_eval << '\n';
			std::cerr << "ASSIGNMENT=\n";
			kt_assignment->print(std::cerr);
		}

		assert (cond_eval->getKind() == Expr::Constant);

		if (cond_eval->isTrue()) {
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
	kt_assignment = new Assignment(true);

	arrs.clear();
#if 0
	for (unsigned i = 0; i < kt->numObjects; i++) {
		const KTestObject		*kobj(&kt->objects[i]);
		std::vector<unsigned char>	arr_vals;
		ref<Array>			array;

		for (unsigned j = 0; j < kobj->numBytes; j++)
			arr_vals.push_back(kobj->bytes[i]);

		array = Array::create(kobj->name, kobj->numBytes);
		array = Array::uniqueByName(array);

		arrs.push_back(array);
		vals.push_back(arr_vals);
		byte_c += kobj->numBytes;
	}

	foreach (it, arrs.begin(), arrs.end())
		arr.push_back((*it).get());

	kt_assignment = new Assignment(arr, vals, true);

	std::cerr << "[ForksTest] Loaded ktest. Bytes="
		<< byte_c << ". Objs=" << kt->numObjects << '\n';
#endif
}

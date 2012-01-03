#include <string.h>
#include "klee/Expr.h"

using namespace klee;

Array::ArrayHashCons Array::arrayHashCons;

extern "C" void vc_DeleteExpr(void*);

Array::~Array()
{
	// FIXME: This shouldn't be necessary.
	if (stpInitialArray) {
		::vc_DeleteExpr(stpInitialArray);
		stpInitialArray = 0;
	}

	chk_val = ~0;
	if (constantValues_u8) {
		delete [] constantValues_u8;
		constantValues_u8 = NULL;
	}
}

Array::Array(
	const std::string &_name,
	MallocKey _mallocKey,
	const ref<ConstantExpr> *constantValuesBegin,
	const ref<ConstantExpr> *constantValuesEnd)
: name(_name), mallocKey(_mallocKey)
, stpInitialArray(0), btorInitialArray(0), z3InitialArray(0)
, refCount(refCountDontCare)
, constantValues_expr(constantValuesBegin, constantValuesEnd)
, constantValues_u8(NULL)
, constant_count(constantValues_expr.size())
, singleValue(0)
{
	chk_val = 0x12345678;
	assert( (isSymbolicArray() || constant_count == mallocKey.size) &&
		"Invalid size for constant array!");

	#ifdef NDEBUG
	for (	const ref<ConstantExpr> *it = constantValuesBegin;
		 it != constantValuesEnd; ++it)
	{
		assert(it->getWidth() == getRange() &&
		     "Invalid initial constant value!");
	}
	#endif

	if (constant_count == 0) return;
	if (getRange() != 8) return;

	/* use u8 constant cache and save a bundle. */
	constantValues_u8 = new uint8_t[constant_count];
	for (unsigned int i = 0; i < constant_count; i++) {
		constantValues_u8[i] = constantValues_expr[i]->getZExtValue(8);
	}
	constantValues_expr.clear();

	for (unsigned int i = 1; i  < constant_count; i++) {
		if (constantValues_u8[i] != constantValues_u8[i-1]) {
			return;
		}
	}

	singleValue = getValue(0);
}

bool Array::operator< (const Array &b) const
{
	if (&b == this) return false;

	if (isConstantArray() != b.isConstantArray())
		return isConstantArray() < b.isConstantArray();

	if (isConstantArray() && b.isConstantArray()) {
	// disregard mallocKey for constant arrays; mallocKey matches are
	// not a sufficient condition for constant arrays, but value matches are
		if (constant_count != b.constant_count)
			return constant_count < b.constant_count;

		if (constantValues_u8) {
			if ( memcmp(
				constantValues_u8,
				b.constantValues_u8,
				constant_count) < 0)
			{
				return true;
			}

			return false;
		}

		for (unsigned i = 0; i < constantValues_expr.size(); i++) {
			if (constantValues_expr[i] != b.constantValues_expr[i])
				return	constantValues_expr[i] <
					b.constantValues_expr[i];
		}
		return false; // equal, so NOT less than
	}

	if (mallocKey.allocSite && b.mallocKey.allocSite) {
		return (mallocKey.compare(b.mallocKey) < 0);
	}

	return name < b.name;
}

void Array::print(std::ostream& os) const
{
	os	<< "ARR: name=" << name << ". Size=" << constant_count
		<< ".\n";

	if (constantValues_u8) {
		for (unsigned i = 0; i < constant_count; i++) {
			if ((i % 16) == 0) os << "\n[" << i << "]: ";
			os << (void*)constantValues_u8[i];
		}
	} else {
		for (unsigned i = 0; i < constant_count; i++) {
			if ((i % 16) == 0) os << "\n[" << i << "]: ";
			os << constantValues_expr[i];
		}
	}

	os << '\n';
}

Array* Array::uniqueArray(Array* arr)
{
	std::pair<ArrayHashCons::iterator,bool> ret(arrayHashCons.insert(arr));
	if (ret.second)
		return arr;

	assert (*arr == *(*ret.first));
	delete arr;
	return *ret.first;
}

void Array::getConstantValues(std::vector< ref<ConstantExpr> >& v) const
{
	if (!constantValues_u8) {
		v = constantValues_expr;
		return;
	}

	v.resize(mallocKey.size);
	for (unsigned i = 0; i < mallocKey.size; i++)
		v[i] = getValue(i);
}

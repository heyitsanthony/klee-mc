#include <iostream>
#include <string.h>
#include "klee/Expr.h"

using namespace klee;

Array::ArrayHashCons	Array::arrayHashCons;
Array::name2arr_ty	Array::name2arr;

extern "C" void vc_DeleteExpr(void*);
unsigned Array::count = 0;

Array::~Array()
{
	assert (chk_val == ARRAY_CHK_VAL && "Double free?");

	count--;
	// FIXME: This shouldn't be necessary.
	// XXX this crashes shit. whoops.
	// if (stpInitialArray) {
	//	::vc_DeleteExpr(stpInitialArray);
	//	stpInitialArray = 0;
	// }

	chk_val = ~0;
	if (constantValues_u8) {
		delete [] constantValues_u8;
		constantValues_u8 = NULL;
	}

	if (name2arr.count(name))
		name2arr.erase(name);
}

ref<Array> Array::get(const std::string &_name)
{
	name2arr_ty::iterator	it(name2arr.find(_name));
	Array			*ret;

	if (it == name2arr.end())
		return NULL;

	ret = it->second;
	ret->incRefIfCared();
	return ret;
}

ref<Array> Array::create(
	const std::string &_name,
	MallocKey _mallocKey,
	const ref<ConstantExpr> *constValBegin,
	const ref<ConstantExpr> *constValEnd)
{
	Array	*ret;

	ret = new Array(_name, _mallocKey, constValBegin, constValEnd);
	ret->initRef();
	if (_name.size())
		name2arr.insert(std::make_pair(_name, ret));

	return ret;
}

Array::Array(
	const std::string &_name,
	MallocKey _mallocKey,
	const ref<ConstantExpr> *constantValuesBegin,
	const ref<ConstantExpr> *constantValuesEnd)
: name(_name)
, mallocKey(_mallocKey)
, stpInitialArray(0), btorInitialArray(0), z3InitialArray(0)
, refCount(refCountDontCare)
, constantValues_expr(constantValuesBegin, constantValuesEnd)
, constantValues_u8(NULL)
, constant_count(constantValues_expr.size())
, singleValue(0)
{
	count++;
	hash_v = mallocKey.size;
	chk_val = ARRAY_CHK_VAL;
	assert( (isSymbolicArray() || constant_count == mallocKey.size) &&
		"Invalid size for constant array!");

	if (constant_count == 0)
		return;

	assert (getRange() == 8);

	/* use u8 constant cache and save a bundle. */
	constantValues_u8 = new uint8_t[constant_count];
	for (unsigned int i = 0; i < constant_count; i++) {
		constantValues_u8[i] = constantValues_expr[i]->getZExtValue(8);
		hash_v += constantValues_u8[i] * (i+1);
	}
	constantValues_expr.clear();

	for (unsigned int i = 1; i  < constant_count; i++) {
		if (constantValues_u8[i] != constantValues_u8[i-1])
			return;
	}

	singleValue = getValue(0);
}

bool ArrayConsLT::operator()(const Array *a, const Array *b) const
{ bool ft; return a->lt_partial(*b, ft); }

bool Array::lt_partial(const Array& b, bool& fell_through) const
{
	fell_through = false;

	if (&b == this) return false;

	if (hash_v < b.hash_v)
		return true;

	if (hash_v > b.hash_v)
		return false;

	if (isConstantArray() != b.isConstantArray())
		return isConstantArray() < b.isConstantArray();

	if (isConstantArray() && b.isConstantArray()) {
		// disregard mallocKey for constant arrays;
		// mallocKey matches are not a
		// sufficient condition for constant arrays,
		// but value matches are.
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

	if (mallocKey.allocSite && b.mallocKey.allocSite)
		return (mallocKey.compare(b.mallocKey) < 0);

	/* maybe equal */
	fell_through = true;
	return false;
}

bool Array::operator< (const Array &b) const
{
	bool	is_lt;
	bool	fell_through;

	is_lt = lt_partial(b, fell_through);
	if (!fell_through)
		return is_lt;
	
	return name < b.name;
}

void Array::print(std::ostream& os) const
{
	os << "ARR: name=" << name << ". Size=" << constant_count << ".\n";

	if (constantValues_u8) {
		for (unsigned i = 0; i < constant_count; i++) {
			if ((i % 16) == 0) os << "\n[" << i << "]: ";
			os << (void*)constantValues_u8[i] << ' ';
		}
	} else {
		for (unsigned i = 0; i < constant_count; i++) {
			if ((i % 16) == 0) os << "\n[" << i << "]: ";
			os << constantValues_expr[i];
		}
	}

	os << '\n';
}

ref<Array> Array::uniqueArray(Array* arr)
{
	std::pair<ArrayHashCons::iterator,bool> ret(arrayHashCons.insert(arr));

	/* added new element? */
	if (ret.second && arr) {
		assert (arr->chk_val == ARRAY_CHK_VAL);
		arr->incRefIfCared();
		return arr;
	}

	/* found a unique */
	assert (*arr == *(*ret.first));
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

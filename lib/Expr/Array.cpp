#include <iostream>
#include <string.h>
#include "static/Sugar.h"
#include "klee/Expr.h"

using namespace klee;

Array::ArrayHashCons		Array::arrayHashConsAnon;
Array::ArrayHashConsExact	Array::arrayHashConsExact;

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

	if (it == name2arr.end())
		return NULL;

	return it->second;
}

ref<Array> Array::create(
	const std::string &_name,
	MallocKey _mallocKey,
	const ref<ConstantExpr> *constValBegin,
	const ref<ConstantExpr> *constValEnd)
{
	ref<Array>	ret;
	Array		*arr;

	/* XXX: will this leak arrays? I hope not. */
	arr = new Array(_name, _mallocKey, constValBegin, constValEnd);
	arr->initRef();
	ret = ref<Array>(arr);

	if (_name.size()) {
		name2arr.insert(std::make_pair(_name, ret));
	}

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

bool ArrayConsLT::operator()(const ref<Array>& a, const ref<Array>& b) const
{ bool ft; return a->lt_partial(*b, ft); }

bool ArrayConsLTExact::operator()(const ref<Array>& a, const ref<Array>& b) const
{ return *a < *b; }


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
			int	v;
			v = memcmp(
				constantValues_u8,
				b.constantValues_u8,
				constant_count);
			if (v < 0) return true;
			if (v > 0) return false;
		}

		for (unsigned i = 0; i < constantValues_expr.size(); i++) {
			if (constantValues_expr[i] != b.constantValues_expr[i])
				return	constantValues_expr[i] <
					b.constantValues_expr[i];
		}
		// return false; // equal, so NOT less than
	}

	if (mallocKey.allocSite && b.mallocKey.allocSite) {
		int	v;
		v = mallocKey.compare(b.mallocKey);
		if (v < 0) return true;
		if (v > 0) return false;
	}

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

ref<Array> Array::uniqueByName(ref<Array>& arr)
{
	std::pair<ArrayHashConsExact::iterator,bool> ret;

	ret = arrayHashConsExact.insert(arr);

	/* added new element? */
	if (ret.second && !arr.isNull()) {
		assert (arr->chk_val == ARRAY_CHK_VAL);
		return arr;
	}

	/* found a unique */
	return *ret.first;
}

void Array::garbageCollect(void)
{
	std::vector<ref<Array> > to_del;

	if (arrayHashConsExact.size() < 100)
		return;

	foreach (it, arrayHashConsExact.begin(), arrayHashConsExact.end()) {
		ref<Array>	a(*it);

		if (a.getRefCount() == 2)
			to_del.push_back(a);
	}

	foreach (it, to_del.begin(), to_del.end())
		arrayHashConsExact.erase(*it);
}

/* gets a unique array ignoring the name */
ref<Array> Array::uniqueArray(ref<Array>& arr)
{
	std::pair<ArrayHashCons::iterator,bool> ret;

	ret = arrayHashConsAnon.insert(arr);

	/* added new element? */
	if (ret.second && !arr.isNull()) {
		assert (arr->chk_val == ARRAY_CHK_VAL);
		return arr;
	}

	/* found a unique */
	assert (arr->hash_v == (*ret.first)->hash_v);
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

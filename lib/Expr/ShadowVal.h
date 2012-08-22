#ifndef SHADOWVAL_H
#define SHADOWVAL_H

#include "klee/Expr.h"
#include <iostream>

namespace klee
{
class ShadowVal
{
public:
	virtual ~ShadowVal() {}
	virtual bool chk(void) const { return true; }
	virtual ref<ShadowVal> operator&(const ref<ShadowVal>& ) const = 0;
	virtual ref<ShadowVal> operator|(const ref<ShadowVal>& ) const = 0;
	virtual int compare(const ShadowVal& v) const = 0;
	virtual void print(std::ostream& os) const = 0;

	/* for 'ref' */
	uint64_t refCount;
	static bool classof(const ShadowVal* v) { return true; }
protected:
	ShadowVal() : refCount(0) {}
private:
};

template <typename T>
class ShadowValT : public ShadowVal
{
public:
	static ref<ShadowVal> create(T _v)
	{
		typename svcache_ty::iterator	it;
		ref<ShadowVal>			ret;

		it = cache.find(_v);
		if (it != cache.end())
			return it->second;
		
		ret = ref<ShadowVal>(new ShadowValT<T>(_v));
		cache.insert(std::make_pair(_v, ret));
		return ret;
	}

	virtual int compare(const ShadowVal& sv) const
	{
		T in_v = static_cast<const ShadowValT<T>&>(sv).getV();
		if (v == in_v) return 0;
		if (v < in_v) return -1;
		return 1;
	}

	virtual void print(std::ostream& os) const { os << v; }

	virtual ref<ShadowVal> operator |(const ref<ShadowVal>& sv) const
	{ return create(v | cast<ShadowValT<T> >(sv)->getV()); }

	virtual ref<ShadowVal> operator &(const ref<ShadowVal>& sv) const
	{ return create(v & cast<ShadowValT<T> >(sv)->getV()); }

	T getV(void) const { return v; }
protected:
	ShadowValT(T _v) : v(_v)  {}

private:
	typedef std::map<T, ref<ShadowVal> > svcache_ty;
	static svcache_ty	cache;
	T			v;
};

typedef ShadowValT<uint64_t>	ShadowValU64;
typedef ShadowValT<ref<Expr> > 	ShadowValExpr;

inline std::ostream& operator<<(std::ostream& os, const ShadowVal& v)
{
	v.print(os);
	return os;
}
}

#endif

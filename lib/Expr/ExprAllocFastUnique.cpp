#include <unordered_map>
#include <assert.h>
#include "klee/Expr.h"
#include "static/Sugar.h"
#include "ExprAllocFastUnique.h"

using namespace klee;

struct hashexpr
{ unsigned operator()(const ref<Expr>& a) const { return a->hash(); } };


#define GET_OR_MK_SLOW(x)	\
auto it(exmap_##x.find(key));	\
if (it != exmap_##x.end()) { expr_hit_c++; return it->second; }	\
expr_miss_c++;	\
ref<Expr> r = new x##Expr

#define GET_OR_MK(x)				\
auto &r = exmap_##x[key];			\
if (!r.isNull()) { expr_hit_c++; return r; }	\
expr_miss_c++;					\
r = new x##Expr


/* OP, expr, expr */
/* OP, expr, w */
/* OP, expr, w, off */
/* OP, expr */
/* reads: expr, UL */

unsigned long ExprAllocFastUnique::expr_miss_c = 0;
unsigned long ExprAllocFastUnique::expr_hit_c = 0;

struct hashexpr_read
{ unsigned operator()(
	const std::pair<UpdateList&, ref<Expr> >& v) const
	{ return v.first.hash() ^ v.second->hash(); } };
typedef std::unordered_map<
	std::pair<UpdateList& /* array */, ref<Expr> /* index */>,
	ref<Expr> /* read expression */,
	hashexpr_read>	exmap_read_t;

struct hashexpr_bin
{ unsigned operator()(
	const std::pair<ref<Expr>, ref<Expr> >& v) const
	{ return v.first->hash() - v.second->hash(); } };

typedef std::unordered_map<
	std::pair<ref<Expr>, ref<Expr> >,
	ref<Expr>,
	hashexpr_bin>	exmap_binop_t;

struct hashexpr_ext
{ unsigned operator()(
	const std::pair<ref<Expr>, Expr::Width >& v) const
	{ return v.first->hash() ^ v.second; } };
typedef std::unordered_map<
	std::pair<ref<Expr>, Expr::Width >,
	ref<Expr>,
	hashexpr_ext>	exmap_ext_t;

struct hashexpr_extr
{ unsigned operator()(
	const std::pair<ref<Expr>, std::pair< Expr::Width, Expr::Width > >& v)
	const 
	{ return v.first->hash() ^ ((v.second.first << 8) | v.second.second); } };

typedef std::unordered_map<
	std::pair<ref<Expr>, std::pair< Expr::Width, Expr::Width > >,
	ref<Expr>,
	hashexpr_extr>	exmap_extr_t;

typedef std::unordered_map<ref<Expr>, ref<Expr>, hashexpr > exmap_unop_t;

struct hashexpr_triop
{ unsigned operator()(
	const std::pair<ref<Expr>, std::pair<ref<Expr>, ref<Expr> > >& v)
	const 
	{ return v.first->hash() ^
		(v.second.second->hash() - v.second.first->hash()); } };
typedef std::unordered_map<
	std::pair<ref<Expr>, std::pair<ref<Expr>, ref<Expr> > >,
	ref<Expr>,
	hashexpr_triop>	exmap_triop_t;


static exmap_unop_t	exmap_NotOptimized;
static exmap_read_t	exmap_Read;
static exmap_triop_t	exmap_Select;
static exmap_extr_t	exmap_Extract;
static exmap_ext_t	exmap_ZExt;
static exmap_ext_t	exmap_SExt;
static exmap_unop_t	exmap_Not;

#define DECL_BIN_MAP(x)	static exmap_binop_t exmap_##x;
DECL_BIN_MAP(Concat)
DECL_BIN_MAP(Add)
DECL_BIN_MAP(Sub)
DECL_BIN_MAP(Mul)
DECL_BIN_MAP(UDiv)

DECL_BIN_MAP(SDiv)
DECL_BIN_MAP(URem)
DECL_BIN_MAP(SRem)
DECL_BIN_MAP(And)
DECL_BIN_MAP(Or)
DECL_BIN_MAP(Xor)
DECL_BIN_MAP(Shl)
DECL_BIN_MAP(LShr)
DECL_BIN_MAP(AShr)
DECL_BIN_MAP(Eq)
DECL_BIN_MAP(Ne)
DECL_BIN_MAP(Ult)
DECL_BIN_MAP(Ule)

DECL_BIN_MAP(Ugt)
DECL_BIN_MAP(Uge)
DECL_BIN_MAP(Slt)
DECL_BIN_MAP(Sle)
DECL_BIN_MAP(Sgt)
DECL_BIN_MAP(Sge)


#define DECL_ALLOC_1(x)				\
ref<Expr> ExprAllocFastUnique::x(const ref<Expr>& src)	\
{	ref<Expr>	key(src);	\
	GET_OR_MK(x)(src);		\
	r->computeHash();		\
	return r; }

DECL_ALLOC_1(NotOptimized)
DECL_ALLOC_1(Not)

#define DECL_ALLOC_2(x)	\
ref<Expr> ExprAllocFastUnique::x(const ref<Expr>& lhs, const ref<Expr>& rhs) \
{	std::pair<ref<Expr>, ref<Expr> >	key(lhs, rhs);	\
	GET_OR_MK(x)(lhs,rhs);			\
	r->computeHash();			\
	return r;				\
}
DECL_ALLOC_2(Concat)
DECL_ALLOC_2(Add)
DECL_ALLOC_2(Sub)
DECL_ALLOC_2(Mul)
DECL_ALLOC_2(UDiv)

DECL_ALLOC_2(SDiv)
DECL_ALLOC_2(URem)
DECL_ALLOC_2(SRem)
DECL_ALLOC_2(And)
DECL_ALLOC_2(Or)
DECL_ALLOC_2(Xor)
DECL_ALLOC_2(Shl)
DECL_ALLOC_2(LShr)
DECL_ALLOC_2(AShr)
DECL_ALLOC_2(Eq)
DECL_ALLOC_2(Ne)
DECL_ALLOC_2(Ult)
DECL_ALLOC_2(Ule)

DECL_ALLOC_2(Ugt)
DECL_ALLOC_2(Uge)
DECL_ALLOC_2(Slt)
DECL_ALLOC_2(Sle)
DECL_ALLOC_2(Sgt)
DECL_ALLOC_2(Sge)

ref<Expr> ExprAllocFastUnique::Read(
	const UpdateList &updates, const ref<Expr> &idx)
{
	std::pair<UpdateList&, ref<Expr> > key(
		const_cast<UpdateList&>(updates), idx);
	ReadExpr*	re;
	GET_OR_MK_SLOW(Read)(updates,idx);
	r->computeHash();
	re = cast<ReadExpr>(r);
	/* reference must match what is stored in hash table to avoid 
	 * dealloc of updatelist elsewhere, so can't use 'key' */
	exmap_Read.insert(
		{{const_cast<UpdateList&>(re->updates), idx},
		r});
	return r;
}

ref<Expr> ExprAllocFastUnique::Select(
	const ref<Expr> &c,
	const ref<Expr> &t, const ref<Expr> &f)
{
	std::pair<ref<Expr>, std::pair<ref<Expr>, ref<Expr> > > key(
		c, std::make_pair(t, f));
	GET_OR_MK(Select)(c, t, f);
	r->computeHash();
	return r;
}

ref<Expr> ExprAllocFastUnique::Extract(
	const ref<Expr> &e, unsigned o, Expr::Width w)
{
	std::pair<ref<Expr>, std::pair<unsigned, unsigned> > key(
		e, std::make_pair(o, w));
	GET_OR_MK(Extract)(e, o, w);
	r->computeHash();
	return r;
}

ref<Expr> ExprAllocFastUnique::ZExt(const ref<Expr> &e, Expr::Width w)
{
	std::pair<ref<Expr>, unsigned> key(e, w);
	GET_OR_MK(ZExt)(e,w);
	r->computeHash();
	return r;
}

ref<Expr> ExprAllocFastUnique::SExt(const ref<Expr> &e, Expr::Width w)
{
	std::pair<ref<Expr>, unsigned> key(e, w);
	GET_OR_MK(SExt)(e,w);
	r->computeHash();
	return r;
}

ExprAllocFastUnique::ExprAllocFastUnique() { }

//	std::vector<remove_const<decltype(exmap_##x.begin()->first)> >
//	rmv_keys_##x; 

#define GC_KIND(x)	\
do {		\
	std::vector<unconst_key_T(exmap_##x)> rmv_keys_##x; \
	for (const auto& p :  exmap_##x) {			\
		ref<Expr>	e(p.second);			\
		if (e.getRefCount() > 2) continue;		\
		rmv_keys_##x.emplace_back(p.first);		\
	}							\
	for (const auto& e : rmv_keys_##x) {			\
		exmap_##x.erase(e);				\
	}							\
	ret += rmv_keys_##x.size();				\
} while (0)
	
unsigned ExprAllocFastUnique::garbageCollect(void)
{
	unsigned ret = 0;

	/* first, GC all non-const kinds */

	GC_KIND(NotOptimized);

	/* Read needs a special one since it's UpdateList& ruins lives */
	{
	std::vector<std::pair<UpdateList*, ref<Expr> > > rmv_keys_Read;
	/* keep the read expressions around until all the removals are done;
	 * otherwise map tries to reference dangling UpdateLists */
	std::vector<ref<Expr> > rmv_read_exprs;
	for (const auto& p : exmap_Read) {
		ref<Expr>	e(p.second);
		if (e.getRefCount() > 2) continue;
		rmv_keys_Read.emplace_back(&p.first.first, p.first.second);
		rmv_read_exprs.push_back(e);
	}
	for (const auto& p : rmv_keys_Read) {
		std::pair<UpdateList&, ref<Expr> > k(*(p.first), p.second);
		exmap_Read.erase(k);
	}
	ret += rmv_keys_Read.size();
	rmv_keys_Read.clear();
	rmv_read_exprs.clear();
	}


	GC_KIND(Select);
	GC_KIND(Extract);
	GC_KIND(ZExt);
	GC_KIND(SExt);
	GC_KIND(Not);

	GC_KIND(Concat);
	GC_KIND(Add);
	GC_KIND(Sub);
	GC_KIND(Mul);
	GC_KIND(UDiv);

	GC_KIND(SDiv);
	GC_KIND(URem);
	GC_KIND(SRem);
	GC_KIND(And);
	GC_KIND(Or);
	GC_KIND(Xor);
	GC_KIND(Shl);
	GC_KIND(LShr);
	GC_KIND(AShr);
	GC_KIND(Eq);
	GC_KIND(Ne);
	GC_KIND(Ult);
	GC_KIND(Ule);

	GC_KIND(Ugt);
	GC_KIND(Uge);
	GC_KIND(Slt);
	GC_KIND(Sle);
	GC_KIND(Sgt);
	GC_KIND(Sge);

	/* note that this is NOT a fixed point--
	 * some expressions may be keys for larger expressions in that
	 * removing a larger expression drops the ref count and more
	 * expressions can be GC'd-- I doubt repeating the operation until
	 * fixed point is worth it since this is pricey. */

	std::cerr << "[ExprAllocFastUnique] Non-const freed " << ret << '\n';

	/* GC constants */
	ret += ExprAlloc::garbageCollect();

	std::cerr << "[ExprAllocFastUnique] Total freed " << ret << '\n';\

	std::cerr << "[ExprAllocFastUnique] Expr Hits " << expr_hit_c
		<< " ; Misses " << expr_miss_c  << '\n';

	std::cerr << "[ExprAllocFastUnique] Const Hits " << const_hit_c
		<< " ; Misses " << const_miss_c  << '\n';

	return ret;
}

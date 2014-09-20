#include <tr1/unordered_map>
#include <assert.h>
#include "klee/Expr.h"
#include "ExprAllocFastUnique.h"

using namespace klee;

struct hashexpr
{ unsigned operator()(const ref<Expr>& a) const { return a->hash(); } };


#define GET_OR_MK(x)		\
ref<Expr> r(exmap_##x[key]);	\
if (!r.isNull()) return r;	\
r = new x##Expr

/* OP, expr, expr */
/* OP, expr, w */
/* OP, expr, w, off */
/* OP, expr */
/* reads: expr, UL */

struct hashexpr_read
{ unsigned operator()(
	const std::pair<const UpdateList&, const ref<Expr> >& v) const
	{ return v.first.hash() ^ v.second->hash(); } };
typedef std::tr1::unordered_map<
	std::pair<const UpdateList&, const ref<Expr> >,
	ref<Expr>,
	hashexpr_read>	exmap_read_t;

struct hashexpr_bin
{ unsigned operator()(
	const std::pair<ref<Expr>, ref<Expr> >& v) const
	{ return v.first->hash() - v.second->hash(); } };

typedef std::tr1::unordered_map<
	std::pair<ref<Expr>, ref<Expr> >,
	ref<Expr>,
	hashexpr_bin>	exmap_binop_t;

struct hashexpr_ext
{ unsigned operator()(
	const std::pair<ref<Expr>, Expr::Width >& v) const
	{ return v.first->hash() ^ v.second; } };
typedef std::tr1::unordered_map<
	std::pair<ref<Expr>, Expr::Width >,
	ref<Expr>,
	hashexpr_ext>	exmap_ext_t;

struct hashexpr_extr
{ unsigned operator()(
	const std::pair<ref<Expr>, std::pair< Expr::Width, Expr::Width > >& v)
	const 
	{ return v.first->hash() ^ ((v.second.first << 8) | v.second.second); } };

typedef std::tr1::unordered_map<
	std::pair<ref<Expr>, std::pair< Expr::Width, Expr::Width > >,
	ref<Expr>,
	hashexpr_extr>	exmap_extr_t;

typedef std::tr1::unordered_map<ref<Expr>, ref<Expr>, hashexpr > exmap_unop_t;

struct hashexpr_triop
{ unsigned operator()(
	const std::pair<ref<Expr>, std::pair<ref<Expr>, ref<Expr> > >& v)
	const 
	{ return v.first->hash() ^
		(v.second.second->hash() - v.second.first->hash()); } };
typedef std::tr1::unordered_map<
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
{	ref<Expr> e(exmap_##x[src]);	\
	if (!e.isNull()) return e;	\
	e = new x##Expr(src);		\
	e->computeHash();		\
	return e; }

DECL_ALLOC_1(NotOptimized)
DECL_ALLOC_1(Not)

#define DECL_ALLOC_2(x)	\
ref<Expr> ExprAllocFastUnique::x(const ref<Expr>& lhs, const ref<Expr>& rhs) \
{	std::pair<ref<Expr>, ref<Expr> >	key(lhs, rhs);	\
	GET_OR_MK(x)(lhs,rhs);			\
	r->computeHash();			\
	exmap_##x[key] = r;			\
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

ref<Expr> ExprAllocFastUnique::Read(const UpdateList &updates, const ref<Expr> &idx)
{
	std::pair<const UpdateList&, const ref<Expr> > key(updates, idx);
	GET_OR_MK(Read)(updates,idx);
	r->computeHash();
	exmap_Read[key] = r;
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
	exmap_Select[key] = r;
	return r;
}

ref<Expr> ExprAllocFastUnique::Extract(
	const ref<Expr> &e, unsigned o, Expr::Width w)
{
	std::pair<ref<Expr>, std::pair<unsigned, unsigned> > key(
		e, std::make_pair(o, w));
	GET_OR_MK(Extract)(e, o, w);
	r->computeHash();
	exmap_Extract[key] = r;
	return r;
}

ref<Expr> ExprAllocFastUnique::ZExt(const ref<Expr> &e, Expr::Width w)
{
	std::pair<ref<Expr>, unsigned> key(e, w);
	GET_OR_MK(ZExt)(e,w);
	r->computeHash();
	exmap_ZExt[key] = r;
	return r;
}

ref<Expr> ExprAllocFastUnique::SExt(const ref<Expr> &e, Expr::Width w)
{
	std::pair<ref<Expr>, unsigned> key(e, w);
	GET_OR_MK(SExt)(e,w);
	r->computeHash();
	exmap_SExt[key] = r;
	return r;
}

ExprAllocFastUnique::ExprAllocFastUnique() { }

unsigned ExprAllocFastUnique::garbageCollect(void)
{
	unsigned ret = 0;
	assert (0 == 1 && "GC OUR STUFF");
	ret += ExprAlloc::garbageCollect();
	return ret;
}

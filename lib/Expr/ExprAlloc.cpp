#include <unordered_map>
#include <llvm/ADT/Hashing.h>
#include <iostream>
#include "static/Sugar.h"
#include "klee/Expr.h"
#include "ExprAlloc.h"

using namespace klee;
using namespace llvm;

struct hashapint
{unsigned operator()(const llvm::APInt& a) const
{return hash_value(a);}};

struct hashpair
{
std::hash<uint64_t>	h64;
unsigned operator()(const std::pair<uint8_t, uint64_t>& a) const
{return (((unsigned)a.first) << 8) ^ h64(a.second) ;}};


struct apinteq
{
bool operator()(const APInt& a, const APInt& b) const
{
	if (a.getBitWidth() != b.getBitWidth()) return false;
	return a == b;
}
};

/* important to use an unordered_map instead of a map so we get O(1) access. */
typedef std::unordered_map<
	APInt,
	ref<ConstantExpr>,
	hashapint,
	apinteq> BigConstantExprTab;

typedef std::unordered_map<
	std::pair<uint8_t, uint64_t>,
	ref<ConstantExpr>,
	hashpair> SmallConstantExprTab;


BigConstantExprTab		const_big_hashtab;
SmallConstantExprTab		const_small_hashtab;
static ref<ConstantExpr>	ce_smallval_tab_1[2];
static ref<ConstantExpr>	ce_smallval_tab_8[256];
static ref<ConstantExpr>	ce_smallval_tab_16[256*2];
static ref<ConstantExpr>	ce_smallval_tab_32[256*2];
static ref<ConstantExpr>	ce_smallval_tab_64[256*2];

void initSmallValTab(void)
{
	ce_smallval_tab_1[0] = ref<ConstantExpr>(
		new ConstantExpr(APInt(1, 0)));
	ce_smallval_tab_1[1] = ref<ConstantExpr>(
		new ConstantExpr(APInt(1, 1)));

	ce_smallval_tab_1[0]->computeHash();
	ce_smallval_tab_1[1]->computeHash();

#define SET_SMALLTAB(w,ext)	\
	for (int i = 0; i < 256+ext; i++) {	\
		ref<ConstantExpr>	r(	\
			new ConstantExpr(APInt(w, i-ext)));	\
		ce_smallval_tab_##w[i] = r;	\
		r->computeHash();		\
	}

	SET_SMALLTAB(8,0)
	SET_SMALLTAB(16,256)
	SET_SMALLTAB(32,256)
	SET_SMALLTAB(64,256)
}

static bool tab_ok = false;
unsigned long ExprAlloc::constantCount = 0;
unsigned long ExprAlloc::const_miss_c = 0;
unsigned long ExprAlloc::const_hit_c = 0;


ref<Expr> ExprAlloc::Constant(uint64_t v, unsigned w)
{
	uint64_t	v_off;

	if (w > 64) {
		return ExprAlloc::Constant(APInt(w, v));
	}

	/* wired constants-- [-255,255] */
	v_off = v + 256;
	if (v_off < 2*256) {
		if (tab_ok == false) {
			initSmallValTab();
			constantCount = 2+256+3*(2*256);
			tab_ok = true;
		}

		switch (w) {
		case 1: return ce_smallval_tab_1[v_off-256];
		case 8: return ce_smallval_tab_8[v_off-256];
		case 16: return ce_smallval_tab_16[v_off];
		case 32: return ce_smallval_tab_32[v_off];
		case 64: return ce_smallval_tab_64[v_off];
		default: break;
		}
	}

	auto	&r = const_small_hashtab[std::make_pair(w, v)];

	if (!r.isNull())
		return r;

	const_hit_c--;
	const_miss_c++;

	r = new ConstantExpr(llvm::APInt(w, v));
	r->computeHash();
	constantCount++;

	return r;
}

ref<Expr> ExprAlloc::Constant(const APInt &v)
{
	const_hit_c++;

	if (v.getBitWidth() <= 64) {
		return ExprAlloc::Constant(
			v.getLimitedValue(), v.getBitWidth());
	}

	auto	&r = const_big_hashtab[v];
	if (!r.isNull())
		return r;

	const_hit_c--;
	const_miss_c++;
	r = new ConstantExpr(v);
	r->computeHash();
	constantCount++;

	return r;
}

ExprAlloc::~ExprAlloc() {}

unsigned ExprAlloc::garbageCollect(void)
{
	std::vector<ConstantExpr*>	to_rmv;
	unsigned			n = 0;

	for (const auto &p : const_big_hashtab) {
		ref<Expr>	e(p.second);

		/* Two refs => hash table and 'e' => garbage */
		if (e.getRefCount() == 2)
			to_rmv.push_back(cast<ConstantExpr>(e));
	}

	n += to_rmv.size();
	for (const auto ce : to_rmv) {
		const_big_hashtab.erase(ce->getAPValue());
	}

	to_rmv.clear();
	for (const auto &p : const_small_hashtab) {
		ref<Expr>	e(p.second);
		/* Two refs => hash table and 'e' => garbage */
		if (e.getRefCount() == 2)
			to_rmv.push_back(cast<ConstantExpr>(e));
	}

	n += to_rmv.size();
	for (const auto ce : to_rmv) {
		const_small_hashtab.erase(
			std::make_pair(	ce->getWidth(),
					ce->getAPValue().getLimitedValue()));
	}

	constantCount -= n;
	return n;
}

#define DECL_ALLOC_1(x)				\
ref<Expr> ExprAlloc::x(const ref<Expr>& src)	\
{	\
	ref<Expr> r(new x##Expr(src));	\
	r->computeHash();		\
	return r;			\
}

DECL_ALLOC_1(NotOptimized)
DECL_ALLOC_1(Not)

#define DECL_ALLOC_2(x)	\
ref<Expr> ExprAlloc::x(const ref<Expr>& lhs, const ref<Expr>& rhs)	\
{	\
	ref<Expr> r(new x##Expr(lhs, rhs));	\
	r->computeHash();		\
	return r;			\
}

#define DECL_ALLOC_2_DIV(x)		\
ref<Expr> ExprAlloc::x(const ref<Expr>& lhs, const ref<Expr>& rhs)	\
{						\
	if (rhs->isZero()) {			\
		Expr::errors++;			\
		Expr::errorExpr = lhs;		\
		return lhs;			\
	}					\
	ref<Expr> r(new x##Expr(lhs, rhs));	\
	r->computeHash();		\
	return r;			\
}


DECL_ALLOC_2(Concat)
DECL_ALLOC_2(Add)
DECL_ALLOC_2(Sub)
DECL_ALLOC_2(Mul)

DECL_ALLOC_2_DIV(UDiv)
DECL_ALLOC_2_DIV(SDiv)
DECL_ALLOC_2_DIV(URem)
DECL_ALLOC_2_DIV(SRem)

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

ref<Expr> ExprAlloc::Read(const UpdateList &updates, const ref<Expr> &idx)
{
	ref<Expr> r(new ReadExpr(updates, idx));

	if (idx->getKind() == Expr::Constant) {
		const ref<ConstantExpr> idx_ce(cast<ConstantExpr>(idx));
		if (idx_ce->getZExtValue() > updates.getRoot()->getSize()) {
			if (!Expr::errors)
				std::cerr << "[Expr] OOB read\n";
			Expr::errors++;
			return MK_CONST(0, 8);
		}
	}

	r->computeHash();
	return r;
}

ref<Expr> ExprAlloc::Select(
	const ref<Expr> &c,
	const ref<Expr> &t, const ref<Expr> &f)
{
	ref<Expr> r(new SelectExpr(c, t, f));
	r->computeHash();
	return r;
}

ref<Expr> ExprAlloc::Extract(const ref<Expr> &e, unsigned o, Expr::Width w)
{
	ref<Expr> r(new ExtractExpr(e, o, w));
	r->computeHash();
	return r;
}

ref<Expr> ExprAlloc::ZExt(const ref<Expr> &e, Expr::Width w)
{
	ref<Expr> r(new ZExtExpr(e, w));
	r->computeHash();
	return r;
}

ref<Expr> ExprAlloc::SExt(const ref<Expr> &e, Expr::Width w)
{
	ref<Expr> r(new SExtExpr(e, w));
	r->computeHash();
	return r;
}


void ExprAlloc::printName(std::ostream& os) const { os << "ExprAlloc\n"; }

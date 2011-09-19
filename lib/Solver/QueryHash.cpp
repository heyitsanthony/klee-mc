#include "static/Support.h"
#include "klee/util/ExprVisitor.h"
#include "klee/Constraints.h"
#include "klee/Solver.h"
#include "static/Sugar.h"
#include <openssl/sha.h>

#include "QueryHash.h"

using namespace klee;

class RewriteVisitor : public ExprVisitor
{
private:
	unsigned	const_counter;
public:
	RewriteVisitor()
	: ExprVisitor(true, true), const_counter(0) {}

	virtual Action visitExpr(const Expr &e)
	{
		const ConstantExpr*	ce;
		uint64_t		v;
		ref<Expr>		new_ce;

		ce = dyn_cast<const ConstantExpr>(&e);
		if (!ce || ce->getWidth() > 64) return Action::doChildren();

		v = ce->getZExtValue();
		if (v < 0x100000) return Action::doChildren();

		if (v != ~0ULL) v++;
		new_ce = ConstantExpr::alloc(
			(++const_counter) % v,
			ce->getWidth());
		return Action::changeTo(new_ce);
	}
};

/* I noticed that we weren't seeing much results across runs. We assume that
 * this is from nondeterministic pointers gunking up the query.
 *
 * So, rewrite expressions that look like pointers into deterministic values.
 * NOTE: We need to use several patterns here to test whether the SMT can be
 * smart about partitioning with low-value constants.
 *
 */
unsigned QHRewritePtr::hash(const Query& q) const
{
	ConstraintManager	cm;
	ref<Expr>		new_expr;

	foreach (it, q.constraints.begin(), q.constraints.end()) {
		RewriteVisitor	rw;
		ref<Expr>	it_e = *it;
    		ref<Expr>	e = rw.visit(it_e);

		cm.addConstraint(e);
	}

	RewriteVisitor rw;
	new_expr = rw.visit(q.expr);
	Query	new_q(cm, new_expr);

	return new_q.hash();
}

/* Just to be sure that the hash Daniel gave us didn't have a lot of
 * bad collisions, we're using a cryptographic hash on the string. */
unsigned QHExprStrSHA::hash(const Query& q) const
{
	std::string	s;
	unsigned char	md[SHA_DIGEST_LENGTH];
	unsigned	ret;

	s = Support::printStr(q);
	SHA1((const unsigned char*)s.c_str(), s.size(), md);

	ret = 0;
	for (unsigned i = 0; i < 4; i++) {
		ret <<= 8;
		ret |= md[i];
	}

	return ret;
}


unsigned QHDefault::hash(const Query& q) const
{
	unsigned	ret;

	ret = q.expr->hash();
	foreach (it, q.constraints.begin(), q.constraints.end()) {
		ref<Expr>	e = *it;
		ret ^= e->hash();
	}

	return ret;
}

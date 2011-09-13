#include "klee/Solver.h"
#include "klee/Constraints.h"
#include "static/Sugar.h"
#include "SMTPrinter.h"

#include <assert.h>
#include <stack>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <utility>

using namespace klee;

SMTPrinter::Action SMTPrinter::visitExprPost(const Expr &e)
{
	switch (e.getKind()) {
	case Expr::Eq:
	case Expr::Ult:
	case Expr::Ule:
	case Expr::Ugt:
	case Expr::Uge:

	case Expr::Slt:
	case Expr::Sle:
	case Expr::Sgt:
	case Expr::Sge:
		os << ") bv1[1] bv0[1])\n";
		break;

	case Expr::NotOptimized:
	case Expr::Constant:
		break;
	case Expr::Ne: os << "))\n"; break;
	case Expr::Add:
	default:
		os << ")\n";
	}

	return Action::doChildren();
}

SMTPrinter::Action SMTPrinter::visitExpr(const Expr &e)
{
#define VISIT_OP(x,y)	\
	case Expr::x: 		\
		os << "("#y" "; break;

// the 'ite' is to appease solvers (i.e. z3) which make a distinction
// between bv[1]'s and booleans. It might be useful to measure the
// perf with/without.
#define LOGIC_OP(x,y)	\
	case Expr::x: 		\
		os << "(ite ("#y" "; break;	\


	switch (e.getKind()) {
	case Expr::NotOptimized: break;
	case Expr::Read: {
		const ReadExpr *re = cast<ReadExpr>(&e);
		/* (select array index) */
		os	<< "( select "
			<< getArrayForUpdate(
				re->updates.root, re->updates.head)
			<< " " << expr2str(re->index)
			<< ")\n";
		return Action::skipChildren();
	}

	case Expr::Extract:
		const ExtractExpr	*ee;
		ee = dynamic_cast<const ExtractExpr*>(&e);
		os	<< "( extract["
			<< ee->offset + e.getWidth() - 1 << ':'
			<< ee->offset
			<< "] ";
		break;

	// (zero_extend[12] ?e20)  (add 12 bits)
	case Expr::ZExt:
		os	<< "( zero_extend["
			<< e.getWidth() - e.getKid(0)->getWidth() << "] ";
		break;
	// (sign_extend[12] ?e20)
	case Expr::SExt:
		os	<< "( sign_extend["
			<< e.getWidth() - e.getKid(0)->getWidth() << "] ";
		break;

	case Expr::Select:
		// logic expressions are converted into bitvectors-- 
		// convert back
		os	<< "(ite (= "
			<< expr2str(e.getKid(0)) << " bv1[1] ) "
			<< expr2str(e.getKid(1)) << " "
			<< expr2str(e.getKid(2)) << " )\n";
		return Action::skipChildren();

	VISIT_OP(Concat, concat)

	LOGIC_OP(Eq, =)
	LOGIC_OP(Ult, bvult)
	LOGIC_OP(Ule, bvule)
	LOGIC_OP(Uge, bvuge)
	LOGIC_OP(Ugt, bvugt)
	LOGIC_OP(Slt, bvslt)
	LOGIC_OP(Sle, bvsle)
	LOGIC_OP(Sgt, bvsgt)
	LOGIC_OP(Sge, bvsge)

	VISIT_OP(Add, bvadd)
	VISIT_OP(Sub, bvsub)
	VISIT_OP(Mul, bvmul)
	VISIT_OP(UDiv,bvudiv)
	VISIT_OP(SDiv, bvsdiv)
	VISIT_OP(URem, bvurem)
	VISIT_OP(SRem, bvsrem)
	VISIT_OP(Not, not)
	VISIT_OP(And, bvand)
	VISIT_OP(Or, bvor)
	VISIT_OP(Xor, bvxor)
	VISIT_OP(Shl, bvshl)
	VISIT_OP(LShr, bvlshr)
	VISIT_OP(AShr, bvashr)
	case Expr::Ne: os << "( not (="; break;
	case Expr::Constant:
		printConstant(dynamic_cast<const ConstantExpr*>(&e));
		break;
	default:
		std::cerr << "Could not handle unknown kind for: ";
		e.print(std::cerr);
		std::cerr << std::endl;
		assert("WHoops");
	}
	return Action::doChildren();
}

void SMTPrinter::printConstant(const ConstantExpr* ce)
{
	unsigned int width;

	assert (ce != NULL);

	width = ce->getWidth();

	if (width <= 64) {
		os	<< "bv"
			<< ce->getZExtValue()
			<< "[" << width << "] ";
		return;
	}

	ref<ConstantExpr> Tmp(ConstantExpr::alloc(ce->getAPValue()));
	os << "(concat ";
	os << "bv" << Tmp->Extract(0, 64)->getZExtValue() << "[64] \n";
	for (unsigned i = (width / 64) - 1; i; --i) {
		Tmp = Tmp->LShr(ConstantExpr::alloc(64, Tmp->getWidth()));

		if (i != 1) os << "(concat ";
		os	<< "bv"
			<< Tmp->Extract(0, 64)->getZExtValue()
			<< "[" << std::min(64, (int)Tmp->getWidth()) << "] ";
	}

	for (unsigned i = (width /64) - 1; i; --i) {
		if (i != 1) os << ")";
	}
	os << ")";
}

/**
 * Prints queries in SMT form.
 */
void SMTPrinter::print(std::ostream& os, const Query& q)
{
	std::stringstream	ss;
	SMTPrinter		smt_pr(ss, new SMTArrays());

	os <<	"(\nbenchmark HAVEABETTERNAME.smt \n"
		":source { klee-mc (Have better name) } \n"
		":logic QF_AUFBV\n"
		":category {industrial}\n";

	/* buffer formulas and assumptions so we know which
	 * arrays we're going to need to load before committing
	 * assumptions and formulas to the ostream */
	foreach (it, q.constraints.begin(), q.constraints.end()) {
		ss << ":assumption\n";
		ss << "(= ";
		smt_pr.visit(*it);
		ss << " bv1[1])\n";
	}

	ss << ":formula\n";
	ss << "(= ";
	smt_pr.visit(q.expr);
	ss << " bv0[1])\n";

	/* now have arrays, build extrafuns and constant assumptions */
	foreach (i,smt_pr.arr->a_initial.begin(),smt_pr.arr->a_initial.end()) {
		const Array	*arr = i->first;
		os << ":extrafuns ((" << arr2name(arr) << " Array[32:8]))\n";
	}

	foreach (it,
		smt_pr.arr->a_assumptions.begin(),
		smt_pr.arr->a_assumptions.end())
	{
		os << ":assumption " << it->second << "\n";
	}

	os << ss.str();
	os << ")\n";

	delete smt_pr.arr;
}

struct update_params
{
	update_params(const Array* r, const UpdateNode* u)
	: root(r), un(u) {}
	const Array* root;
	const UpdateNode* un;
};

const std::string SMTPrinter::getArrayForUpdate(
	const Array *root, const UpdateNode *un)
{
	std::string			ret;
	std::stack<update_params>	s;

	/* build stack */
	s.push(update_params(root, un));
	while (1) {
		update_params	p = s.top();

		if (p.un == NULL) break;
		if (arr->a_updates.count(p.un)) break;

		s.push(update_params(p.root, p.un->next));
	}

	/* unwind stack */
	while (!s.empty()) {
		update_params	p = s.top();

		s.pop();

		if (p.un == NULL) {
			ret = getInitialArray(root);
			continue;
		}

		if (arr->a_updates.count(p.un)) {
			ret = arr->a_updates.find(p.un)->second;
			arr->a_updates.insert(make_pair(p.un, ret));
			continue;
		}

		ret =	"(store " + ret + " " +
			expr2str(p.un->index) + " " +
			expr2str(p.un->value) + ")\n";

		arr->a_updates.insert(make_pair(p.un, ret));
	}

	return ret;
}

/**
Bill,

Chaining store operations in this way creates an intermediate array
value for every element of the array, which is likely to slow down
reasoning in the solver. I think you'd be better off generating a set
of assertions of the form
(= (select A 0) bvhex11[8])
(= (select A 1) bvhex22[8])
and so on.

-Chris
*/
const std::string& SMTPrinter::getInitialArray(const Array *root)
{
	std::string		arr_name;
	std::string		assump_str;

	if (arr->a_initial.count(root))
		return arr->a_initial.find(root)->second;

	arr_name = arr2name(root);
	if (!root->isConstantArray()) goto done;

	/* format:
	 * 	(and	(= (select A 0) bvconst[8])
	 * 		(= (select A 1) bvconst2[8])
	 * 		...)
	 */
	if (root->mallocKey.size > 1) assump_str = "(and\n";

	assert (root->mallocKey.size);
	for (unsigned i = 0, e = root->mallocKey.size; i != e; ++i) {
		char	idxbuf[64];
		sprintf(idxbuf, " bv%d[32]) bv%d[8])\n",
			i,
			(uint8_t)root->getValue(i)->getZExtValue(8));
		assump_str += "(= (select " + arr_name + idxbuf;
	}

	if (root->mallocKey.size > 1) assump_str += ")\n";

	arr->a_assumptions.insert(std::make_pair(root, assump_str));
done:
	arr->a_initial.insert(std::make_pair(root, arr_name));
	return arr->a_initial[root];
}

std::string SMTPrinter::arr2name(const Array* arr)
{
	return arr->name;
}

void SMTPrinter::printArrayDecls(void) const
{
	foreach (it, arr->a_initial.begin(), arr->a_initial.end()) {
		os	<< ":extrafuns (("
			<< arr2name(it->first)
			<< " Array[32:8]))\n";
	}
}

std::string SMTPrinter::expr2str(const ref<Expr>& e)
{
	std::stringstream	ss;
	std::string		fff;
	SMTPrinter		smt_pr(ss, arr);
	ConstantExpr		*ce;

	if ((ce = dyn_cast<ConstantExpr>(e)) != NULL) {
		assert (e->getWidth() <= 64);
		ss << "bv" << ce->getZExtValue() << "[" << e->getWidth() << "]";
		return ss.str();
	}

	smt_pr.visit(e);
	return ss.str();
}

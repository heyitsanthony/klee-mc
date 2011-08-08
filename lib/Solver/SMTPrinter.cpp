#include "klee/Solver.h"
#include "klee/Constraints.h"
#include "static/Sugar.h"
#include "SMTPrinter.h"

#include <assert.h>
#include <ext/rope>
#include <iostream>
#include <sstream>
#include <utility>

using namespace klee;

SMTPrinter::Action SMTPrinter::visitExprPost(const Expr &e)
{
	switch (e.getKind()) {
	case Expr::Eq:
	case Expr::Ult:
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
		std::cerr << "WHAT THE FUCK."<< (void*)this << "\n";
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


	std::cerr << "VISITING EXPR(" << (void*)this << ") " <<
		e.getNumKids() << "\n";
	e.print(std::cerr);
	std::cerr << "\n";

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

	VISIT_OP(Select, ite)
	VISIT_OP(Concat, concat)

	LOGIC_OP(Eq, =)
	LOGIC_OP(Ult, bvult)
	LOGIC_OP(Ule, bvugt)
	LOGIC_OP(Uge, bvuge)
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
		assert (e.getWidth() <= 64);
		os	<< "bv"
			<< static_cast<const ConstantExpr&>(e).getZExtValue()
			<< "[" << e.getWidth() << "] ";
		break;
	default:
		std::cerr << "Could not handle unknown kind for: ";
		e.print(std::cerr);
		std::cerr << std::endl;
		assert("WHoops");
	}
	std::cerr << "DOING CHILDREN\n";
	return Action::doChildren();
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
	ss << " bv1[1])\n";

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

const std::string& SMTPrinter::getArrayForUpdate(
	const Array *root, const UpdateNode *un)
{
	std::string		ret, mid;

	if (un == NULL) return getInitialArray(root);

	if (arr->a_updates.count(un))
		return arr->a_updates.find(un)->second;

	// FIXME: This really needs to be non-recursive.
	mid = getArrayForUpdate(root, un->next);
	ret =	"(store " + 
		mid + " " +
		expr2str(un->index) + " " + 
		expr2str(un->value) + ")\n";
	

	arr->a_updates.insert(make_pair(un, ret));
	return arr->a_updates[un];
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

#define str2rope(x)	__gnu_cxx::rope<char>(x)

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
		sprintf(idxbuf,
			" bv%d[32]) bv%d[8] )\n",
			i,
			((uint8_t)root->constantValues[i]->getZExtValue()));
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
	char	buf[128];
	sprintf(buf, "%s_%p", arr->name.c_str(), (const void*)arr);
	return std::string(buf);
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
	const ConstantExpr	*ce;

	if (ce = dyn_cast<const ConstantExpr>(e)) {
		ss << "bv" << ce->getZExtValue() << "[" << e->getWidth() << "]";
		return ss.str();
	}

	smt_pr.visit(e);
	return ss.str();
}

#include "klee/Solver.h"
#include "klee/Constraints.h"
#include "klee/util/ExprMinimizer.h"
#include "static/Sugar.h"
#include "SMTPrinter.h"
#include "klee/util/ConstantDivision.h"
#include "llvm/Support/CommandLine.h"

#include <assert.h>
#include <queue>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <fstream>
#include <utility>

using namespace klee;
using namespace llvm;

namespace {
	cl::opt<bool>
	SpecializeSimpleEquality(
		"special-simple-equality",
		cl::init(true),
		cl::desc("Use short-form for SMT equalities"));
	cl::opt<bool>
	MinimizeSMT(
		"smt-minimize",
		 cl::init(false), // experimental
		 cl::desc(
		"Minimize SMT requests with let-exprs (default: off)"));

	cl::opt<bool>
	SMTLetArrays(
		"smt-let-arrays",
		cl::init(true),
		cl::desc("Use let expressions for all arrays (default: on)"));

	cl::opt<bool>
	OptimizeSMTMul(
		"smt-optmul",
		 cl::init(false),
		 cl::desc("Shifting multiply for const*sym (default: false)"));

	cl::opt<bool>
	XChkSMTMul(
		"smt-xchkmul",
		 cl::init(false),
		 cl::desc("Test multiplying optimizations (default: off)"));

	cl::opt<bool>
	UseBrokenOptMul(
		"smt-brokenoptmul",
		cl::init(false),
		// "/sbin/fsck.cramfs asd"; source of the old cuserid bug.
		// run with -xchk-exprbuilder to get explosions
		cl::desc("Use broken optimized multiply to test exprxchk."));
}

/* This figures out which arrays are being used in an
 * expression. This is important for SMT because it needs array information
 * up front, before the formula is given.
 *
 * It also keeps track of arrays assigned to expressions.
 * Every expression pushes a new array set on the exprlog vector
 */
class ArrayFinder : public ExprConstVisitor
{
public:
	ArrayFinder(SMTPrinter::SMTArrays* in_arr)
	: cur_let_exprlog(NULL)
	, arr(in_arr)
	{}

	virtual ~ArrayFinder(void) {}

	void addExprArrays(const ref<Expr>& e)
	{
		expr_updates_ty	eu;
		cur_let_exprlog = &eu;
		apply(e);
		visited_exprs.clear();

		used_let_exprlog.push(eu);
		cur_let_exprset.clear();
		cur_let_exprlog = NULL;
	}

	std::list<update_pair> getNextExprLog(void)
	{
		expr_updates_ty	ret(used_let_exprlog.front());
		used_let_exprlog.pop();
		return ret;
	}

protected:
	virtual Action visitExpr(const Expr* e);
	virtual void visitExprPost(const Expr* expr);

private:
	// let bindings
	typedef std::list<update_pair> expr_updates_ty;

	typedef std::map<update_pair, ref<Expr> >
		bindings_ty;

	bindings_ty			bindings;
	std::queue<expr_updates_ty>	used_let_exprlog;
	expr_updates_ty			*cur_let_exprlog;
	std::set<update_pair>		cur_let_exprset;
	std::set<const Expr*>		visited_exprs;

	SMTPrinter::SMTArrays*		arr;
};

void ArrayFinder::visitExprPost(const Expr* e)
{
	const ReadExpr		*re;
	update_pair		upp;

	if (e->getKind() != Expr::Read)
		return;

	re = static_cast<const ReadExpr*>(e);
	upp = update_pair(re->updates.getRoot().get(), re->updates.head);

	if (cur_let_exprset.insert(upp).second == true) {
		cur_let_exprlog->push_back(upp);
	}
}

ExprConstVisitor::Action ArrayFinder::visitExpr(const Expr* e)
{
	bindings_ty::iterator	it;
	ref<Expr>		let_expr;
	const ReadExpr		*re;
	update_pair		upp;

	/* already seen it? */
	if (visited_exprs.find(e) != visited_exprs.end())
		return Close;

	/* make note */
	visited_exprs.insert(e);

	if (e->getKind() != Expr::Read)
		return Expand;

	ref<Expr>	e_ref(const_cast<Expr*>(e));
	re = cast<ReadExpr>(e_ref);

	upp = update_pair(re->updates.getRoot().get(), re->updates.head);
	it = bindings.find(upp);
	if (it != bindings.end()) {
		// seen it elsewhere

		// have we inserted it into this expr log?
		if (!cur_let_exprset.count(upp)) {
			/* nope-- there might be
			 * interesting stuff in the index we haven't
			 * seen yet */
			return Expand;
		}

		// seen both globally and locally, no need to
		// look at any further
		// return Skip;
		return Expand;
	}

	/* record expression */
	arr->getInitialArray(re->updates.getRoot().get());

	let_expr = LetExpr::alloc(e_ref, ConstantExpr::create(0, 1));
	bindings.insert(std::make_pair(upp, let_expr));

	arr->a_updates[upp] = let_expr;

	return Expand;
}


void SMTPrinter::visitExprPost(const Expr *e)
{
	switch (e->getKind()) {
	case Expr::Eq:
	case Expr::Ult:
	case Expr::Ule:
	case Expr::Ugt:
	case Expr::Uge:

	case Expr::Slt:
	case Expr::Sle:
	case Expr::Sgt:
	case Expr::Sge:
		os << ") bv1[1] bv0[1]) \n";
		break;

	case Expr::Bind:
	case Expr::NotOptimized:
	case Expr::Constant:
		break;
	case Expr::Ne: os << ") bv0[1] bv1[1])\n"; break;
	case Expr::Add:
	default:
		os << ")\n";
	}
}

SMTPrinter::Action SMTPrinter::visitExpr(const Expr* e)
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

	switch (e->getKind()) {
	case Expr::NotOptimized:
		if (OptimizeSMTMul && e->getKid(0)->getKind() == Expr::Mul) {
			os << "(bvmul ";
			expr2os(e->getKid(0)->getKid(0), os);
			os << " ";
			expr2os(e->getKid(0)->getKid(1), os);
			os << ")\n";
			return Close;
		}
		break;
	case Expr::Read: {
		const ReadExpr *re = static_cast<const ReadExpr*>(e);
		/* (select array index) */
		os << "( select ";
		if (SMTLetArrays) {
			writeArrayForUpdate(os, re);
		} else {
			writeExpandedArrayForUpdate(
				os, re->updates.getRoot().get(), re->updates.head);
		}
		os << " ";
		expr2os(re->index, os);
		os << ")\n";
		return Close;
	}

	case Expr::Extract:
		const ExtractExpr	*ee;
		ee = static_cast<const ExtractExpr*>(e);
		os	<< "( extract["
			<< ee->offset + e->getWidth() - 1 << ':'
			<< ee->offset
			<< "] ";
		break;

	// (zero_extend[12] ?e20)  (add 12 bits)
	case Expr::ZExt: {
		int	ext_bits;
		ext_bits = e->getWidth() - e->getKid(0)->getWidth();
		if (ext_bits >= 0) {
			os << "( zero_extend[" << ext_bits << "] ";
		} else {
			os << "( extract[" << e->getWidth()-1 << ":0] ";
		}
		break;
	}
	// (sign_extend[12] ?e20)
	case Expr::SExt:
		int	ext_bits;
		ext_bits = e->getWidth() - e->getKid(0)->getWidth();
		if (ext_bits >= 0) {
			os << "( sign_extend[" << ext_bits  << "] ";
		} else {
			os << "( extract[" << e->getWidth()-1 << ":0] ";
		}
		break;

	case Expr::Select:
		// logic expressions are converted into bitvectors--
		// convert back
		os	<< "(ite (= ";
			expr2os(e->getKid(0), os); os << " bv1[1] ) ";
			expr2os(e->getKid(1), os); os << " ";
			expr2os(e->getKid(2), os); os << " )\n";
		return Close;

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
	case Expr::Mul:
		if (OptimizeSMTMul) {
			if (printOptMul(static_cast<const MulExpr*>(e)))
				return Close;
		}
		os << "(bvmul ";
		break;
	VISIT_OP(UDiv,bvudiv)
	VISIT_OP(SDiv, bvsdiv)
	VISIT_OP(URem, bvurem)
	VISIT_OP(SRem, bvsrem)
	VISIT_OP(And, bvand)
	VISIT_OP(Or, bvor)
	VISIT_OP(Xor, bvxor)
	VISIT_OP(Shl, bvshl)
	VISIT_OP(LShr, bvlshr)
	VISIT_OP(AShr, bvashr)

	case Expr::Ne: os << "(ite ( = "; break;
	case Expr::Not:
		os << "(bvnot ";
		break;

	case Expr::Bind:
		os	<< "?e"
			<< static_cast<const BindExpr*>(e)->let_expr->getId()
			<< " ";
		break;
	case Expr::Constant:
		printConstant(static_cast<const ConstantExpr*>(e));
		break;
	default:
		std::cerr << "Could not handle unknown kind for: ";
		e->print(std::cerr);
		std::cerr << std::endl;
		assert("WHoops");
	}
	return Expand;
}

extern void xchkExpr(const ref<Expr>& oracle, const ref<Expr>& test);

#define CREATE_OPTMUL_EV(e,v)	\
	((!UseBrokenOptMul) 	\
		? Expr::createBoothMul(e, v)	\
		: Expr::createShiftAddMul(e, v))

#define CREATE_OPTMUL(e,ce)	CREATE_OPTMUL_EV(e, ce->getZExtValue())

/* returns false if not possible to optimize the multiplication */
bool SMTPrinter::printOptMul(const MulExpr* me) const
{
	const ConstantExpr	*ce;
	ref<Expr>		mul_ref(const_cast<Expr*>((Expr*)me));

	ce = dyn_cast<ConstantExpr>(me->left);
	if (ce == NULL) return false;

	if (ce->getWidth() <= 64) {
		if (XChkSMTMul)
		xchkExpr(
			NotOptimizedExpr::create(mul_ref),
			CREATE_OPTMUL(me->right, ce));
		printOptMul64(me->right, ce->getZExtValue());
		return true;
	}

	/* so long as the multiply only has 64 active bits,
	 * we can cheat and use the sweet multiply transforms that
	 * solvers don't bother to use */
	if (ce->getWidth() <= 128) {
		ref<Expr>		e_hi, e_lo;
		const ConstantExpr	*ce_hi, *ce_lo;

		e_hi = ExtractExpr::create(me->left, 64, ce->getWidth()-64);
		e_lo = ExtractExpr::create(me->left, 0, 64);
		ce_hi = dyn_cast<ConstantExpr>(e_hi);
		ce_lo = dyn_cast<ConstantExpr>(e_lo);

		assert (ce_hi && ce_lo);
		if (ce_hi->isZero()) {
			if (XChkSMTMul)
			xchkExpr(
				NotOptimizedExpr::create(mul_ref),
				CREATE_OPTMUL(me->right, ce_lo));
			printOptMul64(me->right, ce_lo->getZExtValue());
			return true;
		}
	}

	return false;
}

// multiply expr by v
void SMTPrinter::printOptMul64(const ref<Expr>& expr, uint64_t v) const
{
	ref<Expr>	cur_expr(CREATE_OPTMUL_EV(expr, v));
	expr2os(cur_expr, os);
}

void SMTPrinter::printConstant(const ConstantExpr* ce) const
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

	/* This code gave me some trouble. STP and others may
	 * have their API backwards-- the LShr code mixes up endianness.
	 * I should check this out later. */
	ref<ConstantExpr> Tmp(ConstantExpr::alloc(ce->getAPValue()));
	os << "(concat ";
	os << "bv" << Tmp->Extract(width-64, 64)->getZExtValue() << "[64] \n";
	for (unsigned i = 1; i < (width+63)/64; i++) {
		unsigned int	begin, end;

		end = width-i*64;
		begin =  (width < (i+1)*64) ? 0 : width-(i+1)*64;
		if (i != 1) os << "(concat ";
		os	<< "bv"
			<< Tmp->Extract(begin, end - begin)->getZExtValue()
			<< "[" << (end - begin)  << "] ";
	}

	for (unsigned i = (width /64) - 1; i; --i) {
		if (i != 1) os << ") ";
	}
	os << ") ";
}

/**
 * Prints queries in SMT form.
 */
void SMTPrinter::print(std::ostream& os, const Query& q, bool printConsts)
{
	SMTArrays		*smt_arr = new SMTArrays();
	SMTPrinter		smt_pr(os, smt_arr);
	ArrayFinder		arr_finder(smt_arr);

	os <<	"(\nbenchmark EVERYTHINGISBROKEN.smt \n"
		":source { klee-mc (Everything is broken) } \n"
		":logic QF_AUFBV\n"
		":category {industrial}\n";

	/* do a first pass on expressions so we know
	 * which arrays we're going to need to load before committing
	 * assumptions and formulas to the ostream */
	foreach (it, q.constraints.begin(), q.constraints.end())
		arr_finder.addExprArrays(*it);
	arr_finder.addExprArrays(q.expr);

	/* now have arrays, declare them */
	smt_pr.print_const_arrays = printConsts;
	smt_pr.printArrayDecls();

	/* print constraints */
	foreach (it, q.constraints.begin(), q.constraints.end()) {
		std::list<update_pair> s(arr_finder.getNextExprLog());
		smt_pr.printConstraint(*it, s);
	}

	/* print expr */
	std::list<update_pair>	s(arr_finder.getNextExprLog());
	char			bv[32];
	sprintf(bv, "bv0[%d]", q.expr->getWidth());
	smt_pr.printConstraint(q.expr, s, ":formula", bv);

	// closing parens for (benchmark)
	os << ")\n";

	delete smt_arr;
}

bool SMTPrinter::tryPrintSimpleEqConstraint(const ref<Expr>& e) const
{
	const EqExpr		*ee;
	const ReadExpr		*re;
	const ConstantExpr	*idx, *ce;

	ee = dyn_cast<EqExpr>(e);
	if (ee == NULL)
		return false;

	ce = dyn_cast<ConstantExpr>(ee->getKid(0));
	if (ce == NULL)
		return false;

	re = dyn_cast<ReadExpr>(ee->getKid(1));
	if (re == NULL)
		return false;

	idx = dyn_cast<ConstantExpr>(re->index);
	if (idx == NULL)
		return false;

	if (re->updates.head != NULL)
		return false;

	os << "(= bv" << ce->getZExtValue() << "[8] "
		<< "(select "
			<< re->updates.getRoot().get()->name <<
			" bv" << idx->getZExtValue() << "[32]))\n";
	return true;
}

void SMTPrinter::printConstraint(
	const ref<Expr>& e,
	const std::list<update_pair>& updates,
	const char* key,  const char* val)
{
	unsigned int		let_c;
	ref<Expr>		min_expr;
	LetExpr			*le;

	os << '\n' << key << '\n';

	/* silly fast path:
	 * Often see
	 * :assumption
	 * (let (?e122 x)
	 * 	(= (ite (= bv0[8] ( select ?e122  bv7[32] ))
	 * 		bv1[1] bv0[1])
	 *	bv1[1]))
	 * Can rewrite this as:
	 * :assumption (= (= bv0[8] (select x bv7[32])))
	 * Fewer bytes, less processing, everyone wins.
	 */
	if (SpecializeSimpleEquality && key[1] == 'a') {
		if (tryPrintSimpleEqConstraint(e))
			return;
	}

	if (MinimizeSMT) {
		min_expr = ExprMinimizer::minimize(e);
	} else {
		min_expr = e;
	}

	/* forge let expressions for updates */
	let_c = 0;
	if (SMTLetArrays) {
	foreach (it, updates.begin(), updates.end()) {
		update_pair		upp(*it);
		const ReadExpr		*re;

		le = dyn_cast<LetExpr>((*(arr->a_updates.find(upp))).second);
		assert (le != NULL);
		re = dyn_cast<ReadExpr>(le->getBindExpr());
		assert (re != NULL);

		os << "(let (?e" << le->getId() << ' ';
		writeExpandedArrayForUpdate(os, upp.first, upp.second);
		os << ")\n";

		let_c++;
	}
	}

	/* dump let expressions, if any */
	while ((le = dyn_cast<LetExpr>(min_expr)) != NULL) {
		os << "(let (?e" << le->getId() << ' ';
		expr2os(le->getBindExpr(), os);
		os << ")\n";
		let_c++;
		min_expr = le->getScopeExpr();
	}


	os << "(= ";
	apply(min_expr);
	os << ' ' << val << ')';

	for (unsigned i = 0; i < let_c; i++)
		os << ')';

	os << "\n";
}

struct update_params
{
	update_params(const Array* r, const UpdateNode* u)
	: root(r), un(u) {}
	const Array* root;
	const UpdateNode* un;
};

void SMTPrinter::writeArrayForUpdate(
	std::ostream& os,
	const ReadExpr* re)
{
	/* this should be a reference to a let */
	std::map<update_pair, ref<Expr> >::iterator	it;
	const LetExpr*	le;

	it = arr->a_updates.find(
		update_pair(re->updates.getRoot().get(), re->updates.head));
	if (it == arr->a_updates.end()) {
		assert (0 == 1 && "Expected update node to exist!");
		return;
	}

	le = dyn_cast<LetExpr>((*it).second);
	assert (le != NULL && "Expected let expr");

	os << "?e" << le->getId() << " ";
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
/* this is kind of obfuscated, but it's necessary to avoid
 * some nasty recursion eating all memory */
void SMTPrinter::writeExpandedArrayForUpdate(
	std::ostream& os,
	const Array* root, const UpdateNode *un)
{
	std::string			ret;
	std::vector<update_params>	s;

	/* build stack */
	s.push_back(update_params(root, un));
	while (1) {
		update_params	&p(s.back());

		if (p.un == NULL) break;

		s.push_back(update_params(p.root, p.un->next));
	}

	/**
	 * ret = "(store " ret " " p.un->index " " p.un->value " )\n"
	 * Write out all "(store"'s first.
	 */
	for (unsigned int i = 0; i < s.size() - 1; i++) {
		os << "(store ";
	}

	/* dump update params */
	for (int i = s.size() - 1; i >= 0; i--) {
		update_params	&p(s[i]);

		if (p.un == NULL) {
			os << getInitialArray(root);
			continue;
		}

		os << ' ';
		expr2os(p.un->index, os);
		os << ' ';
		expr2os(p.un->value, os);
		os << ")\n";
	}
}

const std::string& SMTPrinter::getInitialArray(const Array *root)
{
	return arr->getInitialArray(root);
}

const std::string& SMTPrinter::SMTArrays::getInitialArray(const Array* root)
{
	std::string		assump_str;

	if (a_initial.count(root))
		return a_initial.find(root)->second;

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
		assump_str += "(= (select " + root->name + idxbuf;
	}

	if (root->mallocKey.size > 1) assump_str += ")\n";

	a_const_decls.insert(std::make_pair(root, assump_str));
done:
	a_initial.insert(std::make_pair(root, root->name));
	return a_initial[root];
}

void SMTPrinter::printArrayDecls(void) const
{
	/* declarations */
	foreach (it, arr->a_initial.begin(), arr->a_initial.end()) {
		const Array	*a = it->first;
		os << ":extrafuns\n((" << a->name << " Array[32:8]))\n";
	}

	/* print constant array values */
	if (!print_const_arrays)
		return;

	foreach (it, arr->a_const_decls.begin(), arr->a_const_decls.end()) {
		os << ":assumption\n" << it->second << "\n";
	}
}

void SMTPrinter::expr2os(const ref<Expr>& e, std::ostream& os) const
{
	ConstantExpr		*ce;

	if ((ce = dyn_cast<ConstantExpr>(e)) != NULL) {
		printConstant(ce);
		return;
	}

	if (!os.good()) return;

	SMTPrinter	smt_pr(os, arr);
	smt_pr.apply(e);
}

void SMTPrinter::dumpToFile(const Query& q, const char* fname, bool printConsts)
{
	std::ofstream	os(fname, std::ios::out);
	print(os, q, printConsts);
}

void SMTPrinter::dump(const Query& q, const char* prefix)
{
	char	fname[256];
	sprintf(fname, "%s.%lx.smt", prefix, q.hash());
	dumpToFile(q, fname);
}

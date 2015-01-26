#include <llvm/Support/CommandLine.h>
#include <sys/types.h>
#include <dirent.h>
#include <iostream>
#include <sstream>
#include "klee/Internal/ADT/LimitedStream.h"

#include "../Core/StateSolver.h"
#include "../Expr/SMTParser.h"
#include "../Solver/SMTPrinter.h"
#include "klee/util/ExprUtil.h"
#include "static/Sugar.h"
#include "klee/ExecutionState.h"
#include "../Core/Executor.h"
#include "ConstraintSeedCore.h"

using namespace klee;

namespace
{
	llvm::cl::opt<bool>
	ConstraintSolveSeeds(
		"constrseed-solve",
		llvm::cl::desc("Save constraints from stuck branches."),
		llvm::cl::init(false));

	llvm::cl::opt<std::string>
	ConstraintSeedDir(
		"constrseed-dir",
		llvm::cl::desc("Satisfiable OOB pointer expr dump dir."),
		llvm::cl::init("ptrexprs/"));
};

ConstraintSeedCore::ConstraintSeedCore(Executor* _exe)
: exe(_exe)
{
	DIR		*dir;
	struct dirent	*de;
	unsigned	expr_c;

	dir = opendir(ConstraintSeedDir.c_str());
	assert (dir != NULL);

	std::cerr	<< "[ConstraintSeed] Loading constraint directory: "
			<< ConstraintSeedDir << '\n';

	expr_c = 0;
	while ((de = readdir(dir)) != NULL) {
		std::stringstream		ss;
		std::string			path;

		ss << ConstraintSeedDir << "/" << de->d_name;
		path = ss.str();
		if (loadConstraintFile(path))
			expr_c++;
	}

	// assert (!name2exprs.empty() && "No constraints loaded??");
	std::cerr << "[ConstraintSeed] Loaded " << expr_c << " exprs.\n";
}

ConstraintSeedCore::~ConstraintSeedCore()
{
	foreach (it, name2exprs.begin(), name2exprs.end())
		delete it->second;
}

bool ConstraintSeedCore::loadConstraintFile(const std::string& path)
{
	ref<Expr>			e;
	expr::SMTParser			*p;
	std::vector<ref<Array> >	arrs;
	bool				ret = false;

	p = expr::SMTParser::Parse(path);
	if (p == NULL)
		return false;

	e = p->satQuery;
	if (e.isNull()) goto done;

	ExprUtil::findSymbolicObjectsRef(e, arrs);
	if (arrs.empty()) goto done;

	/* add expression for all labels */
	// NOTE: e should not be 'NOTed' because the constraints passed in
	// must be TRUE to find new branches.
	// e = MK_NOT(e);
	foreach (it, arrs.begin(), arrs.end())
		addExprToLabel((*it)->name, e);

	ret = true;
done:
	delete p;
	return ret;
}

bool ConstraintSeedCore::isExprAdmissible(
	const exprlist_ty* exprs, const ref<Expr>& e)
{
	/* check conjunction */
	ref<Expr>	base_e(0);
	ref<Expr>	full_constr;
	bool		mbt;

	foreach (it, exprs->begin(), exprs->end()) {
		base_e = (base_e.isNull())
			? *it
			: MK_OR(*it, base_e);
	}

	if (base_e.isNull())
		return true;

	/* try adding the expressoin, check for validity */
	full_constr = MK_OR(e, base_e);
	if (!exe->getSolver()->getSolver()->mustBeTrue(Query(full_constr), mbt))
		return false;

	/* valid; oops */
	if (mbt == true)
		return false;

	/* is the rule useless? */
	if (!exe->getSolver()->getSolver()->mustBeTrue(
		Query(MK_EQ(full_constr, base_e)), mbt))
		return false;

	/* subsumed => useless */
	if (mbt == true)
		return false;

	return true;
}

bool ConstraintSeedCore::addExprToLabel(const std::string& s, const ref<Expr>& e)
{
	name2exprs_ty::iterator		it;
	exprlist_ty			*exprs;

	it = name2exprs.find(s);
	if (it == name2exprs.end()) {
		exprs = new exprlist_ty();
		name2exprs.insert(std::make_pair(s, exprs));
	} else
		exprs = it->second;

	if (isExprAdmissible(exprs, e) == false)
		return false;

	exprs->push_back(e);

	return true;
}

class ReplaceArrays : public ExprVisitor
{
public:
	typedef std::map<std::string, const ref<Array> > arrmap_ty;
	typedef std::list<ref<Expr> > guardlist_ty;

	ReplaceArrays(arrmap_ty& _arrmap)
	: arrmap(_arrmap) {}
	virtual ~ReplaceArrays() {}

	virtual ref<Expr> apply(const ref<Expr>& e)
	{
		ref<Expr>	ret;
		goodExpr = true;
		ret = ExprVisitor::apply(e);
		return (goodExpr) ? ret : NULL;
	}

	virtual Action visit(const Expr& e)
	{
		if (goodExpr == false)
			return Action::skipChildren();

		return Action::doChildren();
	}

	virtual Action visitExprPost(const Expr& e)
	{
		switch(e.getKind()) {
		default: break;
		case Expr::UDiv:
		case Expr::SDiv:
		case Expr::URem:
		case Expr::SRem:
			/* can't divide by zero! */
			guards.push_back(
				MK_NE(MK_CONST(0, e.getWidth()), e.getKid(1)));
		}

		return Action::skipChildren();
	}

	virtual Action visitRead(const ReadExpr& re)
	{
		arrmap_ty::iterator	it;
		const ConstantExpr	*ce;
		ref<Expr>		idx;
		const ref<Array>	repl_arr;

		if (!goodExpr) return Action::skipChildren();

		it = arrmap.find(re.getArray()->name);
		if (it == arrmap.end()) {
			/* could not find matching replacement array */
			goodExpr = false;
			std::cerr
				<< "BAD UNIFIY WITH ARR="
				<< re.getArray()->name << '\n';
			return Action::skipChildren();
		}

		repl_arr = it->second;

		/* out of bounds */
		ce = dyn_cast<ConstantExpr>(re.index);
		if (ce != NULL && ce->getZExtValue() > repl_arr->getSize()) {
			goodExpr = false;
			std::cerr << "IDX OOB: " << ce->getZExtValue() << " vs "
				<< repl_arr->getSize() << '\n';
			return Action::skipChildren();
		}

		idx = apply(re.index);
		if (idx.isNull()) {
			goodExpr = false;
			return Action::skipChildren();
		}

		return Action::changeTo(
			MK_READ(UpdateList(repl_arr, NULL), idx));
	}

	guardlist_ty::const_iterator beginGuards(void) const
	{ return guards.begin(); }
	guardlist_ty::const_iterator endGuards(void) const
	{ return guards.end(); }
	void clearGuards(void) { guards.clear(); }
private:
	std::list<ref<Expr> >	guards;
	arrmap_ty&		arrmap;
	bool			goodExpr;
};

ref<Expr> ConstraintSeedCore::getConjunction(
	ExprVisitor* evv, const exprlist_ty* exprs) const
{
	ReplaceArrays	*ev = dynamic_cast<ReplaceArrays*>(evv);
	ref<Expr>	ret(NULL);
	unsigned	expr_c;
	unsigned	exprs_used_c;
	int		*idxs;

	expr_c = exprs->size();
	idxs = new int[expr_c];

	for (unsigned i = 0; i < expr_c; i++) idxs[i] = i;
	for (unsigned i = 0; i < expr_c; i++) {
		int	t, r;
		r = rand() % expr_c;
		t = idxs[i];
		idxs[i] = idxs[r];
		idxs[r] = t;
	}

	exprs_used_c = 0;
	for (unsigned i = 0; i < expr_c /* && exprs_used_c < 3 */; i++) {
		ref<Expr>	seed_constr((*exprs)[idxs[i]]);
		ref<Expr>	state_constr, tmp_ret;
		bool		mbt;

		state_constr = ev->apply(seed_constr);
		if (state_constr.isNull()) {
			std::cerr << "[CSCore] Couldn't unify constraint\n";
			continue;
		}

		foreach (it, ev->beginGuards(), ev->endGuards())
			state_constr = MK_AND(*it, state_constr);
		ev->clearGuards();

		if (ret.isNull()) {
			ret  = state_constr;
			exprs_used_c++;
			continue;
		}

		tmp_ret  = MK_AND(state_constr, ret);
		if (!exe->getSolver()->getSolver()->mayBeTrue(
			Query(tmp_ret), mbt))
			break;

		if (mbt == false)
			break;

		exprs_used_c++;
		ret = tmp_ret;
	}

	delete [] idxs;

	std::cerr << "[CSCore] Used " << exprs_used_c << " expressions.\n";
	return ret;
}

ref<Expr> ConstraintSeedCore::getDisjunction(
	ExprVisitor* evv, const exprlist_ty* exprs) const
{
	ref<Expr>	ret(NULL);
	ReplaceArrays	*ev = dynamic_cast<ReplaceArrays*>(evv);

	foreach (it, exprs->begin(), exprs->end()) {
		ref<Expr>	seed_constr(*it);
		ref<Expr>	state_constr;

		state_constr = ev->apply(seed_constr);
		if (state_constr.isNull()) {
			std::cerr << "[CSCore] Couldn't unify constraint\n";
			continue;
		}

		foreach (it, ev->beginGuards(), ev->endGuards())
			state_constr = MK_AND(*it, state_constr);
		ev->clearGuards();

		if (ret.isNull())
			ret  = state_constr;
		else
			ret = MK_OR(state_constr, ret);

	}

	return ret;
}

static void checkNoRepeats(
	const ReplaceArrays::arrmap_ty	&arrmap,
	const ref<Expr>			&constr)
{
	std::vector<ref<Array> >	arrs;
	std::set<std::string>		arrnames;
	ExprUtil::findSymbolicObjectsRef(constr, arrs);
	foreach (it3, arrs.begin(), arrs.end()) {
		assert (arrnames.count((*it3)->name) == 0);
		arrnames.insert((*it3)->name);
		assert (arrmap.count((*it3)->name));
	}
}

void ConstraintSeedCore::addSeedConstraints(
	ExecutionState& state, const ref<Array> arr)
{
	name2exprs_ty::const_iterator	it;
	ref<Expr>			constr(NULL);
	bool				mbt;

	it = name2exprs.find(arr->name);
	if (it == name2exprs.end())
		return;

	ReplaceArrays::arrmap_ty	arrmap;
	ReplaceArrays			ra(arrmap);

	for (auto &sym : state.getSymbolics()) {
		const ref<Array>	arr(sym.getArrayRef());
		arrmap.insert(std::make_pair(arr->name, arr));
	}

	std::cerr << "===================\n";
//	constr = getDisjunction(&ra, it->second);
	constr = getConjunction(&ra, it->second);
	if (constr.isNull()) {
		std::cerr << "NULL CONSTR??\n";
		return;
	}

	checkNoRepeats(arrmap, constr);

	if (!exe->getSolver()->mayBeTrue(state, constr, mbt) || !mbt) {
		std::cerr << "[CSCore] Seed/State Contradiction.\n";
		return;
	}

	if (!exe->addConstraint(state, constr)) {
		std::cerr << "Couldn't add seed constraint! (going anyway)\n";
		state.printConstraints(std::cerr);
	}


	std::cerr << ">>>>>>>>>>>>LOLLLLLLL<<<<<<<<<\n" << arr->name << '\n';;
	std::cerr << "ADDED CONSTRS: " << constr << '\n';
	std::cerr << "===================\n";
}

#define MAX_PTR_EXPR_BYTES	(1024*100)
static void dumpSeedCondition(const ref<Expr>& e)
{
	std::stringstream	ss;
	ss << ConstraintSeedDir << "/off." << (void*)e->hash() << ".smt";
	limited_ofstream	lofs(ss.str().c_str(), MAX_PTR_EXPR_BYTES);
	std::cerr << "[CSCore] DUMPING TO: " << ss.str() << '\n';
	SMTPrinter::print(lofs, Query(e));
}

bool ConstraintSeedCore::isActive(void) { return ConstraintSolveSeeds; }

bool ConstraintSeedCore::logConstraint(const ref<Expr> e)
{
	bool				mbt;

	if (!ConstraintSolveSeeds)
		return false;

	if (hashes.count(e->hash()))
		return false;

	hashes.insert(e->hash());
	if (!exe->getSolver()->getSolver()->mayBeTrue(Query(e), mbt)) {
		std::cerr << "[CSCore] Ignoring bad solver call.\n";
		return false;
	}

	if (mbt == false) {
		std::cerr << "[CSCore] Ignoring tautology\n";
		return false;
	}

	dumpSeedCondition(e);
	return true;
}

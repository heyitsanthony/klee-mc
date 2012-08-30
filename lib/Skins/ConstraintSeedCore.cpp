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
		llvm::cl::desc("Solve for out of bound constraints."),
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

	p = expr::SMTParser::Parse(path);
	if (p == NULL)
		return false;

	e = p->satQuery;
	if (e.isNull())
		return false;

	ExprUtil::findSymbolicObjectsRef(e, arrs);
	if (arrs.empty())
		return false;

	/* add expression for all labels */
	e = NotExpr::create(e);
	std::cerr << "HI!! " << e << '\n';
	foreach (it, arrs.begin(), arrs.end())
		addExprToLabel((*it)->name, e);

	return true;
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
	full_constr = OrExpr::create(e, base_e);
	if (!exe->getSolver()->solver->mustBeTrue(Query(full_constr), mbt))
		return false;

	/* valid; oops */
	if (mbt == true)
		return false;

	/* is the rule useless? */
	if (!exe->getSolver()->solver->mustBeTrue(
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
			std::cerr << "Could not unify the seed constraint!\n";
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

	it = name2exprs.find(arr->name);
	if (it == name2exprs.end())
		return;

	ReplaceArrays::arrmap_ty	arrmap;
	ReplaceArrays			ra(arrmap);

	foreach (it2, state.symbolicsBegin(), state.symbolicsEnd()) {
		const ref<Array>	arr(it2->getArrayRef());
		arrmap.insert(std::make_pair(arr->name, arr));
	}

	std::cerr << "===================\n";
	constr = getDisjunction(&ra, it->second);
	if (constr.isNull()) {
		std::cerr << "NULL CONSTR??\n";
		return;
	}

	checkNoRepeats(arrmap, constr);

	if (!exe->addConstraint(state, constr)) {
		std::cerr << "Couldn't add seed constraint! (going anyway)\n";
		std::cerr << "STATE DUMP:\n";
		state.printConstraints(std::cerr);
	}
	std::cerr << ">>>>>>>>>>>>LOLLLLLLL<<<<<<<<<\n" << arr->name << '\n';;
	std::cerr << "ADDED CONSTRS: " << constr << '\n';
	std::cerr << "===================\n";
}

#define MAX_PTR_EXPR_BYTES	(1024*100)
static void dumpOffset(const ref<Expr>& e)
{
	std::stringstream	ss;
	ss << ConstraintSeedDir << "/off." << (void*)e->hash() << ".smt";
	limited_ofstream	lofs(ss.str().c_str(), MAX_PTR_EXPR_BYTES);
	std::cerr << "[CSCore] DUMPING TO: " << ss.str() << '\n';
	SMTPrinter::print(lofs, Query(e));
}

bool ConstraintSeedCore::isActive(void) { return ConstraintSolveSeeds; }

bool ConstraintSeedCore::logConstraint(Executor* ex, const ref<Expr> e)
{
	static std::set<Expr::Hash>	hashes;
	bool				mbt;

	if (!ConstraintSolveSeeds)
		return false;

	if (hashes.count(e->hash()))
		return false;

	hashes.insert(e->hash());
	if (!ex->getSolver()->solver->mayBeTrue(Query(e), mbt))
		return false;

	if (mbt == false)
		return false;

	dumpOffset(e);
	return true;
}

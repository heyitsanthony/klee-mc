#include <llvm/Support/CommandLine.h>
#include <sys/types.h>
#include <dirent.h>
#include <iostream>
#include <sstream>

#include "TimingSolver.h"
#include "../Expr/SMTParser.h"
#include "klee/util/ExprUtil.h"
#include "static/Sugar.h"
#include "klee/ExecutionState.h"
#include "Executor.h"
#include "ConstraintSeedCore.h"

using namespace klee;

namespace
{
	llvm::cl::opt<std::string>
	ConstraintSeedDir(
		"constraint-seed-dir",
		llvm::cl::desc("Directory of constraints for seeding."),
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

	assert (!name2exprs.empty() && "No constraints loaded??");
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

#if 0
	/* check conjunction */
	ref<Expr>	test_conj;
	bool		bt;

	test_conj = e;
	foreach (it2, exprs->begin(), exprs->end()) {
		test_conj = AndExpr::create(*it2, test_conj);
	}

	if (exe->getSolver()->solver->mayBeTrue(
		Query(test_conj), mbt) == false)
	{
		return false;
	}

	if (mbt == false) return false;
#endif
	exprs->push_back(e);

	return true;
}

class ReplaceArrays : public ExprVisitor
{
public:
	typedef std::map<std::string, const ref<Array> > arrmap_ty;

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
			ReadExpr::create(UpdateList(repl_arr, NULL), idx));
	}

private:
	arrmap_ty&	arrmap;
	bool		goodExpr;
};

void ConstraintSeedCore::addSeedConstraints(
	ExecutionState& state, const ref<Array> arr)
{
	name2exprs_ty::const_iterator	it;
	exprlist_ty			*exprs;
	ref<Expr>			e_disjunction(NULL);

	it = name2exprs.find(arr->name);
	if (it == name2exprs.end())
		return;

	ReplaceArrays::arrmap_ty	arrmap;
	ReplaceArrays			ra(arrmap);

	foreach (it2, state.symbolicsBegin(), state.symbolicsEnd()) {
		const ref<Array>	arr(it2->getArrayRef());
		arrmap.insert(std::make_pair(arr->name, arr));
	}

	exprs = it->second;
	std::cerr << "===================\n";
	foreach (it2, exprs->begin(), exprs->end()) {
		ref<Expr>	seed_constr(*it2);
		ref<Expr>	state_constr;

		state_constr = ra.apply(seed_constr);
		if (state_constr.isNull()) {
			std::cerr << "Could not unify the seed constraint!\n";
			continue;
		}

		if (e_disjunction.isNull())
			e_disjunction = state_constr;
		else
			e_disjunction = OrExpr::create(
				state_constr,
				e_disjunction);

#if 0
		std::vector<ref<Array> >	arrs;
		std::set<std::string>		arrnames;
		ExprUtil::findSymbolicObjectsRef(state_constr, arrs);
		foreach (it3, arrs.begin(), arrs.end()) {
			assert (arrnames.count((*it3)->name) == 0);
			arrnames.insert((*it3)->name);
		}
#endif
	//	if (exe->addConstraint(state, state_constr) == false)
	//		std::cerr << "Could not add state constraint!\n";
	}

	if (e_disjunction.isNull()) {
		std::cerr << "NULL DISJUNC??\n";
		return;
	}

	if (exe->addConstraint(state, e_disjunction) == false)
		std::cerr << "Could not add seed disjunction!\n";
	std::cerr << ">>>>>>>>>>>>LOLLLLLLL<<<<<<<<" << arr->name << '\n';;
	std::cerr << "ADDED CONSTRS: " << e_disjunction << '\n';
	std::cerr << "===================\n";
}

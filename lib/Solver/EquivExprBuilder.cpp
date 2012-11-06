#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "klee/Internal/ADT/LimitedStream.h"
#include "klee/Constraints.h"
#include "klee/util/ExprUtil.h"
#include "klee/util/Assignment.h"
#include "llvm/Support/CommandLine.h"
#include "SMTPrinter.h"
#include "../Expr/SMTParser.h"
#include "../Expr/ExprRule.h"
#include "../Expr/RuleBuilder.h"
#include "static/Sugar.h"
#include "../Expr/TopLevelBuilder.h"
#include "../Expr/AssignHash.h"
#include "EquivExprBuilder.h"

#define MAX_EXPRFILE_BYTES	(1024*128)

using namespace klee;
using namespace llvm;

namespace {
	cl::opt<bool>
	ReplaceEquivExprs(
		"replace-equiv",
		cl::init(true),
		cl::desc("Replace large exprs with smaller exprs"));

	cl::opt<std::string>
	ProofsDir(
		"proofs-dir",
		cl::desc("Directory to write equivalence proofs / rules."),
		cl::init("proofs"));

	cl::opt<bool>
	WriteEquivProofs(
		"write-equiv-proofs",
		cl::desc("Dump equivalence proofs to proofs directory."));

	cl::opt<bool>
	WriteEquivRules(
		"write-equiv-rules",
		cl::desc("Dump equivalence rules to proofs directory."));

	cl::opt<std::string>
	EquivRuleFile(
		"equiv-rule-file",
		cl::init(""),
		cl::desc("File for writing equivdb rules."));

	cl::opt<bool>
	QueueSolverEquiv(
		"queue-solver-equiv",
		cl::init(true),
		cl::desc("Check expr equiv outside of solver."));

	cl::opt<std::string>
	EquivDBDir(
		"equivdb-dir",
		cl::init("equivdb"),
		cl::desc("EquivDB directory. Defaults to ./equivdb"));

	cl::opt<bool>
	CheckRepeatRules(
		"check-repeat-rules",
		cl::desc("Check if \"discovered\" a known rule"));
}

EquivExprBuilder::EquivExprBuilder(Solver& s, ExprBuilder* in_eb)
: solver(s)
, eb(in_eb)
, ign_c(0)
, const_c(0)
, wide_c(0)
, hit_c(0)
, miss_c(0)
, failed_c(0)
, blacklist_c(0)
, equiv_rule_file(NULL)
{
	mkdir(EquivDBDir.c_str(), 0700);

	for (unsigned i = 1; i <= 64; i++)
		makeBitDir(EquivDBDir.c_str(), i);
	makeBitDir(EquivDBDir.c_str(), 128);
	makeBitDir(EquivDBDir.c_str(), 96);

	if (WriteEquivProofs || WriteEquivRules)
		mkdir(ProofsDir.c_str(), 0700);

	loadBlacklist((EquivDBDir + "/blacklist.txt").c_str());

	if (!EquivRuleFile.empty())
		equiv_rule_file = new std::ofstream(
			EquivRuleFile.c_str(),
			std::ios::out | std::ios::binary | std::ios::app);
}

EquivExprBuilder::~EquivExprBuilder(void)
{
	if (equiv_rule_file) delete equiv_rule_file;
	delete eb;
}

void EquivExprBuilder::loadBlacklist(const char* fname)
{
	std::ifstream	ifs(fname);
	uint64_t	hash;
	while (ifs >> hash) blacklist.insert(hash);
}

void EquivExprBuilder::makeBitDir(const char* base, unsigned bits)
{
	std::stringstream ss;

	ss << base << '/' << bits;
	mkdir(ss.str().c_str(), 0700);

	for (unsigned i = EE_MIN_NODES; i < EE_MAX_NODES; i++) {
		ss.str("");
		ss << base << '/' << bits << '/' << i;
		mkdir(ss.str().c_str(), 0700);
	}
}


ref<Expr> EquivExprBuilder::lookupConst(const ref<Expr>& e)
{
	const ConstantExpr	*ce;
	uint64_t		v;

	const_c++;
	ce = cast<ConstantExpr>(e);
	assert (ce != NULL);

	v = ce->getZExtValue();
	if (consts.count(v))
		return e;

	consts.insert(v);
	consts_map[AssignHash::getEvalHash(e)] = v;
	return e;
}

ref<Expr> EquivExprBuilder::lookup(ref<Expr>& e)
{
	ref<Expr>	ret;
	unsigned	nodes;

	if (e->getWidth() > 64) {
		wide_c++;
		return e;
	}

	/* constant fast-path */
	if (isa<ConstantExpr>(e))
		return lookupConst(e);

	if (ident_memo.count(e->hash()))
		return e;

	nodes = ExprUtil::getNumNodes(e, false, EE_MAX_NODES+1);
	if (nodes != 1 && (nodes < EE_MIN_NODES || nodes > EE_MAX_NODES)) {
		/* cull 'simple' expressions */
		ign_c++;
		ident_memo.insert(e->hash());
		return e;
	}

	if (lookup_memo.count(e))
		return lookup_memo.find(e)->second;

	handleQueuedExprs();
	ret = lookupByEval(e, nodes);
	if (ret->hash() == e->hash())
		ident_memo.insert(e->hash());
	else if (solver.inSolver() == false)
		lookup_memo[e] = ret;

	return ret;
}

void EquivExprBuilder::handleQueuedExprs(void)
{
	if (QueueSolverEquiv == false || solver.inSolver() == true)
		return;

	foreach (it, solver_exprs.begin(), solver_exprs.end()) {
		ref<Expr>	ne, cur_e;

		cur_e = *it;
		if (ident_memo.count(cur_e->hash()) || lookup_memo.count(cur_e))
			continue;

		ne = lookupByEval(
			cur_e,
			ExprUtil::getNumNodes(cur_e, false, EE_MAX_NODES+1));
		lookup_memo[cur_e] = ne;
	}

	solver_exprs.clear();
}

class ReadUnifier : public ExprVisitor
{
public:
	ReadUnifier(const ref<Expr>& _src_e)
	: src_e(_src_e)
	{ ExprUtil::findSymbolicObjectsRef(src_e, sym_arrays); }

	virtual ~ReadUnifier(void) {}

	virtual ref<Expr> apply(const ref<Expr>& e)
	{
		ref<Expr>	ret;
		goodExpr = true;
		ret = visit(e);
		return (goodExpr) ? ret : NULL;
	}

protected:
	virtual Action visitRead(const ReadExpr& re);

private:
	const ref<Expr>	src_e;

	std::vector<ref<Array> >	sym_arrays;
	typedef std::map<
		ref<Array> /* array to replace */,
		ref<Array> /* replacement */>	uni2src_arr_ty;
	uni2src_arr_ty		uni2src_arr;
	bool			goodExpr;
};

ReadUnifier::Action ReadUnifier::visitRead(const ReadExpr& re)
{
	ref<Array>			read_arr, repl_arr;
	const ConstantExpr		*ce_idx;
	uni2src_arr_ty::const_iterator	it;

	/* ignore expressions with updates for now */
	if (re.hasUpdates()) {
		goodExpr = false;
		return Action::skipChildren();
	}
	assert (re.hasUpdates() == false);

	/* has array already been assigned? */
	read_arr = re.getArray();
	it = uni2src_arr.find(read_arr);
	if (it != uni2src_arr.end()) {
		ce_idx = dyn_cast<ConstantExpr>(re.index);
		if (	ce_idx != NULL &&
			ce_idx->getZExtValue() >= it->second->getSize())
		{
			return Action::changeTo(MK_CONST(0, re.getWidth()));
		}

		return Action::changeTo(
			ReadExpr::create(
				UpdateList(it->second, NULL),
				re.index));
	}

	/* no arrays available for assignment? */
	if (sym_arrays.empty())
		return Action::changeTo(ConstantExpr::create(0, 8));

	repl_arr = sym_arrays.back();

	/* quick OOB sanity check-- ensure index does not exceed
	 * size of the array */
	ce_idx = dyn_cast<ConstantExpr>(re.index);
	if (ce_idx != NULL) {
		if (ce_idx->getZExtValue() >= repl_arr->getSize()) {
			return Action::changeTo(MK_CONST(0, re.getWidth()));
		}
	}

	sym_arrays.pop_back();
	uni2src_arr[read_arr] = repl_arr;

	return Action::changeTo(
		ReadExpr::create(
			UpdateList(repl_arr, NULL),
			re.index));
}

bool EquivExprBuilder::unify(
	const ref<Expr>& e_klee,
	const ref<Expr>& e_db,
	ref<Expr>& e_db_unified,
	ref<Expr>& e_klee_w)
{
	int		w_diff;

	if (isa<ConstantExpr>(e_db)) {
		/* nothing to unify for a constant */
		e_db_unified = e_db;
	} else {
		/* unify e_db into e */
		ReadUnifier	ru(e_klee);
		e_db_unified = ru.apply(e_db);
	}

	if (e_db_unified.isNull()) {
		failed_c++;
		return false;
	}

	e_klee_w = e_klee;
	w_diff = e_db_unified->getWidth() - e_klee->getWidth();
	if (w_diff < 0) {
		e_db_unified = MK_ZEXT(e_db_unified, e_klee->getWidth());
	} else if (w_diff > 0) {
		e_klee_w = MK_ZEXT(e_klee, e_db_unified->getWidth());
	}

	return true;
}

ref<Expr> EquivExprBuilder::tryEquivRewrite(
	const ref<Expr>& e_klee,
	const ref<Expr>& e_db)
{
	ref<Expr>	e_db_unified, e_klee_w;
	bool		ok, must_be_true;

	if (unify(e_klee, e_db, e_klee_w, e_db_unified) == false)
		return NULL;

	ok = solver.mustBeTrue(
		Query(MK_EQ(e_klee_w, e_db_unified)), must_be_true);
	if (ok == false) {
		failed_c++;
		return NULL;
	}

	/* not a precise match */
	if (must_be_true == false) {
		miss_c++;
		return NULL;
	}

	/* hit in the equiv db; found a better, equivalent query */
	hit_c++;

	if (WriteEquivProofs) {
		/* dump query that shows lhs != rhs => unsat */
		SMTPrinter::dump(
			Query(EqExpr::create(e_klee_w, e_db_unified)),
			(ProofsDir + "/proof").c_str());
	}

	if (WriteEquivRules)
		writeEquivRule(e_klee_w, e_db_unified);

	/* return unified query from cache */
	return MK_ZEXT(e_db_unified, e_klee->getWidth());
}

void EquivExprBuilder::writeEquivRule(
	const ref<Expr>& e_klee_w,
	const ref<Expr>& e_db_unified)
{
	ExprRule	*er;

	if (equiv_rule_file == NULL) {
		writeEquivRuleToDir(e_klee_w, e_db_unified);
		return;
	}

	er = ExprRule::createRule(e_klee_w, e_db_unified);
	if (er == NULL)
		return;

	er->printBinaryRule(*equiv_rule_file);
	equiv_rule_file->flush();
	delete er;
}

void EquivExprBuilder::writeEquivRuleToDir(
	const ref<Expr>& e_klee_w, const ref<Expr>& e_db_unified)
{
	std::stringstream	ss;
	Query	q(EqExpr::create(e_klee_w, e_db_unified));

	ss << ProofsDir << "/pending." << q.hash() << ".rule";

	std::ofstream		of(ss.str().c_str());
	ExprRule::printRule(of, e_klee_w, e_db_unified);
	of.close();

	if (	CheckRepeatRules &&
		RuleBuilder::hasRule(ss.str().c_str()))
	{
		std::cerr << "HIT#" << hit_c << '\n';
		std::cerr << "KLEE_W: " << e_klee_w << '\n';
		std::cerr << "E_DB_UNI: " << e_db_unified << '\n';
		std::cerr << "[EquivDB] Already have rule??\n";
		assert ( 0 == 1 && "REPEAT RULE!");
	}
}


void EquivExprBuilder::missedLookup(
	const ref<Expr>& e, unsigned nodes, uint64_t hash)
{
	std::stringstream	ss;
	struct stat		st;

	ss << EquivDBDir << "/" << e->getWidth() << '/' << nodes << '/' << hash;
	if (stat(ss.str().c_str(), &st) != 0) {
		/* entry not found.. this is the first one! */
		Query			q(e);
		limited_ofstream	lofs(
			ss.str().c_str(), MAX_EXPRFILE_BYTES);
		SMTPrinter::print(lofs, q, false /* no consts */);
	}

	written_hashes.insert(hash);
}

ref<Expr> EquivExprBuilder::lookupByEval(ref<Expr>& e, unsigned nodes)
{
	uint64_t		hash;
	unsigned		w;
	bool			maybeConst;

	assert (nodes > 1);
	hash = AssignHash::getEvalHash(e, maybeConst);
	w = e->getWidth();

	if (solver.inSolver()) {
		std::cerr << "[EquivExpr] Skipping replace: In solver!\n";
		if (QueueSolverEquiv)
			solver_exprs.push_back(e);
		goto miss;
	}

	if (ReplaceEquivExprs == false)
		goto miss;

	/* seen constant? */
	if (consts_map.count(hash)) {
		ref<Expr>	ce;

		ce = MK_CONST(consts_map.find(hash)->second, 64);
		ce = MK_ZEXT(ce, w);

		std::cerr << "[EquivExpr] MATCHED CONST!!! "
			<< "hits=" << hit_c
			<< ". miss=" << miss_c
			<< ". failed=" << failed_c
			<< ". blacklist=" << blacklist_c;
		std::cerr << '\n';
		if (!tryEquivRewrite(e, ce).isNull())
			return ce;

		goto miss;
	}

	/* all hashes were the same, so it could be const;
	 * may not have constant cached so could have missed it above */
	if (maybeConst) {
		Assignment		a(e);
		ref<Expr>		ce;
		ref<ConstantExpr>	r_ce;

		a.bindFreeToZero();
		ce = a.evaluate(e);
		r_ce = cast<ConstantExpr>(MK_ZEXT(ce, 64));

		/* constant never seen before, so we may have missed it */
		if (consts.count(r_ce->getZExtValue()) == 0) {
			lookupConst(r_ce);
			if (!tryEquivRewrite(e, ce).isNull())
				return ce;
		}
	}

	if (blacklist.count(hash)) {
		blacklist_c++;
		goto miss;
	}

	/* for smallest matching input-hash, if any */
	for (unsigned k = 2; k < nodes; k++) {
		std::stringstream	ss;
		struct stat		st;
		ref<Expr>		ret, sat_q;

		ss << EquivDBDir << "/" << w << '/' << k << '/' << hash;
		if (stat(ss.str().c_str(), &st) != 0)
			continue;

		sat_q = getParseByPath(ss.str());
		if (sat_q.isNull()) {
			std::cerr << "WTF. BAD READ: " << ss.str() << ".\n";
			unlink(ss.str().c_str());
			continue;
		}

		/* when written out, expressions must be cast as an equality
		 * (the SMTPrinter does ( = 0 x) )
		 * this tries to fix it up into the proper expression */
		if (isa<EqExpr>(sat_q)) {
			EqExpr		*ee = cast<EqExpr>(sat_q);
			ConstantExpr	*ce;

			ce = dyn_cast<ConstantExpr>(ee->getKid(0));
			if (ce != NULL && ce->isZero())
				sat_q = ee->getKid(1);
		}

		ret = tryEquivRewrite(e, sat_q);
		if (!ret.isNull())
			return ret;
	}

miss:
	if (written_hashes.count(hash) == 0)
		missedLookup(e, nodes, hash);

	return e;
}

ref<Expr> EquivExprBuilder::getParseByPath(const std::string& fname)
{
	ref<Expr>		ret;
	expr::SMTParser*	smtp;
	db_parse_cache_ty::iterator it;

	it = db_parse_cache.find(fname);
	if (it != db_parse_cache.end())
		return it->second;

	smtp = expr::SMTParser::Parse(fname, eb);
	if (smtp == NULL)
		return NULL;

	ret = smtp->satQuery;
	db_parse_cache[fname] = ret;
	delete smtp;

	return ret;
}

ExprBuilder* createEquivBuilder(Solver& solver, ExprBuilder* eb)
{ return EquivExprBuilder::create(solver, eb); }


ExprBuilder* EquivExprBuilder::create(Solver& s, ExprBuilder* in_eb)
{ return new TopLevelBuilder(new EquivExprBuilder(s, in_eb), in_eb, false); }

void EquivExprBuilder::printName(std::ostream& os) const
{
	os << "EquivBuilder {\n";
	eb->printName(os);
	os << "}\n";
}

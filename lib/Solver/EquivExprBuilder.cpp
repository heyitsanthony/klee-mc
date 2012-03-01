#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "klee/Constraints.h"
#include "klee/util/ExprUtil.h"
#include "klee/util/Assignment.h"
#include "llvm/Support/CommandLine.h"
#include "SMTPrinter.h"
#include "../Expr/SMTParser.h"
#include "../Expr/ExprRule.h"
#include "../Expr/RuleBuilder.h"
#include "static/Sugar.h"
#include "EquivExprBuilder.h"

using namespace klee;

using namespace llvm;

namespace {
	cl::opt<bool>
	ReplaceEquivExprs(
		"replace-equiv",
		cl::init(false),
		cl::desc("Replace large exprs with smaller exprs"));

	cl::opt<bool>
	WriteEquivProofs(
		"write-equiv-proofs",
		cl::init(false),
		cl::desc("Dump equivalence proofs to proofs directory."));

	cl::opt<bool>
	WriteEquivRules(
		"write-equiv-rules",
		cl::init(false),
		cl::desc("Dump equivalence rules to proofs directory."));


	cl::opt<bool>
	QueueSolverEquiv(
		"queue-solver-equiv",
		cl::init(false),
		cl::desc("Check expr equiv outside of solver."));

	cl::opt<std::string>
	EquivDBDir(
		"equivdb-dir",
		cl::init("equivdb"),
		cl::desc("EquivDB directory. Defaults to ./equivdb"));

	cl::opt<bool>
	CheckRepeatRules(
		"check-repeat-rules",
		cl::init(true),
		cl::desc("Check if \"discovered\" a known rule"));
}


static const uint8_t sample_seq_dat[] = {0xfe, 0, 0x7f, 1, 0x27, 0x2, ~0x27};
static const uint8_t sample_seq_onoff_dat[] = {0xff, 0x0};


EquivExprBuilder::EquivExprBuilder(Solver& s, ExprBuilder* in_eb)
: depth(0)
, solver(s)
, eb(in_eb)
, sample_seq(&sample_seq_dat[0], &sample_seq_dat[7])
, sample_seq_onoff(&sample_seq_onoff_dat[0], &sample_seq_onoff_dat[2])
, served_c(0)
, ign_c(0)
, const_c(0)
, wide_c(0)
, hit_c(0)
, miss_c(0)
, failed_c(0)
, blacklist_c(0)
{
	mkdir(EquivDBDir.c_str(), 0700);

	for (unsigned i = 1; i <= 64; i++)
		makeBitDir(EquivDBDir.c_str(), i);
	makeBitDir(EquivDBDir.c_str(), 128);
	makeBitDir(EquivDBDir.c_str(), 96);

	if (WriteEquivProofs || WriteEquivRules)
		mkdir("proofs", 0700);

	loadBlacklist((EquivDBDir + "/blacklist.txt").c_str());

	for (unsigned i = 0; i < NONSEQ_COUNT; i++) {
		for (unsigned j = 0; j < i; j++) {
			sample_nonseq_zeros[i].push_back(0xa5);
		}
		sample_nonseq_zeros[i].push_back(0);
	}

	for (unsigned i = 0; i < NONSEQ_COUNT; i++) {
		for (unsigned j = 0; j < i; j++) {
			sample_nonseq_fe[i].push_back(0);
		}
		sample_nonseq_fe[i].push_back(0xfe);
	}

}

EquivExprBuilder::~EquivExprBuilder(void) { delete eb; }

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

ref<Expr> EquivExprBuilder::lookup(ref<Expr>& e)
{
	ref<Expr>	ret;
	unsigned	nodes;

	depth--;
	if (depth != 0)
		return e;

	if (e->getWidth() > 64) {
		wide_c++;
		return e;
	}

	if (isa<ConstantExpr>(e)) {
		const ConstantExpr	*ce;
		uint64_t		v;

		const_c++;
		ce = dyn_cast<ConstantExpr>(e);
		assert (ce != NULL);

		v = ce->getZExtValue();
		if (consts.count(v))
			return e;

		consts.insert(v);
		depth++;
		consts_map[getEvalHash(e)] = v;
		depth--;
		return e;
	}

	nodes = ExprUtil::getNumNodes(e, false, EE_MAX_NODES+1);
	/* cull 'simple' expressions */
	if (nodes != 1 && (nodes < EE_MIN_NODES || nodes > EE_MAX_NODES)) {
		ign_c++;
		return e;
	}

	if (lookup_memo.count(e))
		return lookup_memo.find(e)->second;

	// forcing non-zero depth ensures expr builder won't
	// recursively call back into lookup when building
	// intermediate expressions
	depth++;
	handleQueuedExprs();
	ret = lookupByEval(e, nodes);
	if (solver.inSolver() == false)
		lookup_memo[e] = ret;
	depth--;

	return ret;
}

void EquivExprBuilder::handleQueuedExprs(void)
{
	if (QueueSolverEquiv == false || solver.inSolver() == true)
		return;

	foreach (it, solver_exprs.begin(), solver_exprs.end()) {
		ref<Expr>	ne, cur_e;

		cur_e = *it;
		if (lookup_memo.count(cur_e))
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
	{ ExprUtil::findSymbolicObjects(src_e, sym_arrays); }

	virtual ~ReadUnifier(void) {}

	ref<Expr> getUnified(const ref<Expr>& e)
	{
		ref<Expr>	ret;

		goodExpr = true;
		ret = visit(e);
		if (!goodExpr)
			return NULL;

		return ret;
	}

protected:
	virtual Action visitRead(const ReadExpr& re)
	{
		const Array			*read_arr, *repl_arr;
		const ConstantExpr		*ce_idx;
		uni2src_arr_ty::const_iterator	it;

		/* ignore expressions with updates for now */
		if (re.hasUpdates()) {
			goodExpr = false;
			std::cerr << "[EquivExpr] Ignoring updates\n";
			return Action::skipChildren();
		}
		assert (re.hasUpdates() == false);

		/* has array already been assigned? */
		read_arr = re.getArray().get();
		it = uni2src_arr.find(read_arr);
		if (it != uni2src_arr.end()) {
			ce_idx = dyn_cast<ConstantExpr>(re.index);
			if (	ce_idx != NULL &&
				ce_idx->getZExtValue() >= it->second->getSize())
			{
				return Action::changeTo(
					ConstantExpr::create(
						0, re.getWidth()));
			}

			return Action::changeTo(
				ReadExpr::create(
					UpdateList(it->second, NULL),
					re.index));
		}

		/* no arrays available for assignment? */
		if (sym_arrays.empty()) {
			std::cerr << "[EquivExpr] Ran out of syms\n";
			return Action::changeTo(ConstantExpr::create(0, 8));
		}

		repl_arr = sym_arrays.back();

		/* quick OOB sanity check */
		ce_idx = dyn_cast<ConstantExpr>(re.index);
		if (ce_idx != NULL) {
			if (ce_idx->getZExtValue() >= repl_arr->getSize()) {
				return Action::changeTo(
					ConstantExpr::create(0, re.getWidth()));
			}
		}

		sym_arrays.pop_back();
		uni2src_arr[read_arr] = repl_arr;

		return Action::changeTo(
			ReadExpr::create(
				UpdateList(repl_arr, NULL), re.index));
	}

private:
	const ref<Expr>	src_e;

	std::vector<const Array*>	sym_arrays;
	typedef std::map<
		const Array* /* array to replace */,
		const Array* /* replacement */>	uni2src_arr_ty;
	uni2src_arr_ty		uni2src_arr;
	bool			goodExpr;
};

ref<Expr> EquivExprBuilder::tryEquivRewrite(
	const ref<Expr>& e_klee,
	const ref<Expr>& e_db)
{
	ref<Expr>	e_db_unified, e_klee_w;
	bool		ok, must_be_true;
	int		w_diff;

	if (isa<ConstantExpr>(e_db)) {
		/* nothing to unify for a constant */
		e_db_unified = e_db;
	} else {
		/* unify e_db into e */
		ReadUnifier	ru(e_klee);
		e_db_unified = ru.visit(e_db);
	}

	if (e_db_unified.isNull()) {
		failed_c++;
		std::cerr << "[EquivDB] COULD NOT UNIFY " << e_db << '\n';
		return NULL;
	}

	e_klee_w = e_klee;
	w_diff = e_db_unified->getWidth() - e_klee->getWidth();
	if (w_diff < 0) {
		e_db_unified = ZExtExpr::create(e_db_unified, e_klee->getWidth());
	} else if (w_diff > 0) {
		e_klee_w = ZExtExpr::create(e_klee, e_db_unified->getWidth());
	}

	ok = solver.mustBeTrue(
		Query(EqExpr::create(e_klee_w, e_db_unified)),
		must_be_true);
	if (ok == false) {
		failed_c++;
		return NULL;
	}

	/* not a precise match */
	if (!must_be_true) {
		miss_c++;
		return NULL;
	}

	/* found a better query */
	hit_c++;

	/* equivalent-- dump query that shows lhs != rhs => unsat */
	if (WriteEquivProofs)
		SMTPrinter::dump(
			Query(EqExpr::create(e_klee_w, e_db_unified)),
			"proofs/proof");

	if (WriteEquivRules) {
		std::stringstream	ss;
		Query	q(EqExpr::create(e_klee_w, e_db_unified));

		ss << "proofs/pending." << q.hash() << ".rule";

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


	/* return unified query from cache */
	return ZExtExpr::create(e_db_unified, e_klee->getWidth());
}

ref<Expr> EquivExprBuilder::lookupByEval(ref<Expr>& e, unsigned nodes)
{
	std::stringstream	ss;
	struct stat		st;
	int			rc;
	uint64_t		hash;
	unsigned		w;

	assert (nodes > 1);
	hash = getEvalHash(e);

	w = e->getWidth();

	if (!written_hashes.count(hash)) {
		ss << EquivDBDir << "/" << w << '/' << nodes << '/' << hash;
		rc = stat(ss.str().c_str(), &st);
		if (rc != 0) {
			/* entry not found.. this is the first one! */
			Query		q(e);
			SMTPrinter::dumpToFile(
				q,
				ss.str().c_str(),
				false /* no consts */);

		}
		written_hashes.insert(hash);
	}

	if (solver.inSolver()) {
		std::cerr << "[EquivExpr] Skipping replace: In solver!\n";
		if (QueueSolverEquiv)
			solver_exprs.push_back(e);
		return e;
	}

	if (!ReplaceEquivExprs)
		return e;

	/* seen constant? */
	if (consts_map.count(hash)) {
		ref<Expr>	ce;

		ce = ConstantExpr::create(consts_map.find(hash)->second, 64);
		ce = ZExtExpr::create(ce, w);

		std::cerr << "MATCHED CONST!!! "
			<< "hits=" << hit_c
			<< ". miss=" << miss_c
			<< ". failed=" << failed_c
			<< ". blacklist=" << blacklist_c;
		std::cerr << '\n';
		if (!tryEquivRewrite(e, ce).isNull())
			return ce;

		return e;
	}

	if (blacklist.count(hash)) {
		blacklist_c++;
		return e;
	}

	/* for smallest matching input-hash, if any */
	for (unsigned k = 2; k < nodes; k++) {
		ref<Expr>		ret, sat_q;


		ss.str("");
		ss << EquivDBDir << "/" << w << '/' << k << '/' << hash;
		rc = stat(ss.str().c_str(), &st);
		if (rc != 0)
			continue;

		sat_q = getParseByPath(ss.str());
		if (sat_q.isNull()) {
			std::cerr << "WTF. BAD READ: " << ss.str() << '\n';
			continue;
		}

		/* when written out, expressions must be cast as an equality
		 * (the SMTPrinter does ( = 0 x) )
		 * this tries to fix it up into the proper expression */
		if (isa<EqExpr>(sat_q)) {
			EqExpr		*ee = cast<EqExpr>(sat_q);
			ConstantExpr	*ce;

			ce = dyn_cast<ConstantExpr>(ee->getKid(0));
			if (ce != NULL && ce->isZero()) {
				sat_q = ee->getKid(1);
			}
		}

		ret = tryEquivRewrite(e, sat_q);
		if (!ret.isNull())
			return ret;
	}

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

class AssignHash
{
public:
	AssignHash(ref<Expr>& _e)
	: e(_e), a(e), hash_off(1), hash(0), maybe_const(true)
	{}

	virtual ~AssignHash() {}
	void commitAssignment()
	{
		uint64_t	cur_eval_hash;

		cur_eval_hash = a.evaluate(e)->hash();
		if (hash_off == 1)
			last_eval_hash = cur_eval_hash;

		hash ^= (cur_eval_hash + hash_off)*(hash_off);
		hash_off++;

		if (last_eval_hash != cur_eval_hash)
			maybe_const = false;

		last_eval_hash = cur_eval_hash;
		a.resetBindings();
	}

	Assignment& getAssignment() { return a; }
	uint64_t getHash(void) const { return hash; }
	bool maybeConst(void) const { return maybe_const; }
private:
	ref<Expr>	e;
	Assignment	a;
	unsigned	hash_off;
	uint64_t	hash;
	uint64_t	last_eval_hash;
	bool		maybe_const;
};

uint64_t EquivExprBuilder::getEvalHash(ref<Expr>& e, bool& maybeConst)
{
	AssignHash		ah(e);
	Assignment		&a(ah.getAssignment());

	for (unsigned k = 0; k < NONSEQ_COUNT; k++) {
		a.bindFreeToSequence(sample_nonseq_zeros[k]);
		ah.commitAssignment();
	}

	for (unsigned k = 0; k < NONSEQ_COUNT; k++) {
		a.bindFreeToSequence(sample_nonseq_fe[k]);
		ah.commitAssignment();
	}

	a.bindFreeToSequence(sample_seq);
	ah.commitAssignment();

	a.bindFreeToSequence(sample_seq_onoff);
	ah.commitAssignment();

	for (unsigned k = 0; k <= 255; k++) {
		a.bindFreeToU8((uint8_t)k);
		ah.commitAssignment();
	}

	maybeConst = ah.maybeConst();
	return ah.getHash();
}


ExprBuilder *createEquivBuilder(Solver& solver, ExprBuilder* eb)
{ return new EquivExprBuilder(solver, eb); }

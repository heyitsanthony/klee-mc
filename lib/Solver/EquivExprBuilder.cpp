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
{
	mkdir("equivdb", 0700);
	mkdir("equivdb/1", 0700);	/* constants */
	for (unsigned i = EE_MIN_NODES; i < EE_MAX_NODES; i++) {
		std::stringstream ss;
		ss << "equivdb/" << i;
		mkdir(ss.str().c_str(), 0700);
	}

	for (unsigned i = 0; i < 8; i++) {
		for (unsigned j = 0; j < i; j++) {
			sample_nonseq_zeros[i].push_back(0xa5);
		}
		sample_nonseq_zeros[i].push_back(0);
	}
}

EquivExprBuilder::~EquivExprBuilder(void) { delete eb; }

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
		consts_map[getEvalHash(e)] = v;
		return e;
	}

	nodes = ExprUtil::getNumNodes(e, false, EE_MAX_NODES+1);
	/* cull 'simple' expressions */
	if (nodes != 1 && (nodes < EE_MIN_NODES || nodes > EE_MAX_NODES)) {
		ign_c++;
		return e;
	}

	// forcing non-zero depth ensures expr builder won't
	// recursively call back into lookup when building
	// intermediate expressions
	depth = 1;
	ret = lookupByEval(e, nodes);
	depth = 0;

	return ret;
}

class ReadUnifier : public ExprVisitor
{
public:
	ReadUnifier(const ref<Expr>& _src_e)
	: src_e(_src_e)
	{ ExprUtil::findSymbolicObjects(src_e, sym_arrays); }

	virtual ~ReadUnifier(void) {}

protected:
	virtual Action visitRead(const ReadExpr& re)
	{
		const Array			*read_arr, *repl_arr;
		const ConstantExpr		*ce_idx;
		uni2src_arr_ty::const_iterator	it;

		assert (re.hasUpdates() == false);

		read_arr = re.getArray();
		it = uni2src_arr.find(read_arr);
		if (it != uni2src_arr.end()) {
			return Action::changeTo(
				ReadExpr::create(
					UpdateList(it->second, NULL),
					re.index));
		}

		repl_arr = sym_arrays.back();
		ce_idx = dyn_cast<ConstantExpr>(re.index);
		if (ce_idx != NULL) {
			if (ce_idx->getZExtValue() >= repl_arr->mallocKey.size) {
				return Action::changeTo(
					ConstantExpr::create(0, re.getWidth()));
			}
		}

		sym_arrays.pop_back();
		uni2src_arr[read_arr] = repl_arr;

		return Action::changeTo(ReadExpr::create(
			UpdateList(repl_arr, NULL), re.index));
	}

private:
	const ref<Expr>	src_e;

	std::vector<const Array*>	sym_arrays;
	typedef std::map<
		const Array* /* array to replace */,
		const Array* /* replacement */>	uni2src_arr_ty;
	uni2src_arr_ty		uni2src_arr;
};

ref<Expr> EquivExprBuilder::tryEquivRewrite(
	const ref<Expr>& e_klee,
	const ref<Expr>& e_db)
{
	ref<Expr>	e_db_unified;
	bool		ok, must_be_true;

	std::cerr << "CHECK EQUIV[fromKLEE]:\n" << e_klee << '\n';
	std::cerr << "CHECK EQUIV[fromEDB]:\n" << e_db << '\n';

	if (isa<ConstantExpr>(e_db)) {
		/* nothing to unify for a constant */
		e_db_unified = e_db;
	} else {
		/* unify e_db into e */
		ReadUnifier	ru(e_klee);
		e_db_unified = ru.visit(e_db);
	}

	std::cerr << "CHECK EQUIV[unified]:\n";
	std::cerr << e_db_unified << '\n';

	ok = solver.mustBeTrue(
		Query(EqExpr::create(e_klee, e_db_unified)),
		must_be_true);
	if (ok == false) {
		std::cerr << "EQUIVFAILED\n";
		return NULL;
	}

	if (must_be_true) std::cerr << ">>EQUIV\n"; else std::cerr << "NOTEQUIV<<\n";

	if (must_be_true)
		return e_db_unified;

	return NULL;
}

ref<Expr> EquivExprBuilder::lookupByEval(ref<Expr>& e, unsigned nodes)
{
	std::stringstream	ss;
	struct stat		st;
	int			rc;
	uint64_t		hash;

	assert (nodes > 1);
	hash = getEvalHash(e);

	ss << "equivdb/" << nodes << '/' << hash;
	rc = stat(ss.str().c_str(), &st);
	if (rc != 0) {
		/* directory not found.. this is the first one! */
		Query	q(e);
		SMTPrinter::dumpToFile(
			q,
			ss.str().c_str(),
			false /* no consts */);
	}

	if (!ReplaceEquivExprs)
		return e;

	if (solver.inSolver()) {
		std::cerr << "Skipping replace: In solver!\n";
		return e;
	}

	/* seen constant? */
	if (consts_map.count(hash)) {
		unsigned	e_w = e->getWidth();
		ref<Expr>	ce;

		ce = ConstantExpr::create(
				consts_map.find(hash)->second,
				64);
		ce = ZExtExpr::create(ce, e_w);

		std::cerr << "MATCHED CONST!!!! DO SOMETHING...\n";
		if (!tryEquivRewrite(e, ce).isNull())
			return ce;

		return e;
	}

	/* for smallest matching input-hash, if any */
	for (unsigned k = 2; k < nodes; k++) {
		ref<Expr>		ret;
		expr::SMTParser*	smtp;

		ss.str("");
		ss << "equivdb/" << k << '/' << hash;
		rc = stat(ss.str().c_str(), &st);
		if (rc != 0)
			continue;

		smtp = expr::SMTParser::Parse(ss.str(), eb);

		if (smtp == NULL) {
			std::cerr << "WTF. BAD READ: " << ss.str() << '\n';
			continue;
		}

		ret = tryEquivRewrite(e, smtp->satQuery);
		delete smtp;

		if (!ret.isNull())
			return ret;
	}

	return e;
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
		if (hash_off == 2)
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

	for (unsigned k = 0; k < 8; k++) {
		a.bindFreeToSequence(sample_nonseq_zeros[k]);
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

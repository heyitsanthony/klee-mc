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
#include "../SMT/SMTParser.h"
#include "static/Sugar.h"
#include "EquivExprBuilder.h"

using namespace klee;


static const uint8_t sample_seq_dat[] = {0xfe, 0, 0x7f, 1, 0x27, 0x2, ~0x27};
static const uint8_t sample_seq_onoff_dat[] = {0xff, 0x0};


EquivExprBuilder::EquivExprBuilder(Solver& s, ExprBuilder* in_eb)
: solver(s)
, eb(in_eb)
, sample_seq(&sample_seq_dat[0], &sample_seq_dat[7])
, sample_seq_onoff(&sample_seq_onoff_dat[0], &sample_seq_onoff_dat[2])
, depth(0)
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

	ret = lookupByEval(e, nodes);
	return ret;
}

ref<Expr> EquivExprBuilder::lookupByEval(ref<Expr>& e, unsigned nodes)
{
	std::stringstream	ss;
	struct stat		s;
	int			rc;
	uint64_t		hash;

	assert (nodes > 1);
	hash = getEvalHash(e);

	ss << "equivdb/" << nodes << '/' << hash;
	rc = stat(ss.str().c_str(), &s);
	if (rc != 0) {
		/* directory not found.. this is the first one! */
		Query	q(e);
		SMTPrinter::dumpToFile(q, ss.str().c_str());
		return e;
	}

	return e;
}

uint64_t EquivExprBuilder::getEvalHash(ref<Expr>& e)
{
	uint64_t		hash = 0;
	const ConstantExpr	*ce;
	Assignment		a(e);

	a.bindFreeToSequence(sample_seq);
	hash ^= (a.evaluate(e)->hash()+1);
	a.resetBindings();

	a.bindFreeToSequence(sample_seq_onoff);
	hash ^= (a.evaluate(e)->hash()+1)*2;
	a.resetBindings();

	for (unsigned k = 0; k <= 255; k++) {
		a.bindFreeToU8((uint8_t)k);
		hash ^= (a.evaluate(e)->hash()+1)*(k+3);
		a.resetBindings();
	}

	return hash;
}


ExprBuilder *createEquivBuilder(Solver& solver, ExprBuilder* eb)
{ return new EquivExprBuilder(solver, eb); }

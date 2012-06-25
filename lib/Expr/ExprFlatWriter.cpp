#include <iostream>
#include "static/Sugar.h"
#include "klee/util/ExprUtil.h"


#include "ExprFlatWriter.h"

using namespace klee;

exprtags_ty EFWTagged::dummy;

void ExprFlatWriter::setLabelMap(const labelmap_ty& lm)
{
	labels.clear();
	foreach (it, lm.begin(), lm.end()) {
		ref<Expr>	e(it->second);
		unsigned	v(it->first);
		labels.insert(std::make_pair(e,v));
	}
}

void ExprFlatWriter::getLabelMap(labelmap_ty& lm) const
{
	lm.clear();
	foreach (it, labels.begin(), labels.end()) {
		ref<Expr>	e(it->first);
		unsigned	v(it->second);
		lm.insert(std::make_pair(v,e));
	}
}

ExprFlatWriter::Action ExprFlatWriter::visitExpr(const Expr* e)
{
	if (e->getKind() == Expr::Read) {
		ref<Expr>	re(const_cast<Expr*>(e));
		unsigned	lid;

		if (!labels.count(re)) {
			unsigned next_lid = labels.size();
			labels[re] = next_lid;
			lid = next_lid;
		} else
			lid = labels.find(re)->second;

		if (os) 
			*os << " l" << lid << ' ';

		label_list.push_back(lid);
		return Skip;
	}

	if (e->getKind() == Expr::NotOptimized) {
		if (os) *os << " v" << e->getWidth() << ' ';
		return Skip;
	}

	if (os) *os << ' ' << e->getKind() << ' ';

	switch (e->getKind()) {
	case Expr::Constant: {
		unsigned 		bits, off, w;
		const ConstantExpr	*ce;

		bits = e->getWidth();
		if (os) *os << bits << ' ';
		ce = static_cast<const ConstantExpr*>(e);
		if (bits <= 64) {
			if (os) *os << ce->getZExtValue();
			break;
		}

		off = 0;
		while (bits) {
			w = (bits > 64) ? 64 : bits;
			/* NB: bits - w => hi bits listed first, lo bits last */
			ce = dyn_cast<ConstantExpr>(
				ExtractExpr::create(
					const_cast<Expr*>(e), bits - w, w));
			assert (ce);
			if (os) *os << ce->getZExtValue() << ' ';
			bits -= w;
			off += w;
		}
	}
	case Expr::Select: break;
	case Expr::Concat: break;
	case Expr::Extract:
		if (!os) break;
		*os	<< Expr::Constant << ' ' << 32 << ' '
			<< static_cast<const ExtractExpr*>(e)->offset
			<< ' '
			<< Expr::Constant << ' ' << 32 << ' '
			<< e->getWidth();
		break;

	case Expr::SExt:
	case Expr::ZExt:
		if (!os) break;
		*os << Expr::Constant << ' ' << 32 << ' ' << e->getWidth();
		break;

	case Expr::Add:
	case Expr::Sub:
	case Expr::Mul:
	case Expr::UDiv:
	case Expr::SDiv:
	case Expr::URem:
	case Expr::SRem:
	case Expr::Not:
	case Expr::And:
	case Expr::Or:
	case Expr::Xor:
	case Expr::Shl:
	case Expr::LShr:
	case Expr::AShr:
	case Expr::Eq:
	case Expr::Ne:
	case Expr::Ult:
	case Expr::Ule:
	case Expr::Ugt:
	case Expr::Uge:
	case Expr::Slt:
	case Expr::Sle:
	case Expr::Sgt:
	case Expr::Sge:
		break;
	default:
		std::cerr << "WTF??? " << e << '\n';
		assert (0 == 1);
		break;
	}

	return Expand;
}

ExprFlatWriter::Action EFWTagged::preTagVisit(const Expr* e)
{
	if (const_repl) {
		/* constant label */
		(*os) << " c" << tag_c++
			<< ' ' << e->getWidth()
			<< ' ' << *e << ' ';
	} else
		visitVar(e);
	return Close;
}

void EFWTagged::apply(const ref<Expr>& e)
{
	tag_c = 0;
	masked_list_base = 0;
	masked_start_lid = 0;
	masked_new = 0;
	masked_reads = 0;
	label_list.clear();
	ExprVisitorTags<ExprFlatWriter>::apply(e);
}

void EFWTagged::visitVar(const Expr* _e)
{
	std::vector< ref<ReadExpr> >	reads;
	ref<Expr>			e(const_cast<Expr*>(_e));
	std::set<ref<ReadExpr> >	rs;

	/* sink */
	(*os) << " v" << e->getWidth() << ' ';
	/* if we start masking, it's here. */
	masked_list_base = label_list.size();

	ExprUtil::findReads(e, false, reads);
	foreach (it, reads.begin(), reads.end())
		rs.insert(*it);

	/* count number of 'new' labels masked' */
	masked_new = 0;
	foreach (it, rs.begin(), rs.end())
		if (!labels.count(*it))
			masked_new++;

	masked_reads = reads.size();
}

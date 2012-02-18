#include <iostream>
#include "static/Sugar.h"
#include "klee/util/ExprVisitor.h"
#include "ExprRule.h"

using namespace klee;

ref<Expr> ExprRule::flat2expr(
	const labelmap_ty&	lm,
	const flatrule_ty&	fr,
	int&			off) const
{
	int		op_idx;
	ref<Expr>	ret;

	op_idx = off++;
	if (OP_LABEL_TEST(fr[op_idx])) {
		unsigned			label_num;
		labelmap_ty::const_iterator	it;

		label_num = OP_LABEL_NUM(fr[op_idx]);
		it = lm.find(label_num);
		if (it == lm.end())
			return NULL;

		return it->second;
	}

	switch (fr[op_idx]) {
	case Expr::Constant:
		ret = ConstantExpr::create(fr[off+1], fr[off]);
		off += 2;
		break;
	case Expr::Select: {
		ref<Expr>	c, t, f;
		c = flat2expr(lm, fr, off);
		t = flat2expr(lm, fr, off);
		f = flat2expr(lm, fr, off);
		ret = SelectExpr::create(c, t, f);
		break;
	}

	case Expr::Extract: {
		ref<Expr>		e_off, e_w, kid;
		const ConstantExpr	*ce_off, *ce_w;

		e_off = flat2expr(lm, fr, off);
		e_w = flat2expr(lm, fr, off);

		ce_off = dyn_cast<ConstantExpr>(e_off);
		ce_w = dyn_cast<ConstantExpr>(e_w);

		kid = flat2expr(lm, fr, off);

		ret = ExtractExpr::create(
			kid,
			ce_off->getZExtValue(),
			ce_w->getZExtValue());
		break;
	}

	case Expr::SExt:
	case Expr::ZExt:
	{
		ref<Expr>		w, kid;
		const ConstantExpr	*ce_w;

		w = flat2expr(lm, fr, off);
		ce_w = dyn_cast<ConstantExpr>(w);
		kid = flat2expr(lm, fr, off);
		if (fr[op_idx] == Expr::SExt) {
			ret = SExtExpr::create(kid, ce_w->getZExtValue());
		} else {
			ret = ZExtExpr::create(kid, ce_w->getZExtValue());
		}
		break;
	}

	case Expr::Not: {
		ref<Expr> e = flat2expr(lm, fr, off);
		ret = NotExpr::create(e);
		break;
	}

#define FLAT_BINOP(x)				\
	case Expr::x: {				\
		ref<Expr>	lhs, rhs;	\
		lhs = flat2expr(lm, fr, off);	\
		rhs = flat2expr(lm, fr, off);	\
		ret = x##Expr::create(lhs, rhs);\
		break;				\
	}

	FLAT_BINOP(Concat)
	FLAT_BINOP(Add)
	FLAT_BINOP(Sub)
	FLAT_BINOP(Mul)
	FLAT_BINOP(UDiv)
	FLAT_BINOP(SDiv)
	FLAT_BINOP(URem)
	FLAT_BINOP(SRem)
	FLAT_BINOP(And)
	FLAT_BINOP(Or)
	FLAT_BINOP(Xor)
	FLAT_BINOP(Shl)
	FLAT_BINOP(LShr)
	FLAT_BINOP(AShr)
	FLAT_BINOP(Eq)
	FLAT_BINOP(Ne)
	FLAT_BINOP(Ult)
	FLAT_BINOP(Ule)
	FLAT_BINOP(Ugt)
	FLAT_BINOP(Uge)
	FLAT_BINOP(Slt)
	FLAT_BINOP(Sle)
	FLAT_BINOP(Sgt)
	FLAT_BINOP(Sge)
	default:
		std::cerr << "WTF??? op=" << fr[op_idx] << '\n';
		assert (0 == 1);
		break;
	}

	return ret;
}

ExprRule::ExprRule(void) {}

void ExprRule::printBinaryRule(std::ostream& os) const
{
	uint32_t	from_sz, to_sz;

	from_sz = from.size();
	to_sz = to.size();
	os.write((char*)&from_sz, 4);
	os.write((char*)&to_sz, 4);
	os.write((char*)from.data(), from_sz*8);
	os.write((char*)to.data(), to_sz*8);
}

unsigned ExprRule::estNumLabels(const flatrule_ty& fr) const
{
	std::set<unsigned>	labels;

	foreach (it, fr.begin(), fr.end()) {
		if (!OP_LABEL_TEST((*it)))
			continue;

		labels.insert(*it);
	}

	return labels.size();
}

ref<Expr> ExprRule::anonFlat2Expr(
	const Array* arr, const flatrule_ty& fr) const
{
	int		off;
	labelmap_ty	lm;
	unsigned	est_labels;

	est_labels = estNumLabels(fr);
	for (unsigned i = 0; i < est_labels; i++)
		lm[i] = ReadExpr::create(
			UpdateList(arr, NULL), ConstantExpr::create(i, 32));

	off = 0;
	return flat2expr(lm, fr, off);
}

ref<Expr> ExprRule::materialize(void) const
{
	ref<Array>	arr = Array::create("exprrule", 4096);
	ref<Expr>	lhs, rhs;

	lhs = anonFlat2Expr(arr.get(), from);
	rhs = anonFlat2Expr(arr.get(), to);

	return EqExpr::create(lhs, rhs);
}

bool ExprRule::readFlatExpr(std::ifstream& ifs, flatrule_ty& r)
{
	std::string	tok;

	while (ifs >> tok) {
		if (tok[0] == 'l') {
			uint64_t	label_num = atoi(tok.c_str() + 1);
			r.push_back(OP_LABEL_MK(label_num));
			continue;
		}

#define READ_TOK(x)	if (tok == #x) r.push_back((int)Expr::x)

		READ_TOK(Constant);
		else READ_TOK(Select);
		else READ_TOK(Concat);
		else READ_TOK(Extract);
		else READ_TOK(SExt);
		else READ_TOK(ZExt);
		else READ_TOK(Add);
		else READ_TOK(Sub);
		else READ_TOK(Mul);
		else READ_TOK(UDiv);
		else READ_TOK(SDiv);
		else READ_TOK(URem);
		else READ_TOK(SRem);
		else READ_TOK(Not);
		else READ_TOK(And);
		else READ_TOK(Or);
		else READ_TOK(Xor);
		else READ_TOK(Shl);
		else READ_TOK(LShr);
		else READ_TOK(AShr);
		else READ_TOK(Eq);
		else READ_TOK(Ne);
		else READ_TOK(Ult);
		else READ_TOK(Ule);
		else READ_TOK(Ugt);
		else READ_TOK(Uge);
		else READ_TOK(Slt);
		else READ_TOK(Sle);
		else READ_TOK(Sgt);
		else READ_TOK(Sge);
		else if (tok == "->")
			return true;
		else {
			for (unsigned i = 0; i < tok.size(); i++)
				if (tok[i] < '0' || tok[i] > '9')
					return false;
			r.push_back(atoi(tok.c_str()));
		}
	}

	if (ifs.eof())
		return true;

	return false;
}

ExprRule* ExprRule::loadBinaryRule(const char* fname)
{
	std::ifstream ifs(fname);
	return loadBinaryRule(ifs);
}

ExprRule* ExprRule::loadBinaryRule(std::istream& is)
{
	ExprRule	*ret;
	uint32_t	from_sz, to_sz;
	flatrule_ty	from, to;

	is.read((char*)&from_sz, 4);
	is.read((char*)&to_sz, 4);
	if (from_sz > 10000 || to_sz > 10000)
		return NULL;

	from.resize(from_sz);
	to.resize(to_sz);
	is.read((char*)from.data(), from_sz*8);
	is.read((char*)to.data(), to_sz*8);

	if (is.fail())
		return NULL;

	ret = new ExprRule();
	ret->from = from;
	ret->to = to;

	return ret;
}

ExprRule* ExprRule::loadPrettyRule(const char* fname)
{
	ExprRule	*ret;
	flatrule_ty	from, to;
	std::ifstream	ifs(fname);

	if (!readFlatExpr(ifs, from)) return NULL;
	if (!readFlatExpr(ifs, to)) return NULL;

	if (from.size() == 0 || to.size() == 0)
		return NULL;

	ret = new ExprRule();
	ret->from = (from.size() > to.size()) ? from : to;
	ret->to = (from.size() > to.size()) ? to : from;

	return ret;
}

class ExprFlatWriter : public ExprConstVisitor
{
public:
	ExprFlatWriter(std::ostream& _os) : os(_os) {}
	virtual ~ExprFlatWriter(void) {}

	void print(const ref<Expr>& e)
	{
		labels.clear();
		visit(e);
	}

protected:
	virtual Action visitExpr(const Expr* expr);

private:
	ExprHashMap<unsigned>		labels;
	std::ostream			&os;
};

ExprFlatWriter::Action ExprFlatWriter::visitExpr(const Expr* expr)
{
	if (expr->getKind() == Expr::Read) {
		ref<Expr>	re(const_cast<Expr*>(expr));
		if (!labels.count(re))
			labels[re] = labels.size();
		os << " l" << labels.find(re)->second << ' ';
		return Skip;
	}

	os << ' ' << expr->getKind() << ' ';
	switch (expr->getKind()) {
	case Expr::Constant:
		os << expr->getWidth() << ' ' <<
		static_cast<const ConstantExpr*>(expr)->getZExtValue();
		break;
	case Expr::Select: break;
	case Expr::Concat: break;
	case Expr::Extract:
		os	<< Expr::Constant << ' ' << 32 << ' '
			<< static_cast<const ExtractExpr*>(expr)->offset
			<< ' '
			<< Expr::Constant << ' ' << 32 << ' '
			<< expr->getWidth();
		break;

	case Expr::SExt:
	case Expr::ZExt:
		os << Expr::Constant << ' ' << 32 << ' ' << expr->getWidth();
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
		std::cerr << "WTF??? " << expr << '\n';
		assert (0 == 1);
		break;
	}

	return Expand;
}

void ExprRule::printPattern(std::ostream& os, const ref<Expr>& e)
{
	ExprFlatWriter	efw(os);
	efw.print(e);
}

class ExprFindLabels : public ExprConstVisitor
{
public:
	ExprFindLabels(
		const ExprRule::flatrule_ty& _from_rule,
		ExprRule::labelmap_ty& _lm)
	: from_rule(_from_rule)
	, lm(_lm) {}

	virtual ~ExprFindLabels() {}

	bool assignLabels(const ref<Expr>& e)
	{
		success = false;
		lm.clear();
		fr_it = from_rule.begin();
		visit(e);
		return success;
	}

protected:
	virtual Action visitExpr(const Expr* expr);
private:
	bool verifyConstant(const ConstantExpr* ce);
	bool verifyConstant(uint64_t v, unsigned w);

	const ExprRule::flatrule_ty		&from_rule;
	ExprRule::labelmap_ty			&lm;
	ExprRule::flatrule_ty::const_iterator	fr_it;
	bool					success;
};


bool ExprFindLabels::verifyConstant(const ConstantExpr* ce)
{
	if (*fr_it != ce->getWidth())
		return false;

	fr_it++;
	if (*fr_it != ce->getZExtValue())
		return false;

	fr_it++;
	return true;
}

bool ExprFindLabels::verifyConstant(uint64_t v, unsigned w)
{
	if (*fr_it != Expr::Constant)
		return Stop;
	fr_it++;

	if (*fr_it != w)
		return false;
	fr_it++;

	if (*fr_it != v)
		return false;
	fr_it++;

	return true;
}

ExprFindLabels::Action ExprFindLabels::visitExpr(const Expr* expr)
{
	if (OP_LABEL_TEST(*fr_it)) {
		ExprRule::labelmap_ty::iterator	it;
		uint64_t			label_num;

		label_num = OP_LABEL_NUM(*fr_it);
		fr_it++;
		it = lm.find(label_num);

		if (it == lm.end()) {
			/* create new label */
			lm[label_num] = ref<Expr>(const_cast<Expr*>(expr));
			return Skip;
		}

		if (*it->second != *expr) {
			/* label mismatch.. can't assign! */
			success = false;
			return Stop;
		}

		/* label was matched, continue */
		return Skip;
	}

	/* mismatch-- rule does not apply */
	if ((uint64_t)expr->getKind() != *fr_it) {
		success = false;
		return Stop;
	}

	fr_it++;
	switch (expr->getKind()) {
	case Expr::Constant:
		if (!verifyConstant(static_cast<const ConstantExpr*>(expr)))
			return Stop;
		break;
	case Expr::Select: break;
	case Expr::Concat: break;
	case Expr::Extract: {
		const ExtractExpr	*ex;

		ex = static_cast<const ExtractExpr*>(expr);
		if (!verifyConstant(ex->offset, 32))
			return Stop;

		if (!verifyConstant(ex->getWidth(), 32))
			return Stop;
		break;
	}

	case Expr::SExt:
	case Expr::ZExt:
		if (!verifyConstant(expr->getWidth(), 32))
			return Stop;
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
		std::cerr << "[ExprRule] Unknown op. Bailing\n";
		return Stop;
	}

	/* rule ran out of steam. we're done?? */
	if (fr_it == from_rule.end()) {
		success = true;
		return Stop;
	}

	return Expand;
}

ref<Expr> ExprRule::apply(const ref<Expr>& e) const
{
	labelmap_ty	labels;
	int		off;
	ExprFindLabels	efl(from, labels);

	if (efl.assignLabels(e) == false)
		return NULL;

	return flat2expr(labels, to, off);
}

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>
#include "static/Sugar.h"
#include "klee/util/ExprVisitor.h"
#include "klee/util/ExprUtil.h"
#include "ExprRule.h"

using namespace klee;

ExprRule::ExprRule(const Pattern& _from, const Pattern& _to)
: from(_from)
, to(_to)
, materialize_arr(0)
{
	unsigned max_id;

//	max_id = (from.label_c > to.label_c) ? from.label_c : to.label_c;
	max_id = from.label_c + to.label_c;
	from.label_id_max = max_id;
	to.label_id_max = max_id;
}


ref<Expr> ExprRule::flat2expr(
	const labelmap_ty&	lm,
	const flatrule_ty&	fr,
	int&			off)
{
	int		op_idx;
	ref<Expr>	ret;

	op_idx = off++;
	if (OP_LABEL_TEST(fr[op_idx])) {
		unsigned			label_num;
		labelmap_ty::const_iterator	it;

		label_num = OP_LABEL_NUM(fr[op_idx]);
		it = lm.find(label_num);
		if (it == lm.end()) {
			std::cerr << "[ExprRule] Flat2Expr Error. No label: "
					<< label_num << "\n";
			return NULL;
		}

		return it->second;
	}

	switch (fr[op_idx]) {
	case Expr::Constant: {
		unsigned bits = fr[off++];
		if (bits > 64) {
			/* concat it in */
			ret = ConstantExpr::create(fr[off++], 64);
			bits -= 64;
			while (bits) {
				ref<Expr>	new_bits;
				unsigned	w = (bits < 64) ? bits : 64;
				new_bits = ConstantExpr::create(fr[off++], w);
				ret = ConcatExpr::create(ret, new_bits);
				bits -= w;
			}
		} else {
			ret = ConstantExpr::create(fr[off++], bits);
		}
		break;
	}
	case Expr::Select: {
		ref<Expr>	c, t, f;
		c = flat2expr(lm, fr, off);
		t = flat2expr(lm, fr, off);
		f = flat2expr(lm, fr, off);
		if (c.isNull() || t.isNull() || f.isNull()) {
			return NULL;
		}
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

		if (e_off.isNull() || e_w.isNull() || kid.isNull()) {
			return NULL;
		}

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
		if (w.isNull() || kid.isNull()) {
			return NULL;
		}

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
		if (lhs.isNull() || rhs.isNull())\
		{ return NULL;	} \
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

#define HDR_MAGIC	0xFEFEFEFE01010101

void ExprRule::printBinaryRule(std::ostream& os) const
{
	uint64_t	hdr = HDR_MAGIC;

	os.write((char*)&hdr, 8);
	printBinaryPattern(os, from);
	printBinaryPattern(os, to);
}

void ExprRule::printBinaryPattern(std::ostream& os, const Pattern& p)
{
	uint32_t	sz;
	uint32_t	l_c;

	sz = p.rule.size();
	l_c = p.label_c;
	os.write((char*)&sz, 4);
	os.write((char*)&l_c, 4);
	os.write((char*)p.rule.data(), sz*8);
}

ExprRule* ExprRule::loadBinaryRule(const char* fname)
{
	std::ifstream ifs(fname);
	return loadBinaryRule(ifs);
}

ExprRule* ExprRule::loadBinaryRule(std::istream& is)
{
	uint64_t	hdr;
	Pattern		p_from, p_to;


	is.read((char*)&hdr, 8);
	if (hdr != HDR_MAGIC)
		return NULL;

	loadBinaryPattern(is, p_from);
	loadBinaryPattern(is, p_to);

	if (is.fail())
		return NULL;

	return new ExprRule(p_from, p_to);
}

void ExprRule::loadBinaryPattern(std::istream& is, Pattern& p)
{
	uint32_t	sz;
	uint32_t	l_c;

	is.read((char*)&sz, 4);
	is.read((char*)&l_c, 4);
	p.rule.resize(sz);
	p.label_c = l_c;
	is.read((char*)p.rule.data(), sz*8);
}



ref<Expr> ExprRule::anonFlat2Expr(const Pattern& p) const
{
	const Array	*arr;
	int		off;
	labelmap_ty	lm;

	if (materialize_arr.isNull())
		materialize_arr = Array::create("exprrule", 4096);

	arr = materialize_arr.get();
	for (unsigned i = 0; i <= p.label_id_max; i++)
		lm[i] = ReadExpr::create(
			UpdateList(arr, NULL), ConstantExpr::create(i, 32));

	off = 0;
	return flat2expr(lm, p.rule, off);
}

ref<Expr> ExprRule::materialize(void) const
{

	ref<Expr>	lhs, rhs;

	lhs = getFromExpr();
	assert (lhs.isNull() == false);
	rhs = getToExpr();
	assert (rhs.isNull() == false);

	return EqExpr::create(lhs, rhs);
}

bool ExprRule::readFlatExpr(std::ifstream& ifs, Pattern& p)
{
	std::string		tok;
	std::set<uint64_t>	labels;

	while (ifs >> tok) {
		if (tok[0] == 'l') {
			uint64_t	label_num = atoi(tok.c_str() + 1);
			p.rule.push_back(OP_LABEL_MK(label_num));
			labels.insert(label_num);
			continue;
		}

#define READ_TOK(x)	if (tok == #x) p.rule.push_back((int)Expr::x)

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
			goto success;
		else {
			uint64_t	v = 0;
			for (unsigned i = 0; i < tok.size(); i++) {
				if (tok[i] < '0' || tok[i] > '9')
					return false;
				v *= 10;
				v += tok[i] - '0';
			}
			p.rule.push_back(v);
		}
	}

	if (ifs.eof())
		goto success;

	return false;
success:
	p.label_c = labels.size();
	return true;
}

ExprRule* ExprRule::loadPrettyRule(const char* fname)
{
	Pattern		p_from, p_to;

	struct stat	s;

	if (stat(fname, &s) != 0)
		return NULL;

	if (!S_ISREG(s.st_mode))
		return NULL;

	std::ifstream	ifs(fname);
	if (!readFlatExpr(ifs, p_from)) return NULL;
	if (!readFlatExpr(ifs, p_to)) return NULL;

	if (	p_from.rule.size() == 0 || p_to.rule.size() == 0 ||
		(p_from.rule.size() == 1 && p_to.rule.size() == 1))
	{
		return NULL;
	}

	return new ExprRule(
		((p_from.rule.size() > p_to.rule.size()) ? p_from : p_to),
		((p_from.rule.size() > p_to.rule.size()) ? p_to : p_from));
}

class ExprFlatWriter : public ExprConstVisitor
{
public:
	ExprFlatWriter(std::ostream& _os) : os(_os) {}
	virtual ~ExprFlatWriter(void) {}

	void print(const ref<Expr>& e) { visit(e); }

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
		if (!labels.count(re)) {
			unsigned next_lid = labels.size();
			labels[re] = next_lid;
		}
		os << " l" << labels.find(re)->second << ' ';
		return Skip;
	}

	os << ' ' << expr->getKind() << ' ';
	switch (expr->getKind()) {
	case Expr::Constant: {
		unsigned 		bits, off, w;
		const ConstantExpr	*ce;

		bits = expr->getWidth();
		os << bits << ' ';
		ce = static_cast<const ConstantExpr*>(expr);
		if (bits <= 64) {
			os << ce->getZExtValue();
			break;
		}

		off = 0;
		while (bits) {
			w = (bits > 64) ? 64 : bits;
			/* NB: bits - w => hi bits listed first, lo bits last */
			ce = dyn_cast<ConstantExpr>(
				ExtractExpr::create(
					const_cast<Expr*>(expr), bits - w, w));
			assert (ce);
			os << ce->getZExtValue() << ' ';
			bits -= w;
			off += w;
		}
	}
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

void ExprRule::printExpr(std::ostream& os, const ref<Expr>& e)
{
	ExprFlatWriter	efw(os);
	efw.print(e);
}

void ExprRule::printRule(
	std::ostream& os, const ref<Expr>& lhs, const ref<Expr>& rhs)
{
	ExprFlatWriter	efw(os);
	unsigned	lhs_nodes, rhs_nodes;

	lhs_nodes = ExprUtil::getNumNodes(lhs);
	rhs_nodes = ExprUtil::getNumNodes(rhs);

	efw.print((lhs_nodes > rhs_nodes) ? lhs : rhs);
	os << "\n->\n";
	efw.print((lhs_nodes > rhs_nodes) ? rhs : lhs);
	os << "\n\n";
}

class ExprFindLabels : public ExprConstVisitor
{
public:
	ExprFindLabels(
		ExprRule::RuleIterator	&_rule_it,
		ExprRule::labelmap_ty	&_lm)
	: rule_it(_rule_it)
	, lm(_lm) {}

	virtual ~ExprFindLabels() {}

	bool assignLabels(const ref<Expr>& e)
	{
		success = false;
		lm.clear();
		rule_it.reset();
		visit(e);
		return success;
	}

protected:
	virtual Action visitExpr(const Expr* expr);
private:
	bool verifyConstant(const ConstantExpr* ce);
	bool verifyConstant(uint64_t v, unsigned w);


	ExprRule::RuleIterator			&rule_it;
	ExprRule::labelmap_ty			&lm;
	bool					success;
};

bool ExprFindLabels::verifyConstant(const ConstantExpr* ce)
{
	unsigned bits;

	bits = ce->getWidth();


	if (rule_it.matchValue(bits) == false)
		return false;

	if (bits <= 64) {
		if (rule_it.matchValue(ce->getZExtValue()) == false)
			return false;
		return true;
	}

	while (bits) {
		unsigned		w;
		const ConstantExpr	*cur_ce;

		w = (bits > 64) ? 64 : bits;
		cur_ce = dyn_cast<ConstantExpr>(
			ExtractExpr::create(
				const_cast<ConstantExpr*>(ce),
				bits - w,
				w));
		if (cur_ce == NULL) {
			std::cerr << "WTF EXPECTED CE FROM EXT(CE)\n";
			return false;
		}

		if (rule_it.matchValue(cur_ce->getZExtValue()) == false)
			return false;

		bits -= w;
	}

	return true;
}

bool ExprFindLabels::verifyConstant(uint64_t v, unsigned w)
{
	if (rule_it.matchValue(Expr::Constant) == false)
		return Stop;

	if (rule_it.matchValue(w) == false)
		return false;

	if (rule_it.matchValue(v) == false)
		return false;

	return true;
}

ExprFindLabels::Action ExprFindLabels::visitExpr(const Expr* expr)
{
	uint64_t	label_op;

	/* success => stop */
	assert (success == false);

	/* SUPER IMPORTANT: labels are always 8-bits until otherwise
	 * noted. Permitting other sizes tends to wreck expected sizes. */
	if (expr->getWidth() == 8 && rule_it.matchLabel(label_op)) {
		ExprRule::labelmap_ty::iterator	it;
		uint64_t			label_num;

		label_num = OP_LABEL_NUM(label_op);

		it = lm.find(label_num);
		if (it == lm.end()) {
			/* create new label */
			lm[label_num] = ref<Expr>(const_cast<Expr*>(expr));
			goto skip_label;
		}

		if (*it->second != *expr) {
			/* label mismatch.. can't assign! */
			success = false;
			return Stop;
		}

skip_label:
		/* label was matched, continue */
		if (rule_it.isDone()) {
			success = true;
			return Stop;
		}

		return Skip;
	}

	/* match the expression node's opcode */
	if (rule_it.matchValue(expr->getKind()) == false) {
		success = false;
		return Stop;
	}

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
	if (rule_it.isDone()) {
		success = true;
		return Stop;
	}

	return Expand;
}

class SingleRuleIterator : public ExprRule::RuleIterator
{
public:
	bool isDone(void) const { return (it == from_rule.end()); }
	bool matchValue(uint64_t v)
	{
		if (isDone()) return false;

		if (*it != v) {
			it++;
			return true;
		}

		return false;
	}

	void reset(void) { it = from_rule.begin(); }

	bool matchLabel(uint64_t& v)
	{
		if (isDone()) return false;

		v = *it;
		if (OP_LABEL_TEST(v)) {
			it++;
			return true;
		}

		return false;
	}

	const ExprRule::flatrule_ty& getToRule(void) const
	{
		assert (isDone() && "Grabbing to-rule but didn't verify!");
		return to_rule;
	}

	SingleRuleIterator(
		const ExprRule::flatrule_ty& _from_rule,
		const ExprRule::flatrule_ty& _to_rule)
	: from_rule(_from_rule)
	, to_rule(_to_rule)
	, it(from_rule.begin()) {}

	virtual ~SingleRuleIterator() {}

private:
	const ExprRule::flatrule_ty&		from_rule;
	const ExprRule::flatrule_ty&		to_rule;
	ExprRule::flatrule_ty::const_iterator	it;
};

ref<Expr> ExprRule::apply(const ref<Expr>& e, ExprRule::RuleIterator& ri)
{
	labelmap_ty		labels;
	int			off;
	ref<Expr>		ret;
	ExprFindLabels		efl(ri, labels);

	if (efl.assignLabels(e) == false)
		return NULL;

	off = 0;
	ret = flat2expr(labels, ri.getToRule(), off);

	return ret;
}


ref<Expr> ExprRule::apply(const ref<Expr>& e) const
{
	SingleRuleIterator	sri(from.rule, to.rule);
	return apply(e, sri);
}

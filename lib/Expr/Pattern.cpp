#include <iostream>
#include "static/Sugar.h"
#include "Pattern.h"

using namespace klee;

ref<Array> Pattern::materialize_arr = NULL;
ref<Array> Pattern::free_arr = NULL;
unsigned Pattern::free_off = 0;

/* XXX: make non-recursive? */
ref<Expr> Pattern::flat2expr(
	const labelmap_ty&	lm,
	const flatrule_ty&	fr,
	int&			off)
{
	flatrule_tok	tok;
	unsigned	op_idx;
	ref<Expr>	ret;

	op_idx = off++;
	assert (op_idx < fr.size());

	tok = fr[op_idx];
	if (OP_LABEL_TEST(tok)) {
		unsigned			label_num;
		labelmap_ty::const_iterator	it;

		label_num = OP_LABEL_NUM(tok);
		it = lm.find(label_num);
		if (it != lm.end())
			return it->second;

		std::cerr	<< "[ExprRule] Flat2Expr Error. No label: "
				<< label_num
				<< "\n";
		return NULL;
	}

	if (OP_VAR_TEST(tok)) {
		/* fake with a bogus constant */
		uint64_t	w = OP_VAR_W(tok);

		if (free_off + ((w+7)/8) >= 4096)
			free_off = 0;
		ret = NotOptimizedExpr::create(
			Expr::createTempRead(getFreeArray(), w, free_off));
		free_off = (free_off + (w+7)/8) % 4096;
		return ret;
	}

	if (OP_CLABEL_TEST(tok)) {
		/* replace label with provided example */
		unsigned	label_num;

		label_num = OP_CLABEL_NUM(tok);
		tok = Expr::Constant;
	}

	switch (tok) {
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

		if (tok == Expr::SExt) {
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
		if (Expr::x != Expr::Concat &&			\
			rhs->getWidth() != lhs->getWidth())	\
		{ return NULL; }		\
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
		std::cerr << "WTF??? op=" << tok << '\n';
		assert (0 == 1);
		break;
	}

	return ret;
}

/* reads a text expression made by the flat expr writer into a pattern */
bool Pattern::readFlatExpr(std::istream& ifs)
{
	std::string		tok;
	std::set<uint64_t>	labels, clabels;

	rule.clear();
	label_c = 0;
	clabel_c = 0;
	label_id_max = 0;

	while (ifs >> tok) {
		/* read label */
		if (tok[0] == 'l') {
			uint64_t	label_num = atoi(tok.c_str() + 1);
			rule.push_back(OP_LABEL_MK(label_num));
			labels.insert(label_num);
			continue;
		}

		/* constant label */
		if (tok[0] == 'c') {
			uint64_t	label_num = atoi(tok.c_str() + 1);
			rule.push_back(OP_CLABEL_MK(label_num));
			clabels.insert(label_num);
			continue;
		}

		/* var slot; whatever! */
		if (tok[0] == 'v') {
			uint64_t	var_size = atoi(tok.c_str() + 1);
			rule.push_back(OP_VAR_MK(var_size));
			continue;
		}

#define READ_TOK(x)	if (tok == #x) rule.push_back((int)Expr::x)

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
			rule.push_back(v);
		}
	}

	if (ifs.eof())
		goto success;

	return false;
success:
	label_c = labels.size();
	clabel_c = clabels.size();
	return true;
}

bool Pattern::operator <(const Pattern& p) const
{
	if (label_c != p.label_c)
		return (label_c < p.label_c);
	if (label_id_max != p.label_id_max)
		return (label_id_max < p.label_id_max);

	if (rule.size() != p.rule.size())
		return (rule.size() < p.rule.size());

	for (unsigned i = 0; i < rule.size(); i++)
		if (rule[i] != p.rule[i])
			return rule[i] < p.rule[i];

	/* == */
	return false;
}


bool Pattern::operator ==(const Pattern& p) const
{
	if (label_c != p.label_c) return false;
	if (label_id_max != p.label_id_max) return false;
	if (rule.size() != p.rule.size()) return false;

	for (unsigned i = 0; i < rule.size(); i++)
		if (rule[i] != p.rule[i])
			return false;

	return true;
}

void Pattern::getLabelMap(labelmap_ty& lm, unsigned l_max) const
{
	for (unsigned i = 0; i <= l_max; i++) {
		lm[i] = ReadExpr::create(
			UpdateList(getMaterializeArray(), NULL),
			ConstantExpr::create(i, 32));
	}
}


ref<Expr> Pattern::anonFlat2Expr(int label_max) const
{
	int		off;
	labelmap_ty	lm;
	unsigned	max_label;

	max_label = (label_max == -1)
		? label_id_max
		: label_max;

	getLabelMap(lm, max_label);

	off = 0;
	return flat2expr(lm, rule, off);
}

ref<Array> Pattern::getMaterializeArray(void)
{
	if (materialize_arr.isNull())
		materialize_arr = Array::create("exprrule", 4096);
	return materialize_arr;
}


ref<Array> Pattern::getFreeArray(void)
{
	if (free_arr.isNull())
		free_arr = Array::create("freearr", 4096);
	return free_arr;
}

/* so we can have a trie without counter examples making shit impossible */
/* I thought this would be a pain in the butt-- turns out it was OK! */
flatrule_ty Pattern::stripConstExamples(void) const
{
	flatrule_ty	ret;
	unsigned	i;

	if (clabel_c == 0)
		return rule;

	i = 0;
	while (i < rule.size()) {
		flatrule_tok	tok;

		tok = rule[i];

		ret.push_back(tok);
		i++;

		/* tear out constant example */
		if (OP_CLABEL_TEST(tok)) {
			uint64_t	bits;

			/* keep bit width */
			bits = rule[i];
			ret.push_back(bits);

			/* move cursor to constant data */
			i++;

			/* skip constant data */
			i += (bits + 63)/64;
			continue;
		}

		/* TRICKY: all expr types besides const follow a prefix rule
		 * which means they can be safely ignored (thank god) */
		if (tok == Expr::Constant) {
			uint64_t bits = rule[i];
			int	chunks;
			ret.push_back(bits);
			i++;
			chunks = (bits + 63)/64;
			while (chunks) {
				ret.push_back(rule[i]);
				i++;
				chunks--;
			}
		}
	}

	return ret;
}

bool Pattern::isConst(void) const { return (rule[0] == Expr::Constant); }

void Pattern::dump(std::ostream& os) const
{
	foreach (it, rule.begin(), rule.end())
		os << (void*)(*it) << ' ';
	os << '\n';
}

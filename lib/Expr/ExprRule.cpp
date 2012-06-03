#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include "static/Sugar.h"
#include "klee/util/ExprVisitor.h"
#include "klee/util/ExprUtil.h"
#include "ExprRule.h"
#include "ExprPatternMatch.h"

using namespace klee;

class ExprFlatWriter : public ExprConstVisitor
{
public:
	ExprFlatWriter(std::ostream* _os = NULL) : os(_os) {}
	virtual ~ExprFlatWriter(void) {}
	void setOS(std::ostream* _os) { os = _os; }
protected:
	virtual Action visitExpr(const Expr* expr);
	std::ostream		*os;
	ExprHashMap<unsigned>	labels;
};

ExprFlatWriter::Action ExprFlatWriter::visitExpr(const Expr* e)
{
	if (e->getKind() == Expr::Read) {
		ref<Expr>	re(const_cast<Expr*>(e));
		if (!labels.count(re)) {
			unsigned next_lid = labels.size();
			labels[re] = next_lid;
		}
		if (os)
			*os << " l" << labels.find(re)->second << ' ';
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

class EFWTagged : public ExprVisitorTags<ExprFlatWriter>
{
public:
	EFWTagged(const exprtags_ty& _tags_pre)
	: ExprVisitorTags<ExprFlatWriter>(_tags_pre, dummy) {}

	virtual void apply(const ref<Expr>& e)
	{
		tag_c = 0;
		ExprVisitorTags<ExprFlatWriter>::apply(e);
	}

	virtual ~EFWTagged() {}
protected:
	virtual ExprFlatWriter::Action preTagVisit(const Expr* e)
	{
		(*os) << "c" << tag_c++
			<< ' ' << e->getWidth()
			<< ' ' << *e << ' ';
		return Close;
	}
	virtual void postTagVisit(const Expr* e) {}
private:
	unsigned		tag_c;
	static exprtags_ty	dummy;
};
exprtags_ty EFWTagged::dummy;

ExprRule::ExprRule(const Pattern& _from, const Pattern& _to)
: from(_from)
, to(_to)
, const_constraints(NULL)
{
	unsigned max_id;

//	max_id = (from.label_c > to.label_c) ? from.label_c : to.label_c;
	max_id = from.label_c + to.label_c;
	from.label_id_max = max_id;
	to.label_id_max = max_id;
}

ExprRule* ExprRule::loadRule(const char* path)
{
	ExprRule	*r;
	if ((r = loadBinaryRule(path)) != NULL)
		return r;
	return loadPrettyRule(path);
}

bool ExprRule::operator==(const ExprRule& er) const
{
	if (to != er.to) return false;
	if (from != er.from) return false;

	return true;
}

void ExprRule::printBinaryRule(std::ostream& os) const
{
	uint64_t	hdr;

	hdr = (const_constraints == NULL)
		? ER_HDR_MAGIC1
		: ER_HDR_MAGIC2;
	assert (const_constraints == NULL && "STUB: Write constraints");

	os.write((char*)&hdr, 8);
	printBinaryPattern(os, from);
	printBinaryPattern(os, to);
}

ExprRule* ExprRule::changeDest(const ExprRule* er, const ref<Expr>& to)
{
	std::stringstream	ss;
	printRule(ss, er->getFromExpr(), to);
	return loadPrettyRule(ss);
}

void ExprRule::printTombstone(std::ostream& os, unsigned range_len)
{
	char		*buf;
	uint64_t	hdr = ER_HDR_SKIP;
	uint32_t	skip_len;

	assert (range_len >= 12 && "Not enough space for tombstone");

	// amount of space to skip past off=HDR+skip
	skip_len = range_len - (sizeof(hdr) + sizeof(skip_len));
	os.write((char*)&hdr, sizeof(hdr));
	os.write((char*)&skip_len, sizeof(skip_len));

	if (!skip_len) return;

	/* clear the rest out */
	buf = new char[skip_len];
	memset(buf, 0xfe, skip_len);
	os.write(buf, skip_len);
	delete [] buf;
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
	ExprRule	*er;
	unsigned	off;

	/* skip tombstones */
	do {
		uint32_t	skip;

		hdr = 0;
		is.read((char*)&hdr, 8);
		if (hdr != ER_HDR_SKIP)
			break;

		is.read((char*)&skip, 4);
		is.ignore(skip);
	} while (hdr == ER_HDR_SKIP);

	if (is_hdr_magic(hdr) == false)
		return NULL;

	off = (unsigned)is.tellg() - 8;

	loadBinaryPattern(is, p_from);
	loadBinaryPattern(is, p_to);

	assert (hdr != ER_HDR_MAGIC2 && "STUB: EXPECT CONSTRAINTS");

	if (is.fail())
		return NULL;

	er = new ExprRule(p_from, p_to);
	er->off_hint = off;

	return er;
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

ref<Expr> ExprRule::materialize(void) const
{

	ref<Expr>	lhs, rhs;

	lhs = getFromExpr();
	assert (lhs.isNull() == false);
	rhs = getToExpr();
	assert (rhs.isNull() == false);

	return EqExpr::create(lhs, rhs);
}

ExprRule* ExprRule::loadPrettyRule(const char* fname)
{
	struct stat	s;

	if (stat(fname, &s) != 0)
		return NULL;

	if (!S_ISREG(s.st_mode))
		return NULL;

	std::ifstream	ifs(fname);
	return loadPrettyRule(ifs);
}

ExprRule* ExprRule::loadPrettyRule(std::istream& is)
{
	Pattern		p_from, p_to;

	if (!p_from.readFlatExpr(is)) return NULL;
	if (!p_to.readFlatExpr(is)) return NULL;

	if (	p_from.rule.size() == 0 || p_to.rule.size() == 0 ||
		(p_from.rule.size() == 1 && p_to.rule.size() == 1))
	{
		return NULL;
	}

	return new ExprRule(p_from, p_to);
}

void ExprRule::printExpr(std::ostream& os, const ref<Expr>& e)
{
	ExprFlatWriter	efw(&os);
	efw.apply(e);
}

void ExprRule::printRule(
	std::ostream& os, const ref<Expr>& lhs, const ref<Expr>& rhs)
{
	ExprFlatWriter	efw(&os);
	unsigned	lhs_nodes, rhs_nodes;

	lhs_nodes = ExprUtil::getNumNodes(lhs);
	rhs_nodes = ExprUtil::getNumNodes(rhs);

	efw.apply((lhs_nodes > rhs_nodes) ? lhs : rhs);
	os << "\n->\n";
	efw.apply((lhs_nodes > rhs_nodes) ? rhs : lhs);
	os << "\n\n";
}

ref<Expr> ExprRule::apply(
	const ref<Expr>& e,
	ExprPatternMatch::RuleIterator& ri)
{
	labelmap_ty		lm;
	ExprPatternMatch	epm(ri, lm);

	/* match 'e' with ruleset */
	if (epm.match(e) == false)
		return NULL;

	/* got a match; materialize to-expr */
	return ri.getExprRule()->to.flat2expr(lm);
}

class SingleRuleIterator : public ExprPatternMatch::RuleIterator
{
public:
	bool isDone(void) const { return (it == from_rule.end()); }
	bool matchValue(uint64_t v)
	{
		if (isDone()) return false;
		if (*it != v) return false;

		it++;
		return true;
	}

	bool skipValue(void)
	{
		if (isDone()) return false;
		it++;
		return true;
	}

	void reset(void) { it = from_rule.begin(); }

	bool matchLabel(uint64_t& v)
	{
		if (isDone()) return false;

		v = *it;
		if (!OP_LABEL_TEST(v)) return false;

		it++;
		return true;
	}

	bool matchCLabel(uint64_t& v)
	{
		if (isDone()) return false;

		v = *it;
		if (!OP_CLABEL_TEST(v)) return false;

		it++;
		return true;
	}

	const ExprRule* getExprRule(void) const
	{
		assert (isDone() && "Grabbing to-rule but didn't verify!");
		return er;
	}

	SingleRuleIterator(const ExprRule* _er)
	: er(_er)
	, from_rule(er->getFromKey())
	, it(from_rule.begin()) {}

	virtual ~SingleRuleIterator() {}

private:
	const ExprRule			*er;
	const flatrule_ty		&from_rule;
	flatrule_ty::const_iterator	it;
	bool				verified;
};

ref<Expr> ExprRule::apply(const ref<Expr>& e) const
{
	SingleRuleIterator	sri(this);
	return apply(e, sri);
}

/* assumes unique variables for every repl */
ExprRule* ExprRule::addConstraints(
	const Array			*repl_arr,
	const exprtags_ty		&visit_tags,
	const std::vector<ref<Expr> >	&constraints) const
{
	ExprRule		*ret;
	EFWTagged		efwt(visit_tags);
	std::stringstream	ss;
	Pattern			p;
	ref<Expr>		e;

	/* create from-pattern, marking constants as slots */
	efwt.setOS(&ss);
	efwt.apply(getFromExpr());

	if (p.readFlatExpr(ss) == false) {
		std::cerr << "[ExprRule] Failed to add constraints\n";
		return NULL;
	}

	ret = new ExprRule(p, to);

	/* add constraints */
	ret->const_constraints = new std::vector<Pattern>();
	foreach (it, constraints.begin(), constraints.end()) {
		std::stringstream	ss;
		ExprFlatWriter		efw(&ss);
		Pattern			new_p;
		bool			ok;

		efw.apply(*it);
		ok = new_p.readFlatExpr(ss);
		assert (ok == true);

		ret->const_constraints->push_back(new_p);
	}

	return ret;
}

bool ExprRule::checkConstants(const labelmap_ty& clm) const
{
	unsigned	constr_c;

	/* vacuously true */
	if (const_constraints == NULL)
		return true;

	/* try the consts against the constraints */
	constr_c = const_constraints->size();
	for (unsigned i = 0; i < constr_c; i++) {
		ref<Expr>			e, c_e;
		const Pattern			*p;
		labelmap_ty::const_iterator	it;
		labelmap_ty			tmp_lm;

		it = clm.find(i);
		if (it == clm.end())
			return false;
		c_e = it->second;

		/* build fake label map for constant-- 8 bits each */
		for (unsigned j = 0; j < c_e->getWidth() / 8; j++)
			tmp_lm[j] = ExtractExpr::create(c_e, j*8, 8);

		/* evaluate by filling out pattern */
		p = &(*const_constraints)[i];
		e = p->flat2expr(tmp_lm);

		if (e.isNull() == true)
			return false;

		if (e->getKind() != Expr::Constant)
			return false;

		if (e->isTrue() == false)
			return false;
	}

	return true;
}

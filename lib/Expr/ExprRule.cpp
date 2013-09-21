int xxx_rb;
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
#include "ExprFlatWriter.h"
#include "murmur3.h"

using namespace klee;

/* build fake label map for expression-- 8 bits each */
static void expr_to_lmap(const ref<Expr>& e, labelmap_ty& lm)
{
	unsigned byte_c;
	byte_c = e->getWidth() / 8;
	for (unsigned j = 0; j < byte_c; j++)
		lm[byte_c - (j+1)] = MK_EXTRACT(e, j*8, 8);
}

ExprRule::~ExprRule()
{ if (const_constraints != NULL) delete const_constraints; }

ExprRule::ExprRule(
	const Pattern& _from,
	const Pattern& _to,
	const std::vector<Pattern>* constrs)
: from(_from)
, to(_to)
, const_constraints(NULL)
{
	unsigned max_id;

//	max_id = (from.label_c > to.label_c) ? from.label_c : to.label_c;
	max_id = from.label_c + to.label_c;
	from.label_id_max = max_id;
	to.label_id_max = max_id;

	if (constrs != NULL) {
		const_constraints = new std::vector<Pattern>();
		*const_constraints = *constrs;
		from.clabel_c = const_constraints->size();
	}
}

ExprRule::ExprRule(const ExprRule& er)
{
	/* shallow copy data */
	*this = er;
	/* deep copy pointers */
	if (er.const_constraints != NULL) {
		const_constraints = new std::vector<Pattern>();
		*const_constraints = *er.const_constraints;
	}
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

	if (	const_constraints == NULL &&
		er.const_constraints == NULL)
	{
		return true;
	}

	if (	const_constraints == NULL ||
		er.const_constraints == NULL)
	{
		return false;
	}

	return *const_constraints == *er.const_constraints;
}

bool ExprRule::operator<(const ExprRule& er) const
{
	if (to != er.to)
		return (to < er.to);
	if (from != er.from)
		return (from < er.from);

	if (	const_constraints == NULL &&
		er.const_constraints == NULL)
	{
		/* == */
		return false;
	}

	if (const_constraints == NULL)
		return true;

	if (er.const_constraints == NULL)
		return false;

	return *const_constraints < *er.const_constraints;
}

void ExprRule::printBinaryRule(std::ostream& os) const
{
	uint64_t	hdr;
	uint16_t	num_constrs;

	hdr = (const_constraints == NULL)
		? ER_HDR_MAGIC1
		: ER_HDR_MAGIC2;

	os.write((char*)&hdr, 8);
	printBinaryPattern(os, from);
	printBinaryPattern(os, to);

	if (const_constraints == NULL)
		return;

	/* 64 thousand constraints should be enough for any rule */
	num_constrs = const_constraints->size();
	os.write((char*)&num_constrs, 2);
	foreach (it, const_constraints->begin(), const_constraints->end())
		printBinaryPattern(os, *it);
}

void ExprRule::printPrettyRule(std::ostream& os) const
{
	assert (const_constraints == NULL);
	printRule(os, getFromExpr(), getToExpr());
}

void ExprRule::print(std::ostream& os, bool parens) const
{
	unsigned	i;
	ref<Expr>	f_e(getFromExpr()), t_e(getToExpr());

	if (f_e.isNull() || t_e.isNull())
		os << "???\n";
	else {
		if (parens)
			os << f_e << "\n -> \n" << t_e << "\n\n";
		else
			printRule(os, f_e, t_e);
	}

	if (const_constraints == NULL)
		return;

	i = 0;
	foreach (it, const_constraints->begin(), const_constraints->end()) {
		os << "Const[" << i++ << "]: ";
		printConstr(os, *it);
		os << '\n';
	}
}

void ExprRule::printConstr(std::ostream& os, const Pattern& p)
{
	ExprFlatWriter	efw(&os);
	ref<Expr>	pat_expr;

	pat_expr = p.anonFlat2Expr(p.label_c*8);
	efw.apply(pat_expr);
}

ExprRule* ExprRule::changeDest(const ExprRule* er, const ref<Expr>& to)
{
	labelmap_ty		lm;
	Pattern			new_to;
	std::stringstream	ss;

	er->to.getLabelMap(lm, er->to.label_id_max);
	printExpr(ss, to, lm);
	new_to.readFlatExpr(ss);
	if (new_to.size() == 0)
		return NULL;

	return new ExprRule(er->from, new_to, er->const_constraints);
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
	uint64_t		hdr;
	Pattern			p_from, p_to;
	ExprRule		*er;
	unsigned		off;
	std::vector<Pattern>	*constr = NULL;

	/* skip tombstones */
	do {
		uint32_t	skip;

		hdr = 0;
		if (!is.read((char*)&hdr, 8))
			return NULL;

		if (hdr != ER_HDR_SKIP)
			break;

		if (!is.read((char*)&skip, 4))
			return NULL;

		is.ignore(skip);
	} while (hdr == ER_HDR_SKIP);

	if (is_hdr_magic(hdr) == false)
		return NULL;

	off = (unsigned)is.tellg() - sizeof(hdr);

	if (!loadBinaryPattern(is, p_from)) return NULL;
	if (!loadBinaryPattern(is, p_to)) return NULL;
	if (is.fail()) return NULL;

	/* load constant constraints if v2 */
	if (hdr == ER_HDR_MAGIC2) {
		uint16_t	num_constr;

		if (!is.read((char*)&num_constr, 2))
			return NULL;

		constr = new std::vector<Pattern>(num_constr);
		for (unsigned i = 0; i < num_constr; i++) {
			if (!loadBinaryPattern(is, (*constr)[i])) {
				delete constr;
				return NULL;
			}
		}

		if (is.fail()) {
			delete constr;
			return NULL;
		}
	}

	er = new ExprRule(p_from, p_to, constr);
	er->off_hint = off;
	if (constr) delete constr;

	return er;
}

bool ExprRule::hasFree(void) const
{
	ref<Expr>		from(getFromExpr());
	std::vector<ref<Array> > objs;
	ref<Array>		free_arr;

	if (from.isNull()) return false;

	ExprUtil::findSymbolicObjectsRef(from, objs);

	free_arr = Pattern::getFreeArray();
	foreach (it, objs.begin(), objs.end()) {
		if (free_arr.get() == (*it).get())
			return true;
	}

	return false;
}

bool ExprRule::loadBinaryPattern(std::istream& is, Pattern& p)
{
	uint32_t	sz;
	uint32_t	l_c;

	if (!is.read((char*)&sz, 4)) return false;
	if (!is.read((char*)&l_c, 4)) return false;
	p.rule.resize(sz);
	p.label_c = l_c;
	if (!is.read((char*)p.rule.data(), sz*8)) return false;
	return true;
}

/* unsound materialization which is underconstrained */
ref<Expr> ExprRule::materializeUnsound(void) const
{
	ref<Expr>	lhs, rhs;

	lhs = getFromExpr();
	if (lhs.isNull())
		return NULL;

	rhs = getToExpr();
	if (rhs.isNull())
		return NULL;

	return EqExpr::create(lhs, rhs);
}

ref<Expr> ExprRule::materialize(void) const
{
	ref<Expr>	lhs, rhs, e_constrs;
	clabelmap_ty	cm;

	if (const_constraints == NULL) return materializeUnsound();

	/* this does not need to be slotted out */
	rhs = getToExpr();
	if (rhs.isNull())
		return NULL;

	/* this needs constants slotted out */
	lhs = from.anonFlat2ConstrExpr(cm);
	if (lhs.isNull())
		return NULL;

	/* build constraint conjunction */
	e_constrs = MK_CONST(1, 1);
	for (unsigned i = 0; i < const_constraints->size(); i++) {
		ref<Expr>	cur_constr, clbl;
		labelmap_ty	lm;

		clbl = (cm.find(i))->second;
		expr_to_lmap(clbl, lm);
		cur_constr = (*const_constraints)[i].flat2expr(lm);

		e_constrs = MK_AND(cur_constr, e_constrs);
	}

	return Expr::createImplies(e_constrs, MK_EQ(lhs, rhs));
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

void ExprRule::printExpr(
	std::ostream& os, const ref<Expr>& e, const labelmap_ty& lm)
{
	ExprFlatWriter	efw(&os);
	efw.setLabelMap(lm);
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
		if (!OP_LABEL_TEST(v) && !OP_CLABEL_TEST(v) && !OP_VAR_TEST(v))
			return false;
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

/* assumes unique variables for every repl */
ExprRule* ExprRule::addConstraints(
	const exprtags_ty		&visit_tags,
	const std::vector<ref<Expr> >	&constraints) const
{
	ExprRule		*ret;
	EFWTagged		efwt(visit_tags);
	std::stringstream	ss;
	Pattern			p;
	ref<Expr>		e;

	/* already has constraints */
	if (const_constraints) {
		std::cerr << "[ExprRule] XXX rule always has constraints\n";
		return NULL;
	}

	/* create from-pattern, marking constants as slots */
	efwt.setOS(&ss);

	/* skip over already given constraints */
	efwt.setConstTag((const_constraints) ? const_constraints->size() : 0);

	efwt.apply(getFromExpr());

	if (p.readFlatExpr(ss) == false) {
		std::cerr << "[ExprRule] Failed to add constraints\n";
		std::cerr << "BAD PATTERN: " << ss.str() << '\n';
		return NULL;
	}

	ret = new ExprRule(p, to, const_constraints);

	/* add constraints */
	if (ret->const_constraints == NULL)
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

	std::cerr << "[ExprRule] Constraints added:\n";
	ret->print(std::cerr);

	return ret;
}

/* XXX: this will wreck things with label numbers right now
 * how to fix?? */
ExprRule* ExprRule::markUnbound(int tag) const
{
	ref<Expr>		from_expr;
	exprtags_ty		visit_tag(1, tag);
	EFWTagged		efwt(visit_tag, false);
	std::stringstream	ss;
	Pattern			p_from;

	/* create from-pattern, marking constants as slots */
	efwt.setOS(&ss);
	from_expr = getFromExpr();
	efwt.apply(from_expr);

	if (p_from.readFlatExpr(ss) == false) {
		std::cerr << "[ExprRule] Failed to unbind subtree.\n";
		std::cerr << "BAD PATTERN: " << ss.str() << '\n';
		return NULL;
	}

	assert (efwt.visitedTag());

	if (to.isConst() || !efwt.getMaskedNew()) {
		/* no need to translate */
		return new ExprRule(p_from, to, const_constraints);
	}

	/* must do a translation of the to-expr;
	 * labels may have been shifted around.
	 * The algorithm is as follows: keep an ordered list of
	 * visited reads for a normal flat write.
	 * On a var replace, remember the position in the list where
	 * we started chopping reads and the number of reads removed.
	 * Labels *prior* to the mask are OK.
	 * Labels after masked range must be recomputed based on
	 * when they're first seen.
	 * e.g. if I mask labels 2 and 3, then see 3 and 2,
	 * I'd know that 3->2 and 2->3. */
	ss.str("");
	ExprFlatWriter	efw(&ss);
	efw.apply(from_expr);

	/* construct correspondence */
	const labellist_ty	&ll(efw.getLabelList());
	std::vector<std::pair<unsigned, unsigned> > old2new;
	std::set<unsigned>	known_lids;

	/* label ids known before replacement */
	for (unsigned i = 0; i < efwt.getMaskedStartLID(); i++)
		known_lids.insert(i);

	for (	unsigned i = efwt.getMaskedBase() + efwt.getMaskedReads();
		i < ll.size(); i++)
	{
		if (known_lids.count(ll[i]))
			continue;

		old2new.push_back(std::make_pair(ll[i], known_lids.size()));
		known_lids.insert(ll[i]);
	}

	ExprFlatWriter	efw_to(&ss);
	Pattern		p_to;
	labelmap_ty	lm, lm_new;

	efw.getLabelMap(lm);

	/* establish new mappings */
	foreach (it, old2new.begin(), old2new.end())
		lm_new.insert(std::make_pair(it->second, lm[it->first]));

	/* read in old mappigns */
	foreach (it, lm.begin(), lm.end())
		if (!lm_new.count(it->first))
			lm_new.insert(std::make_pair(it->first, it->second));

	efw_to.setLabelMap(lm_new);
	ss.str(""); ss.clear();
	efw_to.apply(getToExpr());
	if (p_to.readFlatExpr(ss) == false) {
		std::cerr << "[ExprRule] Failed to rebind to-expr.\n";
		return NULL;
	}

	return new ExprRule(p_from, p_to, const_constraints);
}

/* verify that the slotted constants collected during matching
 * satisfy the constant constraints */
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

		/* all-accepting? */
		p = &(*const_constraints)[i];
		if (p->isConst())
			continue;

		it = clm.find(i);
		if (it == clm.end())
			return false;
		c_e = it->second;

		expr_to_lmap(c_e, tmp_lm);

		/* evaluate by filling out pattern */
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

ExprRule* ExprRule::createRule(const ref<Expr>& lhs, const ref<Expr>& rhs)
{
	std::stringstream	ss;
	printRule(ss, lhs, rhs);
	return loadPrettyRule(ss);
}

uint64_t ExprRule::hash(void) const
{
	uint64_t		ret[128/(8*8)];
	std::stringstream	ss;
	std::string		s;

	printBinaryRule(ss);
	s = ss.str();

	MurmurHash3_x64_128(s.c_str(), s.size(), 123, &ret);
	return ret[0];
}

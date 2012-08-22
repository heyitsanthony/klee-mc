#include "static/Sugar.h"
#include "klee/ExecutionState.h"
#include "ShadowObjectState.h"
#include "TaintGroup.h"

using namespace klee;

TaintGroup::TaintGroup(void)
: total_states(0)
, total_insts(0)
{}

TaintGroup::~TaintGroup(void) {}

void TaintGroup::apply(ExecutionState* st)
{
	indep_byte_c = 0, dep_byte_c = 0, full_c = 0;
	foreach (it, taint_addr_c.begin(), taint_addr_c.end()) {
		std::cerr << "APPLYING TO " << (void*)it->first << '\n';
		mergeAddr(st, it->first);
	}

	std::cerr << "[TaintGroup] Applied. Indep bytes="
		<< indep_byte_c
		<< ". Dep bytes=" << dep_byte_c
		<< ". Full bytes=" << full_c << '\n';
}


bool TaintGroup::mergeAddrIndep(
	uint64_t taint_addr,
	unsigned off,
	ShadowObjectState* sos)
{
	if (taint_addr_c[taint_addr] != total_states)
		return false;

	full_c++;
	if (!taint_addr_v.count(taint_addr))
		return false;

	ref<Expr>	e(taint_addr_v[taint_addr]);
	e = getUntainted(e);
	assert (e->isShadowed() == false);
	sos->write(off, e);
	assert (sos->read8(off)->isShadowed() == false);
	indep_byte_c++;
	return true;
}

void TaintGroup::mergeAddr(ExecutionState* st, uint64_t taint_addr)
{
	ObjectPair		op;
	ObjectState		*os;
	ShadowObjectState	*sos;
	unsigned		off;

	if (st->addressSpace.resolveOne(taint_addr, op) == false) {
		std::cerr << "[TaintGroup] Bad??\n";
		assert (0 == 1);
		return;
	}
	
	off = taint_addr - op.first->address;

	os = st->addressSpace.getWriteable(op);
	sos = static_cast<ShadowObjectState*>(os);
	if (mergeAddrIndep(taint_addr, off, sos)) {
		assert (sos->read8(off)->isShadowed() == false);
		return;
	}

	ref<Expr>	new_expr(getUntainted(sos->read8(off)));
	foreach (it,
		taint_addr_all[taint_addr].begin(),
		taint_addr_all[taint_addr].end())
	{
		ref<Expr>	cond, t_v;

		cond = getUntainted(it->second->getV());
		t_v = getUntainted(it->first);

		new_expr = MK_SELECT(cond, t_v, new_expr);
	}

	assert (!new_expr->isShadowed());
	sos->write(off, new_expr);

	assert (sos->read8(off)->isShadowed() == false);
	dep_byte_c++;
}

void TaintGroup::addState(const ExecutionState *es)
{
	foreach (it, es->addressSpace.begin(), es->addressSpace.end()) {
		const ObjectState	*os;
		const ShadowObjectState	*sos;

		os = it->second;
		sos = dynamic_cast<const ShadowObjectState*>(os);
		if (sos == NULL) continue;
		if (sos->isClean()) continue;

		addTaintedObject(it->first->address, sos);
	}

	total_states++;
	total_insts += es->personalInsts;
}

void TaintGroup::addTaintedObject(uint64_t addr, const ShadowObjectState* sos)
{
	for (unsigned i = 0; i < sos->size; i++) {
		unsigned	taint_c;
		ref<Expr>	r;

		if (!sos->isByteTainted(i)) continue;

		taint_c = taint_addr_c[i+addr];
		taint_addr_c[i+addr] = taint_c+1;

		r = sos->read8(i);
		ShadowRef	r_s(ShadowAlloc::getExpr(r));
		taint_addr_all[i+addr].insert(
			TaintPair(
				r,
				cast<ShadowValExpr>(r_s->getShadow())));

		/* control dependencies? */
		if (taint_c == 0) {
			taint_addr_v[i+addr] = r;
		} else {
			ref<Expr>	cur_e(sos->read8(i));
			if (	taint_addr_v.count(i+addr) &&
				taint_addr_v[i+addr] != cur_e)
			{
				taint_addr_v.erase(i+addr);
			}
		}
	}
}

void TaintGroup::dump(void) const
{
	foreach (it, taint_addr_c.begin(), taint_addr_c.end()) {
		std::cerr << "ADDR=" << (void*)it->first
			<< ". COUNT=" << it->second << '\n';
	}
}

ref<Expr> TaintGroup::getUntainted(ref<Expr> e)
{
#if 1
	std::set<ref<Expr> >::const_iterator	it(untainted.find(e));
	if (it == untainted.end()) {
		ref<Expr>	ret(ShadowAlloc::drop(e));
		untainted.insert(ret);
		return ret;
	}
	return *it;
#else
	return ShadowAlloc::drop(e);
#endif
}

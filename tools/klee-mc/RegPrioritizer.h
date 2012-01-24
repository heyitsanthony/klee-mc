#ifndef REGPRIORITIZER_H
#define REGPRIORITIZER_H

namespace klee
{
class RegPrioritizer : public Prioritizer
{
public:
	RegPrioritizer(ExecutorVex& in_exe)
	: exe(in_exe)
	{ gs = exe.getGuest(); }

	virtual Prioritizer* copy(void) const
	{ return new RegPrioritizer(exe); }

	virtual ~RegPrioritizer() {}

	int getPriority(ExecutionState& st)
	{
		const ObjectState	*os = GETREGOBJRO(st);
		if (os == NULL)
			return -123;
//		return (getRegPr(st, os, 1)+getRegPr(st, os, 0)+getRegPr(st, os, ~0));
		return getSymCount(st, os);
	}

	int getSymCount(
		ExecutionState		&st,
		const ObjectState	*os)
	{
		int	sym_c;

		sym_c = 0;
		for (unsigned i = 0; i < os->size; i++)
			if (!os->isByteConcrete(i))
				sym_c++;
		return sym_c;
	}


	int getRegPr(
		ExecutionState		&st,
		const ObjectState	*os,
		uint64_t		target)
	{
		ref<Expr>		reg_val;
		uint64_t		ret_base;

		ret_base = gs->getCPUState()->getRetOff();
		for (unsigned i = 0; i < 8; i++)
			if (!os->isByteConcrete(i))
				return -2;

		reg_val = st.read(os, ret_base, 64);
		if (const ConstantExpr* ce = dyn_cast<ConstantExpr>(reg_val)) {
			if (ce->getZExtValue() == target)
				return 0;
			return -1;
		}

		return -3;

	}

	int getHashPr(ExecutionState& st, const ObjectState* os)
	{
		int64_t	hits;
		uint64_t hash = 0;

		if (os == NULL)
			return -123;

		for (unsigned i = 0; i < os->size; i++) {
			const ConstantExpr	*ce;

			if (!os->isByteConcrete(i))
				continue;

			ce = dyn_cast<ConstantExpr>(st.read(os, i, 8));
			if (ce == NULL)
				continue;

			hash += hash^((i+1)*ce->getZExtValue()+i);
		}

		objhashes_ty::const_iterator it;
		it = objhashes.find(hash);
		if (it == objhashes.end()) {
			hits = 1;
		} else {
			hits = it->second;
		}

		if (!isLatched()) {
			objhashes[hash] = hits+1;
		}
		
		return -hits;
	}
private:
	typedef std::map<uint64_t, uint64_t>	objhashes_ty;
	objhashes_ty	objhashes;
	ExecutorVex	&exe;
	const Guest	*gs;
};
}
#endif

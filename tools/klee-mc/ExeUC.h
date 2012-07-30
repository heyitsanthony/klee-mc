#ifndef KLEE_EXEUC_H
#define KLEE_EXEUC_H

#include "ExecutorVex.h"
#include "guestcpustate.h"

#include "UCTabEnt.h"

namespace klee
{
class ObjectState;
class Array;

struct UCPtr
{
	static UCPtr failure(void)
	{
		UCPtr	ucp;
		ucp.base_arr = 0;
		ucp.ptrtab_idx = 0;
		ucp.base_off = 0;
		ucp.depth = ~0;
		return ucp;
	}
	const Array	*base_arr;
	int		ptrtab_idx;
	ref<Expr>	base_off;
	unsigned	depth;
};

class ExeUC : public ExecutorVex
{
public:
class UCPtrFork
{
public:
	UCPtrFork(StatePair& in_sp, ref<Expr>& in_real)
	: sp(in_sp), real_ptr(in_real) {}

	virtual ~UCPtrFork() {}

	ExecutionState* getState(bool t) const
	{ return (t) ? sp.first : sp.second; }

	ref<Expr> getRealPtr(void) const { return real_ptr; }

private:
	StatePair	sp;
	ref<Expr>	real_ptr;
};
	ExeUC(InterpreterHandler *ie);
	virtual ~ExeUC();

	virtual void runImage(void);
	void setupUCEntry(
		ExecutionState* start_state,
		const char *xchk_fn);
	const Array* getRootArray(void) const { return root_reg_arr; }
	const Array* getPtrTabArray(void) const { return lentab_arr; }

	UCPtrFork initUCPtr(
		ExecutionState& st, unsigned idx,
		ref<Expr>& ptr_expr, unsigned min_sz);

	UCPtrFork forkUCPtr(
		ExecutionState	&es,
		MemoryObject	*new_mo,
		unsigned	idx);
	ExecutionState* forkNullPtr(ExecutionState& es, unsigned pt_idx);

	ref<Expr> getUCRealPtr(ExecutionState& es, unsigned idx);
	ref<Expr> getUCSymPtr(ExecutionState& es, unsigned idx);
	uint64_t getUCSym2Real(ExecutionState& es, ref<Expr> sym_ptr);

	unsigned sym2idx(const Expr* sym_ptr) const;
	unsigned tabOff2Idx(unsigned n) const { return n / lentab_elem_len; }
	bool isRegIdx(int idx) const { return idx < (int)lentab_reg_ptrs; }


	unsigned getPtrBytes(void) const;

	void finalizeBuffers(ExecutionState& es);

protected:
	void runSym(const char* sym_name);

private:
	/* format:
	 * 0		  			n
	 * [ length  | sym_ptr | real_ptr	]
	 */
#define LEN_OFF		0
#define SYMPTR_OFF	(4)
#define REALPTR_OFF	(4+getPtrBytes())
#define INITALIGN_OFF	(4+2*getPtrBytes())

	void setupUCAlignment(
		ExecutionState& es, unsigned idx, ref<Expr>& ptr_expr);



	MemoryObject	*lentab_mo;
	const Array	*lentab_arr;
	unsigned int	lentab_reg_ptrs;
	unsigned int	lentab_elem_len;
	unsigned int	lentab_max;

	const Array	*root_reg_arr;
};
}

#endif

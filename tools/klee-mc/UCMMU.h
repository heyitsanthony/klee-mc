#ifndef UCMMU_H
#define UCMMU_H

#include "../../lib/Core/KleeMMU.h"
#include "ExeUC.h"

namespace klee {

typedef std::map<int, ref<Expr> > ptrtab_map;

#define MAX_EXPAND_SIZE	128
class UCMMU: public KleeMMU
{
public:
	struct UCRewrite {
		ptrtab_map		ptrtab_idxs;
		std::set<ref<Expr> >	untracked;
		ref<Expr>		old_expr;
		ref<Expr>		new_expr;
	};

	UCMMU(ExeUC& uc)
	: KleeMMU(uc)
	, exe_uc(uc)
	, aborted(true) {}

	virtual ~UCMMU(void) {}
	virtual bool exeMemOp(ExecutionState &state, MemOp& mop);
protected:
	virtual MemOpRes memOpResolve(
		ExecutionState& state,
		ref<Expr> address,
		Expr::Width type);
	virtual void memOpError(ExecutionState& state, MemOp& mop);

private:
	void resolveSymbolicOffset(
		ExecutionState& state, MemOp& mop, uint64_t residue);

	void bindFixedUC(ExecutionState& state, MemOp& mop, uint64_t real_addr);
	void bindUnfixedUC(
		ExecutionState& state,
		MemOp& mop,
		ref<Expr> ful_ptr_sym,
		uint64_t residue);

	void handleSymResteer(ExecutionState& state, MemOp& mop);
	void assignNewPointer(
		ExecutionState& state, MemOp& mop, uint64_t residue);
	void assignRegPointer(
		ExecutionState& state, MemOp& mop, uint64_t residue,
		int ptab_idx);


	void expandUnfixed(
		ExecutionState& state,
		MemOp& mop,
		unsigned pt_idx,
		uint64_t real_addr,
		uint64_t req_addr);
	void expandMO(
		ExecutionState& state,
		unsigned resize_len,
		ObjectPair &res);

	const char* expandRealPtr(
		ExecutionState& state, 
		uint64_t base_ptr,
		ref<Expr> full_ptr,
		ObjectPair& res);

	UCRewrite rewriteAddress(ExecutionState& state, ref<Expr> address);

	MemOpRes resolveRootPtr(
		ExecutionState& state,
		ref<Expr> address,
		Expr::Width type,
		unsigned ptrtab_idx);
	UCPtr getPtrArray(ref<Expr> &e);

	UCRewrite	resteer;
	bool		resteered;

	ExeUC		&exe_uc;
	bool		is_write;
	bool		aborted;
};
}

#endif

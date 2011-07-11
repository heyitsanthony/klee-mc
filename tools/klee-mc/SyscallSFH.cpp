#include "klee/Expr.h"
#include "ExecutorVex.h"
#include "SyscallSFH.h"
#include "guestcpustate.h"

extern "C"
{
#include "valgrind/libvex_guest_amd64.h"
}

using namespace klee;

static const unsigned int NUM_HANDLERS = 6;
static SpecialFunctionHandler::HandlerInfo hInfo[NUM_HANDLERS] =
{
#define add(name, h, ret) {	\
	name, 			\
	&Handler##h::create,	\
	false, ret, false }
#define addDNR(name, h) {	\
	name, 			\
	&Handler##h::create,	\
	true, false, false }
  add("kmc_sc_regs", SCRegs, true),
  add("kmc_sc_bad", SCBad, false),
  add("kmc_free_run", FreeRun, false),
  addDNR("kmc_exit", KMCExit),
  add("kmc_make_range_symbolic", MakeRangeSymbolic, false),
  add("kmc_alloc_aligned", AllocAligned, true)
#undef addDNR
#undef add
};


SyscallSFH::SyscallSFH(Executor* e) : SpecialFunctionHandler(e)
{
	exe_vex = dynamic_cast<ExecutorVex*>(e);
	assert (exe_vex && "SyscallSFH only works with ExecutorVex!");
}

void SyscallSFH::prepare(void)
{
	SpecialFunctionHandler::prepare();
	SpecialFunctionHandler::prepare((HandlerInfo*)&hInfo, NUM_HANDLERS);
}

void SyscallSFH::bind(void)
{
	SpecialFunctionHandler::bind();
	SpecialFunctionHandler::bind((HandlerInfo*)&hInfo, NUM_HANDLERS);
}

SFH_DEF_HANDLER(SCRegs)
{
	Guest			*gs;
	MemoryObject		*cpu_mo;
	ObjectState		*state_regctx_os;

	const MemoryObject	*old_mo;
	const ObjectState	*old_regctx_os;
	unsigned int		sz;

	ExecutorVex	*exe_vex = dynamic_cast<ExecutorVex*>(sfh->executor);
	SFH_CHK_ARGS(1, "kmc_sc_regs");

	gs = exe_vex->getGuest();
	sz = gs->getCPUState()->getStateSize();

	old_mo = state.getRegCtx();
	assert (old_mo && "No register context ever set?");

	old_regctx_os = state.addressSpace.findObject(old_mo);
	// do not unbind the object, we need to be able to grab it
	// for replay
	// state.unbindObject(old_mo);

	/* 1. make all of symbolic */
	cpu_mo = exe_vex->allocRegCtx(&state);
	state_regctx_os = exe_vex->executeMakeSymbolic(state, cpu_mo, "reg");

	/* 2. set everything that should be initialized */
	for (unsigned int i=0; i < sz; i++) {
		unsigned int	reg_idx;

		reg_idx = i/8;
		if (	reg_idx == offsetof(VexGuestAMD64State, guest_RAX)/8 ||
			reg_idx == offsetof(VexGuestAMD64State, guest_RCX)/8 ||
			reg_idx == offsetof(VexGuestAMD64State, guest_R11)/8)
		{
			/* ignore rax, rcx, r11 */
			assert (state_regctx_os->isByteConcrete(i) == false);
			continue;
		}

		/* copy it by expression */
		state.write(
			state_regctx_os,
			i,
			state.read8(old_regctx_os, i));
	}

	/* make state point to right register context on xfer */
	state.setRegCtx(cpu_mo);

	state.bindLocal(target, ConstantExpr::create(cpu_mo->address, 64));
}

SFH_DEF_HANDLER(SCBad)
{
	SFH_CHK_ARGS(1, "kmc_sc_bad");
	
	ConstantExpr	*ce;
	ce = dyn_cast<ConstantExpr>(arguments[0]);
	std::cerr << "OOPS: " << ce->getZExtValue() << std::endl;
}

SFH_DEF_HANDLER(FreeRun)
{
	SFH_CHK_ARGS(2, "kmc_free_run");
	ExecutorVex	*exe_vex = dynamic_cast<ExecutorVex*>(sfh->executor);
	ConstantExpr		*addr, *len;
	uint64_t		addr_v, len_v;

	const MemoryObject	*mo;

	addr = dyn_cast<ConstantExpr>(arguments[0]);
	len = dyn_cast<ConstantExpr>(arguments[1]);
	if (addr == NULL || len == NULL) {
		exe_vex->terminateStateOnError(
			state,
			"munmap error: non-CE exprs. Bad runtime",
			"munmap.err");
		return;
	}

	addr_v = addr->getZExtValue();
	len_v = len->getZExtValue();

	mo = state.addressSpace.resolveOneMO(addr_v);
	if (mo == NULL) {
		exe_vex->terminateStateOnError(
			state,
			"munmap error: munmapping bad address",
			"munmap.err");
		return;
	}

	assert (mo->size == len_v &&
		mo->address == addr_v && "UNHANDLED BAD SIZE");

	state.unbindObject(mo);
}

SFH_DEF_HANDLER(KMCExit)
{
	ConstantExpr	*ce;
	SFH_CHK_ARGS(1, "kmc_exit");

	ce = dyn_cast<ConstantExpr>(arguments[0]);
	if (ce)  fprintf(stderr, "exitcode=%d\n", ce->getZExtValue());
	else fprintf(stderr, "exitcode=?\n");

	sfh->executor->terminateStateOnExit(state);
}

SFH_DEF_HANDLER(MakeRangeSymbolic)
{
	SFH_CHK_ARGS(3, "kmc_make_range_symbolic");

	ExecutorVex	*exe_vex = dynamic_cast<ExecutorVex*>(sfh->executor);
	ConstantExpr		*addr, *len;
	uint64_t		addr_v, len_v;
	std::string		name_str;

	addr = dyn_cast<ConstantExpr>(arguments[0]);
	len = dyn_cast<ConstantExpr>(arguments[1]);
	if (addr == NULL || len == NULL) {
		exe_vex->terminateStateOnError(
			state,
			"makerangesymolic error: non-CE exprs. Smash runtime",
			"mrs.err");
		return;
	}

	addr_v = addr->getZExtValue();
	len_v = len->getZExtValue();
	name_str = sfh->readStringAtAddress(state, arguments[2]);

	exe_vex->makeRangeSymbolic(state, (void*)addr_v, len_v, name_str.c_str());
}

SFH_DEF_HANDLER(AllocAligned)
{
	MemoryObject	*new_mo;
	SFH_CHK_ARGS(2, "kmc_alloc_aligned");

	ExecutorVex	*exe_vex = dynamic_cast<ExecutorVex*>(sfh->executor);
	ConstantExpr	*len;
	uint64_t	len_v, addr;
	std::string	name_str;

	len = dyn_cast<ConstantExpr>(arguments[0]);
	if (len == NULL) {
		exe_vex->terminateStateOnError(
			state,
			"makerangesymolic error: non-CE exprs. Smash runtime",
			"mrs.err");
		return;
	}

	len_v = len->getZExtValue();
	name_str = sfh->readStringAtAddress(state, arguments[1]);

	/* not requesting a specific address */
	new_mo = exe_vex->getMM()->allocateAligned(
		len_v,
		12 /* aligned on 2^12 */,
		target->inst,
		&state);

	if (!new_mo) {
		state.bindLocal(target, ConstantExpr::create(0, 64));
		return;
	}
	
	addr = new_mo->address;
	new_mo->setName(name_str.c_str());
	exe_vex->executeMakeSymbolic(state, new_mo, name_str.c_str());

	state.bindLocal(target, ConstantExpr::create(addr, 64));
}

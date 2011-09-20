#include "klee/Expr.h"
#include "ExecutorVex.h"
#include "ExeStateVex.h"
#include "SyscallSFH.h"
#include "guestcpustate.h"
#include "klee/breadcrumb.h"
#include "static/Sugar.h"

extern "C"
{
#include "valgrind/libvex_guest_amd64.h"
}

using namespace klee;

static const unsigned int NUM_HANDLERS = 7;
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
	add("kmc_alloc_aligned", AllocAligned, true),
	add("kmc_breadcrumb", Breadcrumb, false)
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

	old_mo = static_cast<ExeStateVex&>(state).getRegCtx();
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
	static_cast<ExeStateVex&>(state).setRegCtx(cpu_mo);

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
	uint64_t		addr_v, addr_end, cur_addr;
	uint64_t		len_v, len_remaining;

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

	// round to page size
	len_v = 4096*((len_v + 4095)/4096);
	len_remaining = len_v;
	addr_end = len_v + addr_v;

	cur_addr = addr_v;
	while (cur_addr < addr_end) {
		const MemoryObject	*mo;

		mo = state.addressSpace.resolveOneMO(cur_addr);
		if (mo == NULL && len_remaining >= 4096) {
			exe_vex->terminateStateOnError(
				state,
				"munmap error: munmapping bad address",
				"munmap.err");
			return;
		}

		if (mo->size > len_remaining) {
			std::cerr
				<< "size mismatch on munmap "
				<< mo->size << ">" << len_remaining
				<< std::endl;
			assert (0 == 1 && "BAD SIZE");
		}

		len_remaining -= mo->size;
		cur_addr += mo->size;

		state.unbindObject(mo);
	}
}

SFH_DEF_HANDLER(KMCExit)
{
	ConstantExpr	*ce;
	SFH_CHK_ARGS(1, "kmc_exit");

	ce = dyn_cast<ConstantExpr>(arguments[0]);
	if (ce)  fprintf(stderr, "exitcode=%lu\n", ce->getZExtValue());
	else fprintf(stderr, "exitcode=?\n");

	sfh->executor->terminateStateOnExit(state);
}

SFH_DEF_HANDLER(MakeRangeSymbolic)
{
	SFH_CHK_ARGS(3, "kmc_make_range_symbolic");
	SyscallSFH	*sc_sfh = static_cast<SyscallSFH*>(sfh);
	ExecutorVex	*exe_vex = static_cast<ExecutorVex*>(sc_sfh->executor);
	ConstantExpr	*addr, *len;
	uint64_t	addr_v, len_v;
	std::string	name_str;

	assert (sfh != NULL);
	assert (exe_vex != NULL);

	addr = dyn_cast<ConstantExpr>(arguments[0]);
	len = dyn_cast<ConstantExpr>(arguments[1]);
	if (addr == NULL || len == NULL) {
		exe_vex->terminateStateOnError(
			state,
			"makerangesymbolic error: Addr or len not CE exprs. Smash runtime",
			"mrs.err");
		return;
	}

	addr_v = addr->getZExtValue();
	len_v = len->getZExtValue();
	name_str = sfh->readStringAtAddress(state, arguments[2]);

	fprintf(stderr, "MAKE RANGE SYMBOLIC %s %d\n", name_str.c_str(), (int)len_v);
	sc_sfh->makeRangeSymbolic(state, (void*)addr_v, len_v, name_str.c_str());
}

SFH_DEF_HANDLER(Breadcrumb)
{
	SFH_CHK_ARGS(2, "kmc_breadcrumb");
	ConstantExpr		*len_expected_ce;
	struct breadcrumb	*bc;
	unsigned char		*buf;
	unsigned int		len_in, len_expected;

	len_expected_ce = dyn_cast<ConstantExpr>(arguments[1]);
	len_expected = (unsigned int)len_expected_ce->getZExtValue();
	buf = sfh->readBytesAtAddress(state, arguments[0], len_expected, len_in, -1);
	bc = (struct breadcrumb*)buf;
	if (len_in < sizeof(struct breadcrumb) || bc->bc_sz != len_in) {
		fprintf(stderr,
			"GOT LENGTH %d. Expected %d\n", len_in, bc->bc_sz);
		assert (0 == 1);
		sfh->executor->terminateStateOnError(
			state,
			"Breadcrumb error: Bad length",
			"breadcrumb.err");
		goto done;
	}

	(static_cast<ExeStateVex&>(state)).recordBreadcrumb(bc);
done:
	delete [] buf;
}

SFH_DEF_HANDLER(AllocAligned)
{
	/* Arguments note:
	 * TJ, if you want symbolics, use kmc_make_range_symbolic
	 * after calling kmc_alloc_aligned. Making things symbolic here
	 * is actually the *less* efficient way to do it-- pages are 
	 * chopped up to reduce the number of constant array assumptions
	 * passed to the solver. Since symbolics have no assumptions,
	 * that optimization doesn't make sense and will result in 
	 * needless overhead when resolving symbolic reads!
	 *
	 * If you don't want me to break your runtime
	 * (inadvertantly, or otherwise),
	 * I'm going to need test cases that rely on it working properly!
	 *
	 * -AJR */
	SFH_CHK_ARGS(2, "kmc_alloc_aligned");

	ExecutorVex			*exe_vex;
	ConstantExpr			*len;
	uint64_t			len_v;
	std::string			name_str;
	std::vector<MemoryObject*>	new_mos;

	len = dyn_cast<ConstantExpr>(arguments[0]);
	if (len == NULL) {
		exe_vex->terminateStateOnError(
			state,
			"kmc_alloc_aligned error: len not constant.",
			"mrs.err");
		return;
	}

	len_v = len->getZExtValue();
	name_str = sfh->readStringAtAddress(state, arguments[1]);

	/* not requesting a specific address */
	exe_vex  = dynamic_cast<ExecutorVex*>(sfh->executor);
	new_mos = exe_vex->getMM()->allocateAlignedChopped(
		len_v,
		12 /* aligned on 2^12 */,
		target->inst,
		&state);

	if (new_mos.size() == 0) {
		std::cerr << "COULD NOT ALLOCATE ALIGNED?????\n\n";
		std::cerr << "LEN_V = " << len_v << std::endl;
		state.bindLocal(target, ConstantExpr::create(0, 64));
		return;
	}

	state.bindLocal(target, new_mos[0]->getBaseExpr());
	foreach (it, new_mos.begin(), new_mos.end()) {
		state.bindMemObj(*it);
		(*it)->setName(name_str.c_str());
	}
}

void SyscallSFH::removeTail(
	ExecutionState& state,
	const MemoryObject* mo,
	unsigned taken)
{
	ObjectState	*os;
	MemoryObject	*mo_head;
	char		*buf_head;
	uint64_t	mo_addr, mo_size, head_size;

	mo_addr = mo->address;
	mo_size = mo->size;

	assert (mo_size > taken && "Off+Taken out of range");

	/* copy buffer data */
	head_size = mo_size - taken;
	buf_head = new char[head_size];
	state.addressSpace.copyToBuf(mo, buf_head, 0, head_size);
	os = state.addressSpace.findObject(mo);

	/* free object from address space */
	state.unbindObject(mo);

	/* mark head concrete */
	mo_head = exe_vex->memory->allocateFixed(mo_addr, head_size, 0, &state);
	os = state.bindMemObj(mo_head);
	for(unsigned i = 0; i < head_size; i++) state.write8(os, i, buf_head[i]);

	delete [] buf_head;
}

void SyscallSFH::removeHead(
	ExecutionState& state,
	const MemoryObject* mo,
	unsigned taken)
{
	ObjectState	*os;
	MemoryObject	*mo_tail;
	char		*buf_tail;
	uint64_t	mo_addr, mo_size, tail_size;

	mo_addr = mo->address;
	mo_size = mo->size;

	if (mo_size == taken) {
		state.unbindObject(mo);
		return;
	}

	assert (mo_size > taken && "Off+Taken out of range");

	/* copy buffer data */
	tail_size = mo_size - taken;
	buf_tail = new char[tail_size];
	state.addressSpace.copyToBuf(mo, buf_tail, taken, tail_size);
	os = state.addressSpace.findObject(mo);

	/* free object from address space */
	state.unbindObject(mo);

	/* create tail */
	mo_tail = exe_vex->memory->allocateFixed(
		mo_addr+taken, tail_size, 0, &state);
	os = state.bindMemObj(mo_tail);
	for(unsigned i = 0; i < tail_size; i++) state.write8(os, i, buf_tail[i]);

	delete [] buf_tail;
}

void SyscallSFH::removeMiddle(
	ExecutionState& state,
	const MemoryObject* mo,
	unsigned mo_off,
	unsigned taken)
{
	ObjectState	*os;
	MemoryObject	*mo_head, *mo_tail;
	char		*buf_head, *buf_tail;
	uint64_t	mo_addr, mo_size, tail_size;

	mo_addr = mo->address;
	mo_size = mo->size;
	assert (mo_size > (mo_off+taken) && "Off+Taken out of range");

	/* copy buffer data */
	buf_head = new char[mo_off];
	tail_size = mo_size - (mo_off + taken);
	buf_tail = new char[tail_size];
	state.addressSpace.copyToBuf(mo, buf_head, 0, mo_off);
	state.addressSpace.copyToBuf(mo, buf_tail, mo_off+taken, tail_size);
	os = state.addressSpace.findObject(mo);

	/* free object from address space */
	state.unbindObject(mo);

	mo_head = exe_vex->memory->allocateFixed(mo_addr, mo_off, NULL, &state);

	os = state.bindMemObj(mo_head);
	for(unsigned i = 0; i < mo_off; i++) state.write8(os, i, buf_head[i]);

	mo_tail = exe_vex->memory->allocateFixed(
		mo_addr+mo_off+taken, tail_size, 0, &state);
	os = state.bindMemObj(mo_tail);
	for(unsigned i = 0; i < tail_size; i++) state.write8(os, i, buf_tail[i]);

	delete [] buf_head;
	delete [] buf_tail;
}

void SyscallSFH::makeRangeSymbolic(
	ExecutionState& state,
	void* addr,
	unsigned sz,
	const char* name)
{
	uint64_t	cur_addr;
	unsigned	total_sz;

	cur_addr = (uint64_t)addr;
	total_sz = 0;
	/* handle disjoint addresses */
	while (total_sz < sz) {
		const MemoryObject	*mo;
		unsigned int		mo_off;
		unsigned int		tail_take_bytes;
		unsigned int		take_remaining;
		unsigned int		taken;

		mo = state.addressSpace.resolveOneMO(cur_addr);
		if (mo == NULL) {
			state.addressSpace.print(std::cerr);
			fprintf(stderr,
				"couldn't find %p in range %p-%p (state=%p)\n",
				(void*)cur_addr,
				addr, ((char*)addr)+sz,
				(void*)&state);
			assert ("TODO: Allocate memory");
		}

		assert (mo->address <= cur_addr && "BAD SEARCH?");
		mo_off = cur_addr - mo->address;
		assert (mo_off < mo->size && "Out of range of MO??");

		take_remaining = sz - total_sz;
		tail_take_bytes = mo->size - mo_off;
		if (mo_off > 0) {
			if (tail_take_bytes <= take_remaining) {
				/* take is excess of length of MO
				 * Chop off all the tail of the MO */
				taken = tail_take_bytes;
				removeTail(state, mo, taken);
			} else {
				taken = take_remaining;
				removeMiddle(state, mo, mo_off, taken);
			}
		} else {
			taken = (take_remaining >= tail_take_bytes) ?
					tail_take_bytes :
					take_remaining;
			removeHead(state, mo, taken);
		}

		/* set stat structure as symbolic */
		cur_addr += taken;
		total_sz += taken;
	}

	MemoryObject	*sym_mo;
	ObjectState	*sym_os;
	sym_mo = exe_vex->memory->allocateFixed((uint64_t)addr, sz, 0, &state);
	sym_mo->setName(name);
	sym_os = exe_vex->executeMakeSymbolic(
		state,
		sym_mo,
		ConstantExpr::alloc(sz, 32),
		name);
}

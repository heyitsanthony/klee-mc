#include "klee/Expr.h"
#include "ExecutorVex.h"
#include "ExeStateVex.h"
#include "SyscallSFH.h"
#include "guestcpustate.h"
#include "klee/breadcrumb.h"
#include <sys/syscall.h>
#include <llvm/Support/CommandLine.h>
#include "static/Sugar.h"
#include <sys/stat.h>

extern "C"
{
#include "valgrind/libvex_guest_amd64.h"
#include "valgrind/libvex_guest_arm.h"
}


extern bool DenySysFiles;

using namespace klee;

static const unsigned int NUM_HANDLERS = 8;
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
	add("kmc_io", IO, true),
	add("kmc_free_run", FreeRun, false),
	addDNR("kmc_exit", KMCExit),
	add("kmc_make_range_symbolic", MakeRangeSymbolic, false),
	add("kmc_alloc_aligned", AllocAligned, true),
	add("kmc_breadcrumb", Breadcrumb, false)
#undef addDNR
#undef add
};

static void copyIntoObjState(
	ExecutionState& state,
	ObjectState* os,
	ref<Expr>* buf,
	unsigned int off,
	unsigned int len)
{
	for (unsigned i = 0; i < len; i++) {
		const ConstantExpr	*ce;
		ce = dyn_cast<ConstantExpr>(buf[i]);
		if (ce != NULL) {
			uint8_t	v = ce->getZExtValue();
			state.write8(os, i+off, v);
		} else {
			state.write(os, i+off, buf[i]);
		}
	}
}


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
	const ObjectState	*old_regctx_os;
	unsigned int		sz;

	ExecutorVex	*exe_vex = dynamic_cast<ExecutorVex*>(sfh->executor);
	SFH_CHK_ARGS(1, "kmc_sc_regs");

	gs = exe_vex->getGuest();
	sz = gs->getCPUState()->getStateSize();

	old_regctx_os = GETREGOBJRO(state);
	assert (old_regctx_os && "No register context ever set?");

	// do not unbind the object, we need to be able to grab it
	// for replay
	// XXX: this should probably be unbound
	// state.unbindObject(old_mo);

	/* 1. make all of symbolic */
	cpu_mo = exe_vex->allocRegCtx(&state);
	state_regctx_os = exe_vex->executeMakeSymbolic(state, cpu_mo, "reg");

	/* 2. set everything that should be initialized */
	for (unsigned int i=0; i < sz; i++) {
		unsigned int	reg_idx;

		reg_idx = i/8;
		if (gs->getArch() == Arch::X86_64) {
			if (	reg_idx == offsetof(VexGuestAMD64State, guest_RAX)/8 ||
				reg_idx == offsetof(VexGuestAMD64State, guest_RCX)/8 ||
				reg_idx == offsetof(VexGuestAMD64State, guest_R11)/8)
			{
				/* ignore rax, rcx, r11 */
				assert (state_regctx_os->isByteConcrete(i) == false);
				continue;
			}
		} else if (gs->getArch() == Arch::ARM) {
			if (reg_idx == offsetof(VexGuestARMState, guest_R0)/8) {
				/* ignore r0 and r1 */
				assert (state_regctx_os->isByteConcrete(i) == false);
				continue;
			}
		} else if (gs->getArch() == Arch::I386) {
			/* EAX = offset 0 in Vex struct */
			if (i < 4) {
				assert (state_regctx_os->isByteConcrete(i) == false);
				continue;
			}
		} else {
			assert (0 == 1);
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


static uint64_t arg2u64(const ref<Expr>& ref)
{
	const ConstantExpr	*ce;

	ce = dyn_cast<ConstantExpr>(ref);
	if (ce == NULL)
		return ~0ULL;

	return ce->getZExtValue();
}
static vfd_t arg2vfd(const ref<Expr>& ref) { return arg2u64(ref); }

static ssize_t do_pread(
	ExecutionState& state,
	int fd, uint64_t buf_base, size_t count, off_t offset)
{
	char	*buf;
	size_t	br;

	buf = new char[4096];
	br = 0;
	while (br < count) {
		ssize_t	ret, to_read;

		to_read = ((br + 4096) > count) ? count - br : 4096;
		ret = pread(fd, buf, to_read, br + offset);
		if (ret == -1) {
			br = -1;
			break;
		}

		state.addressSpace.copyOutBuf(buf_base + br, buf, ret);
		br += ret;

		if (ret < to_read)
			break;
	}
	delete [] buf;

	return br;
}


SFH_DEF_HANDLER(IO)
{
	SyscallSFH		*sc_sfh = static_cast<SyscallSFH*>(sfh);
	const ConstantExpr	*ce_sysnr;
	int			sysnr;

	SFH_CHK_ARGS(5, "kmc_io");

	ce_sysnr = dyn_cast<ConstantExpr>(arguments[0]);
	assert (ce_sysnr);
	sysnr = ce_sysnr->getZExtValue();

	switch (sysnr) {
	case SYS_open: {
		/* expects a path */
		std::string path(sfh->readStringAtAddress(state, arguments[1]));
		vfd_t	vfd;

		if (	DenySysFiles &&
			path.size() > 2 &&
			path[0] == '/' && (path[1] == 'l' || path[1] == 'u'))
		{
			std::cerr << "DENIED '" << path << "'\n";
			state.bindLocal(target, ConstantExpr::create(-1, 64));
			break;
		}

		vfd = sc_sfh->vfds.addPath(path);
		if (vfd != ~0ULL) {
			int base_fd = sc_sfh->vfds.xlateVFD(vfd);
			if (base_fd == -1)
				vfd = ~0ULL;
		}

		std::cerr << "OPENED '" << path << "'. VFD=" << vfd << '\n';
		state.bindLocal(target, ConstantExpr::create(vfd, 64));
		break;
	}
	case SYS_close: {
		vfd_t			vfd;

		vfd = arg2vfd(arguments[1]);
		if (vfd == ~0ULL) {
			state.bindLocal(target, ConstantExpr::create(-1, 64));
			break;
		}

		sc_sfh->vfds.close(vfd);
		state.bindLocal(target, ConstantExpr::create(0, 64));
		break;
	}

	case SYS_pread64: {
		int		fd;
		ssize_t		ret;
		uint64_t	buf_base;
		size_t		count;
		off_t		offset;

		fd = sc_sfh->vfds.xlateVFD(arg2vfd(arguments[1]));
		buf_base = arg2u64(arguments[2]);
		count = arg2u64(arguments[3]);
		offset = arg2u64(arguments[4]);

		if (	fd == -1 || buf_base == ~0ULL ||
			count == ~0ULL || offset == -1)
		{
			state.bindLocal(target, ConstantExpr::create(-1, 64));
			break;
		}

		ret = do_pread(state, fd, buf_base, count, offset);
		state.bindLocal(target, ConstantExpr::create(ret, 64));
		break;
	}

	case SYS_fstat: {
		struct stat	s;
		int		fd;
		int		rc;
		unsigned int	bw;

		fd = sc_sfh->vfds.xlateVFD(arg2vfd(arguments[1]));
		if (fd == -1) {
			state.bindLocal(target, ConstantExpr::create(-1, 64));
			break;
		}

		rc = fstat(fd, &s);
		if (rc == -1) {
			state.bindLocal(target, ConstantExpr::create(-1, 64));
			break;
		}

		bw = state.addressSpace.copyOutBuf(
			arg2u64(arguments[2]),
			(const char*)&s,
			sizeof(struct stat));

		state.bindLocal(
			target,
			ConstantExpr::create(
				(bw == sizeof(struct stat)) ? 0 : -1, 64));
		break;
	}

	}
}

SFH_DEF_HANDLER(SCBad)
{
	SFH_CHK_ARGS(1, "kmc_sc_bad");

	ConstantExpr	*ce;
	ce = dyn_cast<ConstantExpr>(arguments[0]);
	std::cerr << "OOPS calling kmc_sc_bad: " << ce->getZExtValue() << std::endl;
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

	fprintf(stderr, "MAKE RANGE SYMBOLIC %s %p--%p (%d)\n",
		name_str.c_str(),
		(void*)addr_v, (void*)(addr_v + len_v),
		(int)len_v);
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
	if (buf == NULL) {
		sfh->executor->terminateStateOnError(
			state,
			"Breadcrumb error: Symbolic breadcrumb frame",
			"breadcrumb.err");
		return;
	}

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
	 * If you want symbolics, use kmc_make_range_symbolic
	 * after calling kmc_alloc_aligned. Making things symbolic here
	 * is actually the *less* efficient way to do it-- pages are
	 * chopped up to reduce the number of constant array assumptions
	 * passed to the solver. Since symbolics have no assumptions,
	 * that optimization doesn't make sense and will result in
	 * needless overhead when resolving symbolic reads!
	 *
	 * -AJR */
	SFH_CHK_ARGS(2, "kmc_alloc_aligned");

	ExecutorVex			*exe_vex;
	ConstantExpr			*len;
	uint64_t			len_v;
	std::string			name_str;
	std::vector<ObjectPair>		new_op;

	len = dyn_cast<ConstantExpr>(arguments[0]);
	exe_vex = dynamic_cast<ExecutorVex*>(sfh->executor);
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
	new_op = state.allocateAlignedChopped(
		len_v,
		12 /* aligned on 2^12 */,
		target->getInst());

	if (new_op.size() == 0) {
		std::cerr << "COULD NOT ALLOCATE ALIGNED?????\n\n";
		std::cerr << "LEN_V = " << len_v << std::endl;
		state.bindLocal(target, ConstantExpr::create(0, 64));
		return;
	}

	state.bindLocal(target, op_mo(new_op[0])->getBaseExpr());
	foreach (it, new_op.begin(), new_op.end()) {
		const_cast<MemoryObject*>(op_mo((*it)))->setName(
			name_str.c_str());
	}
}

static ObjectState* ALLOC_AT_WR(ExecutionState& s, uint64_t addr, uint64_t sz)
{
	const ObjectState	*os_c;
	const MemoryObject	*mo;

	os_c = s.allocateAt(addr, sz, NULL);
	mo = s.addressSpace.resolveOneMO(addr);

	return s.addressSpace.getWriteable(mo, os_c);
}

void SyscallSFH::removeTail(
	ExecutionState& state,
	const MemoryObject* mo,
	unsigned taken)
{
	ObjectState	*os;
	ref<Expr>	*buf_head;
	uint64_t	mo_addr, mo_size, head_size;

	mo_addr = mo->address;
	mo_size = mo->size;

	assert (mo_size > taken && "Off+Taken out of range");

	/* copy buffer data */
	head_size = mo_size - taken;
	buf_head = new ref<Expr>[head_size];
	state.addressSpace.copyToExprBuf(mo, buf_head, 0, head_size);

	/* free object from address space */
	state.unbindObject(mo);

	/* mark head concrete */
	os = ALLOC_AT_WR(state, mo_addr, head_size);
	copyIntoObjState(state, os, buf_head, 0, head_size);

	delete [] buf_head;
}

void SyscallSFH::removeHead(
	ExecutionState& state,
	const MemoryObject* mo,
	unsigned taken)
{
	ObjectState	*os;
	ref<Expr>	*buf_tail;
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
	buf_tail = new ref<Expr>[tail_size];
	state.addressSpace.copyToExprBuf(mo, buf_tail, taken, tail_size);

	/* free object from address space */
	state.unbindObject(mo);

	/* create tail */
	os = ALLOC_AT_WR(state, mo_addr+taken, tail_size);
	copyIntoObjState(state, os, buf_tail, 0, tail_size);

	delete [] buf_tail;
}

void SyscallSFH::removeMiddle(
	ExecutionState& state,
	const MemoryObject* mo,
	unsigned mo_off,
	unsigned taken)
{
	ObjectState	*os;
	ref<Expr>	*buf_head, *buf_tail;
	uint64_t	mo_addr, mo_size, tail_size;

	mo_addr = mo->address;
	mo_size = mo->size;
	assert (mo_size > (mo_off+taken) && "Off+Taken out of range");

	/* copy buffer data */
	buf_head = new ref<Expr>[mo_off];
	tail_size = mo_size - (mo_off + taken);
	buf_tail = new ref<Expr>[tail_size];
	state.addressSpace.copyToExprBuf(mo, buf_head, 0, mo_off);
	state.addressSpace.copyToExprBuf(mo, buf_tail, mo_off+taken, tail_size);

	/* free object from address space */
	state.unbindObject(mo);

	os = ALLOC_AT_WR(state, mo_addr, mo_off);
	copyIntoObjState(state, os, buf_head, 0, mo_off);

	os = ALLOC_AT_WR(state, mo_addr+mo_off+taken, tail_size);
	copyIntoObjState(state, os, buf_tail, 0, tail_size);

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

	if (sz == 0)
		return;

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

	/* finally, allocate entire symbolic length */
	MemoryObject	*sym_mo;
	ObjectState	*sym_os;
	sym_mo = exe_vex->memory->allocateAt(state, (uint64_t)addr, sz, 0);
	sym_mo->setName(name);
	sym_os = exe_vex->executeMakeSymbolic(
		state,
		sym_mo,
		ConstantExpr::alloc(sz, 32),
		name);
}

#include <llvm/Support/CommandLine.h>
#include <sys/stat.h>

#include "../../lib/Core/MemoryManager.h"
#include "../../lib/Core/MMU.h"
#include "klee/Expr.h"
#include "ExecutorVex.h"
#include "ExeStateVex.h"
#include "SyscallSFH.h"
#include "guestcpustate.h"
#include "klee/breadcrumb.h"
#include <sys/syscall.h>
#include <unistd.h>
#include "static/Sugar.h"

extern "C"
{
#include "valgrind/libvex_guest_amd64.h"
#include "valgrind/libvex_guest_x86.h"
#include "valgrind/libvex_guest_arm.h"
}


extern bool DenySysFiles;

using namespace klee;

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

static bool isSymRegByte(Arch::Arch a, int i)
{
	if (a == Arch::X86_64) {
		if (i/8	== offsetof(VexGuestAMD64State, guest_RAX)/8 ||
			i/8  == offsetof(VexGuestAMD64State, guest_RCX)/8 ||
			i/8 == offsetof(VexGuestAMD64State, guest_R11)/8)
		{
			/* ignore rax, rcx, r11 */
			return true;
		}
		return false;
	}

	if (a == Arch::ARM) {
		if (i/8 == offsetof(VexGuestARMState, guest_R0)/8) {
			/* ignore r0 and r1 */
			return true;
		}
		return false;
	}

	if (a == Arch::I386) {
		/* leave eax and rdx alone */
		if (i/4 == offsetof(VexGuestX86State, guest_EAX)/4)
		// NOTE: some stupid shit depends on EDX being preserved. Ugh.
		//    || i/4 == offsetof(VexGuestX86State, guest_EDX)/4)
		{
		    	return true;
		}
		return false;
	}

	assert (0 == 1);
	return false;
}

SFH_DEF_ALL(SCRegs, "kmc_sc_regs", true)
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
	state_regctx_os = exe_vex->makeSymbolic(state, cpu_mo, "reg");
	if (state_regctx_os == NULL) {
		std::cerr << "[ExeVex] Could not mark regs symbolic\n";
		return;
	}

	/* 2. set everything that should be initialized */
	for (unsigned int i=0; i < sz; i++) {
		if (isSymRegByte(gs->getArch(), i)) {
			/* this can be overridden by
			 * IVC and constraint seeding... */
			// assert (state_regctx_os->isByteConcrete(i) == false);
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

	state.bindLocal(target, MK_CONST(cpu_mo->address, 64));
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

static bool isSysPath(std::string& path)
{
	return (path.size() > 2 &&
		path[0] == '/' && (path[1] == 'l' || path[1] == 'u'));
}


SFH_DEF_ALL(IO, "kmc_io", true)
{
	SyscallSFH		*sc_sfh = static_cast<SyscallSFH*>(sfh);
	const ConstantExpr	*ce_sysnr;
	int			sysnr;

	SFH_CHK_ARGS(5, "kmc_io");

	ce_sysnr = dyn_cast<ConstantExpr>(args[0]);
	assert (ce_sysnr);
	sysnr = ce_sysnr->getZExtValue();

	switch (sysnr) {
	case SYS_mmap: {
		MemoryObject	*mo;
		vfd_t		vfd;
		uint64_t	addr, len, off;
		std::string	path;

		/* (addr, fd, len, off) */
		addr = arg2u64(args[1]);
		vfd = arg2u64(args[2]);
		len = arg2u64(args[3]);
		off = arg2u64(args[4]);

		/* don't mark if symbolic length */
		if (len == ~0ULL)
			break;

		path = sc_sfh->vfds.getPath(vfd);
		if (path.empty())
			break;
		
		/* mark [addr, addr+len) with path from fd */
		mo = NULL;
		for (uint64_t cur_off = 0; cur_off < len; cur_off += mo->size) {
			mo = const_cast<MemoryObject*>(
				state.addressSpace.resolveOneMO(
					addr+cur_off));
			if (mo == NULL)
				break;

			mo->setName(path);
		}

		break;
	}

	case SYS_readlink: {
		std::string path(sfh->readStringAtAddress(state, args[1]));
		char		rl[4096];
		int		rl_n, out_len;
		uint64_t	guest_addr, guest_sz;
	
		guest_addr = arg2u64(args[2]);
		guest_sz = arg2u64(args[3]);
		if (guest_addr == ~0ULL || guest_sz > 4096) {
			state.bindLocal(target, MK_CONST(-1, 64));
			break;
		}


		rl_n = readlink(path.c_str(), rl, 4095);
		if (rl_n == -1) {
			state.bindLocal(target, MK_CONST(-1, 64));
			break;
		}

		out_len = guest_sz;
		if (rl_n < out_len) out_len = rl_n;

		state.addressSpace.copyOutBuf(guest_addr, rl, out_len);
		state.bindLocal(target, MK_CONST(out_len, 64));
		break;
	}

	case SYS_open: {
		/* expects a path */
		std::string path(sfh->readStringAtAddress(state, args[1]));
		vfd_t	vfd;

		if (DenySysFiles && isSysPath(path)) {
			std::cerr << "[kmc-io] DENIED '" << path << "'\n";
			state.bindLocal(target, MK_CONST(-1, 64));
			break;
		}

		vfd = sc_sfh->vfds.addPath(path);
		if (vfd != ~0ULL) {
			int base_fd = sc_sfh->vfds.xlateVFD(vfd);
			if (base_fd == -1) {
				std::cerr << "[VFD] Xlated as fd=-1\n";
				vfd = ~0ULL;
			}
		}

		std::cerr << "[kmc-io] OPENED '"
			<< path << "'. VFD=" << (long)vfd << '\n';
		state.bindLocal(target, MK_CONST(vfd, 64));
		break;
	}
	case SYS_close: {
		vfd_t			vfd;

		vfd = arg2vfd(args[1]);
		if (vfd == ~0ULL) {
			state.bindLocal(target, MK_CONST(-1, 64));
			break;
		}

		sc_sfh->vfds.close(vfd);
		state.bindLocal(target, MK_CONST(0, 64));
		break;
	}

	case SYS_pread64: {
		int		fd;
		ssize_t		ret;
		uint64_t	buf_base;
		size_t		count;
		off_t		offset;

		fd = sc_sfh->vfds.xlateVFD(arg2vfd(args[1]));
		buf_base = arg2u64(args[2]);
		count = arg2u64(args[3]);
		offset = arg2u64(args[4]);

		if (	fd == -1 || buf_base == ~0ULL ||
			count == ~0ULL || offset == -1)
		{
			state.bindLocal(target, MK_CONST(-1, 64));
			break;
		}

		ret = do_pread(state, fd, buf_base, count, offset);
		state.bindLocal(target, MK_CONST(ret, 64));
		break;
	}

	case SYS_fstat: {
		struct stat	s;
		int		fd;
		int		rc;
		unsigned int	bw;

		fd = sc_sfh->vfds.xlateVFD(arg2vfd(args[1]));
		if (fd == -1) {
			state.bindLocal(target, MK_CONST(-1, 64));
			break;
		}

		rc = fstat(fd, &s);
		if (rc == -1) {
			state.bindLocal(target, MK_CONST(-1, 64));
			break;
		}

		bw = state.addressSpace.copyOutBuf(
			arg2u64(args[2]),
			(const char*)&s,
			sizeof(struct stat));

		std::cerr << "[kmc-io] fstat fd="  << fd << "\n";
		state.bindLocal(
			target,
			MK_CONST((bw == sizeof(struct stat)) ? 0 : -1, 64));
		break;
	}

	}
}

SFH_DEF_ALL(SCBad, "kmc_sc_bad", false)
{
	SFH_CHK_ARGS(1, "kmc_sc_bad");

	ConstantExpr	*ce;
	ce = dyn_cast<ConstantExpr>(args[0]);
	std::cerr << "OOPS calling kmc_sc_bad: " << ce->getZExtValue() << std::endl;
}

SFH_DEF_ALL(FreeRun, "kmc_free_run", false)
{
	SFH_CHK_ARGS(2, "kmc_free_run");
	ExecutorVex	*exe_vex = dynamic_cast<ExecutorVex*>(sfh->executor);
	ConstantExpr		*addr, *len;
	uint64_t		addr_v, addr_end, cur_addr;
	uint64_t		len_v, len_remaining;

	addr = dyn_cast<ConstantExpr>(args[0]);
	len = dyn_cast<ConstantExpr>(args[1]);
	if (addr == NULL || len == NULL) {
		TERMINATE_ERROR(exe_vex,
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
	while (cur_addr < addr_end && len_remaining) {
		const MemoryObject	*mo;

		mo = state.addressSpace.resolveOneMO(cur_addr);
		if (mo == NULL) {
			if (len_remaining < 4096) break;
			REPORT_ERROR(exe_vex,
				state,
				"munmap warning: munmapping bad address",
				"munmap.err");
			break;
		}

		if (mo->size > len_remaining) {
			std::cerr
				<< "size mismatch on munmap "
				<< mo->size << ">" << len_remaining
				<< std::endl;
			REPORT_ERROR(exe_vex,
				state,
				"munmap warning: size mismatch on munmap",
				"munmap.err");
			return;

		}

		len_remaining -= mo->size;
		cur_addr += mo->size;

		state.unbindObject(mo);
	}
}

SFH_DEF_ALL_EX(KMCExit, "kmc_exit", true, false, false)
{
	ConstantExpr	*ce;
	SFH_CHK_ARGS(1, "kmc_exit");

	ce = dyn_cast<ConstantExpr>(args[0]);
	if (ce)  fprintf(stderr, "exitcode=%lu\n", ce->getZExtValue());
	else fprintf(stderr, "exitcode=?\n");

	TERMINATE_EXIT(sfh->executor, state);
}

SFH_DEF_ALL(MakeRangeSymbolic, "kmc_make_range_symbolic", false)
{
	SFH_CHK_ARGS(3, "kmc_make_range_symbolic");
	SyscallSFH	*sc_sfh = static_cast<SyscallSFH*>(sfh);
	ExecutorVex	*exe_vex = static_cast<ExecutorVex*>(sc_sfh->executor);
	ConstantExpr	*addr, *len;
	uint64_t	addr_v, len_v;
	std::string	name_str;

	assert (sfh != NULL);
	assert (exe_vex != NULL);

	addr = dyn_cast<ConstantExpr>(args[0]);
	len = dyn_cast<ConstantExpr>(args[1]);
	if (addr == NULL || len == NULL) {
		TERMINATE_ERROR(exe_vex,
			state,
			"makerangesymbolic error: Addr or len not CE exprs. Smash runtime",
			"mrs.err");
		return;
	}

	addr_v = addr->getZExtValue();
	len_v = len->getZExtValue();
	name_str = sfh->readStringAtAddress(state, args[2]);

	fprintf(stderr, "MAKE RANGE SYMBOLIC %s %p--%p (%d)\n",
		name_str.c_str(),
		(void*)addr_v, (void*)(addr_v + len_v),
		(int)len_v);
	sc_sfh->makeRangeSymbolic(state, (void*)addr_v, len_v, name_str.c_str());

	exe_vex->getMMU()->signal(state, (void*)addr_v, len_v);
}

SFH_DEF_ALL(Breadcrumb, "kmc_breadcrumb", false)
{
	SFH_CHK_ARGS(2, "kmc_breadcrumb");
	ConstantExpr		*len_expected_ce;
	struct breadcrumb	*bc;
	unsigned char		*buf;
	unsigned int		len_in, len_expected;

	len_expected_ce = dyn_cast<ConstantExpr>(args[1]);
	len_expected = (unsigned int)len_expected_ce->getZExtValue();
	buf = sfh->readBytesAtAddress(state, args[0], len_expected, len_in, -1);
	if (buf == NULL) {
		TERMINATE_ERROR(
			sfh->executor,
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
		TERMINATE_ERROR(sfh->executor,
			state,
			"Breadcrumb error: Bad length",
			"breadcrumb.err");
		goto done;
	}

	(static_cast<ExeStateVex&>(state)).recordBreadcrumb(bc);
done:
	delete [] buf;
}

SFH_DEF_ALL(AllocAligned, "kmc_alloc_aligned", true)
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

	len = dyn_cast<ConstantExpr>(args[0]);
	exe_vex = dynamic_cast<ExecutorVex*>(sfh->executor);
	if (len == NULL) {
		TERMINATE_ERROR(exe_vex,
			state,
			"kmc_alloc_aligned error: len not constant.",
			"mrs.err");
		return;
	}

	len_v = len->getZExtValue();
	name_str = sfh->readStringAtAddress(state, args[1]);

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
	MemoryObject	*sym_mo;
	ObjectState	*sym_os;
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
			fprintf(stderr,
				"couldn't find %p in user range %p-%p\n",
				(void*)cur_addr,
				addr, ((char*)addr)+sz);
			TERMINATE_ERROR(executor,
				state,
				"Tried to make non-allocated memory symbolic",
				"symoob.err");
			return;
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
			taken = (take_remaining >= tail_take_bytes)
				? tail_take_bytes
				: take_remaining;
			removeHead(state, mo, taken);
		}

		/* set stat structure as symbolic */
		cur_addr += taken;
		total_sz += taken;
	}

	/* finally, allocate entire symbolic length */
	sym_mo = exe_vex->memory->allocateAt(state, (uint64_t)addr, sz, 0);
	sym_mo->setName(name);
	sym_os = exe_vex->makeSymbolic(state, sym_mo, name);
}

SFH_DEF_ALL(RegsGet, "kmc_regs_get", true)
{ state.bindLocal(target, es2esv(state).getRegCtx()->getBaseExpr()); }


static const SpecialFunctionHandler::HandlerInfo *hInfo[] =
{
#define add(h)  &Handler##h::hinfo
	add(AllocAligned), add(Breadcrumb), add(FreeRun), add(IO),
	add(MakeRangeSymbolic), add(SCRegs), add(SCBad), add(RegsGet),
	add(KMCExit)
#undef addDNR
#undef add
};

void SyscallSFH::prepare(void)
{
	SpecialFunctionHandler::prepare();
	SpecialFunctionHandler::prepare(hInfo);
}

void SyscallSFH::bind(void)
{
	SpecialFunctionHandler::bind();
	SpecialFunctionHandler::bind(hInfo);
}

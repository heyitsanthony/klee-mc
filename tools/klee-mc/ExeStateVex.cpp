#include <string.h>
#include "ExeStateVex.h"
#include "klee/breadcrumb.h"
#include "guest.h"
#include "guestcpustate.h"

using namespace klee;

Guest* ExeStateVex::base_guest = NULL;
uint64_t ExeStateVex::base_stack = 0;


ExeStateVex::ExeStateVex(const ExeStateVex& src)
: ExecutionState(src)
, syscall_c(0)
, last_syscall_inst(src.last_syscall_inst)
{
	const ExeStateVex	*esv;
	
	esv = dynamic_cast<const ExeStateVex*>(&src);
	assert (esv != NULL && "Copying non-ESV?");

	bc_log = esv->bc_log;
	reg_mo = esv->reg_mo;
}

void ExeStateVex::recordBreadcrumb(const struct breadcrumb* bc)
{
	bc_log.push_back(std::vector<unsigned char>(
		(const unsigned char*)bc,
		((const unsigned char*)bc) + bc->bc_sz));
}

ObjectState* ExeStateVex::getRegObj()
{ return addressSpace.findWriteableObject(reg_mo); }

const ObjectState* ExeStateVex::getRegObjRO() const
{ return addressSpace.findObject(reg_mo); }

void ExeStateVex::getGDBRegs(
	std::vector<uint8_t>& v,
	std::vector<bool>& is_conc) const
{
	const ObjectState	*reg_os;
	int			cpu_off, gdb_off;
	GuestCPUState		*cpu;

	reg_os = getRegObjRO();
	gdb_off = 0;
	cpu = base_guest->getCPUState();
	while ((cpu_off = cpu->cpu2gdb(gdb_off)) != -1) {
		gdb_off++;
		if (cpu_off < 0 || reg_os->isByteConcrete(cpu_off) == false) {
			v.push_back(0);
			is_conc.push_back(false);
			continue;
		}

		v.push_back(reg_os->read8c(cpu_off));
		is_conc.push_back(true);
	}
}

void ExeStateVex::setAddrPC(uint64_t addr)
{
	ObjectState	*reg_os;
	unsigned	off;

	reg_os = getRegObj();
	off = base_guest->getCPUState()->getPCOff();
	reg_os->write(
		off,
		MK_CONST(addr,
		(base_guest->getMem()->is32Bit() ? 32 : 64)));
}

uint64_t ExeStateVex::getAddrPC(void) const
{
	const ObjectState	*reg_os;
	uint64_t		ret;
	unsigned		off;

	reg_os = getRegObjRO();
	assert (base_guest->getArch() == Arch::X86_64 && "??? ARCH ???");

	off = base_guest->getCPUState()->getPCOff();
	ret = 0;
	for (unsigned i = 0; i < 8; i++) {
		ret |= ((uint64_t)reg_os->read8c(off+i)) << (i*8);
	}

	return ret;
}

unsigned ExeStateVex::getStackDepth(void) const
{
	const ObjectState	*reg_os;
	unsigned		off, w;
	ref<Expr>		stk_expr;
	uint64_t		cur_stack;

	off = base_guest->getCPUState()->getStackRegOff();
	w = (base_guest->getMem()->is32Bit()) ? 32 : 64;

	reg_os = getRegObjRO();
	cur_stack = 0;

	stk_expr = read(reg_os, off, w);
	if (stk_expr->getKind() != Expr::Constant)
		goto symstack;

	cur_stack = cast<ConstantExpr>(stk_expr)->getZExtValue();
	if (base_stack < cur_stack) {
	//	std::cerr << "[ExeStateVex] base="
	//		<< (void*)base_stack.o <<  " < "
	//		<< (void*)cur_stack << "=cur_stack\n";
		return INVALID_STACK;
	}

	return base_stack - cur_stack;

symstack:
	std::cerr << "[ExeStateVex] Symbolic stackptr?\n";
	//std::cerr <<  "Expr=" << reg_os->read8(off+i) << "\n";
	return INVALID_STACK;
}


void ExeStateVex::inheritControl(ExecutionState& es)
{
	ExeStateVex	*esv;
	ObjectState	*reg_os(getRegObj());
	unsigned	stkptr_off;

	assert (base_guest->getArch() == Arch::X86_64 && "??? ARCH ???");

	esv = static_cast<ExeStateVex*>(&es);
	stkptr_off = base_guest->getCPUState()->getStackRegOff();
	reg_os->write(stkptr_off, esv->getRegObjRO()->read(stkptr_off, 64));

	ExecutionState::inheritControl(es);
}

/* copy concrete parts into guest regs. */
void ExeStateVex::updateGuestRegs(void)
{
	void	*guest_regs;
	guest_regs = base_guest->getCPUState()->getStateData();
	addressSpace.copyToBuf(getRegCtx(), guest_regs);
}

void ExeStateVex::logXferRegisters()
{
	updateGuestRegs();
	logXferObj(getRegObjRO(),  BC_TYPE_VEXREG);
}

void ExeStateVex::logError(const char* msg, const char* suff)
{
	uint8_t			*crumb_buf, *crumb_base;
	unsigned		sz;
	struct breadcrumb	*bc;

	sz = strlen(msg) + strlen(suff) + 2;
	crumb_base = new uint8_t[sizeof(struct breadcrumb)+sz];
	crumb_buf = crumb_base;
	bc = reinterpret_cast<struct breadcrumb*>(crumb_base);

	bc_mkhdr(bc, BC_TYPE_ERREXIT, 0, sz);
	crumb_buf += sizeof(struct breadcrumb);

	strcpy((char*)crumb_buf, msg);
	crumb_buf += strlen(msg) + 1;
	strcpy((char*)crumb_buf, suff);

	recordBreadcrumb(bc);
	delete [] crumb_base;
}


/* XXX: expensive-- lots of storage */
void ExeStateVex::logXferObj(const ObjectState* os, int tag, unsigned off)
{
	uint8_t			*crumb_buf, *crumb_base;
	unsigned		sz;
	struct breadcrumb	*bc;

	updateGuestRegs();

	assert (off < os->getSize());
	sz = os->getSize() - off;
	crumb_base = new uint8_t[sizeof(struct breadcrumb)+(sz*2)];
	crumb_buf = crumb_base;
	bc = reinterpret_cast<struct breadcrumb*>(crumb_base);

	bc_mkhdr(bc, tag, 0, sz*2);
	crumb_buf += sizeof(struct breadcrumb);

	/* 1. store concrete cache */
	os->readConcrete(crumb_buf, sz, off);
	crumb_buf += sz;

	/* 2. store concrete mask */
	for (unsigned int i = 0; i < sz; i++)
		crumb_buf[i] = (os->isByteConcrete(i+off)) ? 0xff : 0;

	recordBreadcrumb(bc);
	delete [] crumb_base;
}

void ExeStateVex::logXferMO(uint64_t log_obj_addr)
{
	ObjectPair		op;
	uint8_t			*crumb_buf, *crumb_base;
	unsigned		sz;
	struct breadcrumb	*bc;

	if (addressSpace.resolveOne(log_obj_addr, op) == false)
		return;

	sz = op_os(op)->getSize();
	crumb_base = new uint8_t[sizeof(struct breadcrumb)+(sz*2)+8];
	crumb_buf = crumb_base;
	bc = reinterpret_cast<struct breadcrumb*>(crumb_base);

	bc_mkhdr(bc, BC_TYPE_MEMLOG, 0, sz*2+8);
	crumb_buf += sizeof(struct breadcrumb);

	*((uint64_t*)crumb_buf) = op_mo(op)->address;
	crumb_buf += sizeof(uint64_t);

	/* 1. store concrete cache */
	op_os(op)->readConcrete(crumb_buf, sz);
	crumb_buf += sz;

	/* 2. store concrete mask */
	for (unsigned int i = 0; i < sz; i++)
		crumb_buf[i] = (op_os(op)->isByteConcrete(i)) ? 0xff : 0;

	recordBreadcrumb(bc);
	delete [] crumb_base;
}


void ExeStateVex::logXferStack()
{
	ObjectPair		op;
	unsigned		off;
	ref<Expr>		stk_expr;
	uint64_t		stack_addr;

	/* do not record stack if stack pointer is symbolic */
	off = base_guest->getCPUState()->getStackRegOff();
	stk_expr = getRegObjRO()->read(off, 64);
	if (stk_expr->getKind() != Expr::Constant)
		return;

	/* do not record stack if backing object can't be found */
	stack_addr = cast<ConstantExpr>(stk_expr)->getZExtValue();
	if (addressSpace.resolveOne(stack_addr, op) == false)
		return;

	/* log stack */
	off = stack_addr - op_mo(op)->address;
	logXferObj(op_os(op),  BC_TYPE_STACKLOG, off);
}

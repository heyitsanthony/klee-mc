#include "ExeStateVex.h"
#include "klee/breadcrumb.h"
#include "guest.h"
#include "guestcpustate.h"

using namespace klee;

Guest* ExeStateVex::base_guest = NULL;
uint64_t ExeStateVex::base_stack = 0;

void ExeStateVex::recordRegisters(const void* reg, int sz)
{
/* XXX */
}

ExeStateVex::ExeStateVex(const ExeStateVex& src)
: ExecutionState(src)
, syscall_c(0)
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

#include "ExeStateVex.h"
#include "klee/breadcrumb.h"
#include "guest.h"
#include "guestcpustate.h"

using namespace klee;

Guest* ExeStateVex::base_guest = NULL;

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
	unsigned	off;
	uint64_t	cur_stack;
	guest_ptr	stack_base;

	off = base_guest->getCPUState()->getStackRegOff();
	stack_base = base_guest->getCPUState()->getStackPtr();

	reg_os = getRegObjRO();
	if (reg_os->isByteConcrete(off) == false) {
		std::cerr << "[ExeStateVex] Symbolic stackptr?\n";
		return 0;
	}

	cur_stack = 0;
	for (unsigned i = 0; i < 8; i++)
		cur_stack |= ((uint64_t)reg_os->read8c(off+i)) << (i*8);

	if (stack_base.o < cur_stack) {
	//	std::cerr << "[ExeStateVex] base="
	//		<< (void*)stack_base.o <<  " < "
	//		<< (void*)cur_stack << "=cur_stack\n";
		return 0;
	}

	return stack_base.o - cur_stack;
}

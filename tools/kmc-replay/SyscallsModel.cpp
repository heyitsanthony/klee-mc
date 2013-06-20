/* two tricks here
 * 	1) JIT the syscall code and do whatever
 * 	2) native reimplement all of the intrinsics. how many functions?
 */

#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/IR/Module.h>

#include "klee/Internal/ADT/KTestStream.h"

#include <sys/mman.h>
#include "vexhelpers.h"
#include "guest.h"
#include "guestmem.h"
#include "guestcpustate.h"
#include "SyscallsKTest.h"
#include "SyscallsModel.h"

#include "klee/klee.h"

using namespace llvm;
using namespace klee;

static SyscallsModel	*intr_model;

SyscallsModel::SyscallsModel(
	const char* model_file,
	KTestStream	*in_kts,
	Guest* in_g)
: Syscalls(in_g)
//, file_recons(NULL)
, m(NULL)
, sysf(NULL)
, kts(in_kts)
{
	m = VexHelpers::loadModFromPath(model_file);
	assert (m != NULL && "Expected model bitcode");

	EngineBuilder	eb(m);
	Function	*llvm_f;
	std::string	err_str;

	eb.setErrorStr(&err_str);
	exe = eb.create();
	if (!exe)
		std::cerr << "Exe Engine Error: " << err_str << '\n';
	assert (exe && "Could not make exe engine");

	llvm_f = m->getFunction("sc_enter");
	assert (llvm_f != NULL && "Could not find sc_enter");

	sysf = (sysfunc_t)exe->getPointerToFunction(llvm_f);

	intr_model = this;
}

SyscallsModel::~SyscallsModel(void)
{
//	delete m;
	delete exe;
	delete kts;
}

uint64_t SyscallsModel::apply(SyscallParams& sp)
{
	unsigned	sys_nr;
	void		*jmp_ptr;
	
	sys_nr = getGuest()->getCPUState()->getSyscallParams().getSyscall();
	jmp_ptr = (void*)getGuest()->getCPUState()->getPC().o;

	if (sys_nr != SYS_klee)
		fprintf(stderr, KREPLAY_NOTE"Applying: sys=%d\n", (int)sys_nr);
	else
		fprintf(stderr, KREPLAY_NOTE"Applying: sys=SYS_klee\n");

	if (setjmp(restore_buf) != 0) {
		assert (0 == 1 && "STUB-- exited?");
	}

	sysf(getGuest()->getCPUState()->getStateData(), jmp_ptr);
	return 0;
}

void SyscallsModel::restoreCtx(void) { longjmp(restore_buf, 1); }

/*******************************************************************/
/* intrinsics */
/*******************************************************************/
/*******************************************************************/
extern "C" {
/* nothing is symbolic */
unsigned klee_is_symbolic(uint64_t n) { return 0; }

void klee_warning_once(const char* msg)
{ fprintf(stderr, "Warning once: %s\n", msg); }

void klee_warning(const char* msg)
{ fprintf(stderr, "Warning: %s\n", msg); }


uint64_t __klee_fork_all(uint64_t v) { return v; }

void* kmc_sc_regs(void* r)
{
	uint8_t	*old_reg, *new_reg;

	old_reg = intr_model->getGuest()->getCPUState()->copyOutStateData();
	new_reg = (uint8_t*)intr_model->getGuest()->getCPUState()->getStateData();

	SyscallsKTest::copyInRegMemObj(
		intr_model->getGuest(),
		intr_model->getKTestStream());

	return new_reg;
}

void klee_assume(uint64_t cond)
{ assert (cond != 0 && "Bad Assume"); }

#define CHK_OP(a,b)	\
	case a:		\
	if (!(x b y)) {	\
		printf("Bad assume %lx " #b " %lx\n", x, y);	\
	}	\
	assert(x b y);	\
	break;

void klee_assume_op(uint64_t x, uint64_t y, uint8_t op)
{
	switch (op) {
	CHK_OP(KLEE_CMP_OP_EQ,==);
	CHK_OP(KLEE_CMP_OP_NE, !=);
	CHK_OP(KLEE_CMP_OP_UGT,>);
	CHK_OP(KLEE_CMP_OP_UGE,>=);
	CHK_OP(KLEE_CMP_OP_ULT,<);
	CHK_OP(KLEE_CMP_OP_ULE,<=);
	CHK_OP(KLEE_CMP_OP_SGT,>);
	CHK_OP(KLEE_CMP_OP_SGE,>=); 
	CHK_OP(KLEE_CMP_OP_SLT,<);
	CHK_OP(KLEE_CMP_OP_SLE, <=);
	default:
		fprintf(stderr, "Unknown assume op %d\n", op);
		assert (0 == 1);
	}
}

void klee_print_expr(const char* msg, ...)
{ fprintf(stderr, "print expr %s\n", msg); }

unsigned klee_sym_range_bytes(void* ptr, unsigned max_bytes) { return 0; }

unsigned klee_is_valid_addr(void* ptr) { assert (0 == 1 && "STUB"); return 0; }
int klee_is_shadowed(uint64_t v) { assert (0 == 1 && "STUB"); return 0; }

uint64_t klee_indirect0(const char* s) { assert (0 == 1 && "STUB"); return 0; }
uint64_t klee_indirect1(const char* s, uint64_t v0)
{ assert (0 == 1 && "STUB"); return 0; }
uint64_t klee_indirect2(const char* s, uint64_t v0, uint64_t v1)
{ assert (0 == 1 && "STUB"); return 0; }
uint64_t klee_indirect3(const char* s, uint64_t v0, uint64_t v1, uint64_t v2)
{ assert (0 == 1 && "STUB"); return 0; }


void klee_report_error(
const char *file,
int line, const char *message, const char *suffix)
{ assert (0 == 1 && "STUB"); }

void kmc_sc_bad(unsigned int) { assert (0 == 1 && "STUB"); }

void klee_check_memory_access(const void *address, size_t size)
{
	volatile uint8_t n = 0;
	for (unsigned i = 0; i < size; i++)
		n += ((char*)address)[i];
}

uint64_t klee_get_value(uint64_t expr) { return expr; }
uint64_t klee_min_value(uint64_t expr) { return expr; }
uint64_t klee_max_value(uint64_t expr) { return expr; }

/* XXX: this should read from the ktest file */
void kmc_make_range_symbolic(uint64_t ptr, uint64_t len, const char* name)
{
	KTestStream	*kts = intr_model->getKTestStream();
	Guest		*gs = intr_model->getGuest();
	char		*buf;

	/* first, grab mem obj */
	buf = kts->feedObjData(len);
	assert (buf != NULL);

	gs->getMem()->memcpy(guest_ptr(ptr), buf, len);

	delete [] buf;
}

void klee_define_fixed_object(void *addr, size_t nbytes)
{
assert(0==1&&"STUB");
}

void* kmc_alloc_aligned(uint64_t sz, const char* name)
{
	Guest		*gs = intr_model->getGuest();
	guest_ptr	p;
	int		ret;

	ret = gs->getMem()->mmap(
		p, guest_ptr(0), sz, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

	if (ret != 0)
		return NULL;

	return (void*)p.o;
}

void kmc_free_run(uint64_t addr, uint64_t num_bytes)
{ assert (0 == 1 && "STUB"); }

void kmc_exit(uint64_t exitcode) { assert (0 == 1 && "STUB"); }

void klee_silent_exit(int status) { assert (0 == 1 && "STUB"); }
void klee_yield(void) {}
void kmc_breadcrumb(void* bc, unsigned int len) {}
long kmc_io(int sys_rn, long p1, long p2, long p3, long p4)
{ assert (0 == 1 && "STUB"); return 0; }
}

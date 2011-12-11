#include <klee/klee.h>
#include <klee/breadcrumb.h>
#include "breadcrumb.h"

void kmc_breadcrumb(struct breadcrumb* bc, unsigned int sz);

#define OPBUF_MAX		8
static struct bc_syscall	bc_sc;
static struct bc_sc_memop	bc_opbuf[OPBUF_MAX];
static int			bc_opbufidx;

/** XXX NEED TO FIX LINKER BUG IN KLEEMC SO WE CAN USE
 * INTRINSIC LIBRARY!! */
static void* memset(void* x, int y, size_t z)
{
	unsigned int i;
	for (i = 0; i < z; i++) {
		((uint8_t*)x)[i] = y;
	}
	return x;
}

static struct bc_sc_memop* next_opbuf(void)
{
	struct bc_sc_memop	*op;

	bc_opbufidx++;
	if (bc_opbufidx >= OPBUF_MAX)
		klee_report_error(
			__FILE__, __LINE__,
			"MemOp Buf Overflow", "sc.err");
	
	op = &bc_opbuf[bc_opbufidx];
	op->sop_hdr.bc_sz = sizeof(struct bc_sc_memop);
	op->sop_hdr.bc_type = BC_TYPE_SCOP;
	op->sop_hdr.bc_type_flags = 0;

	return op;
}

struct bc_syscall* sc_breadcrumb_get(void) { return &bc_sc; }

void sc_breadcrumb_reset(void)
{
	memset(&bc_sc, 0, sizeof(struct breadcrumb));
	bc_sc.bcs_sysnr = ~0; /* for debugging uninit value */
	bc_sc.bcs_xlate_sysnr = ~0;
	bc_sc.bcs_hdr.bc_type = BC_TYPE_SC;
	bc_sc.bcs_hdr.bc_sz = sizeof(struct bc_syscall);
	bc_opbufidx = -1;
}

void sc_breadcrumb_add_ptr(void* ptr, unsigned int sz)
{
	struct bc_sc_memop	*op = next_opbuf();
	op->sop_hdr.bc_type_flags = BC_FL_SCOP_USERPTR;
	op->sop_baseptr.ptr = ptr;
	op->sop_off = 0;
	op->sop_sz = sz;
}

void sc_breadcrumb_add_argptr(
	unsigned int arg, unsigned int off, unsigned int sz)
{
	struct bc_sc_memop	*op = next_opbuf();
	op->sop_hdr.bc_type_flags = BC_FL_SCOP_ARGPTR;
	op->sop_baseptr.ptr_sysarg = arg;
	op->sop_off = off;
	op->sop_sz = sz;
}

void sc_breadcrumb_commit(
	unsigned int sysnr,
	unsigned int xlate_sysnr,
	uint64_t aux_ret)
{
	int i;

	if (bc_sc.bcs_sysnr != (uint32_t)~0) {
		klee_report_error(
			__FILE__, __LINE__,
			"Reusing already-set breadcrumb for commit",
			"sc.err");
	}
	bc_sc.bcs_sysnr = sysnr;
	bc_sc.bcs_xlate_sysnr = xlate_sysnr;
	bc_sc.bcs_op_c = bc_opbufidx+1;
	bc_sc.bcs_ret = aux_ret;

	/* dump all breadcrumbs accumulated during syscall */
	kmc_breadcrumb((void*)&bc_sc, bc_sc.bcs_hdr.bc_sz);
	for (i = 0; i <= bc_opbufidx; i++) {
		kmc_breadcrumb((void*)&bc_opbuf[i], bc_opbuf[i].sop_hdr.bc_sz);
	}
}

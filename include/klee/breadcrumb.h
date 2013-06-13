/**
 * Breadcrumbs are the structure that permit annotated replays.
 * Annotations are useful for:
 * 	* Cross-checking execution (e.g. register matching)
 * 	* Communicating model information (e.g. syscall replay)
 * 	* Notifications
 */
#ifndef KLEE_BREADCRUMB_H
#define KLEE_BREADCRUMB_H

#include <stdint.h>

#define BC_TYPE_BOGUS		0
#define BC_TYPE_SC		1
#define BC_TYPE_VEXREG		2		/* format = hdr+regdump+concrete mask */
#define BC_TYPE_SCOP		3
#define BC_TYPE_STACKLOG	4		/* same format as vexreg */
#define BC_TYPE_MEMLOG		5		/* format = hdr+base+regdump+concrete mask" */
#define BC_TYPE_USER		0xffff0000

#define BC_FL_SC_NEWREGS	0x1
#define BC_FL_SC_THUNK		0x2

#define BC_FL_SCOP_USERPTR	1
#define BC_FL_SCOP_ARGPTR	2

struct breadcrumb
{
	unsigned int	bc_type;
	unsigned int	bc_type_flags;
	unsigned int	bc_sz;		/* e.g. sizeof(bc_syscall), etc */
	/* data follows */
};
#define bc_mkhdr(x,y,z,w)	\
do {							\
	((struct breadcrumb*)(x))->bc_type = y;		\
	((struct breadcrumb*)(x))->bc_type_flags = z;	\
	((struct breadcrumb*)(x))->bc_sz = (w)+sizeof(struct breadcrumb);	\
} while (0)

#define bc_data(x)	(((const unsigned char*)x) + sizeof(struct breadcrumb))

#define bc_sc_is_thunk(x)	(x->bcs_hdr.bc_type_flags & BC_FL_SC_THUNK)
#define bc_sc_is_newregs(x)	(x->bcs_hdr.bc_type_flags & BC_FL_SC_NEWREGS)
#define bc_is_type(x,y)		(((const struct breadcrumb*)x)->bc_type == y)

struct bc_syscall
{
	struct breadcrumb	bcs_hdr;
	uint32_t		bcs_xlate_sysnr;
	uint32_t		bcs_sysnr;
	uint64_t		bcs_ret;	/* retcode */
	uint8_t			bcs_op_c;	/* # of ops that follow */
};

union bc_ptr
{
	void		*ptr;
	unsigned long	ptr_sysarg;
};

struct bc_sc_memop
{
	struct breadcrumb	sop_hdr;
	union bc_ptr		sop_baseptr;
	unsigned int		sop_off;
	unsigned int		sop_sz;
};

#endif

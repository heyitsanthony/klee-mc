#ifndef SC_BREADCRUMB_H
#define SC_BREADCRUMB_H

#include <klee/breadcrumb.h>
#include <stdint.h>

void sc_breadcrumb_reset(void);
void sc_breadcrumb_add_ptr(void* ptr, unsigned int sz);
void sc_breadcrumb_add_argptr(
	unsigned int arg, unsigned int off, unsigned int sz);
void sc_breadcrumb_commit(unsigned int sysnr, uint64_t aux_ret);
void sc_breadcrumb_set_flags(unsigned int);
struct bc_syscall* sc_breadcrumb_get(void);

#define sc_breadcrumb_get_flags() sc_breadcrumb_get()->bcs_hdr.bc_type_flags
#define SC_BREADCRUMB_FL_OR(x)	sc_breadcrumb_get()->bcs_hdr.bc_type_flags |= x

#endif

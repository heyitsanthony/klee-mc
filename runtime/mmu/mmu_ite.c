/**
 * MMU which dispatches large reads with ITEs up to some maximum depth.
 */

#include "klee/klee.h"
#include "mmu_testptr.h"
#include "mmu.h"

#define MAX_ADDRS	16
static uint64_t	addr_buf[MAX_ADDRS];

#define MMU_LOAD(x,y)		\
y mmu_load_##x##_ite(void* addr)	\
{	y		ret;		\
	int		n_addrs, i;	\
	mmu_testptr(addr);		\
	n_addrs = klee_get_values((uint64_t)addr, addr_buf, MAX_ADDRS); \
	klee_print_expr("hey", n_addrs);		\
	ret = *((y*)(addr_buf[0]));			\
	if (n_addrs == 1) {				\
		/* force constraint for IVC */		\
		klee_assume_eq(addr, addr_buf[0]);	\
		return ret; }				\
	for (i = 1; i < n_addrs; i++) {			\
		ret = klee_mk_ite(			\
			klee_mk_eq(addr_buf[i], addr),	\
			*((y*)addr_buf[i]),		\
			ret); }				\
	/* overflow? make disjunction to bound deref */	\
	if (n_addrs == MAX_ADDRS) {			\
		uint64_t	disjunct = 0;		\
		for (i = 0; i < n_addrs; i++) {		\
			disjunct = klee_mk_or(		\
				disjunct,		\
				klee_mk_eq(addr, addr_buf[i])); \
		}					\
		__klee_assume(disjunct);		\
	}						\
	return ret; }				

#define MMU_STORE(x,y)			\
void mmu_store_##x##_ite(void* addr, y v)	\
{ mmu_store_##x##_objwide(addr,v); }

MMU_ACCESS_ALL();
DECL_MMUOPS_S(ite);
#ifndef VIRTSYM_H
#define VIRTSYM_H

#include "virtstr.h"

typedef void(*vsym_clo_f)(uint64_t r, void* aux);
typedef int(*vsym_check_f)(const void* cur, const void* old);

typedef int pruneid_t;
#define VS_PRNID_STRCHR		1
#define VS_PRNID_STRCMP		2
#define VS_PRNID_STRLEN		3
#define VS_PRNID_STRTOLL	4
#define VS_PRNID_MEMCHR		5
#define VS_PRNID_MEMCMP		6

/* closure */
struct vsym_clo
{
	uint64_t	vs_ret;
	vsym_clo_f	vs_f;
	void		*vs_aux;
	struct vsym_clo	*vs_next;
};

int virtsym_already(vsym_clo_f, vsym_check_f, const void* aux, uint64_t* ret);
void virtsym_add(vsym_clo_f f, uint64_t ret, void* dat);
void* virtsym_safe_memcopy(const void* m, unsigned len);
void virtsym_prune(pruneid_t);

/* in the future, this might do something smart like install a post hook
 * so that nothing forks so there's no state pollution */
#define virtsym_disabled() do { } while (0)
#define DECL_VIRTSYM_FAKE(f) static void fake_##f(void* c)	\
	{ virtsym_add(f, GET_RET(kmc_regs_get()), c); }
#define virtsym_fake_n(f, dat, n) do {		\
	klee_print_expr("faking func " #f , 123);	\
	klee_hook_return(n, fake_##f, (uint64_t)(dat)); } while (0)
#define virtsym_fake(f, dat) virtsym_fake_n(f, dat, 1)

// void virtsym_disabled(void);
//
#endif

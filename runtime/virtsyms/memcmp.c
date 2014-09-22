#define GUEST_ARCH_AMD64
#include "klee/klee.h"
#include "virtsym.h"
#include <string.h>
#include "../syscall/syscalls.h"


static void memcmp_enter(void* r);
static void memcmp_fini(uint64_t _r, void* aux);

#define HOOK_FUNC(x,y) void __hookpre_##x(void* r) { y(r); }

HOOK_FUNC(__GI_memcmp, memcmp_enter);
HOOK_FUNC(memcmp, memcmp_enter);
HOOK_FUNC(__memcmp_sse4_1, memcmp_enter);
HOOK_FUNC(__memcmp_sse2, memcmp_enter);

struct memcmp_clo
{
	const void	*p[2];
	unsigned	len;
};

/* XXX: could be smarter about symbolic sizes to make it easier to
 * process in fini handler */
static void memcmp_enter(void* r)
{
	uint64_t		ret;
	struct memcmp_clo	clo, *clo_ret;

	clo.p[0] = (const void*)GET_ARG0(r);
	clo.p[1] = (const void*)GET_ARG1(r);
	clo.len = GET_ARG2(r);

	/* 1. check pointers */
	if (!klee_is_valid_addr(clo.p[0])) return;
	if (!klee_is_valid_addr(clo.p[1])) return;

	/* ignore concretes */
	if (	!klee_is_symbolic_addr(clo.p[0]) &&
		!klee_is_symbolic_addr(clo.p[1])) return;

	/* set value to symbolic */
	if (!klee_make_vsym(&ret, sizeof(ret), "vmemcmp")) return;

	/* return {-1,0,1} */
	klee_assume_sge(ret, -1);
	klee_assume_ule(ret, 1);
	GET_SYSRET(r) = ret;

	clo.p[0] = virtsym_safe_memcopy(clo.p[0], clo.len);
	clo.p[1] = virtsym_safe_memcopy(clo.p[1], clo.len);
	clo_ret = malloc(sizeof(clo));
	memcpy(clo_ret, &clo, sizeof(clo));
	virtsym_add(memcmp_fini, ret, clo_ret);

	/* no need to evaluate, skip */
	kmc_skip_func();
}

static void memcmp_fini(uint64_t _r, void* aux)
{
	struct memcmp_clo	*clo = (struct memcmp_clo*)aux;
	int64_t			r = klee_get_value(_r);
	unsigned		i;

	klee_assume_eq(r, _r);

	if (r == 0) {
		/* generate equal bufs */
		for (i = 0; i < clo->len; i++) {
			if (!klee_feasible_eq(
				((const char*)clo->p[0])[i],
				((const char*)clo->p[1])[i]))
				virtsym_prune(VS_PRNID_MEMCMP);
			klee_assume_eq(
				((const char*)clo->p[0])[i],
				((const char*)clo->p[1])[i]);
		}
		return;
	}

	/* swap; only bother with p[0] < p[1] case */
	if (r == 1) {
		const void*	tmp = clo->p[0];
		clo->p[0] = clo->p[1];
		clo->p[1] = tmp;
	}

	/* generate buffer all equal up to some byte p[0][i] < p[1][i] */
	for (i = 0; i < clo->len; i++) {
		if (klee_valid_ugt(
			((const uint8_t*)clo->p[0])[i],
			((const uint8_t*)clo->p[1])[i]))
			virtsym_prune(VS_PRNID_MEMCMP);

		if (klee_valid_ult(
			((const uint8_t*)clo->p[0])[i],
			((const uint8_t*)clo->p[1])[i])) {
			klee_assume_ult(
				((const uint8_t*)clo->p[0])[i],
				((const uint8_t*)clo->p[1])[i]);
		}

		klee_assume_eq(
			((const uint8_t*)clo->p[0])[i],
			((const uint8_t*)clo->p[1])[i]);
	}	

	/* buffers must be equal; prune */
	virtsym_prune(VS_PRNID_MEMCMP);
}
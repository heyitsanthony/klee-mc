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

	/* 1. make sure not symbolic pointers */
	if (klee_is_symbolic_addr(clo.p[0]) || klee_is_symbolic_addr(clo.p[1]))
		return;

	/* 2. check pointers */
	if (!klee_is_valid_addr(clo.p[0])) return;
	if (!klee_is_valid_addr(clo.p[1])) return;

	/* set value to symbolic */
	if (!klee_make_vsym(&ret, sizeof(ret), "vmemcmp")) {
		virtsym_disabled();
		return;
	}

	/* return {-1,0,1} */
	klee_assume_sge(ret, -1);
	klee_assume_sle(ret, 1);
	GET_SYSRET(r) = ret;

	clo.p[0] = virtsym_safe_memcopy(clo.p[0], clo.len);
	clo.p[1] = virtsym_safe_memcopy(clo.p[1], clo.len);
	clo_ret = malloc(sizeof(clo));
	memcpy(clo_ret, &clo, sizeof(clo));
	virtsym_add(memcmp_fini, ret, clo_ret);

	/* no need to evaluate, skip */
	kmc_skip_func();
}

static void memcmp_fini(uint64_t r, void* aux)
{
	int64_t			_r = (int64_t)r;
	struct memcmp_clo	*clo = (struct memcmp_clo*)aux;
	unsigned		i;

	if (_r == 0) {
		/* generate equal bufs */
		for (i = 0; i < clo->len; i++) {
			if (!klee_feasible_eq(
				((const char*)clo->p[0])[i],
				((const char*)clo->p[1])[i])) {
				klee_print_expr("urghrgh", 1);
				virtsym_prune(VS_PRNID_MEMCMP);
			}
			klee_assume_eq(
				((const char*)clo->p[0])[i],
				((const char*)clo->p[1])[i]);
		}
		return;
	}

	/* swap; only bother with p[0] < p[1] case */
	if (_r > 0) {
		const void*	tmp = clo->p[0];
		clo->p[0] = clo->p[1];
		clo->p[1] = tmp;
		klee_assume_sgt(_r, 0);
	} else {
		klee_assume_slt(_r, 0);
	}

	/* generate buffer all equal up to some byte p[0][i] < p[1][i] */
	for (i = 0; i < clo->len; i++) {
		if (klee_feasible_ult(
			((const uint8_t*)clo->p[0])[i],
			((const uint8_t*)clo->p[1])[i])) {
			klee_assume_ult(
				((const uint8_t*)clo->p[0])[i],
				((const uint8_t*)clo->p[1])[i]);
			return;
		}

		if (klee_valid_ugt(
			((const uint8_t*)clo->p[0])[i],
			((const uint8_t*)clo->p[1])[i])) {
			klee_print_expr("urghrgh", 999);
			virtsym_prune(VS_PRNID_MEMCMP);
		}

		/* it's actually >= at this point, but we only want ret value
		 * with <, which means must use == */
		klee_assume_eq(
			((const uint8_t*)clo->p[0])[i],
			((const uint8_t*)clo->p[1])[i]);
	}	

	/* buffers must be equal; prune */
	virtsym_prune(VS_PRNID_MEMCMP);
}
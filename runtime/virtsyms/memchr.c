#define GUEST_ARCH_AMD64
#include "klee/klee.h"
#include "virtsym.h"
#include <string.h>
#include "../syscall/syscalls.h"


static void memchr_enter(void* r);
static void memchr_fini(uint64_t _r, void* aux);

#define HOOK_FUNC(x,y) void __hookpre_##x(void* r) { y(r); }

HOOK_FUNC(__GI_memchr, memchr_enter);
HOOK_FUNC(memchr, memchr_enter);
HOOK_FUNC(__memchr, memchr_enter);
HOOK_FUNC(__memchr_sse4_1, memchr_enter);
HOOK_FUNC(__memchr_sse2, memchr_enter);

/* XXX: rawmemchr does not have a 'count'. how to fix? */
//HOOK_FUNC(__rawmemchr_sse2, memchr_enter);
//HOOK_FUNC(__rawmemchr_sse42, memchr_enter);

struct memchr_clo
{
	const void	*p;
	unsigned	len;
	uint8_t		c;
};

static void memchr_enter(void* r)
{
	uint64_t		ret;
	struct memchr_clo	clo, *clo_ret;
	const char		*s;
	unsigned		i;

	clo.p = (const char*)GET_ARG0(r);
	s = clo.p;
	clo.c = (uint8_t)GET_ARG1(r);
	clo.len = GET_ARG2(r);

	/* 1. check pointers, common to crash in memchr */
	if (!klee_is_valid_addr(klee_get_value(s)))
		return;

	/* ignore concrete prefixes */
	for (i = 0; i < clo.len; i++) {
		if (klee_is_symbolic(s[i])) break;
		if (klee_feasible_eq(s[i], clo.c))
			return;
	}
	if (i == clo.len) return;

	/* set value to symbolic */
	if (!klee_make_vsym(&ret, sizeof(ret), "vmemchr")) {
		virtsym_disabled();
		return;
	}

	klee_assume_uge(ret, i); // ret >= i
	klee_assume_ule(ret, clo.len); // ret <= len
	GET_SYSRET(r) = klee_mk_ite(
		klee_mk_eq(ret, clo.len),
		0,
		s + ret);
		
	clo.p = virtsym_safe_memcopy(clo.p, clo.len);
	clo_ret = malloc(sizeof(clo));
	memcpy(clo_ret, &clo, sizeof(clo));
	virtsym_add(memchr_fini, ret, clo_ret);

	/* no need to evaluate, skip */
	kmc_skip_func();
}

static void memchr_fini(uint64_t _r, void* aux)
{
	const struct memchr_clo	*clo = aux;
	const char	*s = clo->p;
	unsigned int	i;

	/* does not exist in string? */
	if (klee_feasible_eq(_r, clo->len)) {
		for (i = 0; i < clo->len; i++) {
			/* 'c' must exist in s? */
			if (klee_valid_eq(s[i], clo->c))
				break;
		}

		/* 'c' feasibly does not exist in s? */
		if (i == clo->len) {
			klee_assume_eq(_r, clo->len);
			for (i = 0; i < clo->len; i++)
				klee_assume_ne(s[i], clo->c);
			return;
		}
	}

	/* exists in string? */
	for (i = 0; i < clo->len; i++) {
		if (klee_feasible_eq(s[i], clo->c) && klee_feasible_eq(_r, i)) {
			klee_assume_eq(_r, i);
			klee_assume_eq(s[i], clo->c);
			return;
		}
		if (klee_valid_eq(s[i], clo->c) && !klee_feasible_eq(_r, i))
			virtsym_prune(VS_PRNID_MEMCHR);
		klee_assume_ne(s[i], clo->c);
	}

	virtsym_prune(VS_PRNID_MEMCHR);
}
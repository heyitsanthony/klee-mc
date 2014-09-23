#define GUEST_ARCH_AMD64
#include "klee/klee.h"
#include "virtsym.h"
#include <string.h>
#include "../syscall/syscalls.h"


static void strlen_enter(void* r);
static void strlen_fini(uint64_t _r, void* aux);

#define HOOK_FUNC(x,y) void __hookpre_##x(void* r) { y(r); }

HOOK_FUNC(__GI_strlen, strlen_enter);
HOOK_FUNC(strlen, strlen_enter);
HOOK_FUNC(__strlen_sse42, strlen_enter);

static void strlen_enter(void* r)
{
	const char	*s;
	const char	*s_copy;
	uint64_t	ret;
	void		*clo_dat;
	unsigned	i;

	s = (const char*)GET_ARG0(r);

	/* 1. check pointers, common to crash in strlen */
	if (!klee_is_valid_addr(s)) return;

	/* ignore concretes */
	if (!klee_is_symbolic(s[0])) return;

	i = 0;
	while (	klee_is_valid_addr(&s[i]) &&
		(klee_is_symbolic(s[i]) || s[i] != '\0')) i++;

	// XXX: generate a state that causes an overflow here
	// if (!klee_is_valid_addr(&s[i]))

	/* set value to symbolic */
	if (!klee_make_vsym(&ret, sizeof(ret), "vstrlen")) {
		virtsym_disabled();
		return;
	}

	klee_assume_ule(ret, i);
	GET_SYSRET(r) = ret;

	s_copy = virtsym_safe_strcopy(s);
	clo_dat = malloc(sizeof(s_copy));
	memcpy(clo_dat, &s_copy, sizeof(s_copy));
	virtsym_add(strlen_fini, ret, clo_dat);

	/* no need to evaluate, skip */
	kmc_skip_func();
}

static void strlen_fini(uint64_t _r, void* aux)
{
	const char	*s = ((char**)aux)[0];
	unsigned	i = 0;

	while(klee_feasible_ne(s[i], 0)) {
		if (klee_feasible_eq(_r, i) && klee_feasible_eq(s[i], 0)) {
			klee_assume_eq(_r, i);
			klee_assume_eq(s[i], 0);
			break;
		}
		klee_assume_ne(s[i], 0);
		i++;
		if (!klee_is_valid_addr(&s[i])) {
			klee_print_expr("oops", &s[i]);
			virtsym_prune(VS_PRNID_STRLEN);
		}
	}

	if (!klee_valid_eq(s[i], 0) || !klee_valid_eq(_r, i))
		virtsym_prune(VS_PRNID_STRLEN);
}
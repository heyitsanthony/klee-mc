#define GUEST_ARCH_AMD64
#include "klee/klee.h"
#include "virtsym.h"
#include <string.h>
#include "../syscall/syscalls.h"


static void strchr_enter(void* r);
static void strchrnul_enter(void* r);
static void strchr_fini(uint64_t _r, void* aux);

#define HOOK_FUNC(x,y) void __hookpre_##x(void* r) { y(r); }

HOOK_FUNC(__GI_strchr, strchr_enter);
HOOK_FUNC(strchr, strchr_enter);
HOOK_FUNC(__strchr_sse42, strchr_enter);
HOOK_FUNC(__strchrnul, strchrnul_enter);

struct strchr_clo
{
	const char	*s, *s_orig;
	uint8_t		c;
	unsigned	i;
};

static int strchr_internal(void* r, uint64_t* ret, unsigned* len)
{
	const char		*s;
	struct strchr_clo	*clo_dat;
	unsigned		i;

	s = (const char*)GET_ARG0(r);

	/* 1. check pointers, common to crash in strchr */
	if (!klee_is_valid_addr(s)) return 0;

	/* ignore concretes */
	if (!klee_is_symbolic(s[0])) return 0;

	i = 0;
	while (	klee_is_valid_addr(&s[i]) &&
		(klee_is_symbolic(s[i]) || s[i] != '\0')) i++;

	// XXX: generate a state that causes an overflow here
	// if (!klee_is_valid_addr(&s[i]))

	/* set value to symbolic */
	if (!klee_make_vsym(ret, sizeof(*ret), "vstrchr")) return 0;

	klee_assume_ule(*ret, i+1);

	/* XXX: this should copy the whole string */
	clo_dat = malloc(sizeof(*clo_dat));
	clo_dat->s = virtsym_safe_strcopy(s);
	clo_dat->s_orig = s;
	clo_dat->c = (uint8_t)GET_ARG1(r);
	clo_dat->i = i;
	virtsym_add(strchr_fini, *ret, clo_dat);
	*len = i;

	return 1;
}

static void strchr_enter(void* r)
{
	uint64_t 	ret;
	unsigned	i;
	const char	*s = (const char*)GET_ARG0(r);

	if (!strchr_internal(r, &ret, &i)) {
		virtsym_disabled();
		return;
	}

	GET_SYSRET(r) = klee_mk_ite(klee_mk_eq(ret, i+1), 0, s+ret);
	kmc_skip_func();
}

static void strchrnul_enter(void* r)
{
	uint64_t	ret;
	unsigned	i;
	const char	*s = (const char*)GET_ARG0(r);

	if (!strchr_internal(r, &ret, &i)) {
		virtsym_disabled();
		return;
	}

	GET_SYSRET(r) = klee_mk_ite(klee_mk_eq(ret, i+1), s+i, s+ret);
	kmc_skip_func();
}


/* this forces a value for the index-- is this wise? could miss tests */
static void strchr_fini(uint64_t _r, void* aux)
{
	struct strchr_clo	*clo = aux;
	const char		*s = clo->s;
	uint8_t			c = clo->c;
	uint64_t		r = klee_get_value(_r);
	unsigned		i = 0;

	klee_assume_eq(_r, r);
	_r = _r - (uint64_t)clo->s_orig;

	/* does _r exceed bounds? */
	if (_r >= clo->i+1) {
		i = 0;
		while(klee_valid_ne(s[i], 0)) {
			if (!klee_feasible_ne(s[i], c)) {
				klee_print_expr("failing OOB r", _r);
				virtsym_prune(VS_PRNID_STRCHR);
			}
			klee_assume_ne(s[i], c);
			i++;
		}
		klee_assume_eq(s[i], 0);
		return;
	}

	while(klee_feasible_ne(s[i], 0)) {
		if (klee_feasible_eq(_r, i) && klee_feasible_eq(s[i], c)) {
			klee_assume_eq(_r, i);
			klee_assume_eq(s[i], c);
			break;
		}
		i++;
		if (!klee_is_valid_addr(&s[i])) {
			klee_print_expr("failing bad addr", &s[i]);
			virtsym_prune(VS_PRNID_STRCHR);
		}
	}

	if (!klee_valid_eq(s[i], c) || !klee_valid_eq(_r, i)) {
		virtsym_prune(VS_PRNID_STRCHR);
	}
}
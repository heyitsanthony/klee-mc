#define GUEST_ARCH_AMD64
#include "klee/klee.h"
#include "virtsym.h"
#include <string.h>
#include "../syscall/syscalls.h"


static void strcmp_enter(void* r);
static void strcmp_fini(uint64_t _r, void* aux);

#define HOOK_FUNC(x,y) void __hookpre_##x(void* r) { y(r); }

HOOK_FUNC(__GI_strcmp, strcmp_enter);
HOOK_FUNC(__GI___strcmp_ssse3, strcmp_enter);

static void strcmp_enter(void* r)
{
	const char	*s[2];
	int		is_sym[2];
	uint64_t	ret;
	void		*clo_dat;

	s[0] = (const char*)GET_ARG0(r);
	s[1] = (const char*)GET_ARG1(r);

	klee_print_expr("HELLO!", *s[0]);

	/* 1. check pointers, common to crash in strcmp */
	if (!klee_is_valid_addr(s[0]) || !klee_is_valid_addr(s[1])) 
		return;

	is_sym[0] = klee_is_symbolic(*s[0]);
	is_sym[1] = klee_is_symbolic(*s[1]);

	/* both are concrete; ignore */
	if (!is_sym[0] && !is_sym[1])
		return;

	if (is_sym[0] != is_sym[1]) {
		/* one is concrete */
		if (!is_sym[0]) {
			klee_print_expr("ULP!!!!", 0);
		}
	} else {
		/* both are symbolic */

		/* some constraint? */
		if (	!klee_feasible_eq(*s[0], *s[1]) ||
			!klee_feasible_ne(*s[0], *s[1]))
			return;

	}

	klee_print_expr("hullo......", 123);


	/* set value to symbolic */
	klee_make_vsym(&ret, sizeof(ret), "vstrcmp");
	GET_SYSRET(r) = ret;

	/* XXX: this should copy the whole string */
	clo_dat = malloc(sizeof(s));
	memcpy(clo_dat, s, sizeof(s));
	virtsym_add(strcmp_fini, ret, clo_dat);

	/* no need to evaluate, skip */
	kmc_skip_func();
}


static void strcmp_fini(uint64_t _r, void* aux)
{
	const char	*s[2];
	int64_t		r = _r;

	s[0] = ((char**)aux)[0];
	s[1] = ((char**)aux)[1];

	/* XXX: problem. forks too much. FUFKCKCK */

	/* kind of lazy, could do better... */
	while((*s[0]) == (*s[1]) && s[0] != 0) {
		s[0]++;
		s[1]++;
	}

	/* bind */
	if (r == 0) {
		if (*s[0] != *s[1])
			klee_silent_exit(0);
	} else if (r < 0) {
		if (*s[0] >= *s[1])
			klee_silent_exit(0);
	} else {
		if (*s[0] <= *s[1])
			klee_silent_exit(0);
	}
}

static void strcmp_fini2(uint64_t _r, void* aux)
{
	const char	*s[2];
	int64_t		r = _r;

	s[0] = ((char**)aux)[0];
	s[1] = ((char**)aux)[1];

	/* skip prefix */
	while(klee_valid_eq(*s[0], *s[1]) && s[0] != 0) {
		s[0]++;
		s[1]++;
	}

	if (r == 0) {
		while(klee_feasible_eq(*s[0], *s[1]) && s[0] != 0) {
			klee_assume_eq(*s[0], *s[1]);
			s[0]++;
			s[1]++;
		}

		if (s[0] || s[1] || klee_valid_ne(s[0], s[1]))
			klee_silent_exit(0);
	} else if (r < 0) {
		while (klee_feasible_eq(*s[0], *s[1]) && s[0] != 0) {
			if (klee_feasible_ult(s[0], s[1]))
				break;
			s[0]++;
			s[1]++;
		}

		if (!klee_feasible_ult(s[0], s[1]))
			klee_silent_exit(0);

		klee_assume_ult(s[0], s[1]);
	} else {
		while (klee_feasible_eq(*s[0], *s[1]) && s[0] != 0) {
			if (klee_feasible_ugt(s[0], s[1]))
				break;
			s[0]++;
			s[1]++;
		}

		if (!klee_feasible_ugt(s[0], s[1]))
			klee_silent_exit(0);

		klee_assume_ugt(s[0], s[1]);
	}
}
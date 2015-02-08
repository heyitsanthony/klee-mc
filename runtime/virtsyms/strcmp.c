#define GUEST_ARCH_AMD64
#include "klee/klee.h"
#include "virtsym.h"
#include <string.h>
#include "../syscall/syscalls.h"


static void strcmp_enter(void* r);
static void strcmp_fini2(uint64_t _r, void* aux);

#define HOOK_FUNC(x,y) void __hookpre_##x(void* r) { y(r); }

HOOK_FUNC(__GI_strcmp, strcmp_enter);
HOOK_FUNC(__GI___strcmp_ssse3, strcmp_enter);
HOOK_FUNC(__strcmp_sse42, strcmp_enter);

struct strcmp_clo
{
	/* orig pointers */
	const char* s[2];
	/* copied buffers */
	struct virt_str	*vs[2];
};

static void strcmp_enter(void* r)
{	
	struct strcmp_clo	clo, *ret_clo;
	int			is_sym[2];
	uint64_t		ret;

	clo.s[0] = (const char*)GET_ARG0(r);
	clo.s[1] = (const char*)GET_ARG1(r);

	/* 1. check pointers, common to crash in strcmp */
	if (!klee_is_valid_addr(clo.s[0]) || !klee_is_valid_addr(clo.s[1])) 
		return;

	is_sym[0] = klee_is_symbolic(*clo.s[0]);
	is_sym[1] = klee_is_symbolic(*clo.s[1]);

	if (is_sym[0] != is_sym[1]) {
		/* one is concrete. should I bother checking better? */
		if (	!klee_feasible_eq(*clo.s[0], *clo.s[1]) ||
			!klee_feasible_ne(*clo.s[0], *clo.s[1]))
			return;
	} else if (!is_sym[0]) {
		/* both are symbolic */

		/* some constraint? */
		if (	!klee_feasible_eq(*clo.s[0], *clo.s[1]) ||
			!klee_feasible_ne(*clo.s[0], *clo.s[1]))
			return;

	}

	if ((clo.vs[0] = virtsym_safe_strcopy_conc(clo.s[0])) == NULL)
		goto no_s0;
	if ((clo.vs[1] = virtsym_safe_strcopy_conc(clo.s[1])) == NULL)
		goto no_s1;
	if (vs_is_conc(clo.vs[0]) && vs_is_conc(clo.vs[1]))
		goto vs_concrete;
	if (!klee_make_vsym(&ret, sizeof(ret), "vstrcmp"))
		goto disabled;

	/* set value to symbolic */
	GET_SYSRET(r) = ret;

	/* XXX: this should copy the whole string */
	ret_clo = malloc(sizeof(clo));
	memcpy(ret_clo, &clo, sizeof(clo));

	virtsym_add(strcmp_fini2, ret, ret_clo);

	/* no need to evaluate, skip */
	kmc_skip_func();
	return;
disabled:
	virtsym_disabled();
vs_concrete:
	virtsym_str_free(clo.vs[1]);
no_s1:
	virtsym_str_free(clo.vs[0]);
no_s0:
	return;
}
static int is_conc_nul(const char* s)
{
	char c = *s;
	if (klee_is_symbolic(c)) return 0;
	return c == 0;
}

static void strcmp_fini2(uint64_t _r, void* aux)
{
	struct strcmp_clo *clo = aux;
	const char	*s[2];
	int64_t		r = (int)klee_get_value((int)_r);

	klee_assume_eq(r, _r);

	s[0] = clo->vs[0]->vs_str;
	s[1] = clo->vs[1]->vs_str;

	int i = 0;
	/* skip prefix */
	while(klee_valid_eq(*s[0], *s[1]) && *s[0] != 0) {
		s[0]++;
		s[1]++;
		i++;
	}

	klee_print_expr("prefix skipped", i);

	/* strings equal? */
	if (r == 0) {
		while(klee_feasible_eq(*s[0], *s[1])) {
			if (is_conc_nul(s[0])) break;
			if (is_conc_nul(s[1])) break;
			klee_assume_eq(*s[0], *s[1]);
			s[0]++;
			s[1]++;
		}

		if (	klee_valid_ne(*s[0], 0) ||
			klee_valid_ne(*s[1], 0) ||
			klee_valid_ne(*s[0], *s[1]))
		{
			klee_print_expr("bad eq check s[0]", *s[0]);
			klee_print_expr("bad eq check s[1]", *s[1]);
			virtsym_prune(VS_PRNID_STRCMP);
		}

		klee_assume_eq(*s[0], 0);
		klee_assume_eq(*s[1], 0);

		return;
	}
	
	if (r > 0) {
		const char* t = s[0];
		s[0] = s[1];
		s[1] = t;
	} else {
		klee_assume_slt(r, 0);
	}

	/* keep going while equal and not end of string */
	while (klee_feasible_eq(*s[0], *s[1]) && !klee_valid_eq(*s[0], 0)) {
		if (klee_feasible_ult(*s[0], *s[1]))
			break;
		s[0]++;
		s[1]++;
	}

	if (!klee_feasible_ult(*s[0], *s[1]))
		virtsym_prune(VS_PRNID_STRCMP);

	klee_print_expr("got one...", r);
	klee_assume_ult(*s[0], *s[1]);
}
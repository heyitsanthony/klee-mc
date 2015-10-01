#define GUEST_ARCH_AMD64
#include "klee/klee.h"
#include "virtsym.h"
#include <string.h>
#include "../syscall/syscalls.h"

static void strcmp_enter(void* r);
static void strcmp_fini2(uint64_t _r, void* aux);
DECL_VIRTSYM_FAKE(strcmp_fini2)

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

static int strcmp_check(const struct strcmp_clo* cur,
			const struct strcmp_clo* old)
{
	return	klee_expr_hash(cur->s[0]) == klee_expr_hash(old->s[0]) &&
		klee_expr_hash(cur->s[1]) == klee_expr_hash(old->s[1]) &&
		virtsym_str_eq(cur->vs[0], old->vs[0]) &&
		virtsym_str_eq(cur->vs[1], old->vs[1]);
}

static void strcmp_enter(void* r)
{	
	struct strcmp_clo	clo, *ret_clo;
	uint64_t		ret;

	clo.s[0] = (const char*)GET_ARG0(r);
	clo.s[1] = (const char*)GET_ARG1(r);

	if ((clo.vs[0] = virtsym_safe_strcopy_conc(clo.s[0])) == NULL)
		goto no_s0;
	if ((clo.vs[1] = virtsym_safe_strcopy_conc(clo.s[1])) == NULL)
		goto no_s1;
	if (vs_is_conc(clo.vs[0]) && vs_is_conc(clo.vs[1]))
		goto vs_concrete;

	// already been done?
	if (virtsym_already(	strcmp_fini2,
				(vsym_check_f)strcmp_check, &clo, &ret))
	{
		virtsym_str_free(clo.vs[0]);
		virtsym_str_free(clo.vs[1]);
		GET_SYSRET(r) = ret;
		kmc_skip_func();
		return;
	}

	ret_clo = malloc(sizeof(clo));
	memcpy(ret_clo, &clo, sizeof(clo));

	if (!klee_make_vsym(&ret, sizeof(ret), "vstrcmp")) {
		virtsym_fake(strcmp_fini2, ret_clo);
	} else {
		/* set value to symbolic */
		GET_SYSRET(r) = ret;
		virtsym_add(strcmp_fini2, ret, ret_clo);
		/* no need to evaluate, skip */
		kmc_skip_func();
	}
	return;

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
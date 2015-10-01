#define GUEST_ARCH_AMD64
#include "klee/klee.h"
#include "virtsym.h"
#include <string.h>
#include "../syscall/syscalls.h"


static void strchr_enter(void* r);
static void strchrnul_enter(void* r);
static void strchr_fini(uint64_t _r, void* aux);
DECL_VIRTSYM_FAKE(strchr_fini)

#define HOOK_FUNC(x,y) void __hookpre_##x(void* r) { y(r); }

HOOK_FUNC(__GI_strchr, strchr_enter);
HOOK_FUNC(strchr, strchr_enter);
HOOK_FUNC(__strchr_sse42, strchr_enter);
HOOK_FUNC(__strchrnul, strchrnul_enter);

struct strchr_clo
{
	const char	*s_orig;
	struct virt_str	*vs;
	uint8_t		c;
};

static int strchr_check(const struct strchr_clo* cur,
			const struct strchr_clo* old)
{
	return	(klee_expr_hash(cur->c) == klee_expr_hash(old->c)) &&
		virtsym_str_eq(cur->vs, old->vs) &&
		(klee_expr_hash(cur->s_orig) == klee_expr_hash(old->s_orig));
}

static int strchr_internal(void* r, uint64_t* ret, unsigned* len)
{
	const char		*s = (const char*)GET_ARG0(r);
	struct strchr_clo	*clo_dat;
	struct virt_str		*vs;

	if ((vs = virtsym_safe_strcopy(s)) == NULL) {
		return 0;
	}

	clo_dat = malloc(sizeof(*clo_dat));
	clo_dat->vs = vs;
	clo_dat->s_orig = s;
	clo_dat->c = (uint8_t)GET_ARG1(r);

	if (virtsym_already(	strchr_fini,
				(vsym_check_f)strchr_check, clo_dat, ret))
	{
		*len = vs->vs_len_max;
		virtsym_str_free(vs);
		free(clo_dat);
		return 1;
	}

	/* set value to symbolic */
	if (!klee_make_vsym(ret, sizeof(*ret), "vstrchr")) {
		virtsym_fake_n(strchr_fini, clo_dat, 2);
		return 0;
	}

	*len = vs->vs_len_max;
	klee_assume_ule(*ret, vs->vs_len_max+1);
	virtsym_add(strchr_fini, *ret, clo_dat);
	return 1;
}

static void strchr_enter(void* r)
{
	uint64_t 	ret;
	unsigned	i;
	const char	*s = (const char*)GET_ARG0(r);

	if (!strchr_internal(r, &ret, &i))
		return;

	GET_SYSRET(r) = klee_mk_ite(klee_mk_eq(ret, i+1), 0, s+ret);
	kmc_skip_func();
}

static void strchrnul_enter(void* r)
{
	uint64_t	ret;
	unsigned	i;
	const char	*s = (const char*)GET_ARG0(r);

	if (!strchr_internal(r, &ret, &i))
		return;

	GET_SYSRET(r) = klee_mk_ite(klee_mk_eq(ret, i+1), s+i, s+ret);
	kmc_skip_func();
}

/* this forces a value for the index-- is this wise? could miss tests */
static void strchr_fini(uint64_t _r, void* aux)
{
	struct strchr_clo	*clo = aux;
	const char		*s = clo->vs->vs_str;
	uint8_t			c = clo->c;
	unsigned		i = 0;

	/* does range exceed bounds? try to fill out up to index [r] */
	if (_r == clo->vs->vs_len_max+1) {
		while (i < _r) {
			/* string doesn't have 'c' and string hasn't ended? */
			uint64_t has_more_chars = klee_mk_and(
				klee_mk_ne(s[i], 0), klee_mk_ne(s[i], c));
			if (!__klee_feasible(has_more_chars)) {
				/* string doesn't match 'r'; must bail out */
				break;
			}
			__klee_assume(has_more_chars);
			i++;
		}

		if (i+1 != _r || !klee_feasible_eq(s[i], 0)) {
			klee_print_expr("failing OOB r", _r);
			virtsym_prune(VS_PRNID_STRCHR);
		}

		/* index should be to the 'nul' byte */
		klee_assume_eq(s[i], 0);
		return;
	}

	// _r > clo->i+1 should never happen
	klee_assume_ult(_r, clo->vs->vs_len_max+1);

	while (klee_feasible_ne(s[i], 0)) {
		uint64_t match_cond = klee_mk_and(
			klee_mk_eq(_r, i), klee_mk_eq(s[i], c));
		if (__klee_feasible(match_cond)) {
			__klee_assume(match_cond);
			break;
		}
		i++;
		if (!klee_is_valid_addr(&s[i])) {
			klee_print_expr("failing bad addr", &s[i]);
			virtsym_prune(VS_PRNID_STRCHR);
		}
	}

	if (!klee_valid_eq(s[i], c) || !klee_valid_eq(_r, i)) {
		klee_print_expr("Can't complete string?", i);
		virtsym_prune(VS_PRNID_STRCHR);
	}
}
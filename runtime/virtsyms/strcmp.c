#define GUEST_ARCH_AMD64
#include "klee/klee.h"
#include "virtsym.h"
#include <string.h>
#include "../syscall/syscalls.h"

#define STRCMP_MERGE_DEPTH	32

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
	unsigned		i;

	clo.s[0] = (const char*)GET_ARG0(r);
	clo.s[1] = (const char*)GET_ARG1(r);

	if ((clo.vs[0] = virtsym_safe_strcopy_conc(clo.s[0])) == NULL)
		goto no_s0;
	if ((clo.vs[1] = virtsym_safe_strcopy_conc(clo.s[1])) == NULL)
		goto no_s1;
	if (vs_is_conc(clo.vs[0]) && vs_is_conc(clo.vs[1]))
		goto vs_concrete;

	// test concrete prefixes in case strcmp value is concrete
	for (	i = 0;	i < clo.vs[0]->vs_first_sym_idx &&
			i < clo.vs[1]->vs_first_sym_idx;
		i++)
	{
		int diff = clo.vs[0]->vs_str[i] - clo.vs[1]->vs_str[i];
		if (diff != 0) {
			GET_SYSRET(r) = (diff < 0) ? -1 : 1;
			virtsym_str_free(clo.vs[1]);
			virtsym_str_free(clo.vs[0]);
			kmc_skip_func();
			return;
		}
	}

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

// p_{-1} = true
// p_k = p_{k-1} && s0[k] == s1[k]
// q_{-1} = false
// (p_k && s0[k] == 0) => potential solution
// q_k = q_{k-1} || (p_k && s0[k] == 0)
// asssume q_k for all solutions
static unsigned strcmp_fini_eq(const char* s0, const char* s1, uint64_t* sols)
{
	uint64_t	p_k_prev = 1;
	uint64_t	q_k_prev = 0;
	unsigned	solutions = 0;

	do {
		uint64_t p_k = klee_mk_and(p_k_prev, klee_mk_eq(*s0, *s1));
		uint64_t q_k_part;

		// possibly equal up to k?
		if (!__klee_feasible(p_k))
			break;

		p_k_prev = p_k;
		q_k_part = klee_mk_and(p_k, klee_mk_eq(*s0, 0));

		// a possible solution?
		if (__klee_feasible(q_k_part)) {
			q_k_prev = klee_mk_or(q_k_prev, q_k_part);
			solutions++;
		}

		// party's over; char must be nul
		if (is_conc_nul(s0) || is_conc_nul(s1))
			break;

		s0++;
		s1++;
	} while (solutions < STRCMP_MERGE_DEPTH);

	*sols = q_k_prev;
	return solutions;
}

// p_{-1} = true
// p_{k} = p_{k-1} && s0[k] == s1[k] && s0[k] != 0
// q_{-1} = false
// potential solution: (p_{k-1} && s0[k] < s1[k])
// q_{k} = q_{k-1} || (p_{k-1} && s0[k] < s1[k])
// assume q_k for all solutions
static unsigned strcmp_fini_lt(const char* s0, const char* s1, uint64_t* sols)
{
	uint64_t	p_k_prev = 1;
	uint64_t	q_k_prev = 0;
	unsigned	solutions = 0;

	/* keep going while equal and not end of string */
	do {
		uint64_t p_k;
		uint64_t q_k_part;

		p_k = klee_mk_and(
			p_k_prev,
			klee_mk_and(klee_mk_eq(*s0, *s1),  klee_mk_ne(*s0, 0)));

		q_k_part = klee_mk_and(p_k_prev, klee_mk_ult(*s0, *s1));
		if (__klee_feasible(q_k_part)) {
			q_k_prev = klee_mk_or(q_k_prev, q_k_part);
			solutions++;
		}

		if (!__klee_feasible(p_k)) {
			break;
		}

		p_k_prev = klee_mk_and(p_k_prev, p_k);

		s0++;
		s1++;
	} while (solutions < STRCMP_MERGE_DEPTH);

	*sols = q_k_prev;
	return solutions;
}

static void strcmp_fini2(uint64_t r, void* aux)
{
	struct strcmp_clo	*clo = aux;
	uint64_t		sols = 0;
	const char		*s[2];

	// assume the program casts strcmp to int
	r = (int64_t)((int)r);

	s[0] = clo->vs[0]->vs_str;
	s[1] = clo->vs[1]->vs_str;

	/* already skippeded over this part in entry */
	int i = (clo->vs[0]->vs_first_sym_idx < clo->vs[1]->vs_first_sym_idx)
		? clo->vs[0]->vs_first_sym_idx
		: clo->vs[1]->vs_first_sym_idx;

	s[0] += i;
	s[1] += i;

	/* skip equal symbolic prefix */
	while(klee_valid(klee_mk_and(
		klee_mk_eq(*s[0], *s[1]),
		klee_mk_ne(*s[0], 0))))
	{
		s[0]++;
		s[1]++;
		i++;
	}

	if (klee_feasible_eq(r, 0)) {
		uint64_t	sols_eq;

		if (strcmp_fini_eq(s[0], s[1], &sols_eq) != 0) {
			sols = klee_mk_or(
				sols,
				klee_mk_and(klee_mk_eq(r, 0), sols_eq));
		}
	}

	if (__klee_feasible(klee_mk_slt(r, 0))) {
		uint64_t	sols_lt;

		if (strcmp_fini_lt(s[0], s[1], &sols_lt) != 0) {
			sols = klee_mk_or(
				sols,
				klee_mk_and(klee_mk_slt(r, 0), sols_lt));
		}
	}

	if (__klee_feasible(klee_mk_sgt(r, 0))) {
		uint64_t	sols_gt;

		if (strcmp_fini_lt(s[1], s[0], &sols_gt) != 0) {
			sols = klee_mk_or(
				sols,
				klee_mk_and(klee_mk_sgt(r, 0), sols_gt));
		}
	}

	if (!klee_is_symbolic(sols) && sols == 0) {
		virtsym_prune(VS_PRNID_STRCMP);
		return;
	}

	__klee_assume(sols);
}
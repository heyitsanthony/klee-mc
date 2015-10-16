#define GUEST_ARCH_AMD64
#include "klee/klee.h"
#include "virtsym.h"
#include <string.h>
#include "../syscall/syscalls.h"


static void strchr_enter(void* r);
static void strchrnul_enter(void* r);
static void strchr_fini(uint64_t _r, void* aux);
static void fake_strchr_fini(void *c);

#define HOOK_FUNC(x,y) void __hookpre_##x(void* r) { y(r); }
//#define DEBUG_STRCHR(x,y)	klee_print_expr(x,y)
//#define DEBUG_STRCHR_TRACE()	klee_stack_trace()
#define DEBUG_STRCHR(x,y)	do { } while (0)
#define DEBUG_STRCHR_TRACE()	do { } while (0)


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

static void fake_strchr_fini(void *c)
{
	struct strchr_clo	*clo = c;
	uint64_t		ret = GET_RET(kmc_regs_get());

	DEBUG_STRCHR("fake strchr fini0", ret);
	DEBUG_STRCHR_TRACE();

	if (ret == 0) {
		// strchrnul
		DEBUG_STRCHR("strchrnul gave 0", clo->s_orig);
		ret = clo->vs->vs_len_max + 1;
	} else if (ret == (uint64_t)(clo->s_orig + clo->vs->vs_len_max)) {
		// strchr
		ret = clo->vs->vs_len_max + 1;
	} else {
		ret = ret - (uint64_t)clo->s_orig;
	}

	DEBUG_STRCHR("fake strchr fini1", ret);
	virtsym_add(strchr_fini, ret, c);
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
		DEBUG_STRCHR("strchr already seen", s);
		*len = vs->vs_len_max;
		virtsym_str_free(vs);
		free(clo_dat);
		return 1;
	}

	// check prefix
	if (!klee_is_symbolic(clo_dat->c)) {
		unsigned i;
		for (i = 0; i < vs->vs_first_sym_idx; i++) {
			if (vs->vs_str[i] == clo_dat->c) {
				DEBUG_STRCHR("strchr found c", s);
				*len = i;
				virtsym_str_free(vs);
				free(clo_dat);
				return 1;
			}
		}
	}

	/* set value to symbolic */
	if (!klee_make_vsym(ret, sizeof(*ret), "vstrchr")) {
		DEBUG_STRCHR("virtsym faking strchr", s);
		DEBUG_STRCHR("virtsym faking strchr clo", clo_dat);
		DEBUG_STRCHR_TRACE();
		virtsym_fake_n(strchr_fini, clo_dat, 2);
		return 0;
	}

	DEBUG_STRCHR("making strchr", s);
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

	DEBUG_STRCHR("strchr enter", s);
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

	DEBUG_STRCHR("strchrnul enter", s);
	if (!strchr_internal(r, &ret, &i))
		return;

	GET_SYSRET(r) = klee_mk_ite(klee_mk_eq(ret, i+1), s+i, s+ret);
	kmc_skip_func();
}

/* this forces a value for the index-- is this wise? could miss tests */
// _r is the length of the string
// XXX this is crummy because it will fork a lot. Fix like strlen?
static void strchr_fini(uint64_t _r, void* aux)
{
	struct strchr_clo	*clo = aux;
	const char		*s = clo->vs->vs_str;
	uint8_t			c = clo->c;
	unsigned		i = 0;
	unsigned		vs_len_max = clo->vs->vs_len_max;
	uint64_t		p_k = 1;
	uint64_t		q_k = 0;
	unsigned		solutions = 0;

	// not found case
	if (klee_feasible_eq(_r, vs_len_max + 1)) {
		p_k =	klee_mk_and(
				klee_mk_eq(_r, vs_len_max+1),
				klee_mk_and(	klee_mk_eq(s[0], 0),
						klee_mk_ne(c, 0)));
		if (__klee_feasible(p_k)) {
			DEBUG_STRCHR("strchr c not found", p_k);
			__klee_assume(p_k);
			return;
		}

		p_k = 1 ;
		for (i = 0; i < vs_len_max; i++) {
			p_k = klee_mk_and(p_k, klee_mk_ne(s[i], c));
		}

		p_k = klee_mk_and(p_k, klee_mk_eq(_r, vs_len_max + 1));
		if (__klee_feasible(p_k)) {
			DEBUG_STRCHR("strchr2 c not found", p_k);
			__klee_assume(p_k);
			return;
		}
	}

	p_k = ~0;

	for (i = 0; i < vs_len_max; i++) {
		uint64_t next_q_k = klee_mk_and(
			klee_mk_eq(_r, i),
			klee_mk_and(p_k, klee_mk_eq(s[i], c)));
		if (__klee_feasible(next_q_k)) {
			q_k = klee_mk_or(q_k, next_q_k);
			solutions++;
		}

		uint64_t next_p_k = klee_mk_and(
			p_k,
			klee_mk_and(
				klee_mk_ne(s[i], 0),
				klee_mk_ne(s[i], c)));
		if (!__klee_feasible(next_p_k)) {
			if (solutions == 0) {
			DEBUG_STRCHR("vs_str", clo->vs->vs_str);
			DEBUG_STRCHR("vs_str[i]", clo->vs->vs_str[i]);
			DEBUG_STRCHR("vs_str[i]", klee_get_value(clo->vs->vs_str[i]));
			DEBUG_STRCHR("strchr old p_k", p_k);
			DEBUG_STRCHR("strchr c", c);
			DEBUG_STRCHR("strchr i", i);
			DEBUG_STRCHR("strchr r", _r);
			DEBUG_STRCHR("get_val s[i]", klee_get_value(s[i]));
			DEBUG_STRCHR("strchr vs_len_max", vs_len_max);
			DEBUG_STRCHR("shrchr exit early next_p_k", next_p_k);
			DEBUG_STRCHR("rejected next_q_k", next_q_k);
			} else {
				DEBUG_STRCHR("strchr bail on i=", i);
				DEBUG_STRCHR("strchr r=", _r);
			}
			break;
		}
		p_k = next_p_k;
	}

	if (solutions == 0) {
		virtsym_prune(VS_PRNID_STRCHR);
		return;
	}

	DEBUG_STRCHR("strchr has solution", q_k);
	__klee_assume(q_k);
}
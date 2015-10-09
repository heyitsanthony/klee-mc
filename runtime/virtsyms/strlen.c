#define GUEST_ARCH_AMD64
#include "klee/klee.h"
#include "virtsym.h"
#include <string.h>
#include "../syscall/syscalls.h"

static void strlen_enter(void* r);
static void strlen_fini(uint64_t _r, void* aux);
DECL_VIRTSYM_FAKE(strlen_fini)

#define STRLEN_MERGE_DEPTH	32

#define HOOK_FUNC(x,y) void __hookpre_##x(void* r) { y(r); }

HOOK_FUNC(__GI_strlen, strlen_enter);
HOOK_FUNC(strlen, strlen_enter);
HOOK_FUNC(__strlen_sse42, strlen_enter);

static void strlen_enter(void* r)
{
	const char	*s;
	struct virt_str	*vs;
	uint64_t	ret;

	s = (const char*)GET_ARG0(r);
	if ((vs = virtsym_safe_strcopy(s)) == NULL) {
		return;
	}

	// already been done?
	if (virtsym_already(strlen_fini, (vsym_check_f)virtsym_str_eq, vs, &ret)) {
		klee_print_expr("vsym strlen already", s);
		virtsym_str_free(vs);
		GET_SYSRET(r) = ret;
		kmc_skip_func();
		return;
	}

	/* set value to symbolic */
	if (!klee_make_vsym(&ret, sizeof(ret), "vstrlen")) {
		virtsym_fake(strlen_fini, vs);
		return;
	}

	/* can fake w/ vsym */
	klee_assume_ule(ret, vs->vs_len_max);
	klee_assume_uge(ret, vs->vs_first_sym_idx);
	GET_SYSRET(r) = ret;
	virtsym_add(strlen_fini, ret, vs);
	kmc_skip_func();
}

//#define DEBUG_PRINT(x,y)	klee_print_expr(x,y)
#define DEBUG_PRINT(x,y)

// p_{-1} = true
// p_k = p_{k-1} && s[k] != 0
// q_{-1} = false
// q_k = q_{k-1} || (r == (k-1) && p_{k-1} && s[k] == 0)
static void strlen_fini(uint64_t _r, void* aux)
{
	struct virt_str	*vs = (struct virt_str*)aux;
	const char	*s = vs->vs_str;
	unsigned	i = vs->vs_first_sym_idx;
	uint64_t	p_k_prev = ~0;
	uint64_t	q_k_prev = 0;
	unsigned	solutions = 0;

	if (!klee_is_symbolic(_r) && !klee_feasible_eq(s[_r], 0)) {
		DEBUG_PRINT("vstrlen const prune r", _r);
		virtsym_prune(VS_PRNID_STRLEN);
		return;
	}

	do {
		uint64_t	q_k;
		uint64_t	p_k;

		if (!klee_is_valid_addr(&s[i])) {
			klee_print_expr("vstrlen oops", &s[i]);
			break;
		}

		q_k = klee_mk_and(
			klee_mk_eq(_r, i),
			klee_mk_and(p_k_prev, klee_mk_eq(s[i], 0)));
		if (__klee_feasible(q_k)) {
			solutions++;
			DEBUG_PRINT("vstrlen feasible", i);
			DEBUG_PRINT("vstrlen feasible s[i]", s[i]);
			q_k_prev = klee_mk_or(q_k_prev, q_k);
		} else {
			DEBUG_PRINT("vstrlen infeasible", i);
			DEBUG_PRINT("vstrlen infeasible s[i]", s[i]);
		}

		p_k = klee_mk_and(p_k_prev, klee_mk_ne(s[i], 0));
		if (!__klee_feasible(p_k)) {
			DEBUG_PRINT("vstrlen infeasible p_k", i);
			break;
		}
		p_k_prev = p_k;

		i++;
	} while (solutions < STRLEN_MERGE_DEPTH);

	if (solutions == 0) {
		DEBUG_PRINT("vstrlen prune r", _r);
		virtsym_prune(VS_PRNID_STRLEN);
		return;
	}

	DEBUG_PRINT("vstrlen solution count", solutions);
	__klee_assume(q_k_prev);
}
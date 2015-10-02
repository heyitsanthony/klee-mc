#define GUEST_ARCH_AMD64
#include "klee/klee.h"
#include "virtsym.h"
#include <string.h>
#include "../syscall/syscalls.h"

static void strlen_enter(void* r);
static void strlen_fini(uint64_t _r, void* aux);
DECL_VIRTSYM_FAKE(strlen_fini)

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
		virtsym_str_free(vs);
		GET_SYSRET(r) = ret;
		kmc_skip_func();
		return;
	}

	/* set value to symbolic */
	if (klee_make_vsym(&ret, sizeof(ret), "vstrlen")) {
		klee_assume_ule(ret, vs->vs_len_max);
		klee_assume_uge(ret, vs->vs_first_sym_idx);
		GET_SYSRET(r) = ret;
		virtsym_add(strlen_fini, ret, vs);
		/* no need to evaluate, skip */
		kmc_skip_func();
	} else {
		virtsym_fake(strlen_fini, vs);
	}
}

#define CAN_FINISH  klee_mk_and(klee_mk_eq(_r, i), klee_mk_eq(s[i], 0))

static void strlen_fini(uint64_t _r, void* aux)
{
	const char	*s = ((char**)aux)[0];
	unsigned	i = 0;

	while (klee_feasible_ne(s[i], 0)) {
		uint64_t can_finish_cond = CAN_FINISH;
		/* note it's necessary to test feasibility of *both*
		 * predicates at once because feasible(a) /\ feasible(b)
		 * does not imply feasible(a /\ b)! */
		if (__klee_feasible(can_finish_cond)) {
			__klee_assume(can_finish_cond);
			return;
		}
		klee_assume_ne(s[i], 0);
		i++;
		if (!klee_is_valid_addr(&s[i])) {
			klee_print_expr("oops", &s[i]);
			virtsym_prune(VS_PRNID_STRLEN);
		}
	}

	if (!__klee_feasible(CAN_FINISH)) {
		virtsym_prune(VS_PRNID_STRLEN);
	} else {
		__klee_assume(CAN_FINISH);
	}
}
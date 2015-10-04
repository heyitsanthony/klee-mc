#define GUEST_ARCH_AMD64
#include "klee/klee.h"
#include "virtsym.h"
#include <string.h>
#include "../syscall/syscalls.h"


static void memchr_enter(void* r);
static void memchr_fini(uint64_t _r, void* aux);

#define HOOK_FUNC(x,y) void __hookpre_##x(void* r) { y(r); }

HOOK_FUNC(__GI_memchr, memchr_enter);
HOOK_FUNC(memchr, memchr_enter);
HOOK_FUNC(__memchr, memchr_enter);
HOOK_FUNC(__memchr_sse4_1, memchr_enter);
HOOK_FUNC(__memchr_sse2, memchr_enter);

/* XXX: rawmemchr does not have a 'count'. how to fix? */
//HOOK_FUNC(__rawmemchr_sse2, memchr_enter);
//HOOK_FUNC(__rawmemchr_sse42, memchr_enter);

struct memchr_clo
{
	struct virt_mem		*vm;
	const char		*s_orig;	// original ptr
	uint8_t			c;
	unsigned		len;
};

static void memchr_enter(void* r)
{
	uint64_t		ret;
	struct memchr_clo	clo, *clo_ret;

	clo.s_orig = (const char*)GET_ARG0(r);
	clo.c = (uint8_t)GET_ARG1(r);
	clo.len = GET_ARG2(r);

	if ((clo.vm = virtsym_safe_memcopy(clo.s_orig, clo.len)) == NULL)
		return;

	/* ignore concrete prefixes */
	if (!klee_is_symbolic(clo.c)) {
		unsigned	i;
		for (i = 0; i < clo.vm->vm_first_sym_idx; i++) {
			if (clo.vm->vm_buf[i] == clo.c) {
				virtsym_mem_free(clo.vm);
				return;
			}
		}
	}

	/* set value to symbolic */
	if (!klee_make_vsym(&ret, sizeof(ret), "vmemchr")) {
		virtsym_mem_free(clo.vm);
		virtsym_disabled();
		return;
	}

	klee_assume_uge(ret, clo.vm->vm_first_sym_idx);
	klee_assume_ule(ret, clo.vm->vm_len_max);
	GET_SYSRET(r) = klee_mk_ite(
		klee_mk_eq(ret, clo.vm->vm_len_max),
		0,
		clo.s_orig + ret);

	clo_ret = malloc(sizeof(clo));
	memcpy(clo_ret, &clo, sizeof(clo));
	virtsym_add(memchr_fini, ret, clo_ret);

	/* no need to evaluate, skip */
	kmc_skip_func();
}

static void memchr_fini(uint64_t _r, void* aux)
{
	const struct memchr_clo	*clo = aux;
	const char	*s = (const char*)clo->vm->vm_buf;
	unsigned int	i;

	/* possibly does not exist in string? */
	if (klee_feasible_eq(_r, clo->len)) {
		for (i = 0; i < clo->len; i++) {
			/* 'c' must exist in s? */
			if (klee_valid_eq(s[i], clo->c))
				break;
		}

		/* 'c' feasibly does not exist in s? */
		if (i == clo->len) {
			klee_assume_eq(_r, clo->len);
			for (i = 0; i < clo->len; i++)
				klee_assume_ne(s[i], clo->c);
			return;
		}
	}

	/* exists in string? */
	for (i = 0; i < clo->len; i++) {
		uint64_t has_char_cond = klee_mk_and(
			klee_mk_eq(s[i], clo->c), klee_mk_eq(_r, i));
		if (__klee_feasible(has_char_cond)) {
			__klee_assume(has_char_cond);
			return;
		}

		if (klee_valid_eq(s[i], clo->c)) {
			if (!klee_feasible_ne(_r, i)) {
				virtsym_prune(VS_PRNID_MEMCHR);
			}
			klee_assume_ne(_r, i);
			// check again because there may be a constraint
			// _r != i => s[i] == clo->c
			// adding _r != i makes s[i] == clo->c valid
			if (klee_valid_eq(s[i], clo->c)) {
				virtsym_prune(VS_PRNID_MEMCHR);
			}
		}

		klee_assume_ne(s[i], clo->c);
	}

	virtsym_prune(VS_PRNID_MEMCHR);
}
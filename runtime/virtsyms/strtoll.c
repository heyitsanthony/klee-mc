#define GUEST_ARCH_AMD64
#include "klee/klee.h"
#include "virtsym.h"
#include <string.h>
#include "../syscall/syscalls.h"


static void strtoll_enter(void* r);
static void strtoll_fini(uint64_t _r, void* aux);

#define HOOK_FUNC(x,y) void __hookpre_##x(void* r) { y(r); }

HOOK_FUNC(__GI____strtoll_l_internal, strtoll_enter);
HOOK_FUNC(__GI___strtoll_internal, strtoll_enter);
HOOK_FUNC(__GI_strtoll, strtoll_enter);
HOOK_FUNC(__GI_strtoul, strtoll_enter);
HOOK_FUNC(__strtoul, strtoll_enter);

struct strtoll_clo
{
	const char	*s;
	struct virt_str	*str;
	int		base;
};

static void strtoll_enter(void* r)
{
	const char		**endptr;
	struct strtoll_clo	clo_dat, *clo;
	uint64_t		ret;

	endptr = (const char**)GET_ARG1(r);
	if (endptr) {
		klee_print_expr("strtoll endptr unsupported", endptr);
		return;
	}

	/* TODO: support more bases */
	clo_dat.base = GET_ARG2(r); 
	if (GET_ARG2(r) != 10 && GET_ARG2(r) != 0) {
		klee_print_expr("unsupported base", GET_ARG2(r));
		return;
	}

	clo_dat.s = (const char*)GET_ARG0(r);
	if ((clo_dat.str = virtsym_safe_strcopy(clo_dat.s)) == NULL) {
		// concrete or had a crashy string
		return;
	}

	/* set value to symbolic */
	if (!klee_make_vsym(&ret, sizeof(ret), "vstrtoll")) {
		virtsym_disabled();
		return;
	}

	GET_SYSRET(r) = ret;

	clo = malloc(sizeof(*clo));
	memcpy(clo, &clo_dat, sizeof(*clo));
	virtsym_add(strtoll_fini, ret, clo);

	/* no need to evaluate, skip */
	kmc_skip_func();
}

/* I recall something like this being a msft question */
static void strtoll_itoa(char* buf, uint64_t v)
{
	int	i, j;

	/* negate if necessary */
	if (v & (1UL << 63)) {
		buf[0] = '-';
		buf++;
		v = -v;
	}

	/* write number out backwards */
	i = 0;
	do {
		buf[i++] = '0' + (v % 10);
		v /= 10;
	} while (v);
	buf[i] = '\0';

	/* reverse */
	for (j = 0; j < i / 2; j++) {
		char	c = buf[j];
		buf[j] = buf[(i-1)-j];
		buf[(i-1)-j] = c;
		klee_print_expr("HEY", c);
		klee_assert (klee_is_symbolic(c) || c != '\0');
	}
}

static void strtoll_fini(uint64_t _r, void* aux)
{
	/* concretize, then compute */
	uint64_t		r;
	struct strtoll_clo	*clo;
	const char		*s;
	int			i;
	char			buf[64];
	uint64_t		pred;

	r = klee_get_value(_r);
	klee_assume_eq(r, _r);

	clo = aux;
	s = clo->str->vs_str;

	/* write string into 'buf'; itoa. assume base=10 */
	strtoll_itoa(buf, r);

	for (i = 0; buf[i] && klee_feasible_eq(s[i], buf[i]); i++)
		klee_assume_eq(s[i], buf[i]);

	/* didn't feasibily match expected string? */
	if (buf[i]) virtsym_prune(VS_PRNID_STRTOLL);

	/* force non-numeric follow up */
	pred = klee_mk_or(klee_mk_ult(s[i], '0'), klee_mk_ugt(s[i], '9'));
	if (!klee_feasible(pred)) virtsym_prune(VS_PRNID_STRTOLL);
}
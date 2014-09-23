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

struct strtoll_clo
{
	const char	*nptr;
	int		base;
	unsigned	len;


	const char	*endptr;	/* symbolic data */
};

static void strtoll_enter(void* r)
{
	const char		*s;
	uint64_t		ret;
	struct strtoll_clo	*clo_dat;
	char			**endptr;
	unsigned		i;


	s = (const char*)GET_ARG0(r);
	endptr = (char**)GET_ARG1(r);

	klee_print_expr("hello strtoll", s);

	/* 1. check pointers, common to crash in strtoll */
	if (!klee_is_valid_addr(s)) return;

	/* ignore concretes */
	if (!klee_is_symbolic(s[0])) return;

	/* TODO: support more bases */
	if (GET_ARG2(r) != 10 && GET_ARG2(r) != 0) {
		klee_print_expr("unsupported base", GET_ARG2(r));
		return;
	}

	i = 0;
	while (	klee_is_valid_addr(&s[i]) &&
		(klee_is_symbolic(s[i]) || s[i] != '\0')) i++;

	klee_print_expr("all enumerated", i);

	/* set value to symbolic */
	if (!klee_make_vsym(&ret, sizeof(ret), "vstrtoll")) {
		virtsym_disabled();
		return;
	}

	GET_SYSRET(r) = ret;

	/* XXX: this should copy the whole string */
	clo_dat = malloc(sizeof(*clo_dat));
	clo_dat->nptr = s;
	clo_dat->base = GET_ARG2(r);
	if (endptr != NULL) *endptr = (char*)&s[i];
	clo_dat->len = i;
	virtsym_add(strtoll_fini, ret, clo_dat);

	/* no need to evaluate, skip */
	kmc_skip_func();
}

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
	for (i = 0; v; i++) {
		buf[i] = '0' + (v % 10);
		v /= 10;
		i++;
	}
	buf[i] = '\0';

	/* reverse */
	for (j = 0; j < i / 2; j++) {
		char	c = buf[j];
		buf[j] = buf[(i-1)-j];
		buf[(i-1)-j] = c;
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
	s = clo->nptr;

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
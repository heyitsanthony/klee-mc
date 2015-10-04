#include "klee/klee.h"
#include <string.h>
#include "virtsym.h"

struct vsym_clo*	vs = NULL;

void virtsym_add(vsym_clo_f f, uint64_t ret, void* dat)
{
	struct vsym_clo	*cur_vs;

	cur_vs = malloc(sizeof(*vs));
	cur_vs->vs_next = vs;
	cur_vs->vs_f = f;
	cur_vs->vs_ret = ret;
	cur_vs->vs_aux = dat;

	vs = cur_vs;
}

int virtsym_already(	vsym_clo_f clo_f, vsym_check_f chk_f,
			const void *aux, uint64_t *ret)
{
	struct vsym_clo	*cur_vs = vs;
	while (cur_vs) {
		if (vs->vs_f == clo_f) {
			if (chk_f(aux, vs->vs_aux)) {
				*ret = vs->vs_ret;
				return 1;
			}
		}
		cur_vs = cur_vs->vs_next;
	}

	return 0;
}

/* do all virtsym closures */
void __hookfini_virtsym(void)
{
	unsigned i = 0;
	while (vs) {
		vs->vs_f(vs->vs_ret, vs->vs_aux);
		vs = vs->vs_next;
		i++;
	}
	klee_print_expr("vsym complete", i);
}

void virtsym_prune(int n)
{
	klee_print_expr("vsym pruning", n);
	klee_silent_exit(0);
}

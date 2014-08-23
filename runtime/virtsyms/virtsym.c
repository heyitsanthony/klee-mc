#include "klee/klee.h"
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

/* do all virtsym closures */
void __hookfini_virtsym(void)
{
	while (vs) {
		vs->vs_f(vs->vs_ret, vs->vs_aux);
		vs = vs->vs_next;
	}
}

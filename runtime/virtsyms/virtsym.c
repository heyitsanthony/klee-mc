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

/* XXX: this can be smarter in a few ways... */
char* virtsym_safe_strcopy(const char* s)
{
	char*	ret;
	int	i, j;

	for (i = 0; !klee_is_symbolic(s[i]) && s[i] != '\0'; i++);

	if (klee_is_symbolic(s[i])) {
		i = 127;
	} else if (s[i] != '\0') {
		/* ruhroh -- huge string */
		klee_assert(s[i] == '\0');
	}

	ret = malloc(i+1); /* XXX be smarter */
	for (j = 0; j <= i && klee_is_valid_addr(&s[j]); j++)
		ret[j] = s[j];

	return ret;
}



/* do all virtsym closures */
void __hookfini_virtsym(void)
{
	while (vs) {
		vs->vs_f(vs->vs_ret, vs->vs_aux);
		vs = vs->vs_next;
	}
}

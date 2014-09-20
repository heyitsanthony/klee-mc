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
void* virtsym_safe_memcopy(const void* m, unsigned len)
{
	void	*ret;
	int	i;

	/* concretize because we're dum */
	klee_assume_eq(m, klee_get_value(m));
	klee_assume_eq(len, klee_max_value(len));

	/* XXX: in the future this should use the to-be-written
	 * klee_copy_cow instrinsic so there's no space/copying overhead */
	ret = malloc(len);
	for (i = 0; i < len; i++)
		((char*)ret)[i] = ((const char*)m)[i];

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

void virtsym_prune(int n)
{
	klee_print_expr("vsym pruning", n);
	klee_silent_exit(0);
}

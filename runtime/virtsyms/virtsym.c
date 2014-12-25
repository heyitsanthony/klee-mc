#include "klee/klee.h"
#include "virtsym.h"
#include <string.h>

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

void virtsym_str_free(struct virt_str *vs)
{
	free(vs->vs_str);
	free(vs);
}

struct virt_str* virtsym_safe_strcopy_all(const char* s, int copy_concrete)
{
	struct virt_str	vs, *ret;
	char		*s_c;
	unsigned	i, j;

	if (!klee_is_valid_addr(s)) {
		return NULL;
	}

	s_c = klee_get_ptr(s);
	klee_assume_eq(s, s_c);
	s = s_c;
	klee_assert(!klee_is_symbolic_addr(s) && "Smarter way to do this?");

	for (i = 0;
		klee_is_valid_addr(&s[i]) &&
		!klee_is_symbolic(s[i]) &&
		s[i] != '\0'; i++);

	if (!klee_is_valid_addr(&s[i]) || !klee_is_symbolic(s[i])) {
		/* total concrete string */
		klee_assert(!klee_is_valid_addr(&s[i]) || s[i] == '\0');
		if (!copy_concrete) {
			return NULL;
		}

		vs.vs_len_min = i;
		vs.vs_first_sym_idx = ~0U;
		vs.vs_len_max = i; 
		vs.vs_str = malloc(vs.vs_len_max+1);
		for (j = 0; j < vs.vs_len_max; j++) vs.vs_str[j] = s[j];
		// don't crash in handler!
		vs.vs_str[vs.vs_len_max] = '\0';

		ret = malloc(sizeof(vs));
		memcpy(ret, &vs, sizeof(vs));
		return ret;
	}

	vs.vs_len_min = i;
	vs.vs_first_sym_idx = i;
	klee_assert(klee_is_symbolic(s[vs.vs_first_sym_idx]));

	for (i = vs.vs_len_min; klee_is_valid_addr(&s[i]); i++) {
		if (!klee_is_symbolic(s[i]) && s[i] == '\0')
			break;
	}
	vs.vs_len_max = i; 

	vs.vs_str = malloc(vs.vs_len_max+1);
	for (j = 0; j < vs.vs_len_max; j++)
		vs.vs_str[j] = s[j];

	// don't crash in handler!
	vs.vs_str[vs.vs_len_max] = '\0';

	ret = malloc(sizeof(vs));
	memcpy(ret, &vs, sizeof(vs));
	return ret;
}

void* virtsym_safe_memcopy(const void* m, unsigned len)
{
	void	*ret;
	unsigned i;

	/* concretize because we're dum  (XXX smarter way to do this?) */
	klee_assume_eq((uint64_t)m, klee_get_value((uint64_t)m));
	klee_assume_eq(len, klee_max_value(len));
	if (!klee_is_valid_addr(m)) return NULL;

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

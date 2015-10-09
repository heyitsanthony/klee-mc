#include "klee/klee.h"
#include <string.h>
#include "virtstr.h"

void virtsym_str_free(struct virt_str *vs)
{
	free(vs->vs_str);
	free(vs);
}

int virtsym_str_eq(const struct virt_str *vs0, const struct virt_str *vs1)
{
	return	vs0->vs_first_sym_idx == vs1->vs_first_sym_idx &&
		vs0->vs_len_min == vs1->vs_len_min &&
		vs0->vs_len_max == vs1->vs_len_max &&
		vs0->vs_str_min == vs1->vs_str_min &&
		vs0->vs_str_max == vs1->vs_str_max &&
		virtsym_str_hash(vs0) == virtsym_str_hash(vs1);
}

uint64_t virtsym_str_hash(const struct virt_str* vs)
{
	uint64_t	h = 0;
	unsigned	i;
	for (i = 0; i < vs->vs_len_max; i++) {
		h += (h ^ ((i + 1) * vs->vs_str[i]));
	}
	return klee_expr_hash(h);
}

struct virt_str* virtsym_safe_strcopy_all(const char* s, int copy_concrete)
{
	struct virt_str	vs;
	char		*s_c;
	unsigned	i, j;

	if (klee_is_symbolic_addr(s)) {
		// I'm seeing code that does strlen(s + strlen(s)). Concretizing
		// here ruins everything, it seems.
		return NULL;
	}

	if (!klee_is_valid_addr(s)) {
		return NULL;
	}

	s_c = klee_get_ptr(s);
	klee_assume_eq(s, s_c);
	s = s_c;
	klee_assert(!klee_is_symbolic_addr(s) && "Smarter way to do this?");

	/* skip over concrete prefix */
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

		return memcpy(malloc(sizeof(vs)), &vs, sizeof(vs));
	}

	vs.vs_len_min = i;
	vs.vs_first_sym_idx = i;
	klee_assert(klee_is_symbolic(s[vs.vs_first_sym_idx]));

	vs.vs_len_max = vs.vs_len_min;
	for (i = vs.vs_len_min; klee_is_valid_addr(&s[i]); i++) {
		vs.vs_len_max = i;
		if (!klee_is_symbolic(s[i]) && s[i] == '\0') {
			break;
		}
	}

	vs.vs_str = malloc(vs.vs_len_max+1);
	for (j = 0; j < vs.vs_len_max; j++)
		vs.vs_str[j] = s[j];

	// don't crash in handler!
	vs.vs_str[vs.vs_len_max] = '\0';

	return memcpy(malloc(sizeof(vs)), &vs, sizeof(vs));
}
#ifndef VIRTSTR_H
#define VIRTSTR_H

#define vs_is_conc(x)	((x)->vs_first_sym_idx == ~0U)
struct virt_str
{
	char		*vs_str; // buffer containing string's contents
	unsigned	vs_first_sym_idx; // must have some symbolic!
	unsigned	vs_len_min, vs_len_max; // lengths of stream
	char		*vs_str_min, *vs_str_max; // pointers into vs_str
};
#define virtstr_dump(vs)						\
	klee_print_expr("VS->VS_LEN_MAX", vs->vs_len_max);		\
	klee_print_expr("VS->VS_LEN_MIN", vs->vs_len_min);		\
	klee_print_expr("VS->VS_FIRST_SYM_IDX", vs->vs_first_sym_idx);	\

void virtsym_str_free(struct virt_str*);
int virtsym_str_eq(const struct virt_str*, const struct virt_str*);
uint64_t virtsym_str_hash(const struct virt_str*);
// copy only if string has symbolic characters
#define virtsym_safe_strcopy(s) virtsym_safe_strcopy_all(s,0)
// copy if string regardless of content, if possible
#define virtsym_safe_strcopy_conc(s) virtsym_safe_strcopy_all(s,1)
struct virt_str* virtsym_safe_strcopy_all(const char* s, int copy_conc);

#endif

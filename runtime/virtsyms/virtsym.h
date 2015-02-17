#ifndef VIRTSYM_H
#define VIRTSYM_H

typedef void(*vsym_clo_f)(uint64_t r, void* aux);

typedef int pruneid_t;
#define VS_PRNID_STRCHR		1
#define VS_PRNID_STRCMP		2
#define VS_PRNID_STRLEN		3
#define VS_PRNID_STRTOLL	4
#define VS_PRNID_MEMCHR		5
#define VS_PRNID_MEMCMP		6

/* closure */
struct vsym_clo
{
	uint64_t	vs_ret;
	vsym_clo_f	vs_f;
	void		*vs_aux;
	struct vsym_clo	*vs_next;
};

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



void virtsym_add(vsym_clo_f f, uint64_t ret, void* dat);
// copy only if string has symbolic characters
#define virtsym_safe_strcopy(s) virtsym_safe_strcopy_all(s,0)
// copy if string regardless of content, if possible
#define virtsym_safe_strcopy_conc(s) virtsym_safe_strcopy_all(s,1)
struct virt_str* virtsym_safe_strcopy_all(const char* s, int copy_conc);
void virtsym_str_free(struct virt_str*);
void* virtsym_safe_memcopy(const void* m, unsigned len);
void virtsym_prune(pruneid_t);

/* in the future, this might do something smart like install a post hook
 * so that nothing forks so there's no state pollution */
#define virtsym_disabled() do { } while (0)
#define DECL_VIRTSYM_FAKE(f) static void fake_##f(void* c)	\
	{ virtsym_add(f, GET_RET(kmc_regs_get()), c); }
#define virtsym_fake_n(f, dat, n) do {		\
	klee_print_expr("faking func " #f , 123);	\
	klee_hook_return(n, fake_##f, (uint64_t)(dat)); } while (0)
#define virtsym_fake(f, dat) virtsym_fake_n(f, dat, 1)

// void virtsym_disabled(void);
//
#endif

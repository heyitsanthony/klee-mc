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

struct virt_str
{
	char		*vs_str;
	unsigned	vs_first_sym_idx; // must have some symbolic!
	unsigned	vs_len_min, vs_len_max; // lengths of stream
	char		*vs_str_min, *vs_str_max; // pointers into vs_str
};

void virtsym_add(vsym_clo_f f, uint64_t ret, void* dat);
struct virt_str* virtsym_safe_strcopy(const char* s);
void virtsym_str_free(struct virt_str*);
void* virtsym_safe_memcopy(const void* m, unsigned len);
void virtsym_prune(pruneid_t);

/* in the future, this might do something smart like install a post hook
 * so that nothing forks so there's no state pollution */
#define virtsym_disabled() do { } while (0)
// void virtsym_disabled(void);

#endif

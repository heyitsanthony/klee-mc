#ifndef VIRTSYM_H
#define VIRTSYM_H

typedef void(*vsym_clo_f)(uint64_t r, void* aux);

/* closure */
struct vsym_clo
{
	uint64_t	vs_ret;
	vsym_clo_f	vs_f;
	void		*vs_aux;
	struct vsym_clo	*vs_next;
};


void virtsym_add(vsym_clo_f f, uint64_t ret, void* dat);
char* virtsym_safe_strcopy(const char* s);

#endif

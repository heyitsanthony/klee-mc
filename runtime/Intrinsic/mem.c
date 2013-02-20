#include "klee/klee.h"
#include <stdlib.h>
#include <string.h>

void* malloc(size_t sz)
{
	// volatile so that == replacement doesn't screw things up
	volatile size_t	cur_sz;

	if (!klee_is_symbolic(sz))
		return klee_malloc_fixed(sz);

	cur_sz = klee_min_value(sz);
	if (sz == cur_sz) {
		if (cur_sz == 0)
			return NULL;
		return klee_malloc_fixed(cur_sz);
	}

	if (sz >= (1ULL << 31)) {
		klee_print_expr("NOTE: found huge malloc", sz);
		return NULL;
	}

	cur_sz = klee_get_value(sz);
	if (cur_sz == sz)
		return klee_malloc_fixed(cur_sz);

	klee_uerror("too many mallocs", "model.err");
}

void* calloc(size_t nmemb, size_t size)
{
	void	*ret;
	ret = malloc(nmemb * size);
	if (ret == NULL) return NULL;

	memset(ret, 0, nmemb*size);
	return ret;
}

void free(void* f)
{
	uint64_t	addr = klee_fork_all(f);
	if (addr == 0) return;
	klee_free_fixed(addr);
}

static void realloc_obj_to(void* cur_obj_base, void* new_ptr, unsigned size)
{
	unsigned	obj_sz;
	unsigned	copy_bytes;

	obj_sz = klee_get_obj_size(cur_obj_base);
	copy_bytes = (obj_sz < size) ? obj_sz : size;
	memcpy(new_ptr, cur_obj_base, copy_bytes);

	free(cur_obj_base);
}

void* realloc(void* ptr, size_t size)
{
	void	*new_ptr;
	void	*cur_obj_base;

	if (size == 0) {
		free(ptr);
		return NULL;
	}

	if (ptr == NULL) return malloc(size);

	new_ptr = malloc(size);
	if (new_ptr == NULL)
		return NULL;

	cur_obj_base = klee_get_obj(ptr);
	if (cur_obj_base == NULL) {
		klee_uerror("Realloc pointer matched no object", "ptr.err");
		return NULL;
	}


	realloc_obj_to(cur_obj_base, new_ptr, size);
	return new_ptr;
}

// operator delete[](void*)
void _ZdaPv(void* p) { free(p); }
// operator delete(void*)
void _ZdlPv(void* p) { free(p); }
 // operator new[](unsigned int)
void* _Znaj(unsigned int n) { return malloc(n); }
// operator new(unsigned int)
void* _Znwj(unsigned int n) { return malloc(n); }
// operator new[](unsigned long)
void* _Znam(unsigned long n) { return malloc(n); }
// operator new(unsigned long)
void* _Znwm(unsigned long n) { return malloc(n); }

void* klee_get_obj(void* ptr)
{
	void		*new_ptr, *new_base;
	unsigned	new_sz;

	if (!klee_is_symbolic((uintptr_t)ptr))
		return ptr;

	new_ptr = (void*)klee_fork_all(ptr);
	new_base = klee_get_obj_prev(new_ptr);
	if (klee_fork_eq(new_base, NULL))
		return NULL;

	new_sz = klee_get_obj_size(new_base);
	if (((uintptr_t)new_base + new_sz) < (uintptr_t)new_ptr)
		return NULL;

	return new_base;
}

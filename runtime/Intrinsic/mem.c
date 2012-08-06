#include "klee/klee.h"
#include <stdlib.h>
#include <string.h>

void* calloc(size_t nmemb, size_t size)
{
	void	*ret;
	ret = malloc(nmemb * size);
	if (ret == NULL) return NULL;

	memset(ret, 0, nmemb*size);
	return ret;
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

#if 0
void* klee_get_obj(void* ptr)
{
	uintptr_t	ptr_ui;
	uintptr_t	min_addr, max_addr;

	if (!klee_is_symbolic((uintptr_t)ptr))
		return ptr;


	min_addr = (uintptr_t)klee_get_obj_next(NULL);
	max_addr = (uintptr_t)klee_get_obj_prev((void*)~0);
	ptr_ui = (uintptr_t)ptr;

	while (min_addr < max_addr) {
		uintptr_t	mid;
		void		*cur_obj;

		min_addr = klee_fork_all(min_addr);
		max_addr = klee_fork_all(max_addr);

		klee_assert (!klee_is_symbolic(min_addr));
		klee_assert (!klee_is_symbolic(max_addr));

		klee_print_expr("new max", max_addr);
		klee_print_expr("new min", min_addr);

		mid = min_addr + (max_addr - min_addr)/2;
		klee_print_expr("mid point", mid);
		cur_obj = klee_get_obj_prev((void*)mid);

		if (klee_fork_eq(cur_obj, ptr))
			return cur_obj;

		if (ptr_ui < mid) {
			max_addr = (uintptr_t)mid;
		} else if (ptr_ui > mid) {
			min_addr = (uintptr_t)mid+1;
		}
	}

	klee_print_expr("last max", max_addr);
	klee_print_expr("last min", min_addr);

	return NULL;
}
#endif

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

	klee_print_expr("NEW BASE", new_base);
	return new_base;
}
